"""Fake sensors so the whole program can be tried on any machine (--simulate)."""
from __future__ import annotations

import asyncio
import math
import random
import time

from ..models import Reading, StateChange


async def simulate_temp(name: str, queue: "asyncio.Queue", interval_s: float,
                        mid: float = -12.0, swing: float = 6.0, period_s: float = 90.0) -> None:
    start = time.monotonic()
    while True:
        t = time.monotonic() - start
        value = mid + swing * math.sin(2 * math.pi * t / period_s) + random.uniform(-0.1, 0.1)
        queue.put_nowait(Reading(name, round(value, 2), time.time()))
        await asyncio.sleep(interval_s)


async def simulate_switch(name: str, queue: "asyncio.Queue") -> None:
    state = "closed"
    queue.put_nowait(StateChange(name, state, time.time()))
    while True:
        await asyncio.sleep(random.uniform(15, 40))
        state = "open" if state == "closed" else "closed"
        queue.put_nowait(StateChange(name, state, time.time()))
