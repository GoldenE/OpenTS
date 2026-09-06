# Larger map support

## Summary

Phases 1–3 are implemented and validated together with FPS Phases 1–3 to the extent available on this machine. Both dimension constants remain 512. The combined milestone is carried on `engine/smooth-motion-map-fixes`; the sibling plan/preparation record came from `map-size/phase-1` at `f1c9e29` without switching branches. Live testing completed the 256×256 baseline and exposed two additional defects, now fixed: iterator termination at the table boundary and selection of a map without a preview. Final builds, tests, goldens, terrain A/B checks, and save/load checks pass. The [handoff](handoff-2026-09-04.md#resume-boundary-and-next-milestone) owns the user-authorized branch close-out and explicit deferral of the unavailable two-client LAN acceptance check. Historical uncommitted-state statements below describe their original sessions.

Today the ceiling is `W + H <= 512` cells, which allows a 256x256 map of
130,816 cells. That is already about 1.5x the largest map Tiberian Sun shipped,
so this is a capability improvement rather than a fix for a blocking limit. The
goal is to make the ceiling a deliberate, documented engineering choice instead
of an accident of a 512-entry row stride, and to lift it to `W + H <= 1024`
(a 512x512 map, roughly four times the area) for maps that want it.

The work is staged so that the two most valuable pieces land before the
constant changes at all. Decoupling the engine's full-array scans from the size
constant is an unconditional improvement that makes periodic costs proportional
to the actual map rather than to the maximum one, and the latent-defect fixes
are correctness work that stands on its own. Only after those does the constant
move.

This plan is independent of `plans/stargate-mod.md`. That mod does not need a
larger map and must not block on this.

## Current milestone tracker

- [x] Preserve existing FPS work, import the corrected sibling-branch record, and audit current source.
- [x] Revalidate both prepared synthetic maps without modifying their originals.
- [x] Finish Phase 1 editor acceptance and flat 125×125/256×256 runtime measurements, kept separate from stock Grand Canyon.
- [x] Implement the safe Phase 2 scan changes and correct the incompatible conversions in D4.
- [x] Implement Phase 3, including the live legacy constructor callers and missed save-load stride.
- [x] Complete combined validation available on this machine; retain the explicit two-client LAN dependency in the FPS plan.
- [x] Include this milestone in the user-authorized combined close-out commit on `engine/smooth-motion-map-fixes`, excluding unrelated `assets/`.

## How Tiberian Sun maps work

### The playfield is a rotated diamond

A map declares `[Map] Size=0,0,W,H` and `[Map] LocalSize=`. `Size` is the whole
playfield; `LocalSize` is the playable sub-region, clipped inward, that bounds
scrolling, off-map deletion, reinforcement entry, and crate placement
(`manual/content/keys/size.md`, `manual/content/keys/localsize.md`).

Cells are not laid out as a `W` by `H` grid. `MapClass::In_Radar` defines the
playfield as the diamond satisfying all of:

```
x + y >  PlayRect.Width
x - y <  PlayRect.Width
y - x <  PlayRect.Width
x + y <= PlayRect.Width + 2 * PlayRect.Height
```

so a map's cells occupy a rotated region of a square index space, and the cell
count is approximately `2 * W * H`.

### The cell store

`MapClass::Array` is a `VectorClass<CellClass *>` (`code/map.h:509`) resized
once to `MAP_CELL_TOTAL` (`code/map.cpp:654`). It is a **sparse pointer table**:
only cells inside the diamond get a `CellClass` allocated
(`code/map.cpp:715` onward). The size constant therefore costs one pointer per
slot regardless of the map, while real per-cell memory tracks the authored map.

`MAP_CELL_W`, `MAP_CELL_H`, and `MAP_CELL_TOTAL` are defined at
`code/sun.h:80-82`, all currently 512 / 512 / 262,144.

### Where the ceiling comes from

`MapClass::Set_Map_Dimensions` (`code/map.cpp:715`) computes
`msize = W + H - 1`, then walks `y` over `[0, 2*msize+2)` and `x` over
`[0, msize+2)`, indexing `idx = MAP_CELL_H * y + x` for each cell that passes
`In_Radar`.

The loop bounds are wider than the diamond, but `In_Radar` filters, so the
highest coordinate actually allocated is `W + H - 1` on **both** axes. Indexing
with a row stride of 512 therefore requires `W + H - 1 <= 511`:

**`W + H <= 512`.**

A 256x256 map reaches coordinate 511 on each axis, at different cells. Its highest allocated row-major index is 261,889, at (257,511); (511,511) lies outside the diamond. It reaches the final table row and column, not the final slot. The next size sum requires a valid row numbered 512 and exceeds the table.

### Measured sizes

Simulating the allocation loop against the real `In_Radar` predicate:

| Size | W+H | Cells | Coordinate bounds (max X, max Y) | Highest allocated index |
|---|---|---|---|---|
| 256 x 256 | 512 | 130,816 | (511, 511) | 261,889 |
| 400 x 112 | 512 | 89,488 | (511, 511) | 261,745 |
| 298 x 149 | 447 | 88,655 | (446, 446) | 228,502 |
| 120 x 150 | 270 | 35,850 | (269, 269) | 137,879 |
| 125 x 129 | 254 | 32,121 | (253, 253) | 129,666 |

Shipped map sizes were read directly from the `[Map] Size=` entries inside
`MAPS01.MIX`, `MAPS02.MIX`, `maps03.mix`, and `multi.mix` in a retail install -
135 maps, all plain INI text inside the archives:

- **Largest shipped multiplayer map: 298 x 149** (~88,655 cells). Several maps
  share that size. The largest by `W+H` is 328 x 120 (448).
- **Largest shipped campaign map: 120 x 150** (~35,850 cells), then 125 x 129,
  "Core of the Problem", GDI/Nod mission 9.

So stock content reaches about 87% of the coordinate ceiling but only about 68%
of the available area, because the widest shipped maps are elongated rather than
square.

### Editor

The bundled FinalSun 2.0 changelog records "Maps up to 400x112 (or 112x400) are
now allowed". That pair sums to exactly 512, so the editor already tracks the
engine's `W + H` relation, but it is expressed as a fixed dimension pair rather
than as the sum. Whether it will author a 256x256 map is a property of the
editor, not the engine.

## What already scales, and does not need work

Verified, and recorded here so the implementation does not re-litigate it:

- **Network events are coordinate-addressed, not index-addressed.** Events carry
  `xCell { short X; short Y; }` (`code/coord.h:100`, used at `code/event.h:142`
  and elsewhere). Packet layout does not depend on the size constant.
- **Map terrain formats 2 through 5 are coordinate-addressed.** Each record
  carries an explicit `Cell` and the stream is terminated by `CELL_NONE`
  (`code/map.cpp:2257`, `:2292`, `:2324`, `:2360`). They are size-agnostic.
- **The `CELL_NONE` sentinel stays safe.** It is `Cell(0,0)`
  (`code/globals.cpp:99`), and `In_Radar` requires `x + y > W`, so (0,0) is
  never a valid playfield cell at any size.
- **`Cell` is `TPoint2D<short>`** (`code/coord.h:36`). Coordinates up to 1023,
  or far beyond, are well inside range.
- **Saves do not store the cell array positionally.** `MapClass::Serialize`
  records that the array is reallocated on load and each cell reinstalls itself
  in `CellClass::Post_Load`. The project version gates saves, but development snapshots within the same version are not promised to interoperate; [Contributing](../CONTRIBUTING.md#compatibility-boundaries) owns that policy. This milestone changes no serialized fields.
- **The zone graph is sized from the map, not the constant.**
  `CellZoneCount = (PlayRect.Width + PlayRect.Height + 1)^2`
  (`code/map.cpp:963`), and `Search.Update_Map_Dimensions(PlayRect)` follows.
- **A\* neighbour offset tables are expressed in terms of `MAP_CELL_W`**
  (`code/astar.cpp:330-337`) and scale with it automatically.
- **`Coord`/`Cell` conversion has headroom.** `Cell::As_Int` is
  `(Y - MAP_CELL_W * (X + Y) - X) << 6` (`code/coord.h:50`); at a 1024 constant
  with coordinates to 1023 the magnitude stays around 1.3e8, well inside int32.

## Defects a size change would expose

Each was found by reading the code, and each is a real defect today or becomes
one the moment the constant moves.

### D1 - The legacy terrain reader is sized by the wrong constant

`MapClass::Read_Binary_1` (`code/map.cpp:2209`) iterates `MAP_CELL_TOTAL/16`
records three times and decodes each position as `Cell(i % 128, i / 128)`.

`MAP_CELL_TOTAL/16` is 16,384, which is 128 * 128 - the **fixed dimensions of
the version 1 map format**, not a property of the current cell array. The
expression only produces the right number because the constant happens to be
512. Raising it to 1024 makes the loop read 65,536 records and consume four
times the intended stream.

This is the clearest reason not to change the constant casually.

### D2 - `MAP_CELL_W` and `MAP_CELL_H` are used interchangeably as the row stride

The tree does not have one stride. It has two names for the same number, used
inconsistently for the same purpose:

- `code/map.cpp:449`, `:469`, `:490`, `:687` index with `y * MAP_CELL_H`.
- `code/map.cpp:829`, `:1730`, `:1817`, `:4475`, `:4830` index with
  `y * MAP_CELL_W`.
- `code/astar.cpp:281-282` and `:382` index with `MAP_CELL_W * y + x`.
- `code/map.cpp:6585` and `:6600` index with `IterX + IterY * MAP_CELL_H`.
- `code/map.cpp:732` and `:895` decompose an index as
  `Cell(i % MAP_CELL_W, i / MAP_CELL_H)` - one name for the modulus and the
  other for the divisor.
- `code/map.cpp:8698` uses **both in a single expression**:
  `&Array[x + y * MAP_CELL_H - MAP_CELL_W - 1]`.

Consequence: the two constants must remain equal. A non-square pair would
corrupt cell indexing silently across pathfinding, iteration, and terrain.
This milestone uses `MAP_CELL_W` for indexing and decomposition throughout `map.cpp`; `astar.cpp` already does so and needs no edit. `sun.h` enforces equality at compile time. The persistence trace also found `CellClass::Post_Load` using `CellID.Y << 9`; it now multiplies by `MAP_CELL_W`. Its numeric value remains 512 and no save representation changes.

### D3 - The 128-stride constructor has live legacy callers

The original audit's claim that `Cell(int)` was unused was false. Removing it exposed three live legacy INI callers in `AircraftClass::Read_INI`, `BuildingClass::Read_INI`, and `UnitClass::Read_INI`, all under `NewINIFormat < 4`. They now explicitly decode `Cell(c % 128, c / 128)`, preserving the legacy coordinate format while removing an ambiguous general-purpose constructor. The other cited `UnitClass` site selects the copy constructor.

### D4 - Scan semantics differ by routine

Only `DisplayClass::Encroach_Shadow` is safely shortened to the square enclosing the `In_Radar` diamond without changing its effective cell set. Its two loops now end at `min(MAP_CELL_W, PlayRect.Width + PlayRect.Height)`, retain the predicate and row-major order, and leave `All_To_Look` and redraw ordering unchanged. This uses `PlayRect` rather than mutable `MapRect`.

The earlier proposal to bound all remaining loops by the playfield was incorrect:

- `OverlayClass::Read_INI` and `Write_INI` consume and produce positional 512×512 byte planes. Shortening them changes the format and displaces subsequent cells. Both overlay and overlay-data passes remain full-sized.
- `DisplayClass::Write_INI` and `SmudgeClass::Write_INI` have no `In_Radar` filter. They query every array coordinate, including retained allocated cells outside the current playfield and the shared scratch cell. A playfield-only traversal would omit state their existing behavior can emit.
- `MapClass::Init_Cells` resets every non-null array slot. `Set_Map_Dimensions(reset_cells=true)` can retain allocations outside the new playfield, so a playfield iterator is not equivalent. Initialization now uses one linear pointer-table pass in the same order, bounded by the original slot count and actual table length.
- `MapClass::Validate`'s obsolete loop is inside `#if NEVER`; the active function returns true. There is no active validation scan to optimize.
- `DisplayClass::Encroach_Fog` already uses `Reset_Iterator` / `Iterate` and remains unchanged.

These are compatibility corrections to the plan, not pending requests to shorten those loops later. Making all maintenance costs proportional to live allocation would require a separately designed allocation inventory and scratch-cell contract.

### D5 - The IsoMapPack5 decode buffer scales with the constant

`code/display.cpp:3255-3264` derives the terrain payload limit from `MAP_CELL_TOTAL`, converts it to a maximum compressed block size, and allocates a `std::vector<unsigned char>` of `maximum_decoded_size + 1`. Raising the constant multiplies that transient allocation by approximately four inside a 32-bit process.

### D6 - Per-house region arrays are fixed-size and serialized

`HouseClass::Regions` is `RegionClass Regions[MAP_TOTAL_REGIONS]`
(`code/house.h:1059`), where `MAP_TOTAL_REGIONS` derives from the size constant
(`code/sun.h:106-108`). `RegionClass` holds a single `int Threat`
(`code/region.h:38`) and serializes it. At 512 that is 16,900 regions - about
66 KB per house; at 1024 it becomes 66,564 regions - about 260 KB per house,
in-class and written into every save.

### D7 - Minor pre-existing bounds issues

- `code/combat.cpp:732-735` bounds-checks with `> MAP_CELL_W` rather than
  `>= MAP_CELL_W`, an off-by-one that admits one out-of-range column and row.
- `code/debug.cpp:239-240` sets `MapRect` to `MAP_CELL_W-2` / `MAP_CELL_H-2`.

Both are marginal and neither blocks the change; record them, fix D7's first
item opportunistically while the file is open.

## Implementation tracker

Phases run serially. Phases 1 through 3 make no size change and are each
independently valuable; the constant does not move until Phase 4.

### Phase 1: Establish the measurement baseline

**Files:** none changed; measurement only, plus notes recorded in this plan.
**Depends on:** nothing.

- [x] Measure `sizeof(CellClass)` in the supported Win32 Debug and Release configurations and record both figures here.
- [x] Compute and record total cell memory for a 256x256 map from that figure and the verified 130,816 cell count.
- [x] Measure peak process memory, scenario load time, and `Encroach_Shadow` cost on stock Grand Canyon and the two authored flat maps; see the live acceptance record below.
- [x] Record the transient `IsoMapPack5` decode allocation actually reached at load.
- [x] Record the results in this plan as the baseline every later phase is compared against.

#### Phase 1 results (2026-09-04)

Toolchain: VS 2022 Build Tools 17.14.39, CMake 4.4.3, generator
`Visual Studio 17 2022` Win32, tree at `27414fd` on `map-size/phase-1` plus
the temporary instrumentation described below (reverted before commit).

**`sizeof(CellClass)`: 164 bytes in both Win32 Debug and Win32 Release.**
Read from the compiler rather than a run: a probe line
`char (*OpenTS_SizeProbe)[sizeof(CellClass)] = 1;` appended to `code/cell.cpp`
fails with `cannot convert from 'int' to 'char (*)[164]'` under
`cmake --build build --config <Debug|Release> --target OpenTS`. The Debug
figure was also confirmed at runtime by the log line below.

**Cell memory for a 256x256 map:** 130,816 cells x 164 bytes = 21,453,824
bytes (about 20.5 MiB) of `CellClass` storage, plus the pointer table of
262,144 x 4 bytes = 1,048,576 bytes (1 MiB) that every map pays regardless of
size. For comparison the stock map measured below allocates 31,125 cells,
about 5.1 MB.

**Stock-size map (G_CANYON, `Size=0,0,125,125`, `LocalSize=4,4,118,114`),
Win32 Debug, replayed unattended and minimized from the golden recording
`skirmish-gcanyon` with `tools/baseline/Invoke-VanillaBaseline.ps1 -Session
skirmish-gcanyon`:**

| Measure | Value |
|---|---|
| Cells allocated / table slots | 31,125 / 262,144 |
| Scenario load, `Read_Scenario_INI` entry to `Start_Scenario` return | 0.62 s (21:31:01.279 to 21:31:01.896) |
| `IsoMapPack5` decode buffer allocated | 5,784,965 bytes (fixed by `MAP_CELL_TOTAL`) |
| `IsoMapPack5` bytes actually decoded into it | 168,517 (2.9% of the buffer) |
| `Encroach_Shadow` per call, frames 60 / 120 / 180 / 240 | 3,896 / 3,877 / 5,236 / 4,160 us |
| Peak working set at frame 300 | 284,549,120 bytes (271.4 MiB) |
| Working set at frame 300 | 279,314,432 bytes |
| Peak pagefile usage at frame 300 | 227,450,880 bytes (216.9 MiB) |
| Replay result | `MATCH` against the golden dump |

The decode buffer is `maximum_decoded_size + 1` at `code/display.cpp:3262-3264`
with an 11-byte record (`Cell` 4, `IsometricTileType` 4, `SubTile`, `Height`,
`IsIceGrowthAllowed` 1 each): 353 LZO blocks x 16,388 bytes. At a 1024
constant the same formula gives 1,409 blocks, 23,090,693 bytes (22.0 MiB),
four times today's allocation, as D5 predicts. The scenario's actual payload
would still be a few percent of it.

`Encroach_Shadow` figures are from a Debug build and are an upper bound;
`ShroudGrow` is off in the retail rules (`ShroudGrow=0` logged), so the
routine was invoked by the instrumentation at fixed frames rather than by its
rules timer. The calls did not change the sync dump, so the CRC does not fold
shroud state. Each call is two full 512x512 scans of `In_Radar` plus the
per-cell work; the 4 to 5 ms is the price a 125x125 map pays today for the
maximum-size scan.

The original measurement record attributed the roughly 2.5 minutes beyond loading and simulation to focus-loss pauses. The handoff separately reported that skirmish replays did not pause; those observations do not establish reliable background execution. Source verification on September 5 confirms that `Check_For_Focus_Loss` waits for focus for both campaign and skirmish sessions, and `Focus_Restore` can recapture the mouse. [Runtime testing](../docs/TESTING.md#baseline-observations) owns the focus behavior and the constraint on unattended runs.

**256x256 map: not measured.** No shipped map is that size, and the only
unattended load path is replaying a golden recording, whose map is baked into
the recording. Substituting a loose `G_CANYON.MAP` with `Size=0,0,256,256`
(loose files win over archive contents in `CCFileClass::Open`,
`code/ccfile.cpp:410`) would move the playfield diamond: `In_Radar` accepts
`x + y > W`, so the authored cells at `x + y` in (125, 375] mostly fall outside
the (256, 768] band of the new diamond, and the recording's spawn waypoints
with them. A faithful 256x256 measurement needs a map authored at that size
and a live session, and a live session was ruled out because the user was at
the machine. What the analysis above already fixes without a run: cell memory
(20.5 MiB), the pointer table (1 MiB), and the decode buffer (unchanged at
5.78 MB, since it is sized from the constant). Load time and peak working set
at 256x256 remain to be taken when a map and a live session are available.

Temporary instrumentation used, all reverted: a `DebugString` after
`Set_Map_Dimensions` in `DisplayClass::Read_INI` counting non-null `Array`
entries and printing `sizeof(CellClass)`; `DebugString` lines around the
`IsoMapPack5` vector and `Get_UUBlock` in `DisplayClass::Read_INI`; a
`QueryPerformanceCounter` bracket calling `Map.Encroach_Shadow()` at frames
60, 120, 180 and 240 after the shroud-grow block in both `LogicClass::AI`
paths; `GetProcessMemoryInfo` at the `TrapPrintCRC` exit in `Queue_Playback`;
and `MEASURE` markers at the start and end of `Read_Scenario_INI` and the end
of `Start_Scenario`, read with the debug log's millisecond timestamps.

#### September 5 offline preparation

The branch was rebased onto fork main `64383e2`. Two synthetic flat maps are prepared under ignored `baseline/map-prep-2026-09-05/`: `flat-125.mpr` (31,125 cells) and `flat-256.mpr` (130,816 cells). All terrain cells and four spawn waypoints per map satisfy the current `In_Radar` predicate, and every compressed terrain block round-trips through the engine's Win32 Release LZO codec. `validation.json` records sizes, hashes, and verification results; `prepare_maps.py` and `lzo_roundtrip.cpp` make the fixtures reproducible locally without proprietary input files.

The maps have not been opened in FinalSun or loaded by the game, and are not staged in `Run/`. The user deferred desktop access on September 5, so editor acceptance, runtime load/rendering/pathfinding, recording, and measurements remain outstanding. Use the flat 125x125 map as a controlled comparison for the flat 256x256 map, while retaining the stock `G_CANYON` figures above as a separate workload. Generating a fixture does not complete the runtime measurement item or establish editor support.

### Phase 2: Decouple full-array scans from the size constant

**Files:** `code/display.cpp`, `code/overlay.cpp`, `code/smudge.cpp`,
`code/map.cpp`.
**Depends on:** Phase 1.

- [x] Bound both shadow scans by the square enclosing `PlayRect`, preserving `In_Radar` and row-major order.
- [x] Confirm `DisplayClass::Encroach_Fog` already uses the playfield iterator; no source change is required (`code/display.cpp:2943-2966`).
- [x] Audit the remaining scans and preserve their distinct contracts: positional overlay planes and full-domain writers remain unchanged; `Init_Cells` becomes a linear pointer-table pass; inactive `Validate` stays inactive. See corrected D4.
- [x] Compile production scan/initialization bodies into `mapcontracts` and compare order, flags, callbacks, retained cells, null slots, and shorter tables against full-domain reference behavior.
- [x] Re-measure the original full-grid and bounded scans on matched recordings. Three additional stock pairs give pooled medians of 5.104 ms and 4.560 ms, with overlapping ranges; see the live record for variability and method.
- [x] Classify the scan edits as preserved external behavior and reduced internal work. At 125×125 the two shadow loops attempt 125,000 coordinates rather than 524,288 (76.2% fewer); at 256×256 they still attempt 524,288. This is a loop-count result, not measured runtime speed.

### Phase 3: Fix the latent defects

**Files:** `code/map.cpp`, `code/coord.h`, `code/combat.cpp`, `code/sun.h`, `code/cell.cpp`, and legacy INI readers in `code/aircraft.cpp`, `code/building.cpp`, `code/unit.cpp`.

Live validation additionally required the preview guards in `code/preview.cpp` and iterator regression coverage in `tests/mapcontracts/`.
**Depends on:** Phase 2.

- [x] Replace `MAP_CELL_TOTAL/16` in `MapClass::Read_Binary_1` with an explicit named constant for the version 1 format's fixed 128 by 128 grid, so the legacy reader stops tracking the engine's cell array. Keep the `% 128` / `/ 128` decode, which is correct for that format.
- [x] Choose one row-stride name and convert every indexing and index-decomposition site in `map.cpp` and `astar.cpp` to it, including the mixed expression at `code/map.cpp:8698`.
- [x] Make the equality of the two dimension constants an enforced compile-time invariant so a future non-square pair cannot compile silently.
- [x] Replace the three live legacy INI callers with explicit 128-column decoding, then remove `Cell(int)`; full builds verify that no active integer caller remains.
- [x] Correct the `> MAP_CELL_W` / `> MAP_CELL_H` off-by-one bounds test in `code/combat.cpp:732-735`.
- [x] Keep these mechanical corrections in their own change, separate from the size change that follows.
- [x] Fix the live-discovered iterator termination error: preserve the last valid return and stop before forming an out-of-table next pointer. Add an actual production-body iterator regression.
- [x] Fix the live-discovered preview divide-by-zero: reject missing packs/nonpositive dimensions and skip empty source/destination rectangles in preview drawing. Add production-body regression checks and public change records.

### Phase 4: Raise the ceiling

**Files:** `code/sun.h`, plus any site the earlier phases prove still assumes
the old value.
**Depends on:** Phase 3.

- [ ] Raise `MAP_CELL_W` and `MAP_CELL_H` together to 1024, lifting the ceiling to `W + H <= 1024` and a maximum square map of 512x512.
- [ ] Confirm the pointer table grows from about 1 MB to about 4 MB and that live `CellClass` allocation still tracks the authored map rather than the constant.
- [ ] Re-measure the transient `IsoMapPack5` decode allocation and confirm the larger buffer is acceptable in a 32-bit process; bound it by the scenario's declared size rather than the constant if it is not.
- [ ] Measure the `HouseClass::Regions` growth per house and the resulting save-size increase, and reduce the region array to the map's actual extent if the cost is not acceptable.
- [ ] Verify a stock-size map, a 256x256 map, and a map beyond the old ceiling all load, path, save, and reload correctly.
- [ ] Verify a multiplayer session stays in sync across the change, given that events are coordinate-addressed and packet layout should be unaffected.
- [ ] Confirm every shipped map still loads unchanged, including any using terrain format 1.

### Phase 5: Tooling and authoring

**Files:** documentation only in this repository; any editor work is external.
**Depends on:** Phase 4.

- [ ] Determine the bundled FinalSun's true authoring ceiling by creating maps at 256x256 and beyond, and record what it accepts.
- [ ] Document the authoring path for maps the editor will not create, given that terrain formats 2 through 5 are coordinate-addressed and a map file is INI text.
- [ ] State plainly which sizes are engine-supported and which are also editor-authorable; they are not the same set.

### Phase 6: Documentation, validation, and commit

**Files:** `docs/`, `manual/`, `plans/map-size-changes.md`.
**Depends on:** Phase 5.

- [ ] Update the owning manual pages for `[Map] Size` and `LocalSize` with the supported ceiling, expressed as the `W + H` relation rather than as a dimension pair.
- [ ] Add a player-visible intentional-change record for the raised ceiling.
- [ ] Run `python manual/tools/manage.py check`.
- [ ] Run `cmake -S . -B build -G "Visual Studio 17 2022" -A Win32`, then build `--config Debug` and `--config Release`, then run the CTest suite.
- [ ] Report exact commands, configurations, results, and material checks not run. A build result is not runtime evidence.
- [ ] Commit with an imperative subject of at most 72 characters.

## September 5 combined implementation and validation

This section records the earlier background-only pass. [Live acceptance](#september-5-live-acceptance) below supersedes its outstanding runtime checks and restoration status.

Evidence root: ignored `baseline/cross-plan-2026-09-05/`. The work stays on `fps-fix/phase-1` at `1e75984`; the corrected plan/preparation record was obtained with `git show map-size/phase-1:plans/map-size-changes.md`, from verified head `f1c9e29`. Existing dirty FPS work was copied and hash-checked, never overwritten or switched away from. No phase branch or commit was created. Phases 1–3's code is combined in one checkout, but the milestone is not accepted until the runtime gates below pass.

### Corrections and compatibility evidence

The Phase 2 plan originally grouped incompatible scans together. D4 now records the actual contracts: bound the two shroud scans; linearize initialization without dropping retained allocations; preserve full positional overlay streams and full-domain tag/smudge writers; leave inactive `Validate` and already-iterated `Encroach_Fog` alone. The original shroud predicate and row-major order remain intact. No gameplay field, iterator state, map constant, region array, or persistence layout was added or resized.

Phase 3 uses the fixed `128 * 128` legacy terrain count, `MAP_CELL_W` as row stride, and a compile-time square-dimension assertion. The full build exposed three live legacy integer-constructor callers overlooked by the plan; their explicit `% 128` / `/ 128` replacements preserve the `NewINIFormat < 4` input conventions. The persistence audit additionally corrected the hardcoded `<< 9` in `CellClass::Post_Load`. `MapClass::Serialize` and the individual cell fields are unchanged, and the post-load slot calculation is numerically identical at 512. `astar.cpp` already consistently uses `MAP_CELL_W` and needed no edit. The wide-area-damage guard now rejects coordinates equal to either dimension; the existing subsequent `In_Radar` test already excludes these for valid supported maps, so no valid-map damage outcome changes.

The existing manual terrain-format, size, and smoothing pages remain accurate: there is no size increase, format change, newly persisted option, or new dependent-visual behavior. Internal scan and test-workflow documentation is updated here and in `docs/BUILDING.md`; `docs/TESTING.md` corrects the unsafe historical claim about minimized replay execution. No new player-visible change record is needed for these behavior-preserving map corrections.

### Fresh fixture validation

`python baseline/cross-plan-2026-09-05/validate_fixtures.py` passed without rewriting the original fixtures. It verifies ordered, unique terrain cells, the full expected diamond, flat tile/height data, terminators, all four waypoints, strict Base64 decoding, and exact compressed bytes after round-tripping through the retained engine LZO helper. Results are in `fixture-validation.json`; original hashes still match the preparation record.

| Synthetic workload | Cells | Uncompressed terrain bytes | Base64-decoded LZO stream bytes | Codec blocks |
|---|---:|---:|---:|---:|
| Flat 125×125 | 31,125 | 342,379 | 126,013 | 42 |
| Flat 256×256 | 130,816 | 1,438,980 | 528,934 | 176 |

These figures are fixture/codec results, not observed engine allocations or loading performance. The unchanged source allocation formula still yields 5,784,965 bytes for the `IsoMapPack5` staging vector at the current ceiling. That vector holds the Base64-decoded compressed LZO stream, not the decompressed terrain records. The historical stock Grand Canyon workload remains separate from both synthetic flat maps.

### Commands and results

Commands ran from the repository root in PowerShell with installed CMake at `C:\Program Files\CMake\bin`. The configured target remains Visual Studio 2022 Win32. `Get-Process Game,GameD -ErrorAction SilentlyContinue` was checked before builds; neither game process was active. Build logs retain inherited C5055/C4805 warnings, including an unchanged expression in `unit.cpp`; no warning was introduced by the new map or test code.

| Command | Result and evidence |
|---|---|
| `& 'C:\Program Files\CMake\bin\cmake.exe' -S . -B build -G 'Visual Studio 17 2022' -A Win32` | PASS, `configure.log` |
| `& 'C:\Program Files\CMake\bin\cmake.exe' --build build --config Debug --target MapContractsTest RenderClockTest -- /m /nodeReuse:false` | PASS, `focused-debug-build.log` |
| `& 'C:\Program Files\CMake\bin\ctest.exe' --test-dir build -C Debug -R '^(mapcontracts\|renderclock)$' --output-on-failure` | Both focused entries passed before full engine compilation |
| `& 'C:\Program Files\CMake\bin\cmake.exe' --build build --config Debug -- /m /nodeReuse:false` | Initial failure exposed live `Cell(int)` callers; corrected, final PASS in `build-debug-final.log`; earlier failure logs retained |
| `& 'C:\Program Files\CMake\bin\cmake.exe' --build build --config Release -- /m /nodeReuse:false` | PASS, `build-release.log` |
| `& 'C:\Program Files\CMake\bin\cmake.exe' --build build --config <Debug\|Release> --target MapContractsTest -- /m /nodeReuse:false` | PASS after strengthening the all-cells-marked case, `mapcontracts-*-final-build.log` |
| `& 'C:\Program Files\CMake\bin\ctest.exe' --test-dir build -C Debug --output-on-failure` | Final 3/3 PASS, `ctest-debug-idle.log` |
| `& 'C:\Program Files\CMake\bin\ctest.exe' --test-dir build -C Release --output-on-failure` | 3/3 PASS, `ctest-release.log` |
| `python manual/tools/manage.py check` | PASS, complete gate 157.7 seconds, `manual-check.log` |
| `git diff --check` | PASS |

The first full Debug CTest run overlapped Release compilation and failed only `logstress`'s uncontended latency thresholds (mean 33.92 us; p99.9 1663.50 us). Its throughput, contended latency, and all 100,000-message integrity checks passed. The final Debug suite ran after the compiler and manual gate were finished and passed all entries without changing logger code or relaxing thresholds. Release also passed. This is evidence consistent with resource contention, not proof that timing gates are insensitive to host load.

`mapcontracts` compiles the actual production function bodies with substitute cells, an already-decoded stream, and redraw callbacks. Twelve map shapes are tested twice: mixed flags and every cell marked. The latter compares the complete accepted cell set and row-major order against the old full-grid traversal. Initialization tests include null slots, allocated cells outside the current playfield, and a short table. The legacy-reader test verifies all 16,384 coordinates and the exact 65,536-byte consumption across its three planes, leaving a trailing sentinel unread. These tests do not execute real LCW decompression, full shroud recursion, or game save/load; the separate fixture helper exercises LZO. The clock test now explicitly demonstrates the known cadence-jitter limitation.

### Remaining acceptance gates at the end of the background pass

- Phase 1: open/create 256×256 in the locally installed FinalSun and record acceptance or refusal; load both synthetic maps in the engine, check terrain/spawns/camera/radar/pathfinding, and retain separate local recordings. The editor exists, but current desktop availability was not confirmed, so it was not launched.
- Phase 1: measure `Read_Scenario_INI` entry through `Start_Scenario` return, peak working set/private commit, actual decode allocation/stream bytes, and shroud costs on each flat map. Retain stock Grand Canyon as a separate workload. The historical instrumentation was reverted before its original commit; reapply temporary measurements locally with the same overhead for before/after comparisons. No current 256×256 runtime measurement is claimed.
- Phase 2: measure the original full-grid versus bounded shroud routine in matched builds on stock Grand Canyon and the controlled maps. The old full-grid implementation remains recoverable through `git show HEAD:code/display.cpp`; do not switch away from or replace the combined dirty checkout.
- Combined validation: run all four Debug golden recordings with smoothing enabled and disabled against the combined binary. Run the targeted ramp/bridge/cliff occlusion checks and combined save/load/pathfinding checks. Existing FPS-only golden and lifecycle evidence predates these map changes.
- FPS LAN: use two compatible clients for more than 2000 frames without a desync. The local instance mutex prevents an ordinary second local game instance; no second client was supplied. Replay checks cannot replace LAN or visual evidence.

The baseline helpers were inspected. The original `tools/baseline/Invoke-VanillaBaseline.ps1` deletes sync files and was not used. The retained `baseline/fps-runtime-2026-09-05/Invoke-PreservedBaseline.ps1` archives them but still requires outer byte-preserving backup/restoration of settings, recording, saves, and sync files. No runtime helper was executed in this pass. Preserve and hash all originals before any later run, archive generated artifacts instead of deleting them, restore originals in `finally`, and verify their hashes afterward. Build/stage only while the game is closed.

`runtime-restoration.json` confirms unchanged hashes for `SUN.INI`, `KEYBOARD.INI`, `RECORD.BIN`, and `SYNC0.TXT`; there was no save in `Run/` at the start. Backups are under `original-run/`. No restoration write was necessary because no runtime session changed these files. Build outputs were refreshed and hashed in `binary-hashes.json`; `Language.dll` is from Release. Prior archived saves, recordings, goldens, and unrelated `assets/` remain untouched. No commit, push, PR, branch switch, or branch deletion occurred.

## September 5 live acceptance

The user's “go ahead” supplied desktop access. All available work for the first cross-plan milestone is now complete. This record supersedes the preceding background-only status; it does not retroactively turn historical FPS-only runs into combined-build evidence. Local evidence is under ignored `baseline/cross-plan-live-2026-09-05/`.

### Defects found and resolved

Selecting the prepared previewless map initially raised `EXCEPTION_INT_DIVIDE_BY_ZERO` in `MapPreviewClass::Blit_Preview`. `Read_INI` had constructed a zero-size surface from the missing preview metadata. The reader now clears the old pointer and returns false for missing packs or nonpositive dimensions; drawing also checks both rectangles before division. The crash report is archived in `preview-crash/`, and the same previewless map subsequently selected normally. Valid stock previews still render. No new preview format was introduced.

The first 256×256 load then hit the checked vector subscript in `MapClass::Iterate`. A disposable assertion-to-stderr setting let the existing abort handler capture the stack: `Iterate → Overpass → Fresh_Map → Set_Map_Dimensions`. After the final cell, the iterator formed the next row's pointer beyond the table. It now stores a null terminal pointer when the computed slot is outside `Array.Length()`, and an exhausted iterator returns null. The traversal and cell order before termination are unchanged. `load-assert-crash/` retains the diagnosis. Interrupted/ignored-assert diagnostic attempts are excluded from all accepted measurements and checks.

The baseline table also confused the coordinate bounding-box corner with an allocated cell. The corrected table above records actual highest indices: the 256×256 map reaches row/column 511 but its highest allocated index is 261,889 at (257,511), not the table's final slot. The ceiling relation remains unchanged.

`tests/mapcontracts/` now exercises the actual iterator through the ceiling and the preview reader/drawing guards, alongside its prior scan, initialization, and legacy-plane checks. `manual/changes/map-boundary-loading.md`, `manual/changes/optional-map-preview.md`, and the owning size/scenario-format pages document these player-visible fixes. Downstream C++ callers of the removed scalar `Cell(int)` constructor must explicitly decode an index with the intended stride: 128 for the legacy INI format, `MAP_CELL_W` for the current engine table. Binary cell coordinates and serialized layouts remain unchanged.

### Editor and map baseline

A local copy of FinalSun 2.0 opened the original synthetic 256×256 map and displayed both dimensions correctly. Its new-map wizard also created and saved a 256×256 map after the advisory warning that width plus height exceeds 256. `editor-authored-256.mpr` has `[Map] Size=0,0,256,256`; its `[Preview]` dimensions are a separate image size. Screenshots and the editor log are retained. No larger editor size was tested, and no broader editor-ceiling claim is made. The retail installation was read-only throughout.

Fresh Debug skirmishes on both unmodified synthetic fixtures used GDI, one AI, unit count 10, credits 10,000, and speed 3. They were recorded as `flat125.bin` and `flat256.bin`; the flat-map geometry and settings are controlled, while their separately recorded seeds/rosters and process noise are not claimed to be identical. Each full/bounded timing pair uses the exact same recording. Stock Grand Canyon uses its existing golden and remains a separate terrain/unit workload.

The temporary measurement build invoked shroud regrowth at frames 60, 120, 180, 240, and 300, because stock rules disable it. It sampled memory separately and logged load entry/return and the actual decode allocation. `OPENTS_MAP_FULL_SCAN` selected the original 512×512 loops; both variants included the necessary iterator fix, so this isolates scan bounds rather than comparing against the crashing original 256×256 loader. The same instrumentation was present in each variant.

| Workload | Scan | Load time | Peak working set at frame 300 | Peak commit at frame 300 | Median shroud call |
|---|---|---:|---:|---:|---:|
| Flat 125×125 | Full | 317 ms | 290,869,248 B | 234,717,184 B | 5.643 ms |
| Flat 125×125 | Bounded | 339 ms | 291,258,368 B | 235,094,016 B | 4.756 ms |
| Flat 256×256 | Full | 1,067 ms | 339,906,560 B | 284,286,976 B | 13.406 ms |
| Flat 256×256 | Bounded | 1,116 ms | 339,595,264 B | 284,008,448 B | 11.863 ms |
| Stock Grand Canyon | Full | 533 ms | 314,003,456 B | 240,492,544 B | 3.979 ms |
| Stock Grand Canyon | Bounded | 531 ms | 297,062,400 B | 240,775,168 B | 4.313 ms |

Every run observed `sizeof(CellClass)=164` and a 5,784,965-byte decode vector. Flat 125×125 allocated 31,125 cells and decoded a 126,013-byte LZO stream; flat 256×256 allocated 130,816 cells and decoded 528,934 bytes; Grand Canyon decoded 168,517 bytes. These compressed stream sizes are distinct from uncompressed terrain sizes. Results are in `measurements/analysis.json`. All three full/bounded sync pairs matched; both stock runs matched the existing golden.

The initial five-sample stock timings did not demonstrate an improvement. Three quieter repetitions with alternating variant order produced 15 calls per variant: full median 5.104 ms, mean 5.360 ms, range 3.269–6.857 ms; bounded median 4.560 ms, mean 4.374 ms, range 2.667–6.375 ms. All six repeated runs matched the stock golden. The pooled median reduction is 10.7%, with substantial overlap and one near-tied pair; this is a modest measured benefit, not a stable latency guarantee. At 256×256 both variants have identical loop limits, so their timing difference is run variability, not an optimization gain. `stock-repeat-*/` and `validation-analysis.json` own the samples.

### Combined runtime checks

The 256×256 live skirmish accepted a vehicle move, saved `SAVE0029.SAV`, and reloaded successfully in Debug. The final Release binary also loaded that save and deployed the construction vehicle successfully. Its post-load captures and `final-release-runtime.log` are retained. The save is archived as `flat256.SAV`; no pre-existing save was overwritten.

A disposable QA driver placed a stock buggy on actual Grand Canyon terrain and issued fixed movement orders. Paired smoothing-on/off runs traversed ramp (121,125)→(117,121), descending from 416 to 0 leptons; bridge (118,116)→(114,116), staying on the deck at 416 leptons; and a separate cliff-edge route (121,126)→(116,130). The inspected captures showed no new body/terrain occlusion defect in these cases. Their full sync dumps match between toggle values, all disabled runs have zero maximum render offset, and all report zero threshold snaps. This is targeted screenshot/path evidence, not a proof of every terrain transition or uniformly smooth wall-clock motion.

The 256×256 QA pass rendered each of the four waypoint areas, placed the additional waypoint markers, and drove a scout from (190,192) to (322,320), reaching and holding the destination by frame 2000 in both modes. The final controller reapplied the long move so inherited recording commands could not redirect it; combat was inert in this isolated path test so the nearby AI could not kill the scout. Both frame-2400 dumps match. Initial stationary bridge, occupied/killed scout, and redirected-path attempts are retained as rejected test setups. Accepted evidence is `terrain-qa/0-*`, `terrain-qa-refined/1-*`, `terrain-qa-refined/2-*`, and `crossmap-fixed-order/3-*`.

### Final validation and restoration

Temporary measurement, assertion, actor, and camera code was archived under `instrumented-source/` and removed. The original mixed line endings were restored from verified, text-identical backups. All 17 pre-existing FPS implementation files hash-match their pre-test copies; all required FPS instrumentation and explicit resets remain. Only the permanent map/preview fixes and tests remain in the source diff.

- `cmake --build build --config Debug -- /m /nodeReuse:false` and the Release equivalent passed using `C:\Program Files\CMake\bin\cmake.exe`; final logs are `final-debug-restored-build.log` and `final-release-build.log`.
- `ctest --test-dir build -C Debug --output-on-failure` and the Release equivalent passed all three entries; see `final-ctest-debug.log` and `final-ctest-release.log`.
- `pwsh -NoProfile -File baseline/cross-plan-live-2026-09-05/Invoke-Goldens.ps1 -Smooth yes -Label final`, then `-Smooth no -Label final`, each matched all four original frame-300 goldens after temporary code removal. Reports are `golden-final-yes/report.txt` and `golden-final-no/report.txt`.
- `python manual/tools/manage.py check` passed the complete gate after placing preview documentation with the existing scenario-format owner; see `final-manual-check-fixed.log` (146.5 seconds). The earlier rejected standalone format draft is archived rather than falsely recorded as a new engine format.
- `python baseline/cross-plan-live-2026-09-05/analyze_measurements.py` and `analyze_validation.py` validate the measurement/QA comparisons and emit the evidence JSON.

The game and editor are closed. `final-restoration.json` verifies the original hashes of `SUN.INI`, `KEYBOARD.INI`, `RECORD.BIN`, and `SYNC0.TXT`; generated runtime maps/save/output are archived under `final-runtime/`. The newly created FinalSun user-state directory was archived, restoring its initial absence. `final-fps-preservation.json`, `instrumentation-restoration.json`, `editor-restoration.json`, and `final-binary-hashes.json` record the checks. The final `Language.dll` is Release. Unrelated `assets/` and the original prepared maps remain untouched. No file/branch deletion, commit, push, PR, or later-phase implementation was performed.

Two-client LAN is the sole unavailable cross-plan acceptance check: the user explicitly confirmed no second client. Replay and QA checksum matches do not replace it. FPS cadence jitter and the previously documented dependent-visual limitations remain. The recorded next milestone is 64-bit conversion, not undertaken here; full acceptance still needs the deferred LAN evidence or an explicit decision about that gate.

## Compatibility classification

Under the repository's compatibility rules, this change touches several
boundaries. Each is classified deliberately:

- **Map data format - preserved.** Formats 2 through 5 are coordinate-addressed
  and unchanged. Format 1 is corrected to read its own fixed dimensions, which
  fixes a defect rather than changing the format.
- **Saves - unchanged by Phases 1–3.** Cell placement remains coordinate-addressed, the stride stays 512, and no serialized field or per-house array changes. A version stamp alone is not evidence of compatibility between development snapshots. Phase 4 must independently audit layout and region growth.
- **Network packets - preserved.** Events carry cell coordinates as two shorts,
  not indices.
- **Deterministic simulation - preserved.** No change alters cell semantics,
  movement cost, or ordering; the Phase 2 loop bounds must visit the same cell
  set, which is the acceptance criterion for that phase.
- **Existing maps - preserved.** Every shipped map is far below the old ceiling
  and is unaffected.

## Out of scope

- **Non-square dimension constants.** D2 shows the tree cannot support them
  without unifying the stride first, and there is no requirement for them.
- **Removing the fixed cell-array ceiling entirely.** Making the table fully
  dynamic is a larger change with no established need; raising a constant that
  costs one pointer per slot is the proportionate step.
- **Rewriting the terrain format.** The coordinate-addressed formats already
  scale.
- **Editor changes.** FinalSun is external to this repository.
