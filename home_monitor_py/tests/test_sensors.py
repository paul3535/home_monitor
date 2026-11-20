import asyncio
import tempfile
import unittest
from pathlib import Path

from home_monitor.models import StateChange
from home_monitor.sensors.ds18b20 import parse_w1_slave, read_ds18b20
from home_monitor.sensors.gpio import watch_switch

GOOD = "18 ff 4b 46 7f ff 08 10 8b : crc=8b YES\n18 ff 4b 46 7f ff 08 10 8b t=-14500\n"
BAD_CRC = "18 ff 4b 46 7f ff 08 10 8b : crc=8b NO\n18 ff 4b 46 7f ff 08 10 8b t=-14500\n"


class ParseW1(unittest.TestCase):
    def test_good(self):
        self.assertEqual(parse_w1_slave(GOOD), -14.5)

    def test_bad_crc_is_none_not_a_number(self):
        self.assertIsNone(parse_w1_slave(BAD_CRC))

    def test_power_on_reset_value_ignored(self):
        self.assertIsNone(parse_w1_slave(GOOD.replace("t=-14500", "t=85000")))

    def test_garbage(self):
        for text in ("", "YES", "x YES\nno temperature here", "x YES\nt=abc"):
            self.assertIsNone(parse_w1_slave(text))

    def test_error_codes_of_the_old_code_are_real_temperatures_now(self):
        # -1, -3 and -4 C were the C++ error sentinels; they are valid freezer readings.
        for v in (-1, -3, -4):
            self.assertEqual(parse_w1_slave(f"x YES\nx t={v * 1000}"), float(v))

    def test_read_from_directory(self):
        with tempfile.TemporaryDirectory() as d:
            dev = Path(d) / "28-test"
            dev.mkdir()
            (dev / "w1_slave").write_text(GOOD)
            self.assertEqual(read_ds18b20("28-test", root=Path(d)), -14.5)
            self.assertIsNone(read_ds18b20("28-missing", root=Path(d)))

    def test_retries_after_crc_error(self):
        with tempfile.TemporaryDirectory() as d:
            dev = Path(d) / "28-test"
            dev.mkdir()
            (dev / "w1_slave").write_text(BAD_CRC)
            self.assertIsNone(read_ds18b20("28-test", root=Path(d), retries=2, retry_delay=0))


class FakeButton:
    instances = []

    def __init__(self, pin, pull_up, bounce_time):
        self.pin, self.pull_up, self.bounce_time = pin, pull_up, bounce_time
        self.is_pressed = False
        self.when_pressed = self.when_released = None
        self.closed = False
        FakeButton.instances.append(self)

    def close(self):
        self.closed = True


class Switch(unittest.IsolatedAsyncioTestCase):
    async def test_reports_initial_state_and_edges_once(self):
        queue = asyncio.Queue()
        task = asyncio.create_task(watch_switch("door", 17, queue, button_factory=FakeButton))
        await asyncio.sleep(0.05)
        btn = FakeButton.instances[-1]
        self.assertEqual((btn.pin, btn.pull_up, btn.bounce_time), (17, True, 0.05))
        self.assertEqual((await queue.get()).state, "open")        # initial state at startup
        btn.when_pressed()                                         # edge from gpiozero's thread
        btn.when_pressed()                                         # duplicate: ignored
        btn.when_released()
        await asyncio.sleep(0.05)
        states = [queue.get_nowait().state for _ in range(queue.qsize())]
        self.assertEqual(states, ["closed", "open"])
        task.cancel()
        with self.assertRaises(asyncio.CancelledError):
            await task
        self.assertTrue(btn.closed)


if __name__ == "__main__":
    unittest.main()
