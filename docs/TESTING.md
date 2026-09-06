# Runtime testing

> [!IMPORTANT]
> A build result is not runtime evidence. This document owns how runtime
> behavior is established: a simulation replay harness that turns "did I change
> the simulation?" into a file comparison, and a play-through checklist for
> everything the simulation checksum cannot see.

Everything here needs a populated `Run/` tree built from legitimately owned
Tiberian Sun data. None of it runs in continuous integration, and nothing it
produces is committed: recordings, sync dumps, saves, and manifests live under
the ignored `baseline/` directory. `AGENTS.md` records the local install used as
the data source and the rule that no repository file may require it.

## The replay harness

A Debug build records a session to `Run/RECORD.BIN` when launched with `-XX`
and replays it when launched with `-XY`. During playback the engine computes a
whole-game CRC every frame and, when `[SyncBug] PrintCRC=<frame>` is set in
`Run/SUN.INI`, writes `Run/SYNC<house>.TXT` on reaching that frame and exits.
The dump holds the last 256 frame CRCs and a per-house listing of every object's
position, facing, and targets. Replaying a fixed recording on two builds and
comparing the dumps names the first frame at which the simulations diverged.

Both switches exist only in Debug builds. Playback skips movies and replays recorded input, but focus handling can wait for the window or recapture the mouse. Run it only when the desktop is available.

### Golden set

The golden set is a manifest, four recordings, and their sync dumps under
`baseline/golden/`, captured on 2026-09-04 from commit `59fae72` plus the
playback fixes recorded in the manual change
`recording-playback-session`, all replayed at frame 300:

| Session | Scenario | Content |
| --- | --- | --- |
| `ts-gdi01` | `GDI1A.MAP` | Tiberian Sun GDI mission 1, normal, two move orders |
| `ts-nod01` | `NOD1A.MAP` | Tiberian Sun Nod mission 1, normal, one move, one build queued |
| `fs-gdi01` | `FSGDI01.MAP` | Firestorm GDI mission 1, normal, one move order |
| `skirmish-gcanyon` | `G_CANYON.MAP` | Grand Canyon, GDI vs one AI, unit count 10, speed 3 |

Every session replayed identically on two independent runs of the same build,
and the campaign sessions replayed identically both foregrounded and minimized.
Campaign recordings therefore carry golden dumps alongside skirmish ones; a
campaign replays with `CAMPAIGN_NONE`, which did not change the first 300
frames of these three missions. LAN multiplayer is not in the set.

### Running it

```powershell
.\tools\baseline\Invoke-VanillaBaseline.ps1            # compare against golden
.\tools\baseline\Invoke-VanillaBaseline.ps1 -Capture   # write new golden dumps
.\tools\baseline\Invoke-VanillaBaseline.ps1 -Session ts-gdi01
```

The driver replays each session against `Run/GameD.exe`, stores the dumps and a
report under `baseline/runs/<timestamp>/`, and exits non-zero on any difference,
timeout, missing dump, or missing file. It ignores the dump's build-identity
header (version, commit, CPU vendor, average frame rate, network statistics)
and reports the first differing comparable line plus the earliest divergent CRC
frame. A tampered golden entry is detected; that negative control was run before
the set was accepted. [tools/baseline/README.md](../tools/baseline/README.md)
owns the driver's usage details.

Capture a new golden set only when a change to the simulation is intentional,
and say so in the change that captures it. A recording is only meaningful
against the data it was made from, so the data manifest under
`baseline/manifest/` travels with it.

### Recording a session

Recording needs a person or the developer harness at the controls: menus and
the tactical view hit-test against the real cursor. Launch
`Run/GameD.exe -WIN -XX`, set up and play the session, abort back to the menu,
and copy `Run/RECORD.BIN` next to the manifest. Set the skirmish game speed
below the fastest position; the fastest setting runs the simulation uncapped
(about 1,000 frames per second on the capture machine), which is unplayable and
makes the recording finish in seconds.

## Developer harness

`tools/dev/OpenTS.Dev.psm1` launches a built configuration from `Run/`, finds
the debug log the run created, waits for log lines, captures the game window to
PNG through `PrintWindow` without bringing it to the front, and drives it:
clicks at logical game pixels, key presses, dialog buttons by caption, and
dialog trackbars by control id. Import it and read `Get-Help Start-OpenTS`.

Menu and tactical hit tests read the system cursor, so a click moves the real mouse. Focus-loss handling also waits for focus during both campaign and skirmish playback, and restoring focus can recapture the mouse. Run recording and replay sessions only when the desktop is available; a minimized launch is not a guarantee of background-safe execution.

## Isolated local LAN clients

Debug builds support an explicit local-only developer mode through `OPENTS_DEBUG_LAN_PORTS=<local>,<peer>`. Both ports must be distinct decimal numbers from 1024 through 65535; whitespace, missing values, and malformed values stop startup. With the variable absent, startup mutexes and networking retain their normal behavior. Release builds ignore the variable.

The LAN UDP transport binds only `127.0.0.1`, sends discovery and game packets only to the configured peer endpoint, and rejects other source endpoints. It preserves the ordinary LAN lobby, packet framing, version negotiation, and game queues. Each local port receives its own application/autoplay mutex names. It does not enable adapter broadcasts or change Windows interfaces or firewall settings. This transport restriction is not a sandbox for unrelated WOL COM or TCP services; use the LAN menu for this test.

Use two separate, locally populated runtime directories with matching game data and Debug binaries. The process changes its working directory to its executable directory, so separate executables also isolate `SUN.INI`, logs, saves, and recordings. Set distinct player names in the LAN lobby. Launch the two clients from separate developer PowerShell sessions when the desktop is available:

```powershell
$env:OPENTS_DEBUG_LAN_PORTS = '45001,45002'
& 'C:\path\to\peer-a\GameD.exe' -WIN
```

```powershell
$env:OPENTS_DEBUG_LAN_PORTS = '45002,45001'
& 'C:\path\to\peer-b\GameD.exe' -WIN
```

Unset the variable with `$env:OPENTS_DEBUG_LAN_PORTS = $null` before an ordinary launch. These are developer QA commands requiring desktop control; they are not an automated background-safe test.

Acceptance uses two x64 clients to join and play through the real lobby, then a Win32/x64 pair to verify live rejection. Retain both debug logs and captures under `baseline/`. The host compares the architecture-stamped build identity before version-range negotiation, so an incompatible pair can report a general game mismatch. A successful local session establishes the real handshake and simulation path over loopback; it does not establish physical-LAN broadcast discovery or router behavior. `LocalLANTest` verifies the opt-in parser, unchanged default mutex names, instance separation, and exact self/peer endpoint classification. `LocalLANTransportTest` executes production LAN configuration, socket setup, send, and receive function bodies against socket substitutes to check binding, discovery destinations, tunnel rejection, and unchanged default behavior. Neither automated test opens sockets.

## Movie audio diagnostics

Set `OPENTS_VQA_AUDIO_TRACE=1` for a Debug or Release launch to record bounded VQA audio diagnostics in the game log. This reports DirectSound buffer creation, locking, playback, volume-setting results and volume readback, plus decoded and submitted PCM summaries. It does not change volume, record raw audio, or inspect other applications.

Each process emits at most 24 DirectSound operation records and eight PCM summaries for each of the decoded and submitted phases. A scan reads at most 16 KiB of the supplied data and reports sample count, format, extrema, and nonzero count. The first seven fills are recorded; if they are silent, the final record is reserved for the first later nonzero fill. Clear the variable before an ordinary launch. `VQAAudioTraceTest` checks the enabled and disabled behavior, sample interpretation, scan limit, and record budgets.

A nonzero decoded buffer establishes decoder output, and a successful DirectSound call establishes API acceptance. Neither alone establishes audible output. Correlate the movie timeline with a meter attributed to the game process or a listening check, and keep an unavailable audio-output observation explicit.

## Play-through checklist

The checklist covers what a CRC dump cannot observe. Each item states the
evidence it produces. A reported result must include the run's
`Run/Debug/DEBUG_*.LOG` and any `Run/Exceptions/` folder the run created.

The reference pass below was taken on 2026-09-04 against Steam build 15918072
of the retail data (see `baseline/manifest/README.md`) with Win32 Debug and
Release builds of commit `59fae72` on the toolchain recorded in
[Building OpenTS](BUILDING.md). "Not exercised" means exactly that.

| Item | Evidence | Debug | Release |
| --- | --- | --- | --- |
| Launch to main menu | log banner names the build; menu screenshot | Pass | Pass |
| Expansion select and both menu themes | Firestorm and Tiberian Sun menus render | Pass | Pass |
| Campaign mission loads | scenario read in log; map and objective text render | Pass (`FSGDI01`, `GDI1A`, `NOD1A`) | Pass (`FSGDI01`) |
| Units respond to orders | unit moves after a move order | Pass | Pass |
| Skirmish loads and runs | scenario read; AI creates teams; score screen on defeat | Pass (`G_CANYON`) | Pass (`G_CANYON`) |
| Abort mission to menu | menu returns; no crash folder | Pass | Pass |
| Clean exit from menu | process ends; no crash folder | Pass | Pass |
| Windowed rendering and DPI | window client is the configured size at 240 DPI; captures match | Pass | Pass |
| Music and sound effects | audio heard at menu and in play | Not exercised (no listener) | Not exercised |
| Movie playback | intro and mission movies play | Pass (paused while unfocused) | Pass |
| Save and load round-trip | save mid-mission, reload, state matches | Not exercised | Not exercised |
| Mission briefing and restate | briefing text and restate dialog | Not exercised | Not exercised |
| Campaign carryover | next mission starts with carried state | Not exercised | Not exercised |
| Full-screen mode | borderless window covers the display | Not exercised | Not exercised |
| Hotkeys and options dialogs | keyboard commands and the options screens | Not exercised | Not exercised |
| Radar and sidebar interaction | build queue, radar clicks | Not exercised | Not exercised |
| LAN multiplayer | two clients play without desync | Not exercised |  Not exercised |

### Baseline observations

Defects and surprises found while establishing the baseline are recorded here,
not fixed as part of it, except for the playback fixes the harness could not
exist without.

- The skirmish dialog's game speed slider opens at its fastest position on
  every visit and does not persist. That position maps to `GameSpeed=0`, an
  uncapped simulation, which ran at roughly 1,000 frames per second and lost a
  skirmish to the AI at frame 25,307 after 26 real seconds.
- Recording playback could not work before the playback fixes: a skirmish
  recording faulted on a null player house while reading the map, and every
  playback wrote its sync dump at frame 0 and exited. The manual change record
  `recording-playback-session` documents the fix.
- The game window pauses movies and the skirmish dialog whenever it loses
  focus, so a launched game that is not in front stalls at its intro movie.
- `Check_For_Focus_Loss` in `code/mainloop.cpp` waits while unfocused for both `GAME_NORMAL` and `GAME_SKIRMISH`; `Focus_Restore` in `code/winstub.cpp` recaptures a previously captured mouse. Historical completed skirmish replays do not establish safe execution during other desktop use.
