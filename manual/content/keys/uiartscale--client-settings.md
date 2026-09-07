---
key: UIArtScale
scope: client-settings
label: Interface artwork density
summary: Selects the preferred density of interface artwork independently of world artwork.
see_also: [RenderMode, RenderScale, WorldArtScale]
when_omitted:
  kind: computed
  note: Uses the configured RenderScale, initially two.
---

Set an integer from `1` through `4` under `[Video]` in `sun.ini`. This chooses an interface asset variant while keeping its logical canvas and hit targets. It does not change text size, dialog dimensions, or the framebuffer density. The assignment has no effect in Classic mode.

An out-of-range value selects Classic mode. See [HD rendering](/systems/hd-rendering/) for mixed Classic and HD artwork.
