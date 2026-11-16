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
| `canvas/` | GoogleTest unit tests of the canvas IR library (`tnzcanvas`): no rasteriser, no display, no other library. |
| `canvas_skia/` | Pixel tests of the Skia canvas backend (`tnzskia`): what each canvas call puts into the pixels. Built with `WITH_SKIA`. |
| `canvas_gl/` | Tests of the OpenGL canvas backend (`tnzglcanvas`) and of how far it agrees with the Skia one. Off by default (`WITH_CANVAS_GL_TESTS`): they need an OpenGL context. |
| `skia_spike/` | Phase 1 spike: one test binary that links Skia and checks pixels. Off by default (`WITH_SKIA_SPIKE`, with `WITH_SKIA`); see its README. |
| `ios_gtest.sh` | Builds a GoogleTest binary for iPadOS with Xcode's clang and runs it in the iPad simulator; used by the `run_ios.sh` of `canvas/`, `canvas_skia/` and `skia_spike/`. |

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

## Sample files (not in the repository)

Files for looking at the application by hand, and pictures made for a
review, live in `samples/` at the root of the main checkout. The directory
is not part of the repository: it is ignored through `.git/info/exclude`,
which every worktree shares, and its own `README.md` lists what is there.
Each machine needs that one line once:

```
echo /samples/ >> "$(git rev-parse --git-common-dir)/info/exclude"
```

Do not open `tests/fixtures/reference_project` in the application: it writes
backups and scene icons into the project, and the tests read that project.
Work on a copy:

```
cp -R toonz/sources/tests/fixtures/reference_project samples/reference_project
```

## Vector images without OpenGL

Since migration step 2 of `doc/nexttoonz_canvas_ir.md`, styles draw through
the canvas interface: solid colours and constant lines, then patterns,
textures, vector and MyPaint brushes (step 3), the sixteen region styles of
colorfx (step 4) and its 25 stroke styles (step 5): every style a release
build registers. Since step 6 the other places that draw a vector drawing,
or one stroke or region of it, into a raster (the conversion to a Toonz
raster image, `TVectorImage::render`, style and level icons, export, the
tracker, meshify, the script rasterizer) ask the CPU rasteriser first too,
since step 7 so do the two effects that drew meshes through OpenGL (the
plastic deformer and the flow paint brush), and since step 8 so does the
camera-stand render of a frame (`ToonzScene::renderFrame`: scene icons,
xsheet thumbnails, printing). Since step 9 this is the default in a build with Skia: renders draw vectors
on the CPU, and `NEXTTOONZ_VECTOR_RASTERIZER=gl` goes back to `TOfflineGL`,
where the migrated styles reach OpenGL through `tnzglcanvas` and come out bit
for bit as before step 2. A build without Skia always uses OpenGL.

In a build with `-DWITH_SKIA=ON`, two environment variables change how
renders (tcomposer, tconverter, the application's renders and icons) draw
vectors:

```
NEXTTOONZ_VECTOR_RASTERIZER=gl     rasterise vector levels with OpenGL, as
                                   before step 9; without it they are drawn
                                   with Skia (an image that uses a style
                                   that is not on the canvas, none of the
                                   registered ones any more, would still go
                                   through OpenGL, as a whole), and so are
                                   the meshes of the plastic deformer and
                                   the flow paint brush, scene icons and
                                   thumbnails
NEXTTOONZ_STROKE_GROW=0            draw solid strokes at their true width
                                   instead of grown by half a pixel, which
                                   is the default and the weight they have
                                   through OpenGL
```

The golden baselines of the scenes with vectors are Skia renders, so their
golden tests fail with the first variable set, and in a build without
Skia; that is expected, and the way to get OpenGL pictures to compare:

```
NEXTTOONZ_VECTOR_RASTERIZER=gl python3 ../sources/tests/golden/run_golden.py \
    --tcomposer <tcomposer> --stuff ../../stuff \
    --project ../sources/tests/fixtures/reference_project \
    --scene scenes/vector_basic.tnz \
    --expected ../sources/tests/golden/expected/vector_basic --work <dir>
```

`core/test_vector_canvas.cpp` covers the same code without a rasteriser: it
records what drawing an image asks of a canvas and compares the text.
`toonzlib/test_style_canvas.cpp` does the same for the styles that draw with
a picture or another vector image (patterns, vector brushes, textures,
MyPaint brushes), taken from the reference project's `vec_styles` level.
`toonzlib/test_region_style_canvas.cpp` does it for the region styles of
colorfx, made by their tags; its last test also rasterises all sixteen
without OpenGL (in a build that has Skia) and checks that each covers about
the area of the region. `toonzlib/test_stroke_style_canvas.cpp` is the same
for the stroke styles of colorfx. `toonzlib/test_offscreen_canvas.cpp`
runs the functions of step 6 with the CPU rasteriser selected; with no
window system there is no OpenGL context, so none of them can fall back on
one there. The two effects of step 7 are covered by the golden scenes
`plastic_basic` and `fx_sweep_iwa_a` (by default, through OpenGL); what they
need of the canvas (a textured mesh drawn over a raster, an image shrunk a
great deal and sampled from its mipmaps) by `core/test_vector_canvas.cpp`,
`canvas_skia/test_skia_meshes.cpp` and `canvas_gl/test_backends_agree.cpp`.
`toonzlib/test_camstand_canvas.cpp` draws two frames of eight reference
scenes the way `renderFrame` does with the CPU rasteriser selected, and
checks that the painter did not have to give up on its raster (and go back
to OpenGL) and that `renderFrame` puts the same pixels over its background.

## Canvas tests

`nexttoonz_canvas_tests` links `tnzcanvas` and nothing else of the tree.
`tnzcanvas` is the path-level canvas IR of `doc/nexttoonz_canvas_ir.md`: the
interface drawing code calls (`TCanvas`), its value types, and a recorder
that keeps the calls and prints them as text. The tests compare that text,
so they say which operations a piece of drawing code emits without
rasterising anything:

```
ctest -L canvas
./tests/canvas/nexttoonz_canvas_tests --gtest_filter='CanvasDisplayList.*'
```

`tnzcanvas` is the first library that also has to build for iPadOS, so it
may use the C++ standard library only. The test
`canvas.sources_are_self_contained` reads its sources and fails on any other
`#include` (Qt, OpenGL, tnzcore). To run the same tests in the iPad
simulator, or to check that they link for a real iPad:

```
../sources/tests/canvas/run_ios.sh simulator _deps/googletest-src ./ios-work
../sources/tests/canvas/run_ios.sh device    _deps/googletest-src ./ios-work
```

The simulator used is the first iPad `xcrun simctl list devices available`
shows, or the one named by `NEXTTOONZ_IOS_SIMULATOR`.
`_deps/googletest-src` is there when CMake fetched GoogleTest; with a system
GoogleTest, pass a checkout of its sources instead.

### The Skia backend

`nexttoonz_canvas_skia_tests` links `tnzskia`, the canvas that rasterises on
the CPU with Skia, and checks pixels: coverage at edges, what growing a
shape does to a seam and to a hole, line widths and dashes under a
transform, dots, meshes painted once where they overlap, clips to paths and
to meshes, a fill that is also a clip, images and gradients.
It is built only with `-DWITH_SKIA=ON`, which needs Skia itself:
`-DNEXTTOONZ_SKIA_PACK_DIR=<unpacked skia-pack archive>` for the prebuilt
one, see `skia_spike/README.md` and `doc/nexttoonz_phase1_skia.md`.

```
ctest -L canvas_skia
../sources/tests/canvas_skia/run_ios.sh simulator <unpacked iosSim-Release-x64> _deps/googletest-src ./ios-work
../sources/tests/canvas_skia/run_ios.sh device    <unpacked ios-Release-arm64>  _deps/googletest-src ./ios-work
```

`SkiaCanvasPicture.RenderingIsRepeatable` prints a hash of a small picture
that uses every call; it is the number to compare between machines and
between builds of Skia. `NEXTTOONZ_CANVAS_SKIA_DUMP=<file>` makes it write
the pixels. `tnzskia` may include Skia's headers besides the standard
library and the canvas headers, and nothing else:
`canvas_skia.sources_are_self_contained` checks that.

### The OpenGL backend, and the two compared

`nexttoonz_canvas_gl_tests` links `tnzglcanvas`, the canvas that draws with
the fixed-function OpenGL of the old vector renderer, and draws into a
`TOfflineGL`. It is built with `-DWITH_CANVAS_GL_TESTS=ON` and needs what
the golden tests need: a window system that gives an OpenGL context (on
Linux, run it under `xvfb-run`). On macOS the binary stays in the
background; it opens no window.

```
ctest -L canvas_gl
./tests/canvas_gl/nexttoonz_canvas_gl_tests --gtest_filter='CanvasBackends.*'
```

The `GlCanvas` tests check what any driver should agree on: the inside of
shapes, premultiplied results, clips, where images and gradients land. One
of them draws the way the old code does (`TGlCanvas::State::Callers`): there
the non-zero rule fills by positive winding, as the old tessellator did.

The `CanvasBackends` tests (built when `WITH_SKIA` is on as well) record a
picture once and replay it through both canvases. The two are not meant to
match pixel for pixel, since OpenGL softens outlines with a line or a band
where Skia computes coverage; each test prints how far apart they are and
fails beyond about twice what this was on the machine it was written on:

```
regions                mean  1.43  over 32:  0.23%  over 96:  0.00%  largest  56
```

That is the mean of the largest channel difference per pixel, the share of
pixels that differ by more than 32 and by more than 96 (of 255), and the
largest difference. `NEXTTOONZ_CANVAS_GL_DUMP=<directory>` keeps both
pictures of every test as raw BGRA.

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
its pixels differ by more than 2 in any channel, colours taken premultiplied
by alpha (the PNG files are unpremultiplied, which turns a difference of a
level or two along an antialiased edge into tens). The tolerance absorbs
the differences between platforms while still catching real regressions
(a missing stroke, a wrong colour or a blank frame changes far more
pixels). On failure the log also reports how many non-blank pixels each
image has, so a frame that rendered empty is recognisable without
downloading it.

Where the baselines come from (step 9 of `doc/nexttoonz_canvas_ir.md`):
the scenes whose frames Skia changes (every scene with vectors, and the
effects whose input has vectors) are Skia renders made on an x86_64 Mac,
and Linux agrees with them within the tolerance (the largest difference is
0.24% of a frame's pixels); `vector_styles`, compared on Linux only, is a
Linux Skia render. The other scenes keep the baselines that came from the
Ubuntu clang CI job, which every platform matched already. So a baseline
can now be made on a development machine with `--update`, as long as the
build has Skia; CI builds without Skia (`WITH_SKIA` is off by default) draw
vectors with OpenGL and do not match them.

On Linux the renders run under `xvfb-run` with Mesa's software rasteriser
because the OpenGL paths (GLSL effects, and vectors without Skia) need a
display (`NEXTTOONZ_GOLDEN_XVFB`, on by
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

## Sanitizers

`workflow_sanitizers.yml` builds the unit tests and tcomposer with the
`asan` preset (AddressSanitizer with leak detection, and
UndefinedBehaviorSanitizer) and runs the whole suite, golden scenes
included. To do the same on a Linux machine:

```
cd toonz/sources
cmake --preset asan -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cd ../build-asan
export ASAN_OPTIONS=detect_leaks=1:halt_on_error=1
export LSAN_OPTIONS=suppressions=$PWD/../sources/tests/lsan.supp:print_suppressions=0
export UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1
ninja nexttoonz_core_tests nexttoonz_toonzlib_tests tcomposer lzocompress lzodecompress
ctest --output-on-failure
```

`lsan.supp` lists the leaks the library is known to have, each with its
number in `doc/nexttoonz_known_issues.md`. A leak that is not listed fails
the test that hit it. Fix a new leak, or record it as a known issue before
adding an entry. One entry is by test name: the three tests that call
`TVectorImage::removeStroke()` (known issue 10); a new test that calls it
needs to be added there.

Leak detection does not work with Apple's clang; on macOS, `leaks --atExit
-- ./nexttoonz_core_tests` (with `MallocStackLogging=1` for stacks) gives a
similar report.

## Regenerating the reference project

```
cd toonz/build
ninja nexttoonz_gen_fixtures
./bin/nexttoonz_gen_fixtures ../../stuff ../sources/tests/fixtures/reference_project
```

The generator deletes and recreates the project folder. The result is not
byte for byte the same from one run to the next: the order of the terminal
fxs and of the cast in some scenes, and the bytes of the mesh, change. When
adding a scene, add only its own files and leave the others as they were. It works on a scratch copy of `stuff/`; the `TOONZROOT not
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
| `vector_cmapped_fx` | 4 | vector levels under the fxs that work on palette indices (palette filter on lines, palette filter on areas deleting every line, calligraphic, outBorder): the renderer converts each vector frame to a Toonz raster image first (`ToonzImageUtils::vectorToToonzImage`) |
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
