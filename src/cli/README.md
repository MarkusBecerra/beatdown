# `src/cli/`: the command-line front end

The `beatdown` executable's `main()`, and nothing else. It turns command-line arguments into an `Options` struct, hands that to the conversion engine in [`../core/`](../core/README.md), and exits with the engine's exit code. All real behaviour lives in `core`. This file only deals with arguments, the console and the process.

## `main.cpp`

**Order of events:**

1. **Console setup.** On Windows, the console is switched to UTF-8 so non-ASCII filenames print correctly. This does nothing on macOS and Linux.
2. **Argument parsing** with [CLI11](https://github.com/CLIUtils/CLI11). On Windows, `ensure_utf8` first re-reads the arguments as UTF-16 and converts them, since the normal `argv` would already have mangled non-ASCII paths. The options and their rules:
   - `source` and `destination` are required.
   - `--format` must be `mp3` or `flac`.
   - `--bitrate` must be one of 128, 160, 192, 224, 256 or 320.
   - `--vbr` must be one of 0–3 or 5–8, and can't be combined with `--bitrate`.
   - `--jobs` must be a positive number.
   - `--quiet` and `--verbose` exclude each other.
   - `--strip-suffix` may repeat, one value each time.
3. **Building `Options`.**
   - A path that starts with `~/` is expanded to the home directory (`HOME`, or `USERPROFILE` on Windows). The shell doesn't expand `~` inside quotes, and the README's own examples quote their paths.
   - Paths are built from UTF-8 with `path_from_utf8`, so they survive on Windows.
4. **Running.** It installs the Ctrl-C handler, creates a `ConsoleReporter` writing to standard output, and calls `beatdown::run`.

**Exit codes:**

| Exit code | Meaning |
|---|---|
| `0` | Success, or `--help` / `--version`. |
| `1` | One or more files failed, or there wasn't enough free space. This code also covers an internal error: any exception that escapes the engine is caught here, printed as `beatdown: internal error: …`, and turned into `1` instead of crashing. |
| `2` | A usage error. CLI11 prints the problem; `run` also returns `2` for a missing source or a destination whose parent folder doesn't exist. |
| `130` | Interrupted by Ctrl-C. |

**Why the bitrate and VBR choices are limited:** beatdown checks every MP3 it writes against its source (see `content_check` in `core`). Below 128 kbps, and at VBR levels 4 and 9, that check can't certify the result reliably. So the CLI only offers the settings it can stand behind; the comment in `main.cpp` has the details.

**Style note:** unlike everything in `core`, this file's code is at global scope, not inside `namespace beatdown`. So it declares its own few `using std::…` lines instead of relying on `core/std_names.hpp`.

**Tested by:** `tests/test_cli.cpp`, which runs the real built executable with various arguments and checks exit codes and output.

Up: [`src/`](../README.md)
