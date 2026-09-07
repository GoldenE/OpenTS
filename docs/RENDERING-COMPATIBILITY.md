# Portable rendering compatibility

The native renderer retains the established pixel formats and integer arithmetic across Win32 and x64. `renderportable.cpp` owns palette adjustment, spotlight brightening, and indexed interpolation; `voxlib.cpp` owns voxel traversal. The legacy C entry-point names in `winasm.cpp` are wrappers around C++ implementations and do not require assembly instructions or MASM.

Classic mode remains the reference for the legacy raster behavior below. Opt-in [HD rendering](../manual/content/systems/hd-rendering.md) intentionally increases physical sampling while retaining logical geometry, numerical depth, and simulation state. [HD sidecars](../manual/content/formats/hd-pack.md) own the versioned asset contract and fallback rules.

The [retained source adapters](../tests/hdassets/README.md#retained-source-adapters) define source tokens, parsed-owner aliases, raw-span eligibility, and cleanup/invalidation. New consumers use those value tokens instead of inventing a universal archive lookup or treating decoded objects as raw files. [Render-stage sampling](../tests/renderstage/README.md) owns the directed timer and cache-refresh checks; [core composition checks](../tests/rendercore/README.md) and [assembled scenes](../tests/hdworld/README.md) describe synthetic rendering coverage. Live timing uses the separate [runtime measurement workflow](TESTING.md#hd-rendering-measurements).

## Measured assembly parity

`tests/asmparity/vectors/render.tar.gz` contains 572 synthetic input/output vectors captured from the original Win32 assembly. CMake extracts the losslessly compressed `render.txt` into the build directory and verifies its hash. The portable implementations passed live assembly comparisons and persisted-vector comparisons in Win32 Debug and Release during retirement. These checks establish byte output and tested state transitions, not full-engine rendering or performance. [Runtime testing](TESTING.md) owns the runtime verification boundary.

The corpus covers all four formats (565, 555, 556, 655), all three palette arithmetic paths, transparent palette entry zero, component saturation, intensity masks, three interpolation modes with row padding, six voxel drawers with forward/reverse traversal, empty columns, packed coordinate carry and wraparound, lighting lookup, and mutated matrix and traversal index state. Spotlight vectors enumerate all 65,536 input pixel values for multipliers 0, 1, 63, 127, 128, 254, and 255.

The lookup spotlight algorithm and the scalar fallback are distinct contracts. Across the 458,752 tested pixels per format, the original MMX and scalar outputs differ at zero pixels for 565 and 555, 281,920 pixels for 556, and 315,392 pixels for 655. The portable lookup path preserves the original MMX output in every format, including the 655 green mask literal `0x423a0a60`. Replacing that path with the scalar fallback would change rendering; the lookup table and dispatch remain in place.

Palette adjustment likewise retains the MMX path's signed 16-bit multiplier saturation after a four-bit shift. Replacing it with a conventional full-precision multiply is not equivalent. CPU feature globals continue to select the established arithmetic, although the implementations themselves use portable integer operations.

## Voxel traversal

The non-depth-buffered drawers add the two 16-bit projection components as one unsigned 32-bit word. Carry from the horizontal component into the vertical component is observable and retained. Drawers with normals write two adjacent pixels, while drawers without normals write one. Each visited column updates `TransformMatrix[0]`; completion also updates `StartIndex`. Existing depth-buffered C++ drawers retain their separate implementation.

The formerly unused C++ alternatives did not reproduce all these details. Their replacement and the dispatch switch were validated together against the assembly corpus. Synthetic tests provide valid encoded spans and initialized lookup tables; malformed voxel stream validation is outside this low-level interface.

## Interpolation and transparent spans

Indexed horizontal interpolation writes each source pixel followed by a palette lookup against its neighbor and terminates the row with index zero. The vertical line interpolator retains its unusual padded-input contract: it consumes `lines + 1` source rows and writes that final extra row after the interpolated pairs. Callers must provide the extra row. The doubled-stride argument remains twice the physical destination row stride for both vertical modes.

The 16-bit transparent remap specialization processes `length - 1` pixels for positive lengths, preserving the original assembly's omitted final pixel. Empty and one-pixel spans are no-ops. The other transparent specializations use their equivalent primary templates. `RenderContractsTest` distinguishes the retained remap behavior from a full-length copy.

## TMP representation

TMP tile sets retain 32-bit offsets from the beginning of the file allocation; loaders never overwrite those offsets with native pointers. `IsoTileSet::Fetch_Record_Pointer` and `Fetch_Record_Pointer_Unsafe` derive a native pointer when requested, with zero representing an absent record. The packed set prefix including its first offset occupies 20 bytes, and `IsoTileRecord` occupies 52 bytes, on both architectures. Shared MIX-backed artwork and separately loaded artwork use the same representation and preserve their existing ownership.

`RenderContractsTest` extracts the production layout and accessors to check fixed widths, zero offsets, modulo indexing, mutable record access, and unchanged serialized header bytes using synthetic data.

## Alpha and depth ring buffers

`ABuffer` and `ZBuffer` carry their start address, end address, fill addresses, and wrapped addresses as `std::uintptr_t`. Pixel counts and the normalized byte offset within the allocation retain their existing integer representation. Bounded negative pans subtract from the native base address through unsigned modular addition before the underflow and overflow wrappers normalize the result.

`RingBuffersTest` compiles the production classes and methods with synthetic surface storage. It checks constructor fills, exact-end and preceding-address wrap, positive and negative horizontal and vertical pans, scroll bias, wrapped pixel lookup, complete ring copying, and allocation canaries. Its x64 runs require allocated storage above 4 GiB. These tests preserve the documented inherited `Set` behavior in which an aligned one-pixel run writes nothing.

## Running focused checks

After configuring a supported build as described in [Building](BUILDING.md), build `AsmParityRender` and `RenderContractsTest`, then run their CTest entries:

```powershell
cmake --build build --config Debug --target AsmParityRender RenderContractsTest RingBuffersTest
ctest --test-dir build -C Debug -R '^(asmparity-render|rendercontracts|ringbuffers)$' --output-on-failure
```

Use the same configuration in both commands, and repeat with `Release` for optimized checks. The committed vectors require no proprietary artwork, executable, or assembly toolchain.
