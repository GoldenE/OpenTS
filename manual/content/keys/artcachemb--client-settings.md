---
key: ArtCacheMB
scope: client-settings
label: Decoded artwork cache
summary: Bounds the decoded HD artwork cache in mebibytes.
see_also: [RenderMode, RenderScale]
when_omitted:
  kind: value
  value: '256'
---

Set an integer from `1` through `1024` under `[Video]` in `sun.ini`. One unit is 1048576 bytes. The cache evicts unused assets to make room; an asset in use by a draw remains pinned. If a new asset cannot fit, its Classic artwork is used.

This limit covers decoded HD assets, not the framebuffers, voxel raster cache, movie buffers, or total process memory. An out-of-range value selects Classic mode. The setting is read at startup.

Additional retained rendering storage is bounded separately:

| Storage | Retained limit |
| --- | --- |
| Decoded and scaled Classic sprite rows | Smaller of 64 MiB and one quarter of this setting; at most 4096 entries |
| Reusable interface mask surfaces | Smaller of 16 MiB and one sixteenth of this setting; at most eight surfaces |
| Reusable raster row scratch | Four nested slots, at most 256 KiB each |

Uncached, oversized, or deeply nested drawing work can use temporary buffers released when the draw ends. Classic raster-cache pressure rebuilds the same pixels; it does not drop a draw or reduce its density.
