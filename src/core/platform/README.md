# `src/core/platform/`: the operating-system shim

Everything in beatdown that has to differ between Windows and POSIX (macOS and Linux) sits behind one small header. The rest of the code calls these functions and never tests which OS it's on.

CMake compiles exactly one implementation file: `win.cpp` on Windows and `posix.cpp` everywhere else. So `win.cpp` is only ever compiled by the Windows CI job.

## `platform.hpp`: the interface

Four functions and one struct, all in `namespace beatdown::platform`:

| Function | What it's for |
|---|---|
| `install_interrupt_handler(flag)` | Makes Ctrl-C set an atomic "cancel" flag instead of killing the process, so the batch can stop cleanly. A *second* Ctrl-C force-quits. |
| `console_utf8()` | Makes the console print UTF-8. |
| `sf_open_path(path, mode, info)` | Opens a file with libsndfile given a `std::filesystem::path`, whatever characters the name contains. |
| `file_id(path)` | Returns a file's identity (a `FileId`), or nothing if it can't be read. |

**`FileId`** holds two 64-bit numbers that identify a file on disk:
- On POSIX, the device number and inode (`st_dev`/`st_ino`).
- On Windows, the volume serial number and the 64-bit file index.

Two paths that name the same file (through a symlink, `dir/./x`, or a Unicode spelling that macOS's filesystem treats as equal) produce equal `FileId`s. Unlike a file's size or contents, its identity doesn't change when the file is written to.

The scanner builds a hash set of every source file's identity. It uses it to guarantee an output is never written over a source file, however the two paths are spelled. That's why the header also specializes `std::hash<FileId>`, so `FileId` can go into an `unordered_set`.

## `posix.cpp`: macOS and Linux

- **Ctrl-C** installs a `sigaction` handler for `SIGINT` and `SIGTERM`. On the first signal it stores `true` in the flag and resets the signal to its default action, so the next Ctrl-C terminates the process as usual. A `static_assert` checks that `atomic<bool>` is lock-free, which is what makes storing to it from a signal handler safe.
- **Console:** nothing to do, since terminals are already UTF-8.
- **Opening files:** plain `sf_open`, because POSIX paths are already bytes.
- **Identity:** `stat()`.

## `win.cpp`: Windows

- **Ctrl-C** uses `SetConsoleCtrlHandler`. The first Ctrl-C, Ctrl-Break or console-close event sets the flag and reports "handled". Later ones fall through to the default action, which terminates the process.
- **Console:** sets both input and output code pages to UTF-8.
- **Opening files:** `sf_wchar_open`, libsndfile's wide-character entry point. A narrow open would go through the ANSI code page and mangle non-ASCII names.
- **Identity:**
  - `CreateFileW` with no read or write access and full sharing, so it never conflicts with other processes holding the file open, and with backup semantics so directories work too.
  - Then `GetFileInformationByHandle`.

## Things to know

- A *console-close* event (the window's X button) is treated like Ctrl-C. Windows may still end the process before cleanup finishes, which could leave a temp file behind. The console code page is also not restored on exit. Both are recorded as deferred in the rulings log (`docs/superpowers/plans/`).
- Tested by `tests/test_platform.cpp`: same file via two spellings gives the same `FileId`, two different files give different ones, and a missing file gives none.

Up: [`src/core/`](../README.md)
