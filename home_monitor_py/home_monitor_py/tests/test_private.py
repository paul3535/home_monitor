import os
import stat
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from home_monitor.__main__ import main
from home_monitor.config import ConfigError, load_config

CONFIG = """
[system]
name = "t"
{extra_system}
[contacts]
paul = "placeholder@example.com"
[notify]
report_to = ["paul"]
[notify.levels]
warn = ["paul"]
error = ["{error_names}"]
[notify.channels.email]
enabled = true
smtp_host = "smtp.example.com"
from_addr = "placeholder@example.com"
[notify.channels.ntfy]
enabled = true
"""

PRIVATE = """
[contacts]
paul = { email = "paul@real.test", ntfy = "paul-topic" }
mum = "mum@real.test"
dad = { email = "dad@real.test", ntfy = "dad-topic" }
[groups]
family = ["paul", "mum", "dad"]
[notify.channels.email]
from_addr = "paul@real.test"
[notify.channels.ntfy]
server = "https://ntfy.real.test"
"""


def emails(cfg, names):
    return [c.addresses["email"] for c in cfg.resolve(names)]


class PrivateFile(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self._tmp.name)
        self.addCleanup(self._tmp.cleanup)

    def write(self, name, text, mode=0o600):
        p = self.dir / name
        p.write_text(text)
        p.chmod(mode)
        return p

    def config(self, error_names="family", extra_system='private_file = "private.toml"'):
        return self.write("config.toml", CONFIG.format(error_names=error_names, extra_system=extra_system))

    def test_contacts_groups_and_channel_details_come_from_the_private_file(self):
        self.write("private.toml", PRIVATE)
        cfg = load_config(self.config())
        self.assertEqual(emails(cfg, ["family"]), ["paul@real.test", "mum@real.test", "dad@real.test"])
        self.assertEqual(cfg.channels["email"]["from_addr"], "paul@real.test")        # private wins
        self.assertEqual(cfg.channels["email"]["smtp_host"], "smtp.example.com")      # merged, not replaced
        self.assertEqual(cfg.channels["ntfy"]["server"], "https://ntfy.real.test")
        self.assertEqual(cfg.contacts["paul"], {"email": "paul@real.test", "ntfy": "paul-topic"})
        self.assertEqual(cfg.contacts["mum"], {"email": "mum@real.test"})             # a string is an email
        self.assertEqual(cfg.warnings, [])

    def test_private_value_beats_the_placeholder_in_the_config(self):
        self.write("private.toml", PRIVATE)
        self.assertEqual(emails(load_config(self.config()), ["paul"]), ["paul@real.test"])

    def test_groups_expand_without_duplicates(self):
        self.write("private.toml", PRIVATE)
        cfg = load_config(self.config())
        self.assertEqual(emails(cfg, ["paul", "family"]), ["paul@real.test", "mum@real.test", "dad@real.test"])

    def test_several_configs_share_one_private_file_with_different_recipients(self):
        self.write("private.toml", PRIVATE)
        mine = load_config(self.write("mine.toml", CONFIG.format(error_names="paul", extra_system='private_file = "private.toml"')))
        parents = load_config(self.write("parents.toml", CONFIG.format(error_names="family", extra_system='private_file = "private.toml"')))
        self.assertEqual(emails(mine, mine.level_contacts["error"]), ["paul@real.test"])
        self.assertEqual(len(parents.resolve(parents.level_contacts["error"])), 3)

    def test_precedence_cli_then_env_then_config(self):
        self.write("private.toml", PRIVATE)
        self.write("from_env.toml", PRIVATE.replace("paul@real.test", "env@real.test"))
        self.write("from_cli.toml", PRIVATE.replace("paul@real.test", "cli@real.test"))
        config = self.config()
        with mock.patch.dict(os.environ, {"HOME_MONITOR_PRIVATE": str(self.dir / "from_env.toml")}):
            self.assertEqual(emails(load_config(config), ["paul"]), ["env@real.test"])
            self.assertEqual(emails(load_config(config, self.dir / "from_cli.toml"), ["paul"]), ["cli@real.test"])

    def test_missing_private_file_is_an_error_not_a_silent_fallback(self):
        with self.assertRaises(ConfigError) as cm:
            load_config(self.config())
        self.assertIn("private file not found", str(cm.exception))

    def test_unknown_name_error_says_where_to_add_it(self):
        self.write("private.toml", PRIVATE)
        with self.assertRaises(ConfigError) as cm:
            load_config(self.config(error_names="grandma"))
        self.assertIn("grandma", str(cm.exception))
        self.assertIn("private.toml", str(cm.exception))

    def test_bad_private_files_are_rejected(self):
        for text in ("[contact]\nx='y'\n",                                   # typo'd section
                     "[notify.levels]\nerror=['paul']\n",                   # recipients belong in each config
                     "[notify.email]\nfrom_addr='x'\n",                     # old form: now [notify.channels.email]
                     "[contacts]\npaul=5\n",                                # not an address or a table
                     "[contacts]\npaul='a@b'\n[groups]\npaul=['paul']\n",   # group shadows a contact
                     "[contacts]\na='a@b'\n[groups]\ng=['zed']\n",          # group member does not exist
                     "this is not toml"):
            self.write("private.toml", text)
            with self.assertRaises(ConfigError, msg=text):
                load_config(self.config(error_names="paul"))

    def test_world_readable_private_file_gets_a_warning(self):
        self.write("private.toml", PRIVATE, mode=0o644)
        cfg = load_config(self.config())
        self.assertEqual(len(cfg.warnings), 1)
        self.assertIn("chmod 600", cfg.warnings[0])

    def test_describe_names_recipients_but_never_prints_addresses(self):
        self.write("private.toml", PRIVATE)
        text = "\n".join(load_config(self.config()).describe())
        self.assertIn("error -> family (3 people: email 3, ntfy 2)", text)   # dad has no ntfy topic: visible
        self.assertIn("channels: email (smtp.example.com:587), ntfy (https://ntfy.real.test)", text)
        self.assertNotIn("@", text)
        self.assertNotIn("topic", text.replace("ntfy topic", ""))

    def test_cli_check_config_works_with_the_private_flag(self):
        self.write("private.toml", PRIVATE)
        config = self.config(extra_system="")
        with mock.patch("builtins.print") as pr:
            self.assertEqual(main(["-c", str(config), "--private", str(self.dir / "private.toml"), "--check-config"]), 0)
        printed = " ".join(str(c.args[0]) for c in pr.call_args_list)
        self.assertIn("private.toml", printed)
        self.assertNotIn("@real.test", printed)
        self.assertNotIn("paul-topic", printed)


class NewUser(unittest.TestCase):
    def run_main(self, *names):
        import contextlib
        import io
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = main(["--new-user", *names])
        return code, out.getvalue(), err.getvalue()

    def test_prints_a_loadable_contact_with_a_long_random_topic_for_each_person(self):
        code, out, _ = self.run_main("steph", "Ryan", "kimmie")
        self.assertEqual(code, 0)
        lines = [l for l in out.splitlines() if l and not l.startswith("#")]
        self.assertEqual([l.split(" = ")[0] for l in lines], ["steph", "ryan", "kimmie"])   # names are lower-cased
        with tempfile.TemporaryDirectory() as d:                     # pasting them into private.toml works
            p = Path(d) / "c.toml"
            p.write_text("[contacts]\n" + "\n".join(lines) + "\n")
            cfg = load_config(p)
        topics = [cfg.contacts[n]["ntfy"] for n in ("steph", "ryan", "kimmie")]
        self.assertEqual(len(set(topics)), 3)
        self.assertTrue(all(t.split("-", 1)[0] == n and len(t) >= 25 for t, n in zip(topics, ("steph", "ryan", "kimmie"))))
        self.assertIn(f"ntfy://ntfy.sh/{topics[0]}", out)

    def test_two_runs_give_different_topics(self):
        self.assertNotEqual(self.run_main("steph")[1], self.run_main("steph")[1])

    def test_bad_names_are_refused(self):
        for names in (["Bad Name"], ["a", "a"], ["1abc"], ["x" * 40], ["a/b"]):
            code, out, err = self.run_main(*names)
            self.assertEqual((code, out), (2, ""))
            self.assertIn("names must be unique", err)


class ConfigShapes(unittest.TestCase):
    def load(self, text):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "c.toml"
            p.write_text(text)
            return load_config(p)

    def test_old_notify_sections_get_a_migration_message(self):
        for legacy in ("email", "ntfy"):
            with self.assertRaises(ConfigError) as cm:
                self.load(f"[notify.{legacy}]\nenabled = true\n")
            self.assertIn(f"[notify.channels.{legacy}]", str(cm.exception))

    def test_channel_name_defaults_to_its_type_and_names_can_be_reused(self):
        cfg = self.load('[notify.channels.email]\nsmtp_host="h"\n'
                        '[notify.channels.alerts_phone]\ntype="ntfy"\n'
                        '[notify.channels.off]\ntype="email"\nenabled=false\n')
        self.assertEqual(sorted(cfg.channels), ["alerts_phone", "email", "off"])
        self.assertIn("channels: email (h:587), ntfy 'alerts_phone'", " ".join(cfg.describe()))   # disabled one is hidden

    def test_bad_channels_are_rejected_with_the_channel_name(self):
        for text, expect in (('[notify.channels.x]\ntype="pager"\n', "unknown type"),
                             ('[notify.channels.email]\nsmtp_port=25\n', "needs smtp_host"),
                             ('[notify.channels.email]\nsmtp_host="h"\nsmpt_port=1\n', "unknown option"),
                             ('[notify.channels.ntfy]\npriorities={error=9}\n', "1 to 5"),
                             ('[notify.channels.ntfy]\nlevels=["panic"]\n', "levels must be")):
            with self.assertRaises(ConfigError, msg=text) as cm:
                self.load(text)
            self.assertIn(expect, str(cm.exception))

    def test_contact_addresses_are_checked_without_echoing_secrets(self):
        ok = self.load('[contacts]\nme = { email = "a@b.c", ntfy = "example_Topic_Mixed-Case-9" }\n')
        self.assertEqual(ok.contacts["me"]["ntfy"], "example_Topic_Mixed-Case-9")
        for bad_ntfy in ("has$dollar", "has@at", "has space", "x" * 65):
            with self.assertRaises(ConfigError) as cm:
                self.load(f'[contacts]\nme = {{ ntfy = "{bad_ntfy}" }}\n')
            self.assertIn("letters, digits", str(cm.exception))
            self.assertNotIn(bad_ntfy, str(cm.exception))          # a topic is a secret: never print it
        with self.assertRaises(ConfigError):
            self.load('[contacts]\nme = "not-an-address"\n')

    def test_alert_recipients_override_levels(self):
        cfg = self.load('[contacts]\na="a@x"\nb="b@x"\n[notify.levels]\nerror=["a"]\n')
        self.assertEqual([c.name for c in cfg.recipients_for(None, "error")], ["a"])
        self.assertEqual([c.name for c in cfg.recipients_for({"error": ["b"]}, "error")], ["b"])
        self.assertEqual([c.name for c in cfg.recipients_for({"warn": ["b"]}, "error")], ["a"])   # no override for this level
        self.assertEqual(cfg.recipients_for(None, "watch"), [])


if __name__ == "__main__":
    unittest.main()
