---
title: Reject recordings from a different architecture or configuration
category: fix
release: 0.1.0
breaking: true
migration:
- Recreate older Release recordings with the selected architecture and configuration; their ambiguous OTSREC headers are no longer accepted by Release.
targets:
- type: command
  id: launch:playback
  effect: changed
---

Recording playback checks the architecture and build configuration before reading any session fields. This prevents a recording from another architecture, or a Debug recording with its extra header field, from being interpreted as the local layout.

Win32 Debug keeps its existing recording tag, so current Win32 Debug baseline recordings remain readable. x64 Debug and both Release architectures have distinct tags. Recordings remain development artifacts without a compatibility promise across snapshots.
