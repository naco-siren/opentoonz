# CI and acceptance tools

| File | What |
|---|---|
| `collect_results.sh <build> <results>` | Run by every CI job that tests: gathers JUnit, golden JSON, contact sheets, renders and ccache statistics into the `results-<platform>-<sha>` artifact. |
| `triage.py <bundle>` | Matches a results bundle's differences against `tests/known_differences.yaml`, re-derives the gates, picks up to 10 frames to look at, draws them blind (X/Y in random order), plants a canary, writes `triage.json`, `triage.key` and the AI's view `accept/view/`. |
| `triage.py round2 <bundle> --pr pr.md` | Gives round 2 the change's description, freezing round 1. |
| `accept_prompt.md` | What the acceptance AI is told: inputs, rules, the two rounds, the output files. |
| `check_verdict.py <bundle>` | Checks `accept/verdict.json` (schema, coverage, gates, evidence paths, cited numbers, ranking, round consistency, canary) and decides the outcome: `accept/check.json`. |
| `brief.py <bundle>` | The one-page brief for a person, `accept/brief.md`, from `brief_template.md`. |

The design is `doc/nexttoonz_cli_ci.md`, sections 4.3 to 4.6; the documents are
described in `doc/nexttoonz_cli.md` and `tools/nt/schemas/`.

## One acceptance, by hand

```bash
tools/nt/nexttoonz test --suite pr --program $AFTER/tcomposer --base $BEFORE/tcomposer --bundle $B
tools/ci/triage.py $B [--criteria step.md]
# round 1: a fresh session, not the one that made the change, given only
#   tools/ci/accept_prompt.md and $B/accept/view; it writes $B/accept/round1.json
tools/ci/triage.py round2 $B --pr pr.md [--diffstat stat.txt]
# round 2: the same session continues; it writes $B/accept/verdict.json
tools/ci/check_verdict.py $B      # once more after a rewrite if it fails
tools/ci/brief.py $B
```

Where the acceptance runs, with which model and budget, and whether anything is
posted to GitHub are the owner's questions 1, 3 and 4 (`doc/nexttoonz_cli_ci.md`,
section 8). Nothing here posts or blocks a merge.

## What keeps the AI honest

- The machine's gates decide pass and fail; `check_verdict.py` refuses an
  `accept` while a gate fails and decides the outcome from the itemised
  findings, not from the AI's overall verdict.
- Known differences are matched by program, with bounds; over a bound they are
  unexplained again.
- Blind sheets: X and Y are base and test in random order, the mapping is only
  in `triage.key`, outside the view. Round 1 sees no description of the change;
  calling an item a defect blind and "expected" afterwards needs a person.
- A canary: one unchanged frame altered on purpose (a one-pixel shift, a colour
  one step off, a missing stroke), listed, counted and drawn like a real
  difference. Missing it voids the acceptance. It never reaches the brief.
- Every claim points at files of the view and copies its numbers from
  `triage.json`; a script checks both.
