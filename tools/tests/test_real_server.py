"""End to end against the real 26.3 server.jar. Slow (about a minute), so it only runs when asked:

  set MC_HOST_REAL=1 && python -m unittest discover -s tools/tests

It writes eula=true into a temporary instance, so only run it if you accept https://aka.ms/MinecraftEULA.
"""

import json
import os
import tempfile
import unittest
from pathlib import Path

import fakes
from mchost import Instance, Server, WorldOptions, find_java, offline_uuid
from mchost.server import SERVER_JAR, free_port

REAL = os.environ.get("MC_HOST_REAL") == "1"


@unittest.skipUnless(REAL, "set MC_HOST_REAL=1 to start a real server")
class RealServerTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not SERVER_JAR.is_file():
            raise unittest.SkipTest("no cache/server/server.jar; run tools/fetch_cache.py")
        cls.tmp = tempfile.TemporaryDirectory()
        cls.port = free_port()
        cls.inst = Instance.create(Path(cls.tmp.name), "e2e",
                                   WorldOptions(seed="8675309", game_mode="creative", port=cls.port,
                                                allowlist=True), accept_eula=True)
        cls.inst.allowlist_add("Alex")
        cls.inst.ops_add("Alex", level=2)
        # Share the unpacked libraries with server/ so repeat runs don't unpack them again.
        cls.server = Server(cls.inst, java=find_java(), libraries=SERVER_JAR.parents[2] / "server" / ".libraries")
        cls.server.start()
        cls.server.wait_ready(timeout=300)

    @classmethod
    def tearDownClass(cls):
        try:
            cls.code = cls.server.stop(timeout=120)
        finally:
            cls.tmp.cleanup()

    def test_status_reports_26_3(self):
        status = self.server.rpc.status()
        self.assertTrue(status["started"])
        self.assertEqual(status["version"], {"name": "26.3", "protocol": 777})

    def test_properties_reached_the_server(self):
        rpc = self.server.rpc
        self.assertEqual(rpc.setting("game_mode"), "creative")
        self.assertEqual(rpc.setting("difficulty"), "normal")
        self.assertTrue(rpc.setting("use_allowlist"))

    def test_settings_and_gamerules_change_live(self):
        rpc = self.server.rpc
        self.assertEqual(rpc.set_setting("difficulty", "hard"), "hard")
        self.assertEqual(rpc.set_gamerule("minecraft:keep_inventory", True)["value"], True)
        # Notification params come positionally, like call params.
        self.assertEqual(rpc.wait_for("gamerules/updated", timeout=10)[0]["key"], "minecraft:keep_inventory")
        self.assertTrue(rpc.say("hello from the host test"))

    def test_offline_allowlist_and_ops_from_files(self):
        alex = str(offline_uuid("Alex"))
        self.assertIn({"id": alex, "name": "Alex"}, self.server.rpc.allowlist())
        self.assertEqual(self.server.rpc.operators()[0]["player"]["id"], alex)
        self.assertEqual(fakes.offline_login(self.port, "Alex", 777), ("finished", alex))
        state, reason = fakes.offline_login(self.port, "Bob", 777)
        self.assertEqual(state, "disconnect")
        self.assertIn("not_whitelisted", reason)

    def test_offline_allowlist_add_while_running(self):
        self.server.allowlist_add("Cara")
        self.assertIn({"id": str(offline_uuid("Cara")), "name": "Cara"}, self.server.rpc.allowlist())
        self.assertEqual(fakes.offline_login(self.port, "Cara", 777)[0], "finished")

    def test_save(self):
        self.assertTrue(self.server.rpc.save(flush=True))
        self.assertTrue((self.inst.path / "world" / "level.dat").is_file())


if __name__ == "__main__":
    unittest.main()
