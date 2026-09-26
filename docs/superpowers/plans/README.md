# `docs/superpowers/plans/`: how v1 was built

The two documents behind beatdown v1's implementation. They were written for, and during, an agent-driven build: a coordinating session handed one task at a time to implementer and reviewer agents.

**Read these as history, not as a description of the current code.** The code has moved on since, through review fix rounds, two tasks added mid-way (17 and 18) and later M4A support. When a plan and the code disagree, trust the code and the per-directory READMEs. Use these files for *why* something is the way it is.

## `2026-09-15-beatdown-v1.md`: the implementation plan

The step-by-step plan for v1, split into 16 tasks, each small enough to hand to one agent.

- **Global constraints** apply to every task, for example:
  - C++20 with warnings on
  - one build recipe on all three OSes
  - never modify a source file, and never leave a half-written output under its final name
  - UTF-8 everywhere
  - one Catch2 `TEST_CASE` per behaviour
- **A file map** lists the planned files and what each is responsible for.
- **Tasks 1–16**, in dependency order. Each gives the files to create, the tests to write first and the code to write:
  - 1: toolchain and project skeleton
  - 2: options and tags
  - 3: Unicode and the platform shim
  - 4: test fixtures and the decoder
  - 5: ID3v2
  - 6: scanner
  - 7: MP3 frame parser
  - 8: encoder interface and LAME
  - 9: FLAC
  - 10: verifier
  - 11: space check
  - 12: scheduler
  - 13: converter
  - 14: reporter and runner
  - 15: CLI
  - 16: CI, release workflow and README
- **A self-review** of the plan closes it.

Its header names the "superpowers" agent-workflow skills it was written to be executed with, which is where this folder's name comes from.

## `2026-09-15-beatdown-v1-rulings.md`: decisions made while building

The coordinating session's decision log, copied verbatim, in the order the decisions were made. Each entry says what was decided, why, and what it would cost if it turned out wrong.

- **Rulings R-A to R-M** are cross-cutting decisions. For example: no `u8"…"` literals (R-A), and float or 32-bit sources become 24-bit FLAC (R-F).
- **Per-task rulings** cover places where the plan's own code had a defect and how it was fixed. They are marked "plan-mandated".
- **Tasks 17 and 18** were added mid-way:
  - 17: the file-identity-based "never overwrite a source" check
  - 18: the "club-ready" per-file audio content verification and decoded-peak reporting
- **"Known limitations and deferred findings"** lists what reviewers found and deliberately left alone. Some entries have since been fixed (for example, the UTF-8 decoder now validates continuation bytes), so check the code before relying on one.

## Decoding labels you'll see in code comments

Comments across `src/` and `tests/` cite where a rule came from. The labels mean:

| Label in a comment | Where to look |
|---|---|
| `R1`–`R28` | a requirement in [`../../PRD.md`](../../PRD.md), §6 |
| `Q1`–`Q16` | an open question and its decision in the PRD, §8 |
| `Task N` | a task in the plan (1–16), or 17–18 in the rulings log |
| `Ruling R-A` … `R-M` | the rulings log, top section |
| `A2`, `A3`, `A4` | amendments the coordinating session made to the Task 14 brief (dry-run reporting, the disk-full summary, and per-job exception handling) |
| `Finding N`, `fix round N` | review findings on a task, and the round of fixes that addressed them |

Up: [`docs/superpowers/`](../README.md)
