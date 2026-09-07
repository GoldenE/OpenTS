# Building OpenTS

> [!IMPORTANT]
> Visual Studio 2022 Win32 and x64 Debug and Release builds are supported. Build and automated-test verification establishes compilation and the covered contracts, not runtime behavior.

## Supported target

| Component | Requirement |
| --- | --- |
| Host and architecture | Windows, 32-bit (`Win32`) or 64-bit (`x64`) target |
| Processor | SSE2, so a Pentium 4 or Athlon 64 onward |
| Generator and compiler | Visual Studio 2022 MSVC 19.30 or newer |
| Windows SDK | A Visual Studio-installed Windows SDK |
| CMake | 3.23 or newer |
| C++ language level | C++20 |
| Configurations | Debug and Release |

Other generators, compilers, architectures, and configurations are not
supported by the current tree.

Install Visual Studio 2022 with the **Desktop development with C++** workload,
a Windows SDK, CMake 3.23 or newer, and Git for Windows.

The tree includes ATL (`atlbase.h`) and the MFC resource header (`afxres.h`),
so the **C++ ATL** and **C++ MFC** components for the latest build tools must
be selected as well. The Build Tools edition does not add them with the
workload; from the command line they are
`Microsoft.VisualStudio.Component.VC.ATL` and
`Microsoft.VisualStudio.Component.VC.ATLMFC`.

## Dependencies

The renderer is built on [bgfx](https://github.com/bkaradzic/bgfx), vendored as the
`thirdparty/bgfx.cmake` submodule and pinned to a tested tag. It carries bgfx, bx, and
bimg as submodules of its own, so the checkout must be recursive:

```powershell
git submodule update --init --recursive
```

A fresh clone can do the same in one step with `git clone --recurse-submodules`.
Configuration fails with instructions if the submodule is missing. Updating the
dependency means moving the submodule to a new tag in its own change.

The engine uses portable C++ implementations for its codecs, blitters, voxel drawers, interpolation, and CPU detection. MASM is no longer required. Synthetic assembly-reference corpora are stored losslessly compressed; CMake unpacks and verifies them without a separate compression tool or Python build dependency.

## Configure and build

Run these commands from the repository root in PowerShell:

```powershell
cmake -S . -B build/Win32 -G "Visual Studio 17 2022" -A Win32
cmake --build build/Win32 --config Debug
cmake --build build/Win32 --config Release
cmake -S . -B build/x64 -G "Visual Studio 17 2022" -A x64
cmake --build build/x64 --config Debug
cmake --build build/x64 --config Release
```

CMake normally discovers Visual Studio through the Visual Studio Installer. If
the installation is not registered, provide its installation directory and
product version through `CMAKE_GENERATOR_INSTANCE`.

Use separate build directories for each architecture. `OPENTS_POINTER_DIAGNOSTICS` defaults to `ON`, treating MSVC C4302, C4311, and C4312 pointer-truncation diagnostics as errors. Win32 uses `/arch:SSE2` to avoid x87 excess precision; x64 has SSE2 as its baseline. Both use `/fp:precise`.

The generated solution exposes only Debug and Release. Successful builds write the engine executable under `build/<architecture>/bin/<configuration>/` under its runtime name and copy the runtime files into `TS_RUN_DIR`, which defaults to `Run/`:

| Configuration | Runtime files |
| --- | --- |
| Debug | `GameD.exe`, `GameD.pdb`, `GameD.map`, `Language.dll` |
| Release | `Game.exe`, `Game.pdb`, `Game.map`, `Language.dll` |

`Language.dll` has the same name in both configurations, so the most recently
built configuration replaces the previous copy in `Run/`. Compiler and linker
intermediates remain under the selected build directory.

Set `-DTS_RUN_DIR=<directory>` at configuration when keeping separate runtime installations for each architecture. The executable, language library, saves, recordings, and network peers must match the architecture; the enforced compatibility stamp is described below.

## Build identity

The project version is declared once, by `project(OpenTS VERSION ...)` in the
top-level `CMakeLists.txt`, with any SemVer prerelease label alongside it in
`OPENTS_VERSION_PRERELEASE`, because `project()` accepts numbers only. Both must
match the development entry of the manual's release registry, which
`python manual/tools/manage.py check` enforces.

Each build writes two generated headers from that version and the repository
state:

| Header | Contents |
| --- | --- |
| `opents_version.h` | The version components, the version string, a prerelease flag, and the packed version number |
| `opents_build.h` | The commit, branch, commit date, whether tracked files were modified, and the version as it is displayed |

The packed release version uses one byte each for major, minor, and patch. `code/architecture.hh` adds `0x01000000` for x64 to produce the save and network state version; Win32 retains the existing packed value. Save loading, LAN negotiation, and the online lobby build number reject an architecture mismatch. Development snapshots within one release cycle and architecture share a stamp without promising interoperability. A prerelease is not distinguished from the release it leads up to.

Recording headers also distinguish architecture and build configuration because Debug carries an additional field. Win32 Debug retains `OTSREC1`, x64 Debug uses `OTSREC2`, and Release uses `OTSREL1` or `OTSREL2`. Each tag occupies eight bytes including its terminating null. The loader rejects a mismatched or truncated tag before reading session fields. Recreate older Release recordings with the selected build; ambiguous `OTSREC` headers are no longer accepted by Release.

Everything that names a version to the player reads these headers: the version
resources of `Game.exe` and `Language.dll`, the title screen, the version
dialog, the crash report, and the debug log's opening banner. A build reports
its version with the commit it came from, as in `0.1.0 (ab12cd3)`, and adds a
modification marker when tracked files differ from that commit. The commit is a
diagnostic build identity, not an enforced save or network compatibility stamp.
Configuring with
`-DOPENTS_OFFICIAL_BUILD=ON` reports the version alone, for a build published
under the version it declares.

The version stamp is rewritten only when the version changes, so an ordinary
commit does not recompile the code that reads it. The build stamp refreshes on
every build, so committing is reflected without reconfiguring, and an unchanged
stamp is not rewritten.

A detached checkout, which is what building a tag or a pull request produces, has
no branch of its own. The stamp then reports a ref that points at the commit,
preferring a tag, so a continuous integration build of a pull request reports
that pull request rather than the bare word `HEAD`.

Git is not required. A build with no Git available, or from a source archive
with no repository, succeeds and reports the commit as `unknown` and the version
without one.

## Asset-independent clock checks

The `RenderClockTest` target exercises render-clock startup, fixed-point progression, cadence changes, disabled interpolation, rejected stalls, millisecond-counter wraparound, and three-axis render-offset arithmetic without launching the engine or loading game assets. It also checks relocation thresholds, large-coordinate arithmetic, and the options layout used by the recording format. Run its `renderclock` CTest entry in either supported configuration:

```powershell
cmake --build build/Win32 --config Debug --target RenderClockTest
ctest --test-dir build/Win32 -C Debug -R '^renderclock$' --output-on-failure
```

Use `Release` in both commands for the optimized configuration. These checks establish clock arithmetic and layout, not visual behavior or replay compatibility; those still need the [runtime checks](TESTING.md).

## Asset-independent map checks

`MapContractsTest` compiles production map and preview function bodies with small cell, decoded-stream, surface, and redraw substitutes. CMake extracts these bodies at configuration and reconfigures when their source files change. Tests compare shroud visitation order and flags against a full-grid oracle, walk the diamond iterator through its final cell at the table ceiling, check initialization of retained cells and short tables, and verify the legacy reader's 65,536-byte decoded payload and 128-column coordinates. Preview checks reject missing packs and nonpositive dimensions and exercise the zero-size drawing guard. The target also rejects integer construction of `Cell` and verifies its four-byte layout.

```powershell
cmake --build build/Win32 --config Debug --target MapContractsTest
ctest --test-dir build/Win32 -C Debug -R '^mapcontracts$' --output-on-failure
```

Use `Release` for optimized checks and `build/x64` for the native 64-bit target. The stream substitute supplies already decoded bytes, and the redraw substitutes record calls; these tests do not establish codec behavior, full-engine shroud effects, save compatibility, editor acceptance, or runtime performance.

## Architecture and portable-routine checks

The [assembly parity harness](../tests/asmparity/README.md) compares portable codec and rendering output against complete synthetic input/output vectors captured from Win32 assembly before retirement. It covers 2,918 codec vectors and 572 rendering vectors, and validates its own negative control and capture/replay transport. CMake checks the uncompressed corpus SHA256 before tests run.

`ArchitectureTest` exercises the save/recording guard placement and network version negotiation, including the WOL build identity. [VQAPointersTest](../tests/vqapointers/README.md) checks the memory/event callback return width, high pointer bits, cached file-offset handling, and native audio-initialization parameter size. `VQADrawersTest` checks the existing C++ movie drawers across multiple rows and geometries; `VQAAudioTraceTest` checks both enabled and disabled tracing. `RingBuffersTest` exercises the production alpha/depth-buffer addressing, signed pans, wrap boundaries, and copy behavior with synthetic surfaces. `RenderContractsTest` covers the TMP disk-offset/native-pointer boundary and surface pixel addressing. `LCWStreamsTest` checks production pipe/straw framing and allocation bounds, including literal-only expansion, fragmented input, and maximum encoded/decoded blocks. `LZOStreamsTest` checks native dictionary sizing and real stream/pipe/straw buffers with guarded allocations, incompressible blocks, fragmented transfers, and retained partial-block framing. All run without proprietary assets.

After building all targets, run the full suite for both configurations and architectures:

```powershell
ctest --test-dir build/Win32 -C Debug --output-on-failure
ctest --test-dir build/Win32 -C Release --output-on-failure
ctest --test-dir build/x64 -C Debug --output-on-failure
ctest --test-dir build/x64 -C Release --output-on-failure
```

## HD rendering checks

The HD feature adds asset-independent targets for the versioned pack codec and cache (`HDAssetsTest`), retained-source registry (`HDRuntimeTest`), color/depth compositor (`HDShapeTest`), physical surfaces and reusable raster storage (`RenderCoreTest`), production timer/owner continuity (`RenderStageTest`), dense world rasterization (`HDWorldTest`), and assembled mixed-scene image comparisons (`HDSceneTest`). The [rendering compatibility guide](RENDERING-COMPATIBILITY.md) links their coverage and limitations.

When Python 3.10 or newer is available, CMake also registers `hdpack` for the standard-library authoring tools and their production native validator. Python remains optional for the engine build. All these tests use synthetic data and run in Win32/x64 Debug/Release; they do not establish live GPU-backend behavior.

```powershell
cmake --build build/x64 --config Debug --target HDAssetsTest HDRuntimeTest HDShapeTest RenderCoreTest RenderStageTest HDWorldTest HDSceneTest
ctest --test-dir build/x64 -C Debug -R '^(hdassets|hdruntime|hdshape|hdpack|rendercore|renderstage|hdworld|hdscene)$' --output-on-failure
```

## Continuous integration

The `Engine` workflow builds every pull request and every push to `main` that touches the engine, its build files, or the workflows themselves. The `Engine nightly` workflow builds on a daily schedule, and skips the build when nothing has been committed since the last one. Both call the same reusable `Engine build` workflow, which on a Windows runner with Visual Studio 2022 configures and builds Win32 and x64 Debug and Release with the commands above, runs the CTest suite, and uploads each configuration's executable, language library, and symbol file as an artifact named for the architecture, configuration, and short commit. The linker map is not uploaded, because the symbol file covers the same ground. After a successful pull-request build, the `Engine build comment` workflow keeps one comment on the pull request with direct nightly.link downloads of that build's artifacts.

The `Engine release` workflow runs when a GitHub release is published. It builds the release's commit with `-DOPENTS_OFFICIAL_BUILD=ON`, packages `Game.exe`, `Language.dll`, and `Game.pdb` into a separate zip for each architecture named `OpenTS-<tag>-<architecture>.zip`, attaches both archives to the release, and appends release notes generated from the manual's change records by `python manual/tools/manage.py release-notes`. [Maintaining](../manual/MAINTAINING.md) owns the release procedure around it.

Continuous integration builds redirect `TS_RUN_DIR` to an empty directory, so an
uploaded artifact holds only the files that build produced.

Continuous integration establishes the same thing a local build does, on the
runner's toolchain. It does not establish runtime behavior.

## Verification boundary

Win32 and x64 Debug and Release were verified on September 7, 2026 with CMake 4.4.3, Visual Studio 2022 Build Tools, MSVC 19.44.35228, and Windows SDK 10.0.26100. Full builds and all 29 asset-independent CTest entries passed in each configuration, including the HD checks above and the opt-in loopback LAN configuration and transport checks described in [Runtime testing](TESTING.md). The final gate ran the logger check sequentially while idle and the other 28 checks separately, covering every entry in all four configurations. Earlier logger-latency failures under concurrent builds remain recorded in local evidence; no threshold was relaxed. Builds retain inherited MSVC warnings, while the three pointer-truncation diagnostics listed above are errors. Contributions should not add new warnings.

Build verification establishes that the supported toolchain compiles and links
the configured targets and produces the listed artifacts. Runtime behavior is
established separately, by the replay harness and play testing that
[Runtime testing](TESTING.md) describes, and is outside this build-support
record.

The repository contains no maps, movies, audio, or other original game assets.
Keep legally obtained runtime data local and outside version control. Do not
commit populated run directories, original executables, proprietary SDKs, IDE
state, compiler output, generated CMake projects, or credentials.
