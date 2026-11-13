"""Remote commands (status, mute, unmute) and the signed messages that carry them over ntfy.

A notification's buttons post a message to a private command topic; the monitor polls that topic.
Each message is signed (HMAC-SHA256) with a secret that only the monitor and the buttons it made
know, and it expires, so knowing or guessing the topic name is not enough to command anything.

    v1|<expires>|<reply topic>|<command>|<args...>|<signature>      (each field URL-quoted)

Mutes are capped (max_mute), recoveries are never muted, and every command is logged.
"""
from __future__ import annotations

import hashlib
import hmac
import logging
import re
import time
from dataclasses import dataclass
from urllib.parse import quote, unquote

log = logging.getLogger(__name__)

_DURATION = re.compile(r"^(\d+)([smhd])$")
_UNITS = {"s": 1, "m": 60, "h": 3600, "d": 86400}


def parse_duration(text: str) -> float:
    """'90s', '30m', '2h', '1d' -> seconds."""
    m = _DURATION.match(str(text).strip().lower())
    if not m or int(m.group(1)) == 0:
        raise ValueError(f"bad duration {text!r}; use a number and s, m, h or d (e.g. 30m, 2h)")
    return int(m.group(1)) * _UNITS[m.group(2)]


def fmt_duration(seconds: float) -> str:
    seconds = int(round(seconds))
    if seconds < 60:
        return f"{seconds}s"
    days, rest = divmod(seconds, 86400)
    hours, rest = divmod(rest, 3600)
    minutes = rest // 60
    return " ".join(f"{n}{u}" for n, u in ((days, "d"), (hours, "h"), (minutes, "m")) if n) or "0m"


class CommandError(Exception):
    """The command was understood but cannot be done; the message is shown to the person."""


class CommandRejected(Exception):
    """A message that must not be run. ``reply`` is set only when the signature was valid."""

    def __init__(self, reason: str, reply: str | None = None) -> None:
        super().__init__(reason)
        self.reason, self.reply = reason, reply


@dataclass(frozen=True)
class Command:
    name: str
    args: tuple
    reply: str
    expires: int


@dataclass(frozen=True)
class CommandResult:
    headline: str
    body: str = ""
    undo: tuple | None = None   # a command to offer as an "Unmute" button on the reply


def _sign(secret: str, body: str) -> str:
    return hmac.new(secret.encode(), body.encode(), hashlib.sha256).hexdigest()[:32]


def encode(secret: str, name: str, args, reply: str, expires: float) -> str:
    body = "|".join(quote(str(p), safe="") for p in ("v1", int(expires), reply, name, *args))
    return f"{body}|{_sign(secret, body)}"


def decode(secret: str, text: str, now: float) -> Command:
    body, _, sig = text.strip().rpartition("|")
    if not body or not hmac.compare_digest(sig, _sign(secret, body)):
        raise CommandRejected("bad signature")          # forged or garbled: never answer these
    parts = [unquote(p) for p in body.split("|")]
    reply = parts[2] if len(parts) > 2 else None
    if len(parts) < 4 or parts[0] != "v1" or not parts[1].isdigit():
        raise CommandRejected("unsupported command format", reply)
    if now > int(parts[1]):
        raise CommandRejected("that button has expired; use one on a newer notification", reply)
    return Command(parts[3], tuple(parts[4:]), parts[2], int(parts[1]))


class CommandHandler:
    """Runs commands against a Monitor. Everything here runs on the event loop thread."""

    def __init__(self, monitor, max_mute_s: float = 86400) -> None:
        self.monitor, self.max_mute_s = monitor, max_mute_s

    def run(self, name: str, args) -> CommandResult:
        args = tuple(args)
        if name == "status" and not args:
            return self._status()
        if name == "mute" and len(args) == 2:
            return self._mute(*args)
        if name == "unmute" and len(args) == 1:
            return self._unmute(args[0])
        raise CommandError(f"unknown command {name!r} {list(args)}; try: status | mute <alert|all> <30m|2h> | unmute <alert|all>")

    def _targets(self, rule: str) -> str | None:
        if rule == "all":
            return None
        if rule not in {r.name for r in self.monitor.engine.rules}:
            raise CommandError(f"no alert called {rule!r}")
        return rule

    def _mute(self, rule: str, duration: str) -> CommandResult:
        target = self._targets(rule)
        try:
            seconds = parse_duration(duration)
        except ValueError as exc:
            raise CommandError(str(exc)) from exc
        capped = seconds > self.max_mute_s
        seconds = min(seconds, self.max_mute_s)
        count = self.monitor.engine.mute(self.monitor.clock(), seconds, target)
        what = "every alert" if target is None else rule
        log.warning("command: muted %s for %s", what, fmt_duration(seconds))
        note = f" (limited to {fmt_duration(self.max_mute_s)})" if capped else ""
        return CommandResult(f"muted {what} for {fmt_duration(seconds)}{note}",
                             f"{count} alert{'' if count == 1 else 's'} silenced. Recoveries still come through, "
                             "and reminders resume when the mute ends if the problem is still there.",
                             undo=("unmute", rule))

    def _unmute(self, rule: str) -> CommandResult:
        target = self._targets(rule)
        count = self.monitor.engine.unmute(target)
        log.warning("command: unmuted %s", "every alert" if target is None else rule)
        return CommandResult(f"unmuted {'every alert' if target is None else rule}", f"{count} alert{'' if count == 1 else 's'} active again.")

    def _status(self) -> CommandResult:
        m = self.monitor
        now_wall, lines = time.time(), []
        lines.append("Temperatures")
        for d in m.cfg.ds18b20:
            got = m.latest.get(d["name"])
            lines.append(f"  {d['name']}: " + (f"{got[0]:.1f} C ({fmt_duration(now_wall - got[1])} ago)" if got else "no reading yet"))
        if m.devices:
            lines.append("Devices")
            lines += [f"  {dev.name}: {dev.published or 'waiting for first reading'}" for dev in m.devices]
        lines.append("Alerts")
        snap = m.engine.snapshot(m.clock())
        active = [a for a in snap if a["state"] != "ok" or a["muted"]]
        if not active:
            lines.append("  all OK")
        for a in active:
            mute = f" [muted, {fmt_duration(a['muted_for_s'])} left]" if a["muted"] else ""
            lines.append(f"  {a['rule']}: {a['state'].upper()}{mute}" + (f" ({a['detail']})" if a["state"] != "ok" else ""))
        return CommandResult("status", "\n".join(lines))
