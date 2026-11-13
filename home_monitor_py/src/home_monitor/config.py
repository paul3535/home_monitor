"""Load config.toml and build the alert rules from it."""
from __future__ import annotations

import os
import tomllib
from dataclasses import dataclass, field
from pathlib import Path

from .alerts import AlertRule, BoundsRule, DoorClosingFailureRule, Escalation, StaleRule, StateRule
from .devices import DerivedDevice, GarageDoor, WashingMachine
from .models import AlertState
from .notify.base import Contact
from .notify.ntfy import TOPIC
from .notify.registry import build_channels

LEVELS = ("watch", "warn", "error")
PRIVATE_KEYS = {"contacts", "groups", "notify"}
PRIVATE_NOTIFY_KEYS = {"channels"}  # who gets told stays in each config; only channel details go here
LEGACY_HINT = ("[notify.{0}] has moved: write [notify.channels.{0}] instead "
               "(see config.example.toml; the keys are the same, plus optional levels/kinds)")
DEVICE_KEYS = {"garage_door": ("up", "down"), "washing_machine": ("lid_up",)}


class ConfigError(ValueError):
    pass


@dataclass
class Config:
    system_name: str = "home"
    db_path: Path = Path("data/home_monitor.db")
    log_dir: Path = Path("logs")
    log_level: str = "INFO"
    tick_seconds: float = 1.0
    retention_days: int = 365
    send_startup: bool = False
    daily_report_hour: int | None = None
    contacts: dict[str, dict] = field(default_factory=dict)   # name -> {channel kind: address}
    groups: dict[str, list[str]] = field(default_factory=dict)
    private_file: Path | None = None
    warnings: list[str] = field(default_factory=list)
    level_contacts: dict[str, list[str]] = field(default_factory=dict)
    report_to: list[str] = field(default_factory=list)
    channels: dict[str, dict] = field(default_factory=dict)   # name -> options, from [notify.channels.*]
    ds18b20: list[dict] = field(default_factory=list)
    switches: list[dict] = field(default_factory=list)
    devices: list[dict] = field(default_factory=list)
    alerts: list[dict] = field(default_factory=list)

    def resolve(self, names: list[str]) -> list[Contact]:
        """Contacts for contact and/or group names, without duplicates, in order."""
        out: dict[str, Contact] = {}
        for name in names:
            for member in self.groups.get(name, [name]):
                out.setdefault(member, Contact(member, self.contacts[member]))
        return list(out.values())

    def recipients_for(self, override: dict | None, level: str) -> list[Contact]:
        """Who hears about ``level``: the alert's own recipients if it has them, else [notify.levels]."""
        names = override.get(level) if override else None
        return self.resolve(self.level_contacts.get(level, []) if names is None else names)

    def describe(self) -> list[str]:
        """A summary that names recipients and channels but never prints addresses or topics."""
        channels = build_channels(self.channels)
        kinds = [c.kind for c in channels]
        def people(names: list[str]) -> str:
            contacts = self.resolve(names)
            reach = ", ".join(f"{k} {sum(1 for c in contacts if k in c.addresses)}" for k in dict.fromkeys(kinds))
            return f"{len(contacts)} {'person' if len(contacts) == 1 else 'people'}" + (f": {reach}" if reach else "")
        who = " | ".join(f"{lvl} -> {', '.join(self.level_contacts[lvl]) or 'nobody'} ({people(self.level_contacts[lvl])})"
                         for lvl in LEVELS)
        private = (f"{self.private_file} ({len(self.contacts)} contacts, {len(self.groups)} groups)"
                   if self.private_file else "none (using [contacts] from the config itself)")
        return [f"{len(self.ds18b20)} sensors, {len(self.switches)} switches, {len(self.devices)} devices, "
                f"{len(self.alerts)} alerts", f"private file: {private}",
                "channels: " + (", ".join(c.summary for c in channels) or "none (log only)"),
                f"who is told: {who}"]


def _contacts(table: dict, source: str) -> dict[str, dict]:
    """A contact is "a@b.c" (an email address) or a table of {channel kind: address}."""
    out: dict[str, dict] = {}
    for name, value in table.items():
        entry = {"email": value} if isinstance(value, str) else dict(value) if isinstance(value, dict) else None
        if not entry or not all(isinstance(k, str) and isinstance(v, str) and v for k, v in entry.items()):
            raise ConfigError(f"{source}: contact {name!r} must be an email address or a table like "
                              f'{{ email = "...", ntfy = "topic" }}')
        # Errors never repeat the value: topics are secrets and these messages end up in logs and chats.
        for kind, value in entry.items():
            if kind == "ntfy" and not TOPIC.match(value):
                raise ConfigError(f"{source}: contact {name!r}: an ntfy topic may only use letters, digits, - and _ (max 64)")
            if kind == "email" and "@" not in value:
                raise ConfigError(f"{source}: contact {name!r}: the email address needs an @")
        out[name] = entry
    return out


def _load_private(path: Path) -> tuple[dict, list[str]]:
    """Read the private file. Returns (contents, warnings)."""
    if not path.is_file():
        raise ConfigError(f"private file not found: {path}")
    with path.open("rb") as f:
        try:
            data = tomllib.load(f)
        except tomllib.TOMLDecodeError as exc:
            raise ConfigError(f"{path}: {exc}") from exc
    unknown = set(data) - PRIVATE_KEYS
    if unknown:
        raise ConfigError(f"{path}: unknown section(s) {sorted(unknown)}; allowed: {sorted(PRIVATE_KEYS)}")
    for legacy in ("email", "ntfy"):
        if legacy in data.get("notify", {}):
            raise ConfigError(f"{path}: " + LEGACY_HINT.format(legacy))
    unknown = set(data.get("notify", {})) - PRIVATE_NOTIFY_KEYS
    if unknown:
        raise ConfigError(f"{path}: [notify.{sorted(unknown)[0]}] belongs in the config; "
                          f"only {sorted(PRIVATE_NOTIFY_KEYS)} can go in the private file")
    warnings = []
    if os.name == "posix" and path.stat().st_mode & 0o077:
        warnings.append(f"{path} can be read by other users; run: chmod 600 {path}")
    return data, warnings


def load_config(path: str | Path, private: str | Path | None = None) -> Config:
    """``private`` (or $HOME_MONITOR_PRIVATE, or [system] private_file) names a file of secrets
    (contacts, groups, channel details) kept out of git and shared by several configs."""
    path = Path(path)
    with path.open("rb") as f:
        raw = tomllib.load(f)
    base = path.resolve().parent
    system = raw.get("system", {})
    notify = raw.get("notify", {})
    for legacy in ("email", "ntfy"):
        if legacy in notify:
            raise ConfigError(f"{path}: " + LEGACY_HINT.format(legacy))

    private_path: Path | None = None
    if private:
        private_path = Path(private)
    elif os.environ.get("HOME_MONITOR_PRIVATE"):
        private_path = Path(os.environ["HOME_MONITOR_PRIVATE"])
    elif system.get("private_file"):
        private_path = base / system["private_file"]  # relative to the config file
    secret, warnings = ({}, [])
    if private_path:
        secret, warnings = _load_private(private_path)
    cfg = Config(
        system_name=system.get("name", "home"),
        db_path=base / system.get("db_path", "data/home_monitor.db"),
        log_dir=base / system.get("log_dir", "logs"),
        log_level=system.get("log_level", "INFO").upper(),
        tick_seconds=float(system.get("tick_seconds", 1)),
        retention_days=int(system.get("retention_days", 365)),
        send_startup=bool(system.get("send_startup", False)),
        daily_report_hour=system.get("daily_report_hour"),
        contacts={**_contacts(raw.get("contacts", {}), str(path)),
                  **_contacts(secret.get("contacts", {}), str(private_path))},  # the private file wins
        groups={**raw.get("groups", {}), **secret.get("groups", {})},
        private_file=private_path.resolve() if private_path else None,
        warnings=warnings,
        level_contacts={lvl: list(notify.get("levels", {}).get(lvl, [])) for lvl in LEVELS},
        report_to=list(notify.get("report_to", [])),
        channels={name: {**notify.get("channels", {}).get(name, {}), **secret.get("notify", {}).get("channels", {}).get(name, {})}
                  for name in {**notify.get("channels", {}), **secret.get("notify", {}).get("channels", {})}},
        ds18b20=raw.get("ds18b20", []),
        switches=raw.get("switches", []),
        devices=raw.get("devices", []),
        alerts=raw.get("alerts", []),
    )
    _validate(cfg)
    return cfg


def _validate(cfg: Config) -> None:
    sensors = {d["name"] for d in cfg.ds18b20}
    switches = {s["name"] for s in cfg.switches}
    devices = {d["name"] for d in cfg.devices}
    names = [d["name"] for d in cfg.ds18b20] + [s["name"] for s in cfg.switches] + [d["name"] for d in cfg.devices]
    if len(names) != len(set(names)):
        raise ConfigError("sensor, switch and device names must be unique")
    for d in cfg.devices:
        keys = DEVICE_KEYS.get(d.get("type"))
        if keys is None:
            raise ConfigError(f"device {d['name']!r}: unknown type {d.get('type')!r}; use {sorted(DEVICE_KEYS)}")
        for key in keys:
            if d.get(key) not in switches:
                raise ConfigError(f"device {d['name']!r}: {key} must name a switch, got {d.get(key)!r}")
    where = f" (or in {cfg.private_file})" if cfg.private_file else ""
    for gname, members in cfg.groups.items():
        if gname in cfg.contacts:
            raise ConfigError(f"{gname!r} is both a contact and a group")
        for member in members:
            if member not in cfg.contacts:
                raise ConfigError(f"group {gname!r}: unknown contact {member!r}; add it under [contacts]{where}")
    lists = list(cfg.level_contacts.values()) + [cfg.report_to] + [
        g for a in cfg.alerts for g in a.get("recipients", {}).values()]
    for names in lists:
        for name in names:
            if name not in cfg.contacts and name not in cfg.groups:
                raise ConfigError(f"unknown contact or group {name!r}; add it under [contacts]{where}")
    try:
        build_channels(cfg.channels)
    except ValueError as exc:
        raise ConfigError(str(exc)) from exc
    for a in cfg.alerts:
        kind, src = a.get("type"), a.get("source")
        valid = sensors if kind in ("bounds", "stale") else switches | devices
        if src not in valid:
            raise ConfigError(f"alert {a.get('name')!r}: a {kind!r} alert needs a source among {sorted(valid)}, got {src!r}")


def build_rules(cfg: Config, now: float) -> list[AlertRule]:
    """``now`` is the monotonic time at startup (a StaleRule's first 'last seen')."""
    rules: list[AlertRule] = []
    for a in cfg.alerts:
        try:
            esc = Escalation(
                watch_s=float(a.get("watch_s", 0)),
                warn_s=float(a.get("warn_s", 300)),
                error_s=float(a.get("error_s", 600)),
                error_retry_s=float(a.get("error_retry_s", 300)),
                notify_on=frozenset(AlertState(x) for x in a.get("notify_on", ["warn", "error"])),
            )
            common = dict(escalation=esc, recipients=a.get("recipients"))
            kind = a["type"]
            if kind == "bounds":
                rules.append(BoundsRule(a["name"], a["source"], a["op"], float(a["value"]),
                                        a.get("value2"), **common))
            elif kind == "state":
                rules.append(StateRule(a["name"], a["source"], a["state"], **common))
            elif kind == "door_failed_to_close":
                rules.append(DoorClosingFailureRule(a["name"], a["source"], **common))
            elif kind == "stale":
                rules.append(StaleRule(a["name"], a["source"], float(a["max_age_s"]), now, **common))
            else:
                raise ConfigError(f"unknown alert type {kind!r}")
        except (KeyError, ValueError) as exc:
            raise ConfigError(f"alert {a.get('name', '?')!r}: {exc}") from exc
    return rules


def build_devices(cfg: Config) -> list[DerivedDevice]:
    out: list[DerivedDevice] = []
    for d in cfg.devices:
        settle = float(d.get("settle_s", 1.0))
        if d["type"] == "garage_door":
            out.append(GarageDoor(d["name"], d["up"], d["down"], settle))
        else:
            out.append(WashingMachine(d["name"], d["lid_up"], settle))
    return out
