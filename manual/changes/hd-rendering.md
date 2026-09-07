---
title: Render higher-density artwork
category: feature
release: 0.1.0
targets:
- type: system
  id: hd-rendering
  effect: added
- type: format
  id: hd-pack
  effect: added
- type: key
  id: RenderMode
  effect: added
- type: key
  id: RenderScale
  effect: added
- type: key
  id: WorldArtScale
  effect: added
- type: key
  id: UIArtScale
  effect: added
- type: key
  id: ArtCacheMB
  effect: added
---

OpenTS adds an opt-in HD rendering mode with denser framebuffers, optional higher-density artwork, directed animation subframes, and finer voxel projection. Classic mode remains the default. Logical map geometry, object sizes, simulation timing, and input coordinates retain their existing units.

HD artwork is selected within the existing winning asset source and falls back to its Classic asset when unavailable or rejected. Mod overrides retain their precedence. Framebuffer density, preferred world and interface artwork densities, and the decoded artwork cache budget are configured in `sun.ini`; see [HD rendering](/systems/hd-rendering/).
