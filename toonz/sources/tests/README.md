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

Comparison is tolerant, not bit-exact: a frame fails when more than 0.5% of
its pixels differ by more than 2 in any channel. Vector levels and some
effects are rasterised through OpenGL today, so antialiasing differs
between drivers and platforms: the goldens come from Mesa's llvmpipe on
Linux, and Apple's OpenGL rasteriser disagrees with it on up to about 0.4%
of the pixels of a stroke-heavy frame. The tolerance absorbs that while
still catching real regressions (a missing stroke, a wrong colour or a
blank frame changes far more pixels). On failure the log also reports how
many non-blank pixels each image has, so a frame that rendered empty is
recognisable without downloading it. Expect the goldens to be regenerated
once Phase 1 replaces the OpenGL rasteriser with a deterministic CPU one.

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

Scenes in the reference project (320x180). The first twelve are 8 frames
each:

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

Those twelve scenes each isolate one feature with a simple drawing. The
scenes below do the opposite: they pack many independent features into
every frame, so that a regression anywhere shows up somewhere.

| Scene | Frames | Covers |
|---|---|---|
| `fx_sweep_base_a`, `fx_sweep_std_a`..`e`, `fx_sweep_ino_a`..`c`, `fx_sweep_iwa_a`..`b` | up to 16 each | every registered fx that renders on the CPU, by family, one per frame, applied to the test cards |
| `fx_sweep_font_a` | 2 | the fxs that draw text (system fonts) |
| `fx_sweep_rand_a` | 2 | the fxs that take random numbers from the C library's `rand()` |
| `fx_sweep_gpu_a` | 5 | GLSL shader fxs without inputs |
| `fx_sweep_gpuin_a` | 4 | GLSL shader fxs with inputs (they crash tcomposer on macOS, known issue 15) |
| `vector_styles` | 7 | every registered colour style class, eight per frame, on a stroke and a region |
| `stage_showcase` | 12 | keyframe interpolation types, rotation, non-uniform scale, shear, a two-level pegbar hierarchy, a motion path, stacking order, column opacity, colour filter and visibility, cell timing |

`fx_sweep_font_*`, `fx_sweep_rand_*`, `fx_sweep_gpu*_*` and `vector_styles`
depend on the system's fonts, C library or OpenGL driver (known issue 18),
so they are compared on Linux only, where the goldens come from. The other
scenes are compared on every platform.

### What `stage_showcase` should look like

Every column carries one marker drawing and is named after what it
exercises. Column 1 is the colour card behind everything.

| Column | Where | What to expect over the 12 frames |
|---|---|---|
| `y_linear`, `y_speed`, `y_ease`, `y_ease_pct`, `y_expo`, `y_expr`, `y_const` | row of seven dots on the left | all rise from the same height to the same height, each along its own interpolation; `y_const` stays put until the last frame |
| `rot_scale` | first flag, top middle | turns 300 degrees while getting wider and flatter |
| `shear` | second flag, top middle | slants to 45 degrees |
| `on_peg1`, `on_peg2` | square and small dot, upper right | the square turns with `Peg1`; the dot orbits `Peg2`, a child of `Peg1` that turns the other way |
| `path_aim` | flag, lower left | follows a sine-shaped motion path, pointing along it |
| `so_5`, `so_2_to_8` | square and large dot, bottom middle | the dot is behind the square for frames 1 to 6 and in front of it from frame 7 |
| `opacity_110` | top square of three, bottom right of centre | half transparent (110 of 255) |
| `filter_red` | middle square of three | tinted by the column's red colour filter |
| `no_render` | bottom square of three, twice the size | **missing from every render**: the column's render toggle is off, so it shows in the camera stand view only |
| `timing` | tally marks, far right | one to four bars on twos, nothing on frames 9 and 10, then three and two bars |

### Looking at a scene in OpenToonz

Open a copy, not the fixtures themselves, so that saving cannot change
them, and give OpenToonz a copy of `stuff/` so it leaves any installed
configuration alone:

```
cp -R stuff /tmp/review/stuff
cp -R toonz/sources/tests/fixtures/reference_project /tmp/review/reference_project
OpenToonz -TOONZROOT /tmp/review/stuff /tmp/review/reference_project/scenes/stage_showcase.tnz
```

(On macOS the binary is `OpenToonz.app/Contents/MacOS/OpenToonz`.) When
asked which project to use, choose "Change Project".

The viewer starts in the camera stand view, which draws the source
drawings only. Fxs, column opacity, colour filters and render toggles
take effect in the preview (the eye button in the viewer's title bar),
and that is what the goldens hold. The preview is rendered at the camera
resolution of 320x180, so it looks coarse when the viewer zooms in; the
camera stand view draws vectors at screen resolution and does not.

The sweeps are built from the fx registry (`TFx::listFxs`), so a new fx
gets a frame the next time the project is regenerated. Their inputs are
four test cards in `drawings/`:

- `card_main`: hue, luminance and alpha ramps, hard-edged primaries, a
  1 px grid, a half-transparent overlap, speckles, a 1 px checker, fine
  lines, a soft blob and a thin ring;
- `card_back`: the second input of two-input fxs (Back, Down, Source2);
- `card_ref`: the reference input (Reference, Matte, Depth, Flow, ...);
- `card_tz`: an ink-and-paint (Toonz raster) drawing for the fxs that work
  on palette indexes.

Most fxs run with their default parameters. The ones whose defaults leave
the input unchanged are listed with the parameters they get in
`sweepSetups()` in the generator. `extras/fx_sweep.txt` maps every output
frame to its fx and inputs and lists the fxs that are not swept, with the
reason; `extras/vector_styles.txt` does the same for the style cells.

`golden/contact_sheet.py` lays the frames of the sweeps out as labelled
sheets and flags frames that are blank, flat or the same as an input:

```
python3 ../sources/tests/golden/contact_sheet.py \
    --project ../sources/tests/fixtures/reference_project \
    --renders ../sources/tests/golden/expected --out ./sheets
```

Not covered yet: `externalPaletteFx` (needs a palette column),
`iwa_TiledParticlesFx` (needs its texture on earlier frames),
`iwa_MotionFlowFx` (renders flat: it does not see its column's motion in
the sweep), mask columns, depth and stereoscopic cameras, palette
animation, raster formats other than png and tlv, and output settings
(16-bit, shrink, resampling quality).

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
