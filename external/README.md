# `external/`: the pinned dependency manager

This folder holds one thing: [vcpkg](https://github.com/microsoft/vcpkg), Microsoft's C/C++ package manager, as a **git submodule** at `external/vcpkg`. Nothing in `external/vcpkg` is beatdown's code. Don't edit it; treat it as read-only.

## Why a submodule

beatdown ships as a single executable with every library linked in statically. That needs the same library versions, built the same way, on macOS, Windows and Linux. Pinning vcpkg to one exact commit gives that:

- **Reproducible builds.** vcpkg's recipes ("ports") at that commit decide every library's version and build flags. The submodule commit matches the `builtin-baseline` in [`../vcpkg.json`](../vcpkg.json), so the two always agree.
- **Licence compliance.** Several linked libraries are LGPL, which requires that users can relink against their own build of them. The repository plus this pinned submodule is the complete recipe for rebuilding those exact libraries. The README's Licence section explains this.
- **One build recipe everywhere.** Contributors and CI use the same steps.

## How it's used

1. Clone with `git clone --recursive`, or run `git submodule update --init` afterwards, so this folder isn't empty.
2. Run `external/vcpkg/bootstrap-vcpkg.sh` (or `.bat` on Windows) once. This builds the `vcpkg` tool itself.
3. `cmake --preset default` does the rest.
   - The presets in [`../CMakePresets.json`](../CMakePresets.json) point CMake's toolchain at `external/vcpkg/scripts/buildsystems/vcpkg.cmake`.
   - That runs vcpkg in *manifest mode*: it reads [`../vcpkg.json`](../vcpkg.json) and builds each listed library, plus the libraries those depend on, into `build/<preset>/vcpkg_installed/`.

The libraries it builds:

| Port | Why beatdown needs it |
|---|---|
| `libsndfile[external-libs, mpeg]` | Reads WAV/AIFF/FLAC; writes FLAC; decodes MP3 for verification and for the "refuse MP3 sources" check. Brings in libFLAC, libogg, libvorbis, opus and mpg123. |
| `mp3lame` | The MP3 encoder (LAME). |
| `ffmpeg[avcodec, avformat]` | Decodes M4A: the MP4 container, AAC and ALAC. Built without FFmpeg's GPL and non-free parts. |
| `cli11` | Command-line parsing. Header-only, compiled into the executable. |
| `catch2` | The test framework (tests only). |

macOS builds also use the custom *triplets* in [`../triplets/`](../triplets/README.md), which set the minimum macOS version.

## Changing versions

To move to newer library versions, update the submodule's commit **and** `builtin-baseline` in `vcpkg.json` together, to the same vcpkg commit. The first configure afterwards rebuilds everything from source; FFmpeg is the slow one.

Up: [repository overview](../ARCHITECTURE.md)
