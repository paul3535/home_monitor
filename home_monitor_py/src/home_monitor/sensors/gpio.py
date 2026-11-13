"""Reed switches (or any contact input) using edge interrupts instead of polling.

Wiring assumption, taken from reed_switch.py: BCM pin, internal pull-up, switch to
ground. A closed switch pulls the pin LOW. With gpiozero's Button(pull_up=True) that is
"pressed", which we report as "closed".
"""
from __future__ import annotations

import asyncio
import logging
import time
from typing import Callable

from ..models import StateChange

log = logging.getLogger(__name__)


async def watch_switch(name: str, pin: int, queue: "asyncio.Queue", *, active_low: bool = True,
                       bounce_s: float = 0.05, button_factory: Callable | None = None) -> None:
    if button_factory is None:
        try:
            from gpiozero import Button
        except ImportError as exc:
            raise RuntimeError("gpiozero is not installed: sudo apt install python3-gpiozero "
                               "python3-lgpio (or pip install '.[pi]'), or run with --simulate") from exc
        button_factory = Button

    loop = asyncio.get_running_loop()
    button = button_factory(pin, pull_up=active_low, bounce_time=bounce_s)
    last: str | None = None

    def publish(closed: bool) -> None:  # runs on the event loop thread
        nonlocal last
        state = "closed" if closed else "open"
        if state == last:
            return
        last = state
        log.info("%s (GPIO %d) is %s", name, pin, state)
        queue.put_nowait(StateChange(name, state, time.time()))

    # gpiozero calls these from its own thread, so hand off to the loop.
    button.when_pressed = lambda: loop.call_soon_threadsafe(publish, True)
    button.when_released = lambda: loop.call_soon_threadsafe(publish, False)
    publish(bool(button.is_pressed))  # report the initial state at startup
    try:
        await asyncio.Event().wait()  # nothing to poll: callbacks do the work
    finally:
        button.close()
