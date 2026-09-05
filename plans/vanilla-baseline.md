# Vanilla runtime baseline for OpenTS

## Summary

Establish a reproducible record of how OpenTS behaves against unmodified
Tiberian Sun 2.03 Firestorm data, captured from a pinned commit, so that later
engine work has something to regress against.

The repository can currently prove that the engine *compiles*: CI builds Win32
Debug and Release and runs a CTest suite whose only member is
`tests/logstress`. `docs/BUILDING.md` states plainly that a build result is not
runtime evidence, and that runtime behavior "is established separately, by play
testing". Nothing in the tree records what that play testing covered or what it
produced, so there is no artifact a later change can be compared against.

The engine already contains the mechanism this needs. A Debug build records a
session to disk with `-XX` and replays it with `-XY` (`code/init.cpp:1910`,
`code/init.cpp:1917`, both inside the `#ifdef _DEBUG` block). The recording
hooks fire from the campaign and skirmish queue (`code/queue.cpp:550`) as well
as the multiplayer queue (`code/queue.cpp:920`). During playback the engine
computes a whole-game CRC every frame, and `[SyncBug] PrintCRC=<frame>` in
`sun.ini` makes it write a `SYNC<n>.TXT` dump and exit on reaching that frame
(`code/queue.cpp:4126`, `code/session.cpp:532`, `Print_CRCs` at
`code/queue.cpp:4334`). Replaying a fixed recording on a changed build and
diffing the dump therefore turns "did I change the simulation?" into a file
comparison that names the exact frame of divergence.

That covers the simulation and nothing else. Rendering, audio, video playback,
menus, input, resolution handling, and save/load round-trips produce no CRC, so
they are covered by a written play-through checklist instead. The two together
are the baseline.

### Scope boundaries

- The harness depends on legitimately owned game data and therefore can never
  run in continuous integration. `AGENTS.md` forbids any repository file, build
  step, or automated check from requiring the local retail install, and that
  rule is not relaxed here. The harness is a local contributor tool.
- No game asset, recording, save, or captured dump is ever committed. Captured
  artifacts live in ignored directories.
- This plan adds no engine behavior. If a defect is found while establishing
  the baseline it is recorded as a baseline observation, not fixed here.

### Status (2026-09-04)

Executed through Phase 8 except the commit. The "no engine behavior" boundary
was crossed once, deliberately, and is flagged for review: Phase 4 proved that
recording playback could not work at all in the inherited tree (a skirmish
replay faulted on a null player house because the recording never stored the
session, and every replay dumped at frame 0 because the trap frame defaulted to
zero on the playback path). Without a fix Phases 4 through 6 had no deliverable,
so the minimal debug-path fix was made (`code/init.cpp`, `code/session.cpp`,
`code/session.h`, `code/movie.cpp`) and recorded as the manual change
`recording-playback-session`. Everything else found is a baseline observation
in `docs/TESTING.md`.

## Implementation tracker

### Phase 1: Stage the run tree

**Files touched:** `Run/` (ignored, untracked)
**Depends on:** nothing

- [x] Confirm `.gitignore` still ignores `/Run/*` except the tracked
      `place_steam_build_here` marker before copying anything into it.
- [x] Copy the Tiberian Sun data files from the local retail install into
      `Run/`, treating the install as read-only. (Already staged; all 148 data
      files verified byte-identical to the retail copies by SHA-256.)
- [x] Exclude the original `Game.exe`, `SUN.EXE`, `Language.dll`, and the
      bundled `ddraw.dll` / `wsock32.dll` compatibility wrappers, so that no
      original binary sits in the run tree and no DirectDraw wrapper interposes
      on the bgfx renderer.
- [x] Exclude the install's existing `SAVE*.SAV` files and stale logs so that
      save/load testing starts from a known-empty state.
- [x] Record the exact copied file set and the install's build date as the
      baseline data manifest. (`baseline/manifest/README.md`,
      `run-data-files.csv`; Steam build 15918072, data dated 2024-03-10.)

### Phase 2: Establish the toolchain and pin the baseline build

**Files touched:** none (local toolchain and build directory only)
**Depends on:** nothing; may proceed alongside Phase 1

- [x] Install Visual Studio 2022 with the Desktop development with C++
      workload, a Windows SDK, and CMake 3.23 or newer, per `docs/BUILDING.md`.
      (Build Tools 17.14.39 plus the ATL and MFC components the tree turned
      out to need; `docs/BUILDING.md` now says so. CMake 4.4.3.)
- [x] Fetch submodules recursively so the vendored bgfx dependency is present.
      (Already present at v1.147.9339-555.)
- [x] Record the commit that the baseline is pinned to, and confirm the working
      tree is clean so the build stamp does not report `modified`. (Pinned to
      `59fae72`. The tree was not clean: `AGENTS.md` carried an uncommitted
      section and later the playback fix; the stamp reads `modified` and
      `baseline/manifest/build.md` records exactly what differed.)
- [x] Configure and build Win32 Debug and Release from that commit, and record
      the exact commands, toolchain versions, and results.
      (`baseline/manifest/build.md`.)
- [x] Confirm both configurations copied their executable and `Language.dll`
      into `Run/`, noting that the second build replaces the first's
      `Language.dll`.

### Phase 3: First-run smoke test

**Files touched:** none (produces captured artifacts only)
**Depends on:** Phases 1 and 2

- [x] Launch the Release build from `Run/` with `-XC` so the debug console is
      open from startup, and capture the resulting `Debug/DEBUG_*.LOG`.
      (`Run/Debug/DEBUG_04-09-2026_11-03-14.LOG`.)
- [x] Confirm from the log banner that the running build is the pinned commit
      and is not marked modified. (Banner: `59fae72 on main (modified)`; the
      modification was the `AGENTS.md` documentation section only.)
- [x] Confirm the bootstrap mixfile sequence completes and the main menu
      reaches an interactive state.
- [x] Start and briefly play one campaign mission and one skirmish match,
      confirming the mission loads, units respond, and the session can be
      exited cleanly. (`FSGDI01` and `G_CANYON` on Release.)
- [x] Launch the Debug build the same way and confirm it reaches the same state
      without an assertion report. (`G_CANYON` skirmish on Debug; no
      assertion, no crash folder.)
- [x] Record every observed defect, each with its log and any `Exceptions/`
      folder, as a baseline observation rather than fixing it here. (The
      "Baseline observations" section of `docs/TESTING.md`.)

### Phase 4: Validate the record and playback harness

**Files touched:** none (produces captured artifacts only)
**Depends on:** Phase 3

- [x] Record a short skirmish session with the Debug build using `-XX`, and
      identify where the recording file is written. (`Run/RECORD.BIN`, fixed
      name from `code/session.cpp:213`.)
- [x] Replay that recording with `-XY` and confirm playback runs rather than
      falling back to a live session, which `code/init.cpp:1104` does when the
      recording cannot be opened. (First attempt crashed: access violation at
      `code/scenario.cpp:3414`, `PlayerPtr` null, because the recording never
      stored `Session.Type` or the player list and `SessionClass::Save`/`Load`
      were declared but never defined. Fixed; see Status.)
- [x] Set `[SyncBug] PrintCRC` in `sun.ini` to a frame inside the recording and
      confirm the run writes `SYNC<n>.TXT` and exits at that frame. (Before the
      fix every replay dumped at frame 0: `TrapPrintCRC` defaulted to 0 and the
      playback path never read `[SyncBug]`. After the fix, dump at frame 300.
      The file is `SYNC<house>.TXT`, so a Nod session writes `SYNC1.TXT`.)
- [x] Replay the same recording a second time on the identical unchanged build
      and confirm the two dumps are identical apart from the version, commit,
      and build-date lines that `Print_CRCs` writes into the header. (Identical
      in full, 2,476 lines, for the skirmish; 3,025 lines for the campaign.)
- [x] If the two runs differ, stop and record what varies; an unstable dump
      makes the harness worthless and must be understood before any golden
      output is captured. (They did not differ.)
- [x] Determine whether campaign recordings replay as reliably as skirmish
      recordings, and record the answer, since the `[SyncBug]` block is read
      only as a session outside a campaign is set up (`code/session.cpp:532`).
      (They do: three campaign missions replayed identically, foregrounded and
      minimized, once playback reads `[SyncBug]` itself and skips movies.)

### Phase 5: Capture the golden baseline

**Files touched:** `baseline/` (ignored, untracked)
**Depends on:** Phase 4

- [x] Add an ignore rule for the captured baseline directory so no recording,
      dump, or save can be committed. (`/baseline/` in `.gitignore`.)
- [x] Choose the baseline session set: one GDI campaign mission, one Nod
      campaign mission, one Firestorm mission, and at least one fixed-map
      skirmish, restricted to the session types Phase 4 proved replayable.
      (`GDI1A`, `NOD1A`, `FSGDI01`, `G_CANYON`.)
- [x] Record each session on the pinned Debug build and store the recording
      alongside the map, rules, and any INI it depends on, since a recording is
      only meaningful against the data it was made from. (`baseline/golden/`;
      the maps and rules are inside the archives the data manifest hashes.)
- [x] Capture the golden `SYNC` dump for each recording at a fixed, recorded
      `PrintCRC` frame. (Frame 300 for all four.)
- [x] Write a manifest naming the pinned commit, the toolchain, the data
      manifest from Phase 1, and the `PrintCRC` frame used for each session.
      (`baseline/golden/manifest.json`.)

### Phase 6: Automate replay and comparison

**Files touched:** `tools/baseline/Invoke-VanillaBaseline.ps1`,
`tools/baseline/README.md`
**Depends on:** Phase 5

- [x] Write a PowerShell driver that replays each recording in the manifest
      against a nominated build and writes the resulting dumps to a run
      directory. (Runs minimized and unattended; all four sessions `MATCH` in
      about 50 seconds.)
- [x] Make the comparison ignore the build-identity header lines and report the
      first differing frame for each session. (Negative control: a tampered
      `CRC[44]` in a golden dump reported `DIFF` at frame 300.)
- [x] Make the driver fail with a clear message, rather than a misleading pass,
      when the run tree is unpopulated, the build is a Release build, or a
      recording named in the manifest is missing.
- [x] Keep the driver out of the CTest suite, and state in its README that it
      requires proprietary data and cannot run in continuous integration.

### Phase 7: Manual play-through checklist

**Files touched:** `docs/TESTING.md`
**Depends on:** Phase 3

- [x] Write the checklist covering what a CRC dump cannot observe: rendering
      and theater art, audio and speech, video playback, menus and dialogs,
      input and hotkeys, windowed and fullscreen resolution handling, save and
      load round-trips, mission briefings, and campaign carryover.
- [x] State the evidence each item produces, and require the debug log and any
      crash folder to accompany a reported result.
- [x] Record the baseline result for every item from the pinned build, so the
      checklist ships with a filled-in reference pass rather than as an empty
      form.
- [x] State which items were not exercised, rather than leaving them ambiguous.
      (Nine items are marked not exercised: audio, save/load, briefings,
      carryover, full-screen, hotkeys, sidebar, LAN.)

### Phase 8: Documentation and handoff

**Files touched:** `docs/TESTING.md`, `docs/README.md`, `docs/BUILDING.md`,
`CONTRIBUTING.md`, `.gitignore`
**Depends on:** Phases 6 and 7

- [x] Make `docs/TESTING.md` the single owner of runtime validation procedure,
      covering both the CRC harness and the manual checklist, and link to the
      driver rather than restating its usage.
- [x] Link the new document from the `docs/README.md` index.
- [x] Link to it from the sentence in `docs/BUILDING.md` that defers runtime
      behavior to separate play testing, and from the runtime-evidence
      expectations in `CONTRIBUTING.md`, without copying the procedure into
      either.
- [x] Confirm the ignore rules keep every captured artifact untracked, and
      inspect the final diff for stray data files.
- [x] Commit the tracked changes with an imperative subject of at most 72
      characters. (Committed as `042818a` and `0a14ef1` on the
      `vanilla-baseline` branch; the engine fix the plan did not authorize is
      flagged for review in the pull request.)

## Open questions

- ~~Whether campaign sessions replay deterministically enough to carry golden
  dumps.~~ Answered in Phase 4: they do, and three carry golden dumps.
- Whether LAN multiplayer joins the baseline set. It is the least tested area
  per `README.md` and the most valuable to pin, but it needs two machines or
  two instances, so it is deliberately left out of Phase 5 until the
  single-instance harness is proven. The single-instance harness is now proven;
  the recording format stores the player list, so a LAN recording is the next
  candidate.
- Whether the engine should gain a debug-only auto-start switch (scenario or
  skirmish from the command line, no focus wait) so recordings can be made
  unattended as well as replayed. Recording still needs the real cursor.
