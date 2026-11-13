from __future__ import annotations

import asyncio
import json
import os
import re
import time
import urllib.request

from ..commands import encode, parse_duration
from .base import Channel, Notification

DEFAULT_PRIORITIES = {"watch": 3, "warn": 4, "error": 5}   # ntfy: 1 min .. 3 default .. 5 max (rings through)
LEVEL_TAGS = {"watch": ["eyes"], "warn": ["warning"], "error": ["rotating_light"]}
MAX_BYTES = 3800   # ntfy messages are limited to 4096 bytes


def truncate(text: str, limit: int = MAX_BYTES) -> str:
    data = text.encode()
    if len(data) <= limit:
        return text
    return data[:limit].decode(errors="ignore").rstrip() + "\n[truncated]"


TOPIC = re.compile(r"^[A-Za-z0-9_-]{1,64}$")


class NtfyChannel(Channel):
    """Push notifications through ntfy (https://ntfy.sh or your own server) to the ntfy Android app.

    Buttons: with ``command_topic`` and ``command_secret`` set, alert notifications carry action buttons
    (Mute 1h, Mute 8h, Status). Tapping one makes the phone post a signed command to the command topic,
    which the monitor polls (see ntfy_commands.py), so nothing has to be opened on your network.
    Keep both values in private.toml. For a protected server, allow anonymous write-only on the
    command topic (the phone posts without a login; the signature is the credential).

    Destinations are topics: give a contact ``ntfy = "topic"``. Each person subscribes their phone to
    their topic(s); for different sounds per level, point levels at different contacts (and so topics).
    A topic works like a password, so keep topics in private.toml. For a protected server set ``token``
    (or HOME_MONITOR_NTFY_TOKEN).
    """
    kind = "ntfy"
    option_names = frozenset({"server", "token", "priorities", "command_topic", "command_secret", "command_ttl",
                              "mute_buttons", "max_mute", "command_poll_s"})
    default_kinds = ("event", "test", "startup")   # no daily-report push unless you add "report"

    def __init__(self, name: str, options: dict) -> None:
        super().__init__(name, options)
        self.server = str(options.get("server", "https://ntfy.sh")).rstrip("/")
        self._token = options.get("token")
        self.priorities = {**DEFAULT_PRIORITIES, **options.get("priorities", {})}
        if not all(isinstance(v, int) and 1 <= v <= 5 for v in self.priorities.values()):
            raise ValueError("priorities must be whole numbers from 1 to 5")
        self.command_topic = options.get("command_topic")
        self._secret = options.get("command_secret")
        if bool(self.command_topic) != bool(self._secret):
            raise ValueError("command_topic and command_secret go together; set both or neither")
        self.command_ttl = self.max_mute_s = self.command_poll_s = 0.0
        self.mute_buttons: list[str] = []
        if self.command_topic:
            if not TOPIC.match(str(self.command_topic)):
                raise ValueError("command_topic may only use letters, digits, - and _ (max 64)")
            if len(str(self._secret)) < 16:
                raise ValueError("command_secret must be at least 16 characters (a long random string)")
            try:
                self.command_ttl = parse_duration(options.get("command_ttl", "24h"))
                self.max_mute_s = parse_duration(options.get("max_mute", "24h"))
                self.mute_buttons = list(options.get("mute_buttons", ["1h", "8h"]))
                for d in self.mute_buttons:
                    parse_duration(d)
            except ValueError as exc:
                raise ValueError(str(exc)) from exc
            if len(self.mute_buttons) > 2:
                raise ValueError("at most 2 mute_buttons (ntfy allows 3 buttons; Status takes one)")
            self.command_poll_s = float(options.get("command_poll_s", 5))
            if self.command_poll_s < 0.05:
                raise ValueError("command_poll_s must be at least 0.05")

    @property
    def summary(self) -> str:
        return f"{self.label} ({self.server}{', Mute/Status buttons' if self.command_topic else ''})"

    def priority(self, n: Notification) -> int:
        if n.kind == "event" and not n.recovery:
            return self.priorities.get(n.level or "", 3)
        return 2 if n.kind == "report" else 3

    def tags(self, n: Notification) -> list[str]:
        if n.kind == "event":
            return ["white_check_mark"] if n.recovery else LEVEL_TAGS.get(n.level or "", [])
        return {"report": ["bar_chart"], "test": ["test_tube"], "startup": ["rocket"]}.get(n.kind, [])

    def _button(self, reply_topic: str, label: str, name: str, args=(), clear: bool = False) -> dict:
        body = encode(self._secret, name, args, reply_topic, time.time() + self.command_ttl)
        return {"action": "http", "label": label, "url": f"{self.server}/{self.command_topic}",
                "method": "POST", "body": body, "clear": clear}

    def actions(self, n: Notification, topic: str) -> list[dict]:
        """Buttons for this message. Each is signed for this topic, so the reply goes back to whoever tapped."""
        if not self.command_topic:
            return []
        buttons = []
        if n.kind == "event" and n.rule and not n.recovery:
            buttons += [self._button(topic, f"Mute {d}", "mute", (n.rule, d), clear=True) for d in self.mute_buttons]
        return buttons + [self._button(topic, "Status", "status")]

    def _publish(self, payload: dict) -> None:
        req = urllib.request.Request(self.server + "/", data=json.dumps(payload).encode(), method="POST",
                                     headers={"Content-Type": "application/json"})
        token = self._token or os.environ.get("HOME_MONITOR_NTFY_TOKEN")
        if token:
            req.add_header("Authorization", f"Bearer {token}")
        urllib.request.urlopen(req, timeout=15).close()

    def _post(self, topic: str, n: Notification) -> None:
        payload = {"topic": topic, "title": n.subject, "message": truncate(n.body),
                   "priority": self.priority(n), "tags": self.tags(n)}
        actions = self.actions(n, topic)
        if actions:
            payload["actions"] = actions
        self._publish(payload)

    def reply(self, topic: str, title: str, message: str, undo: tuple | None = None) -> None:
        """Answer a command on the topic it came from (blocking; call from a thread)."""
        payload = {"topic": topic, "title": title, "message": truncate(message), "priority": 3, "tags": ["speech_balloon"]}
        if undo:
            payload["actions"] = [self._button(topic, "Unmute", undo[0], undo[1:], clear=True)]
        self._publish(payload)

    def _send_blocking(self, topics: list[str], n: Notification) -> None:
        failures = []
        for topic in topics:
            try:
                self._post(topic, n)
            except Exception as exc:  # noqa: BLE001 - report every topic, not just the first failure
                failures.append(f"{topic}: {exc}")
        if failures:
            raise RuntimeError("; ".join(failures))

    async def send(self, n: Notification) -> str:
        topics = self.destinations(n)
        if not topics:
            return "skipped: no ntfy topic for any recipient"
        await asyncio.to_thread(self._send_blocking, topics, n)
        return f"sent to {len(topics)} topic{'' if len(topics) == 1 else 's'}"
