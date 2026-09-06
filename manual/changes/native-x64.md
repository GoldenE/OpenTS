---
title: Add native x64 Windows builds
category: feature
release: 0.1.0
targets:
- type: format
  id: save-games
  effect: changed
---

OpenTS adds native x64 Windows builds alongside Win32 in Debug and Release. The x64 build uses portable C++ implementations of the retired x86 assembly routines and keeps pointer-bearing runtime state at its native width. The supported compiler remains Visual Studio 2022.

Win32 and x64 save games and network sessions carry different architecture stamps. An incompatible save is rejected before the active session is changed, and LAN and online-lobby version negotiation distinguish the two architectures. Existing Win32 games can be finished with the Win32 executable; no cross-architecture save converter is provided. Game-data formats and logical map geometry retain their existing widths and units.
