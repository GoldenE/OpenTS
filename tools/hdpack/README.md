# HD art pack authoring

`hdpack.py` builds lossless, versioned `.HDP` sidecars on Windows with Python 3.10 or newer and its standard library. It imports noninterlaced 8-bit grayscale, RGB, or RGBA PNG masters, verifies chunk CRCs and bounded decompression, preserves straight RGBA channel bytes, and exports deterministic PNG previews. JPEG, indexed PNG, color-key transparency, premultiplied alpha, and implicit color-space conversion are unsupported: convert these deliberately in the source-art project first. PNG profile/gamma metadata does not transform the channel bytes; supply masters whose bytes are already in the intended display color space. The engine's final software surface retains its existing hicolor precision.

Keep source projects, proprietary or derived artwork, packs, previews, and converted SHPs outside the repository, or in its ignored `baseline/` directory. The tests generate their own synthetic artwork and require no installed game assets. Generated imagery is an authoring input: review identity, perspective, facing, animation continuity, remap masks, shadow boundaries, and terrain sorting before shipping it.

## Build and review

```powershell
cmake --build baseline/build-hd-assets --config Debug --target HDAssetsTest
python tools/hdpack/hdpack.py C:/ArtProject/pack.json --output C:/ArtProject/output/UNIT.SHP.HDP --preview C:/ArtProject/preview --validator baseline/build-hd-assets/tests/hdassets/Debug/HDAssetsTest.exe
python tools/hdpack/test_hdpack.py baseline/build-hd-assets/tests/hdassets/Debug/HDAssetsTest.exe
```

Use a configured Visual Studio 2022 build tree as described in [Building](../../docs/BUILDING.md). `--validator` runs the production C++ decoder and requires an identical binary re-encoding. It is mandatory for voxel packs because their nested VXL span validation belongs to the native parser. The script prints output and classic SHA-256 identities, byte size, and scale count; a validation failure exits nonzero. Outputs are overwritten at the explicitly supplied output/preview locations. A native validation failure can leave the rejected output file in that location; do not install outputs from a failed command.

Before writing SHP-backed outputs, the builder cross-checks every variant's logical width, height and frame count, and every authored frame rectangle, against the actual paired SHP. This includes every temporal subframe, facing and direction. A guaranteed runtime metadata/crop fallback is an error naming the scale and frame identity; no output is written for that error. With `--legacy-shp`, the generated fallback is the paired file checked; it retains the original frame crops, so this option cannot enlarge the permitted HD visual envelope. The JSON result's `fallbacks` list reports all absent logical frames as compact ranges that will use classic artwork. Draw-effect fallbacks still depend on engine callers; the wire validator alone cannot predict them.

Terrain pairs also check the classic TMP header, subtiles, and base/extra visual envelope; voxel pairs check layer counts, info counts and layer-to-info mapping against the original VXL. These checks precede output. Known file families without a version-1 HD replacement consumer are named explicitly in the `fallbacks` result instead of being presented as active replacements.

Open `preview/index.html` locally. Each frame identifies logical frame, temporal subframe, extended facing, playback direction, physical offset, and logical canvas. Its RGBA, remap, and shadow images are exported separately; signed depth is exported as `.i16le`. The page presents contact sheets for manual comparison; it is not an engine lighting/depth simulator. Repeat the command with identical inputs and the same Python/zlib versions to obtain identical pack, PNG and HTML bytes. The pack itself uses no compression and is independent of zlib output variation.

With `--preview <directory> --palette <six-bit.pal>`, SHP packs also export a linked `legacy/index.html`, palette swatches, and palette-applied PNGs for every frame of the actual paired classic file. Both raw and RLE-compressed retained frames are decoded with row/frame bounds checks. When `--legacy-shp` is selected these previews show the generated fallback, including unchanged classic frames; otherwise they show the manifest's source SHP. The common preview canvas retains frame offsets so anchor and crop differences are visible. Colors are the supplied palette's eight-bit RGB expansion before engine hicolor quantization, lighting, and draw effects.

Terrain packs export a linked `terrain/index.html`, an assembled `terrain-seams.png`, a hole/boundary `terrain-coverage.png`, and the exact `terrain-layout.json`. By default, four neighboring cells cycle through the classic TMP's subtiles. Pass `--terrain-layout layout.json` to choose up to 256 placements and an exact authored scale, for example `{"scale":2,"placements":[{"frame":0,"cell_x":0,"cell_y":0},{"frame":1,"cell_x":1,"cell_y":0},{"frame":2,"cell_x":0,"cell_y":1}]}`. Assembly uses the classic 48×24 diamond plus extra-image footprint, the HD RGBA/remap/shadow/depth planes, and deterministic draw ordering. A combined HD sample is applied once even where classic base/extra footprints overlap. Supply `--palette` for absent HD frames or classic pixels outside an HD crop; the saved layout lists all classic fallback frame IDs. This neutral source-color assembly is for seam and mask authoring, not proof of game lighting, fog, map-height behavior, or backend output.

Preview input and work are bounded: 4,096-pixel output dimensions, 256 MiB total decoded/exported SHP pixels, validated TMP offsets/planes, and at most 33,554,432 terrain sample visits. Malformed RLE runs, unavailable palette-dependent fallbacks, invalid layout coordinates, and excessive canvases fail explicitly instead of reading beyond an input or silently omitting pixels.

Add `--mix C:/ArtProject/output/ecache98.mix` to package the matching classic image and sidecar into one unencrypted MIX, using the engine's padded CRC32 filename keys and signed ordering. This archive contains only the requested pair and does not merge or edit installed archives. `python tools/hdpack/make_proof.py baseline/hd-proof --validator <HDAssetsTest.exe>` generates wholly synthetic `HDTEST.SHP` (48×48, 512 frames), `HDBUTTON.SHP` (60×48, one UI frame), matching HDP sidecars, masters, previews, and a combined `ecache98.mix`; it does not install or launch them. SHP readers require a cached archive: use `ECACHE00.MIX` through `ECACHE99.MIX`, or put that cached archive inside an expansion. Direct SHP members in an uncached `EXPANDxx.MIX` are not reachable through cached-only retrieval; do not install the same pair in both containers.

## Manifest version 1

Paths are relative to the manifest. `classic` supplies the authoritative content digest; it is not embedded in the pack. For SHP consumers, logical width, height, and frame count must exactly equal the winning classic SHP header. All original frame numbers, sequence owners, animation timing, radar colors, footprints, theater selection, and gameplay semantics remain authoritative.

```json
{
  "version": 1,
  "name": "UNIT.SHP",
  "classic": "classic/UNIT.SHP",
  "kind": "shape",
  "variants": [{
    "scale": 2,
    "logical_width": 60,
    "logical_height": 40,
    "logical_frames": 32,
    "facings": 1,
    "sequences": [{"first": 0, "count": 4, "temporal": 1, "reverse": false}],
    "frames": [{
      "logical": 0,
      "subframe": 0,
      "facing": 0,
      "direction": 1,
      "x": 0,
      "y": 0,
      "image": "masters/frame-000.png",
      "remap": "masks/frame-000-remap.png",
      "shadow": "masks/frame-000-shadow.png",
      "depth": "depth/frame-000.i16le"
    }]
  }]
}
```

Only `logical`, `subframe`, `facing`, `direction`, `x`, and `y` can default on frames: their defaults are `0, 0, 0, 1, 0, 0`. Remap, shadow, and depth are optional for SHP frames. `facings` defaults to 1; sequences default to none, which means one forward subframe per logical frame. A declared sequence defaults to temporal 1 and forward-only playback. Omit complete logical frame blocks for classic fallback; a partially present facing/temporal/direction block is invalid. Variant scales, sequence starts, and frame identities are sorted by the builder for deterministic output.

RGBA image dimensions and `x/y` offsets are physical pixels at the variant's scale. Offsets are measured from the upper-left corner of the unchanged logical canvas; a centered draw subtracts half the classic logical canvas before applying them. For SHP-backed shape, UI, and font draws, every selected HD frame rectangle must fit wholly within the corresponding classic frame's actual crop multiplied by the variant scale. This includes temporal subframes and extended facings, and applies to the entire image rectangle even when border pixels are transparent. Matching the logical canvas alone is insufficient. A rectangle outside that classic visual envelope produces a bounded diagnostic and uses the classic frame, preserving existing culling and dirty-region bounds. Crop masters to that envelope and supply the cropped rectangle's physical offset. This SHP constraint does not redefine terrain extra-image bounds.

Masks are same-sized grayscale PNGs. Remap values are 0 for ordinary color and 1–16 for the classic house palette shades 16–31; those samples use the active converter/remap table rather than inferred RGB. Shadow values are 0–255 coverage for a half-bright destination shadow, mixed with the frame color before its RGBA coverage is applied. Depth is one signed, little-endian 16-bit logical depth offset per pixel, subtracted from the draw baseline; positive values bring pixels forward. A shape depth plane substitutes for a classic Z-shape. Terrain requires depth and retains the classic tile's gameplay metadata: to reproduce the TMP depth byte's additive convention, store its negative in HD depth. Do not multiply numerical depth by render scale.

Temporal multiplier `K` means `K` authored subframes for every present logical frame/facing. Direction is +1 for forward and -1 for reverse. Reverse and ping-pong sequences must declare `reverse: true` and provide separate reverse blocks. The renderer holds the terminal subframe until the simulation advances; it does not predict a neighboring logical frame. Held stages use forward subframe zero. Extended facings subdivide one complete turn evenly and are additional render samples per existing logical frame; they do not renumber classic frames or advance simulation stages.

Author each block for its owner's actual next rendered frame. Numeric wraparound is not a topology contract: an animation owner's start/end comparisons can omit an endpoint or use a different loop window. The tools preserve supplied identities and do not infer neighboring frames. Check the owner's forward, reverse and ping-pong boundary behavior before relying on a generated continuity pattern; the generic four-frame K4 proof must be adapted when the owning animation does not render that exact four-frame cycle.

## Resolution, source, and lifetime contract

Resolution selection occurs after the original caller has selected its source. Cached `MixFileClass::Retrieve` remains cached-MIX-only, while retained images loaded through `CCFileClass` retain that call's loose-file or archive source captured at open time. Archive sidecars must be members of that exact archive; loose sidecars must sit beside the captured physical loose file. A base archive's HD asset cannot override a higher-priority mod's classic replacement. The sidecar name appends `.HDP` to the complete canonical asset name, such as `UNIT.SHP.HDP`. Name and SHA-256 must both match the winning classic bytes. Missing, malformed, unsupported, or mismatched sidecars fall back to classic artwork, with bounded diagnostics.

Within the winning source, selection prefers the requested art scale, then the smallest higher scale, then the largest lower scale. Rendering uses deterministic nearest samples when pack density differs from physical surface density. An exact-scale frame is sampled one-to-one. UI and world art have separate requested density domains. Draws retain a shared pack pin; archive/theater invalidation removes future bindings while an active pin remains valid and charged against the cache budget. Owned image release/replacement unregisters its pointer. Reconfiguring the cache retains charges for outstanding pins and refuses new packs until their memory can fit.

The SHP compositor supports RGBA coverage, house remap, logical depth, Z-shapes, fixed translucency, shadow/darken, tint/ion lighting, alpha lighting, and applicable zero/nonzero-alpha gates. Raw RGB samples receive the active converter tint; masked house samples use the exact palette lighting table, including its tint exemptions. Existing palette-index-as-light writes, alpha-buffer blend operations, and displaced predator samples use classic fallback because RGBA alone does not carry those source-index semantics. Classic mode does not read HD sidecars.

## Legacy fallback export

```powershell
python tools/hdpack/hdpack.py C:/ArtProject/pack.json --output C:/ArtProject/output/UNIT.SHP.HDP --legacy-shp C:/ArtProject/output/UNIT.SHP --palette C:/ArtProject/palette.pal --accept-lossy-legacy --validator baseline/build-hd-assets/tests/hdassets/Debug/HDAssetsTest.exe
```

The palette is 768 bytes of classic six-bit RGB. The tool uses the lowest-scale master, nearest center sampling, a 128 coverage threshold, and squared-RGB nearest-palette matching with deterministic lowest-index ties. Index 0 is transparency; indices 16–31 are reserved for explicit remap samples. It preserves the classic SHP header, frame count, and every frame's original X/Y/Width/Height, radar color and unused metadata. Masters are sampled within those original crops, including nonzero or negative crop origins. Each frame keeps its original compression mode: raw stays raw and RLE is re-encoded, preserving the renderer's RLE-only Z-shape path. Encoded payloads must fit the classic signed 16-bit frame-size field. Only transparency/encoding metadata, encoded size and data offset may change; unrelated flag bits are retained. Empty crops and frames without an HD forward base sample retain their original bytes. Keeping each crop area preserves the `Biggest` frame ranking used by animation middle/scorch/crater timing. The original unused payload is retained, avoiding reinterpretation of untouched compressed frames.

This conversion is deliberately lossy: extra spatial samples, temporal/facing samples, partial alpha, non-palette colors, and separate shadow/depth planes cannot all fit a classic SHP. The command requires explicit `--accept-lossy-legacy` and writes a per-frame `.report.txt` with conversion counts and fallback decisions. It never silently infers remap or changes logical frame identities. When this option is used, the HDP digest binds the generated fallback SHP; install that pair together. Without this option, the HDP binds the manifest's original `classic` file.

## Binary HDP version 1

The manual's [HD artwork sidecars](../../manual/content/formats/hd-pack.md) owns the wire format, semantic channels, model rules, and resource ceilings. The production C++ decoder remains the validator used by `--validator`.
