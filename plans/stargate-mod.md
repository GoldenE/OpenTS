# Stargate mod for OpenTS

## Summary

Build a Stargate-themed total conversion on OpenTS, plus the minimum generic
engine capability it requires.

The mod reskins the two shipped factions rather than adding new ones: the GDI
slot becomes the Tau'ri/SGC and the Nod slot becomes the Goa'uld. Tiberium is
rethemed as a Replicator infestation, and the harvester's existing harvest
action is reskinned as a disassembly beam. Ancient and Asgard technology
appears as neutral capturable structures, not as a playable side.

The signature mechanic is the gate. A Stargate is a tunnel tube pair: a unit
walks in, vanishes, and emerges elsewhere. Wormholes are one-way, a gate holds
one connection at a time, the receiving side gets a dial-in warning, and the
iris is a Firestorm wall over the exit cell. Only what fits through the ring
travels, which the tube system already enforces by excluding aircraft.

The campaign runs a persistent Earth hub. The player builds and defends a base
around the SGC gate on the largest map the engine supports, dials out to launch
an off-world scenario, and returns to an Earth restored exactly as it was left
plus whatever the expedition changed. Earth is paused while the player is away
and never loses in absentia; expeditions change what is waiting on return.

The only engine work this requires is an expedition state machine built on the
existing save/load and campaign-carryover machinery, plus one new trigger
action. Everything else is mod data.

## Verified engine facts

Every figure below was measured against this repository or the installed game;
none is carried from memory.

### Map size

- The playfield is a rotated diamond. `MapClass::In_Radar` (`code/map.cpp`)
  bounds it by `x+y > W`, `|x-y| < W`, and `x+y <= W + 2H`.
- `MapClass::Set_Map_Dimensions` (`code/map.cpp:715`) allocates cells at
  `idx = MAP_CELL_H * y + x` into a `MAP_CELL_TOTAL` (262,144) pointer table.
  Simulating that loop against the real `In_Radar` predicate shows the highest
  allocated coordinate is `W + H - 1` on both axes.
- The ceiling is therefore **`W + H <= 512`**. A 256x256 map lands on array
  index 262,143 - the last slot - which identifies it as the designed limit.
- `MapClass::Array` is `VectorClass<CellClass *>` (`code/map.h:509`), sparsely
  populated (`code/map.cpp:654`), so only diamond cells allocate a `CellClass`.

Measured cell counts:

| Size | W+H | Cells | Note |
|---|---|---|---|
| 256 x 256 | 512 | 130,816 | largest area the engine supports |
| 400 x 112 | 512 | 89,488 | largest the bundled editor documents |
| 112 x 400 | 512 | 89,200 | |
| 298 x 149 | 447 | 88,655 | largest shipped TS map (multiplayer) |
| 120 x 150 | 270 | 35,850 | largest shipped TS campaign map |
| 125 x 129 | 254 | 32,121 | "Core of the Problem", GDI/Nod 9 |

Shipped sizes were read directly from the `[Map] Size=` entries in
`MAPS01.MIX`, `MAPS02.MIX`, `maps03.mix`, and `multi.mix` (135 maps). TS map
files are plain INI text inside the archives.

A 256x256 hub is about 1.5x the largest map Tiberian Sun ever shipped and
roughly 3.6x its largest campaign mission.

### Editor

The bundled FinalSun 2.0 changelog records "Maps up to 400x112 (or 112x400) are
now allowed" and "Tunnel tube editing completely reimplemented ... You can now
create unidirectional tubes". One-way tubes - the exact primitive the gate
mechanic needs - are natively authorable.

### Gate primitives

- `TubeClass` (`code/tube.h`) carries `Enter`, `Exit`, `EnterDir`, and a
  `Dirs[100]` path; the A* pathfinder treats a tube as a graph edge
  (`code/astar.cpp:83`). Maps store them in the `[Tubes]` section
  (`code/tube.cpp:28`). Traversal is implemented for infantry, drive, and hover
  locomotion only, so aircraft cannot use a gate.
- A tube path is a contiguous traced cell chain capped at 99 steps, and each
  tube has exactly one fixed exit. A bidirectional pair is two tubes.
- `TeleportLocomotionClass` (`code/teleport.cpp`) is fully implemented and
  assignable through `Locomotor=` on any TechnoType.
- The Firestorm wall is a togglable barrier with a charge budget that drains
  when damaged (`code/building.cpp:1758`, `:2264`), destroying what it catches
  via `Rule->FirestormWarhead`. Rules keys: `FirestormWall`,
  `GDIFirestormGenerator`. Trigger actions `TACTION_ACTIVATE_FIRESTORM` and
  `TACTION_DEACTIVATE_FIRESTORM` already exist.

### Campaign and persistence

- `Do_Win` (`code/scenario.cpp:1016`) ends a mission with `Environment.Store()`,
  then `Start_Scenario(...)`, then `Environment.Restore()`.
- `EnvironmentClass` (`code/enviro.cpp`) carries 50 global flags
  (`SCEN_GLOBAL_COUNT`, `code/scenario.h:57`), leftover money, the mission
  timer, difficulty, and the map-select stage across missions.
- `Load_Game(const char *)` and `Request_Save_Game(const char *, const char *)`
  (`code/saveload.h:25-27`) take arbitrary filenames, not fixed UI slots.
- `Request_Save_Game` writes **synchronously** for `GAME_NORMAL` and
  `GAME_SKIRMISH` (`code/saveload.cpp:1030`). The deferred
  `Process_Pending_Save_Game` path exists only for multiplayer, so the campaign
  hub save completes within the call and needs no wait.
- **Scenario transitions never run inside simulation.** `PlayerWins` and
  `PlayerLoses` are flags set during the frame (`code/house.cpp:1265`) and acted
  on at a safe point in the main loop after `Queue_AI()` and `Call_Back()`,
  which is where `Do_Win()` and `Do_Lose()` are finally called
  (`code/mainloop.cpp:362-402`). `PlayerRestarts` and `PlayerAborts` are
  dispatched in the same block. Tearing down and rebuilding the world from
  inside a trigger action would destroy the objects whose AI is on the stack;
  the expedition transition must use this same deferred-flag pattern.
- Saves are gated to an exact engine-version match: `ExpectedGameVersion` is
  `OPENTS_VERSION_PACKED` and `Load_Game` rejects a mismatch
  (`code/saveload.cpp:1170`). There is no cross-version save migration.
- Map `[Basic]` supplies `NextScenario`, `AltNextScenario`, `CarryOverMoney`,
  `CarryOverCap`, `AllowableUnits`, `StartingDropships`, `SkipMapSelect`.
- The inter-mission map-select screen is data-driven from `MAPSEL.INI`
  (`code/mschoice.cpp:69`).

### Trigger actions

- The enum is `TActionType` (`code/taction.hh`), currently ending at
  `TACTION_TALK_BUBBLE` (index 105) followed by `TACTION_COUNT`.
- Dispatch is the `INVOKE` macro switch at `code/taction.cpp:537`; parameter
  shape comes from `Action_Needs` (`code/taction.cpp:2505`).
- Action indices are written into map files and are a compatibility boundary.

### Mod delivery

`init.cpp` wildcard-scans `ECACHE*.MIX` (`:2159`) and `ELOCAL*.MIX` (`:2182`)
and loads numbered `EXPAND%02d.MIX` / `ECACHE%02d.MIX` (`:2393`). The mod ships
through those archives; no engine change is needed to load it.

## Implementation tracker

Phases run serially. Mod content lives in a separate repository; only Phase 2
and Phase 7 touch OpenTS.

### Phase 1: Prove the gate loop with stock assets

**Files:** mod repository `maps/` only. No OpenTS files.
**Depends on:** nothing.

- [ ] Author one stock-asset map with two regions that share no walkable ground route.
- [ ] Author an outbound gate as a one-way `[Tubes]` pair and a separate return gate as a second one-way pair.
- [ ] Place a Firestorm wall segment on the receiving exit cell as the iris and wire it to `TACTION_ACTIVATE_FIRESTORM` and `TACTION_DEACTIVATE_FIRESTORM`.
- [ ] Add a dial-in warning that fires before arrival: `TACTION_RADAR_EVENT`, `TACTION_PLAY_SOUND_AT`, and `TACTION_LIGHT_MEDIUM` at the receiving gate.
- [ ] Add DHD address pads: marked cells beside the gate, each firing its own trigger through `TEVENT_ENTERS_ZONE` or `TEVENT_NEAR_WAYPOINT`, so dialling is a diegetic move order and needs no new UI.
- [ ] Measure transit time as a function of tube path length and record the usable range under the 99-step cap.
- [ ] Confirm which locomotion types traverse the tube and that aircraft are excluded.
- [ ] Record the result in this plan. Gate control must be the decisive objective on this map before Phase 2 begins; if it is not, revise the mechanic here rather than building the hub on it.

### Phase 2: Engine expedition state machine

**Files:** `code/taction.hh`, `code/taction.h`, `code/taction.cpp`,
`code/scenario.h`, `code/scenario.cpp`, `code/enviro.h`, `code/enviro.cpp`,
`code/saveload.h`, `code/saveload.cpp`, `code/loaddlg.cpp`, `code/mainloop.cpp`,
`code/globals.h`, `code/globals.cpp`, `code/init.cpp`,
`manual/data/scripting.yaml` (regenerated, never hand-edited),
`manual/content/mapping/actions/`, `manual/changes/`.
**Depends on:** Phase 1.

- [ ] Append `TACTION_BEGIN_EXPEDITION` after `TACTION_TALK_BUBBLE` and before `TACTION_COUNT`. Never renumber existing actions; map files store the indices.
- [ ] Add its `Action_Needs` entry and a `TAction_BEGIN_EXPEDITION` handler registered through the `INVOKE` macro.
- [ ] Give the action a destination-scenario parameter.
- [ ] Add expedition state to `ScenarioClass`: hub scenario name, hub save path, in-expedition flag, and the engine version the hub save was written with. Serialize each member.
- [ ] Make the trigger action set a pending-expedition flag and record the destination only. It must not perform the transition: `Load_Game` tears down and rebuilds the world, and a trigger fires with object AI on the stack.
- [ ] Dispatch the transition from the main loop's existing safe point, in the same block that already handles `PlayerWins`, `PlayerLoses`, `PlayerRestarts`, and `PlayerAborts` after `Queue_AI()` and `Call_Back()` (`code/mainloop.cpp:362-402`). Clear the flag exactly as that block clears the others, and reset it wherever `init.cpp` resets `PlayerWins`/`PlayerLoses`.
- [ ] On dispatch: `Environment.Store()`, then write the hub save, then start the destination scenario. `Request_Save_Game` is synchronous for `GAME_NORMAL`/`GAME_SKIRMISH` (`code/saveload.cpp:1030`), so no wait on `Process_Pending_Save_Game` is needed; confirm the write succeeded before starting the destination and abort the expedition if it did not, rather than launching an expedition with no hub to return to.
- [ ] In `Do_Win`, take the expedition return path instead of `Start_Scenario`: `Load_Game(hub file)` followed by `Environment.Restore()`, in that order, so expedition globals overwrite the ones frozen inside the save.
- [ ] Make the `Do_Lose` replay path restore the hub save rather than restarting the Earth map from its INI, so a failed expedition does not discard hub progress.
- [ ] Define what `PlayerAborts` and `PlayerRestarts` mean during an expedition and implement them explicitly; neither may leave the campaign pointing at a hub save that no longer matches the expedition state.
- [ ] Fail an expedition return gracefully back to the campaign when the hub save's internal version no longer matches `ExpectedGameVersion`, rather than leaving a campaign unrecoverable after an engine update mid-expedition.
- [ ] Hide the reserved hub file from the load dialog's save listing.
- [ ] Confirm an ordinary player-initiated save taken during an expedition round-trips: it must restore into the expedition, not into a stale hub.
- [ ] Regenerate manual data with `python manual/tools/manage.py update`.
- [ ] Author the mapping page for the new action and add an intentional-change record.

### Phase 3: Expedition economy and stakes

**Files:** mod repository `rules/`, `maps/`.
**Depends on:** Phase 2.

- [ ] Give the Earth gate a large `Power=` drain so an unpowered grid prevents dialling, and wire `TEVENT_LOW_POWER` to a stranded-team state.
- [ ] Carry the deployment budget as credits through `CarryOverMoney` and `CarryOverCap` on off-world maps.
- [ ] Encode the loadout as a manifest, not a roster: reserve a contiguous global range set on Earth by what the player has built, and have off-world maps preplace starting forces from those globals. This fits the 50-global budget and needs no new UI.
- [ ] Write the reserved allocation for all 50 globals into this plan and keep it authoritative.
- [ ] Make the off-world win condition reaching and holding the local gate while it dials home.
- [ ] Add mid-mission reinforcement from Earth at the gate waypoint through `TACTION_REINFORCEMENTS_SPECIAL`, gated on the Earth treasury with a credits event.
- [ ] Gate the MCV behind a mid-campaign unlock so early expeditions are commando missions and the unlock forms the act break.

### Phase 4: Earth hub map at maximum supported size

**Files:** mod repository `maps/`.
**Depends on:** Phase 3.

- [ ] Author the Earth hub at `Size=0,0,256,256` - 130,816 cells, `W+H=512`, the engine's exact ceiling.
- [ ] If the editor refuses 256x256, author at 400x112 or 112x400 instead (89,488 / 89,200 cells). Both sit at `W+H=512`; the changelog documents only the elongated pair. Record which size was used and why.
- [ ] Set `LocalSize` inside the playfield and confirm the radar scale and minimap behave at this size.
- [ ] Lay out the SGC gate room, the power grid that dialling depends on, and the defended surface approaches.
- [ ] Preplace the Antarctic gate shrouded and neutral, flipped to Goa'uld ownership by `TACTION_CHANGE_HOUSE` on a story global, so the enemy can dial in behind the player's defences.
- [ ] Preplace the Ha'tak crash site as a neutral capturable tech structure revealed by a story global.
- [ ] Verify load time, pathfinding, and frame cost at full size, and record them against a stock-size map.
- [ ] Author three off-world scenario maps at ordinary campaign scale.

### Phase 5: Faction, resource, and neutral technology data

**Files:** mod repository `rules/`, `art/`.
**Depends on:** Phase 4.

- [ ] Retheme the GDI slot as the Tau'ri/SGC and the Nod slot as the Goa'uld. Keep exactly two playable sides.
- [ ] Retheme tiberium as Replicator blocks, visceroids as assembling spiders, and the veinhole monster as a nest.
- [ ] Reskin the harvester's harvest action as a disassembly beam. Art only; harvesting logic is unchanged.
- [ ] Use `TiberiumProof` for anti-Replicator shielding as a tech unlock.
- [ ] Add neutral capturable Ancient and Asgard structures granting weapon and support unlocks on capture.
- [ ] Assign `Locomotor=` the Teleport CLSID to an Asgard beam-out unit.
- [ ] Keep the GDI and Nod lineage as briefing-text backstory only. Do not build them as separate factions.

### Phase 6: Art production

**Files:** mod repository `art/`.
**Depends on:** Phase 5.

- [ ] Produce buildings and terrain first - one facing plus buildup, damaged, and loop frames: gate, DHD, sarcophagus, pyramid, Ancient outpost, Replicator fields.
- [ ] Reskin units through palette and house-remap work before authoring any new sprite sets.
- [ ] Target classic SHP and VXL. Do not make this mod depend on `plans/image-upgrades.md`; that plan is an eight-phase engine-wide rendering feature and blocking on it would stall everything here.
- [ ] Treat generated imagery as an authoring input requiring facing, anchor, shadow, and remap-mask review, per that plan's own constraint that generation does not produce valid voxel volumes, normals, pivots, or HVA animation.
- [ ] Author `MAPSEL.INI` as the dialling computer and gate-address book.

### Phase 7: Documentation, validation, and commit

**Files:** `docs/`, `manual/`, `plans/stargate-mod.md`.
**Depends on:** Phase 6.

- [ ] Update the owning manual pages for the new trigger action and the expedition behaviour, and state why any untouched page remains accurate.
- [ ] Run `python manual/tools/manage.py check`.
- [ ] Run `cmake -S . -B build -G "Visual Studio 17 2022" -A Win32`, then build `--config Debug` and `--config Release`, then run the CTest suite.
- [ ] Report exact commands, configurations, results, and material checks not run. A build result is not runtime evidence.
- [ ] Confirm no game assets, original binaries, generated art packs, or build output are staged.
- [ ] Commit with an imperative subject of at most 72 characters and no body narrative.

## Design reference

### Why two factions, not four

Keeping GDI and Nod as playable entities alongside the SGC and the Goa'uld
means four rosters, four tech trees, and four sets of cameo and sidebar art.
That is the scope failure mode for a side project. The C&C lineage survives as
backstory in briefing text at zero art cost: the Goa'uld ruled Earth as gods,
every cargo cult since is a memory of that, the Brotherhood was the last and
most successful, its leader stayed alive because the symbiote kept replacing
him, and GDI was founded to fight it and became something with a different name
and a mountain in Colorado. That is one paragraph, not a faction.

### Why Replicators map onto tiberium

The tiberium system already supplies everything the infestation needs: a
substance that spreads over terrain, damages units that enter it, mutates
infantry into a hostile creature, and supports a stationary tendrilled organism
in the veinhole monster. Retheming it costs art and no simulation change, and
it yields the campaign's central irony - the player's economy runs on consuming
the thing consuming the galaxy - with the late-game turn already built in,
because harvesting a spreading resource propagates it.

### Why the gate rules are the game

Taken literally, the fiction's constraints are good RTS mechanics:

- One connection per gate at a time, so dialling out locks incoming travel.
- One-way transit, so committing troops through a gate is a real commitment and
  the far side must be held long enough to dial home.
- A dial-in warning, so the defender gets a window to close the iris or mass at
  the gate room.
- Only what fits through the ring travels. The tube system's exclusion of
  aircraft is lore-correct rather than a limitation, and it splits the army into
  expeditionary and garrison forces.

### Why Earth pauses

Managing a hub and expeditions is only unpleasant if the hub can lose while the
player cannot watch it. Earth is therefore paused and never loses in absentia;
expeditions change what is waiting on return. That converts unwatched-base
dread into consequence for choices, and it is also the cheap implementation,
because paused-and-restored-from-save is exactly what the hub mechanic provides.

## Explicitly out of scope

- **Wraith and Atlantis.** Culling beams need mechanics the engine lacks.
- **A four-faction GDI/Nod/SGC/Goa'uld fusion.** See above.
- **Off-world trading.** Needs UI and persistent state the global budget cannot
  carry, and it does not serve the gate fantasy.
- **Generic warp nodes.** A `WarpNode=` building type with a selectable
  destination, no contiguous-path requirement, and one active connection per
  network would be the upstream-worthy generalisation of this mechanic. It is
  deferred: address pads plus one-way tubes deliver the same play without it.
  If built later it belongs in OpenTS as a generic capability, never as
  Stargate-specific code.
- **Larger map support.** Tracked separately in
  [`map-size-changes.md`](map-size-changes.md), which investigates the ceiling
  and stages the change. This mod does not need it and must not block on it:
  256x256 already exceeds every map Tiberian Sun shipped.
- **A dependency on `plans/image-upgrades.md`.** The mod targets classic assets
  and inherits HD rendering later if that plan lands.

## Repository and licensing boundaries

- Engine changes land in OpenTS. Mod content - INI text, maps, and art - lives
  in a separate repository and ships as `ECACHE*.MIX` / `EXPAND*.MIX`.
- No game assets, original binaries, or generated art packs enter either
  repository.
- Stargate is MGM property. This is an unofficial fan modification, must not be
  represented as official or affiliated, and must not be distributed with game
  assets.
