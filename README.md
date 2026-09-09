# libmempressure — a Linux answer to Android's onTrimMemory

Apps on Android never get OOM-killed without warning: the OS sends them
`onTrimMemory()` callbacks and they shed caches gracefully. Desktop Linux
just kills them. `libmempressure` closes that gap: it watches the kernel's
[Pressure Stall Information](https://docs.kernel.org/admin-guide/perf/psi.html)
and delivers memory-pressure events to your application — in **C, C++, Java
(JVM) and Python** — so instead of dying under pressure, your app trims.

```
level       some avg10   what your app should do
----------- ------------ ------------------------------------
none        < 5%         business as usual
low         >= 5%        stop prefetching, trim opportunistically
moderate    >= 15%       drop object caches, shrink pools
critical    >= 40%       shed everything, survive
```

Events are level *changes* only, filtered by hysteresis (consecutive
readings) so your callback doesn't flap. The monitor reads
`/proc/pressure/memory` on its own thread; callbacks fire from that thread.

## The C API

```c
#include <mempressure.h>

static void on_pressure(mp_level_t level, void *userdata) {
    if (level >= MP_LEVEL_MODERATE) cache_drop_all();
}

mp_init(NULL);                       /* defaults; zero fields fall back too */
mp_subscribe(on_pressure, my_app);
/* ... */
mp_shutdown();
```

`mp_config_t` tunes thresholds (5/15/40%), poll interval (0.5s) and
hysteresis (2 readings); any zero field falls back to its default. See
[`include/mempressure.h`](include/mempressure.h) for the full contract —
threading rules, error codes, and the `psi_path` override (used by the test
suites to drive the monitor from fixture files).

## C++

Header-only RAII wrapper — [`bindings/c++/mempressure.hpp`](bindings/c++/mempressure.hpp):

```cpp
mp::Monitor monitor;                 /* starts the monitor */
monitor.subscribe([](mp::Level level) {
    if (level >= mp::Level::Moderate) cache_drop_all();
});                                  /* std::function, refcounted lifetime */
```

## JVM (JNI)

```java
import io.kutu.mempressure.MemPressure;

MemPressure.start(new MemPressure.Config());
int handle = MemPressure.subscribe(level -> {
    if (level >= 2) cache.dropAll();          // 0=none 1=low 2=moderate 3=critical
});
MemPressure.unsubscribe(handle);
MemPressure.stop();
```

Native side: `bindings/jvm/jni/mempressure_jni.c` (`libmempressure_jni.so`),
built automatically when a JDK is present. Callbacks arrive on a daemon
thread attached to the JVM.

## Python

```python
import mempressure as mp

mp.start()                                    # defaults
mp.subscribe(lambda level: level >= 2 and cache.drop_all())
print(mp.psi())                               # {'some_avg10': 0.12, ...}
mp.stop()
```

CPython extension (`mempressure` module) built against your interpreter;
callbacks fire with the GIL acquired from the monitor thread.

## Build and test

```sh
make test          # cmake build + ctest (C core, C++ binding, JVM binding)
                   # + python binding tests (needs a venv with pytest, see below)
```

Details:

```sh
cmake -S . -B build                # auto-detects Python dev + JDK; skips gracefully
cmake --build build
ctest --test-dir build --output-on-failure
python3 -m venv .venv && .venv/bin/pip install pytest
.venv/bin/pytest tests/python -q   # (build with -DPython3_EXECUTABLE=.venv/bin/python
                                   #  if your system python differs from the venv's)
```

The test suites never require real memory pressure: they point the monitor at
fixture files via `psi_path` and mutate them to drive level changes. The
example binary (`mp_example_c`) runs against your real
`/proc/pressure/memory`.

## Distro packaging (RPM / DEB)

The library is plain C11 + pthreads and installs via CMake's GNUInstallDirs,
so it lands correctly on any FHS distro (including `/usr/lib64` RPM
convention). Upstream ships packaging metadata:

- **RPM** (Fedora/RHEL/openSUSE): [`packaging/rpm/libmempressure.spec`](packaging/rpm/libmempressure.spec).
  Build a tarball and rpmbuild it, or point a COPR at the spec:

  ```sh
  git archive --prefix=libmempressure-0.1.0/ -o libmempressure-0.1.0.tar.gz HEAD
  rpmbuild -bb packaging/rpm/libmempressure.spec --define "_sourcedir $PWD"
  ```

- **DEB** (Debian/Ubuntu): a `debian/` directory with `libmempressure0`
  (runtime) and `libmempressure-dev` packages, native-format source:

  ```sh
  sudo apt install build-essential cmake debhelper pkg-config
  dpkg-buildpackage -us -uc -b
  ```

Distro packages ship the C core and the header-only C++ binding
(`-DMP_PYTHON=OFF -DMP_JAVA=OFF` in the build); the Python and JVM bindings
are built from the same source wherever those toolchains live. CI exercises
both paths on every push: `package-rpm` (fedora container) and
`package-deb` (debian container) build and inspect the actual packages.

## Relationship to kutu OS

This is the M3 building block of
[kutu OS](https://github.com/kutuso/os): the `mempressured` policy daemon and
eventually applications themselves use these events to shed memory gracefully
under pressure instead of being killed. The C core has no dependencies beyond
libc/pthreads, so it ships anywhere — including inside the kutu ISO.

MIT licensed — see [LICENSE](LICENSE).
