---
key: WorldArtScale
scope: client-settings
label: World artwork density
summary: Selects the preferred density of world artwork independently of the framebuffer density.
see_also: [RenderMode, RenderScale, UIArtScale]
when_omitted:
  kind: computed
  note: Uses the configured RenderScale, initially two.
---

Set an integer from `1` through `4` under `[Video]` in `sun.ini`. This chooses a world asset variant; it does not enlarge objects or change map geometry. A selected variant is resampled to the framebuffer density when the two densities differ. The assignment has no effect in Classic mode.

An out-of-range value selects Classic mode. [HD rendering](/systems/hd-rendering/) owns the source-precedence and missing-variant rules.
