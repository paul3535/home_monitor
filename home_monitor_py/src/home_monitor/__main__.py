from __future__ import annotations

import argparse
import asyncio
import sys
import tempfile
from pathlib import Path

from .app import Monitor, setup_logging
from .config import ConfigError, load_config
from .notify import valid_test_target


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(prog="home-monitor")
    p.add_argument("-c", "--config", default="config.toml")
    p.add_argument("--private", metavar="FILE", help="secrets file (contacts, groups, ntfy topic); "
                   "overrides $HOME_MONITOR_PRIVATE and [system] private_file")
    p.add_argument("--simulate", action="store_true", help="random fake sensors; no hardware needed")
    p.add_argument("--fake-pi", action="store_true",
                   help="real code against a fake Pi, controlled from a web page; alerts only go to the log "
                        "unless you add --fake-send-to or --fake-send-real")
    p.add_argument("--time-scale", type=float, default=1.0, help="with --fake-pi: run alert timers N times faster")
    p.add_argument("--fake-host", default="127.0.0.1", help="control panel address (it has no password)")
    p.add_argument("--fake-port", type=int, default=8099)
    p.add_argument("--fake-send-to", nargs="+", metavar="TARGET",
                   help="with --fake-pi: really send through your enabled channels, but only to these email "
                        "addresses and/or ntfy topic names (messages say who would really have been told)")
    p.add_argument("--fake-send-real", action="store_true",
                   help="with --fake-pi: really send to the REAL recipients on every enabled channel (asks you to confirm)")
    p.add_argument("--check-config", action="store_true", help="validate the config and exit")
    p.add_argument("-v", "--verbose", action="store_true")
    args = p.parse_args(argv)
    if args.simulate and args.fake_pi:
        p.error("choose --simulate or --fake-pi, not both")
    if args.time_scale != 1.0 and not args.fake_pi:
        p.error("--time-scale only works with --fake-pi")
    if args.time_scale < 1.0:
        p.error("--time-scale must be 1 or more")
    if (args.fake_send_to or args.fake_send_real) and not args.fake_pi:
        p.error("--fake-send-to and --fake-send-real only work with --fake-pi")
    if args.fake_send_to and args.fake_send_real:
        p.error("choose --fake-send-to or --fake-send-real, not both")
    if args.fake_send_to and not all(valid_test_target(a) for a in args.fake_send_to):
        p.error("--fake-send-to takes email addresses and/or ntfy topic names (letters, digits, - and _)")
    try:
        cfg = load_config(args.config, args.private)
    except (OSError, ConfigError) as exc:
        print(f"config error: {exc}", file=sys.stderr)
        return 2
    for warning in cfg.warnings:
        print(f"warning: {warning}", file=sys.stderr)
    if args.check_config:
        print("OK: " + "\n    ".join(cfg.describe()))
        return 0

    fake = server = None
    dispatcher = None
    if args.fake_pi:
        from .fakepi import FakePi, start_control_server
        from .notify import Dispatcher, FakeModeChannel, LogChannel, build_channels
        # Keep test data away from the real database and logs, and never message anyone by default.
        scratch = Path(tempfile.mkdtemp(prefix="fakepi_data_"))
        cfg.db_path, cfg.log_dir = scratch / "fake.db", scratch / "logs"
        cfg.daily_report_hour, cfg.send_startup = None, False
        notify_label = "the log only (nothing is sent)"
        channels: list = [LogChannel()]
        if args.fake_send_to or args.fake_send_real:
            real = build_channels(cfg.channels)
            if not real:
                print("Nothing to send with: enable a channel under [notify.channels.*] in the config "
                      "(email needs smtp_host; ntfy works with defaults).", file=sys.stderr)
                return 2
            for c in real:
                if c.kind == "email" and not c.credentials():
                    print(f"note: {c.label} has no login (user/password in the private file, or "
                          "HOME_MONITOR_SMTP_USER / HOME_MONITOR_SMTP_PASSWORD); sending without logging in.", file=sys.stderr)
            if args.fake_send_real:
                who = ", ".join(sorted(cfg.contacts)) or "(no contacts)"
                print(f"--fake-send-real will message the REAL recipients from your config: {who}")
                if not sys.stdin.isatty() or input("Fake alerts will reach them. Type 'send' to continue: ").strip() != "send":
                    print("not confirmed; exiting.", file=sys.stderr)
                    return 1
            wrapped = [FakeModeChannel(c, args.fake_send_to) for c in real]
            channels += wrapped
            notify_label = "; ".join(c.label for c in wrapped) + " (max 10 a minute each, marked [FAKE-PI TEST])"
        dispatcher = Dispatcher(channels)
        fake = FakePi(cfg)
    setup_logging(cfg, args.verbose)
    monitor = Monitor(cfg, simulate=args.simulate, dispatcher=dispatcher, fake_pi=fake,
                      time_scale=args.time_scale)
    if fake:
        monitor.notify_label = notify_label
        server = start_control_server(fake, monitor, args.fake_host, args.fake_port)
        print(f"FAKE PI: control panel at http://{args.fake_host}:{args.fake_port}  "
              f"(time x{args.time_scale:g}; alerts go to {notify_label})\n"
              f"         fake 1-wire files: {fake.w1_root}\n         scratch data: {scratch}", flush=True)
    try:
        asyncio.run(monitor.run())
    finally:
        if server:
            server.shutdown()
            server.server_close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
