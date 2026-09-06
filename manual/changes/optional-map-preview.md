---
title: Allow map selection without a preview image
category: fix
release: 0.1.0
targets:
- type: format
  id: scenario-terrain
  effect: changed
---

Selecting a custom map without a preview no longer creates a zero-sized preview surface and divides by zero while scaling it. Missing preview packs and nonpositive image dimensions now leave the selection picture empty. Valid preview images retain their existing drawing behavior; the map format and terrain data are unchanged.
