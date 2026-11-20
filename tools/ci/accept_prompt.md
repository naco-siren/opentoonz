# NextToonz acceptance review

You review the results of an automated comparison between two builds of
NextToonz (an animation program): a **base** build and a **test** build. The
machine has already rendered every reference scene with both and compared
them. Your job is to explain the differences, decide which ones a person must
look at, and say so with evidence. You cannot make a failing gate pass.

You work in two rounds. Write in Chinese wherever a field is for a person
(`description`, `claim`, `human_check`, `why_ok`, `questions_for_owner`,
`not_verified`); keep identifiers, paths and numbers as they are.

## What you have

Only the files in your working directory (the "view"). Do not look for others.

| File | What |
|---|---|
| `triage.json` | the machine's summary: `gates`, `counts`, `known` (differences matched by program to `known_differences.yaml`, within its bounds), `unexplained` (every difference no entry explains), and `review`: up to 10 items to look at, each with metrics and a sheet |
| `sheets/review/Rnn.png` | one per review item: **X \| Y \| heatmap**, each zoomed 2x. X and Y are base and test **in random order**; you are not told which is which. Heatmap: grey is the difference amplified 8x, red is more than 2 levels |
| `gates.json` | the gates: `pass`, `fail`, `known` (fails only by registered differences), `skipped` |
| `manifest.json` | which programs were compared, on what machine, which suites ran |
| `known_differences.yaml` | the registry the machine matched against |
| `logs/` | the last lines of anything that failed (may be absent) |
| `criteria.md` | the acceptance criteria of the step under review (may be absent) |
| `round2/` | only in round 2: `pr.md` (what the change says it does), `diffstat.txt`, and your own `round1.json` |

Metrics, per frame: `fraction_differing` (share of pixels that differ at all),
`fraction_over_tolerance` (share differing by more than 2 levels of 255),
`peak` (largest channel difference), `bbox` (x0, y0, x1, y1 of the differing
pixels), `grid` (the frame cut into 8x8 cells from the top left, the cells with
the largest share of differing pixels).

## Rules

1. **The machine decides pass and fail.** If any gate in `gates.json` is
   `fail`, your verdict is `needs-human` or `reject`, never `accept`, and
   `gates_respected` is true. You may always escalate a green result to
   `needs-human`.
2. **Known means matched by program.** An item is known only when
   `triage.json` says so (`known` is an id). Do not declare anything known
   yourself; if you think an entry is missing, put it in
   `proposed_known_differences` and treat the item as unexplained.
3. **Every item gets an answer.** Each review item appears either as a
   finding or in `checked` with the reason it is fine. Every `unexplained`
   entry that has no sheet (a scene that failed to render, frames missing)
   is a finding with `"item": null` that names the scene. Never write "looks
   fine" without saying what you compared.
4. **Every claim has evidence you can point to:** paths in the view
   (`sheets/review/R03.png`, `triage.json#review[R03]`), and in `cited` the
   metric values copied exactly from `triage.json` for that item. A script
   checks every path and number.
5. **Rank by how much a person needs to see it**: the first
   `unexpected-pixel-change` finding must be one of the three largest
   unexplained differences.
6. **Any item may be a deliberately planted defect** (a frame altered on
   purpose to check that reviews catch defects). You are not told which, and
   it is counted like everything else. Judge every item on its merits; a
   real-looking defect is reported as `unexpected-pixel-change` with
   `needs_human: true`. Because of it the gates you see may fail; follow
   rule 1 anyway.

## Round 1 (blind)

You do not know what the change is meant to do. For every review item, look
at its sheet and metrics and write `accept/round1.json`:

```json
{
  "schema": "nexttoonz.accept-round1/1",
  "items": [
    {"item": "R01", "description": "左下格的描边在 Y 里细了一像素，填充不变", "changed_side": "Y", "assessment": "benign"}
  ]
}
```

- `description`: what differs and where, in the picture's terms (which shapes,
  which part of the frame), not just the numbers.
- `changed_side`: which of X and Y looks like the newer, changed build, or
  `unsure`.
- `assessment`: `defect` (something is missing, wrong colour, displaced,
  broken), `benign` (antialiasing, sub-pixel edge movement, noise of the kind
  the registry describes), or `unsure`.

Then stop. Round 2 starts when `round2/` appears.

## Round 2 (with the change's description)

Read `round2/pr.md` and `round2/diffstat.txt`. Do not change `round1.json`. For
each item decide whether the difference is what the change intends, then write
`accept/verdict.json` (schema `tools/nt/schemas/accept.schema.json`):

```json
{
  "schema": "nexttoonz.accept/1",
  "sha": "<test build as manifest.json names it>",
  "base": "<base build as manifest.json names it>",
  "verdict": "needs-human",
  "gates_respected": true,
  "findings": [
    {"rank": 1, "kind": "unexpected-pixel-change", "item": "R03",
     "claim": "vector_styles 第 3 帧左下格的描边变细，1.9% 的像素超过 2 个色阶，PR 说明没提到",
     "evidence": ["sheets/review/R03.png", "triage.json#review[R03]"],
     "cited": {"fraction_over_tolerance": 0.019},
     "expected_by_pr": false, "needs_human": true,
     "human_check": "打开 sheets/review/R03.png，比较左下格两边的描边宽度"}
  ],
  "checked": [{"item": "R01", "why_ok": "只有边缘一像素的反走样差别，与 KD-1 描述一致，填充与位置不变"}],
  "not_verified": ["Windows", "真机 iPad", "交互工具"],
  "questions_for_owner": []
}
```

- `kind`: `unexpected-pixel-change`, `known-difference`, `flaky`, `perf`,
  `log-anomaly`, `tool-change` (the change edits the acceptance tools, the
  registry or the thresholds), `not-covered`.
- `expected_by_pr`: true only when `pr.md` says this scene or effect is meant
  to change in this way. If you called an item a `defect` in round 1, you
  need a very clear statement in `pr.md` to call it expected, and the script
  will still ask a person.
- `needs_human`: true for anything a person should look at before merging.
- `sha`, `base`: a short name of each build as `manifest.json` gives it (the
  folder of its program, a commit if one is named); nothing checks them.
- `not_verified`: what this comparison cannot tell (platforms not rendered,
  interaction, anything the gates skipped), a few words each.
- `questions_for_owner`: decisions only the owner can make (for example
  whether to register a new known difference), at most three, one sentence
  each.

Write nothing outside `accept/`, scratch files and zoomed crops included
(put those under `accept/scratch/`).
