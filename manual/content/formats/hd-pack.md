---
format_id: hd-pack
title: HD artwork sidecars
summary: Stores versioned visual variants bound to the exact Classic asset selected by its existing loader.
kind: binary
source_files:
- code/hdasset.hh
- code/hdasset.cpp
- code/hdruntime.cpp
- code/hdshape.cpp
extensions:
- .HDP
role: image
related:
- type: system
  id: hd-rendering
- type: guide
  id: hd-artwork
---

A sidecar appends `.HDP` to the complete selected name: `UNIT.SHP.HDP`, for example. It must be beside the selected loose file or a member of the exact winning MIX archive that supplied the Classic asset. Its canonical name and SHA-256 digest must match that asset. The digest binds content, not a filename in another archive.

## Binary version 1

All integers use explicit little-endian widths. A `u32` occupies four bytes; signed fields use two's-complement bit patterns. A `blob` is a u32 byte count followed by those bytes without padding. C++ structure layouts, pointers, and host word size never enter the representation. Unknown versions and trailing bytes are rejected.

| Record | Ordered fields |
| --- | --- |
| Pack | u32 magic `0x5048444f` (bytes `ODHP`), u32 version 1, u32 kind, blob canonical ASCII name, blob 32-byte SHA-256, u32 variant count, variants |
| Kind | 0 shape, 1 UI, 2 terrain, 3 voxel, 4 palette, 5 font, 6 movie |
| Variant | u32 scale, logical width, logical height, logical frame count, facing count; nine signed 32-bit model components; blob VXL; blob motion; u32 sequence count, sequences; u32 frame count, frames |
| Sequence | u32 first logical frame, count, temporal multiplier, reverse flag (0 or 1) |
| Frame | u32 logical frame, subframe, facing; signed 32-bit direction, x, y; u32 width, height; blob RGBA, remap, shadow, signed-int16 depth |

Kind identifiers reserve distinct visual families; a declared kind does not create a consumer. Raster shape, interface, terrain, and voxel adapters accept their matching kinds. SHP-backed fonts accept the raster `FONT` kind and use `UIArtScale`; legacy FNT fonts, palettes, and movies retain their Classic consumers.

## Raster semantics

RGBA stores straight color and coverage. Coordinates and frame dimensions are physical pixels at the variant density; the canvas and logical frame count preserve the Classic header. Centered drawing subtracts half that logical canvas before applying physical frame offsets. HD data must not replace semantic frame counts or dimensions used by animation, radar, or gameplay. A shape frame must fit within the selected Classic frame's cropped visual rectangle at the variant density; a frame extending outside it uses Classic fallback. This preserves existing culling and cached redraw bounds.

Remap is one byte per pixel: zero retains RGBA color, and 1–16 select Classic house-color shades 16–31. Shadow is one byte of coverage for a half-bright destination shadow. Depth is a signed little-endian int16 logical offset subtracted from the draw baseline. Terrain requires a depth plane; reproducing the additive TMP byte convention requires its negative here. Numerical depth is never multiplied by framebuffer density.

Declared sequences require complete temporal, facing, and direction blocks for each present logical frame. Omitting an entire frame block permits Classic fallback. A partial block is invalid. Reverse playback requires separately authored reverse blocks. Extended facings are additional render samples per existing logical frame and do not renumber those frames.

## Model semantics

Raster variants keep the model components and VXL/motion blobs zero or empty. Voxel variants have no raster frames or sequences. Their nine signed 16.16 model values declare minimum XYZ, maximum XYZ, and pivot XYZ, with ordered bounds. These values are validated declarations; they do not override the model transform or pivot. Rendering normalizes the denser model against the Classic model's per-layer bounds, scale, transforms, and HVA motion. Version 1 rejects replacement motion blobs.

Before the native voxel reader runs, validation checks header/body/tail lengths, palette count, layer and info-record counts, finite bounds and transforms, positive scale, dimensions, normal modes and indices, span offsets, and complete forward/reverse runs. The maximum aggregate layer volume is 16777216 voxels.

## Resource limits and fallback

The format permits at most 16 variants, densities 1–8, 16384 total raster frames, 16384 sequences per variant, 256 facings, and 32 temporal subframes. Physical dimensions and scaled logical canvases are limited to 4096; signed frame offsets range from -4096 through 4096. Input and decoded-pack budgets are each capped at 256 MiB. Decoded metadata and temporary depth conversion count toward decoder allocation checks.

The active renderer accepts a narrower framebuffer-density range, documented by `RenderScale`. Its decoded asset cache may reject a pack below these format ceilings when insufficient unpinned space remains. Rejected or missing variants retain the winning Classic asset. Source and pack-validation failures emit bounded diagnostics; an absent selected frame or unsupported draw effect can fall back silently.

Shape draws that use predator displacement, palette-index alpha writes or blends, or remaps affecting indices outside the house-color range use Classic artwork. A compressed Classic Z-shape also requires an explicit HD depth plane to use the HD variant. Required alpha and depth buffers must match the drawing surface density.
