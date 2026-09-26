# `.github/actions/msvc-env/`: MSVC setup for Windows CI

A small, in-repo GitHub Action that makes Microsoft's C++ compiler usable from the steps that follow it in a Windows job. The CI and release workflows run it right before building on `windows-latest`.

## Why it exists

On Windows, the compiler (`cl`), linker (`link`) and their `INCLUDE`/`LIB` paths only work after Visual Studio's `vcvars64.bat` has set up the environment, and that script only affects the shell it runs in. GitHub Actions runs each step in a fresh shell, so the environment has to be exported explicitly for later steps.

This used to be done with the marketplace action `ilammy/msvc-dev-cmd`, which still runs on GitHub's deprecated Node 20 runtime. This action replaces it without needing Node at all.

## How it works (`action.yml`)

It's a *composite* action: a list of shell steps, no JavaScript. Its single PowerShell step:

1. Uses `vswhere.exe` (installed with Visual Studio) to find the newest installation that has the x64 C++ toolset. It stops with an error if there is none.
2. Records every environment variable as it is *before*.
3. Runs `vcvars64.bat` inside `cmd.exe`, followed by `set`, and captures the resulting environment. It stops with an error if the script fails.
4. For every variable that is new or changed, appends `NAME=value` to the file named by `$GITHUB_ENV`. GitHub then applies those variables to all later steps of the job.

Only x64 is set up; beatdown builds nothing else on Windows.

## Used by

- [`../../workflows/ci.yml`](../../workflows/README.md): the Windows leg of the CI matrix.
- [`../../workflows/release.yml`](../../workflows/README.md): the Windows release build.

Both reference it as `uses: ./.github/actions/msvc-env`, which requires the repository to be checked out first.

Up: [`.github/actions/`](../README.md)
