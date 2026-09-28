import threading
import time

import pytest

import mempressure as mp


@pytest.fixture(autouse=True)
def _stop_monitor_after_each_test():
    yield
    mp.stop()

PSI_NONE = (
    "some avg10=0.00 avg60=0.00 avg300=0.00 total=0\n"
    "full avg10=0.00 avg60=0.00 avg300=0.00 total=0\n"
)


def write_psi(path, some10):
    path.write_text(
        f"some avg10={some10:.2f} avg60=0.00 avg300=0.00 total=0\n"
        "full avg10=0.00 avg60=0.00 avg300=0.00 total=0\n"
    )


def wait_until(fn, timeout=3.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if fn():
            return True
        time.sleep(0.01)
    return False


def test_version_and_names():
    assert mp.__version__
    assert mp.level_name(0) == "none"
    assert mp.level_name(3) == "critical"


def test_full_cycle(tmp_path):
    fixture = tmp_path / "psi"
    write_psi(fixture, 0.0)
    assert mp.start(psi_path=str(fixture), interval=0.02, hysteresis=1)
    assert mp.current_level() == 0

    events = []
    handle = mp.subscribe(events.append)
    assert handle > 0

    write_psi(fixture, 60.0)
    assert wait_until(lambda: events and events[-1] == 3), events
    assert mp.current_level() == 3
    assert wait_until(lambda: mp.psi()["some_avg10"] == 60.0)
    assert mp.psi()["full_avg10"] == 0.0

    write_psi(fixture, 0.0)
    assert wait_until(lambda: events and events[-1] == 0), events

    mp.unsubscribe(handle)
    with pytest.raises(ValueError):
        mp.unsubscribe(handle)
    mp.stop()


def test_hysteresis_requires_consecutive_readings(tmp_path):
    fixture = tmp_path / "psi"
    write_psi(fixture, 0.0)
    events = []
    assert mp.start(psi_path=str(fixture), interval=0.2, hysteresis=3)
    mp.subscribe(events.append)
    write_psi(fixture, 60.0)
    time.sleep(0.35)
    assert mp.current_level() == 0
    assert wait_until(lambda: mp.current_level() == 3, timeout=3.0)


def test_invalid_config(tmp_path):
    with pytest.raises(RuntimeError):
        mp.start(critical=1.0)
    assert mp.current_level() in (0, 1, 2, 3)


def test_unsubscribe_quiesces(tmp_path):
    fixture = tmp_path / "psi"
    write_psi(fixture, 0.0)
    events = []
    assert mp.start(psi_path=str(fixture), interval=0.02, hysteresis=1)
    handle = mp.subscribe(events.append)

    write_psi(fixture, 60.0)
    assert wait_until(lambda: len(events) >= 1)

    mp.unsubscribe(handle)
    after = len(events)
    time.sleep(0.2)
    assert len(events) == after
    write_psi(fixture, 0.0)
    time.sleep(0.1)
    assert len(events) == after
    mp.stop()


def test_stop_from_thread_while_firing(tmp_path):
    fixture = tmp_path / "psi"
    write_psi(fixture, 0.0)
    events = []
    assert mp.start(psi_path=str(fixture), interval=0.02, hysteresis=1)
    mp.subscribe(events.append)

    write_psi(fixture, 60.0)
    stopper = threading.Thread(target=mp.stop)
    stopper.start()
    stopper.join(timeout=5)
    assert not stopper.is_alive(), "stop() deadlocked against a callback acquiring the GIL"
