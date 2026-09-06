---
title: Interpolate moving objects between simulation ticks
category: feature
release: 0.1.0
targets:
- type: key
  id: SmoothMotion
  effect: added
---

`SmoothMotion` adds interpolation of object drawing between simulation ticks, enabled initially. Moving bodies, selection boxes, and health bars share the positional offset. The picture can trail the simulation by up to one tick; movement speed and combat timing are unchanged. The added previous-position state is excluded from saves and simulation checksums, and loading restores it from each object's saved position.

Set `SmoothMotion=no` in `[Video]` in `sun.ini` to restore drawing at current simulation positions. Some dependent effects, including aircraft shadows, still step at the simulation rate; see the setting's reference for the current limits.
