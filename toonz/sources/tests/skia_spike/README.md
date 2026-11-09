# Skia spike (Phase 1)

A single test binary, `nexttoonz_skia_spike_tests`, that links a CPU-only
Skia and checks the pixels of a filled path, a stroked path and a mesh. It
exists to prove how Skia is obtained and linked; the render path does not
use it. The route, the alternatives that were rejected and the measurements
are in `doc/nexttoonz_phase1_skia.md`.

The target is off by default (`WITH_SKIA_SPIKE`), so a normal build and the
CI are unaffected.

## Getting Skia

Skia comes from the vcpkg port, built from source with nothing but the CPU
rasteriser (`default-features: false` in `vcpkg.json` here). The vcpkg
revision is pinned by `builtin-baseline`, which also pins the Skia
milestone.

```
git clone https://github.com/microsoft/vcpkg.git
./vcpkg/bootstrap-vcpkg.sh -disableMetrics
./vcpkg/vcpkg install --triplet x64-osx-release \
    --x-manifest-root=toonz/sources/tests/skia_spike \
    --x-install-root=<somewhere>/vcpkg_installed
```

Use `arm64-osx-release` on Apple Silicon and `x64-linux-release` on Linux.
Python 3.10 or newer has to come first on `PATH` (the build of pkgconf uses
meson); macOS's own `/usr/bin/python3` is too old.
The `-release` triplets skip the debug build of every package, which halves
the build time and the disk use.

## Building and running the test

Add two options to the usual configure line:

```
cmake ../sources -G Ninja ... \
    -DWITH_SKIA_SPIKE=ON \
    -Dunofficial-skia_DIR=<somewhere>/vcpkg_installed/x64-osx-release/share/unofficial-skia
ninja nexttoonz_skia_spike_tests
ctest -L skia_spike --output-on-failure
```

The vcpkg toolchain file is not used on purpose: it would make every other
`find_package()` of this build look in vcpkg first.
