# `triplets/`: macOS build targets for vcpkg

A vcpkg *triplet* is a small CMake file that tells vcpkg how to build every library for one target: which CPU architecture, which OS, and whether libraries (and the C runtime) are linked statically or dynamically.

This folder overrides the two macOS triplets. CMake hands it to vcpkg as an "overlay" (`VCPKG_OVERLAY_TRIPLETS` in [`../CMakePresets.json`](../CMakePresets.json)), so these files are used *instead of* vcpkg's own copies of the same names.

## The files

- **`arm64-osx.cmake`**: Apple Silicon.
- **`x64-osx.cmake`**: Intel Macs.

Each is vcpkg's upstream triplet (`arm64-osx` is built in; `x64-osx` is a community triplet) plus one line: `VCPKG_OSX_DEPLOYMENT_TARGET "11.0"`. Without it, the libraries would be built for whatever macOS version the build machine runs. The finished `beatdown` would then refuse to start on older Macs. With it, the release binary runs on macOS 11 (Big Sur) and later.

Both build every library **statically**, so its code ends up inside the `beatdown` executable. The C runtime stays **dynamic**, because every Mac already has it.

## How they're used

- The `macos-arm64` and `macos-x64` presets in `CMakePresets.json` each select one of these triplets. The release workflow builds both and merges them with `lipo` into a single *universal* executable that runs natively on either kind of Mac.
- The everyday `default` preset on a Mac picks the triplet for the machine's own architecture; on Apple Silicon that's this folder's `arm64-osx`.
- **Other OSes don't use this folder:**
  - Linux uses vcpkg's standard `x64-linux` triplet, which already links libraries statically.
  - Windows uses `x64-windows-static`, which also links the C runtime statically. The top-level `CMakeLists.txt` selects it before `project()`.

Up: [repository overview](../ARCHITECTURE.md)
