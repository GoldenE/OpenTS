# 64-bit conversion for OpenTS

## Goal

Add a native x64 Windows target to OpenTS, alongside the existing Win32 target rather
than replacing it.

## Current execution

The user authorized the full conversion, runtime desktop access, commit and merge into the fork's `main`, followed by image upgrades on a new branch. Implementation is on `x64/native-windows`, based on `56a6f99`; `x64/phase-0-asm-parity` is retained at that baseline. Unrelated `assets/` remains excluded. The fork has issues disabled, so the contributor issue prerequisite cannot be performed there; review will use a pull request to `GoldenE/OpenTS`.

The conversion and software acceptance checks are complete. Original Win32 outputs were captured before retirement; all four Win32/x64 Debug/Release builds and all 21 tests pass. Final Win32 goldens match. Native save/load, pre-change Win32 save loading, architecture rejection, repeated native replay, complete skirmishes, stock movie rendering, and actual same-host LAN play have passed. Runtime testing found and resolved LZO dictionary sizing, VQA initializer sizing, and video row-rewind defects that compilation alone did not establish. Nonzero decoded/submitted audio and successful DirectSound calls were observed; physical or human-heard output remains unconfirmed because the external meters read zero on both original Win32 and native builds. This explicit measurement boundary is not a claim of audible validation.

Initial source review corrected two assumptions: the unsigned-short transparent remap specialization processes `len - 1` pixels, unlike its primary template, and colour adjustment has CPU-specific arithmetic. Portable replacements must preserve measured valid-input behavior, with these differences represented in synthetic parity cases. Record any deliberate behavior change separately.

## Current milestone tracker

- [x] Create the full-scope `x64/native-windows` branch from the merged FPS/map milestone and preserve unrelated assets.
- [x] Capture and verify 2,918 codec and 572 rendering input/output vectors before retiring production assembly.
- [x] Replace live assembly, preserve measured arithmetic and global storage, and remove the production MASM requirement.
- [x] Correct native pointer transport, fixed-width TMP records, radio parameters, callback signatures, and crash diagnostics.
- [x] Enforce architecture-specific save/network identities and architecture/configuration-specific recording headers before session-field reads.
- [x] Fix demonstrated LCW and LZO stream-capacity defects and test real wrappers with guarded allocations and legacy partial-block compatibility.
- [x] Correct VQA's fixed-size audio initialization guard and existing C++ video row rewinds, with native-layout and multirow raster tests against Win32 reference bodies.
- [x] Build Win32/x64 Debug/Release with fatal pointer diagnostics and pass the final 21-test matrix; retain the isolated logger rerun after build contention in the evidence.
- [x] Match the original four Win32 goldens after assembly retirement.
- [x] Repeat the original four Win32 goldens on the final candidate after the later stream and VQA fixes.
- [x] Verify native Debug save/load, repeated native replay checksums, and a skirmish through its normal AI defeat/score screen.
- [x] Pass the complete manual gate for the native-build, save, recording, and stream documentation.
- [x] Complete the opt-in loopback mode, independent review, and real same-architecture/mixed-architecture LAN checks.
- [x] Complete Release, pre-change Win32 save, opposite-architecture save rejection, movie rendering, audio-submission, and rendering runtime checks, with the physical-audio measurement limit explicit.
- [x] Re-run final affected build/test/runtime/documentation gates after the loopback addition and record exact evidence and limits.
- [x] Include the completed implementation, tests and documentation in the user-authorized close-out commit; the subsequent merge and image-branch creation are recorded by Git.

## Design baseline before implementation

Historical source paths and line numbers in the design below refer to the pre-conversion tree at `56a6f99`. The execution tracker and evidence records describe the current implementation and corrections found during validation.

The blocker is not pointer width. It is that the tree contains hand-written 32-bit
MASM (`code/*.asm`, `.model flat, C` with `use32` at `code/winasm.asm:35`, assembled
with the x86-only `/coff /safeseh` flags at `code/CMakeLists.txt:117` and `:138`) plus
MSVC inline `__asm`, which the x64 compiler rejects outright. Every live routine among
them needs a portable C++ implementation before a second target is possible.

The strategy is therefore **not** "port to x64". It is **"retire the assembly on
Win32, then add the target"**:

- Phases 1–7 build, run, and are verified as Win32, against the existing working
  binary as the reference implementation. Each phase leaves the engine playable.
- Phase 8 is the only phase that adds an architecture, and by then no assembly and
  no known pointer-width hazard remains.

This ordering is the plan's main risk control. Changing implementation and
architecture at the same time would destroy the known-good reference that every
equivalence check depends on.

The work also serves `docs/DIRECTION.md` independently of x64: the assembly blocks
the entity-component migration, any non-MSVC compiler, and any non-Windows target.

### Win32 is retained

`docs/BUILDING.md:3`–`21` and `CONTRIBUTING.md` establish Win32 as the supported,
currently verified target, and `CONTRIBUTING.md` requires an issue before a large
compatibility break. Nothing in this task authorises removing it. x64 is therefore
**added** as a second supported configuration; retiring Win32 is a later, separately
authorised change once x64 has equivalent runtime evidence.

Keeping Win32 also preserves a working reference build for parity diagnosis across
the whole migration.

### Scope boundary

This plan retires assembly and corrects pointer-width defects. It does **not**
change rendering behavior, the pixel-format matrix (565/555/556/655), the renderer
architecture, or the object model. Replacement routines reproduce existing output
except where a phase explicitly classifies a change and states the evidence.

### Preserved colour arithmetic

The initial plan proposed routing spotlight drawing to the scalar fallback after measuring it. The measurements established that the live MMX and scalar arithmetic differ for the 556 and 655 formats. The implementation instead retains the lookup storage and CPU arithmetic-selection flags, with portable C++ reproducing each measured mode. No MMX instructions or assembly dependencies remain. [Rendering compatibility](../docs/RENDERING-COMPATIBILITY.md) owns the output contracts and measurements; this correction preserves all four existing formats rather than accepting an unnecessary visual change.

### Accepted compatibility break, enforced not documented

Pointer members serialise at `sizeof(pointer)` (`code/savestream.h:133`–`139`) and
object identities at `sizeof(uintptr_t)` (`code/abstract.cpp:209`–`242`), so those slots
widen from 4 to 8 bytes on x64. **x86 and x64 builds cannot exchange saves, replays,
or network sessions.**

Documentation alone is not sufficient. `Load_Game` rejects only a mismatched
`ExpectedGameVersion` (`code/saveload.cpp:1161`–`1173`), which is the architecture-neutral
`OPENTS_VERSION_PACKED` (`code/loaddlg.h:89`), and the network min/max versions
(`code/version.h:117`) are architecture-neutral too. An x86 save would therefore pass
the x64 version check and then be read with 8-byte reads against 4-byte slots, and
cross-architecture peers would negotiate successfully. Phase 8 adds an explicit
discriminator that rejects both before any state is mutated.

## Implementation tracker

### Phase 0 — Equivalence harness

**Files:** `tests/CMakeLists.txt`, `tests/asmparity/` (new), `code/CMakeLists.txt`
**Depends on:** nothing

`tests/` contains `logstress/`, `renderclock/`, and `mapcontracts/`; none compares the assembly routines with replacements, so this phase adds that missing oracle.

Two structural constraints shape this phase. First, the assembly is compiled straight
into the executable — `code/CMakeLists.txt:63` is `add_executable(OpenTS WIN32 ${OPENTS_SRC})`
and `code/vqalib/CMakeLists.txt` globs only `*.cpp`/`*.h` into `VQALib` — so no
existing library exposes the `.asm` objects to a test. Second, once a phase deletes
an assembly routine the in-process comparison is gone, so the harness's long-term
oracle must be persisted vectors, not the assembly itself.

- [x] Add an `OpenTSAsm` CMake OBJECT library holding the `.asm` files, consumed by
      both `OpenTS` and the harness, replacing the direct glob at
      `code/CMakeLists.txt:31`. Confirm the objects are not linked twice into `OpenTS`.
- [x] Add a `tests/asmparity/` target wired into `tests/CMakeLists.txt`.
- [x] Add a deterministic seeded generator producing inputs for **only the routines
      that need it**: LCW compression, `VQA_LCW_Uncompress` (both reference modes),
      the three SOS decoders, `AudioUnzap`, the six live UnVQ routines, the voxel
      drawers, the colour blitters, and palette interpolation. Supplement the assembly vectors with production-body blitter tests because source review found that one compiled specialization differs from its primary template.
- [x] Add a byte-compare reporter giving the first differing offset, both values, and
      the seed, so a failure is reproducible from the report alone.
- [x] **Validate the harness with a negative control**: feed it a deliberately
      perturbed implementation and confirm it reports a mismatch. A harness that has
      only ever compared the assembly against itself has not been shown to detect
      anything.
- [x] Add a golden-vector mode: run each routine under Win32 with the assembly present
      and persist input/output vector pairs into `tests/asmparity/vectors/`. These are
      the oracle after the assembly is deleted and the only oracle available on x64.
      Generate them from synthetic input only, so no proprietary asset is required.
- [x] Confirm the harness builds and passes in Win32 Debug and Release.

### Phase 1 — Remove dead assembly

**Files:** `code/isoasm.asm`, `code/cliprect.asm`, `code/smartdeform_.asm`,
`code/auduncmp.asm`, `code/rlerle.h`, `code/mpu.cpp`, `code/vqa_uncomp.asm`,
`code/winasm.asm`, `code/unvq_asm.asm`, `code/isotype.cpp`, `code/misc.h`,
`code/smartdeform.cpp`, `code/soundint.h`, `code/interpal.cpp`, `code/interpal.h`,
`code/voxlib.cpp`, `code/soscomp.h`, `code/ovrlight.cpp`, `code/vqalib/unvq.h`
**Depends on:** Phase 0

A large fraction of the assembly procedures have no live caller. Audit exported data as well before removing a module: `smartdeform_.asm` owned four live deformation globals, which must retain matching C++ storage and initialization even though its procedure is unused.

- [x] Delete `code/isoasm.asm` entirely, and the `Iso_Blit_Asm2` call at
      `code/isotype.cpp:2423` with its `IsoTileUseAsmDrawFunc` gate. `Iso_Blit_Asm1`
      has no reference at all, and `code/isotype.cpp:1575` already records that the
      gate is never enabled and the live renderer is the C++ loop in
      `IsometricTileTypeClass::Draw_Tile`. Update that comment to describe the C++
      rasterizer alone.
- [x] Delete `code/cliprect.asm`. `Clip_Rect` and `Confine_Rect` are declared at
      `code/misc.h:97` and `:99` with no call site; remove the declarations.
- [x] Delete `code/smartdeform_.asm`. `Asm_Ripple_Deform_Points` has only the extern
      declaration at `code/smartdeform.cpp:128`; remove it.
- [x] **Delete `code/auduncmp.asm` entirely.** Its only PROC is `Decompress_Frame`
      (`:64`–`:355`), whose sole reference is the commented-out call at
      `code/dsaudio.cpp:2666`. Remove its declarations at `code/soundint.h:276`–`:278`.
      `AudioUnzap` is **not** in this file; it is in `code/vqa_uncomp.asm:57`–`:348`
      and is live, handled in Phase 5.
- [x] Delete the `#if 0` block at `code/rlerle.h:2773`–`:3027`. All fifteen `__asm`
      blocks in that file are inside it and never compile.
- [x] Delete the `#if 0` block at `code/mpu.cpp:84`–`:115`. The `Get_CPU_Clock` body it
      contains is dead; the live definition is `code/detproc.asm:223` and is handled in
      Phase 3.
- [x] Delete `OLD_VQA_LCW_Uncompress` from `code/vqa_uncomp.asm`; it has no C++
      reference. Retain the rest of the file.
- [x] Delete `Draw_Voxel_Regular_UNUSED_ASM` and `Draw_Voxel_Reverse_UNUSED_ASM` from
      `code/winasm.asm` (lines 1756–2009), together with the already-stale externs at
      `code/voxlib.cpp:48`–`:49`, which declare `Draw_Voxel_UNUSED1_ASM` and
      `Draw_Voxel_UNUSED2_ASM` — names that do not exist in `code/winasm.asm`.
- [x] Delete `Asm_Create_Palette_Interpolation_Table` from `code/winasm.asm` and its
      declaration at `code/interpal.h:29`; the only call is commented out at
      `code/interpal.cpp:153`.
- [x] Delete `ASM_UnVQ_6`, `ASM_UnVQ_8`, `ASM_UnVQ_9`, and `ASM_UnVQ_10` from
      `code/unvq_asm.asm` and their declarations at `code/vqalib/unvq.h:107`, `:115`,
      `:119`, `:123`. Only six of the ten routines are live: `ASM_UnVQ1_C1_TABLE` and
      `ASM_UnVQ1_C1_TABLE_ALT` (`code/vqa.cpp:498`, `:500`, `:632`), and `ASM_UnVQ_4x2`,
      `ASM_UnVQ1_C1_4x4`, `ASM_UnVQ_4x4`, `ASM_UnVQ_4x4_HALF`
      (`code/vqalib/drawer.cpp:187`, `:216`, `:221`, `:233`).
- [x] Delete the `sosCODECCompressData` declaration at `code/soscomp.h:72`; it has no
      definition anywhere in the tree.
- [x] Delete `SpotLightDontUseMMX` (`code/ovrlight.cpp:32`) and the dead branch it
      gates at `:243`; nothing ever writes it.
- [x] Build Win32 Debug and Release and confirm the engine still launches and plays,
      establishing the removals were inert.

### Phase 2 — Pointer-width correctness

**Files:** `code/abuffer.h`, `code/abuffer.cpp`, `code/zbuffer.h`, `code/zbuffer.cpp`,
`code/isotype.cpp`, `code/dsurface.cpp`, `code/alphashp.cpp`, `code/cell.cpp`,
`code/display.cpp`, `code/sidebar.cpp`, `code/ownrdraw.cpp`, `code/progress.cpp`,
`code/wstring.cpp`, `code/dsaudio.cpp`, `code/lcwuncmp.cpp`, `code/aircraft.cpp`,
`code/building.cpp`, `code/unit.cpp`, `code/foot.cpp`, `code/CMakeLists.txt`,
`manual/content/internals/radio.md`
**Depends on:** Phase 1

These are latent truncation defects that compile silently today because pointers are
32 bits. Fixing them on Win32 is behavior-preserving and testable now.

**The audit is compiler-driven, not hand-listed.** A hand-picked file list already
proved unreliable: `code/dsurface.cpp` alone carries 91 casts through `unsigned int`
from `:975` onward. The file list above records where work is known to be needed, not
its boundary.

- [x] Add a temporary migration configuration that makes pointer-truncation
      diagnostics (C4311/C4312 and equivalents) **fatal**, and drive the audit from
      what it reports across the whole tree rather than from any list.
- [x] Widen the buffer-wrapping state, not only its signatures. `ABuffer::Wrap_Overflow`
      (`code/abuffer.h:41`, `:100`) and `ZBuffer::Wrap_Overflow` (`code/zbuffer.h:43`,
      `:102`) must take and return `uintptr_t`, **and** `BufferStart`, `BufferEnd`, and
      `BufferSize` (`code/abuffer.h:80`–`:82`, `code/zbuffer.h:81`–`:83`) and
      `Get_Buffer_End()` (`code/abuffer.h:48`, `code/zbuffer.h:50`) must widen with
      them. Changing the signature alone would leave a 64-bit pointer compared against
      a truncated 32-bit bound, so the wrap fires or fails arbitrarily in the hot
      render path — the exact silent failure this phase exists to remove.
- [x] Sweep the pointer-versus-`Get_Buffer_End()` comparisons at
      `code/alphashp.cpp:264`, `:343` and `code/cell.cpp:2053`, `:2124`, and the pointer
      reconstruction at `code/abuffer.cpp:62`.
- [x] Convert the ~20 `Wrap_Overflow((unsigned int)ptr)` call sites in
      `code/isotype.cpp`, plus `code/abuffer.h:123` and `code/abuffer.cpp:73`.
- [x] Audit the **complete** radio parameter protocol, not a sample. Known pointer
      sends include `param = (int)this` at `code/aircraft.cpp:2744`,
      `code/building.cpp:531`, `:550`, `code/unit.cpp:1251`; `param = (int)&Map[...]` at
      `code/building.cpp:496`, `:533`; and `param = (int)NavCom` at `code/foot.cpp:2225`.
      `manual/content/internals/radio.md:82`–`95` owns this contract and requires a
      complete sender/receiver audit; widen the parameter type and update that page.
- [x] Convert `SetWindowLong`/`GetWindowLong` uses carrying pointers or handles to the
      `Ptr` forms, and the pointer-bearing dialog calls such as `code/progress.cpp:363`.
- [x] Resolve every remaining diagnostic across `code/dsurface.cpp`, `code/display.cpp`,
      `code/sidebar.cpp`, `code/ownrdraw.cpp`, `code/wstring.cpp`, `code/dsaudio.cpp`,
      and `code/lcwuncmp.cpp`, and record any deliberate exception with justification.
- [x] Build Win32 Debug and Release with the migration configuration clean, and confirm
      play is unchanged. `long` stays 32-bit under Windows LLP64, so inherited
      `long`-typed persisted structures need no change; confirm none were altered.

### Phase 3 — Retire inline `__asm` and CPU detection

**Files:** `code/blitblit.h`, `code/mpu.cpp`, `code/mpu.h`, `code/xsurface.cpp`,
`code/getcpu.cpp`, `code/getcpu.h`, `code/detproc.asm`, `code/winasm.asm`,
`code/init.cpp`, `code/bench.h`, `code/milsectmr.cpp`, `code/ovrlight.cpp`
**Depends on:** Phase 2

MSVC rejects inline `__asm` when targeting x64 regardless of what the code does.

- [x] Retire the compiled and disabled inline-assembly specializations and their optimization guards. Reuse primary templates where they match; retain a portable unsigned-short remap specialization because the original processes `len - 1` pixels. Test that distinction, including zero/one-length no-ops, against the captured behavior instead of assuming equivalence by inspection.
- [x] Inventory **every** assembly token in `code/mpu.cpp`, not only the first. Live
      sites are `:106`, the `ASM_RDTSC` macro at `:134`, and `:154`, `:237`, `:251`;
      the single-underscore `_asm` form is easy to miss when grepping. Replace them
      with `__rdtsc()`, or delete the helpers that prove dead.
- [x] Replace the `_asm` block at `code/xsurface.cpp:807`. `code/keyboard.cpp` needs no
      work — its only assembly is commented out at `:75`.
- [x] Replace `code/detproc.asm` with `__cpuid`-based C++, covering **everything it
      defines**, then delete it:
      - `Get_CPU_Clock` (`code/detproc.asm:223`–`:230`) — the live definition, consumed
        by `code/bench.h:45`, `:46` and `code/milsectmr.cpp:90`.
      - `Processor` (`:234`–`:288`), consumed at `code/init.cpp:311`.
      - `CPUType` and `VendorID`, consumed at `code/getcpu.cpp:53`, `:54`, `:86`, `:89`.
      - `Detect_MMX_Availability` and `Detect_CMOV_Availability` (`code/getcpu.h:19`,
        `:20`; called at `code/getcpu.cpp:80`, `:81`).
      - **`UseCMOV` and `UseMMX`.** These are the critical ones: `code/winasm.asm:50`,
        `:51` declare them `EXTERNDEF` and read them at `:236` and `:239` inside the
        `ADJUST_COLOR` macro body, so all four `Adjust_Color_*` procs depend on them and
        `code/winasm.asm` survives until Phase 7. The C++ replacement must define
        `extern "C" char UseCMOV, UseMMX` **and populate them** with the semantics of
        `code/detproc.asm:139`–`:196`. Defining them without writing them would default
        both to 0 and silently flip every `Adjust_Color_*` proc onto its no-CMOV path.
- [x] Measure the MMX and scalar blitters on identical synthetic inputs across all four formats. Retire the assembly instantiations while retaining portable MMX-equivalent lookup arithmetic and its storage where the outputs differ. Keep the existing dispatcher semantics, and capture every preserved mode in the golden vectors.
- [x] Build Win32 Debug and Release and confirm rendering is unchanged apart from any
      spotlight difference recorded above.

### Phase 4 — LCW

**Files:** `code/lcw.cpp`, `code/lcw.h`, `code/vqa_uncomp.asm`, `code/vqalib/cmp.h`,
`code/vqalib/drawer.cpp`, `code/vqalib/loader.cpp`
**Depends on:** Phase 3

- [x] Rewrite `LCW_Comp` (`code/lcw.cpp:195`) in C++, removing its inline `__asm` and
      the `#ifdef _DEBUG` self-assignments at `:205`–`:215` that exist only to pin the
      local variable layout the assembly assumed. Simplify `code/lcw.h:37`–`:43`.
- [x] Verify `LCW_Comp` byte-for-byte against the assembly, then verify round-trip
      through `LCW_Uncomp`. Byte-exactness matters because the compressor feeds
      `code/lcwpipe.cpp:198`, `:213`, `:298` and `code/lcwstraw.cpp:171`, which are on
      the persistence path.
- [x] **Port `VQA_LCW_Uncompress` as its own implementation. Do not route it to
      `LCW_Uncomp`.** The two are not the same algorithm, in two independent ways:
      - **Relative mode.** `code/vqa_uncomp.asm:530`–`:537` reads the first source byte
        and, if zero, branches to a second decoder at `:670`–`:787` whose destination
        copies compute the source as *current dest minus offset*, where the absolute
        decoder adds the first-destination base (`:652`). `LCW_Uncomp`
        (`code/lcw.cpp:70`) implements only the absolute form (`:145`, `:155`). A
        relative stream fed to it decodes the leading `0x00` as a short-copy opcode
        reading before the buffer, then resolves every back-reference against the wrong
        base — silent asset corruption.
      - **The length argument is the overrun guard, not unused.** The assembly computes
        `lastbyte = dest + _length` (`:552`–`:553`) and clamps the count before every
        `rep movsb`/`rep stosb` (`:580`, `:604`, `:635`, `:658`, and the relative-mode
        mirrors). `code/lcw.cpp:63`–`:66` states outright that `LCW_Uncomp` does not
        check the uncompressed length. Substituting it turns a bounded decoder into an
        unbounded one on file- and mod-controlled VQA data.

      The commented-out hint at `code/vqalib/cmp.h:39` names `LCW_Uncompress`, a symbol
      that does not exist in the tree, so it is not evidence of interchangeability.
- [x] Verify the new `VQA_LCW_Uncompress` against the assembly on each mode separately:
      absolute references, relative references, explicit end markers, and
      output-length termination, including decoded runs shorter and longer than the output limit. The existing interface carries no compressed-source length, so compressed-input truncation hardening is not claimed by this conversion.
- [x] Retain `code/vqa_uncomp.asm` — it still holds the live `AudioUnzap`
      (`:57`–`:348`), which Phase 5 replaces.
- [x] Build Win32 Debug and Release; confirm a save written before the change still
      loads, one written after loads correctly, and VQA movies play.

### Phase 5 — Audio codecs

**Files:** `code/soscodec.asm`, `code/olsosdec.asm`, `code/vqa_sos.asm`,
`code/vqa_uncomp.asm`, `code/soscomp.h`, `code/dsaudio.cpp`, `code/vqalib/cmp.h`,
`code/vqalib/loader.cpp`, `code/vqalib/task.cpp`, new `code/soscodec.cpp`
**Depends on:** Phase 4

The three SOS modules decode the same family of Westwood ADPCM, but they do **not**
share one interface: the normal and `General_` forms take `_SOS_COMPRESS_INFO` plus a
byte count (`code/soscomp.h:70`–`:74`), while the VQA form takes separate source,
destination, bit size, channel count, output size, and VQA state
(`code/vqalib/cmp.h:46`–`:48`). Port the distinct public contracts directly; extract a
shared private primitive only after tests prove the predictor and frame-state
transitions are genuinely identical.

- [x] Implement the normal and `General_` decoders in a new `code/soscodec.cpp`,
      preserving each public contract, then delete `code/soscodec.asm` and
      `code/olsosdec.asm`. They dispatch at `code/dsaudio.cpp:993`, `:995`, `:2671`,
      `:2673`.
- [x] Implement `VQA_sosCODECInitStream` / `VQA_sosCODECDecompressData` against their
      own signature (called at `code/vqalib/task.cpp:559`,
      `code/vqalib/loader.cpp:3478`, `:3504`), then delete `code/vqa_sos.asm`.
- [x] Rewrite `AudioUnzap` (`code/vqa_uncomp.asm:57`–`:348`; declared
      `code/vqalib/cmp.h:42`, called at `code/vqalib/loader.cpp:3365`, `:3397`) in C++,
      then delete `code/vqa_uncomp.asm` — now that both it and `VQA_LCW_Uncompress`
      have replacements.
- [x] Verify every decoder against the assembly on a fixed corpus of seeded synthetic
      streams plus explicit clamp and saturation boundary vectors, and on multi-frame
      sequences so carried predictor state is exercised rather than only single frames.
- [x] Build Win32 Debug and Release; confirm in-game audio and VQA movie audio.

### Phase 6 — VQ codebook expansion

**Files:** `code/unvq_asm.asm`, `code/vqalib/unvq.h`, `code/vqalib/unvq.cpp`,
`code/vqalib/drawer.cpp`, `code/vqa.cpp`
**Depends on:** Phase 5

`code/vqalib/unvq.cpp` **already exists** and carries the current C++ implementations
from `:24`. This phase extends that file; it does not create one. After Phase 1 only
six routines remain, all dispatched through function pointers, which makes A/B
comparison straightforward: both implementations can be installed behind the same
pointer and run on identical input.

- [x] Extend `code/vqalib/unvq.cpp` to cover the six live entry points —
      `ASM_UnVQ1_C1_TABLE`, `ASM_UnVQ1_C1_TABLE_ALT`, `ASM_UnVQ1_C1_4x4`,
      `ASM_UnVQ_4x2`, `ASM_UnVQ_4x4`, `ASM_UnVQ_4x4_HALF` — reusing the primitives
      already in that file wherever parity permits. Share what proves genuinely common
      after reading the six variants rather than committing to a parameterised shape in
      advance.
- [x] Verify each against the assembly byte-for-byte through the function-pointer seam,
      then delete `code/unvq_asm.asm` and update `code/vqalib/unvq.h`.
- [x] Build Win32 Debug and Release; confirm VQA playback at each block geometry the
      game selects at `code/vqalib/drawer.cpp:187`, `:216`, `:221`, `:233` and
      `code/vqa.cpp:498`, `:500`, `:632`.

### Phase 7 — Voxel drawers, colour blitters, and interpolation

**Files:** `code/winasm.asm` (deleted), `code/voxlib.cpp`, `code/voxdrsys.cpp`,
`code/ovrlight.cpp`, `code/interpal.cpp`, `code/interpal.h`, `code/CMakeLists.txt`,
`CMakeLists.txt`
**Depends on:** Phase 6

**The existing C++ voxel drawers must be verified and corrected before use.** `code/voxlib.cpp:52` declares
`VoxelFuncPtr VoxelDrawFunctions[32]`: entries 0–15 are the assembly path and entries
16–31 are, in the file's own words, *"The same set again, with the C++ drawers in place
of the assembly ones"* — `Draw_Voxel_Regular_Normals` (`:1065`),
`Draw_Voxel_Reverse_Normals` (`:1159`), `Draw_Voxel_Regular_Normals_Lighting` (`:1484`),
`Draw_Voxel_Reverse_Normals_Lighting` (`:1583`), `Draw_Voxel_Regular` (`:1936`), and
siblings at `:1256`, `:1366`, `:1685`, `:1809`. Dispatch at `code/voxlib.cpp:841`–`:855`
builds `funcnum` from four bits, so it never exceeds 15 and the C++ half is currently
unreachable. Several slots in the first half (the `_ZBuffer` variants) already point at
C++ today.

- [x] Verify the existing C++ drawers in `code/voxlib.cpp` against the six live
      assembly procs through `VoxelDrawFunctions`, driving each from the harness.
- [x] Collapse `VoxelDrawFunctions` to the C++ half, remove the assembly entries and
      the externs at `code/voxlib.cpp:42`–`:47`, and delete the six
      `Draw_Voxel_*_ASM` procs from `code/winasm.asm`. Any behavioral difference the
      comparison reveals is classified and recorded per `AGENTS.md`, not absorbed
      silently.
- [x] Implement `ADJUST_COLOR` (`code/winasm.asm:128`, instantiated `:623`–`:626`) and
      `BRIGHTEN_COLOR` (`:629`, instantiated `:798`–`:802`) in C++ across the four
      pixel formats. The assembly already parameterises these by macro, so the same
      shape is evidenced rather than speculative. Retain the `UseCMOV`/`UseMMX` arithmetic selectors in the portable implementation because those modes have different measured rounding; they no longer select assembly instructions.
- [x] Implement `Asm_Interpolate`, `Asm_Interpolate_Line_Double`, and
      `Asm_Interpolate_Line_Interpolate` (`code/winasm.asm:2053`, `:2143`, `:2361`;
      called at `code/interpal.cpp:358`, `:362`, `:366`) in C++ and update the externs
      at `code/interpal.cpp:54`–`:66`.
- [x] Verify each replacement against the assembly, then delete `code/winasm.asm`.
- [x] Remove MASM support now that no `.asm` file remains: the `OpenTSAsm` library from
      Phase 0, the `ASM_MASM` blocks at `code/CMakeLists.txt:116`, `:137`, `:246`,
      `:252`, `:288`, and `ASM_MASM` from the `project()` languages at
      `CMakeLists.txt:3`. Switch `asmparity` to its persisted golden vectors.
- [x] Build Win32 Debug and Release; confirm voxel units render correctly across
      rotations, lighting, and Z-buffer states, and that scaled output is unchanged.

### Phase 8 — Add the x64 target

**Files:** `CMakeLists.txt`, `code/CMakeLists.txt`, `code/saveload.cpp`,
`code/loaddlg.h`, `code/version.h`, `code/savestream.h`, `docs/BUILDING.md`,
`docs/HISTORY.md`, `manual/` release and lifecycle records, `.github/workflows/`
**Depends on:** Phase 7

Only now does a second architecture appear, against a tree with no assembly and no
known pointer-width defect.

- [x] Configure and build with `-A x64` in Debug and Release and resolve what the
      compiler reports. Make `/arch:SSE2` conditional on Win32 rather than removing it
      (`code/CMakeLists.txt:107`, `:112`, `:128`, `:133`) — it is rejected on x64, where
      SSE2 is the baseline, but Win32 still needs it. Keep `/fp:precise` on both.
- [x] Record that the excess-precision hazard `/arch:SSE2` exists to prevent is removed
      on x64 by the architecture itself, since x64 has no x87. State this alongside the
      flags rather than leaving the conditional unexplained.
- [x] **Add an enforced save-format discriminator.** `Load_Game`
      (`code/saveload.cpp:1161`–`:1173`) currently rejects only a mismatched
      `ExpectedGameVersion`, which is the architecture-neutral `OPENTS_VERSION_PACKED`
      (`code/loaddlg.h:89`), so an x86 save would pass and then be read with 8-byte
      reads against 4-byte slots. Add an architecture or save-format discriminator and
      reject the mismatch **before any game state is mutated**.
- [x] **Make network negotiation reject architecture-incompatible peers.** The min and
      max versions at `code/version.h:117` are architecture-neutral, so x86 and x64
      peers would currently negotiate successfully and then desync. Extend the
      negotiated identity.
- [x] Verify the save, replay, and network paths end to end on x64, and confirm the
      widened pointer and identity slots (`code/savestream.h:133`–`139`,
      `code/abstract.cpp:209`–`242`) round-trip correctly within an x64 build.
- [x] Record the cross-architecture incompatibility and the new guards in
      `docs/HISTORY.md`, in the manual's release notes, and wherever the save stamp and
      network version are owned.
- [x] Update `docs/BUILDING.md`: add x64 to the supported-target table and the
      configure commands, keep Win32 documented as supported, and note that MASM is no
      longer a build requirement.
- [x] Update the `Engine` and `Engine nightly` workflows under `.github/workflows/` to
      build both architectures.
- [x] Run `python manual/tools/manage.py update` then `manage.py check`, and resolve
      what it reports.
- [x] Run the full `tests/` suite including `asmparity` against its golden vectors and
      `logstress`, in Win32 and x64, Debug and Release. Record exact commands,
      configurations, and results.
- [x] Play-test on x64 against the retail install referenced in `AGENTS.md`: skirmish
      start to finish, save and reload, voxel and infantry rendering, VQA playback with
      audio, spotlight rendering, and the scaled view.
- [x] On the user's request, and following the commit rules in `AGENTS.md`, commit the
      work with an imperative subject of at most 72 characters, such as
      `Retire x86 assembly and add an x64 target`.

## September 5–6 implementation and acceptance record

The build commands used Visual Studio 2022 Build Tools with MSVC 19.44.35228, Windows SDK 10.0.26100, and CMake 4.4.3. Separate trees `baseline/build-win32-migration` and `baseline/build-x64-audit` held the architectures; `cmake --build <tree> --config Debug` and `--config Release` completed with fatal pointer-truncation diagnostics. The final test gate ran `ctest --test-dir <tree> -C <configuration> -R '^logstress$' --output-on-failure` while idle, then `-E '^logstress$'` for the other 20 entries. Every entry passed in all four combinations. One earlier logger-latency failure under concurrent builds is retained in the logs; thresholds were not relaxed.

The 2,918 codec and 572 rendering vectors were captured with the original Win32 assembly present and passed live comparisons in Debug and Release before retirement. Their lossless archives retain every input/output byte. [Codec evidence](x64-codec-evidence.md), [rendering compatibility](../docs/RENDERING-COMPATIBILITY.md), and the individual test READMEs own the narrow contracts. Actual LCW/LZO wrapper tests use guarded allocations, and the VQA tests exercise native audio initialization and multirow drawing through production entry points rather than only isolated replacements.

Local runtime evidence is under `baseline/x64-migration-2026-09-05/`; `runtime-report.md` owns the detailed cases, commands, screenshots, logs, build identities and measurement limits. Native Debug and Release both saved and reloaded live Grand Canyon sessions and reached ordinary AI defeat/score screens. A pre-change Win32 save loaded in current Win32. Both directions of architecture mismatch were rejected by the actual `Load_Game` guard before world replacement, in addition to being hidden by the load list. Three native replay runs, including the final candidate, matched all 2,475 comparable lines at frame 300.

Two native x64 Debug clients used the opt-in loopback transport and the ordinary LAN discovery/join/options/event protocol. Move orders were accepted on both clients; six read-only CRC samples matched, including host/peer frames 3310/3313, 3331/3333 and 3351/3353 with 252–253 completed common frames per sample and zero mismatches. The sampler excludes the active ring slot and its oldest alias. No desync dialog occurred, and both clients reached the same peer-winner result after surrender. A Win32 peer discovered the x64 room but its real Join request was rejected as version-incompatible while rules/art/AI fingerprints matched. These are same-host transport results, not separate-machine network measurements. The recorded LAN binary identities predate the subsequent movie-only row fixes and bounded audio diagnostics; those paths are covered by the final builds, tests, movie retries and replay comparison.

The final Debug and Release candidates rendered full, changing stock Intro video, including a 3200×2000 physical capture from the 1280×800 logical viewport. Both produced nonzero decoded and submitted 16-bit PCM and successful DirectSound Create/Lock/Play and volume readback. The external per-process meter remained zero with an active, unmuted session on both original Win32 and native builds; no listening or physical-device output claim is made. Renderer arithmetic and the additional VQA geometries are covered separately by the synthetic production-body comparisons.

A final live spotlight fixture used a stock powered `GASPOT` light tower on an ignored synthetic map. Both native Debug and Release showed its ground-light pool sweeping while the tower remained fixed, exercising `BuildingLightClass` and `SpotLightClass::Draw_It` rather than inferring integration from pixel vectors. The paired captures and fixture record are under the runtime evidence directory's `spotlight/` folder.

`pwsh -NoProfile -File baseline/x64-migration-2026-09-05/Invoke-Win32Goldens.ps1 -Label final` matched all four original Win32 goldens after the complete source changes. The desktop was released and runtime input files were restored with matching recorded hashes. Generated saves, recordings, screenshots and logs remain local and ignored; unrelated `assets/` is excluded. No subsequent HD engine work belongs to this branch.

## Effort and risk

**The live surface is far smaller than the line count.** After Phase 1's removals and
Phase 7's reuse of the existing C++ voxel drawers, the routines that genuinely need
new implementations are: `LCW_Comp`, `VQA_LCW_Uncompress`, three SOS decoders,
`AudioUnzap`, six UnVQ routines, two colour blitters, three interpolation routines, and
the CPU detection block. Everything else is deleted or already exists in C++.

**Effort is dominated by verification, not translation.** Any one routine is a day or
less to write and considerably longer to prove equal.

**The failures here are silent.** A wrong blitter does not crash; it renders subtly
wrong in a rarely-seen unit at a rarely-seen angle. This is why Phase 0 comes first,
why it must be validated with a negative control, and why every phase compares against
the assembly rather than reasoning about it.

**The sharpest risks are the two decoders on the data path.** `VQA_LCW_Uncompress` is
bounds-checked assembly consuming file- and mod-controlled data; getting it wrong is
heap corruption, not a visual artifact. `LCW_Comp` feeds the persistence path, so it
requires byte-exactness rather than visual equivalence. Both are sequenced before the
purely presentational phases.

**Performance risk is low.** Modern compilers auto-vectorize these loops and the
performance target is a 1999 game. The one place to measure rather than assume is the
spotlight path, where the MMX implementation is the live one.

**Reference implementations exist.** LCW, VQA, and the Westwood SOS codecs have public
C implementations in GPL-licensed projects. Provenance and licence must be checked
against this repository's SPDX and attribution requirements in `AGENTS.md` before any
of it is used, and any borrowed work attributed accordingly.
