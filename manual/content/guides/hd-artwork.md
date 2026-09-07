---
title: Build HD artwork
summary: Creates and validates an HD sidecar from lossless image masters and explicit semantic masks.
category: files-formats
uses_keys: [RenderMode, RenderScale, ArtCacheMB]
prerequisites:
- Python 3.10 or newer on Windows
- A configured OpenTS build with the HDAssetsTest validator
- The Classic asset that the sidecar will accompany
related:
- type: format
  id: hd-pack
- type: system
  id: hd-rendering
---

Keep the art project outside the repository or in the ignored `baseline/` directory. Use noninterlaced 8-bit grayscale, RGB, or RGBA PNG masters. Provide remap and shadow masks explicitly; painted RGB cannot reliably identify house-color regions.

1. Create the version-1 JSON manifest described in the repository's `tools/hdpack/README.md`. Preserve the Classic logical canvas, frame count, and frame identities, and point `classic` at the exact file that will be installed.
2. Build the `HDAssetsTest` target in a supported configuration, then run `tools/hdpack/hdpack.py` with the manifest, explicit output and preview paths, and `--validator` pointing to that executable. The validator decodes with the production parser and checks identical re-encoding.
3. Open the generated `index.html` preview. Inspect frame placement, facings, forward and reverse continuity, remap masks, shadow coverage, and signed depth. Supply `--palette` for previews of the actual paired Classic SHP and its palette; use `--terrain-layout` for assembled tile seams, depth ordering, and Classic tile fallback. These previews show the submitted data; verify engine lighting, fog, motion, and presentation as well.
4. Install the successful sidecar beside its loose Classic asset or inside the exact MIX archive that supplies it. SHP readers require a cached archive, such as `ECACHE98.MIX`; direct SHP members in uncached `EXPAND98.MIX` are not available to those readers. Enable HD rendering and test the asset alongside Classic fallbacks. A diagnostic naming a digest or source mismatch means the sidecar accompanies different Classic bytes.

The builder preserves PNG channel bytes without applying color profiles or gamma transforms. Supply straight-alpha masters in the intended display color space. The final engine framebuffer retains hicolor precision.

Generating a Classic SHP fallback is a separate, explicitly lossy operation. The builder requires `--accept-lossy-legacy`, an indexed palette, and an output path, and writes a per-frame conversion report. Frame crops, their area ranking, radar colors, and raw/RLE compression modes remain unchanged because the engine uses that metadata beyond color drawing. Spatial detail, temporal samples, continuous alpha, and independent shadow/depth planes cannot all survive the conversion. When generating this fallback, install the resulting SHP and HDP together: the sidecar digest binds the generated SHP.
