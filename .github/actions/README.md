# `.github/actions/`: in-repo GitHub Actions

Reusable workflow steps that live in this repository rather than on the GitHub Marketplace. A workflow uses one with `uses: ./.github/actions/<name>`, after checking out the repository.

There is one so far:

- [`msvc-env/`](msvc-env/README.md): puts Visual Studio's x64 C++ toolchain on the environment for the rest of a Windows job. It replaces a marketplace action that still ran on GitHub's deprecated Node 20 runtime.

The workflows that call it are described in [`../workflows/`](../workflows/README.md).
