---
title: Preserve incompressible packed map blocks
category: fix
release: 0.1.0
targets:
- type: format
  id: scenario-terrain
  effect: changed
---

LCW-compressed terrain and overlay blocks that expand during compression no longer overwrite their working buffers. The readers and writers reserve room for the complete encoded block, including a partial final block, while preserving the existing bytes and 8 KiB decoded block size. Blocks whose header exceeds the configured storage are rejected before copying their data.
