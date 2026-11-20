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
    buttons: tuple = ()   # (label, command, args) offered on the reply, at most 3


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
        if name == "mute" and len(args) in (2, 3):
            return self._mute(*args)
        if name == "unmute" and len(args) in (1, 2):
            return self._unmute(*args)
        if name == "claim" and len(args) in (2, 3):
            return self._claim(*args)
        if name == "release" and len(args) in (1, 2):
            return self._release(*args)
        if name == "choose" and len(args) in (1, 2):
            return self._choose(*args)
        raise CommandError(f"unknown command {name!r} {list(args)}; try: status | mute <alert|all> <30m|2h> | "
                           "unmute <alert|all> | claim <alert> <person> | release <alert>")

    def _targets(self, rule: str) -> str | None:
        if rule == "all":
            return None
        if rule not in {r.name for r in self.monitor.engine.rules}:
            raise CommandError(f"no alert called {rule!r}")
        return rule

    def _claimable(self, rule: str):
        """The rule, if it can be claimed and there is something to claim."""
        found = next((r for r in self.monitor.engine.rules if r.name == rule), None)
        if found is None:
            raise CommandError(f"no alert called {rule!r}")
        if not found.claim:
            raise CommandError(f"{rule} cannot be claimed")
        if found.state.value == "ok":
            raise CommandError(f"nothing to do: {rule} is back to normal")   # e.g. a button from earlier
        return found

    def _person(self, name: str) -> str:
        if name and name not in self.monitor.cfg.contacts:
            raise CommandError(f"I don't know {name!r}")
        return name

    def _claim(self, rule: str, who: str, by: str = "") -> CommandResult:
        r = self._claimable(rule)
        by = self._person(by) or who
        if who not in r.claim:
            raise CommandError(f"{who} is not one of the people who can take {rule}")
        previous = r.claimed_by
        r.claimed_by = who
        log.warning("command: %s %s %s", by, "claimed" if by == who else f"gave it to {who}:", rule)
        self.monitor.announce_claim(r, who, by, previous)
        name = who.capitalize()
        return CommandResult(f"{r.label}: {name} has it" if by != who else f"{r.label}: you have it",
                             f"Reminders for this alert now go only to {name}. It clears by itself when the problem is over.",
                             buttons=(("Release", "release", (rule, by)), ("Mute 1h", "mute", (rule, "1h", by))))

    def _release(self, rule: str, by: str = "") -> CommandResult:
        r = self._claimable(rule)
        by = self._person(by)
        previous, r.claimed_by = r.claimed_by, None
        log.warning("command: %s released %s", by or "someone", rule)
        self.monitor.announce_claim(r, None, by, previous)
        return CommandResult(f"{r.label}: back to everyone", "Reminders go to everyone again.")

    def _choose(self, rule: str, by: str = "") -> CommandResult:
        """The 'Give to...' menu: one button per person, because a notification only holds 3 buttons."""
        r = self._claimable(rule)
        by = self._person(by)
        others = [p for p in r.claim if p != by][:3]
        return CommandResult(f"{r.label}: give it to whom?", "Pick a person; reminders will go only to them.",
                             buttons=tuple((f"Give to {p.capitalize()}", "claim", (rule, p, by)) for p in others))

    def _name_of(self, target: str | None) -> str:
        """The words for an alert in a reply: its display_name, or "every alert"."""
        found = next((r for r in self.monitor.engine.rules if r.name == target), None)
        return "every alert" if target is None else found.label if found else target

    def _scope(self, target: str | None, who: str) -> bool:
        """Is this a personal mute? Only for an alert with mute_scope "person" and a known person."""
        found = next((r for r in self.monitor.engine.rules if r.name == target), None)
        return bool(who) and found is not None and found.mute_scope == "person"

    def _mute(self, rule: str, duration: str, who: str = "") -> CommandResult:
        target = self._targets(rule)
        who = self._person(who)
        try:
            seconds = parse_duration(duration)
        except ValueError as exc:
            raise CommandError(str(exc)) from exc
        capped = seconds > self.max_mute_s
        seconds = min(seconds, self.max_mute_s)
        personal = self._scope(target, who)
        count = self.monitor.engine.mute(self.monitor.clock(), seconds, target, who or None)
        what = self._name_of(target)
        note = f" (limited to {fmt_duration(self.max_mute_s)})" if capped else ""
        if personal:
            log.warning("command: %s muted %s for %s", who, what, fmt_duration(seconds))
            return CommandResult(f"muted {what} for you, {fmt_duration(seconds)}{note}",
                                 "Only your messages about this alert are paused; everyone else still gets theirs. "
                                 "Recoveries still come through.", buttons=(("Unmute", "unmute", (rule, who)),))
        log.warning("command: muted %s for %s", what, fmt_duration(seconds))
        return CommandResult(f"muted {what} for {fmt_duration(seconds)}{note}",
                             f"{count} alert{'' if count == 1 else 's'} silenced. Recoveries still come through, "
                             "and reminders resume when the mute ends if the problem is still there.",
                             buttons=(("Unmute", "unmute", (rule,)),))

    def _unmute(self, rule: str, who: str = "") -> CommandResult:
        target = self._targets(rule)
        who = self._person(who)
        personal = self._scope(target, who)
        count = self.monitor.engine.unmute(target, who or None)
        what = self._name_of(target)
        log.warning("command: unmuted %s%s", what, f" for {who}" if personal else "")
        return CommandResult(f"unmuted {what}{' for you' if personal else ''}",
                             f"{count} alert{'' if count == 1 else 's'} active again.")

    def _status(self) -> CommandResult:
        m = self.monitor
        now_wall, lines = time.time(), []
        lines.append("Temperatures")
        for d in m.cfg.ds18b20:
            got = m.latest.get(d["name"])
            lines.append(f"  {m.cfg.label(d['name'])}: " + (f"{got[0]:.1f} C ({fmt_duration(now_wall - got[1])} ago)" if got else "no reading yet"))
        if m.devices:
            lines.append("Devices")
            lines += [f"  {m.cfg.label(dev.name)}: {dev.published or 'waiting for first reading'}" for dev in m.devices]
        lines.append("Alerts")
        snap = m.engine.snapshot(m.clock())
        active = [a for a in snap if a["state"] != "ok" or a["muted"] or a.get("muted_people")]
        if not active:
            lines.append("  all OK")
        for a in active:
            mute = f" [muted, {fmt_duration(a['muted_for_s'])} left]" if a["muted"] else ""
            claim = f" [{a['claimed_by']} has it]" if a.get("claimed_by") else ""
            mute += "".join(f" [{w} muted, {fmt_duration(left)} left]" for w, left in a.get("muted_people", {}).items())
            lines.append(f"  {a.get('label', a['rule'])}: {a['state'].upper()}{claim}{mute}" + (f" ({a['detail']})" if a["state"] != "ok" else ""))
        return CommandResult("status", "\n".join(lines))
