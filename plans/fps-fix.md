# Sub-tick render interpolation for OpenTS

## Context

OpenTS already composites the tactical view hundreds of times a second and presents at
the display's refresh rate, but every frame between two simulation ticks is identical.
World state only advances in `LogicClass::AI()`, once per `Main_Loop()`, paced by
`FrameTimer = Options.GameSpeed` sixtieths of a second (`code/mainloop.cpp:296`) — about
20 Hz at the default `Options.GameSpeed == 3` (`code/options.cpp:112`).

Raising the simulation rate is not the fix: every gameplay duration is counted in
simulation frames, not seconds (`ROF` read raw at `code/techtype.cpp:178`; superweapon
`RechargeTime` as `recharge * TICKS_PER_MINUTE` frames at `code/suprtype.cpp:231`), so
lowering `GameSpeed` speeds the whole game up — `manual/content/keys/gamespeed.md:12`
says so outright, and `Options.GameSpeed = 0` already gives an uncapped, unplayable
simulation today.

So the simulation is left alone and each rendered frame is made to show a different
moment inside the current tick: moving objects are drawn at a position interpolated
between where they stood at the start of the tick and where they stand now. Nothing in
the simulation is read differently or written at all.

## What this will and will not visibly change

**This section is the most important part of the plan.** It was established by
measurement against the source after the first draft, and it materially narrows the
expected result.

`Tactical::Rectangular_To_Isometric` (`code/tactical.cpp:189-197`) with
`ISO_TILE_PIXEL_W = 48` and `ISO_TILE_PIXEL_H = 24` (`code/globals.cpp:87-88`), followed
by the `/ CELL_LEPTON` divide in `Coord_To_Pixel_Absolute` (`code/tactical.cpp:207-214`),
gives:

| Axis | Leptons per screen pixel |
|---|---|
| X | `256 / 24` ≈ **10.7** |
| Y | `256 / 12` ≈ **21.3** |

Ground vehicles move only a few leptons per tick — `DriveLocomotionClass` spends
accumulated speed in units of 7 leptons, which `code/drive.cpp:1326` documents as
`CELL_LEPTON / ((CELL_PIXEL_W + CELL_PIXEL_H) / 2)`, i.e. **7 leptons ≈ 1 average screen
pixel**. A vehicle therefore moves roughly **0.5–1.5 pixels per simulation tick**.

The composite surface is integer-pixel and shapes blit at integer positions, so:

- **Ordinary ground units will look close to unchanged.** Their sub-tick offsets round
  to zero pixels on most frames. Interpolation still buys a little: the drive locomotor
  spends `SpeedAccum` in whole 7-lepton chunks (`code/drive.cpp:1326`), so per-tick
  travel jitters between chunk counts, and interpolation converts that jitter into
  uniformly timed pixel steps. The 1-pixel quantisation itself is a hard ceiling of the
  2D integer-pixel renderer, not a weakness of the technique; the genuine fix is more
  physical pixels per lepton — see *Relationship to the HD image-upgrades plan*.
- **Aircraft, projectiles, jumpjets, drop pods and tunnel movers will be visibly
  smoother.** These move many leptons per tick and will gain several distinct sub-tick
  positions per tick.
- **Most particles come along for free.** `ParticleClass` derives from `ObjectClass` and
  is submitted to the display layers (`code/particle.h:25,41`), and its artwork branch
  draws at the pixel handed down from the Phase 3 funnel (`code/particle.cpp:662-682`),
  so drifting smoke, gas, fire and debris interpolate without extra work. Only the spark
  and railgun branch plots its own pixel — see Phase 4.
- **Sprite animation stepping is not addressed here.** Walk cycles, explosions, muzzle
  flashes and building animations advance by whole artwork frames on the simulation tick.
  They are discrete artwork and cannot be interpolated without more frames; the delivery
  path for those frames is the HD image-upgrades plan — see *Relationship to the HD
  image-upgrades plan*. If the perceived "20 fps" feel comes mostly from animation
  cadence rather than object travel, this change alone will not move it.

Phase 0 exists to measure the real per-tick pixel deltas in a live game **before** the
rest is built, so the decision to continue is made on data rather than on this estimate.

**Accepted trade-off:** interpolating between the previous and current tick means the
picture trails the simulation by up to one tick (50 ms at 20 Hz, ~25 ms average).
Extrapolating forward instead would remove the lag but overshoot on every direction
change.

## Not in scope

- Changing the simulation tick rate, `Options.GameSpeed`, `Frame`'s cadence, or any
  `rules.ini` timing.
- Interpolating animation artwork frames.
- Sub-pixel rendering (would require a renderer change; `Tactical::ZoomFactor` magnifies
  *after* composition, so zooming makes quantisation more visible, not less).
- Render-loop pacing and time-basing of edge scroll / screen shake. Both were in the
  first draft and were cut — see *Cut after review*.
- Rotation smoothing. A turning voxel vehicle snaps between 32 drawn facings because
  `As_Radian32()` rounds to `Dir32` (`code/face.h:204`) and the voxel rasterization
  cache is keyed the same way (`code/loco.cpp:88-91`); SHP facings are artwork-limited.
  Both need the image-upgrades asset architecture (more facings, finer voxel cache
  keys), not interpolation — see *Relationship to the HD image-upgrades plan*.

## Relationship to the HD image-upgrades plan

`plans/image-upgrades.md` is the delivery vehicle for everything this plan's ceiling
excludes, and the two designs are built to compose. Sequencing: this plan lands first —
it is small, render-only, and its render clock and previous-state sweep are
infrastructure the HD work reuses.

- **Ground units.** The 1-pixel quantisation ceiling falls when the physical raster gets
  finer, which is image-upgrades Phase 3 (logical-to-physical render scale). At scale 2
  the X axis drops to ~5.3 leptons per physical pixel, so the same 7-lepton drive step
  becomes ~1.3 physical pixels per tick and the interpolated offsets finally have room
  to land between logical pixels — even with entirely classic art. **Contract:** this
  only works if the render context converts leptons to physical pixels in a single
  divide (the scale multiplies *before* the `/ CELL_LEPTON` division), never by scaling
  an already-truncated logical pixel. **D3**'s decision to keep the offset in lepton
  space and apply it before `Coord_To_Pixel` is what makes the composition possible;
  image-upgrades Phase 3 records the matching API requirement.
- **Sprite animations.** Animation stage is simulation state — `StageClass` serializes
  `Stage`/`Timer`/`Rate` (`code/stage.h:80-87`), infantry `Doing` is folded into the CRC
  (`code/infantry.cpp:4112`), and gameplay reads the stage directly (the firing-frame
  check at `code/infantry.cpp:3594`) — so it can never advance faster than the tick.
  Smoother animation therefore means more artwork frames selected draw-side:
  image-upgrades Phase 4's manifest carries K-fold temporal variants, and the renderer
  picks the sub-frame from two cleanly separated inputs. `StageClass` supplies only
  what it owns — **directed intra-stage progress**: the elapsed fraction of the
  countdown's *own programmed interval*, signed by `Step`, zero when `Step == 0` or the
  stage holds. Not the fraction of `Rate`: `Adjust_Rate` and `Just_Set_Rate` change
  `Rate` mid-interval without resetting the countdown (`code/stage.h:75-76`), and
  callers lean on exactly that to keep progress across a rate change
  (`code/factory.cpp:790`), so `(Rate - Timer)/Rate` can regress or leave `[0,1]`
  after one. The countdown owns its programmed interval as `DelayTime`
  (`code/timer.h:536-549`); the accessor — new, render-only, on `code/stage.h` since
  `Timer` is private — reads programmed and remaining from the countdown itself and is
  combined with this plan's `Fetch_Render_Alpha()` for the intra-tick fraction. Sequence topology deliberately stays out of the accessor, because
  `StageClass` does not own it: loop bounds and ping-pong reversal live in the
  animation owner (`code/anim.cpp:184-187,909-914`), buildings window their stages
  through `AnimControlType` `Start`/`Count`, and infantry swaps whole sequences at
  `DoControls[Doing].Count` boundaries (`code/infantry.cpp:3636-3680`). The selection
  layer therefore **never predicts the next logical frame**: it maps (current logical
  frame, direction, progress) through the manifest and holds the terminal sub-frame at
  an interval boundary until the simulation flips the stage. Direction is not free,
  though: a forward sample block for frame F in-betweens F toward F+1, and playing it
  backward morphs toward the wrong neighbour, so a sequence that declares reverse or
  ping-pong playback must author direction-specific sub-frame blocks — the manifest
  refuses a temporal variant for such a sequence without its reverse blocks. Every input is read-only
  simulation state plus the render clock, so the selection is determinism-safe by
  construction. One coupling is
  intentional and worth stating: with `SmoothMotion` off, alpha pins at 256 and
  sub-frame selection degrades to tick-rate stepping — disabling smooth motion also
  freezes temporal upscaling's intra-tick phase. The in-between frames themselves are
  an asset-production cost (image-upgrades Phase 7).
- **Rotation.** The SHP facing limit and the 32-bucket voxel rotation (*Not in scope*)
  are also asset-architecture problems: more facings ride the same Phase 4 manifest, and
  finer voxel rotation belongs to image-upgrades Phase 6 — where it must change the
  *transform quantization and every cache-key layout together* (`As_Radian32` rounds
  the drawn matrix itself, `code/face.h:204`; the key packs five facing bits,
  `code/unit.cpp:2585-2589`, with matching body and shadow layouts at
  `code/loco.cpp:88-91,111-113`), or a re-keyed cache would rasterize the same 32
  orientations.

Phase 0's decision gate should be read with this in mind: if ordinary vehicles measure
under ~1.5 px/tick, the immediate win is air, projectiles and particles, and the ground
unit and animation win arrives when the image-upgrades phases land on top of the clock
and sweep built here.

## Standing constraints

1. **`Render_Coord()` must not become interpolated.** `ObjectClass::Sort_Y()`
   (`code/object.cpp:2732`) returns `Render_Coord().Y` and feeds the ground-layer sort
   that `code/mainloop.cpp:330-335` performs explicitly "for the purposes of game
   sync'ing between machines"; `code/techno.cpp:644,683` use it for fire and anim-attach
   coordinates. Interpolation is injected at draw sites only.
2. **Synchronized RNG discipline.** `Scen->RandomNumber` (via `Random_Pick`,
   `Percent_Chance`, `Probability_Of`, `Random_Double` in `code/ccrand.h`) is folded into
   the per-frame network CRC (`code/queue.cpp:4281`) and serialized. New render-side code
   uses `Sim_Random_Pick`/`Sim_Percent_Chance` or no RNG at all.
   `manual/changes/anim-loop-delay-sync.md` records a real desync of exactly this shape.
3. **No automated coverage exists.** `tests/` holds only a debug-logger stress test
   (`tests/logstress/`). Every phase is verified by building and playing.

---

## Design decisions

### D1 — Previous position is captured by a sweep at the tick boundary, not in a setter

The first draft hooked `ObjectClass::Set_Coord()` (`code/object.cpp:2558`) as "the single
write point for every object's position". **That claim is false.**
`ObjectClass::Set_Height_AGL` (`code/object.cpp:2136,2139`) and `ObjectClass::Set_Height`
(`code/object.cpp:2157,2160`) write `Position.Z` directly, bypassing `Set_Coord`, and the
flight, hover, levitate and drop-pod locomotors drive them every tick
(`code/fly.cpp:378,520,621,686`, `code/hover.cpp:120`, `code/levitate.cpp:132`,
`code/droppod.cpp:116,130`, `code/aircraft.cpp:257,440,448,458`). A setter hook would
silently skip altitude changes — i.e. exactly the aircraft and jumpjets that are the only
objects this change visibly helps.

A repo-wide search for `Position =` does **not** find these: they are written as
`Position.Z = ...`. Two independent reviews reached the false "single writer" conclusion
from exactly that search. Do not re-derive it; the direct reads above are the evidence.

Instead, sweep `DisplayClass::Layer[]` once per tick and copy `Position` into
`RenderPrevious`. This cannot miss a writer, needs no "first write of the tick"
bookkeeping, removes the frame-tag sentinel entirely, and is O(objects) once per tick.

State lives as one non-serialized member on `ObjectClass`, beside `Position`:

```cpp
/*
 * Where the object stood before the tick that moved it. It is not saved and not
 * checksummed: it exists only so a rendered frame can draw the object part way between
 * two simulation positions.
 */
Coord RenderPrevious;
```

`docs/DIRECTION.md:10-16` prefers composition, and a side table keyed by object would
honour that more literally. It is rejected because heap slots are reused — `ObjectClass`
destruction runs through `Process_Deferred_Deletion()` (`code/mainloop.cpp:453`), so a
stale entry can be matched against a *new* object at the same address and streak it — and
because the value is read once per object per rendered frame. The member is plain data
with no behaviour, read through one accessor, so lifting it into a component later
touches the interpolation module and `object.cpp` only. **Record this as a deliberate
trade in the commit.**

Save format is unaffected: `ObjectClass::Serialize` (`code/object.cpp:2050-2071`) names
members individually, and `code/savestream.h:117-126` is field-by-field with no raw-image
object load. Network determinism is unaffected: `Compute_Game_CRC`
(`code/queue.cpp:4263-4281`) reads `PositionCoord` only.

### D2 — Relocations invalidate explicitly where that is semantic; a per-tick lepton threshold backstops the rest

The first draft derived a teleport threshold from "the largest legitimate per-tick
displacement" and the idea was cut when the repository offered no data for it. The
evidence situation has changed twice: `AGENTS.md` (*Local game files*) now records a
retail Tiberian Sun install on this machine as read-only evidence, and Phase 0 now
*measures* per-family per-tick displacement in live retail play — which is the number
that actually matters. Raw `rules.ini` `Speed=` values do not bound it: runtime speed
multiplies in the house ground-speed bias, `SpeedBias`, veterancy and throttle
(`code/foot.cpp:3407-3418`), ballistic weapons overwrite `MaxSpeed` from range and
gravity after the rules load (`code/weapon.cpp:242-251`), and particles move on their
own velocity fields. The threshold is therefore derived from the Phase 0 measured
maxima, with the INI values kept only as a sanity cross-check; it remains a documented
heuristic, never a correctness bound, because mods change all of these numbers.

- Add `void ObjectClass::Invalidate_Render_Interpolation(void)` which sets
  `RenderPrevious = Position`. Call it where non-interpolation is *semantic*: object
  entry (`ObjectClass::Unlimbo`, `code/object.cpp:1393`) and the chronoshift teleport
  (`code/teleport.cpp:105-112`), whose displacement can be arbitrarily short and must
  still pop rather than glide. The first draft's wider hook list — `Limbo`, drop-pod
  (`code/droppod.cpp:203-206`), tunnel arrival (`code/tunnel.cpp:358-360`),
  reinforcement (`code/reinf.cpp:611`) — is dropped on review: those paths either enter
  through `Unlimbo` or move continuously, and anything larger the threshold below
  catches. The hooks' residual delta beyond the threshold is the short-chronoshift case
  alone, so only that hook stays.
- The backstop is evaluated **once per tick, in lepton space**, immediately after
  `Logic.AI()`: walk the layers and set `RenderPrevious = Position` for any object
  whose full tick delta exceeds `RENDER_INTERP_SNAP_LEPTONS`, a named constant chosen
  with comfortable margin (at least 4x) above the largest per-tick **lepton** delta
  Phase 0 measured across every family and calibration case — the lepton maxima, never
  the pixel maxima, and by the same metric the walk tests (largest absolute component
  of the lepton delta). The decision is made from the full delta and holds for
  the whole tick by construction — `RenderPrevious` itself carries it — so it cannot
  flip mid-tick. A modded mover faster than the constant degrades to exactly today's
  unsmoothed drawing, and nothing else. A false snap is silent by construction — a
  snapped offset reads zero — so Phase 2's log makes it observable: pre-snap delta
  magnitudes and a per-family snap count.

Two backstop designs were rejected on review. A per-frame **pixel** test against
`Get_Render_Rect()` fails twice over: the alpha-scaled offset shrinks within the tick,
so a large transition snaps on early frames and then *un*-snaps and jumps backward once
the remaining offset drops under the threshold; and `Get_Render_Rect()` returns the
zero-sized `RECT_NONE` for classes with no SHP artwork (`code/object.cpp:1130-1140`,
`code/rect.h:309`) — voxel projectiles among them — which would silently disable
interpolation for exactly the fast movers this plan is for. (It is also non-const,
`code/object.h:301`, while every draw-path caller is const.) And clamping instead of
snapping still draws an unaudited teleport a body-width from its true position for a
tick.

### D3 — Interpolate in lepton space, apply as a coordinate offset

Leptons are 10–21× finer than pixels, so integer division by a 0–256 fixed-point alpha
costs at most one lepton (≈0.05–0.09 px). Interpolating already-truncated pixel positions
would discard that precision first. More decisively, a single lepton offset keeps body,
shadow, selection box and depth adjust locked together for free, because all are derived
from `Position`; pixel-space lerps would have to be re-derived at each site and would
drift apart.

```cpp
Coord ObjectClass::Fetch_Render_Offset(void) const
{
    int back = 256 - Fetch_Render_Alpha();
    if (back == 0) return(Coord(0, 0, 0));

    Coord delta = Position - RenderPrevious;
    return(Coord(-(delta.X * back) / 256, -(delta.Y * back) / 256, -(delta.Z * back) / 256));
}
```

Z **is** interpolated, because D1's sweep captures it correctly and altitude change is
the main source of visible motion for aircraft and jumpjets. The lift plumbing is
asymmetric, and Phase 3 must respect it. `Height` is a property over `Position.Z`
(`code/object.h:369-372`, `code/object.cpp:2093-2098`), and the funnel's
`Coord_To_Pixel` already subtracts `Z_Lepton_To_Pixel(coord.Z)`
(`code/tactical.cpp:308-311` via `code/tactical.cpp:207-214`). A voxel aircraft body
draws at the funnel pixel directly (`code/aircraft.cpp:478`), so its altitude
interpolates correctly through the Phase 3 offset with no further work. The SHP path in
`TechnoClass::Techno_Draw_Object` additionally shifts `drawpoint.Y` by
`Z_Lepton_To_Pixel(Height)` for `RTTI_AIRCRAFT` (`code/techno.cpp:5951-5953`) — a
second subtraction from the same `Position.Z` on top of the funnel's. Which subtraction
carries the visible lift for an SHP-drawn aircraft is not established here; Phase 3
establishes it in a live game and routes the interpolated Z through exactly one of
them, zeroing the pixel contribution of `offset.Z` on the other.

### D4 — Attached animations must resolve through their host

`AnimClass::Center_Coord()` (`code/anim.cpp:444-450`) returns
`xObject->Center_Coord() + BASECLASS::Center_Coord()` when attached: the anim's own
`Position` is a static relative offset that never changes while the host moves. Without
handling, burn/damage smoke and muzzle effects would draw at the host's true position
while the host draws interpolated — visibly worse than today. `Fetch_Render_Offset` is
therefore virtual, with an `AnimClass` override that defers to `xObject` when attached,
mirroring the existing `Center_Coord` override exactly.

### D5 — The tick interval is measured, never derived from `Options.GameSpeed`

`FrameTimer` is armed at the **top** of `Main_Loop` (`code/mainloop.cpp:296`), before
`Logic.AI()` (`:340`) and `Frame++` (`:436`). An alpha computed as
`(now - t_tick_boundary) / (GameSpeed * 16ms)` therefore tops out at `(T - L) / T`, where
`L` is the loop-top-to-boundary time dominated by `Logic.AI()`. The last `L/T` of each
tick's movement would never be drawn and every object would snap forward at each
boundary — reintroducing the exact 20 Hz stutter the change removes, worst in the large
battles that motivate it.

Measure the wall-clock span between consecutive tick boundaries instead and normalise by
that. It is self-correcting and absorbs the 16 ms quantisation of `FrameTimer`, the
multiplayer `NetFrameTimer` path (`code/mainloop.cpp:259-294`), latency padding and live
`GameSpeed` changes without branching on `Session.Type`. Discard spans outside
`[1, 250] ms` so a stall (alt-tab, level load, breakpoint) cannot stretch the next tick.
A rejected span does not fall back to the previous estimate: it pins alpha at 256 until
the next valid boundary-to-boundary span is measured, so a stall — or `GameSpeed=0`,
whose sub-millisecond spans are all rejected — degrades to exactly today's rendering
rather than to a glide computed against a stale interval.

The timestamp's **placement** in the loop is part of the design, not an implementation
detail. `Sim_Tick_Advance()` stamps the boundary where the Phase 2 sweep runs —
immediately before `Logic.AI()` (`code/mainloop.cpp:340`) — and alpha is
`(now - boundary) / span` with the span measured sweep-to-sweep. That placement makes
drawn motion uniform in wall-clock time. Two numbers fall out of the loop shape and are
*correct*, not defects: the `Map.Render()` at the top of the next loop pass
(`code/mainloop.cpp:315`) composes the tick's last frame a few milliseconds *before*
the next boundary, so its alpha reads about `256 * (T - R) / T` where `R` is the
loop-top-to-sweep time; and the first frame composed after `Logic.AI()` picks up at
about `256 * L / T` where `L` is the AI time. The apparent jump between those two
frames equals exactly the displacement uniform motion covers during the gap in which no
frame could be composed at all — it is not a stutter, and it does not become one as
scenes get heavier: heavier scenes widen the composition gap, which is a frame-rate
cost, not an interpolation error.

The load path leans on the rejection rule deliberately: after a scenario load or save
restore, the first measured span covers the load and is rejected, pinning alpha at 256 —
which also zeroes every offset while `RenderPrevious` is still stale from
deserialization (see Phase 2). That is the upper bound's concrete job; do not remove it
as unreachable.

Clock is `timeGetTime()` at 1 ms. `SystemTimerClass` is `timeGetTime()/16`
(`code/stimer.cpp:57`) and offers only ~3 distinct values per tick — far too coarse.
`timeBeginPeriod(1)` is already in force process-wide via `MillisecondSystemTimerClass`
(`code/mstimer.cpp:24`), used by the global `NetFrameTimer` (`code/_timer.cpp:41`).

### D6 — Moving objects do not write the depth buffer, so no depth cleanup is needed

The first draft contained a whole phase to drive dirty-area registration at render rate,
on the premise that foreground draws write depth and would leave stale depth at previous
sub-frame positions. **That premise is false, and the phase is cut.**

`code/draw.hh:34-36` distinguishes three flags: `SHAPE_ZREAD` (test, flat depth),
`SHAPE_ZGRAD` (test, gradient depth) and `SHAPE_ZWRITE` ("Depth-test **AND write** the
z-buffer"). Only `SHAPE_ZWRITE` writes, and it appears in seven files, all static
scenery: `cell.cpp` (tiles, overlays, tiberium), `terrain.cpp`, `isotype.cpp`, `fog.cpp`
(last-seen objects), plus `convert.cpp` (the blitter) and `draw.hh` itself. Infantry draw
with `SHAPE_ZGRAD` only (`code/infantry.cpp:646`); voxel units with
`SHAPE_ZGRAD|SHAPE_ALPHA` (`code/unit.cpp:2853,2864`). Movers reach
`TechnoClass::Techno_Draw_Object` through `FootClass::Draw_Object` (`code/foot.h:541-544`)
whose `zwrite` parameter defaults to `false` (`code/techno.h:654`), so
`code/techno.cpp:5979` never sets the flag for them. Buildings write depth only because
they pass `zwrite = true` explicitly (`code/building.cpp:853,888,890`) — and buildings do
not move.

Colour residue is likewise a non-issue: `code/tactical.cpp:1242` re-blits the whole
`TacticalRect` from the cached `TileSurface` on every background pass, so the foreground
starts clean each frame.

Settled at the blitter level: `ConvertClass::Blitter_From_Flags` (`code/convert.cpp:716`)
routes `SHAPE_ZREAD|SHAPE_ZGRAD` to the depth-read-only blitters, whose inner loop
(`code/blitblit.h:711-737`) reads `if (z_min < *zb++)` and **never assigns** to the depth
buffer; it likewise only reads the alpha buffer through `aLUT[*ap]`. Every mobile-object
call site passes `zwrite = false` explicitly — `code/unit.cpp:2727,2736,2846,2857,2875`
and `code/infantry.cpp:654,702`.

Corroborating: `Sync_Delay` already calls `Map.Render()` on every spin iteration
(`code/mainloop.cpp:606,625`), so the foreground pass already runs hundreds of times per
tick against a depth buffer built once. Occlusion is provably insensitive to foreground
repetition today.

### D7 — Reuse what already exists

- `Coord Lerp(Coord const &, Coord const &, float)` exists (`code/wave.h:185`,
  `code/wave.cpp:364`) and is fine where simulation code already uses it
  (`code/drive.cpp:1326`), but it blends through `float`. The render offset in **D3**
  deliberately stays integer fixed-point — it runs per object per rendered frame — so
  do not reach for `Lerp` there.
- `MillisecondTimerClass MillisecondTimer` already exists (`code/milsectmr.h`, global in
  `code/_milsectmr.h`), is RDTSC-based returning `double` milliseconds, and is already used
  for time budgeting at `code/light.cpp:196-253`. It is finer than `timeGetTime()` but is
  scaled by `Get_CPU_Rate`, so a few-percent frequency error would show as a small hold or
  jump at each tick boundary. Prefer `timeGetTime()` per **D5**; note this as the upgrade
  path if sub-millisecond phase is ever wanted.
- `Tactical::Flag_Cell` (`code/tactical.cpp:3281`) uses the same `LastRedrawFrame != Frame`
  idempotence idiom as `Tactical::AI` — the pattern is established repo-wide, so following
  it needs no justification.

---

## Implementation tracker

### Phase 0 — Measure, and decide whether to continue
*Files: `code/logic.h`, `code/logic.cpp`, `code/gscreen.cpp`, `code/mainloop.cpp`. Depends on: nothing.*

- [x] Add `RenderFramesThisSecond` / `LastRenderFramesPerSecond` beside the existing
      `FramesThisSecond` / `LastFramesPerSecond` (`code/logic.h:60-63`,
      `code/logic.cpp:88-91`), incremented at the top of `GScreenClass::Render()`
      (`code/gscreen.cpp:386`) — the single funnel for every composed frame. Do **not**
      repurpose the existing counter: `code/queue.cpp:2051` multiplies
      `LastFramesPerSecond` by a response time to derive network latency, so it must keep
      meaning the simulation rate.
- [x] Roll both over in the existing once-per-second `fps_timer` block
      (`code/mainloop.cpp:638-649`).
- [x] Emit a once-per-second `DebugString` from that block reporting: simulation rate,
      render rate, the measured tick span, and **the per-tick pixel-delta maxima
      accumulated since the last report, split by object family** (ground vehicles,
      aircraft, projectiles, particles — `RTTI_UNIT` / `RTTI_AIRCRAFT` / `RTTI_BULLET` /
      `RTTI_PARTICLE`). Route it through the debug log, not the screen:
      the on-screen readout at `code/mainloop.cpp:836` early-returns for
      `Session.Type == GAME_NORMAL` (`code/mainloop.cpp:824-826`), only draws on
      `(Frame & 7) == 7`, and requires the `-MPDEBUG` switch (`code/init.cpp:1809-1811`),
      so it is unusable for campaign measurement. `manual/changes/debug-log-console.md`
      documents the log that is always available.
- [x] The deltas are measured **once per simulation tick, across every display layer**:
      immediately after `Logic.AI()` in `Main_Loop`, walk `DisplayClass::Layer[]`,
      compute each object's delta against a debug-only static side table in
      `mainloop.cpp` (object pointer to previous `Position`), fold it into its family's
      running maxima, and refresh the table. Record **two maxima per family, in
      different units for different consumers**: the projected pixel delta, which
      answers the visible-benefit question, and the full 3-D lepton delta (largest
      absolute component — the exact metric the Phase 2 threshold walk will test),
      which calibrates `RENDER_INTERP_SNAP_LEPTONS`. Projection is many-to-one —
      diagonal X/Y motion cancels in screen X and Z lift cancels in screen Y
      (`code/tactical.cpp:189-214`) — so a pixel figure cannot stand in for the lepton
      one. All layers, not just the ground layer: a
      flying Orca lives in `LAYER_TOP` (`code/fly.cpp:1562-1565`) and bullets in
      `LAYER_AIR` (`code/bullet.cpp:1147-1150`), so a ground-only sweep cannot observe
      two of the decision gate's three cases — and a once-per-second refresh would
      report per-second displacement, roughly twenty times the per-tick figure the
      gate's threshold is written against. This is the side-table design **D1** rejects
      for production — heap-slot reuse can pair a stale entry with a new object —
      acceptable here only because a wrong sample skews one debug log line and draws
      nothing. The whole measurement patch is disposable: Phase 5's close-out removes
      it.
- [ ] Take the baseline from a **Release** build. The `Benchmark` harness is allocated
      only under `#ifdef _DEBUG` (`code/init.cpp:310-315`) and `BStart`/`BEnd` are no-ops
      when `Benches` is `NULL` (`code/_bench.h:21-22`), so `BENCH_*` figures do not exist
      in Release. Note also that `BENCH_OBJECTS` and `BENCH_TACTICAL` are printed
      (`code/debug.cpp:472,480`) but never bracketed anywhere in the tree — they read
      zero and must not be cited as evidence.
- [ ] **Decision gate.** Order a vehicle across open ground, an Orca across the map, and
      fire rockets at a distant target; record max px/tick for each. If ordinary vehicles
      come out under ~1.5 px/tick, the visible win is confined to air and projectiles —
      confirm that is still wanted before funding Phases 1–5.
- [ ] **Threshold calibration** is a separate concern from the visible-benefit gate and
      uses the lepton maxima. Measurement only includes a multiplier when the measured
      object actually receives it, so exercise every threshold-controlled family and
      each materially different speed source deliberately: a veteran or speed-boosted
      vehicle (`code/foot.cpp:3407-3418`), a jumpjet, an unguided ballistic arc whose
      launch speed is computed from range and gravity (`code/weapon.cpp:242-251`),
      rockets, and the particle velocity families (sparks, debris). Record the lepton
      maxima per case in the plan.

**Phase 0 results (2026-09-04).** Instrumentation on branch `fps-fix/phase-0`
(`code/logic.h`, `code/logic.cpp`, `code/gscreen.cpp`, `code/mainloop.cpp`); every
line reference above was re-verified against the tree before use and none had moved.
The sweep runs immediately after `Logic.AI()` over all five `DisplayClass::Layer[]`
lists, keyed by object address; the pixel figure is the largest absolute component of
the delta between `Coord_To_Pixel_Absolute` projections (Z lift included), the lepton
figure the largest absolute component of the 3-D lepton delta. `RTTI_UNIT` is vehicles
only; infantry is not a plan family and was not measured.

Live play was not available (the user was at the machine), so the deltas were taken by
replaying the golden recordings in `baseline/golden/` on the Win32 Debug build,
minimized and unattended through `tools/baseline/Invoke-VanillaBaseline.ps1`. Deltas
are simulation state and do not depend on the configuration. With the instrumentation
in place all four sessions still report `MATCH` at frame 300. Longer runs used a
scratch manifest with `printCrcFrame` raised; a recording that runs out returns the
engine to the menu, and a campaign session pauses whenever the window loses focus, so
two of the long runs were cut short by ordinary desktop use.

| Session | Frames observed | Vehicles px / lep | Aircraft px / lep | Bullets px / lep | Particles px / lep |
|---|---|---|---|---|---|
| `ts-gdi01` | 2,191 (whole recording) | 2 / 16 | 7 / 51 | none present | 3 / 9 |
| `ts-nod01` | 300 | 4 / 32 | none present | none present | 6 / 42 |
| `fs-gdi01` | 862 | 3 / 26 | none present | 6 / 57 | 8 / 56 |
| `skirmish-gcanyon` | 981 (whole recording) | 2 / 13 | none present | none present | none present |

Measured tick span: 31-33 ms in the three campaign sessions (simulation 30 ticks/s;
the campaign runs at `GameSpeed` 2) and 47-49 ms in the skirmish (20 ticks/s at
`GameSpeed` 3), matching the **D5** estimate of 48 ms. `GScreenClass::Render` ran
1,000-3,900 times per second on the minimized Debug build; that is a Debug figure and
is not the Release render rate the item below asks for.

Provisional gate reading: the ordinary skirmish vehicles stayed at 1-2 px/tick
(3-13 leptons), consistent with the estimate above; one campaign vehicle reached
4 px/tick (32 leptons) for about a second in `NOD1A`, type not identified. Aircraft
(7 px), bullets (6 px) and particles (up to 8 px) each gain several distinct sub-tick
positions per tick. On this evidence the visible win is confined to air, projectiles and
particles, as the section *What this will and will not visibly change* predicts.
Largest lepton delta seen in any family: 57. Not measured, and required before the gate
and calibration items are closed: a Release-build render rate, a heavy scene, and the
deliberate calibration cases (veteran or speed-boosted vehicle, jumpjet, unguided
ballistic arc, rockets, sparks and debris), all of which need a live session.

**Verification:** Release build; skirmish; the log shows a simulation rate near 20, a
render rate in the hundreds, and per-tick pixel deltas for the three cases above.

**September 5 preparation:** PRs #2 and #1 merged into fork main `64383e2`, and this measurement branch was rebased onto it. Win32 Debug and Release builds passed before the additional instrumentation below; logs are in `baseline/rebase-debug-build-2026-09-05.log` and `baseline/rebase-release-build-2026-09-05.log`. No replay or live measurement was run on September 5 because the user deferred desktop access. The four-session replay check remains required.

The original sweep omitted every `RTTI_INFANTRY`, making the required jumpjet calibration unobservable. The disposable instrumentation now records a separate `jumpjet` family for infantry whose type has `IsJumpJet`, including their ground movement and flight, and records the object type producing each pixel and lepton maximum. Ordinary infantry remain excluded. This is a measurement-only change: the sweep reads positions and type metadata and writes only its private counters, strings, and debug log; no simulation state, serialization, draw position, or production interpolation has changed. The Phase 0 calibration and gate remain open until the live cases are exercised.

Both additional-instrumentation builds passed on September 5 with `& 'C:\Program Files\CMake\bin\cmake.exe' --build build --config Debug --target OpenTS -- /m /nodeReuse:false` and the same command with `--config Release`, using the existing VS 2022 Win32 configuration. Full logs are `baseline/calibration-debug-build-2026-09-05.log` and `baseline/calibration-release-build-2026-09-05.log`. Both report the existing C5055 warning in untouched `code/queue.cpp:4471`; no warning was reported for the changed measurement code. The builds were made before the measurement commit and carry a modified stamp. Runtime golden comparisons, render-rate measurements, and all live calibration remain unrun. No production manual change is needed for disposable private instrumentation; this phase record owns its measurement-log additions.

### Phase 1 — The render clock and the A/B gate
*Files: new `code/interp.h`, `code/interp.cpp`; `code/mainloop.cpp`, `code/gscreen.cpp`, `code/options.h`, `code/options.cpp`. Depends on: Phase 0.*

`code/CMakeLists.txt:24-32` globs `*.cpp`/`*.h` with `CONFIGURE_DEPENDS`, so new files
need no build-file change.

- [ ] Create `code/interp.h` / `code/interp.cpp` (header block copied from a recent
      OpenTS-authored file; `#include "always.h"` first per `docs/STYLE.md`). Public
      interface: `Sim_Tick_Advance()`, `Render_Frame_Begin()`, `Fetch_Render_Alpha()`
      returning 0–256 fixed point.
- [ ] Implement the measured-interval clock per **D5**, including the `[1, 250] ms`
      sanity window, the pin-to-256 behaviour on a rejected span, and the clamp of
      alpha to `[0, 256]`.
- [ ] Call `Sim_Tick_Advance()` from `Main_Loop()` immediately before `Logic.AI()`
      (`code/mainloop.cpp:340`), where the Phase 2 sweep will join it — the placement is
      load-bearing per **D5** — and `Render_Frame_Begin()` at the top of
      `GScreenClass::Render()` (`code/gscreen.cpp:386`) so every object in a frame
      shares one alpha.
- [ ] Add the `SmoothMotion` gate now, not in Phase 5: `bool SmoothMotion;` on
      `OptionsClass` beside `VSync` (`code/options.h:166`), default `true` in the
      constructor beside `VSync(false)` (`code/options.cpp:134` — **not** `:112`, which
      is `GameSpeed(3)`), read in `Load_Settings` with the other `[Video]` keys, and the
      pin-alpha-to-256 branch in `Render_Frame_Begin()`: when false, every
      `Fetch_Render_Offset` returns zero and the draw path is byte-identical to today.
      Every later phase's verification A/Bs against this switch, so it must exist before
      the first visible change. Persistence and the manual page stay in Phase 5.
- [ ] Log, for a window of consecutive frames spanning at least one boundary, the pair
      (alpha, wall-clock ms since the last boundary). **Acceptance: alpha advances
      uniformly — delta-alpha per delta-ms stays near 256/span across the window,
      including across the boundary.** The last frame before a boundary is *expected*
      to read about `256 * (T - R)/T`, not 256 — per **D5** that is correct behaviour,
      and tuning the clock to force it to 256 would break uniformity. Record the
      numbers in a heavy scene as well as a light one.

**Verification:** measured span ≈ 48 ms at `GameSpeed=3` and tracking changes to the
speed slider; alt-tab for ten seconds and confirm the span is rejected rather than
recorded; `GameSpeed=0` pins alpha at 256. **Nothing on screen may change — that is the
test.**

### Phase 2 — Previous-position capture
*Files: `code/object.h`, `code/object.cpp`, `code/anim.h`, `code/anim.cpp`, `code/mainloop.cpp`. Depends on: Phase 1.*

- [ ] Add `Coord RenderPrevious;` to `ObjectClass` beside `Position`, initialised in the
      constructor (`code/object.cpp:157-176`). Deliberately omit it from
      `ObjectClass::Serialize` (`code/object.cpp:2050-2071`) and `Compute_CRC`
      (`code/object.cpp:2246`), with a one-line comment saying the omission is
      intentional — that omission is the load-bearing part.
- [ ] Add the tick-boundary sweep over `DisplayClass::Layer[]` copying `Position` into
      `RenderPrevious`, called from `Main_Loop()` immediately before `Logic.AI()`
      (`code/mainloop.cpp:340`) so the snapshot is start-of-tick state.
- [ ] Add `Invalidate_Render_Interpolation()` and wire the two paths listed in **D2**
      (`Unlimbo`, chronoshift).
- [ ] Add the post-`Logic.AI()` threshold walk from **D2**: one pass over
      `DisplayClass::Layer[]` snapping `RenderPrevious` to `Position` wherever the tick
      delta exceeds `RENDER_INTERP_SNAP_LEPTONS`. Derive the constant from the Phase 0
      per-family measured maxima with the margin **D2** states (the retail INI values
      are a sanity cross-check only), and record the measured maxima and the chosen
      value in the plan when done.
- [ ] Declare `virtual Coord Fetch_Render_Offset(void) const;` beside `Render_Coord`
      (`code/object.h:244`), implement per **D3**, and override in `AnimClass` per **D4**.
- [ ] State the load path where it lives in code: a save restore fills `Position`
      field-by-field without running `Unlimbo` (`code/object.cpp:2050-2071`), so
      `RenderPrevious` is stale until the first sweep — and no frame can draw the stale
      offset because the first post-load span is rejected and alpha pins at 256
      (**D5**).
- [ ] Extend the Phase 0 log line with the largest offset magnitude across the layers,
      the largest **pre-snap** tick delta, and a per-family count of threshold snaps. A
      snapped offset reads zero, so without the pre-snap figures a false snap — a
      legitimate fast mover silently losing interpolation — would be invisible.
      Acceptance: zero threshold snaps for legitimate movers, checked not against the
      Phase 0 calibration sample (a threshold set 4x above those observations passes
      there trivially — that check is circular) but across the *different* Phase 3 and
      Phase 4 verification scenarios on retail data, with the pre-snap log kept alive
      through both phases.

**Verification:** save mid-mission, quit, reload — the save must load, and the first
seconds after the load must draw at true positions (the **D5** pin covering the stale
`RenderPrevious`). Play a two-client LAN skirmish past ~2000 frames with no desync
message. Chronoshift a unit and confirm its offset for that frame is zero. **Nothing on
screen may change.**

### Phase 3 — Apply the offset at the two funnel sites (first visible result)
*Files: `code/object.cpp`, `code/tactical.cpp`. Depends on: Phase 2.*

- [ ] `code/object.cpp:1192` — `Coord_To_Pixel(Render_Coord() + Fetch_Render_Offset(), point)`.
- [ ] `code/tactical.cpp:2753` — `Coord_To_Pixel(obj->Center_Coord() + obj->Fetch_Render_Offset(), pixel)`.
      This supplies the pixel to `Draw_Pre_Render`/`Draw_Post_Render` (selection box,
      health bar) and **must land with the first site** or decorations detach from bodies.
- [ ] Resolve the **D3** lift asymmetry: establish in a live game which of the two
      `Position.Z` subtractions — the funnel's (`code/tactical.cpp:207-214`) or the
      SHP-aircraft shift in `Techno_Draw_Object` (`code/techno.cpp:5951-5953`) — carries
      the visible lift for an SHP-drawn aircraft, route the interpolated Z through
      exactly one, and confirm the voxel body path (`code/aircraft.cpp:478`) glides
      vertically.

Two lines plus one check. That is the whole phase.

**Verification:** A/B against `SmoothMotion=no`. Orca across the map and rockets at a
distant target should glide. Selection box and health bar must stay glued to a moving
unit. Chronoshift, drop-pod and tunnel a unit each — no streaking. Ground units are
*expected* to look unchanged. LAN skirmish past ~2000 frames with no desync.

### Phase 4 — Dependent visuals
*Files: `code/aircraft.cpp`, `code/unit.cpp`, `code/building.cpp`, `code/vanim.cpp`, `code/techno.cpp`. Depends on: Phase 3.*

These derive a coordinate from a moving object without passing through
`ObjectClass::Render()`, so they stay pinned to the true position while the body moves.

- [ ] Aircraft shadow — `code/aircraft.cpp:421`. Most visible defect.
- [ ] Hunter-Seeker shadow — `code/unit.cpp:2547`.
- [ ] Vehicle exiting a war factory, drawn from the building's hook using the *techno's*
      offset — `code/building.cpp:935`.
- [ ] `VoxelAnimClass::Draw_It` recomputes its own pixel — `code/vanim.cpp:198,201`.
- [ ] 3-D selection bracket for core-defender units —
      `code/techno.cpp:1194-1252,1316-1340`.
- [ ] Spark and railgun particles plot their own pixel from `PositionCoord` —
      `code/particle.cpp:684-690`. Sparks are single short-lived pixels; fix or
      document.
- [ ] Particle artwork lifts by true `Height` while its X/Y arrives interpolated from
      the funnel — `code/particle.cpp:668`. Same family as the **D3** aircraft
      question.
- [ ] Confirm `AlphaShapeClass` light halos: they are positioned in absolute pixel space
      at `Unlimbo` (`code/object.cpp:1414-1422`) and not repositioned by
      `AlphaShapeClass::Update_All` (`code/alphashp.cpp:172-183`). Establish whether any
      moving unit type carries `AlphaImageData`, and either fix or document.

Deliberately **not** changed, and documented as such: `LaserDrawClass`
(`code/laser.cpp:192-194`) stores absolute `Start`/`End` snapshots with no owning object
reference, so a beam already detaches from its barrel for its duration today; the same
applies to `code/wave.cpp`, `code/ionblast.cpp` and `code/blight.cpp`. Action lines
(`code/foot.cpp:3803-3804`) are a ≤2 px wobble on a 1 px line.

**Verification:** fly an Orca low over flat ground and watch the shadow stay pinned.
Build a vehicle and watch it exit the war factory. Set a unit on fire and confirm the
flame tracks it (the **D4** override). Select a core defender and confirm the bracket
tracks. Watch missile smoke and rising fire particles glide (the free layer coverage)
and confirm sparks stay unobtrusive.

### Phase 5 — Persistence, documentation, close-out
*Files: `code/options.cpp`, `code/logic.h`, `code/logic.cpp`, `code/gscreen.cpp`, `code/mainloop.cpp`, `code/interp.cpp`, `manual/`. Depends on: Phases 3–4.*

- [ ] `SmoothMotion` itself landed in Phase 1 (member, `Load_Settings` read, pin-alpha
      gate — the A/B instrument every phase's verification used). Here, finish its
      persistence: save it in `Save_Settings` beside `VSync` (`code/options.cpp:468`).
      The precedent is not symmetric: `VSync` is **not** read by `Load_Settings`
      (`code/options.cpp:351-422`) but in `code/startup.cpp:530-534`, before the
      renderer exists. `SmoothMotion` has no such ordering constraint, which is why it
      reads in `Load_Settings` — say so in the commit.
- [ ] Remove the Phase 0/1 instrumentation: the debug side table and per-family
      accumulators, the once-per-second measurement log line, the alpha-uniformity log,
      and the `RenderFramesThisSecond` / `LastRenderFramesPerSecond` counters. The
      measurement patch was disposable; its results live in this plan's phase records,
      not in the engine.
- [ ] `python manual/tools/manage.py update`, then scaffold and write:
      `manage.py scaffold key SmoothMotion` and
      `manage.py scaffold change smooth-motion --category feature --target-type key --target-id SmoothMotion --effect added --title "<title>"`.
      Model the prose on `manual/changes/bgfx-presenter.md`. State plainly that
      simulation rate, pace, balance, save format and multiplayer behaviour are
      unchanged, and that only where an object is *drawn* between two simulation
      positions has changed. Say which movers are visibly affected and which are not.
- [ ] `python manual/tools/manage.py check`.
- [ ] Clean-configure Win32 Debug and Release per `docs/BUILDING.md`.
- [ ] Commit — **only on explicit user request**. `AGENTS.md:76` permits committing only
      when asked. Imperative subject ≤72 characters, no body unless a brief factual
      exception is needed, no AI-attribution or `Co-authored-by` trailers.

**Verification:** with `SmoothMotion=no`, behaviour matches the Phase 0 baseline; absent
or `yes`, motion is smooth. `manage.py check` passes.

---

## Known limitations, documented rather than fixed

- **Draw order stays on true positions.** `DisplayClass::Layer[LAYER_GROUND].Sort()`
  (`code/mainloop.cpp:335`) orders by `Sort_Y()` = true `Render_Coord().Y`, which
  constraint 1 forbids interpolating. Because movers do not write depth (**D6**), overlap
  between two crossing movers is resolved purely by that order, so for up to one tick two
  objects whose true Y ordering has just swapped are drawn in the new order at old-ish
  positions. Bounded by the per-tick delta (~1 px for ground units) and self-correcting.
  Watch for it during Phase 3 verification around unit crossings.
- **The depth *key* stays on the true position.** `FootClass::Get_Z_Adjust()`
  (`code/foot.cpp:3343-3365`) picks its cliff, tunnel and bridge fudge from the object's
  true cell while the sprite draws at the interpolated pixel. Verify at ramps, bridges
  and cliff edges during Phase 3.
- **Beams and waves keep frozen endpoints** — see Phase 4.
- **Locomotor draw-point bobs stay tick-stepped.** `AircraftClass::Draw_It` adds
  `Locomotion->Draw_Point()` to the funnel pixel after interpolation
  (`code/aircraft.cpp:409-410`, `code/fly.cpp:1401`); the flight bob is a per-tick sim
  value, so a hovering jumpjet or Orca keeps a slight 20 Hz vertical dither on top of
  its now-smooth travel.
- **Mouse hit-testing follows the drawn position, deliberately.**
  `Tactical::SelectableObjects` is cleared each pass (`code/tactical.cpp:1048`) and filled
  by objects as they draw (`code/unit.cpp:2919`, `code/aircraft.cpp:459`), so it picks up
  the interpolated point for free once Phase 3 lands. The player clicks what they can see;
  the discrepancy is under one tick of movement against an acceptance radius of roughly
  14 px (`code/tactical.cpp:3155-3158`). **Do not "fix" this back to true positions.**

## Cut after review

Two phases from the first draft were removed, both on adversarial review:

- **Render-loop pacing.** Gating composition on a present-due check inside `Sync_Delay()`
  has a real race: `Map.Input()` pumps Windows messages via `Keyboard->Check()`
  (`code/keyboard.cpp:178`), and the pump itself calls `Video_Present_If_Dirty()`
  (`code/msgloop.cpp:167`), which resets `_LastPresentTime` immediately before the gate
  would query it — so the gate reads false and the frame is skipped. It is also
  unnecessary: the engine already presents at refresh rate, so interpolation alone
  delivers the goal. Keep as a contingency only if Phase 0 shows the render rate must be
  capped, and design it with an independent frame-production deadline evaluated before
  anything that can present.
- **Time-basing edge scroll and screen shake.** Pre-existing frame-rate dependence, not
  caused or worsened by this change once pacing is cut, and it alters felt behaviour the
  user asked to leave alone. Two facts settle it: `Map.ScreenX` is never set non-zero
  anywhere in the tree (the only simulation write is `Map.ScreenY = 10` at
  `code/ionblast.cpp:108`), so half the shake code is unreachable; and `ScreenX`/`ScreenY`
  **are** serialized (`code/gscreen.cpp:105-106`). Worth a separate change on its own
  merits.

## Risks

- **The visible win may be smaller than hoped.** This is the dominant risk and Phase 0's
  decision gate exists for it. See *What this will and will not visibly change*.
- **Aircraft altitude is the highest-value case and the one the original design got
  wrong** (**D1**). If the Phase 2 sweep is implemented as a setter hook instead, the
  feature will appear to work for ground units and silently not work for aircraft.
- **No regression safety net.** Mitigated by the change being render-only by
  construction: it never reads or writes simulation state and never touches the
  synchronized RNG. Keeping that property true is the single most important review
  criterion.
- **Draw-order and depth-key artifacts** are bounded and listed above, but they are the
  most likely source of "looks subtly wrong" reports.

## Review record

Reviewed by two independent blind adversarial passes (a fresh Opus agent and Codex
`gpt-5.6-sol` at high reasoning, both read-only). Both returned `VERDICT: REVISE`.
Accepted and applied: the false single-write-point claim (D1), the alpha phase-shift bug
(D5), the attached-animation defect (D4), the undeliverable teleport threshold (D2), the
Debug-only benchmark harness and dead `BENCH_OBJECTS`/`BENCH_TACTICAL` counters (Phase 0),
the `VSync` precedent citation errors (Phase 5), and the removal of the pacing and
scroll/shake phases.

The reviewers **disagreed** on whether foreground draws write the depth buffer. Resolved
against the Claude reviewer by direct source verification — see **D6** — which removed an
entire phase founded on the false premise.

A second dual-blind adversarial loop ran on 2026-08-27 against the revision that added
the *Relationship to the HD image-upgrades plan* section (fresh Claude adversary and
Codex `gpt-5.6-sol` at high reasoning, both read-only, neither seeing the other; two
Codex-led follow-up rounds). Both round-1 verdicts were `REVISE`. Applied across the
three rounds: the Phase 0 rebuild (per-tick, all layers, per-family pixel *and* lepton
maxima, explicit calibration cases); the D2 redesign (the per-frame render-rect snap —
which un-snapped mid-tick and zeroed out for artwork-less voxel classes — replaced by
the post-AI lepton-threshold walk, the hook list trimmed to `Unlimbo` plus chronoshift,
the threshold calibrated from measured lepton maxima with false-snap logging and a
non-circular acceptance check); the D5 placement and uniformity specification, replacing
Phase 1's "alpha near 256" acceptance test with a uniformity check; the `SmoothMotion`
gate moved to Phase 1; instrumentation removal added to Phase 5's close-out; the
magic-16 naming bullet cut; the load-path statement; and, in the cross-plan contract,
the stage-phase accessor narrowed to countdown-derived directed progress with sequence
topology left to its owners, direction-specific sub-frame blocks for reversible
sequences, and owner-family fixtures. Codex's proposal to cut the `SmoothMotion` option
was rejected (verification dependency, deliberate-behaviour-change policy, `VSync`
precedent) and Codex conceded the rejection in round 2. The two blind reviewers
contradicted each other twice — cut-the-option versus move-it-earlier, and
cut-the-backstop versus cut-the-hooks — and both times the applied synthesis differs
from both recommendations. The round cap ended the loop after round 3; the round-3
fixes implement Codex's own recommended changes but have not themselves been
re-reviewed.

## Status

Phase 0 is partially implemented on `fps-fix/phase-0`: disposable engine instrumentation and the September 4 replay measurements are committed. Release render-rate measurements, the deliberate calibration cases, and a heavy scene remain outstanding; see the Phase 0 results above. Phases 1-5 have not started. The measurement branch was rebased onto the merged baseline on September 5.

The current text reflects three review cycles: the original dual adversarial pass, a
source-verified deep dive (2026-08-27) that added the *Relationship to the HD
image-upgrades plan* section and its cross-plan contract, and a three-round dual-blind
adversarial loop on that revision — see *Review record* for what each accepted,
rejected, and why. The final round's fixes follow the reviewer's own recommendations
but were applied after the round cap and have not been re-reviewed.
