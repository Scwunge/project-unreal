"""Instances and the process manager, against a fake console instead of java."""

import json
import socket
import tempfile
import unittest
from pathlib import Path

import fakes
from mchost import EulaNotAccepted, HostError, Instance, Server, WorldOptions, offline_uuid
from mchost.server import free_port


class InstanceTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def test_create_writes_offline_local_properties(self):
        inst = Instance.create(self.root, "dev", WorldOptions(seed="-42", game_mode="creative", port=25570))
        p = inst.properties()
        self.assertEqual(p["online-mode"], "false")
        self.assertEqual(p["server-ip"], "127.0.0.1")
        self.assertEqual(p["server-port"], "25570")
        self.assertEqual(p["level-seed"], "-42")
        self.assertEqual(p["gamemode"], "creative")
        self.assertEqual(p["level-type"], "minecraft:normal")
        self.assertEqual(p["white-list"], "false")
        self.assertEqual(p["management-server-enabled"], "true")
        self.assertEqual(p["management-server-tls-enabled"], "false")
        self.assertRegex(p["management-server-secret"], r"^[A-Za-z0-9]{40}$")
        self.assertTrue(inst.exists())
        self.assertEqual([i.name for i in Instance.list(self.root)], ["dev"])

    def test_eula_is_never_written_unless_asked(self):
        inst = Instance.create(self.root, "dev")
        self.assertFalse((inst.path / "eula.txt").exists())
        self.assertFalse(inst.eula_accepted())
        inst.accept_eula()
        self.assertTrue(inst.eula_accepted())
        self.assertTrue(Instance.create(self.root, "two", accept_eula=True).eula_accepted())

    def test_bad_names_and_options_are_refused(self):
        for name in ["", ".hidden", "a/b", "..", "x" * 65, "spa ce"]:
            with self.assertRaises(HostError, msg=name):
                Instance.create(self.root, name)
        with self.assertRaises(HostError):
            Instance.create(self.root, "x", WorldOptions(difficulty="nightmare"))
        Instance.create(self.root, "dup")
        with self.assertRaises(HostError):
            Instance.create(self.root, "dup")

    def test_offline_allowlist_and_ops_files_use_offline_uuids(self):
        inst = Instance.create(self.root, "dev")
        self.assertEqual(inst.allowlist_add("Alex", "Bob", "alex"), ["Alex", "Bob"])
        entries = json.loads((inst.path / "whitelist.json").read_text())
        self.assertEqual(entries[0], {"uuid": str(offline_uuid("Alex")), "name": "Alex"})
        self.assertEqual(inst.allowlist_remove("BOB"), ["Alex"])
        self.assertEqual(inst.ops_add("Alex", level=2), ["Alex"])
        self.assertEqual(inst.operators()[0]["level"], 2)
        self.assertEqual(inst.ops_add("Alex", level=4), ["Alex"])
        self.assertEqual(inst.operators()[0]["level"], 4)
        self.assertEqual(inst.ops_remove("alex"), [])


class ServerProcessTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.inst = Instance.create(self.root, "dev", WorldOptions(port=free_port()), accept_eula=True)
        # The fake console has no management server to connect to.
        from mchost import properties
        properties.update(self.inst.properties_path, {"management-server-enabled": "false"})

    def tearDown(self):
        self.tmp.cleanup()

    def server(self, mode="normal"):
        return Server(self.inst, command=fakes.fake_console_command(self.root, mode))

    def test_start_ready_command_stop(self):
        srv = self.server()
        lines = []
        srv.on_console(lines.append)
        srv.start()
        srv.wait_ready(timeout=30)
        self.assertEqual(srv.startup_seconds, 1.234)
        self.assertTrue(srv.running)
        self.assertIsNotNone(self.inst.running_pid())
        srv.send_command("say hi")
        self.assertEqual(srv.stop(timeout=30), 0)
        self.assertIn("[00:00:03] [Server thread/INFO]: got say hi", lines)
        self.assertIn("[00:00:03] [Server thread/INFO]: got stop", lines)
        self.assertIsNone(self.inst.meta()["pid"])
        self.assertEqual(self.inst.meta()["exit_code"], 0)

    def test_crash_before_ready_reports_the_console(self):
        srv = self.server("crash")
        srv.start()
        with self.assertRaises(HostError) as cm:
            srv.wait_ready(timeout=30)
        self.assertIn("Failed to start the minecraft server", str(cm.exception))
        self.assertIn("code 3", str(cm.exception))

    def test_stop_kills_a_server_that_ignores_stop(self):
        srv = self.server("stubborn")
        srv.start()
        srv.wait_ready(timeout=30)
        code = srv.stop(timeout=1)
        self.assertNotEqual(code, 0)
        self.assertFalse(srv.running)

    def test_refuses_without_eula(self):
        (self.inst.path / "eula.txt").unlink()
        with self.assertRaises(EulaNotAccepted):
            self.server().start()

    def test_refuses_when_game_port_is_taken(self):
        port = int(self.inst.properties()["server-port"])
        with socket.socket() as s:
            s.bind(("127.0.0.1", port))
            s.listen()
            with self.assertRaises(HostError) as cm:
                self.server().start()
            self.assertIn(str(port), str(cm.exception))

    def test_offline_allowlist_while_running_reloads(self):
        srv = self.server()
        lines = []
        srv.on_console(lines.append)
        srv.start()
        srv.wait_ready(timeout=30)
        try:
            self.assertEqual(srv.allowlist_add("Alex"), ["Alex"])
        finally:
            srv.stop(timeout=30)
        self.assertIn("[00:00:03] [Server thread/INFO]: got whitelist reload", lines)

    def test_management_port_is_fresh_each_start(self):
        from mchost import properties
        properties.update(self.inst.properties_path, {"management-server-enabled": "true"})
        srv = self.server()
        srv.start()
        try:
            srv.wait_ready(timeout=30, connect=False)
            self.assertNotEqual(self.inst.properties()["management-server-port"], "0")
        finally:
            srv.stop(timeout=30)


if __name__ == "__main__":
    unittest.main()
