# LZO stream contracts

`LZOStreamsTest` compiles production `CStreamClass`, `LZOPipe`, `LZOStraw`, and the bundled LZO1X compressor/decompressor, replacing only the engine precompiled header with the standard headers needed for this standalone target. A real in-memory COM stream is the save-stream sink and source. Array allocations end at guard pages, so a dictionary or compressed-output overrun fails the test rather than corrupting unrelated heap storage. On x64 the synthetic input must reside above 4 GiB.

The corpus includes seeded incompressible data spanning two complete 64 KiB save blocks plus a partial block, fragmented writes and reads, repeated input, and compression in both directions through the pipe/straw interfaces. It exercises all five dictionary allocation sites, including the pipe's buffered-block, direct-block, and final-flush paths.

The dictionary size comes from the bundled library's `LZO1X_MEM_COMPRESS`: 16,384 native pointer slots, which occupy 64 KiB on Win32 and 128 KiB on x64. The dictionary is process-local scratch storage and is never persisted. Compressed staging allocations use the conservative `LZO_Compression_Bound` capacity, covering literal expansion and end markers even for incompressible or very small input blocks.

`CStreamClass` keeps its two 32-bit block-header fields. New partial blocks record the actual uncompressed length. Older writers recorded 64 KiB even for a short final block; the reader accepts that representation and uses the actual decoded length. Tests cover both directions of compatibility: an old-style partial header is read by the production reader, and a new partial block is read by the frozen pre-migration `Read` body in `legacy_read.inc`. The legacy fixture substitutes only that reader body while using current wrapper allocation and lifecycle code. These checks cover compression framing, not cross-architecture object serialization or complete save-game compatibility.

```powershell
cmake --build build --config Debug --target LZOStreamsTest
ctest --test-dir build -C Debug -R '^lzostreams$' --output-on-failure
```

Repeat with `Release` and with the separately configured x64 build directory. No game data or running engine is required.
