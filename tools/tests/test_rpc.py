import socket
import threading
import time
import unittest
import uuid

import fakes
from fakes import FakeManagementServer, FakeRpcError
from mchost import websocket as ws
from mchost.rpc import ManagementClient, RpcError, Schema, offline_uuid
from mchost.server import load_schema

SCHEMA = Schema({"info": {"version": "test"}, "methods": [
    {"name": "minecraft:server/status", "params": []},
    {"name": "minecraft:server/save", "params": [{"name": "flush", "required": True}]},
    {"name": "minecraft:allowlist/add", "params": [{"name": "add", "required": True}]},
    {"name": "minecraft:notification/server/started", "params": []},
]})


class WebSocketFrameTest(unittest.TestCase):
    def test_accept_key_matches_rfc_example(self):
        self.assertEqual(ws.accept_key("dGhlIHNhbXBsZSBub25jZQ=="), "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=")

    def test_frames_of_every_length_form_round_trip(self):
        for n in (0, 5, 125, 126, 1000, 65535, 65536, 200_000):
            for mask in (False, True):
                a, b = socket.socketpair()
                try:
                    payload = bytes(i % 251 for i in range(n))
                    sender = threading.Thread(target=a.sendall, args=(ws.encode_frame(ws.OP_TEXT, payload, mask),))
                    sender.start()
                    fin, op, got = ws.read_frame(b)
                    sender.join()
                    self.assertEqual((fin, op, got), (True, ws.OP_TEXT, payload), (n, mask))
                finally:
                    a.close()
                    b.close()

    def test_fragments_and_pings_are_handled(self):
        a, b = socket.socketpair()
        try:
            a.sendall(ws.encode_frame(ws.OP_TEXT, b"hel", False, fin=False)
                      + ws.encode_frame(ws.OP_PING, b"p", False)
                      + ws.encode_frame(ws.OP_CONT, "lo ✓".encode(), False))
            client = ws.WebSocket(b)
            self.assertEqual(client.recv_text(), "hello ✓")
            fin, op, payload = ws.read_frame(a)
            self.assertEqual((op, payload), (ws.OP_PONG, b"p"))
        finally:
            a.close()
            b.close()

    def test_close_frame_raises_with_code(self):
        a, b = socket.socketpair()
        try:
            a.sendall(ws.encode_frame(ws.OP_CLOSE, b"\x03\xe8bye", False))
            with self.assertRaises(ws.ConnectionClosed) as cm:
                ws.WebSocket(b).recv_text()
            self.assertEqual((cm.exception.code, cm.exception.reason), (1000, "bye"))
        finally:
            a.close()
            b.close()


class ManagementClientTest(unittest.TestCase):
    def setUp(self):
        self.server = FakeManagementServer("s" * 40, {
            "minecraft:server/status": lambda p: {"started": True, "version": {"name": "26.3", "protocol": 777}},
            "minecraft:server/save": lambda p: p == [True],
            "minecraft:allowlist/add": lambda p: p[0],
            "minecraft:server/stop": lambda p: (_ for _ in ()).throw(FakeRpcError(-32603, "boom")),
        })

    def tearDown(self):
        self.server.close()

    def connect(self, schema=SCHEMA):
        return ManagementClient.connect("127.0.0.1", self.server.port, "s" * 40, schema)

    def test_call_sends_bearer_secret_and_returns_result(self):
        c = self.connect()
        try:
            self.assertEqual(c.status()["version"]["protocol"], 777)
            self.assertEqual(self.server.auth_headers, ["Bearer " + "s" * 40])
            self.assertEqual(self.server.requests[0]["jsonrpc"], "2.0")
            self.assertNotIn("params", self.server.requests[0])
        finally:
            c.close()

    def test_keyword_params_are_bound_by_the_schema(self):
        c = self.connect()
        try:
            self.assertTrue(c.call("minecraft:server/save", flush=True))
            self.assertEqual(self.server.requests[-1]["params"], [True])
            with self.assertRaises(TypeError):
                c.call("minecraft:server/save")
            with self.assertRaises(TypeError):
                c.call("minecraft:server/save", nope=1)
            with self.assertRaises(ValueError):
                c.call("minecraft:not/a/method")
            with self.assertRaises(ValueError):
                c.call("minecraft:notification/server/started")
        finally:
            c.close()

    def test_errors_become_rpc_errors(self):
        c = self.connect(schema=None)
        try:
            with self.assertRaises(RpcError) as cm:
                c.call("minecraft:server/stop")
            self.assertEqual(cm.exception.code, -32603)
            with self.assertRaises(RpcError) as cm:
                c.call("minecraft:unknown")
            self.assertEqual(cm.exception.code, -32601)
        finally:
            c.close()

    def test_wrong_secret_is_refused(self):
        with self.assertRaises(ws.WebSocketError):
            ManagementClient.connect("127.0.0.1", self.server.port, "x" * 40)

    def test_notifications_reach_listeners_and_wait_for(self):
        c = self.connect()
        try:
            seen = []
            c.on_notification(lambda m, p: seen.append((m, p)))
            c.status()  # make sure the server has registered the client
            self.server.push("minecraft:notification/players/joined", [{"name": "Alex"}])
            self.assertEqual(c.wait_for("players/joined", timeout=5), [{"name": "Alex"}])
            self.assertEqual(seen, [("minecraft:notification/players/joined", [{"name": "Alex"}])])
            with self.assertRaises(TimeoutError):
                c.wait_for("server/stopping", timeout=0.3)
        finally:
            c.close()

    def test_concurrent_calls_get_their_own_replies(self):
        c = self.connect()
        results = {}
        try:
            def worker(i):
                results[i] = c.call("minecraft:allowlist/add", [{"name": f"p{i}"}])
            threads = [threading.Thread(target=worker, args=(i,)) for i in range(20)]
            for t in threads:
                t.start()
            for t in threads:
                t.join()
            self.assertEqual(results, {i: [{"name": f"p{i}"}] for i in range(20)})
        finally:
            c.close()

    def test_pending_call_fails_when_connection_drops(self):
        self.server.handlers["minecraft:server/status"] = lambda p: time.sleep(10)
        c = self.connect()
        threading.Timer(0.3, self.server.close).start()
        try:
            with self.assertRaises(ConnectionError):
                c.call("minecraft:server/status", timeout=5)
        finally:
            c.close()


class SchemaTest(unittest.TestCase):
    def test_real_schema_loads_if_cached(self):
        schema = load_schema()
        if schema is None:
            self.skipTest("cache/data/reports/json-rpc-api-schema.json not generated")
        self.assertIn("minecraft:server/status", schema.calls())
        self.assertIn("minecraft:notification/server/started", schema.notifications())
        self.assertEqual(schema.bind("minecraft:server/save", (), {"flush": False}), [False])


class OfflineUuidTest(unittest.TestCase):
    def test_known_values(self):
        # Notch's well-known offline UUID, and the ones the real 26.3 server gave these names at login.
        self.assertEqual(offline_uuid("Notch"), uuid.UUID("b50ad385-829d-3141-a216-7e7d7539ba7f"))
        self.assertEqual(offline_uuid("Bob"), uuid.UUID("faa5dca3-c3d4-354b-ae1b-dde9e5a14b3b"))
        self.assertEqual(offline_uuid("Alex").version, 3)


if __name__ == "__main__":
    unittest.main()
