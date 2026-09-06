# Portable codec evidence

The codec portion of the native x64 conversion replaces production x86 assembly while preserving tested compressed bytes, decoded bytes, and carried state. This record covers codec validation only; it is not evidence that saves, movies, or audio were exercised in the running game.

## Implementations and contracts

| Implementation | Preserved contract |
| --- | --- |
| `code/lcw.cpp`, `LCW_Comp` | Literal partitioning, latest equal-length match selection, relative short copies, absolute medium/long copies, long fill decisions, and the original EOF fill boundary. The compressor is on the pipe/straw persistence path. |
| `code/vqalib/lcwdecode.cpp`, `VQA_LCW_Uncompress` | Separate absolute and leading-zero relative reference modes, overlap copying, explicit end marker, and clamping before every output copy/fill. This is deliberately separate from `LCW_Uncomp`. |
| `code/soscodec.cpp`, normal SOS | 16-bit mono, low nibble first, predictor saturation, and index state scaled by 32. Source/destination pointers and the other state fields remain unchanged. |
| `code/soscodec.cpp`, General SOS | 8/16-bit mono/stereo, byte-interleaved compressed stereo channels, ordinary unscaled indexes, and complete sample/code/difference/step state. |
| `code/soscodec.cpp`, VQA SOS | 16-bit mono/stereo with planar compressed stereo channels, interleaved PCM output, and separately carried channel predictors/indexes. |
| `code/vqalib/lcwdecode.cpp`, `AudioUnzap` | All four opcode families, signed delta wrap versus saturating table deltas, command-level output termination, and return of compressed source bytes consumed. |
| `code/vqalib/unvq.cpp` | Six live entry points retain their public names, split pointer planes, table/direct 16-bit solid colors, byte-color fills, alternate output rows, and half-resolution sampling. |

Behavior is preserved on the measured corpus. Empty and single-byte `LCW_Comp` inputs are fixed: the original unconditional second loop reads beyond the requested source length; the replacement emits the defined empty terminator or single-byte literal plus terminator. Independent explicit expected-byte checks cover these two cases. Valid existing LCW streams and sizes from two through 65,535 bytes are compared against the original compressor.

The legacy decoder APIs contain no compressed source length. VQA LCW's length argument bounds decoded output, not compressed input reads. AudioUnzap finishes a command even when its samples exceed the requested count; its caller must provide capacity for complete commands. The parity tests deliberately compare this historical behavior with extra writable capacity. This conversion does not claim malformed-input hardening.

## Synthetic oracle

`tests/asmparity/vectors/codecs.txt` contains 2,918 synthetic input/output records captured with the original Win32 assembly still linked. The test suite compares the portable implementation with the original assembly and with persisted records before retirement. The records contain no proprietary assets, original executable data, or pointer values.

- 204 LCW compressor cases cover random, low-alphabet, periodic, and repeated inputs, literal/run boundaries, and lengths through 65,535; every compressed result also round-trips through the production `LCW_Uncomp`.
- 362 VQA LCW cases cover both modes at every output limit from 0 through 180, including partial literals/copies/fills and explicit end-marker termination.
- 1,424 SOS cases cover all 89 predictor indexes and all 16 tokens with positive/negative saturation boundaries.
- 448 multiframe SOS cases cover the normal decoder, all four General sample/channel combinations, and VQA mono/stereo. Frames vary source alignment, sample remainder, and initial predictor/index saturation, and compare carried state explicitly without serializing pointers or native structure padding.
- 288 AudioUnzap cases cover all 256 opcodes and 32 seeded multi-command streams.
- 192 UnVQ cases cover all six live routines, codebook and solid-color blocks, table conversion, row stride, and unaligned destination storage.

## Commands and results

On September 5, 2026, Windows with Visual Studio 2022 Build Tools, MSVC 19.44.35228, and Windows SDK 10.0.26100:

```powershell
cmake --build baseline/build-asmparity --config Debug --target AsmParityCodecs -- /m /nodeReuse:false
& baseline/build-asmparity/tests/asmparity/codecs/Debug/AsmParityCodecs.exe --capture tests/asmparity/vectors/codecs.txt
cmake --build baseline/build-asmparity --config Release --target AsmParityCodecs -- /m /nodeReuse:false
& baseline/build-asmparity/tests/asmparity/codecs/Release/AsmParityCodecs.exe --golden tests/asmparity/vectors/codecs.txt
```

Both Win32 configurations built and passed all 2,918 records with live assembly present. The five production codec assembly modules were then removed. A dedicated x64 configuration used the temporary migration options in effect at validation time:

```powershell
cmake -S . -B baseline/build-codec64 -G "Visual Studio 17 2022" -A x64 -DOPENTS_EXPERIMENTAL_X64=ON -DOPENTS_POINTER_DIAGNOSTICS=ON
cmake --build baseline/build-codec64 --config Debug --target AsmParityCodecs -- /m /nodeReuse:false
ctest --test-dir baseline/build-codec64 -C Debug -R '^asmparity-codecs$' --output-on-failure
cmake --build baseline/build-codec64 --config Release --target AsmParityCodecs -- /m /nodeReuse:false
ctest --test-dir baseline/build-codec64 -C Release -R '^asmparity-codecs$' --output-on-failure
```

Both x64 builds and golden-vector CTest runs passed with pointer-truncation diagnostics promoted to errors. Existing C5033 warnings remain in untouched `register` declarations in older UnVQ routines. No game process, retail asset, save/reload, movie playback, or audio device was used by these checks; the parent implementation's runtime validation owns those results.

## Architecture review follow-up

The independent state-boundary review found and corrected two identity gaps. WOL advertises and checks `Build_Number()` without using LAN's version-range negotiation, so `code/winstub.cpp` now returns `OPENTS_STATE_VERSION` for this compatibility identity. Win32 retains its existing value, and x64 gains the architecture stamp already enforced by LAN and save loading.

Recording headers differ between Debug and Release because Debug includes `Debug_Unshroud`. Their tags now distinguish both architecture and configuration: Win32 Debug retains `OTSREC1`, x64 Debug retains `OTSREC2`, Win32 Release uses `OTSREL1`, and x64 Release uses `OTSREL2` (each includes a trailing NUL). Payload serialization is unchanged. This intentionally narrows the existing snapshot compatibility boundary: old Release recordings carrying an ambiguous Debug tag are rejected before session fields are read, rather than misread with shifted fields. Existing Win32 Debug recordings keep their tag.

`ArchitectureTest` extracts the production `Build_Number`, version clipping, and initial save/recording guards. After these fixes, `cmake --build <directory> --config <configuration> --target ArchitectureTest -- /m /nodeReuse:false` followed by `ctest --test-dir <directory> -C <configuration> -R '^architecture$' --output-on-failure` passed for Debug and Release in both `baseline/build-asmparity` (Win32) and `baseline/build-codec64` (x64). The cases include opposite-architecture WOL identity, rejection without changing the accepted LAN version range, save rejection before session mutation, same-configuration/opposite-architecture recording rejection, and same-architecture/opposite-configuration recording rejection.

Source review found pointer-width identities consistently carried through `SaveStreamClass`, `AbstractClass`/`LocomotionClass`, and `SwizzleManagerClass`; the save guard precedes session mutation. Internal COM interfaces keep their typed pointers and GUID definitions, with in-process factories registered at startup. The exception implementation selects AMD64 unwinding and native instruction/stack/frame registers on x64. WOL's external COM provider availability remains unverified: its startup failure path reports the missing API and returns to the menu before using a null provider. None of these source checks establishes successful external-provider activation or multiplayer runtime behavior.

## LCW stream allocation follow-up

Adversarial wrapper review found that an 8,192-byte input with no repeated three-byte sequence emits 8,324 compressed bytes, exceeding the old pipe allocation by four bytes and the straw allocation by 71 bytes when its inline block header is included. The straw reader could also place that compressed block before its allocation. Both wrappers now reserve the complete literal expansion, framing, and decoder word-store padding. Custom compressor blocks are validated in 1..64,510 and decoder blocks in 1..65,535 before allocation; encoded header counts outside configured storage stop the wrapper before copying. The pipe uses an explicit header-reading state so a valid 65,535-byte compressed count remains distinguishable while its body arrives in fragments.

`LCWStreamsTest` compiles production `LCW_Comp`, `LCW_Uncomp`, pipe/straw wrappers, and their base streams. Guarded array allocations plus independently constructed literal-only streams check compression bytes and decompression in both directions, empty and partial final blocks, default 8,192-byte decoded blocks, custom block sizes 1/2/63, maximum encoded and decoded header counts, and rejected constructor/header sizes. Final `cmake --build <directory> --config <configuration> --target LCWStreamsTest -- /m /nodeReuse:false` and `ctest --test-dir <directory> -C <configuration> -R '^lcwstreams$' --output-on-failure` passed in Debug and Release for `baseline/build-lcwstreams` (Win32) and `baseline/build-codec64` (x64). `manage.py update` passed structural/lifecycle validation with zero catalog deltas; the parent owns the final integrated manual check and runtime rerun.
