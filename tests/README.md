# `tests/`: the test suite

The automated tests for everything in [`../src/`](../src/README.md), written with [Catch2 v3](https://github.com/catchorg/Catch2) and built into one executable, `beatdown_tests`. CI runs the whole suite on macOS, Windows and Linux for every push.

## Running them

```
cmake --preset default
cmake --build --preset default
ctest --preset default                 # everything; shows output only for failures
build/default/beatdown_tests "*M4A*"   # or run the binary directly, filtered by test name
```

CMake registers every Catch2 `TEST_CASE` as its own ctest test. Keep test names **ASCII-only**: ctest passes the name back to the test binary on the command line, and on Windows that goes through the ANSI code page, so a non-ASCII name would match no test.

## How the tests are built

- **No binary test files are checked in.** Every audio file a test needs is generated at test time, in a fresh temporary folder, from a few parameters. This keeps the repository small and makes each test's input visible in its own code.
- **One file per module.** `test_x.cpp` tests `src/core/x.cpp`; `test_cli.cpp` tests the executable.
- **One `TEST_CASE` per behaviour**, named as a sentence describing that behaviour.
- **Tests exercise the real libraries.** Almost nothing is mocked: an MP3 test really encodes with LAME and really decodes the result. The exceptions are the runner tests, which can swap in a fake per-file conversion function and a recording reporter. Those two are hooks the engine exposes for exactly this.
- **The CLI tests run the real `beatdown` executable** as a separate process and check its exit code and output. CMake passes its path in as `BEATDOWN_BIN` and builds it before the tests.
- **Environment-dependent checks adapt rather than fail.** Tests that need to create symlinks `SKIP` where the OS doesn't allow it (e.g. Windows without Developer Mode). Assertions about Unicode-normalization twins apply only where the filesystem really treats the two names as one file (macOS APFS, not Linux ext4).

## `fixtures.hpp` / `fixtures.cpp`: shared helpers

| Helper | What it gives a test |
|---|---|
| `TempDir` | A fresh folder under the system temp directory, deleted again when the test ends. |
| `make_audio(path, spec)` | A test tone written with libsndfile as WAV, AIFF or FLAC, at any sample format, rate, channel count, length, level and frequency, optionally tagged. The `FixtureSpec` struct describes it, with sensible defaults (24-bit 48 kHz stereo, 1 s, 440 Hz at half scale). |
| `make_m4a(path, codec, spec, moov_first)` | The same tone as an M4A, encoded with FFmpeg's own AAC or ALAC encoder. It can also make an AC-3-in-MP4 file, a codec beatdown must refuse. `moov_first` puts the file's index before the audio, as iTunes does, instead of after it, as FFmpeg does by default. |
| `alac_samples(spec)` / `sine_for_test(spec)` | The exact samples a fixture was built from, so a lossless round trip can be checked sample for sample. |
| `write_bytes` / `read_file` | Write or read raw bytes. Used for corrupt files, and for proving an existing file was left untouched. |
| `kCorruptWav` | A deliberately broken WAV header. |
| `amp_for_dbfs(db)` | Converts a level in dBFS to a linear amplitude for building tones at a given level. |

## What each file covers

| File | Covers |
|---|---|
| `test_cli.cpp` | The executable end to end: help and version, bad arguments (exit 2), converting a folder and skipping it on a re-run, M4A alongside WAV to both formats, a corrupt file failing alone (exit 1), dry runs, the missing-parent destination rule. |
| `test_runner.cpp` | Whole batches through `run()`: the counts and exit codes, the destination rule, relative paths, dry runs, the free-space refusal, stopping after a disk-full error or Ctrl-C, same-output collisions, never overwriting a source under a differently spelled name, exceptions staying contained to one file, the "very loud master" count, and a 12-file batch giving identical results at `--jobs 1` and `--jobs 8`. |
| `test_converter.cpp` | One file through `convert_one()`: the temp-file name and cleanup, cancellation, creating sub-folders, overwriting, tag resolution and Windows-1252 tag repair, refusing MP3 sources, very short sources, and M4A (AAC→MP3 with tags, ALAC→FLAC bit-exact, AAC→FLAC, truncated and unsupported M4A). |
| `test_scanner.cpp` | `scan()`: which files count as audio, mirrored sub-folders, `--no-recursive`, ignored files, skip-existing, single-file sources, and every collision rule (same output, case-only differences, output onto a source, symlinked destination, destination inside the source). |
| `test_decoder.cpp` | The libsndfile decoder: WAV/AIFF/FLAC at various bit depths and rates, non-ASCII paths, corrupt files, tags (including Windows-1252 repair), integer reads and rewinding. |
| `test_m4a_decoder.cpp` | The FFmpeg decoder: recognising MP4 by content, bit-exact ALAC (16/24-bit, 6-channel), AAC level, exact length and sample alignment at several rates, tags, file layouts and names, refusing other codecs, damaged and truncated files (cut mid-packet and between packets), a packet that decodes to the wrong length, and rewinding. |
| `test_mp3_encoder.cpp` | LAME: the sample-rate rule, CBR and VBR settings, refusing settings LAME would silently change, low and high sample rates, mono and float input, ID3 tags (including non-ASCII), cancellation, and refusing more than two channels. |
| `test_flac_encoder.cpp` | FLAC output: bit-exact 16/24-bit, float and 32-bit sources written as 24-bit with the same audio, tags, 96 kHz, cancellation. |
| `test_verifier.cpp` | The header checks: good CBR and VBR MP3s pass; wrong bitrate, truncation, wrong duration, a missing or tiny file, and FLAC frame-count mismatches fail. |
| `test_content_check.cpp` | The audio checks, with synthetic material chosen to trip them: good MP3 and FLAC encodes pass; silence, wrong level and wrong content fail. Also high and low sample rates, near-silent channels, decoded-peak reporting (including a hard-clipped square wave that decodes above 0 dBFS), float sources over full scale, truncated MP3s, and "bright" material (white noise, high-passed risers and hi-hats) that an earlier design wrongly rejected. |
| `test_mp3_parse.cpp` | The MP3 frame walker: frame counting and duration, a leading ID3 tag, the Info/Xing frame (with and without CRC), mixed bitrates, trailing garbage, empty or non-MP3 files. |
| `test_id3v2.cpp` | ID3 tag reading in every text encoding, Windows-1252 repair, rejecting truncated tags, the v2.4 footer, and tags inside a WAV's `id3 ` chunk. |
| `test_report.cpp` | Terminal output: size and time formatting, exit codes, per-file and summary lines under `--quiet` / `--verbose` / `--dry-run`, the disk-full line, the failure recap, decoded peaks. |
| `test_tags.cpp` | Filename-derived tags ("Artist - Title"), suffix stripping, tag merging, and two `Options` helpers. |
| `test_unicode.cpp` | UTF-8/UTF-16/Latin-1 conversions, UTF-8 paths, and `sanitize_utf8` on every kind of malformed input. |
| `test_scheduler.cpp` | The thread pool: every job exactly once, real overlap, order with one worker, cancellation, exceptions. |
| `test_space.cpp` | Output-size estimates, the 10% margin, reading free space. |
| `test_platform.cpp` | File identity: the same file under two spellings matches, different files don't, a missing file has none. |
| `test_version.cpp` | The version string matches the project version. |

## Things to know

- The test binary links FFmpeg directly, not just through `beatdown_core`, because `make_m4a` uses FFmpeg's encoders and MP4 muxer. `test_m4a_decoder.cpp` also uses FFmpeg's demuxer to find where each packet sits inside a file.
- Some comments here cite the review finding or ruling that motivated a test ("Task 18 fix round 2", "Finding 3"). [`../docs/superpowers/plans/README.md`](../docs/superpowers/plans/README.md) explains the labels.
- One comment in `test_runner.cpp`, above the "differently sized siblings" test, still describes a file-size prefilter the scanner no longer uses. The test itself remains valid, and the rulings log lists the comment as a known leftover.

Up: [repository overview](../ARCHITECTURE.md)
