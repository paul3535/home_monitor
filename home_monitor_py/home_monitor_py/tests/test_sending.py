"""Whole-program check: a fake alert and the panel's test buttons reach email AND ntfy at once,
redirected to test targets, through real SMTP and HTTP calls to throwaway local servers."""
import asyncio
import json
import tempfile
import unittest
import urllib.request
from pathlib import Path

from home_monitor.app import Monitor
from home_monitor.config import load_config
from home_monitor.fakepi import FakePi, start_control_server
from home_monitor.notify import Dispatcher, EmailChannel, FakeModeChannel, LogChannel, NtfyChannel

try:
    from tests.helpers import NtfyServer, SMTPServer
except ImportError:  # run from inside tests/
    from helpers import NtfyServer, SMTPServer

CONFIG = """
[system]
name = "t"
tick_seconds = 1
[contacts]
me = { email = "me@real", ntfy = "me-real-topic" }
mum = { email = "mum@real", ntfy = "mum-real-topic" }
[notify]
report_to = ["me"]
[notify.levels]
warn = ["mum"]
error = ["mum"]
[[ds18b20]]
name = "chest"
id = "28-x"
interval_s = 60
[[alerts]]
name = "hot"
type = "bounds"
source = "chest"
op = "greater_than"
value = -8.0
warn_s = 30
error_s = 6000
"""


class TwoChannelsEndToEnd(unittest.IsolatedAsyncioTestCase):
    async def test_alert_and_test_buttons_reach_both_channels_at_the_test_targets_only(self):
        with SMTPServer() as smtp, NtfyServer() as srv, tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / "c.toml"
            p.write_text(CONFIG)
            cfg = load_config(p)
            cfg.db_path = Path(tmp) / "t.db"
            targets = ["tester@test", "my-test-topic"]
            channels = [LogChannel(),
                        FakeModeChannel(EmailChannel("email", smtp.options()), targets),
                        FakeModeChannel(NtfyChannel("ntfy", {"server": srv.url, "kinds": ["event", "test", "report"]}), targets)]
            fake = FakePi(cfg)
            mon = Monitor(cfg, dispatcher=Dispatcher(channels), fake_pi=fake, time_scale=60)
            mon.notify_label = "; ".join(c.label for c in channels[1:])
            server = start_control_server(fake, mon, "127.0.0.1", 0)
            base = f"http://127.0.0.1:{server.server_address[1]}"

            def post(path, body):
                req = urllib.request.Request(base + path, data=json.dumps(body).encode(),
                                             headers={"Content-Type": "application/json"})
                with urllib.request.urlopen(req, timeout=60) as r:
                    return json.loads(r.read())

            stop = asyncio.Event()
            run = asyncio.create_task(mon.run(stop))
            try:
                await asyncio.sleep(0.4)
                state = await asyncio.to_thread(lambda: json.loads(urllib.request.urlopen(base + "/api/state").read()))
                self.assertEqual(state["notify_label"], "email -> tester@test; ntfy -> my-test-topic")

                result = await asyncio.to_thread(post, "/api/test-email", {})
                self.assertEqual(result["message"].splitlines(), [
                    "log: logged", "email -> tester@test: sent to 1 address", "ntfy -> my-test-topic: sent to 1 topic"])
                report = await asyncio.to_thread(post, "/api/test-report", {})
                self.assertIn("email -> tester@test: sent", report["message"])

                await asyncio.to_thread(post, "/api/temp", {"name": "chest", "value": -5})   # fake warm freezer
                await asyncio.sleep(2.5)
            finally:
                stop.set()
                await run
                server.shutdown()
                server.server_close()

            mail = [m[2]["Subject"] for m in smtp.messages]
            push = [(p["topic"], p["title"], p["priority"]) for p in srv.payloads]
            self.assertIn("[FAKE-PI TEST] [t] hot: WATCH -> WARN", mail)
            self.assertIn(("my-test-topic", "[FAKE-PI TEST] [t] hot: WATCH -> WARN", 4), push)   # warn = priority 4
            self.assertIn("[FAKE-PI TEST] [t] daily report (test)", mail)
            self.assertIn("[FAKE-PI TEST] [t] daily report (test)", [t for _, t, _ in push])
            for _, rcpts, msg in smtp.messages:
                self.assertEqual(rcpts, ["tester@test"])                    # never the real people
            self.assertEqual({t for t, _, _ in push}, {"my-test-topic"})
            warn = next(m for m in smtp.messages if "WATCH -> WARN" in m[2]["Subject"])
            self.assertIn("mum", warn[2].get_payload())                      # who would really have been told
            self.assertNotIn("mum@real", warn[2].as_string())


if __name__ == "__main__":
    unittest.main()
