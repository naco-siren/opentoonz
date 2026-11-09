# Skia spike (Phase 1)

A single test binary, `nexttoonz_skia_spike_tests`, that links Skia, uses
only its CPU rasteriser and checks the pixels of a filled path, a stroked
path and a mesh. It
exists to prove how Skia is obtained and linked; the render path does not
use it. The route, the alternatives that were rejected and the measurements
are in `doc/nexttoonz_phase1_skia.md`.

The target is off by default (`WITH_SKIA_SPIKE`, which needs `WITH_SKIA`), so
a normal build and the CI are unaffected. The canvas backend that came out
of the spike is `tnzskia`; its tests are in `../canvas_skia`.

## Getting Skia

There are two ways; both end in the CMake target `nexttoonz_skia`, which is
defined in `tnzskia/CMakeLists.txt`.

### Prebuilt (recommended for macOS and iPadOS)

Download and unpack a release of
[JetBrains/skia-pack](https://github.com/JetBrains/skia-pack/releases) for
the target: `macos-Release-x64`, `iosSim-Release-x64` (the iPad simulator on
an Intel Mac) or `ios-Release-arm64`. Check the SHA-256 against the ones
listed in `doc/nexttoonz_phase1_skia.md`. Nothing is built.

```
cmake ../sources -G Ninja ... \
    -DWITH_SKIA=ON -DWITH_SKIA_SPIKE=ON \
    -DNEXTTOONZ_SKIA_PACK_DIR=<unpacked macos-Release-x64>
ninja nexttoonz_skia_spike_tests
ctest -L skia_spike --output-on-failure
```

For iPadOS there is no CMake build of the tree yet. `run_ios.sh` compiles
the tests and GoogleTest with Xcode's clang and, for the simulator, runs
them with `xcrun simctl spawn`:

```
./run_ios.sh simulator <unpacked iosSim-Release-x64> <googletest sources> <work dir>
./run_ios.sh device    <unpacked ios-Release-arm64>  <googletest sources> <work dir>
```

`<googletest sources>` can be `_deps/googletest-src` of a build directory
that fetched GoogleTest. The `device` form only compiles and links.

### From source through vcpkg (fallback)

Builds Skia with nothing but the CPU rasteriser (`default-features: false`
in `vcpkg.json` here). The vcpkg revision is pinned by `builtin-baseline`,
which also pins the Skia milestone.

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

Configure with
`-Dunofficial-skia_DIR=<somewhere>/vcpkg_installed/x64-osx-release/share/unofficial-skia`
instead of `NEXTTOONZ_SKIA_PACK_DIR`. The vcpkg toolchain file is not used
on purpose: it would make every other `find_package()` of this build look
in vcpkg first.
