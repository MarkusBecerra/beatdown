# How beatdown works

This is the map of the repository: what's where, how the pieces fit, and where to read next. [`README.md`](README.md) is for *users* (installing and running beatdown). This page is for anyone, person or AI agent, about to read or change the code.

**How the documentation is laid out:** every directory has its own `README.md` describing its files in plain English and linking to its sub-folders' READMEs. Read this page first, then the README of the folder you're working in. Each one is short enough to take in at once, so you can pick up only the context a task needs. If a README and the code ever disagree, the code wins. Please fix the README in the same change.

## What beatdown is

A command-line tool that batch-converts a folder of audio files into 320 kbps MP3s (or FLAC) for DJ use in rekordbox. It reads WAV, AIFF, FLAC and M4A (AAC or Apple Lossless).

It is written in C++20 and runs on macOS, Windows and Linux. Each platform gets one self-contained executable, with every library linked in. Its defining promises:
- **Source files are never modified.**
- **A half-written output never appears under its real name.**
- **An output never overwrites a source.**
- **Every output is checked against its source** before it counts as converted.

## End to end

```mermaid
flowchart LR
    user(["beatdown SOURCE DEST"]) --> cli["src/cli<br/>parse arguments"]
    cli --> plan["plan the batch<br/>find files, apply skip rules,<br/>check free space"]
    plan --> pool["worker threads<br/>one file at a time each"]
    pool --> dec["decode the source<br/>libsndfile or FFmpeg"]
    dec --> enc["encode to a temp file<br/>LAME (MP3) or libsndfile (FLAC)"]
    enc --> ver["verify: headers,<br/>then the audio itself"]
    ver --> ren["rename into place"]
    ren --> rep["report each file,<br/>then a summary and exit code"]
```

The detailed versions of this picture, with every decision point and failure path, are in [`src/core/README.md`](src/core/README.md).

## Repository map

| Path | What it is | Read more |
|---|---|---|
| `README.md` | User documentation: install, usage, behaviour, licences. | |
| `ARCHITECTURE.md` | This page. | |
| `src/` | The program: a thin command-line front end over a conversion-engine library. | [`src/README.md`](src/README.md) |
| `src/cli/` | `main()`: command-line arguments → `Options` → run → exit code. | [`src/cli/README.md`](src/cli/README.md) |
| `src/core/` | The engine (static library `beatdown_core`): planning, decoding, encoding, verifying, reporting. | [`src/core/README.md`](src/core/README.md) |
| `src/core/platform/` | The few things that differ between Windows and POSIX. | [`src/core/platform/README.md`](src/core/platform/README.md) |
| `tests/` | The Catch2 test suite. Every audio fixture is generated at test time. | [`tests/README.md`](tests/README.md) |
| `docs/` | The requirements (PRD), audio measurements, and records of how v1 was built. | [`docs/README.md`](docs/README.md) |
| `docs/superpowers/plans/` | The v1 implementation plan and its decision log, plus what the labels in code comments mean. | [`docs/superpowers/plans/README.md`](docs/superpowers/plans/README.md) |
| `external/vcpkg/` | The C/C++ package manager, pinned as a git submodule. | [`external/README.md`](external/README.md) |
| `triplets/` | macOS build targets for vcpkg (macOS 11 and later). | [`triplets/README.md`](triplets/README.md) |
| `.github/workflows/` | CI on three OSes for every push; release binaries for every `v*` tag. | [`.github/workflows/README.md`](.github/workflows/README.md) |
| `.github/actions/` | An in-repo action that sets up the MSVC compiler in Windows jobs. | [`.github/actions/README.md`](.github/actions/README.md) |
| `CMakeLists.txt` | The build definition (below). | |
| `CMakePresets.json` | Named build configurations (below). | |
| `vcpkg.json` | The list of third-party libraries, and the vcpkg version they come from. | [`external/README.md`](external/README.md) |
| `.gitmodules`, `.gitignore`, `LICENSE` | The vcpkg submodule; build output and the vcpkg cache kept out of git; the MIT licence. | |

`.github/` itself has no README on purpose. GitHub would show a `.github/README.md` on the repository's front page in place of the real one, so `.github/` is described here and in its sub-folders' READMEs.

## The ideas that explain most of the code

1. **Safety before speed.** Before anything is written, the planner:
   - pairs every source with its output and sets aside anything that would collide;
   - checks there's enough free space for the whole batch;
   - refuses a destination whose parent folder is missing, which usually means an unmounted drive.

   Each file is then encoded under a temporary name next to its output. It is renamed into place only after passing verification, and the temp file is deleted on any failure.
2. **Verify everything, twice.** After encoding, the new file's headers are checked (bitrate, sample rate, length), then its *audio* is decoded and compared with the source. The comparison tolerances come from measurements (`docs/club-readiness-measurements.md`). That's also why the CLI only offers MP3 settings the check can vouch for.
3. **Formats sit behind interfaces.** `Decoder` has two backends:
   - libsndfile, for WAV, AIFF and FLAC;
   - FFmpeg, for M4A.

   `Encoder` has two:
   - LAME, for MP3;
   - libsndfile, for FLAC.

   Adding a format means adding a backend, not changing the pipeline.
4. **Parallel, one file per thread.** A small thread pool runs one conversion per worker. The few shared hazards each have a guard:
   - a process-wide lock around every libsndfile open;
   - a lock around terminal output;
   - MP3 decoding for verification done only through thread-safe code.
5. **One codebase, three OSes, identical output.**
   - Operating-system differences live in `src/core/platform/`.
   - Text and paths are UTF-8 throughout, with wide-character APIs on Windows.
   - Libraries are linked statically.
   - FFmpeg's decoders are chosen by name, so a Mac never quietly uses Apple's decoder instead.

## Building

The README's Install section has the prerequisites and commands. In short: bootstrap the vcpkg submodule once, then run `cmake --preset default` and `cmake --build --preset default`.

- **`CMakePresets.json`**:
  - `default` (Release) and `debug` build for the host machine.
  - `macos-arm64` and `macos-x64` build the two halves of the universal macOS release, with tests off.
  - All share the Ninja generator, output in `build/<preset>/`, vcpkg's toolchain, the overlay triplets, and a macOS 11 minimum.
- **`CMakeLists.txt`** defines three targets:
  - **`beatdown_core`**, the static library holding all logic. It links libsndfile and LAME publicly, and FFmpeg privately.
  - **`beatdown`**, the executable. It adds CLI11 for argument parsing.
  - **`beatdown_tests`**, which uses Catch2 and FFmpeg's encoders for fixtures, and needs the executable built first for the CLI tests. It can be switched off with `-DBEATDOWN_BUILD_TESTS=OFF`.

  It also:
  - selects the fully static `x64-windows-static` triplet and static C runtime on Windows;
  - turns on compiler warnings (`-Wall -Wextra`, `/W4`);
  - passes the project version to the code.
- **The first configure is slow.** vcpkg builds every library from source, and FFmpeg takes the most time. Later configures reuse the built packages.

## Testing and CI

- **Tests:** `ctest --preset default` runs the whole suite. Tests use real libraries on generated audio. [`tests/README.md`](tests/README.md) maps each test file to what it covers.
- **CI:** every push is built and tested on macOS, Windows and Linux.
- **Releases:** pushing a `v*` tag builds the three release binaries and publishes a GitHub release. See [`.github/workflows/README.md`](.github/workflows/README.md).

## Where decisions are written down

| Question | Where |
|---|---|
| What must beatdown do, and why? | [`docs/PRD.md`](docs/PRD.md): goals, requirements R1–R28, decisions Q1–Q16 |
| Why is a piece of code the way it is? | The comment above it, which often cites a requirement or ruling. [`docs/superpowers/plans/README.md`](docs/superpowers/plans/README.md) decodes the labels. |
| How was v1 built, and what was deliberately left for later? | The plan and rulings log in `docs/superpowers/plans/` |
| Are the default settings good enough for a club? | [`docs/club-readiness-measurements.md`](docs/club-readiness-measurements.md) |
| What's being discussed or planned now? | GitHub issues and pull requests, e.g. the M4A follow-ups in issues #3–#9 |

## Conventions

- **C++20, warnings on.** Everything in `src/core` is in `namespace beatdown`. `src/core/std_names.hpp` brings common `std::` names into that namespace, and deliberately leaves a few qualified.
- **One responsibility per source file.** Each `src/core/x.cpp` is tested by `tests/test_x.cpp`, with one `TEST_CASE` per behaviour and ASCII-only test names.
- **Paths are `std::filesystem::path`, text is UTF-8.** Convert between them with `path_to_utf8` / `path_from_utf8`, never through the platform's narrow encoding.
- **Comments explain why,** often citing the requirement, review finding or measurement behind a rule.
- **Commit messages use conventional prefixes:** `feat`, `fix`, `docs`, `test`, `refactor`, `ci`.
