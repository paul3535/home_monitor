import asyncio
import tempfile
import time
import unittest
from pathlib import Path

from home_monitor.alerts import AlertEngine, BoundsRule, Escalation
from home_monitor.app import Monitor
from home_monitor.commands import CommandError, CommandHandler, decode
from home_monitor.config import ConfigError, load_config
from home_monitor.fakepi import FakePi
from home_monitor.models import AlertState as S
from home_monitor.notify import (Contact, Dispatcher, FakeModeChannel, LogChannel, Notification, NtfyChannel,
                                 NtfyCommandListener)

try:
    from tests.helpers import NtfyServer, RecordingDispatcher
except ImportError:  # run from inside tests/
    from helpers import NtfyServer, RecordingDispatcher

SECRET = "a-long-random-secret-123"
OPTS = {"command_topic": "cmd-topic", "command_secret": SECRET, "command_poll_s": 0.05}
PAUL = Contact("paul", {"ntfy": "paul-topic"})
RYAN = Contact("ryan", {"ntfy": "ryan-topic"})
MUM = Contact("mum", {"ntfy": "mum-topic"})
DAD = Contact("dad", {"ntfy": "dad-topic"})

CONFIG = """
[system]
name = "t"
tick_seconds = 1
[contacts]
paul = { ntfy = "paul-topic" }
ryan = { ntfy = "ryan-topic" }
mum = { ntfy = "mum-topic" }
[notify.levels]
watch = ["paul"]
warn = ["paul"]
error = ["paul"]
[[ds18b20]]
name = "chest"
id = "28-x"
interval_s = 60
[[alerts]]
name = "laundry"
type = "bounds"
source = "chest"
op = "greater_than"
value = -8.0
watch_s = 0
warn_s = 30
error_s = 6000
notify_on = ["watch", "warn", "error"]
claim = ["paul", "ryan"]
recipients = { watch = ["paul", "ryan"], warn = ["paul", "ryan"], error = ["paul", "ryan", "mum"] }
[[alerts]]
name = "plain"
type = "bounds"
source = "chest"
op = "greater_than"
value = 99.0
"""


def push(who, *, claim=("paul", "ryan"), claimed_by=None, recovery=False, kind="event", rule="laundry", recipients=(PAUL, RYAN)):
    return Notification(kind, "[t] laundry: x", "body\n", tuple(recipients), level="watch", recovery=recovery,
                        rule=rule, claim=tuple(claim), claimed_by=claimed_by), who.addresses["ntfy"]


def labels(actions):
    return [a["label"] for a in actions]


def command(action):
    return decode(SECRET, action["body"], time.time())


class ButtonLayouts(unittest.TestCase):
    def setUp(self):
        self.ch = NtfyChannel("ntfy", {"server": "https://n.test", **OPTS})

    def acts(self, who, **kw):
        n, topic = push(who, **kw)
        return self.ch.actions(n, topic)

    def test_the_question_for_each_of_two_people(self):
        paul = self.acts(PAUL)
        self.assertEqual(labels(paul), ["I'll do it", "Give to Ryan", "Status"])
        c1, c2 = command(paul[0]), command(paul[1])
        self.assertEqual((c1.name, c1.args, c1.reply), ("claim", ("laundry", "paul", "paul"), "paul-topic"))
        self.assertEqual((c2.name, c2.args), ("claim", ("laundry", "ryan", "paul")))     # Paul assigns it to Ryan
        self.assertEqual(labels(self.acts(RYAN)), ["I'll do it", "Give to Paul", "Status"])
        self.assertEqual(command(self.acts(RYAN)[0]).args, ("laundry", "ryan", "ryan"))

    def test_three_or_more_people_get_a_menu_instead_of_one_button_each(self):
        acts = self.acts(PAUL, claim=("paul", "ryan", "mum"))
        self.assertEqual(labels(acts), ["I'll do it", "Give to…", "Status"])
        c = command(acts[1])
        self.assertEqual((c.name, c.args), ("choose", ("laundry", "paul")))

    def test_a_recipient_who_cannot_claim_can_still_hand_it_on(self):
        acts = self.acts(DAD, recipients=(DAD,))
        self.assertEqual(labels(acts), ["Give to…", "Status"])          # two candidates: choose between them
        acts = self.acts(DAD, claim=("ryan",), recipients=(DAD,))
        self.assertEqual(labels(acts), ["Give to Ryan", "Status"])

    def test_once_claimed_the_claimer_gets_mute_and_release(self):
        acts = self.acts(RYAN, claimed_by="ryan", recipients=(RYAN,))
        self.assertEqual(labels(acts), ["Mute 1h", "Release", "Status"])
        self.assertEqual(command(acts[1]).args, ("laundry", "ryan"))

    def test_recoveries_and_unclaimable_alerts_are_unchanged(self):
        self.assertEqual(labels(self.acts(PAUL, recovery=True)), ["Status"])
        self.assertEqual(labels(self.acts(PAUL, claim=())), ["Mute 1h", "Mute 8h", "Status"])

    def test_someone_watching_can_take_over_or_release(self):
        acts = self.acts(PAUL, claimed_by="ryan", recipients=(RYAN, PAUL))
        self.assertEqual(labels(acts), ["I'll take over", "Release", "Status"])
        self.assertEqual(command(acts[0]).args, ("laundry", "paul", "paul"))
        self.assertEqual(command(acts[1]).args, ("laundry", "paul"))
        self.assertEqual(labels(self.acts(DAD, claimed_by="ryan", recipients=(DAD,))), ["Release", "Status"])   # not a claimant

    def test_the_announcement_gives_the_new_owner_a_release_button(self):
        self.assertEqual(labels(self.acts(RYAN, kind="claim", claimed_by="ryan")), ["Release", "Status"])
        self.assertEqual(labels(self.acts(PAUL, kind="claim", claimed_by="ryan")), ["Status"])

    def test_mute_buttons_say_who_tapped_them(self):
        acts = self.acts(PAUL, claim=())
        self.assertEqual(command(acts[0]).args, ("laundry", "1h", "paul"))
        claimed = self.acts(RYAN, claimed_by="ryan", recipients=(RYAN,))
        self.assertEqual(command(claimed[0]).args, ("laundry", "1h", "ryan"))

    def test_never_more_than_three_buttons(self):
        ch = NtfyChannel("ntfy", {"server": "https://n.test", **OPTS, "mute_buttons": ["1h", "8h"]})
        n, topic = push(PAUL, claim=("paul", "ryan", "mum", "dad"))
        self.assertLessEqual(len(ch.actions(n, topic)), 3)


class PersonalMute(unittest.TestCase):
    def rule(self, scope="person"):
        return BoundsRule("laundry", "s", "greater_than", 0, mute_scope=scope, escalation=Escalation(0, 10, 20, 0))

    def test_a_personal_mute_covers_only_that_person(self):
        r = self.rule()
        r.mute(0, 3600, "ryan")
        self.assertTrue(r.is_muted_for("ryan", 10))
        self.assertFalse(r.is_muted_for("paul", 10))
        self.assertFalse(r.is_muted_for("ryan", 3601))                  # and it ends
        self.assertEqual(r.muted_until, 0.0)                           # the alert itself is not muted
        events = r.on_value(1, 0) + r.tick(10)
        self.assertTrue(events[-1].notify)                             # events still go out; routing skips Ryan

    def test_unmuting_one_person_leaves_the_others_and_unmuting_the_alert_clears_all(self):
        r = self.rule()
        r.mute(0, 3600, "ryan")
        r.mute(0, 3600, "paul")
        r.unmute("ryan")
        self.assertEqual((r.is_muted_for("ryan", 1), r.is_muted_for("paul", 1)), (False, True))
        r.unmute()
        self.assertFalse(r.is_muted_for("paul", 1))

    def test_an_alert_with_the_default_scope_ignores_who_and_mutes_everyone(self):
        r = self.rule("alert")
        r.mute(0, 3600, "ryan")
        self.assertEqual((r.muted_until, r.muted_for), (3600, {}))

    def test_bad_scope_is_refused_and_the_snapshot_lists_people(self):
        with self.assertRaises(ValueError):
            self.rule("everyone")
        r = self.rule()
        r.mute(0, 3600, "ryan")
        self.assertEqual(AlertEngine([r]).snapshot(60)[0]["muted_people"], {"ryan": 3540})


class EngineClaims(unittest.TestCase):
    def rule(self):
        return BoundsRule("laundry", "s", "greater_than", 0, claim=("paul", "ryan"), escalation=Escalation(0, 10, 20, 0))

    def test_events_carry_the_claimer_and_recovery_clears_it(self):
        r = self.rule()
        self.assertIsNone(r.on_value(1, 0)[0].claimed_by)
        r.claimed_by = "ryan"
        self.assertEqual(r.tick(10)[0].claimed_by, "ryan")
        recovery = r.on_value(-1, 15)[0]
        self.assertEqual((recovery.is_recovery, recovery.claimed_by), (True, "ryan"))   # the claimer hears it is over
        self.assertIsNone(r.claimed_by)                                                 # and the next problem is unclaimed
        self.assertIsNone(r.on_value(1, 30)[0].claimed_by)

    def test_snapshot_shows_the_claimer(self):
        r = self.rule()
        r.on_value(1, 0)
        r.claimed_by = "paul"
        self.assertEqual(AlertEngine([r]).snapshot(1)[0]["claimed_by"], "paul")


def make_monitor(tmp, config=CONFIG, **kw):
    p = Path(tmp) / "c.toml"
    p.write_text(config)
    cfg = load_config(p)
    cfg.db_path = Path(tmp) / "t.db"
    return Monitor(cfg, **kw)


class Commands(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.mon = make_monitor(self._tmp.name, dispatcher=RecordingDispatcher())
        self.addCleanup(self.mon.store.close)
        self.h = CommandHandler(self.mon)
        self.rule = self.mon.engine.rule("laundry")
        self.mon.engine.on_value("chest", -5.0, self.mon.clock())          # the alert is active

    def test_claim_for_yourself_and_release(self):
        r = self.h.run("claim", ("laundry", "ryan", "ryan"))
        self.assertEqual((self.rule.claimed_by, r.headline), ("ryan", "laundry: you have it"))
        self.assertEqual([b[0] for b in r.buttons], ["Release", "Mute 1h"])
        self.assertIn("[ryan has it]", self.h.run("status", ()).body)
        self.h.run("release", ("laundry", "ryan"))
        self.assertIsNone(self.rule.claimed_by)

    def test_assigning_to_someone_else_says_so(self):
        r = self.h.run("claim", ("laundry", "ryan", "paul"))
        self.assertEqual((self.rule.claimed_by, r.headline), ("ryan", "laundry: Ryan has it"))

    def test_the_by_argument_is_optional(self):
        self.h.run("claim", ("laundry", "paul"))
        self.assertEqual(self.rule.claimed_by, "paul")

    def test_choose_lists_everyone_but_the_asker(self):
        self.rule.claim = ("paul", "ryan", "mum")
        r = self.h.run("choose", ("laundry", "paul"))
        self.assertEqual([b[0] for b in r.buttons], ["Give to Ryan", "Give to Mum"])
        self.assertEqual(r.buttons[0][1:], ("claim", ("laundry", "ryan", "paul")))

    def test_refusals_say_why(self):
        for args, expect in ((("claim", ("laundry", "mum", "paul")), "not one of the people"),
                             (("claim", ("laundry", "ryan", "nobody")), "don't know"),
                             (("claim", ("plain", "paul", "paul")), "cannot be claimed"),
                             (("claim", ("nosuch", "paul", "paul")), "no alert called"),
                             (("release", ("plain",)), "cannot be claimed")):
            with self.assertRaises(CommandError, msg=args) as cm:
                self.h.run(*args)
            self.assertIn(expect, str(cm.exception))

    def test_personal_mute_through_the_commands(self):
        self.rule.mute_scope = "person"
        r = self.h.run("mute", ("laundry", "1h", "ryan"))
        self.assertEqual(r.headline, "muted laundry for you, 1h")
        self.assertEqual(r.buttons, (("Unmute", "unmute", ("laundry", "ryan")),))
        self.assertEqual(self.rule.muted_until, 0.0)                            # not the alert
        self.assertTrue(self.rule.is_muted_for("ryan", self.mon.clock()))
        self.assertFalse(self.rule.is_muted_for("paul", self.mon.clock()))
        self.assertIn("[ryan muted, 1h left]", self.h.run("status", ()).body)
        self.h.run("mute", ("laundry", "1h", "paul"))
        self.h.run("unmute", ("laundry", "ryan"))                              # the Unmute button on Ryan's reply
        self.assertFalse(self.rule.is_muted_for("ryan", self.mon.clock()))
        self.assertTrue(self.rule.is_muted_for("paul", self.mon.clock()))      # Paul's mute is untouched

    def test_without_the_person_scope_the_same_button_mutes_everyone(self):
        r = self.h.run("mute", ("laundry", "1h", "ryan"))
        self.assertEqual(r.headline, "muted laundry for 1h")
        self.assertGreater(self.rule.muted_until, 0)

    def test_a_cap_still_applies_and_unknown_people_are_refused(self):
        self.rule.mute_scope = "person"
        self.assertIn("limited to 1d", self.h.run("mute", ("laundry", "30d", "ryan")).headline)
        with self.assertRaises(CommandError):
            self.h.run("mute", ("laundry", "1h", "nobody"))

    def test_a_button_from_an_earlier_problem_is_refused(self):
        self.mon.engine.on_value("chest", -15.0, self.mon.clock())         # back to normal
        with self.assertRaises(CommandError) as cm:
            self.h.run("claim", ("laundry", "ryan", "ryan"))
        self.assertIn("back to normal", str(cm.exception))


class ConfigChecks(unittest.TestCase):
    def load(self, text):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "c.toml"
            p.write_text(text)
            return load_config(p)

    BASE = ('[contacts]\npaul = { ntfy = "paul-t" }\n[[ds18b20]]\nname="c"\nid="28-x"\n'
            '[[alerts]]\nname="a"\ntype="bounds"\nsource="c"\nop="greater_than"\nvalue=1\n')

    def test_claim_names_must_be_contacts(self):
        with self.assertRaises(ConfigError) as cm:
            self.load(self.BASE + 'claim = ["paul", "ghost"]\n')
        self.assertIn("ghost", str(cm.exception))
        for bad in ('claim = "paul"\n', 'claim = ["a","b","c","d","e"]\n'):
            with self.assertRaises(ConfigError):
                self.load(self.BASE + bad)

    def test_always_notify_names_are_checked_and_groups_are_allowed(self):
        with self.assertRaises(ConfigError) as cm:
            self.load(self.BASE + 'always_notify = ["ghost"]\n')
        self.assertIn("ghost", str(cm.exception))
        with self.assertRaises(ConfigError):
            self.load(self.BASE + 'always_notify = "paul"\n')
        ok = self.load('[groups]\nparents = ["paul"]\n' + self.BASE + 'always_notify = ["parents"]\n')
        self.assertEqual(ok.alerts[0]["always_notify"], ["parents"])

    def test_bad_alert_settings_fail_at_load_so_check_config_catches_them(self):
        for extra, expect in (('warn_s = 500\nerror_s = 100\n', "watch_s <= warn_s"), ('op = "bigger"\n', "unknown op")):
            with self.assertRaises(ConfigError, msg=extra) as cm:
                self.load(self.BASE.replace('op="greater_than"\n', "") + ('op="greater_than"\n' if "op" not in extra else "") + extra)
            self.assertIn(expect, str(cm.exception))

    def test_recovery_to_and_mute_scope_are_checked(self):
        with self.assertRaises(ConfigError) as cm:
            self.load(self.BASE + 'recovery_to = ["ghost"]\n')
        self.assertIn("recovery_to", str(cm.exception))
        with self.assertRaises(ConfigError):
            self.load(self.BASE + 'mute_scope = "everyone"\n')
        ok = self.load('[groups]\nhome = ["paul"]\n' + self.BASE + 'recovery_to = ["home"]\nmute_scope = "person"\n')
        self.assertEqual((ok.alerts[0]["recovery_to"], ok.alerts[0]["mute_scope"]), (["home"], "person"))

    def test_claim_without_ntfy_commands_is_warned_about(self):
        cfg = self.load(self.BASE + 'claim = ["paul"]\n[notify.channels.ntfy]\n')
        self.assertTrue(any("nobody can tap" in w for w in cfg.warnings), cfg.warnings)
        ok = self.load(self.BASE + 'claim = ["paul"]\n[notify.channels.ntfy]\ncommand_topic="cmd"\ncommand_secret="' + SECRET + '"\n')
        self.assertEqual(ok.warnings, [])


async def wait_for(predicate, timeout=8.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return True
        await asyncio.sleep(0.05)
    return False


class FlowBase(unittest.IsolatedAsyncioTestCase):
    """Separate topics per person; taps are real HTTP posts to a stand-in ntfy server."""
    config_text = CONFIG

    async def asyncSetUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.srv = NtfyServer().__enter__()
        self.addCleanup(self.srv.__exit__, None, None, None)
        ntfy = NtfyChannel("ntfy", {"server": self.srv.url, **OPTS})
        self.mon = make_monitor(self._tmp.name, self.config_text, dispatcher=Dispatcher([LogChannel(), ntfy]), time_scale=60)
        self.mon.fake_pi = FakePi(self.mon.cfg)
        self.stop = asyncio.Event()
        self.run_task = asyncio.create_task(self.mon.run(self.stop))
        await asyncio.sleep(0.4)

    async def asyncTearDown(self):
        self.stop.set()
        await self.run_task
        self.mon.store.close = lambda: None

    def titles(self, topic):
        return [m.get("title", "") for m in self.srv.messages.get(topic, [])]

    def last(self, topic, text):
        return next(m for m in reversed(self.srv.messages[topic]) if text in m.get("title", ""))

    async def has(self, topic, text):
        return await wait_for(lambda: any(text in t for t in self.titles(topic)))

    @property
    def rule(self):
        return self.mon.engine.rule("laundry")

    async def tap(self, message, index):
        await asyncio.to_thread(self.srv.press, message["actions"][index])

class FullFlow(FlowBase):
    async def test_the_question_goes_to_both_and_ryan_claims_it(self):
        self.mon.fake_pi.set_temp("chest", -5.0)
        self.assertTrue(await self.has("paul-topic", "OK -> WATCH"))
        self.assertTrue(await self.has("ryan-topic", "OK -> WATCH"))
        self.assertEqual([a["label"] for a in self.last("paul-topic", "OK -> WATCH")["actions"]], ["I'll do it", "Give to Ryan", "Status"])
        self.assertEqual([a["label"] for a in self.last("ryan-topic", "OK -> WATCH")["actions"]], ["I'll do it", "Give to Paul", "Status"])

        await self.tap(self.last("ryan-topic", "OK -> WATCH"), 0)                       # Ryan: "I'll do it"
        self.assertTrue(await wait_for(lambda: self.rule.claimed_by == "ryan"))
        self.assertTrue(await self.has("ryan-topic", "laundry: you have it"))       # Ryan's confirmation
        self.assertTrue(await self.has("paul-topic", "laundry: Ryan has it"))       # Paul is told it is handled
        self.assertIn("Ryan is doing it", self.last("paul-topic", "Ryan has it")["message"])

        before = len(self.srv.messages["paul-topic"])
        self.assertTrue(await self.has("ryan-topic", "WATCH -> WARN"))              # the reminder goes to Ryan...
        await asyncio.sleep(0.5)
        self.assertEqual(len(self.srv.messages["paul-topic"]), before)             # ...and not to Paul
        self.assertEqual([a["label"] for a in self.last("ryan-topic", "WARN")["actions"]], ["Mute 1h", "Release", "Status"])
        self.assertNotIn("mum-topic", self.srv.messages)

        self.mon.fake_pi.set_temp("chest", -15.0)                                  # the lid goes up: laundry done
        self.assertTrue(await self.has("ryan-topic", "back to normal"))
        await asyncio.sleep(0.5)
        self.assertEqual(len(self.srv.messages["paul-topic"]), before)             # the recovery went only to Ryan
        self.assertIsNone(self.rule.claimed_by)

        await self.tap(self.last("ryan-topic", "OK -> WATCH"), 0)                        # an old button after it is over
        self.assertTrue(await self.has("ryan-topic", "command not run"))
        self.assertIn("back to normal", self.last("ryan-topic", "command not run")["message"])

    async def test_paul_gives_it_to_ryan_and_ryan_hands_it_back(self):
        self.mon.fake_pi.set_temp("chest", -5.0)
        self.assertTrue(await self.has("paul-topic", "OK -> WATCH"))
        await self.tap(self.last("paul-topic", "OK -> WATCH"), 1)                        # Paul: "Give to Ryan"
        self.assertTrue(await wait_for(lambda: self.rule.claimed_by == "ryan"))
        self.assertTrue(await self.has("paul-topic", "laundry: Ryan has it"))       # Paul's reply
        self.assertTrue(await self.has("ryan-topic", "Paul gave it to Ryan"))       # Ryan is told it is his
        self.assertEqual([a["label"] for a in self.last("ryan-topic", "gave it")["actions"]], ["Release", "Status"])

        await self.tap(self.last("ryan-topic", "gave it"), 0)                      # Ryan: "Release"
        self.assertTrue(await wait_for(lambda: self.rule.claimed_by is None))
        self.assertTrue(await self.has("paul-topic", "back to everyone"))           # Paul is told it is open again

    async def test_claiming_twice_does_not_announce_twice(self):
        self.mon.fake_pi.set_temp("chest", -5.0)
        self.assertTrue(await self.has("ryan-topic", "OK -> WATCH"))
        watch = self.last("ryan-topic", "OK -> WATCH")
        await self.tap(watch, 0)
        self.assertTrue(await has_count(self, "paul-topic", "Ryan has it", 1))
        await self.tap(watch, 0)
        await asyncio.sleep(0.6)
        self.assertEqual(sum("Ryan has it" in t for t in self.titles("paul-topic")), 1)


CONFIG_HOUSEHOLD = CONFIG.replace(
    'claim = ["paul", "ryan"]\nrecipients = { watch = ["paul", "ryan"], warn = ["paul", "ryan"], error = ["paul", "ryan", "mum"] }',
    'claim = ["paul", "ryan", "mum"]\nalways_notify = ["paul"]\nmute_scope = "person"\nrecovery_to = ["home"]\n'
    'recipients = { watch = ["home"], warn = ["home"], error = ["home"] }').replace(
    'warn_s = 30\nerror_s = 6000', 'warn_s = 30\nerror_s = 60\nerror_retry_s = 30').replace(
    '[notify.levels]', '[groups]\nhome = ["paul", "ryan", "mum"]\n[notify.levels]')
assert "recovery_to" in CONFIG_HOUSEHOLD and "[groups]" in CONFIG_HOUSEHOLD

CONFIG_ALWAYS = CONFIG.replace(
    'claim = ["paul", "ryan"]\nrecipients = { watch = ["paul", "ryan"], warn = ["paul", "ryan"], error = ["paul", "ryan", "mum"] }',
    'claim = ["paul", "ryan", "mum"]\nalways_notify = ["paul"]\n'
    'recipients = { watch = ["ryan", "mum"], warn = ["ryan", "mum"], error = ["ryan", "mum"] }')
assert "always_notify" in CONFIG_ALWAYS


class AlwaysNotifyFlow(FlowBase):
    """Paul is not in the alert's own recipient lists, but always_notify keeps him in the loop."""
    config_text = CONFIG_ALWAYS

    async def test_paul_always_hears_it_and_a_claim_drops_everyone_else(self):
        self.mon.fake_pi.set_temp("chest", -5.0)
        for who in ("ryan-topic", "mum-topic", "paul-topic"):                       # Paul is not in `recipients`...
            self.assertTrue(await self.has(who, "OK -> WATCH"), who)               # ...and still gets the question
        self.assertEqual([a["label"] for a in self.last("paul-topic", "OK -> WATCH")["actions"]],
                         ["I'll do it", "Give to…", "Status"])

        await self.tap(self.last("ryan-topic", "OK -> WATCH"), 0)                  # Ryan takes it
        self.assertTrue(await wait_for(lambda: self.rule.claimed_by == "ryan"))
        self.assertTrue(await self.has("mum-topic", "Ryan has it"))                # Mum is told, once

        counts = {t: len(self.srv.messages[t]) for t in ("mum-topic", "paul-topic", "ryan-topic")}
        self.assertTrue(await self.has("ryan-topic", "WATCH -> WARN"))
        self.assertTrue(await self.has("paul-topic", "WATCH -> WARN"))             # Paul keeps getting the reminders
        await asyncio.sleep(0.5)
        self.assertEqual(len(self.srv.messages["mum-topic"]), counts["mum-topic"])  # Mum does not
        self.assertEqual([a["label"] for a in self.last("ryan-topic", "WATCH -> WARN")["actions"]], ["Mute 1h", "Release", "Status"])
        self.assertEqual([a["label"] for a in self.last("paul-topic", "WATCH -> WARN")["actions"]], ["I'll take over", "Release", "Status"])

        mum_before = len(self.srv.messages["mum-topic"])
        self.mon.fake_pi.set_temp("chest", -15.0)
        self.assertTrue(await self.has("ryan-topic", "back to normal"))
        self.assertTrue(await self.has("paul-topic", "back to normal"))             # the recovery too
        await asyncio.sleep(0.5)
        self.assertEqual(len(self.srv.messages["mum-topic"]), mum_before)

    async def test_paul_can_take_over_from_the_claimer(self):
        self.mon.fake_pi.set_temp("chest", -5.0)
        self.assertTrue(await self.has("ryan-topic", "OK -> WATCH"))
        await self.tap(self.last("ryan-topic", "OK -> WATCH"), 0)
        self.assertTrue(await self.has("paul-topic", "WATCH -> WARN"))
        await self.tap(self.last("paul-topic", "WATCH -> WARN"), 0)                 # Paul: "I'll take over"
        self.assertTrue(await wait_for(lambda: self.rule.claimed_by == "paul"))
        self.assertTrue(await self.has("ryan-topic", "Paul has it"))                # Ryan is told he has been replaced


class HouseholdFlow(FlowBase):
    """Ryan takes the laundry and mutes himself; Paul still hears it; everyone hears when it is done."""
    config_text = CONFIG_HOUSEHOLD

    async def test_personal_mute_and_everyone_told_when_it_is_done(self):
        self.mon.fake_pi.set_temp("chest", -5.0)
        for who in ("paul-topic", "ryan-topic", "mum-topic"):
            self.assertTrue(await self.has(who, "OK -> WATCH"), who)
        await self.tap(self.last("ryan-topic", "OK -> WATCH"), 0)                  # Ryan: "I'll do it"
        self.assertTrue(await wait_for(lambda: self.rule.claimed_by == "ryan"))
        self.assertTrue(await self.has("ryan-topic", "WATCH -> WARN"))
        mute = self.last("ryan-topic", "WATCH -> WARN")
        self.assertEqual(mute["actions"][0]["label"], "Mute 1h")

        await self.tap(mute, 0)                                                    # Ryan mutes himself for an hour
        self.assertTrue(await wait_for(lambda: self.rule.is_muted_for("ryan", self.mon.clock())))
        self.assertIsNone(next(iter(self.rule.muted_for.keys() - {"ryan"}), None))   # nobody else is muted
        self.assertEqual(self.rule.muted_until, 0.0)
        self.assertTrue(await self.has("ryan-topic", "muted laundry for you"))
        ryan_after = len(self.srv.messages["ryan-topic"])
        paul_after = len(self.srv.messages["paul-topic"])

        reminders = lambda: sum("still ERROR" in t for t in self.titles("paul-topic"))
        base = reminders()
        self.assertTrue(await wait_for(lambda: reminders() >= base + 2))           # Paul keeps getting reminders
        self.assertGreater(len(self.srv.messages["paul-topic"]), paul_after)
        self.assertEqual(len(self.srv.messages["ryan-topic"]), ryan_after)         # Ryan, muted, gets none

        self.mon.fake_pi.set_temp("chest", -15.0)                                  # the lid goes up: it is done
        for who in ("ryan-topic", "paul-topic", "mum-topic"):                      # everyone is told, Mum too
            self.assertTrue(await self.has(who, "back to normal"), who)
        self.assertIn("Taken by Ryan", self.last("mum-topic", "back to normal")["message"])


class FakeModeSinglePhone(unittest.IsolatedAsyncioTestCase):
    async def test_one_test_phone_can_walk_through_the_claim_flow(self):
        with NtfyServer() as srv, tempfile.TemporaryDirectory() as tmp:
            channel = FakeModeChannel(NtfyChannel("ntfy", {"server": srv.url, **OPTS}), ["my-test-topic"])
            mon = make_monitor(tmp, dispatcher=Dispatcher([LogChannel(), channel]), time_scale=60)
            mon.fake_pi = FakePi(mon.cfg)
            stop = asyncio.Event()
            run = asyncio.create_task(mon.run(stop))
            try:
                titles = lambda: [m.get("title", "") for m in srv.messages.get("my-test-topic", [])]
                await asyncio.sleep(0.4)
                mon.fake_pi.set_temp("chest", -5.0)
                self.assertTrue(await wait_for(lambda: any("OK -> WATCH" in t for t in titles())))
                watch = next(m for m in srv.messages["my-test-topic"] if "OK -> WATCH" in m["title"])
                self.assertEqual([a["label"] for a in watch["actions"]], ["I'll do it", "Give to Ryan", "Status"])
                self.assertTrue(watch["actions"][0]["url"].endswith("/cmd-topic-fake"))     # never the real command topic
                await asyncio.to_thread(srv.press, watch["actions"][0])
                self.assertTrue(await wait_for(lambda: mon.engine.rule("laundry").claimed_by == "paul"))
                self.assertTrue(await wait_for(lambda: any("you have it" in t for t in titles())))
                self.assertTrue(await wait_for(lambda: any("WATCH -> WARN" in t for t in titles())))
                warn = next(m for m in srv.messages["my-test-topic"] if "WATCH -> WARN" in m["title"])
                self.assertEqual([a["label"] for a in warn["actions"]], ["Mute 1h", "Release", "Status"])
                self.assertTrue(warn["title"].startswith("[FAKE-PI TEST]"))
            finally:
                stop.set()
                await run


async def has_count(test, topic, text, n):
    return await wait_for(lambda: sum(text in t for t in test.titles(topic)) >= n)


if __name__ == "__main__":
    unittest.main()
