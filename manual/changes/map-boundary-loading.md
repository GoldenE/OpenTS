---
title: Load maps at the cell-table boundary
category: fix
release: 0.1.0
targets:
- type: key
  id: Size
  effect: changed
---

Maps at the existing cell-table limit, including 256×256, no longer index beyond the pointer table when a full-map iterator finishes its final row. Iteration returns the last valid cell and then terminates without forming an out-of-range pointer. This fixes loading at the existing limit; it does not raise the size ceiling or change saved cell coordinates.
