---
title: Replay recordings of skirmish sessions and honour the sync dump frame
category: fix
release: 0.1.0
targets:
- type: command
  id: launch:record
  effect: changed
- type: command
  id: launch:playback
  effect: changed
---

A recording made in a debug build with `-XX` now carries the session it was made in: the
session type, the game options, and the player list. Playing it back with `-XY` restores
them before the scenario starts. Before, a recording held only the scenario, seed, and
options, so a skirmish recording played back as a campaign with no houses and the engine
faulted while reading the map. Campaign recordings were unaffected by that part.

Playback also reads the `[SyncBug]` block of `sun.ini` on its own. Before, that block was
read only on the way through the multiplayer menus, which playback skips, and the frame to
write the sync dump at defaulted to zero, so every playback wrote its dump and exited at
frame 0. Now the dump is written at the frame `PrintCRC` names and never when the key is
absent.

Playback no longer plays movies, so a replay runs unattended without stalling when the
window is not in front, the same way a multiplayer session already skipped them.

The recording file opens with a tag naming its layout. A file written before this change
has no tag and is refused with a line in the debug log, after which the game continues to
the menu. Recordings are debugging artifacts and carry no promise across development
snapshots.
