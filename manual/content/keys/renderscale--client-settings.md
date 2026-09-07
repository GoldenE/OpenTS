---
key: RenderScale
scope: client-settings
label: Framebuffer density
summary: Multiplies the logical frame dimensions to choose the HD framebuffer dimensions.
see_also: [RenderMode, ScreenWidth, ScreenHeight]
when_omitted:
  kind: value
  value: '2'
  note: Classic mode uses density one regardless of this setting.
---

Set an integer from `1` through `4` under `[Video]` in `sun.ini`. With `RenderMode=HD`, a logical 1280 by 800 frame at density two is composed into 2560 by 1600 pixels, preserving the field of view and logical selection coordinates. Final window fitting still happens after composition.

Each physical dimension is limited to 16384 pixels, and one surface allocation is limited to 1 GiB. An out-of-range scale selects Classic mode. A valid scale can still exceed the framebuffer limits when combined with a large logical resolution. See [HD rendering](/systems/hd-rendering/) for memory costs and fallback behavior.
