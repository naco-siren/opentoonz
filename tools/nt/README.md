# nexttoonz (Python front end)

The command line of `doc/nexttoonz_cli.md`, implemented over the programs
that exist today (tcomposer, tconverter). The C++ `nexttoonz` on the engine
API replaces it later; the sub-commands, JSON documents (`schemas/`) and exit
codes stay.

```bash
tools/nt/nexttoonz probe --json --program $TCOMPOSER
tools/nt/nexttoonz render vector_basic --frames 1-3 --hash-only --program $TCOMPOSER --json
tools/nt/nexttoonz diff images a.png b.png --profile golden --heatmap d.png
tools/nt/nexttoonz diff tree renders/before renders/after --out ab --heatmaps diff
tools/nt/nexttoonz sheet build/golden --out overview.png
tools/nt/nexttoonz test --suite pr --program $AFTER --base $BEFORE --bundle results/x
tools/nt/nexttoonz validate results/x/gates.json
```

Requires Python 3.10+ with numpy and Pillow (the golden tests' Python).
`$NEXTTOONZ_TCOMPOSER` / `$NEXTTOONZ_TCONVERTER` stand in for `--program`.

| Path | What |
|---|---|
| `nexttoonz`, `__main__.py` | entry points |
| `ntpy/cli.py` | argument parsing, one function per sub-command |
| `ntpy/tcomposer.py` | running tcomposer safely (scratch copies, timeout, out of the Dock), build detection |
| `ntpy/abtree.py` | render trees and their comparison (also `tools/ab/ab_tcomposer.py`) |
| `ntpy/images.py` | pixel hash, metrics, heatmaps; reuses `tests/golden/compare_images.py` |
| `ntpy/suite.py` | `test`: suites and the results bundle |
| `ntpy/sheets.py` | overview and pair images |
| `ntpy/schema.py` | the small JSON Schema checker behind `validate` |
| `ntpy/determinism.py` | determinism levels by scene and rasteriser |
| `schemas/` | one JSON Schema per document kind |
