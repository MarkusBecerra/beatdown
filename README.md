# beatdown

![CI](https://github.com/MarkusBecerra/beatdown/actions/workflows/ci.yml/badge.svg)

beatdown is a command-line tool that batch-converts a folder of WAV, AIFF, FLAC or M4A (AAC or Apple Lossless) files into 320 kbps CBR MP3s — the format every CDJ and rekordbox standardise on, and the default here — or lossless FLAC (`--format flac`) for players that support it. It runs entirely on your machine, with no network access at all, and ships as a single self-contained binary for macOS, Windows and Linux, so a fresh machine needs nothing installed first.

## Install

Release binaries — macOS 11 or later (universal, arm64 + x86_64), Windows x64 and Linux x86_64 — are built by the release workflow and attached to [Releases](https://github.com/MarkusBecerra/beatdown/releases) when a version is tagged. None has been published yet, so for now build from source — one recipe on all three OSes.

Prerequisites: CMake 3.25 or newer, Ninja and a C++20 compiler; on macOS and Linux also pkg-config, the autotools (autoconf, automake, libtool) and, for an x86_64 build, nasm (FFmpeg's assembly; vcpkg fetches it itself on Windows) — on macOS, `brew install cmake ninja pkg-config autoconf automake libtool nasm`. On Windows, run the commands from a Visual Studio 2022 Developer prompt (MSVC).

```
git clone --recursive https://github.com/MarkusBecerra/beatdown.git
cd beatdown
./external/vcpkg/bootstrap-vcpkg.sh -disableMetrics   # Windows: .\external\vcpkg\bootstrap-vcpkg.bat -disableMetrics
cmake --preset default
cmake --build --preset default
```

`--recursive` matters: vcpkg is pinned as a submodule under `external/vcpkg`, and bootstrapping builds vcpkg itself before it can fetch the rest of the dependencies. The first configure then builds every dependency from source, which takes a while — FFmpeg (for M4A) is by far the biggest; later configures reuse them. The binary lands at `build/default/beatdown` (`build\default\beatdown.exe` on Windows).

Only macOS (Apple Silicon) has been built and run by hand so far — see *Platform status* below.

## Usage

```
beatdown <source> <destination> [options]

  <source>          folder (or single file) of WAV / AIFF / FLAC / M4A files
  <destination>     folder to write MP3s into; created if missing

  --format mp3|flac output format (default: mp3 — 320 kbps CBR; flac — lossless)
  --bitrate KBPS    MP3 CBR bitrate: 128, 160, 192, 224, 256 or 320 (default: 320)
  --vbr N           MP3 VBR at LAME quality N: 0 (best) .. 3, or 5 .. 8 (instead of --bitrate)
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

A Patreon drop typically arrives untagged, with the mastering house's export suffix still in the filename (`simple fact - slipz Mastered_Master.wav`). `--tag-from-name` splits that on the first ` - ` into Artist/Title, and `--strip-suffix` trims the suffix from the derived Title:

```
beatdown "~/Downloads/Patreon Sept Drop" "/Volumes/LaCie/Music/Patreon Sept Drop" --tag-from-name --strip-suffix " Mastered_Master"
```

gives Artist `simple fact`, Title `slipz` — the output *filename* is unaffected either way.

## Behaviour notes

- **Skip-existing.** If the destination file already exists, the source is skipped and reported; re-running on a folder you've already converted — or the whole Downloads folder — only encodes what's new. `--overwrite` forces re-encoding.
- **`--overwrite` and rekordbox.** rekordbox can keep its old analysis for a track that was overwritten in place; re-analyse the track (or remove and re-add it) afterwards.
- **Destination rule.** Only the last path component of `<destination>` is created. If its parent doesn't exist (an unmounted external drive, a typo), beatdown stops with an error instead of creating the whole path — it will never create a phantom `/Volumes/<Drive>` on your boot disk just because the real drive wasn't mounted.
- **Free-space check.** Before writing anything, beatdown estimates the total output size from the source files' headers and refuses to start unless free space on the destination volume is at least that estimate plus a 10% margin; `--dry-run` shows the same projection (and fails the same way if space is short). If the disk fills up mid-run anyway, the file that hit it fails and the batch stops launching new encodes — "Stopped early: destination disk is full" — rather than failing every remaining file one by one.
- **FLAC bit depth.** 16- and 24-bit sources are written bit-exact. 32-bit integer and 32-bit float sources are written as 24-bit FLAC, because libsndfile's FLAC writer tops out at 24 bits.
- **MP3 sample rate.** 44.1 and 48 kHz sources keep their rate; higher rates are resampled to 48 kHz and lower ones to 44.1 kHz — the MPEG-1 rates, the only ones at which 320 kbps exists.
- **MP3 bitrate/VBR range.** `--bitrate` accepts 128–320 kbps and `--vbr` accepts levels 0–3 and 5–8: lower settings aren't club quality, and beatdown can't verify their audio content reliably (below 128 kbps CBR, LAME may omit the tag its own gapless decoding depends on; VBR level 4 and 9 measured outside the level-check tolerance on real bright material — see the task report in `.superpowers/` for the numbers).
- **M4A sources.** `.m4a` files are read whether they hold ALAC (Apple Lossless) or AAC, and are recognised by content, like everything else (an M4A renamed to `.wav` is still read as M4A). ALAC converts losslessly: a FLAC made from it decodes bit-identical to the ALAC. AAC is lossy in its own right, so it can't give back more than it kept — an MP3 made from it carries AAC's losses as well as its own, and a FLAC made from it is a lossless copy of the *decoded AAC*, not of the original master (written as 24-bit, like any float source). Either way the conversion is gapless — the encoder's priming and padding are trimmed, so the output is as long as the original — and the tags (title, artist, album, year, track, genre, comment) carry over. Any other codec in an MP4 container (AC-3, MP3, …) is refused by name, and a file whose audio stops before its index says it should (an interrupted download) fails with "the audio ends early" rather than producing a shorter track. Damaged audio fails the file too: anything FFmpeg's decoder reports, a packet cut short, or a packet that decodes to a different length than the file gives it. AAC and ALAC carry no checksum in an M4A, though, so some damage (typically a single flipped bit) decodes without any sign of it, and no converter can catch that. Decoding is FFmpeg's, through its own AAC and ALAC decoders on every OS (never Apple's AudioToolbox on a Mac), so a given file converts identically everywhere.
- **MP3 sources are refused.** A source that's already MP3 data (including one saved with a `.wav` extension) fails with "source is MP3 data — not re-encoding lossy audio" — re-encoding an already-lossy file only compounds the loss.
- **Ctrl-C.** The first press stops launching new encodes and aborts the ones in flight — their temp files are removed, so nothing half-written is left behind — then prints the summary and exits 130. Press it again to force-quit immediately; that skips the cleanup and can leave a hidden `.beatdown-….part` temp file in the destination, which is safe to delete.
- **Exit codes.** `0` everything converted or skipped · `1` one or more files failed, or the free-space check failed (including a `--dry-run` whose projection shows insufficient space) · `2` bad arguments, or a destination whose parent folder doesn't exist · `130` interrupted by Ctrl-C.
- **`--dry-run` / `--quiet` / `--verbose`.** `--dry-run` lists the files that would be converted (with an estimated output size) and the files that would be skipped, without writing anything. `--quiet` prints only errors, failures and the final summary. `--verbose` prints the encoder settings used for each file plus the skipped files — beatdown links the encoder as a library rather than shelling out to one, so there's no "encoder command" to print, only the settings it used.
- **Failure recap.** However quiet the run, the end-of-run summary lists every failed file under `Failed:` with its reason, so a long batch doesn't need scrolling back through hundreds of per-file lines to find the handful that failed.
- **Same-name sources.** Two source files in one folder that differ only in extension or letter case — e.g. `Track.wav` and `Track.aiff` — would produce the same output, `Track.mp3`. The first in name order wins and the other is reported as skipped (`same output as Track.aiff`). Likewise, a file whose output would land on another source file (`--format flac` into the source folder turns `Track.wav` into `Track.flac`) is skipped, with or without `--overwrite`, rather than replacing that source — including when the two names only *look* the same, such as one spelled in a different Unicode normalization form or reached through a symlinked destination, on a filesystem (e.g. APFS) that treats them as one file; if beatdown can't even verify whether an existing output is a source file (a rare filesystem hiccup), it skips that file too (`cannot check whether the output is a source file`) rather than risk it.
- **Platform status.** Built and tested by hand on macOS (Apple Silicon), where the full test suite passes locally. CI workflows for macOS, Windows and Linux (`.github/workflows/ci.yml`) and for release binaries (`release.yml`) are in the repository but have not run yet, so the Windows and Linux builds are untested so far.

## Right-click recipes

There's no GUI yet (a real one is on the roadmap), but each OS has a cheap way to run beatdown from a file manager instead of a terminal.

**macOS** — Automator → File → New → Quick Action. Set "Workflow receives" to *files or folders* in *Finder*, add a "Run Shell Script" action with "Pass input" set to *as arguments*, and use:

```sh
for f in "$@"; do /usr/local/bin/beatdown "$f" "${f%/}-mp3"; done
```

Save it (e.g. as "Convert to MP3"), then right-click a folder or file in Finder → Quick Actions → Convert to MP3. Adjust the path if `beatdown` isn't installed at `/usr/local/bin`.

**Windows** — create `%APPDATA%\Microsoft\Windows\SendTo\beatdown.cmd` containing:

```bat
beatdown.exe %1 "%~1-mp3"
pause
```

(put `beatdown.exe` on `PATH`, or use its full path in the script). Right-click a folder → Send to → beatdown. The `pause` keeps the window open so the summary stays readable.

**Linux (Nautilus)** — drop an executable script into `~/.local/share/nautilus/scripts/`, e.g. `~/.local/share/nautilus/scripts/Convert to MP3`, with the same loop as the macOS recipe above (`beatdown` on `PATH`), and `chmod +x` it. Right-click a folder in Nautilus → Scripts → Convert to MP3.

## Verifying the encoder settings yourself

beatdown links libmp3lame directly, using the same settings as the `lame` command-line tool's `--cbr -b 320 -q 0 --add-id3v2`. To check that independently of beatdown, before trusting a whole library to it:

```
brew install lame
lame --cbr -b 320 -q 0 --add-id3v2 in.wav out.mp3
```

Import `out.mp3` into rekordbox and confirm it reads **320 kbps**, then compare it against beatdown's own output for the same source using `afinfo` (macOS):

```
afinfo out.mp3
afinfo <beatdown-output>.mp3
```

Bitrate, sample rate and channel layout should match.

## Club-ready?

A measurement pass on synthetic test material (sine sweeps, noise, and a few synthesised "loud master" style loops — not real songs) found the defaults transparent: the 320 kbps MP3 didn't change level, stayed flat to 20 kHz, and kept the stereo image; FLAC output was bit-exact. Real music will vary, but those numbers are a reasonable baseline for what LAME at these settings does. The full measurements are in [docs/club-readiness-measurements.md](docs/club-readiness-measurements.md).

Beyond that one-time measurement, every file beatdown converts now has its *audio content* checked against its source before it's left under its final name — not just the container metadata (bitrate, sample rate, duration) verification already checked, but the decoded samples themselves: the per-second level (below 16 kHz, so the check isn't thrown off by the MP3's own intentional cutoff above that), the total decoded length, and the channel count. That catches the kind of bug that matters most — a conversion that's silent, badly truncated, or has dropped a channel — but it isn't a guarantee against every possible kind of corruption.

- **Loud masters.** A master peaking near 0 dBFS decodes slightly over full scale — around +0.5 dB on a handful of samples for a typical loud master, which measured tens of dB below the music and is inaudible on a club system. beatdown flags anything louder itself: if a converted file decodes above +1.0 dBFS, the run's summary calls it out as a very loud master. A −1 dBTP export avoids the overs for typical masters (an unusually bright or hard-clipped one can still show a couple, since much of its energy sits above 20 kHz, where the MP3 removes it anyway).
- **One check only you can do.** MP3 decoders don't all agree on where the stream starts — LAME's own decoder and Apple's differ by a constant 23 ms, depending on whether the decoder reads LAME's gapless tag. Put a hot cue exactly on a kick in rekordbox, export to USB, and confirm the cue still lands on the transient when you play it on the CDJ.

## How the code is organized

To read or change the code, start with [ARCHITECTURE.md](ARCHITECTURE.md), a map of the repository and how a conversion flows through it. Every directory also has its own `README.md` describing its files in plain English.

## Licence

beatdown itself is MIT-licensed (see `LICENSE`). It statically links four LGPL libraries: libmp3lame (LGPL-2.0 — vcpkg's port metadata says LGPL-2.0-only), libsndfile (LGPL-2.1-or-later), mpg123 (LGPL-2.1-or-later — libsndfile's MP3-decoding backend, used to verify a converted MP3's audio content against its source) and FFmpeg's libavformat, libavcodec and libavutil (LGPL-2.1-or-later — built without FFmpeg's GPL and non-free parts; they decode M4A). The LGPL requires that relinking against a different build of those libraries stay possible: this repository, together with the pinned `external/vcpkg` submodule that fixes their exact versions and build flags, is the complete recipe for that — `git clone --recursive` and the two build commands above reproduce the exact libraries any given release was linked against.

Release binaries also contain libFLAC, libogg, libvorbis and opus (which libsndfile uses for FLAC and Ogg Vorbis/Opus) and CLI11 (compiled in from its headers), all under BSD-style licences whose copyright notices must ship with the binaries; vcpkg installs each one's licence text as `share/<port>/copyright` under `build/<preset>/vcpkg_installed/<triplet>/`.
