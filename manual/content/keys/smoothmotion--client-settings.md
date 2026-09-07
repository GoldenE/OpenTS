---
key: SmoothMotion
scope: client-settings
label: Motion smoothing
summary: Draws moving objects between their previous and current simulation positions.
see_also: [GameSpeed]
when_omitted:
  kind: unchanged
  note: Retains the current setting, initially enabled.
---

Set `SmoothMotion=no` under `[Video]` in `sun.ini` to draw objects at their current simulation positions. When enabled, the engine draws intermediate positions during the interval between simulation ticks. Aircraft bodies and projectiles have more room to benefit than slow ground vehicles because they travel farther per tick. Selection boxes and health bars use the same positional offset as the object.

The picture trails the simulation by up to one tick. This setting changes drawing and the screen position used to select an object; it does not change movement speed, combat, simulation checksums, or saved coordinates. `GameSpeed=0` disables interpolation. A long stall also temporarily returns drawing to current positions. Changing tick durations can still produce uneven motion.

Teleports reset interpolation immediately. Other movements exceeding 1024 leptons along any coordinate axis in one tick draw at the current position, so unusually fast modded objects may remain unsmoothed.

Aircraft shadows, flying voxel debris, sparks, and some attached drawing paths still use simulation positions. Classic sprite animation frames and locomotor bobbing retain their simulation cadence. [HD artwork](/systems/hd-rendering/) can supply directed subframes sampled from the active animation interval and this render clock. Disabling smoothing pins the intra-tick animation contribution to zero.

The setting is read when options are loaded and has no in-game control. It is not currently written by the settings dialog.
