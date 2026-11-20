import asyncio
import os
import time
import unittest
from unittest import mock

from home_monitor.notify import (Channel, Contact, Dispatcher, EmailChannel, FakeModeChannel, LogChannel, Notification,
                                 NtfyChannel, RateLimited, build_channels, valid_test_target)

try:
    from tests.helpers import NtfyServer, SMTPServer
except ImportError:  # run from inside tests/
    from helpers import NtfyServer, SMTPServer

PAUL = Contact("paul", {"email": "paul@real.test", "ntfy": "paul-topic"})
MUM = Contact("mum", {"email": "mum@real.test"})                    # no ntfy topic
DAD = Contact("dad", {"ntfy": "dad-topic"})                         # no email


def event(level="warn", recovery=False, recipients=(PAUL, MUM, DAD), kind="event", subject="[t] hot: WATCH -> WARN"):
    return Notification(kind, subject, "chest = -5.00\nTime: now\n", tuple(recipients), level=level,
                        recovery=recovery, rule="hot", source="chest")


class Selection(unittest.TestCase):
    def test_each_channel_picks_its_own_address_and_skips_people_without_one(self):
        n = event()
        self.assertEqual(EmailChannel("email", {"smtp_host": "h"}).destinations(n), ["paul@real.test", "mum@real.test"])
        self.assertEqual(NtfyChannel("ntfy", {}).destinations(n), ["paul-topic", "dad-topic"])

    def test_duplicate_destinations_are_collapsed(self):
        twin = Contact("twin", {"ntfy": "paul-topic"})
        self.assertEqual(NtfyChannel("ntfy", {}).destinations(event(recipients=(PAUL, twin))), ["paul-topic"])

    def test_levels_filter_events_only(self):
        ch = EmailChannel("email", {"smtp_host": "h", "levels": ["error"]})
        self.assertFalse(ch.accepts(event("warn")))
        self.assertTrue(ch.accepts(event("error")))
        self.assertTrue(ch.accepts(event("warn", recovery=True, kind="report")))   # levels do not apply to reports
        self.assertFalse(ch.accepts(event("warn", recovery=True)))                  # a recovery uses the old level

    def test_ntfy_skips_the_daily_report_unless_asked(self):
        self.assertFalse(NtfyChannel("ntfy", {}).accepts(event(kind="report")))
        self.assertTrue(NtfyChannel("ntfy", {"kinds": ["event", "report"]}).accepts(event(kind="report")))
        self.assertTrue(NtfyChannel("ntfy", {}).accepts(event(kind="startup")))

    def test_labels(self):
        self.assertEqual(NtfyChannel("ntfy", {}).label, "ntfy")
        self.assertEqual(NtfyChannel("phones", {}).label, "ntfy 'phones'")

    def test_build_channels_skips_disabled_and_uses_the_name_as_default_type(self):
        built = build_channels({"email": {"smtp_host": "h"}, "ntfy": {"enabled": False}, "p": {"type": "ntfy"}})
        self.assertEqual([c.label for c in built], ["email", "ntfy 'p'"])

    def test_valid_test_targets(self):
        self.assertTrue(all(map(valid_test_target, ["me@x.com", "my-topic_1"])))
        self.assertFalse(any(map(valid_test_target, ["has space", "a/b", "", "x" * 65])))


class Email(unittest.IsolatedAsyncioTestCase):
    async def test_delivers_only_to_recipients_with_an_email_address(self):
        with SMTPServer() as smtp:
            status = await EmailChannel("email", smtp.options()).send(event())
            _, rcpts, msg = smtp.messages[0]
            self.assertEqual((rcpts, msg["Subject"], status), (["paul@real.test", "mum@real.test"], "[t] hot: WATCH -> WARN", "sent to 2 addresses"))

    async def test_error_level_is_marked_high_priority_but_recoveries_are_not(self):
        with SMTPServer() as smtp:
            ch = EmailChannel("email", smtp.options())
            await ch.send(event("error"))
            await ch.send(event("error", recovery=True))
            self.assertEqual([m["X-Priority"] for _, _, m in smtp.messages], ["1", None])

    async def test_nobody_with_an_address_is_reported_not_hidden(self):
        with SMTPServer() as smtp:
            status = await EmailChannel("email", smtp.options()).send(event(recipients=(DAD,)))
            self.assertEqual((status, smtp.messages), ("skipped: no email address for any recipient", []))

    def test_login_from_options_or_environment(self):
        ch = EmailChannel("email", {"smtp_host": "h", "user": "u", "password": "p"})
        self.assertEqual(ch.credentials(), ("u", "p"))
        with mock.patch.dict(os.environ, {"HOME_MONITOR_SMTP_USER": "eu", "HOME_MONITOR_SMTP_PASSWORD": "ep"}):
            self.assertEqual(EmailChannel("email", {"smtp_host": "h"}).credentials(), ("eu", "ep"))
        with mock.patch.dict(os.environ, clear=True):
            self.assertIsNone(EmailChannel("email", {"smtp_host": "h"}).credentials())


class Ntfy(unittest.IsolatedAsyncioTestCase):
    async def test_publishes_json_to_each_topic_with_priority_and_tags(self):
        with NtfyServer() as srv:
            status = await NtfyChannel("ntfy", {"server": srv.url}).send(event("error"))
            self.assertEqual(status, "sent to 2 topics")
            self.assertEqual([p["topic"] for p in srv.payloads], ["paul-topic", "dad-topic"])
            p = srv.payloads[0]
            self.assertEqual((p["title"], p["priority"], p["tags"]), ("[t] hot: WATCH -> WARN", 5, ["rotating_light"]))
            self.assertIn("chest = -5.00", p["message"])
            self.assertEqual(srv.requests[0][0], "/")

    async def test_priorities_by_level_recovery_and_override(self):
        with NtfyServer() as srv:
            ch = NtfyChannel("ntfy", {"server": srv.url, "priorities": {"watch": 2}})
            for lvl, rec in (("watch", False), ("warn", False), ("error", False), ("error", True)):
                await ch.send(event(lvl, rec, recipients=(PAUL,)))
            self.assertEqual([p["priority"] for p in srv.payloads], [2, 4, 5, 3])
            self.assertEqual(srv.payloads[3]["tags"], ["white_check_mark"])

    async def test_token_from_option_or_environment(self):
        with NtfyServer() as srv:
            await NtfyChannel("ntfy", {"server": srv.url, "token": "tk_a"}).send(event(recipients=(PAUL,)))
            with mock.patch.dict(os.environ, {"HOME_MONITOR_NTFY_TOKEN": "tk_env"}):
                await NtfyChannel("ntfy", {"server": srv.url}).send(event(recipients=(PAUL,)))
            self.assertEqual([r[1].get("Authorization") for r in srv.requests], ["Bearer tk_a", "Bearer tk_env"])

    async def test_one_failing_topic_is_named_and_does_not_stop_the_others(self):
        with NtfyServer(fail_topics={"paul-topic"}) as srv:
            with self.assertRaises(RuntimeError) as cm:
                await NtfyChannel("ntfy", {"server": srv.url}).send(event())
            self.assertIn("paul-topic", str(cm.exception))
            self.assertNotIn("dad-topic", str(cm.exception))
            self.assertEqual([p["topic"] for p in srv.payloads], ["paul-topic", "dad-topic"])   # dad still got it

    async def test_long_messages_are_truncated_on_a_character_boundary(self):
        with NtfyServer() as srv:
            n = Notification("test", "s", "é" * 5000, (PAUL,))
            await NtfyChannel("ntfy", {"server": srv.url}).send(n)
            msg = srv.payloads[0]["message"]
            self.assertLessEqual(len(msg.encode()), 3900)
            self.assertTrue(msg.endswith("[truncated]"))

    async def test_no_topics_is_reported_not_hidden(self):
        self.assertEqual(await NtfyChannel("ntfy", {}).send(event(recipients=(MUM,))),
                         "skipped: no ntfy topic for any recipient")


class Recording(Channel):
    kind = "rec"

    def __init__(self, log, fail_times=0, name="rec"):
        super().__init__(name, {})
        self.log, self.fail_times = log, fail_times

    async def send(self, n):
        if self.fail_times:
            self.fail_times -= 1
            raise RuntimeError("down")
        self.log.append((self.name, time.monotonic()))
        return "sent"


class DispatcherBehaviour(unittest.IsolatedAsyncioTestCase):
    async def test_a_failing_channel_does_not_delay_the_others(self):
        log = []
        slow = Recording(log, fail_times=2, name="slow")
        fast = Recording(log, name="fast")
        d = Dispatcher([slow, fast])
        d.RETRY_DELAYS = (0, 0.3, 0.3)
        start = time.monotonic()
        await d.send(event())
        times = dict(log)
        self.assertLess(times["fast"] - start, 0.1)          # delivered at once
        self.assertGreaterEqual(times["slow"] - start, 0.55)  # after its own two retries

    async def test_every_successful_send_is_logged(self):
        with self.assertLogs("home_monitor.notify.dispatcher", "INFO") as cm:
            await Dispatcher([Recording([], name="rec")]).send(event())
        self.assertEqual(cm.output, ["INFO:home_monitor.notify.dispatcher:rec: sent"])

    async def test_gives_up_after_the_retries_and_does_not_raise(self):
        d = Dispatcher([Recording([], fail_times=99)])
        d.RETRY_DELAYS = (0, 0, 0)
        await d.send(event())

    async def test_only_channels_that_accept_the_message_are_used(self):
        log = []
        only_errors = Recording(log, name="only_errors")
        only_errors.levels = frozenset({"error"})
        await Dispatcher([only_errors, Recording(log, name="all")]).send(event("warn"))
        self.assertEqual([n for n, _ in log], ["all"])

    async def test_send_once_reports_each_channel_including_failures_and_skips(self):
        with SMTPServer() as smtp, NtfyServer() as srv:
            d = Dispatcher([LogChannel(), EmailChannel("email", smtp.options()),
                            NtfyChannel("ntfy", {"server": srv.url, "kinds": ["event"]}),
                            EmailChannel("dead", {"smtp_host": "127.0.0.1", "smtp_port": 1, "starttls": False})])
            results = await d.send_once(event(kind="test", recipients=(PAUL,)))
        self.assertEqual(results[:2], ["log: logged", "email: sent to 1 address"])
        self.assertEqual(results[2], "ntfy: not used for test messages")
        self.assertTrue(results[3].startswith("email 'dead': FAILED"), results[3])


class FakeMode(unittest.IsolatedAsyncioTestCase):
    async def test_email_redirect_sends_nothing_to_ntfy_and_names_the_real_recipients(self):
        with SMTPServer() as smtp, NtfyServer() as srv:
            mail = FakeModeChannel(EmailChannel("email", smtp.options()), ["tester@test"])
            push = FakeModeChannel(NtfyChannel("ntfy", {"server": srv.url}), ["tester@test"])
            self.assertEqual(await mail.send(event()), "sent to 1 address")
            self.assertEqual(await push.send(event()), "skipped: no ntfy topic for any recipient")
            _, rcpts, msg = smtp.messages[0]
            self.assertEqual((rcpts, msg["Subject"]), (["tester@test"], "[FAKE-PI TEST] [t] hot: WATCH -> WARN"))
            self.assertIn("paul, mum, dad", msg.get_payload())
            self.assertNotIn("paul@real.test", msg.as_string())
            self.assertEqual(srv.requests, [])
            self.assertEqual((mail.label, push.label), ("email -> tester@test", "ntfy -> (no matching test target)"))

    async def test_a_topic_target_redirects_ntfy_only(self):
        with SMTPServer() as smtp, NtfyServer() as srv:
            mail = FakeModeChannel(EmailChannel("email", smtp.options()), ["my-test-topic"])
            push = FakeModeChannel(NtfyChannel("ntfy", {"server": srv.url}), ["my-test-topic"])
            await mail.send(event())
            await push.send(event())
            self.assertEqual(smtp.messages, [])
            self.assertEqual([p["topic"] for p in srv.payloads], ["my-test-topic"])
            self.assertTrue(srv.payloads[0]["title"].startswith("[FAKE-PI TEST]"))
            self.assertIn("paul, mum, dad", srv.payloads[0]["message"])

    async def test_mixed_targets_reach_each_channel_once(self):
        with SMTPServer() as smtp, NtfyServer() as srv:
            targets = ["tester@test", "my-test-topic"]
            await FakeModeChannel(EmailChannel("email", smtp.options()), targets).send(event())
            await FakeModeChannel(NtfyChannel("ntfy", {"server": srv.url}), targets).send(event())
            self.assertEqual(([m[1] for m in smtp.messages], [p["topic"] for p in srv.payloads]), ([["tester@test"]], ["my-test-topic"]))

    async def test_real_mode_keeps_real_recipients_but_marks_the_message(self):
        with NtfyServer() as srv:
            ch = FakeModeChannel(NtfyChannel("ntfy", {"server": srv.url}), None)
            await ch.send(event())
            self.assertEqual([p["topic"] for p in srv.payloads], ["paul-topic", "dad-topic"])
            self.assertTrue(srv.payloads[0]["title"].startswith("[FAKE-PI TEST]"))
            self.assertEqual(ch.label, "ntfy to the REAL recipients")

    async def test_rate_limit_stops_a_flood_and_skips_cost_nothing(self):
        with SMTPServer() as smtp:
            ch = FakeModeChannel(EmailChannel("email", smtp.options()), ["me@test"], max_per_minute=2)
            await ch.send(event())
            await ch.send(event())
            with self.assertRaises(RateLimited):
                await ch.send(event())
            self.assertEqual(len(smtp.messages), 2)
            quiet = FakeModeChannel(NtfyChannel("ntfy", {}), ["me@test"], max_per_minute=1)   # no topic: nothing to limit
            for _ in range(3):
                self.assertTrue((await quiet.send(event())).startswith("skipped"))


if __name__ == "__main__":
    unittest.main()
