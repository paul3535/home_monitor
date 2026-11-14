from __future__ import annotations

import asyncio
import os
import smtplib
from email.message import EmailMessage

from .base import Channel, Notification


class EmailChannel(Channel):
    """Email over SMTP. The login comes from ``user``/``password`` (put them in private.toml) or from
    the HOME_MONITOR_SMTP_USER / HOME_MONITOR_SMTP_PASSWORD environment variables."""
    kind = "email"
    option_names = frozenset({"smtp_host", "smtp_port", "from_addr", "starttls", "user", "password"})

    def __init__(self, name: str, options: dict) -> None:
        super().__init__(name, options)
        if not options.get("smtp_host"):
            raise ValueError("needs smtp_host")
        self.host = str(options["smtp_host"])
        self.port = int(options.get("smtp_port", 587))
        self.from_addr = options.get("from_addr", "")
        self.starttls = bool(options.get("starttls", True))
        self._user, self._password = options.get("user"), options.get("password")

    @property
    def summary(self) -> str:
        return f"{self.label} ({self.host}:{self.port})"

    def credentials(self) -> tuple[str, str] | None:
        user = self._user or os.environ.get("HOME_MONITOR_SMTP_USER")
        password = self._password or os.environ.get("HOME_MONITOR_SMTP_PASSWORD")
        return (user, password) if user and password else None

    def _send_blocking(self, n: Notification, to: list[str]) -> None:
        creds = self.credentials()
        msg = EmailMessage()
        msg["From"] = self.from_addr or (creds[0] if creds else "")
        msg["To"] = ", ".join(to)
        msg["Subject"] = n.subject
        if n.level == "error" and not n.recovery:
            msg["X-Priority"] = "1"
        msg.set_content(n.body)
        with smtplib.SMTP(self.host, self.port, timeout=30) as smtp:
            if self.starttls:
                smtp.starttls()
            if creds:
                smtp.login(*creds)
            smtp.send_message(msg)

    async def send(self, n: Notification) -> str:
        to = self.destinations(n)
        if not to:
            return "skipped: no email address for any recipient"
        await asyncio.to_thread(self._send_blocking, n, to)
        return f"sent to {len(to)} address{'' if len(to) == 1 else 'es'}"
