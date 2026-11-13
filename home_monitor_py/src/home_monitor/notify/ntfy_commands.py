"""Polls the ntfy command topic and runs the signed commands found there (see commands.py)."""
from __future__ import annotations

import asyncio
import json
import logging
import time
import urllib.request

from ..commands import CommandError, CommandHandler, CommandRejected, decode

log = logging.getLogger(__name__)


class NtfyCommandListener:
    """Outbound-only: the monitor asks ntfy for new messages every ``command_poll_s`` seconds.

    Messages already on the topic at startup are skipped, so a restart never replays old button presses.
    A bad signature is ignored without any reply; a valid but expired button gets an explanation.
    """

    def __init__(self, channel, handler: CommandHandler, system_name: str) -> None:
        self.channel, self.handler, self.system_name = channel, handler, system_name

    def _poll(self, since: str) -> list[dict]:
        ch = self.channel
        req = urllib.request.Request(f"{ch.server}/{ch.command_topic}/json?poll=1&since={since}")
        token = ch._token or None
        if token:
            req.add_header("Authorization", f"Bearer {token}")
        with urllib.request.urlopen(req, timeout=20) as resp:
            events = [json.loads(line) for line in resp.read().decode().splitlines() if line.strip()]
        return [e for e in events if e.get("event") == "message" and e.get("message")]

    async def run(self) -> None:
        since: str | None = None
        failures = 0
        while True:
            try:
                if since is None:   # first contact: note what is already there and do not run it
                    old = await asyncio.to_thread(self._poll, "all")
                    since = old[-1]["id"] if old else str(int(time.time()))
                    log.info("listening for ntfy commands on topic %s...", self.channel.command_topic[:4] + "…")
                else:
                    for msg in await asyncio.to_thread(self._poll, since):
                        since = msg["id"]
                        await self._handle(msg["message"])
                failures = 0
            except asyncio.CancelledError:
                raise
            except Exception as exc:  # noqa: BLE001 - a flaky network must never stop the monitor
                failures += 1
                if failures in (1, 10) or failures % 100 == 0:
                    log.warning("ntfy command poll failed (%d in a row): %s", failures, exc)
            await asyncio.sleep(min(60.0, self.channel.command_poll_s * (2 ** min(failures, 6))))

    async def _handle(self, text: str) -> None:
        try:
            cmd = decode(self.channel._secret, text, time.time())
        except CommandRejected as exc:
            log.warning("ignored a command message: %s", exc.reason)
            if exc.reply:
                await self._reply(exc.reply, "command not run", exc.reason)
            return
        log.info("command from %s: %s %s", cmd.reply, cmd.name, " ".join(cmd.args))
        try:
            result = self.handler.run(cmd.name, cmd.args)
        except CommandError as exc:
            await self._reply(cmd.reply, "command not run", str(exc))
            return
        await self._reply(cmd.reply, f"[{self.system_name}] {result.headline}", result.body, result.undo)

    async def _reply(self, topic: str, title: str, message: str, undo=None) -> None:
        try:
            await asyncio.to_thread(self.channel.reply, topic, title, message, undo)
        except Exception as exc:  # noqa: BLE001
            log.warning("could not reply on %s: %s", topic, exc)
