# beatdown

![CI](https://github.com/MarkusBecerra/beatdown/actions/workflows/ci.yml/badge.svg)

beatdown is a command-line tool that batch-converts a folder of WAV, AIFF or FLAC files into 320 kbps CBR MP3s — the format every CDJ and rekordbox standardise on, and the default here — or lossless FLAC (`--format flac`) for players that support it. It runs entirely on your machine, with no network access at all, and ships as a single self-contained binary for macOS, Windows and Linux, so a fresh machine needs nothing installed first.

## Install

Download the binary for your platform from [Releases](https://github.com/MarkusBecerra/beatdown/releases): macOS (universal, arm64 + x86_64), Windows x64, or Linux x86_64.

Or build from source — one recipe on all three OSes:

```
git clone --recursive https://github.com/MarkusBecerra/beatdown.git
cd beatdown
./external/vcpkg/bootstrap-vcpkg.sh -disableMetrics   # Windows: .\external\vcpkg\bootstrap-vcpkg.bat -disableMetrics
cmake --preset default
cmake --build --preset default
```

`--recursive` matters: vcpkg is pinned as a submodule under `external/vcpkg`, and bootstrapping builds vcpkg itself before it can fetch the rest of the dependencies. The binary lands at `build/default/beatdown` (`build\default\beatdown.exe` on Windows).

Only macOS (Apple Silicon) has been built and run by hand so far — see *Platform status* below.

## Usage

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
- **Ctrl-C.** The first press stops launching new encodes, lets in-flight ones finish, cleans up temp files, prints the summary, and exits 130. Press it again to force-quit immediately.
- **Exit codes.** `0` everything converted or skipped · `1` one or more files failed, or the free-space check failed (including a `--dry-run` whose projection shows insufficient space) · `2` bad arguments, or a destination whose parent folder doesn't exist · `130` interrupted by Ctrl-C.
- **`--dry-run` / `--quiet` / `--verbose`.** `--dry-run` lists the files that would be converted (with an estimated output size) and the files that would be skipped, without writing anything. `--quiet` prints only errors, failures and the final summary. `--verbose` prints the encoder settings used for each file plus the skipped files — beatdown links the encoder as a library rather than shelling out to one, so there's no "encoder command" to print, only the settings it used.
- **Failure recap.** However quiet the run, the end-of-run summary lists every failed file under `Failed:` with its reason, so a long batch doesn't need scrolling back through hundreds of per-file lines to find the handful that failed.
- **Same-name collisions.** Two source files that share a stem but differ in extension in the same folder — e.g. `Track.wav` and `Track.aiff` — produce the same output, `Track.mp3`. Keep only one per folder; the scanner doesn't currently deduplicate this case.
- **Platform status.** Built and tested by hand on macOS (Apple Silicon), where the full test suite passes locally. Windows and Linux are built and tested by the CI matrix on every push (see the badge above) but have not yet been run on physical hardware.

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
```

(put `beatdown.exe` on `PATH`, or use its full path in the script). Right-click a folder → Send to → beatdown.

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

## Licence

beatdown itself is MIT-licensed (see `LICENSE`). It statically links two LGPL-2.1 libraries, libsndfile and libmp3lame. The LGPL requires that relinking against a different build of those libraries stay possible: this repository, together with the pinned `external/vcpkg` submodule that fixes their exact versions and build flags, is the complete recipe for that — `git clone --recursive` and the two build commands above reproduce the exact libraries any given release was linked against.
