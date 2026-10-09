# NextToonz tests

This directory holds the automated tests introduced by the NextToonz plan
(`doc/nexttoonz_plan.md`, Phase 0). They are built when the CMake option
`WITH_TESTS` is on (the default) and run with `ctest` from the build
directory.

```
cd toonz/build
cmake ../sources -G Ninja            # or: cmake --preset release (from toonz/sources)
ninja
ctest --output-on-failure
```

## Layout

| Path | What it is |
|---|---|
| `core/` | GoogleTest unit tests against the core libraries (`tnzcore` for now). |
| `golden/` | Golden-render tests: `tcomposer` renders the reference project and the frames are compared with `golden/expected/<scene>/`. |
| `fixtures/reference_project/` | The reference project: scenes, levels and palettes the golden tests render. Generated, but committed. |
| `fixtures/gen_reference_project.cpp` | The generator that builds the reference project through the toonzlib API. |

## Unit tests

`nexttoonz_core_tests` links `tnzcore` and GoogleTest. GoogleTest is taken
from the system when a CMake package is found (`libgtest-dev` on Debian and
Ubuntu, `brew install googletest` on macOS) and fetched from GitHub
otherwise.

Library behaviours and bugs that the tests pin down as-is are listed in
`doc/nexttoonz_known_issues.md`; fix the test expectation together with the
library when you fix one of them.

Tests are discovered with `gtest_discover_tests`, so every `TEST()` shows up
as its own ctest entry:

```
ctest -R TFilePath
./bin/nexttoonz_core_tests --gtest_filter='TAffine.*'
```

## toonzlib tests

`nexttoonz_toonzlib_tests` links toonzlib, image, stdfx and colorfx and
initialises them the way tcomposer does (a scratch copy of `stuff/`, image
and fx registries, shader interfaces, the project manager pointed at the
reference project). It covers level formats (pli, tlv, png) and the palette
format with fixture-content and round-trip tests, loading and re-saving
every reference scene, and xsheet operations. The discovered tests carry
the prefix `toonzlib.` and the label `toonzlib`:

```
ctest -L toonzlib
./bin/nexttoonz_toonzlib_tests --gtest_filter='SceneLoad*'
```

## Golden renders

Each scene in `fixtures/reference_project/scenes/` becomes a ctest named
`golden_<scene>`. The test driver `golden/run_golden.py`:

1. copies `stuff/` and the reference project into
   `<build>/golden/<scene>/`, so nothing is written into the source tree;
2. runs `tcomposer` single-threaded with that copy as `TOONZROOT`;
3. compares every frame with `golden/expected/<scene>/` using
   `golden/compare_images.py`, and writes diff images into
   `<build>/golden/<scene>/diff/` on failure.

Comparison is tolerant, not bit-exact: a frame fails when more than 0.1% of
its pixels differ by more than 2 in any channel. Vector levels and some
effects are rasterised through OpenGL today, so antialiasing differs
between drivers and platforms; the tolerance absorbs that while still
catching real regressions. Expect the goldens to be regenerated once Phase
1 replaces the OpenGL rasteriser with a deterministic CPU one.

On Linux the renders run under `xvfb-run` with Mesa's software rasteriser
because the OpenGL paths need a display (`NEXTTOONZ_GOLDEN_XVFB`, on by
default on Linux). This needs `xvfb`, `mesa-utils`, `libgl1-mesa-dri`,
`python3-numpy` and `python3-pil`.

Run one scene by hand, or refresh its goldens after an intentional change:

```
python3 ../sources/tests/golden/run_golden.py \
    --tcomposer ./bin/tcomposer --stuff ../../stuff \
    --project ../sources/tests/fixtures/reference_project \
    --scene scenes/vector_basic.tnz \
    --expected ../sources/tests/golden/expected/vector_basic \
    --work ./golden/vector_basic --xvfb [--update]
```

Review the new frames before committing updated goldens; they are the
source of truth for every later refactor.

## Regenerating the reference project

```
cd toonz/build
ninja nexttoonz_gen_fixtures
./bin/nexttoonz_gen_fixtures ../../stuff ../sources/tests/fixtures/reference_project
```

The generator deletes and recreates the project folder, so the result is
deterministic. It works on a scratch copy of `stuff/`; the `TOONZROOT not
set` messages it prints come from `TEnv` looking for an installed
configuration and are harmless.

Scenes in the reference project (320x180, 8 frames each):

| Scene | Covers |
|---|---|
| `vector_basic` | pli level, region fill, stroke rendering, per-frame images |
| `toonz_raster_basic` | tlv level, ink and paint, tone ramp |
| `raster_png_basic` | full-colour png level, premultiplied compositing |
| `pegbar_hierarchy` | pegbar parenting, keyframe interpolation, camera zoom |
| `fx_blur_over_colorcard` | zerary fx column, fx inserted in the dag |
| `subxsheet` | child level, nested xsheet, frame offsets |
| `checkerboard_camera` | asset-free generator fx, camera pan and rotation |
| `sound_column` | sound column (wav loaded via `ToonzScene::loadLevel`) next to a vector level |
| `particles_basic` | particlesFx zerary column with the default sprite (needs GL), over a colour card |
| `fx_gallery_cpu` | fx chains: linearGradientFx background, glowFx then inoBlurFx, radialBlurFx |
| `shader_fx` | GLSL shader fx (`sunflare.xml` from `stuff/library/shaders`) with an animated parameter |
| `plastic_basic` | mesh level from `buildMesh`, mesh column parenting, animated plastic skeleton, PlasticDeformerFx |

Still to add: the remaining fx families (iwa_, noise and so on, see
`doc/nexttoonz_plan.md`, D7).

Things to know about the generator and the scene files:

- The generator needs no display or OpenGL. It reads
  `stuff/library/shaders` through `loadShaderInterfaces()` so shader fx
  can be instantiated, writes its WAV fixture byte by byte and registers a
  minimal WAV reader because it does not link the sound library, and builds
  the plastic meshing mask on the CPU because every vector rasteriser in
  the codebase still goes through `TOfflineGL`.
- `.tnz` output is not byte-deterministic: fx sets, terminal sets and cast
  folders are serialised in pointer order, so a regenerated scene can have
  lines reordered while describing the same graph. Compare renders, not
  bytes, until the Phase 2 persistence work orders them by id.
- `PlasticDeformer` drops a skeleton handle that lies exactly on a mesh
  edge (`TTextureMesh::faceContains` uses closed sign tests); the generator
  keeps joints off edges and throws if a joint is outside every face.
