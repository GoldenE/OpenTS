---
title: Compatibility and save games
summary: How OpenTS lists and loads save games, and which save games a build accepts.
category: compatibility-migration
source_files:
  - README.md
  - code/loaddlg.cpp
  - code/saveload.cpp
  - code/savever.cpp
  - code/architecture.hh
related:
  - type: using
    id: project-status
---

OpenTS uses the English Tiberian Sun 2.03 release as its inherited data and behavior baseline.

Every save file's header carries an internal version stamp. The load dialog lists a save only when its stamp matches the version the current build expects; files with any other stamp do not appear, and the multiplayer network save file is never listed. A save that reaches the engine without passing through the dialog is checked the same way and refused if its stamp does not match. A listed save that was not made in a campaign is marked with a `*` before its description.

OpenTS does not read save games written by the vanilla game, by a different OpenTS release-cycle version, or by the other target architecture. Win32 and x64 saves are separate, and no converter is provided. Development snapshots within one cycle and architecture share a version stamp, so a save from an older snapshot can appear in the list even though loading it is unsupported and may fail. Finish or abandon a game in progress before replacing a development snapshot.

A save game stores each object member by member rather than as a copy of its memory.

Network players must use the same architecture as well as a compatible release. Debug recordings additionally require the architecture and configuration that wrote them; they are development artifacts rather than portable save games. Keep the Win32 executable for finishing an existing Win32 game when moving new sessions to x64.
