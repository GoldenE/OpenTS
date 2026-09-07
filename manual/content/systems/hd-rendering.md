---
title: HD rendering
summary: Renders more physical samples while retaining logical object sizes, map geometry, and gameplay state.
category: rendering-presentation
keys: [RenderMode, RenderScale, WorldArtScale, UIArtScale, ArtCacheMB, SmoothMotion]
---

HD rendering separates the logical game image from the physical framebuffer. The logical image controls the field of view, interface layout, and mouse hit tests. Density two gives each logical pixel a two-by-two physical area. World positions are projected directly into that denser image, so subpixel movement can survive projection.

## Enable HD rendering

Close the game and add these assignments:

```ini title="sun.ini"
[Video]
RenderMode=HD
RenderScale=2
```

World and interface artwork densities follow the framebuffer density unless overridden. An HD pack can replace individual assets while the remaining assets use Classic fallback. The mode does not create new detail in a Classic sprite: that requires denser source artwork. Existing voxels can gain detail from denser projection and finer render-facing buckets.

## Artwork and precedence

The existing asset lookup chooses the winning source before HD resolution selection. A Classic mod replacement therefore takes precedence over an HD rendition in a lower-priority source. Loose-first file readers and cached-MIX readers retain their distinct lookup orders.

The HD sidecar belongs to the same source as the selected Classic asset and carries its SHA-256 digest. Missing, malformed, unsupported, or mismatched sidecars fall back to that Classic asset. Logical dimensions, frame counts, animation sequences, map properties, and model attachments continue to come from their semantic owners.

## Animation and gameplay

An artwork sequence can supply several directed subframes per logical frame. Drawing samples the animation owner's current countdown interval and the render clock; it never advances the logical stage or predicts a future one. Reverse and ping-pong sequences require direction-specific artwork. At an expired interval, drawing holds the terminal subframe until simulation advances.

HD settings and render samples stay outside saved options and simulation state. Installing artwork does not change object footprints, pathfinding, targeting, stage rates, replay events, or network compatibility. Classic mode retains the legacy asset and draw paths.

## Resource costs

Density two uses four times as many framebuffer samples; density four uses sixteen times as many. Color surfaces, depth and alpha buffers, voxel projection, and cached compositions contribute separate costs. The decoded artwork cache limit is not a limit on total memory. Lower `RenderScale` or return to Classic when the chosen resolution and density exceed available resources.

Movie decoding, map previews, radar semantics, and source-image metrics retain their own logical dimensions. Final presentation filters and window fitting operate on the composed frame.
