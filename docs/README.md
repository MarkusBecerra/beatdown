# `docs/`: requirements, measurements and build records

The documents *behind* the code: what beatdown must do and why, the audio measurements that justify its defaults, and the record of how v1 was built. Nothing in here is compiled or read by the program.

## `PRD.md`: product requirements

The spec the code implements, and the first place to look when you need to know *why* a behaviour exists. The problem it starts from: turning folders of unreleased WAV downloads into rekordbox-ready MP3s, locally, safely and in parallel. It is organized as follows:

| Section | What's there |
|---|---|
| §3 Goals | The eight promises, e.g. "Safe": sources are never modified and no half-written output is ever left behind. Goal 2 was revised after v1: network access is allowed, as long as the music stays on your machine. |
| §4 Non-goals | What v1 deliberately doesn't do: other output formats, a GUI, loudness normalisation or any other audio processing, rekordbox library integration, deleting sources. |
| §6 Requirements R1–R28 | The numbered rules code comments cite ("R25", "R28", …). Examples: R25 only creates the destination's last folder, R28 checks free space first, R17 verifies every output. |
| §7 Technical approach | Why C++ with libsndfile and LAME, and the alternatives weighed: Swift, Rust, Go, linking FFmpeg, shelling out to ffmpeg or lame, platform encoders, Python. Also the architecture sketch and the CLI sketch. |
| §8 Open questions Q1–Q16 | Each question with the decision taken in review. Q16 planned FFmpeg as a second decoder backend for M4A, which is now built. |
| §9–§10 | Success criteria and milestones, plus §10.1's format roadmap. ALAC/AAC (`.m4a`) input is marked done. |
| §11–§12 | Risks, and facts checked on the author's Mac. |

## `club-readiness-measurements.md`: are the conversions transparent?

A measurement report on synthetic "loud master" material comparing 320 kbps MP3 and FLAC output with the source.

**Verdict:**
- FLAC is bit-exact.
- The MP3s are transparent: no level change, flat to 20 kHz, then a cutoff around 20.2 kHz.
- One side effect: masters peaking near 0 dBFS decode slightly *over* full scale.
- MP3 decoders disagree on the start offset by about 23 ms.

Two features rest on these numbers:
- The **"very loud master" callout**, which reports decoded peaks above +1.0 dBFS.
- The **tolerances in the per-file audio content check**, for example comparing levels after a shared 16 kHz lowpass. Both are described in [`../src/core/README.md`](../src/core/README.md).

The README's "Club-ready?" section summarizes it.

## Subdirectories

- [`superpowers/`](superpowers/README.md) holds the v1 implementation plan and its decision log. It explains the labels code comments use to cite their origins: `R25` and `Q16` point back to the PRD above, while `Task 18`, `Ruling R-F`, `Finding 3` and `A2` point into the plan and log.

Up: [repository overview](../ARCHITECTURE.md)
