# Larger map support

## Summary

Raise the map-size ceiling in OpenTS above the value inherited from Tiberian
Sun, and fix the latent defects that a size change would otherwise expose.

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

A 256x256 map produces a maximum coordinate of (511, 511) and a maximum index
of 262,143 - exactly the last slot of the table. That exact fit identifies 512
as the designed limit rather than a coincidence.

### Measured sizes

Simulating the allocation loop against the real `In_Radar` predicate:

| Size | W+H | Cells | Max coord | Max index |
|---|---|---|---|---|
| 256 x 256 | 512 | 130,816 | (511, 511) | 262,143 |
| 400 x 112 | 512 | 89,488 | (511, 511) | 262,143 |
| 298 x 149 | 447 | 88,655 | (446, 446) | 228,502 |
| 120 x 150 | 270 | 35,850 | (269, 269) | 137,997 |
| 125 x 129 | 254 | 32,121 | (253, 253) | 129,789 |

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
  in `CellClass::Post_Load`. Saves are additionally gated to an exact engine
  version match (`code/saveload.cpp:1170`), so there is no cross-version
  migration burden.
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
Either unify on a single stride macro first, or make equality an enforced
invariant.

### D3 - A dead `Cell(int)` constructor hardcodes a 128 stride

`code/coord.h:46` declares
`explicit Cell(int cellnum) : BASECLASS(cellnum % 128, cellnum / 128)`.

The 128 matches the version 1 map format, not the engine's cell array, and it
disagrees with every other index decomposition in the tree. An audit of
single-argument `Cell(...)` constructions found no live caller - the one
candidate, `code/unit.cpp:4500`, passes a `Cell` and therefore selects the copy
constructor. It is dead code that reads like a general-purpose index decoder and
would silently produce wrong cells if anyone used it.

### D4 - Full-array scans are bounded by the constant, not the map

Several routines walk the entire `MAP_CELL_H` by `MAP_CELL_W` index space and
discard non-playfield cells with `In_Radar`, rather than walking the playfield:

- `DisplayClass::Encroach_Shadow` (`code/display.cpp:2893`) does **two** full
  scans per call - 524,288 iterations today.
- `DisplayClass::Encroach_Fog` is its fog counterpart (`code/display.cpp:2939`).
- `DisplayClass::Write_INI` (`code/display.cpp:3327`), `OverlayClass::Write_INI`
  (`code/overlay.cpp:491`, and further scans at `:404` and `:450`),
  `SmudgeClass::Write_INI` (`code/smudge.cpp:252`).
- `MapClass::Init_Cells` (`code/map.cpp:685`) and `MapClass::Validate`
  (`code/map.cpp:2609`).

`Encroach_Shadow` is the one that matters at runtime: `LogicClass` calls it on a
rules-driven timer whenever shroud regrowth is enabled (`code/logic.cpp:302`
and `:431`, gated on `Rule->IsShroudGrow` and `Rule->ShroudRate`). Its cost is
proportional to the **maximum** map size, so a 120x150 mission pays the same
price as a 256x256 one, and raising the constant would quadruple that cost for
every map regardless of size.

Bounding these by `MapRect` is worth doing on its own merits, before any size
change.

### D5 - The IsoMapPack5 decode buffer scales with the constant

`code/display.cpp:3258` sizes a transient decode buffer from `MAP_CELL_TOTAL`,
and `code/display.cpp:3264` allocates a `std::vector<unsigned char>` of the
resulting `maximum_decoded_size`. Raising the constant multiplies that transient
allocation by four inside a 32-bit process.

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

- [ ] Measure `sizeof(CellClass)` in the supported Win32 Debug and Release configurations and record both figures here.
- [ ] Compute and record total cell memory for a 256x256 map from that figure and the verified 130,816 cell count.
- [ ] Measure peak process memory, scenario load time, and `Encroach_Shadow` cost on a stock-size map and on a 256x256 map.
- [ ] Record the transient `IsoMapPack5` decode allocation actually reached at load.
- [ ] Record the results in this plan as the baseline every later phase is compared against.

### Phase 2: Decouple full-array scans from the size constant

**Files:** `code/display.cpp`, `code/overlay.cpp`, `code/smudge.cpp`,
`code/map.cpp`.
**Depends on:** Phase 1.

- [ ] Bound `DisplayClass::Encroach_Shadow`'s two scans by the playfield rectangle instead of `MAP_CELL_H` by `MAP_CELL_W`, preserving the existing `In_Radar` semantics exactly.
- [ ] Apply the same bounding to `DisplayClass::Encroach_Fog`.
- [ ] Apply the same bounding to the `Write_INI` scans in `display.cpp`, `overlay.cpp`, and `smudge.cpp`, and to `MapClass::Init_Cells` and `MapClass::Validate`.
- [ ] Confirm each converted routine visits exactly the same cell set as before; the change is a loop bound, not a behaviour change.
- [ ] Re-measure `Encroach_Shadow` against the Phase 1 baseline on a stock-size map and record the improvement.
- [ ] Classify this change as preserved external behaviour with an internal cost improvement, and state the evidence.

### Phase 3: Fix the latent defects

**Files:** `code/map.cpp`, `code/coord.h`, `code/combat.cpp`.
**Depends on:** Phase 2.

- [ ] Replace `MAP_CELL_TOTAL/16` in `MapClass::Read_Binary_1` with an explicit named constant for the version 1 format's fixed 128 by 128 grid, so the legacy reader stops tracking the engine's cell array. Keep the `% 128` / `/ 128` decode, which is correct for that format.
- [ ] Choose one row-stride name and convert every indexing and index-decomposition site in `map.cpp` and `astar.cpp` to it, including the mixed expression at `code/map.cpp:8698`.
- [ ] Make the equality of the two dimension constants an enforced compile-time invariant so a future non-square pair cannot compile silently.
- [ ] Remove the dead `explicit Cell(int cellnum)` constructor at `code/coord.h:46`, having confirmed no live caller selects it.
- [ ] Correct the `> MAP_CELL_W` / `> MAP_CELL_H` off-by-one bounds test in `code/combat.cpp:732-735`.
- [ ] Keep these mechanical corrections in their own change, separate from the size change that follows.

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

## Compatibility classification

Under the repository's compatibility rules, this change touches several
boundaries. Each is classified deliberately:

- **Map data format - preserved.** Formats 2 through 5 are coordinate-addressed
  and unchanged. Format 1 is corrected to read its own fixed dimensions, which
  fixes a defect rather than changing the format.
- **Saves - preserved in practice.** The cell array is not stored positionally,
  and saves are already gated to an exact engine version, so no migration path
  is required. Save size grows with the per-house region array unless Phase 4
  reduces it.
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
