---
title: Build and run
summary: Builds the Win32 or x64 Debug or Release executable and copies it into the local Run directory.
category: getting-started
source_files:
  - docs/BUILDING.md
  - CMakeLists.txt
  - code/CMakeLists.txt
related:
  - type: using
    id: game-data
  - type: using
    id: developer-build-troubleshooting
---

Install Visual Studio 2022 with the **Desktop development with C++** workload, a Windows SDK, CMake 3.23 or newer, and Git for Windows. The repository's `docs/BUILDING.md` covers toolchain details and options.

The renderer is a vendored dependency, so a clone that did not fetch submodules has to fetch them before configuring. Configuration stops with instructions if they are missing.

```powershell title="PowerShell"
git submodule update --init --recursive
cmake -S . -B build/Win32 -G "Visual Studio 17 2022" -A Win32
cmake --build build/Win32 --config Debug
cmake -S . -B build/x64 -G "Visual Studio 17 2022" -A x64
cmake --build build/x64 --config Debug
```

The Debug build copies `GameD.exe`, its symbols, map file, and the matching `Language.dll` into `Run/`. Use `--config Release` to produce `Game.exe` instead. Keep separate CMake build directories for the two architectures; each build writes the same runtime filenames, so the last architecture built supplies `Run/`.

After supplying the required game data in `Run/`, launch the selected executable from that directory:

```powershell title="PowerShell"
.\Run\GameD.exe
```

Building another configuration or architecture replaces `Run/Language.dll` with that build's copy. Keep the executable, symbols, and language library from the same build together. Every string and dialog the engine displays is read from that library, so a `Language.dll` supplied by a localized or edited installation is overwritten by the build and none of its text reaches the screen.
