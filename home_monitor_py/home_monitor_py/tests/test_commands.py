import asyncio
import tempfile
import time
import unittest
from pathlib import Path

from home_monitor.app import Monitor
from home_monitor.commands import (CommandError, CommandHandler, CommandRejected, decode, encode, fmt_duration,
                                   parse_duration)
from home_monitor.config import load_config
from home_monitor.notify import (Contact, Dispatcher, FakeModeChannel, LogChannel, Notification, NtfyChannel,
                                 NtfyCommandListener)

try:
    from tests.helpers import NtfyServer, RecordingDispatcher
except ImportError:  # run from inside tests/
    from helpers import NtfyServer, RecordingDispatcher

SECRET = "a-long-random-secret-123"
NTFY = {"command_topic": "cmd-topic", "command_secret": SECRET, "command_poll_s": 0.05}
PAUL = Contact("paul", {"ntfy": "paul-topic"})
MUM = Contact("mum", {"ntfy": "mum-topic"})

CONFIG = """
[system]
name = "t"
tick_seconds = 1
[contacts]
paul = { ntfy = "paul-topic" }
[notify.levels]
warn = ["paul"]
error = ["paul"]
[[ds18b20]]
name = "chest"
id = "28-x"
interval_s = 60
[[switches]]
name = "gup"
pin = 17
[[switches]]
name = "gdown"
pin = 27
[[devices]]
name = "garage"
type = "garage_door"
up = "gup"
down = "gdown"
[[alerts]]
name = "hot"
type = "bounds"
source = "chest"
op = "greater_than"
value = -8.0
warn_s = 30
error_s = 6000
[[alerts]]
name = "door_up"
type = "state"
source = "garage"
state = "up"
"""


def make_monitor(tmp, **kw):
    p = Path(tmp) / "c.toml"
    p.write_text(CONFIG)
    cfg = load_config(p)
    cfg.db_path = Path(tmp) / "t.db"
    return Monitor(cfg, dispatcher=kw.pop("dispatcher", RecordingDispatcher()), **kw)


def warn_event(rule="hot", recovery=False, kind="event"):
    return Notification(kind, "[t] hot: WATCH -> WARN", "chest = -5\n", (PAUL,), level="warn", recovery=recovery, rule=rule)


class Durations(unittest.TestCase):
    def test_parse(self):
        self.assertEqual([parse_duration(x) for x in ("90s", "30m", "2h", "1d", " 2H ")], [90, 1800, 7200, 86400, 7200])
        for bad in ("", "0m", "2", "h", "1.5h", "-1h", "2 weeks", "10x"):
            with self.assertRaises(ValueError, msg=bad):
                parse_duration(bad)

    def test_format(self):
        self.assertEqual([fmt_duration(x) for x in (45, 60, 5400, 7200, 86400, 90000)], ["45s", "1m", "1h 30m", "2h", "1d", "1d 1h"])


class Signing(unittest.TestCase):
    def test_round_trip_including_awkward_characters(self):
        text = encode(SECRET, "mute", ("a|b é", "2h"), "paul-topic", 2_000_000_000)
        cmd = decode(SECRET, text, now=1_900_000_000)
        self.assertEqual((cmd.name, cmd.args, cmd.reply, cmd.expires), ("mute", ("a|b é", "2h"), "paul-topic", 2_000_000_000))

    def test_any_change_breaks_the_signature(self):
        good = encode(SECRET, "mute", ("hot", "1h"), "paul-topic", 2_000_000_000)
        for forged in (good.replace("hot", "cold"), good.replace("1h", "9h"), good.replace("paul-topic", "evil-topic"),
                       good.replace("2000000000", "2999999999"), good[:-1] + ("0" if good[-1] != "0" else "1"),
                       "hello", "", "|", good.rsplit("|", 1)[0], "v1|1|x|status|" + "0" * 32):
            with self.assertRaises(CommandRejected, msg=forged) as cm:
                decode(SECRET, forged, now=1_900_000_000)
            self.assertIsNone(cm.exception.reply)          # forged messages never get an answer

    def test_wrong_secret(self):
        with self.assertRaises(CommandRejected):
            decode("another-long-secret-value", encode(SECRET, "status", (), "t", 2_000_000_000), now=1)

    def test_expired_button_is_refused_but_the_owner_is_told(self):
        with self.assertRaises(CommandRejected) as cm:
            decode(SECRET, encode(SECRET, "status", (), "paul-topic", 1000), now=1001)
        self.assertEqual(cm.exception.reply, "paul-topic")
        self.assertIn("expired", cm.exception.reason)


class Handler(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.mon = make_monitor(self._tmp.name)
        self.addCleanup(self.mon.store.close)
        self.h = CommandHandler(self.mon, max_mute_s=86400)

    def test_status_summarises_temperatures_devices_and_alerts(self):
        self.mon.latest["chest"] = (-15.24, time.time() - 120)
        self.mon.devices[0].published = "down"
        r = self.h.run("status", ())
        self.assertEqual(r.headline, "status")
        self.assertIn("chest: -15.2 C (2m ago)", r.body)
        self.assertIn("garage: down", r.body)
        self.assertIn("all OK", r.body)

    def test_status_shows_problems_and_mutes(self):
        self.mon.engine.on_value("chest", -5.0, self.mon.clock())
        self.mon.engine.tick(self.mon.clock() + 31)
        self.h.run("mute", ("hot", "2h"))
        body = self.h.run("status", ()).body
        self.assertIn("hot: WARN [muted, 2h left]", body)
        self.assertNotIn("all OK", body)
        self.assertIn("no reading yet", body)

    def test_mute_sets_the_rule_and_offers_an_undo(self):
        r = self.h.run("mute", ("hot", "2h"))
        rule = self.mon.engine.rule("hot")
        self.assertAlmostEqual(rule.muted_until - self.mon.clock(), 7200, delta=2)
        self.assertEqual((r.headline, r.buttons), ("muted hot for 2h", (("Unmute", "unmute", ("hot",)),)))
        self.assertEqual(self.mon.engine.rule("door_up").muted_until, 0.0)   # only that alert

    def test_mute_is_capped(self):
        r = self.h.run("mute", ("hot", "30d"))
        self.assertAlmostEqual(self.mon.engine.rule("hot").muted_until - self.mon.clock(), 86400, delta=2)
        self.assertIn("limited to 1d", r.headline)

    def test_mute_all_and_unmute(self):
        self.h.run("mute", ("all", "1h"))
        self.assertTrue(all(r.muted_until > 0 for r in self.mon.engine.rules))
        self.h.run("unmute", ("all",))
        self.assertTrue(all(r.muted_until == 0 for r in self.mon.engine.rules))
        self.h.run("mute", ("hot", "1h"))
        self.h.run("unmute", ("hot",))
        self.assertEqual(self.mon.engine.rule("hot").muted_until, 0.0)

    def test_a_muted_alert_does_not_notify_but_its_recovery_does(self):
        self.h.run("mute", ("hot", "1h"))
        now = self.mon.clock()
        self.mon.engine.on_value("chest", -5.0, now)
        warn = self.mon.engine.tick(now + 31)[0]
        self.assertEqual((warn.notify, warn.muted), (False, True))
        recovery = self.mon.engine.on_value("chest", -15.0, now + 40)[0]
        self.assertTrue(recovery.notify)

    def test_bad_requests_say_what_is_wrong(self):
        for args, expect in ((("mute", ("nosuch", "1h")), "no alert called"), (("mute", ("hot", "soon")), "bad duration"),
                             (("mute", ("hot",)), "unknown command"), (("reboot", ()), "unknown command"),
                             (("unmute", ("nosuch",)), "no alert called")):
            with self.assertRaises(CommandError, msg=args) as cm:
                self.h.run(*args)
            self.assertIn(expect, str(cm.exception))


class Buttons(unittest.TestCase):
    def channel(self, **extra):
        return NtfyChannel("ntfy", {"server": "https://n.test", **NTFY, **extra})

    def test_alert_gets_mute_and_status_buttons_signed_for_the_recipient(self):
        acts = self.channel().actions(warn_event(), "paul-topic")
        self.assertEqual([a["label"] for a in acts], ["Mute 1h", "Mute 8h", "Status"])
        self.assertTrue(all(a["action"] == "http" and a["method"] == "POST" and a["url"] == "https://n.test/cmd-topic" for a in acts))
        self.assertEqual([a["clear"] for a in acts], [True, True, False])
        cmd = decode(SECRET, acts[0]["body"], time.time())
        self.assertEqual((cmd.name, cmd.args, cmd.reply), ("mute", ("hot", "1h", "paul"), "paul-topic"))   # says who tapped
        self.assertAlmostEqual(cmd.expires, time.time() + 86400, delta=5)

    def test_each_recipient_replies_to_their_own_topic(self):
        ch = self.channel()
        self.assertEqual(decode(SECRET, ch.actions(warn_event(), "mum-topic")[0]["body"], time.time()).reply, "mum-topic")

    def test_recoveries_and_non_alert_messages_only_get_status(self):
        ch = self.channel()
        for n in (warn_event(recovery=True), warn_event(kind="test"), warn_event(kind="startup"), warn_event(rule="")):
            self.assertEqual([a["label"] for a in ch.actions(n, "paul-topic")], ["Status"])

    def test_options_change_the_buttons(self):
        ch = self.channel(mute_buttons=["30m"], command_ttl="2h")
        acts = ch.actions(warn_event(), "paul-topic")
        self.assertEqual([a["label"] for a in acts], ["Mute 30m", "Status"])
        self.assertAlmostEqual(decode(SECRET, acts[0]["body"], time.time()).expires, time.time() + 7200, delta=5)

    def test_no_buttons_without_command_options(self):
        self.assertEqual(NtfyChannel("ntfy", {}).actions(warn_event(), "t"), [])

    def test_option_validation(self):
        for opts, expect in (({"command_topic": "x"}, "go together"), ({"command_secret": "x" * 20}, "go together"),
                             ({"command_topic": "x", "command_secret": "short"}, "at least 16"),
                             ({"command_topic": "bad topic!", "command_secret": SECRET}, "letters, digits"),
                             ({**NTFY, "mute_buttons": ["1h", "2h", "3h"]}, "at most 2"),
                             ({**NTFY, "mute_buttons": ["soon"]}, "bad duration"),
                             ({**NTFY, "max_mute": "forever"}, "bad duration"),
                             ({**NTFY, "command_poll_s": 0}, "at least 0.05")):
            with self.assertRaises(ValueError, msg=opts) as cm:
                NtfyChannel("ntfy", opts)
            self.assertIn(expect, str(cm.exception))

    def test_fake_mode_never_uses_the_real_command_topic(self):
        inner = self.channel()
        FakeModeChannel(inner, ["my-test-topic"])
        self.assertEqual(inner.command_topic, "cmd-topic-fake")


class PhoneRoundTrip(unittest.IsolatedAsyncioTestCase):
    """The listener against a stand-in ntfy server, with 'phone' presses made as real HTTP posts."""

    async def asyncSetUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.srv = NtfyServer().__enter__()
        self.addCleanup(self.srv.__exit__, None, None, None)
        self.mon = make_monitor(self._tmp.name)
        self.addCleanup(self.mon.store.close)
        self.ch = NtfyChannel("ntfy", {"server": self.srv.url, **NTFY})
        self.listener = NtfyCommandListener(self.ch, CommandHandler(self.mon), "t")

    async def start(self):
        self.task = asyncio.create_task(self.listener.run())
        self.addCleanup(self.task.cancel)
        await asyncio.sleep(0.2)

    async def press(self, action):
        await asyncio.to_thread(self.srv.press, action)
        await asyncio.sleep(0.4)

    def last_reply(self, topic="paul-topic"):
        return self.srv.messages[topic][-1]

    async def test_mute_then_unmute_from_the_notification_and_the_reply(self):
        await self.start()
        mute_1h = self.ch.actions(warn_event(), "paul-topic")[0]
        await self.press(mute_1h)
        self.assertGreater(self.mon.engine.rule("hot").muted_until, self.mon.clock() + 3500)
        reply = self.last_reply()
        self.assertEqual((reply["title"], reply["priority"]), ("[t] muted hot for 1h", 3))
        unmute = reply["actions"][0]
        self.assertEqual(unmute["label"], "Unmute")
        await self.press(unmute)                                   # the Unmute button on the reply
        self.assertEqual(self.mon.engine.rule("hot").muted_until, 0.0)
        self.assertEqual(self.last_reply()["title"], "[t] unmuted hot")

    async def test_logs_show_the_reply_but_never_a_full_topic(self):
        with self.assertLogs("home_monitor.notify.ntfy_commands", "INFO") as cm:
            await self.start()
            await self.press(self.ch.actions(warn_event(), "paul-topic")[2])
        text = "\n".join(cm.output)
        self.assertNotIn("paul-topic", text)
        self.assertNotIn("cmd-topic", text)
        self.assertIn("listening for ntfy commands on topic cmd-…", text)
        self.assertIn("command from paul…: status", text)
        self.assertIn("replied on paul…: [t] status", text)

    async def test_status_button_replies_with_the_status(self):
        self.mon.latest["chest"] = (-15.2, time.time())
        await self.start()
        await self.press(self.ch.actions(warn_event(), "mum-topic")[2])
        self.assertIn("chest: -15.2 C", self.last_reply("mum-topic")["message"])      # went to the person who tapped

    async def test_messages_already_on_the_topic_at_startup_are_not_replayed(self):
        self.srv._store("cmd-topic", message=self.ch.actions(warn_event(), "paul-topic")[0]["body"])   # an old press
        await self.start()
        self.assertEqual(self.mon.engine.rule("hot").muted_until, 0.0)
        self.assertNotIn("paul-topic", self.srv.messages)

    async def test_forged_and_garbled_messages_are_ignored_silently(self):
        await self.start()
        for text in ("hello", "v1|9999999999|paul-topic|mute|hot|1h|" + "0" * 32,
                     encode("some-other-secret-0000", "mute", ("hot", "1h"), "paul-topic", time.time() + 60)):
            await asyncio.to_thread(self.srv.press, {"url": f"{self.srv.url}/cmd-topic", "method": "POST", "body": text})
        await asyncio.sleep(0.4)
        self.assertEqual(self.mon.engine.rule("hot").muted_until, 0.0)
        self.assertNotIn("paul-topic", self.srv.messages)

    async def test_expired_button_gets_an_explanation_and_does_nothing(self):
        await self.start()
        old = encode(SECRET, "mute", ("hot", "1h"), "paul-topic", time.time() - 5)
        await self.press({"url": f"{self.srv.url}/cmd-topic", "method": "POST", "body": old})
        self.assertEqual(self.mon.engine.rule("hot").muted_until, 0.0)
        self.assertEqual(self.last_reply()["title"], "command not run")
        self.assertIn("expired", self.last_reply()["message"])

    async def test_a_command_that_cannot_be_done_says_why(self):
        await self.start()
        body = encode(SECRET, "mute", ("nosuch", "1h"), "paul-topic", time.time() + 60)
        await self.press({"url": f"{self.srv.url}/cmd-topic", "method": "POST", "body": body})
        self.assertIn("no alert called 'nosuch'", self.last_reply()["message"])

    async def test_survives_poll_failures_and_still_runs_the_next_command(self):
        await self.start()
        self.srv.fail_polls = 3
        await asyncio.to_thread(self.srv.press, self.ch.actions(warn_event(), "paul-topic")[0])
        await asyncio.sleep(1.5)
        self.assertGreater(self.mon.engine.rule("hot").muted_until, 0)
        self.assertFalse(self.task.done())


async def wait_for(predicate, timeout=6.0):
    """Wait until predicate() is true (a fixed sleep is a race under load)."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return True
        await asyncio.sleep(0.05)
    return False


class WholeProgram(unittest.IsolatedAsyncioTestCase):
    async def test_fake_alert_push_has_buttons_and_tapping_mute_silences_the_real_rule(self):
        with NtfyServer() as srv, tempfile.TemporaryDirectory() as tmp:
            ntfy = NtfyChannel("ntfy", {"server": srv.url, **NTFY})
            fake_channel = FakeModeChannel(ntfy, ["my-test-topic"])
            from home_monitor.fakepi import FakePi
            mon = make_monitor(tmp, dispatcher=Dispatcher([LogChannel(), fake_channel]), time_scale=60)
            mon.fake_pi = FakePi(mon.cfg)
            stop = asyncio.Event()
            run = asyncio.create_task(mon.run(stop))
            try:
                await asyncio.sleep(0.5)
                mon.fake_pi.set_temp("chest", -5.0)   # the sensor itself, so its poller keeps reporting -5
                warned = lambda: any("WARN" in m.get("title", "") for m in srv.messages.get("my-test-topic", []))
                self.assertTrue(await wait_for(warned), "no WARN push arrived")   # 30 virtual s = 0.5 s real
                push = next(m for m in srv.messages["my-test-topic"] if "WARN" in m.get("title", ""))
                self.assertEqual([a["label"] for a in push["actions"]], ["Mute 1h", "Mute 8h", "Status"])
                self.assertTrue(push["actions"][0]["url"].endswith("/cmd-topic-fake"))    # never the real command topic
                await asyncio.to_thread(srv.press, push["actions"][0])
                muted = lambda: next(r for r in mon.engine.snapshot(mon.clock()) if r["rule"] == "hot")["muted"]
                self.assertTrue(await wait_for(muted), "the tap did not mute the rule")
                self.assertTrue(await wait_for(lambda: srv.messages["my-test-topic"][-1]["title"] == "[t] muted hot for 1h"))
            finally:
                stop.set()
                await run


class StartupMessage(unittest.IsolatedAsyncioTestCase):
    async def run_startup(self, tmp, unplug=False, wait=5.0):
        mon = make_monitor(tmp, time_scale=1.0)
        from home_monitor.fakepi import FakePi
        mon.fake_pi = FakePi(mon.cfg)
        mon.cfg.send_startup = True
        mon.cfg.report_to = ["paul"]
        mon.startup_wait_s = wait
        if unplug:
            mon.fake_pi.set_temp("chest", mode="unplugged")
        stop = asyncio.Event()
        run = asyncio.create_task(mon.run(stop))
        try:
            await wait_for(lambda: mon.dispatcher.notifications, timeout=wait + 3)
        finally:
            stop.set()
            await run
        return mon.dispatcher.notifications

    async def test_startup_message_carries_the_status_once_readings_are_in(self):
        with tempfile.TemporaryDirectory() as tmp:
            sent = await self.run_startup(tmp)
        self.assertEqual(len(sent), 1)
        n = sent[0]
        self.assertEqual((n.kind, n.subject, [c.name for c in n.recipients]), ("startup", "[t] started", ["paul"]))
        self.assertIn("t started ", n.body)
        self.assertIn("home_monitor 0.1.0", n.body)
        self.assertIn("chest: -15.0 C", n.body)            # it waited for the first reading
        self.assertIn("garage: down", n.body)              # and for the device to settle
        self.assertNotIn("no reading yet", n.body)

    async def test_it_is_still_sent_when_a_sensor_never_reports(self):
        with tempfile.TemporaryDirectory() as tmp:
            start = time.monotonic()
            sent = await self.run_startup(tmp, unplug=True, wait=1.6)   # longer than the device's 1 s settle time
        self.assertGreaterEqual(time.monotonic() - start, 1.6)     # it gave the sensor its chance
        self.assertIn("chest: no reading yet", sent[0].body)       # and says so instead of staying silent
        self.assertIn("garage: down", sent[0].body)

    async def test_nothing_is_sent_when_send_startup_is_off(self):
        with tempfile.TemporaryDirectory() as tmp:
            mon = make_monitor(tmp)
            from home_monitor.fakepi import FakePi
            mon.fake_pi = FakePi(mon.cfg)
            mon.cfg.send_startup = False
            stop = asyncio.Event()
            run = asyncio.create_task(mon.run(stop))
            await asyncio.sleep(0.5)
            stop.set()
            await run
        self.assertEqual(mon.dispatcher.notifications, [])


if __name__ == "__main__":
    unittest.main()
