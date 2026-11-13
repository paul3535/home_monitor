"""DS18B20 temperature sensors via the Linux w1 kernel driver.

Enable the driver on the Pi with this line in /boot/firmware/config.txt (then reboot):

    dtoverlay=w1-gpio,gpiopin=4,pullup=on

``pullup=on`` replaces the C++ code that asked GPIO for a pull-up on the one-wire pin.
"""
from __future__ import annotations

import asyncio
import logging
import time
from pathlib import Path
from typing import Callable

from ..models import Reading

log = logging.getLogger(__name__)
W1_ROOT = Path("/sys/bus/w1/devices")
POWER_ON_RESET = 85.0  # what a DS18B20 reports before its first conversion finishes


def parse_w1_slave(text: str) -> float | None:
    """Parse the two-line w1_slave format. Returns degrees C, or None if invalid.

    Failure is None, never a number: the C++ code returned -1/-3/-4, which are
    plausible freezer temperatures.
    """
    lines = text.strip().splitlines()
    if len(lines) < 2 or not lines[0].strip().endswith("YES"):
        return None  # CRC check failed
    _, sep, raw = lines[1].partition("t=")
    if not sep:
        return None
    try:
        value = int(raw.strip()) / 1000.0
    except ValueError:
        return None
    if value == POWER_ON_RESET or not -55.0 <= value <= 125.0:
        return None
    return value


def read_ds18b20(device_id: str, root: Path = W1_ROOT, retries: int = 3,
                 retry_delay: float = 0.2) -> float | None:
    """Blocking read with a few retries on CRC errors. Run it in a thread."""
    path = root / device_id / "w1_slave"
    for attempt in range(retries):
        try:
            value = parse_w1_slave(path.read_text())
        except OSError as exc:
            log.debug("cannot read %s: %s", path, exc)
            return None  # sensor unplugged: retrying will not help
        if value is not None:
            return value
        if attempt < retries - 1:
            time.sleep(retry_delay)
    return None


async def poll_ds18b20(name: str, device_id: str, interval_s: float,
                       queue: "asyncio.Queue", reader: Callable[[str], float | None] = read_ds18b20) -> None:
    failing = False
    while True:
        value = await asyncio.to_thread(reader, device_id)  # a read takes ~750 ms
        if value is None:
            if not failing:
                log.warning("%s (%s): no valid reading", name, device_id)
                failing = True
        else:
            if failing:
                log.info("%s (%s): reading again", name, device_id)
                failing = False
            queue.put_nowait(Reading(name, value, time.time()))
        await asyncio.sleep(interval_s)
