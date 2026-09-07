import ctypes
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
BUILD = REPO / "build"


def _find(pattern: str) -> Path:
    hits = sorted(BUILD.glob(pattern))
    if not hits:
        raise RuntimeError(f"{pattern} not found in {BUILD}; run: make build")
    return hits[0]


# Runs at conftest import time: the extension module is imported at collection
# time by the test modules, so the core library must already be loadable and
# build/ on sys.path before pytest collects anything.
ctypes.CDLL(str(_find("libmempressure.so")), mode=ctypes.RTLD_GLOBAL)
sys.path.insert(0, str(BUILD))
