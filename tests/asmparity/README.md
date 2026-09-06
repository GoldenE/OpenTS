# Assembly equivalence checks

These asset-independent tests compare portable routines with persisted output from the original Win32 assembly on deterministic synthetic inputs. Each comparison reports the vector name, seed, first differing byte, actual and expected values, and both sizes. A negative control deliberately changes a byte and verifies the exact diagnostic. The harness also tests its vector encoding and seeded generator and exercises capture followed by replay of a fixed harness fixture through CTest.

The reference objects were shared through `OpenTSAsm` and demand-linked through `OpenTSAsmReference` during migration. After all codec and rendering comparisons passed in Win32 Debug and Release, the assembly and MASM build requirements were retired. Both architectures now consume the same committed vectors. The suite wrappers retain distinct portable names to keep the original comparison seam explicit.

Configure from the repository root as described in [Building OpenTS](../../docs/BUILDING.md), then build a suite and run its tests:

```powershell
cmake --build build --config Debug --target AsmParityHarness AsmParityCodecs AsmParityRender
ctest --test-dir build -C Debug -R '^asmparity-' --output-on-failure
```

Use `Release` in both commands for the optimized configuration. The codec and render suites own their input generation and routine-specific state serialization. These checks establish byte equivalence for the covered inputs; they do not establish game playback or rendering behavior.

## Persisted vectors

CTest passes `--golden <file>` to each suite. CMake unpacks the committed `.tar.gz` corpus into the build directory using its built-in archive support, then verifies its full uncompressed SHA256 against the accompanying `.sha256` file. No compression library or Python interpreter is required to configure, build, or run the tests. A missing, duplicate, changed-input, or unvisited record fails the test, so coverage cannot disappear silently. Golden files originated from synthetic inputs and live Win32 assembly, never from game assets or an unverified portable implementation. Each record stores the vector name, decimal seed, hexadecimal input and output; `-` represents an empty byte sequence. The header versions the format as `OPENTS_ASMPARITY_1`. State is serialized without pointers and with fixed-width values so vectors remain architecture-independent.

`pack_vectors.py <capture.txt> <suite.tar.gz>` uses the Python standard library to package a newly captured corpus deterministically. It verifies exact restored bytes and SHA256 before publishing the archive and checksum. Compression changes storage only; all original inputs and outputs remain present.

The runner retains `--capture <file>` for future reference-backed migrations and rejects it for the current codec and render executables, whose references have been retired. It writes a capture only after every comparison passes. The harness fixture alone enables capture from known fixed bytes to exercise the file round trip; that fixture is not an assembly result.

## Adding a suite

Call `opents_add_asmparity(Name SOURCES ... LIBRARIES ...)` from a suite CMake file, include `parity.h`, and implement `void Run_Suite(asmparity::Context & context)`. Invoke `context.Check(name, seed, input, portable_output, assembly_output)` for every case. Guard reference calls with `#if OPENTS_ASM_REFERENCE`; after retirement omit the reference output. `Random(seed).Next()` and `Fill(span)` use the same fixed-width generator on both architectures. Use distinct names and seeds for each case, and include relevant parameters and initial state in the serialized input and mutated state in the output.

`OPENTS_POINTER_DIAGNOSTICS=ON` is the default and rejects MSVC pointer-truncation diagnostics throughout the build. `OPENTS_BUILD_ASM_PARITY=OFF` disables this test family for a targeted development build; complete validation requires it enabled.
