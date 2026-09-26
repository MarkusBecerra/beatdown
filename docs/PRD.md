# beatdown — Product Requirements

| | |
|---|---|
| **Status** | v0.3 — reviewed; all 16 open questions decided (v0.2: C++, cross-platform v1; v0.3: FLAC out, R26–R28, remaining decisions) |
| **Date** | 2026-09-15 |
| **Owner** | Markus Becerra |
| **Repo** | `beatdown` (MIT) |

## 1. Summary

`beatdown` is a local, batch converter that turns a folder of WAV files into 320 kbps MP3s ready for rekordbox. Point it at the folder you download into, give it a destination, and it produces one MP3 per WAV with the same filename, using every core on the Mac, skipping anything it has already done. Nothing leaves the machine.

v1 is deliberately narrow: WAV (or AIFF or FLAC) in, 320 kbps CBR MP3 out by default — FLAC out on request — command line, one codebase and one self-contained binary each for macOS, Windows and Linux. The internals are split into a decoder and an encoder so the README's broader promise — "convert audio files to different formats" — is a later addition, not a rewrite.

## 2. Problem

You subscribe to a Patreon that releases unreleased house music as WAV files, and you download a lot of it. WAV is the wrong format for a DJ library:

- **Size.** A 16-bit/44.1 kHz stereo WAV is 10.6 MB per minute; 24-bit/48 kHz is 17.3 MB per minute. A six-minute house track is 64–104 MB. At 320 kbps an MP3 is 2.4 MB per minute — 14 MB for the same track, a 4.4–7.2× reduction with no audible difference on a club system.
- **rekordbox convention.** 320 kbps MP3 is the format DJs standardise on: it plays on every CDJ ever made that reads MP3, exports to USB quickly, and keeps the library small enough to back up.
- **Online converters aren't an option.** Uploading unreleased music to an anonymous web service is a privacy problem and a trust problem (you can't verify what encoder or settings they used), and dragging files through a browser one batch at a time doesn't scale to a subscription that keeps delivering.

There is no built-in macOS tool for this. Apple's AudioToolbox — the engine behind `afconvert`, Music.app and QuickTime — decodes MP3 but does not encode it. *(Verified on this Mac: `afconvert -f MPG3 -d .mp3` fails with `'fmt?'`.)*

## 3. Goals

1. **Batch or single file.** One command converts a whole folder tree, or just one file when that is all you have (R5). Same tool, same output, same options either way.
2. **Local and private.** Conversion happens on your own machine; files are never sent to an online converter. *(Revised after v1: this used to read "no network access, ever". The concern was ad-laden online conversion services, not the network itself, so beatdown may use the network if a feature ever needs it, as long as the music stays on your machine.)*
3. **Correct output for rekordbox.** 320 kbps constant bitrate MP3 by default, ID3v2 tags, filenames preserved exactly; rekordbox must show *320 kbps* on import. FLAC on request (`--format flac`), lossless and tagged, for players that read it.
4. **Safe.** Source files are never modified or deleted. No half-written MP3s are ever left in the output folder, even on Ctrl-C.
5. **Incremental, and never clobbers.** A file whose output already exists in the destination is skipped and reported, so re-running on a folder converts only what's new — point it at the whole Downloads folder and it does the right thing. Re-encoding an existing output is an explicit `--overwrite`.
6. **Fast.** Encodes in parallel across all cores. A 100-track batch should finish in a couple of minutes on an M-series Mac.
7. **Clear about what happened.** A per-file progress line and an end-of-run summary: converted / skipped / failed, size before and after, elapsed time. Failures are reported, never silently swallowed.
8. **Cross-platform and self-contained.** One codebase; the same command and the same behaviour on macOS, Windows and Linux; a single binary per platform with the encoder built in, so a fresh machine needs nothing installed first.

## 4. Non-goals (v1)

- Any output format other than MP3 320 (default) and FLAC. (Architecture allows adding formats later — see §7 and §10.1.)
- A graphical interface — but see Q1; the core is a library, so a GUI (a real project of its own once it has to work on three platforms) can be added later without touching the conversion logic.
- Loudness normalisation, trimming, fades, or any audio processing beyond format conversion.
- rekordbox library integration (XML import/export, playlist creation). Converted files land in your main Music folder and rekordbox imports from there, as it does today.
- Reading tags from a database or the web. Tags come only from the source file or its filename.
- Deleting source WAVs after conversion (see Q8).
- Timestamp-aware re-runs ("re-encode if the *source* is newer than its output"). The basic incremental behaviour — skip anything already converted — is in v1 (Goal 5, R12). What's deferred is only the refinement that notices a source file was replaced with a newer version and re-encodes it; that's an opt-in `--refresh` flag for M3 (see Q5).
- A watch mode that runs by itself when new files appear (a background process listening to the Downloads folder). That is a different feature from incremental, with its own problems — half-downloaded files, zips that still need unzipping, a process that has to be running. Not planned; could be an M3 item if the manual run gets tedious.

## 5. Users and workflow

One user today: a DJ running rekordbox, mainly on an Apple Silicon Mac, who wants the same tool to work on Windows and Linux machines too. Comfortable in the terminal.

The library lives on an external drive: the main Music folder is `/Volumes/LaCie/Music`. Today's workflow is: download the drop, drag the music into that folder, import into rekordbox from there.

With beatdown in the loop:

1. Download a release from Patreon. About 75% of the time it arrives as a zip; unzip it into a folder (say `~/Downloads/Patreon Sept Drop/`).
2. Run `beatdown "~/Downloads/Patreon Sept Drop" "/Volumes/LaCie/Music/Patreon Sept Drop"` — the destination is the release's folder inside the Music folder on the LaCie, so the MP3s land in the library directly. If you'd rather keep the drag step, use any scratch destination and drag the result into `/Volumes/LaCie/Music` as you do now; the tool doesn't care which.
3. Import into rekordbox from `/Volumes/LaCie/Music`, as you do today.

Because sub-folders are mirrored (R1), a release that arrives as a folder stays a folder inside Music, and because outputs are never overwritten (R12), a run can't touch tracks already in the library. Writing to an external drive adds one rule the tool must get right: if the LaCie isn't mounted, `/Volumes/LaCie` doesn't exist, and naively creating the destination would silently write a new `LaCie` folder onto the internal disk — so the tool creates only the last folder of the destination path and refuses if its parent is missing (R25).

Because a file whose output already exists is skipped (R12), re-running is always cheap: on a fresh unzipped folder everything converts; on the whole Downloads folder only the new tracks do. Accepting the zip directly, without the unzip step, is a natural M3 addition.

## 6. Requirements

### 6.1 Input

- **R1.** Accept a source directory. Recursive by default; mirror the sub-folder structure into the destination. *(Q4)*
- **R2.** Convert files with `.wav`, `.aif`/`.aiff` and `.flac` extensions (case-insensitive). All three are read by the same decoder call; WAV is the primary case, the other two cost nothing. *(Q9)*
- **R3.** Accept 16-, 24- and 32-bit integer and 32-bit float PCM WAV, mono or stereo, at any sample rate.
- **R4.** Ignore non-audio files and macOS `._*` resource forks silently; report a count of ignored files in the summary.
- **R5.** Accept a single file as the source as well as a directory.

### 6.2 Output

- **R6.** One output file per source file, same basename, extension set by the output format (`.mp3` in v1 — other formats are roadmap, §10.1), in the destination directory (mirroring sub-folders per R1).
- **R7.** Default output is MP3: encode at **320 kbps constant bitrate**, joint stereo, highest-quality encoder setting (LAME `-q 0`).
- **R26.** `--format flac` produces FLAC instead: lossless, source bit depth and sample rate preserved unchanged (R8's resampling rule applies to MP3 only), compression level 8, tags written as Vorbis comments (same fields as R9). MP3 stays the default because it's the format every club CDJ plays; FLAC is for NXS2-and-later players and the laptop.
- **R27.** MP3 encoding options: `--bitrate <kbps>` selects a different CBR bitrate (any LAME-legal value: 128, 192, 256, 320 …; default 320) and `--vbr <0-9>` switches to VBR at that LAME quality level (0 = V0, highest). The two are mutually exclusive. Everything else about the MP3 path (joint stereo, `-q 0`, tags, sample-rate rule) is unchanged. In M1 because it's the same libmp3lame calls with different arguments.
- **R8.** Sample rate: keep the source rate if it is 44.1 or 48 kHz; resample to 48 kHz if higher (MP3 does not support 88.2/96 kHz). The Patreon files are 48 kHz, so the normal path is "keep" and the MP3s come out at 48 kHz. Test fixtures must include 48 kHz sources. *(Q6)*
- **R9.** Write ID3v2.3 tags. Carry across any tags present in the source (WAV `LIST/INFO` or embedded `id3` chunk). With `--tag-from-name`, derive Artist and Title from the filename when the source has none: split on the first ` - `, Artist is the left side, Title the right, casing kept exactly as written. `--strip-suffix TEXT` (repeatable) removes trailing export junk from the derived Title. The Patreon's files look like `simple fact - slipz Mastered_Master.wav`, so `--tag-from-name --strip-suffix " Mastered_Master"` gives Artist `simple fact`, Title `slipz`. The output *filename* is never altered (R6). *(Q7)*
- **R10.** Write to a temporary file in the destination folder and rename to the final name only on success, so an interrupted run never leaves a partial `.mp3` with the final name.
- **R11.** Preserve the source file's modification time on the output, so sorting by date in Finder or rekordbox reflects when the track was released, not when it was converted.
- **R25.** Create the destination folder if it's missing, but only its last path component: if the parent doesn't exist (an unmounted external drive, a typo), stop with a clear message rather than creating the whole path. Temporary files (R10) are written on the destination's own volume so the final rename is atomic on an external drive too.
- **R28.** Before the first encode, estimate the total output size and compare it with the free space on the destination volume (`std::filesystem::space`). The estimate needs no decoding: the decoder reports each file's frame count and sample rate up front, so MP3 is duration × bitrate, and FLAC is taken as 70% of the source PCM size (a conservative bound). If free space is below the estimate plus a 10% margin, stop before writing anything and say how much is needed versus available. `--dry-run` prints the same projection. If the disk fills mid-run anyway, the file that hit it fails (R14) and the batch stops launching new encodes rather than failing every remaining file the same way.

### 6.3 Batch behaviour

- **R12.** If the destination `.mp3` already exists, skip the file and report it as skipped. `--overwrite` forces re-encoding. No timestamp comparison in v1. *(Q5)*
- **R13.** Run N encodes in parallel, N defaulting to the hardware thread count (`std::thread::hardware_concurrency()` — 10 on this M1 Pro); `--jobs N` overrides.
- **R14.** Continue past a failed file; report every failure at the end with the reason; exit non-zero if any file failed.
- **R15.** `--dry-run` lists what would be converted, skipped and ignored without writing anything.
- **R16.** Ctrl-C stops launching new encodes, lets in-flight encodes finish or kills them, cleans up any temporary files, and prints the summary for what completed.

### 6.4 Verification

- **R17.** After each encode, confirm the output exists, is non-trivially sized, and reads back correctly: for MP3, the requested CBR bitrate (or a VBR stream with a valid Xing/Info header when `--vbr`) with the expected duration (±1 s); for FLAC, the same channel count, sample rate and frame count as the source. Treat any mismatch as a failure for R14.

### 6.5 Reporting

- **R18.** During the run: one line per file as it completes, showing name, result, and size in → out.
- **R19.** At the end: totals for converted / skipped / failed / ignored, total bytes before and after, elapsed time.
- **R20.** `--quiet` prints only the summary and failures; `--verbose` prints the underlying encoder command and output.

### 6.6 Dependencies and install

- **R21.** No runtime dependencies. The decoder and encoder are linked into the binary; on a fresh macOS, Windows or Linux machine the binary runs without installing anything.
- **R22.** One codebase, one build recipe: `cmake --preset default && cmake --build --preset default` works unchanged on all three platforms, with dependencies resolved by a vcpkg manifest. CI builds and runs the test suite on macOS, Windows and Linux on every push and attaches release binaries for each.
- **R23.** Paths, filenames and console output are Unicode-correct on every platform — including Windows, where that takes deliberate work (wide-character file APIs, UTF-8 console output).
- **R24.** Ctrl-C (SIGINT on macOS/Linux, `CTRL_C_EVENT` on Windows) triggers the same clean shutdown described in R16.

## 7. Technical approach

### 7.1 Recommendation

**Language: C++20**, one CMake project with two targets — `beatdown_core` (a static library: file discovery, job scheduling, decoder, encoder, verification, reporting) and `beatdown` (a thin command-line executable using CLI11). Dependencies come through a vcpkg manifest so the same `cmake` commands work on macOS, Windows and Linux.

**Encoder: libmp3lame, linked into the binary.** With three operating systems in scope, shelling out to a `lame` executable would mean three install stories and two process-spawning implementations. Linking the library gives one self-contained binary per platform, no runtime dependency, and — because it is the same encoder everywhere — the same MP3 from the same WAV whether it was made on the Mac, a Windows box or a Linux server. Platform encoders can't offer that: macOS has none (verified), Windows Media Foundation has one with different behaviour, Linux has none.

To be clear: **LAME is still the encoder.** `libmp3lame` *is* LAME, packaged as a library; the `lame` command-line tool is a thin wrapper around it. The earlier Swift plan would have launched that wrapper as a separate process for every file; this plan calls the library directly from inside `beatdown`. Same code, same settings, same MP3 — the only difference is that nothing has to be installed alongside the tool.

**Decoder: libsndfile.** Reads every WAV variant (16/24/32-bit, float, `WAVE_FORMAT_EXTENSIBLE`), AIFF and FLAC through one API, exposes the source file's `LIST/INFO` metadata, and is packaged for all three platforms. Both libraries are LGPL — the same licence as each other, so adding the second costs nothing the first didn't.

Why C++: two reasons from you — Windows and Linux compatibility, and it's the language you know best, which matters more than anything else for a tool you'll maintain yourself. And it fits: it is the natural host for two C libraries, has first-class toolchains on all three targets (Apple clang here, MSVC on Windows, GCC/clang on Linux), and produces a single static-ish binary using `std::filesystem`, `std::thread` and a small platform shim for the two things that genuinely differ (Ctrl-C handling, console UTF-8 and wide-char file open on Windows). Swift was the recommendation while this was macOS-only; it does run on Linux and Windows, but without AudioToolbox and with a rougher toolchain and packaging story on both, which removes the reason to prefer it.

The reference encode, expressed as the LAME API calls the tool makes for every file:

```
lame_set_num_channels(gf, 2);   lame_set_in_samplerate(gf, 44100);
lame_set_brate(gf, 320);        lame_set_VBR(gf, vbr_off);          // CBR 320
lame_set_quality(gf, 0);        lame_set_mode(gf, JOINT_STEREO);
id3tag_set_title(gf, "Title");  id3tag_set_artist(gf, "Artist");   id3tag_add_v2(gf);
```

Equivalent to `lame --cbr -b 320 -q 0 --add-id3v2` on the command line, which is what M0 uses to check the settings before any code exists.

### 7.2 Alternatives considered

| Option | Verdict |
|---|---|
| **Swift (SwiftPM + AudioToolbox)** | The right choice for a Mac-only tool, and the original recommendation. Cross-platform Swift exists, but the Windows toolchain and packaging are still second-tier and there is no AudioToolbox off the Mac, so its advantages disappear. |
| **Rust** | The strongest alternative to C++ for a portable single binary: `cargo` handles builds and dependencies on all three OSes and the `mp3lame-encoder` crate binds libmp3lame. Not installed here and you chose C++; the fallback if C++ tooling grates. |
| **Go** | Portable and simple, but no maintained MP3-encoder binding, and cgo makes builds with C libraries awkward. |
| **ffmpeg libraries (libavcodec / libavformat), linked** | The one dependency that covers the entire §10.1 roadmap — ALAC, AAC, M4A in and out, every container, all metadata — and it wraps the same libmp3lame for MP3, so v1 output would be identical. Costs: 10–30× the binary size of sndfile + lame even when trimmed to audio only, a much larger and more error-prone C API, and CI builds of 15+ minutes per platform until vcpkg's binary cache warms up. Overkill for v1; the right backend the day ALAC/AAC are wanted (Q16). |
| **ffmpeg as a subprocess** | Zero C-API work and everything ffmpeg can do. But either a per-OS install step or bundling an ~80–100 MB static ffmpeg per platform in every release, plus process spawning and stderr parsing per OS. Fastest to write, worst to ship. |
| **`lame` as a subprocess** | Same install and spawn costs as ffmpeg-as-subprocess for a fraction of the capability. Superseded by linking libmp3lame. |
| **Platform encoders (AudioToolbox / Media Foundation)** | AudioToolbox cannot encode MP3 (verified). Media Foundation can on Windows, but that would mean a different encoder — and different output — per OS. |
| **Python + subprocess** | Fastest to write, worst to distribute on three platforms. |

### 7.3 Architecture

```
beatdown (CLI)                 — CLI11 argument parsing, output formatting, exit codes
  └─ beatdown_core (library)
       ├─ Scanner               — walks source, pairs each input with its output path,
       │                          decides convert / skip / ignore (R1–R5, R12)
       ├─ Scheduler             — thread pool with a bounded queue, cancellation on
       │                          Ctrl-C (R13, R16, R24)
       ├─ Decoder               — libsndfile: any WAV/AIFF/FLAC → PCM + metadata
       │                          (R2, R3, R9)
       ├─ Encoder (interface)   — two implementations in v1: LameEncoder (libmp3lame,
       │                          LAME as a linked library, not a subprocess) and
       │                          FlacEncoder (libsndfile); both write to temp and
       │                          rename on success (R7, R26, R10)
       ├─ Verifier              — MP3: parses the frame headers it just wrote (bitrate,
       │                          sample rate, frame count vs. expected duration);
       │                          FLAC: reads back via libsndfile and compares (R17)
       ├─ Report                — per-file events + end-of-run totals (R18–R20)
       └─ platform/             — the only OS-specific code: Ctrl-C handler, console
                                  UTF-8 and wide-char file open on Windows (R23, R24)
```

The `Encoder` interface is the seam: a FLAC or Opus encoder plugs in here without changing the scanner, scheduler or reporting. `platform/` is deliberately tiny so the rest of the code never sees `#ifdef _WIN32`.

### 7.4 Performance expectation

LAME is single-threaded and encodes CBR 320 at well over 50× real time per core on Apple Silicon, so a 6-minute track takes under 10 seconds on one core. With 8–10 parallel jobs a 100-track batch should finish in roughly one to two minutes. This is an estimate; M0 measures it.

### 7.5 CLI sketch

```
beatdown <source> <destination> [options]

  <source>          folder (or single file) of WAV / AIFF / FLAC files
  <destination>     folder to write MP3s into; created if missing

  --format mp3|flac output format (default: mp3 — 320 kbps CBR; flac — lossless)
  --bitrate KBPS    MP3 CBR bitrate (default: 320)
  --vbr N           MP3 VBR at LAME quality N, 0 = best (instead of --bitrate)
  --jobs N          parallel encodes (default: all hardware threads)
  --overwrite       re-encode even if the output exists and is up to date
  --no-recursive    only the top level of <source>
  --tag-from-name   derive Artist/Title from "Artist - Title" filenames when untagged
  --strip-suffix S  drop trailing text S from the derived Title (repeatable)
  --dry-run         show the plan, write nothing
  --quiet / --verbose
  --version / --help

exit 0  everything converted or skipped
exit 1  one or more files failed (details printed)
exit 2  bad arguments
```

Example session (identical on Windows apart from the paths, e.g. `beatdown "D:\Patreon Sept Drop" "D:\DJ\Patreon"`):

```
$ beatdown "~/Downloads/Patreon Sept Drop" "/Volumes/LaCie/Music/Patreon Sept Drop"
Found 12 WAV files, 0 already in destination, 12 to encode (8 jobs)

  ✓ Some Artist - Deep Cut (Original Mix).mp3          71.2 MB → 15.9 MB   6.4s
  ✓ Some Artist - Late Nights.mp3                      58.0 MB → 12.9 MB   5.1s
  ✗ Another Artist - Broken Export.wav                  RIFF header truncated
  ...

Converted 11, skipped 0, failed 1, ignored 2 non-audio files
Size 812.4 MB → 178.6 MB (−78%)   Elapsed 0:19
```

## 8. Open questions

All sixteen were decided in review; each entry records the decision and what it changed. They stay here as the record of why the requirements read the way they do.

- **Q1. Interface — CLI only, or a small GUI?**
  *Decided in review: CLI first; a real GUI is a confirmed follow-up on the roadmap.* The core is a library so the GUI reuses the conversion logic unchanged. Until then, each OS has a cheap right-click "UI" that needs no GUI code: a Finder Quick Action on macOS, a *Send to* / context-menu entry on Windows, a Nautilus script on Linux — all just call the CLI. The GUI itself (Qt, or one shell per OS) is a project of its own and is listed in M3.

- **Q2. Language — Swift, or C++?**
  *Decided in review: C++, with Windows and Linux as v1 targets — and because it's the language you understand best.* This flipped the recommendation (§7.1): once portability is a goal, Swift's advantages are gone, C++ is the natural host for the two C libraries the tool is built on, and familiarity is the deciding factor for a project you maintain alone.

- **Q3. Encoder dependency — shell out to `lame`, or link libmp3lame?**
  *Decided in review: link it.* One self-contained binary per OS, no install step anywhere, and the same encoder — so the same output — on every platform.

- **Q4. Sub-folders — recurse and mirror the structure, or flatten?**
  *Decided in review: recurse and mirror.* A release that arrives as a folder stays a folder inside `/Volumes/LaCie/Music`; a flat drop stays flat. `--no-recursive` remains for the odd case.

- **Q5. Incremental re-runs — needed in v1?**
  *Decided in review: yes, and it's already what R12 does.* Skip any file whose output exists, convert the rest — so running on a fresh unzipped drop or on the entire Downloads folder both work, and nothing is ever re-encoded by accident. The only piece deferred to M3 is timestamp comparison (re-encode when the source is newer than its output), as an opt-in `--refresh`.

- **Q6. Sample rate — keep the source rate, or force 44.1 kHz?**
  *Decided in review: the Patreon files are 48 kHz; keep it.* The MP3s will be 48 kHz, which rekordbox and every CDJ from the 2000 onward play without complaint. Anything higher than 48 gets downsampled to 48; 44.1 sources stay 44.1. A `--sample-rate 44100` override can be added later if an older player ever needs it.

- **Q7. Tags — derive Title/Artist from the filename when the WAV is untagged?**
  *Decided in review: yes, opt-in.* Filenames look like `simple fact - slipz Mastered_Master` — `Artist - Title` plus a mastering-export suffix. So `--tag-from-name` splits on the first ` - `, and `--strip-suffix " Mastered_Master"` cleans the Title; both in R9. Filenames themselves stay exactly as downloaded; if you'd rather the output be named `simple fact - slipz.mp3` too, that's a one-line `--rename-stripped` option to add later.

- **Q8. Delete source WAVs after a verified conversion?**
  *Decided in review: no for v1; an opt-in `--delete-source` later (M3).* Keep the originals until the tool has earned trust; when it lands, it deletes only after R17 verification passes for that file, never on a failure.

- **Q9. Input formats — WAV only, or WAV + AIFF now and FLAC later?**
  *Decided in review: WAV, AIFF and FLAC input in v1.* libsndfile reads all three through the same call, so it's only an entry in the scanner's extension filter and a test fixture each. ALAC/M4A are not covered and would need a second decoder later (Q16). *Update: M4A input (AAC and ALAC) is done — see §10.1.*

- **Q10. Output format — is 320 MP3 the right target for *unreleased* music?**
  *Decided in review: MP3 320 is the default; FLAC output is also in v1 (`--format flac`, R26).* You play on club-standard CDJs and rekordbox on the laptop. Every club CDJ plays 320 MP3, which is why it stays the default and the industry convention; NXS2/3000-era decks and the laptop also play FLAC, lossless at ~55–60% of WAV, so it's there for when the room supports it. Both encoders sit behind the same `Encoder` interface (§7.3); the FLAC one is libsndfile writing instead of reading.

- **Q11. Where do converted files go relative to rekordbox?**
  *Decided in review: into the main Music folder on the LaCie (`/Volumes/LaCie/Music`), and rekordbox imports from there.* The tool can write there directly (destination = the release's folder inside that Music folder), which removes the manual drag. Two consequences already in the requirements: R12 means a run can never overwrite a track that's already in the library, and R1's folder mirroring keeps a release together. One cheap convenience to consider for M2: `--into ~/Music`, which sets the destination to `~/Music/<source folder name>` so the command is the same every time.

- **Q12. Distribution — build from source, or downloadable binaries?**
  *Decided in review: GitHub Releases with a binary per platform, built by CI, from v1.* Package managers (Homebrew tap, winget, AUR) once it's stable.

- **Q13. Which platforms exactly?**
  *Decided in review: macOS universal (arm64 + x86_64), Windows 10/11 x64, Linux x86_64 (glibc, Ubuntu 22.04 or newer).* Linux arm64 if it's free in CI. Windows arm64 and 32-bit anything are out.

- **Q14. Do you have a Windows or Linux machine to test on, or is CI the only place those builds run?**
  *Decided in review: real machines for all three.* So CI proves the build and the test suite, and the release binaries get run by hand on each OS before v1 is called done — including the rekordbox import check on Windows (criterion 1) and a run against the actual library drive on each (R25). Criterion 7 is worded accordingly.

- **Q15. Windows toolchain — MSVC or MinGW?**
  *Decided in review: MSVC (Visual Studio Build Tools).* It's what vcpkg and GitHub's Windows runners assume, and it produces a binary with no MinGW runtime baggage.

- **Q16. ffmpeg — now, later, or never?**
  *Decided in review: libsndfile + libmp3lame for v1; ffmpeg deferred to M3.* For WAV → MP3, that's the *same encoder* ffmpeg would use, in a tenth of the dependency, with a far simpler API. The format roadmap is what would bring ffmpeg in: ALAC, AAC and M4A each need pieces — an MP4 muxer/demuxer, an ALAC codec, an AAC codec — that ffmpeg's libraries bundle and nothing else provides cleanly. When those formats get scheduled in M3, `libavcodec`/`libavformat` go behind the same `Decoder`/`Encoder` interfaces as a second backend (or replace the first outright); the interfaces exist so that either is possible without touching the scanner, scheduler or reporting. *Update: done for input — FFmpeg's libavformat/libavcodec are the second backend, decoding M4A (AAC and ALAC) behind the `Decoder` interface, while libsndfile still reads WAV/AIFF/FLAC.*

## 9. Success criteria

The v1 is done when all of these hold on this Mac with real Patreon files:

1. A folder of N WAVs produces N MP3s with identical basenames; rekordbox imports them and displays **320 kbps** for every one.
2. Running the same command a second time into the same destination encodes nothing, reports N skipped, and leaves the existing MP3s byte-for-byte untouched.
3. A deliberately corrupt WAV in the batch fails alone, is named in the summary, and the exit code is 1.
4. Ctrl-C mid-batch leaves no `.mp3` in the destination that isn't complete and valid.
5. 100 tracks encode in under three minutes on the M1 Pro.
6. A/B listening on studio monitors between the WAV and the MP3 is not distinguishable (the LAME CBR-320 baseline; this confirms the settings, not the tool).
7. The build and the full test suite pass in CI on macOS, Windows and Linux, and the MP3 each platform produces from the same WAV matches in bitrate, sample rate, duration and tags. A release run attaches a binary for each, and each binary has been run by hand on a real machine of that OS against a real Patreon drop.
8. `--format flac` produces FLACs that rekordbox imports, and decoding one back yields PCM bit-identical to the source WAV.

## 10. Milestones

| | Scope | Exit |
|---|---|---|
| **M0 — Spike** | Two halves. (a) `brew install lame`; encode one real Patreon WAV with `lame --cbr -b 320 -q 0`; import to rekordbox; time it. (b) A ~60-line C++ program that links libmp3lame + libsndfile via vcpkg and does the same encode, plus a FLAC write of the same file through libsndfile — built on the Mac *and* in a CI matrix on Windows and Linux. | (a) confirms the encoder settings, the rekordbox bitrate readout and the per-track time; (b) proves the cross-platform toolchain and both encoders before the real code depends on them. |
| **M1 — Core CLI** | CMake + vcpkg project with CI matrix from day one; scanner, scheduler, libsndfile decoder, libmp3lame encoder with `--bitrate`/`--vbr` (R27), libsndfile FLAC encoder behind `--format flac` (R26), temp-file-and-rename, skip-existing, parallel, summary. | Success criteria 1, 2, 5 and 8. |
| **M2 — Hardening** | Verification (R17), Ctrl-C cleanup on both signal models (R16, R24), Windows Unicode paths and console (R23), failure reporting (R14), dry-run, tags (R9), mtime (R11), README, right-click recipes per OS, release binaries from CI. | Success criteria 3, 4, 6, 7. |
| **M3 — Optional** | Accept a `.zip` as the source; `--refresh` (re-encode when the source is newer than its output); `--delete-source` after verified conversion (Q8); a real cross-platform GUI (confirmed follow-up, Q1); package managers (Homebrew tap, winget, AUR); ffmpeg's libraries as a second backend for the M4A-family formats (Q16; M4A input done), and the rest of the roadmap in §10.1. | Whichever of these you still want after using v1 for a few weeks. |

### 10.1 Format roadmap

Not v1, but the README's promise is "convert audio files to different formats", and the architecture is built for it: libsndfile already decodes several formats and can *write* several too, and every encoder sits behind the same `Encoder` interface. Formats surface as `--format <name>` (default `mp3`); the output extension follows the format. Ordered by value to a DJ divided by effort:

| Format | Direction | Why a DJ wants it | Effort | Notes |
|---|---|---|---|---|
| **MP3 320 CBR** | out | Universal; plays on every CDJ that reads MP3. | v1 | — |
| **FLAC** | out | Lossless at ~55–60% of WAV. The right target for unreleased music if your players support it. | M1 | libsndfile writes FLAC, so it's a second `Encoder` over a library already linked. Pulled into the MVP in review (Q10); the M0 spike exercises it. |
| **AIFF** | out | Lossless, same size as WAV, but carries ID3 tags properly — WAV tagging is a mess. Popular on Mac. | Small | libsndfile writes AIFF. |
| **WAV** | out | Downconvert 24-bit/96 kHz masters to 16/44.1 for old players, or FLAC → WAV. | Small | libsndfile writes WAV; add `--bit-depth 16\|24`. |
| **MP3 options** | out | `--bitrate`, `--vbr` (V0) for non-DJ use. | M1 | Same libmp3lame calls with different arguments; pulled into M1 in review (R27). |
| **ALAC (.m4a)** | out | Apple Lossless; rekordbox and NXS2+ players read it. | Medium | Needs an ALAC encoder (Apple's is open source) and an MP4 muxer; libsndfile doesn't do the M4A container. ffmpeg's libraries have both (Q16). |
| **AAC (.m4a)** | out | Smaller than MP3 at equal quality; NXS2+ players read it. | Hard | No clean encoder: fdk-aac's licence is awkward for an MIT project, and platform encoders break "same output on every OS". Probably never. |
| **FLAC** | in | Convert lossless downloads to MP3 for USB sticks. | v1 | Free with libsndfile; pulled into v1 in review (Q9). |
| **ALAC / AAC (.m4a)** | in | Convert Apple-format purchases. | Done | FFmpeg's MP4 demuxer and its own AAC and ALAC decoders, as a second backend behind the `Decoder` interface (Q16). Other codecs in an MP4 are refused. |
| **MP3** | in | Only as pass-through (copy + retag), never re-encode: lossy → lossy degrades. | Small | libsndfile reads MP3; the tool should refuse `mp3 → mp3` re-encoding unless forced. |
| **Opus / Ogg** | either | No Pioneer player or rekordbox support. | — | Skip. |

Player compatibility, as far as it matters here: CDJ-2000NXS2 and everything after it (CDJ-3000, XDJ-XZ, XDJ-RX2/RX3) play FLAC and ALAC; the original CDJ-2000 and 2000NXS do not, and play MP3/AAC/WAV/AIFF only. Check the spec sheet for whatever you actually play on before choosing a lossless target.

## 11. Risks

- **Cross-platform toolchain friction is now the biggest risk** — vcpkg bootstrap, MSVC quirks, a library that builds on two platforms and not the third. M0(b) exists to hit this before anything depends on it; Rust is the documented fallback if it turns into a time sink.
- **Windows Unicode.** DJ filenames are full of parentheses, apostrophes, ampersands and non-ASCII. On Windows that means wide-character file opens (`sf_wchar_open`, `_wfopen`) and switching the console to UTF-8, or names silently mangle. Covered by R23 and a test fixture with non-ASCII names on every platform.
- **Odd WAV exports** (broken headers, unusual chunk layouts from a producer's DAW). libsndfile handles the legitimate variants; anything else fails one file, not the batch (R14).
- **LGPL.** libmp3lame and libsndfile are LGPL. For a personal tool this is moot; if the binaries are published, either link them dynamically or ship the build recipe so relinking is possible — the standard way an MIT project satisfies the LGPL.
- **Disk space.** Source and output coexist; a 500-file batch adds ~7 GB of MP3, or ~25 GB of FLAC, to a library drive that may already be nearly full. R28 checks before starting and stops the batch early if the disk fills anyway.
- **Unmounted library drive.** The destination is on an external drive. If it's unplugged, the obvious implementation writes a phantom `/Volumes/LaCie/Music` onto the boot disk and the files vanish from the library the next time the drive is mounted over it. R25 prevents this. The same applies to a missing drive letter on Windows and an unmounted `/media` path on Linux.
- **rekordbox caching an old analysis** if a file is overwritten in place with `--overwrite`. Document: re-analyse or remove and re-add.

## 12. Facts verified on this machine

| | |
|---|---|
| macOS | 26.6.2, Apple M1 Pro, 10 cores (8 performance) |
| Swift | 6.3.3 via Command Line Tools (no Xcode) |
| Homebrew | `/opt/homebrew/bin/brew` |
| ffmpeg / lame / sox | not installed |
| `afconvert` MP3 encode | **fails** — `ExtAudioFileSetProperty ('cfmt') failed ('fmt?')` |
| rekordbox | installed (`~/Library/Pioneer`) |
| C++ toolchain | Apple clang 21 (`/usr/bin/clang++`) present; `cmake`, `ninja`, `vcpkg` **not installed** (`brew install cmake ninja` + a vcpkg checkout) |
| Go / Rust | not installed |
