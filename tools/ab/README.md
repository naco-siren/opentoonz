# Before/after comparisons

| Tool | What it does |
|---|---|
| `ab_tcomposer.py render <tcomposer> --out DIR [--threads 1 8] [--rasterizer gl]` | Renders every reference scene with one build; `DIR/render.json` has a pixel hash per frame. |
| `ab_tcomposer.py compare <A> <B> --out DIR [--heatmaps DIR]` | Compares two such trees on the pixel hashes; one JSON per scene and thread count, `summary.json`, exit 1 on any judged difference. |

The comparison code is shared with the `nexttoonz` front end (`tools/nt`,
`render` and `diff tree`); the JSON documents are described in
`doc/nexttoonz_cli.md` and `tools/nt/schemas/`. Run it with the Python of the
golden tests (numpy, Pillow). Output goes to `local/runs/ab-tcomposer/` unless
`--out` says otherwise.

Typical checks:

```bash
ab_tcomposer.py render $BEFORE/tcomposer --out /tmp/ab/before --threads 1 8
ab_tcomposer.py render $AFTER/tcomposer  --out /tmp/ab/after  --threads 1 8
ab_tcomposer.py compare /tmp/ab/before /tmp/ab/after --out /tmp/ab/cmp
```

`--rasterizer gl` renders through the OpenGL fallback
(`NEXTTOONZ_VECTOR_RASTERIZER=gl`); with more than one thread that path is not
deterministic (known issue 26), so those frames are reported as `unstable` and
never judged.
