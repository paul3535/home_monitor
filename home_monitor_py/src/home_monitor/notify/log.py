from __future__ import annotations

import logging

from .base import Channel, Notification

log = logging.getLogger("home_monitor.notify")


class LogChannel(Channel):
    """Always on: every notification is written to the log, so there is a record of what was sent."""
    kind = "log"

    def __init__(self) -> None:
        super().__init__("log", {})

    async def send(self, n: Notification) -> str:
        who = ", ".join(c.name for c in n.recipients) or "nobody"
        log.warning("NOTIFY [%s] %s | %s | to: %s", n.kind, n.subject, n.body.strip().replace("\n", " | "), who)
        return "logged"
