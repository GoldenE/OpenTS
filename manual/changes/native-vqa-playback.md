---
title: Keep native video initialization and row addressing correct
category: fix
release: 0.1.0
targets:
- type: format
  id: vqa
  effect: changed
---

VQA playback accepts the audio initialization structure at its native pointer width. The x64 structure no longer fails the old fixed-size check before a movie starts.

Video block drawing also keeps backward row adjustments as pointer subtraction. On x64, those adjustments no longer become nearly four-gigabyte forward jumps that leave only a small strip of the movie drawn. Win32 output and the VQA data format are preserved.
