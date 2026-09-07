---
key: RenderMode
scope: client-settings
label: Rendering mode
summary: Selects Classic rendering or a denser framebuffer with optional HD artwork.
see_also: [RenderScale, WorldArtScale, UIArtScale, ArtCacheMB]
when_omitted:
  kind: value
  value: Classic
---

Set `RenderMode=HD` in `[Video]` in `sun.ini` to enable [HD rendering](/systems/hd-rendering/). `Classic` uses the legacy artwork and a framebuffer at the logical resolution. Mode names are case-insensitive; other strings select Classic.

The setting takes effect at startup. Invalid scale or cache settings also select Classic. The settings dialog does not write these assignments, so edit `sun.ini` while the game is closed.
