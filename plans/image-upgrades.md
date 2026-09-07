# Genuine HD image upgrades

## Summary

OpenTS now renders genuinely higher-density artwork without changing the apparent size, placement, or gameplay meaning of objects and terrain. The implementation separates logical coordinates from physical render resolution, introduces versioned source-bound HD asset variants with explicit scale and semantic metadata, and retains Classic artwork and rendering as the compatibility fallback.

The feature retains software composition and RGB565 output while supporting RGBA artwork, semantic remapping and shadows, denser terrain and voxel projection, optional normalized models, and directed animation subframes driven by the smooth-motion clock. Individual assets can be upgraded while the rest use Classic fallback. The [HD rendering manual](../manual/content/systems/hd-rendering.md), [pack format](../manual/content/formats/hd-pack.md), and [authoring guide](../manual/content/guides/hd-artwork.md) own current behavior and limitations.

Implementation and local acceptance are complete on `render/hd-image-upgrades`. Validation corrected startup allocations made before HD settings were loaded, a density-one unit composition buffer, and a successful-pack eviction path that could leave a permanent Classic fallback. The acceptance record below separates compiled/synthetic checks, live observations and limited performance measurements. The branch is prepared for its scoped implementation commit and pull request; merging or releasing the HD feature is outside this closeout.

## Implementation tracker

### Phase 1: Establish the rendering and compatibility contract

- [x] Define logical resolution, physical render resolution, world-art scale, and UI scale as distinct concepts.
- [x] Define Classic and HD rendering modes, with Classic preserving the current asset selection and drawing behavior.
- [x] Define how `ScreenWidth`, `ScreenHeight`, window size, and the new render scale combine without changing the current meaning of existing settings when HD mode is disabled.
- [x] Define asset precedence so a higher-priority mod's classic replacement is not silently shadowed by a lower-priority HD asset.
- [x] Classify all art-derived data as either visual or simulation-relevant and prohibit HD variants from changing simulation-relevant data.

### Phase 2: Introduce an asset-resolution layer

- [x] Add an engine-owned asset identifier and resolver that can represent the current theater naming, archive precedence, loose-file behavior, and HD variants without exposing format-specific pointers to new code.
- [x] Adapt legacy SHP, theater tile, PCX, font, VXL/HVA, palette, and fixed-name lookups to the resolver incrementally while preserving their existing lifetime and fallback behavior.
- [x] Add owned decoded-asset and scaled-asset caches with explicit archive/theater invalidation and bounded memory use.
- [x] Make a missing, malformed, or unsupported HD variant fall back to the selected source's classic asset with a useful diagnostic.

### Phase 3: Add logical-to-physical rendering scale

- [x] Introduce a render context that converts logical draw coordinates, clipping rectangles, dirty rectangles, and depth-buffer addresses into physical framebuffer coordinates while keeping numerical depth and scroll depth bias logical.
- [x] Accept world-space leptons (or an equivalent sub-pixel fixed-point form) at the render context's world-draw interface and perform the lepton-to-physical conversion in a single divide — the scale multiplies before the `CELL_LEPTON` division — so sub-logical-pixel positions, including the sub-tick render offsets from `plans/fps-fix.md`, survive scaling instead of being truncated to logical pixels first.
- [x] Render HD mode into a framebuffer whose dimensions are logical resolution multiplied by the selected render scale.
- [x] Scale the alpha buffer, depth buffer, voxel scratch surfaces, cached composites, and dirty-region tracking consistently with the physical framebuffer.
- [x] Keep simulation coordinates, map cells, object footprints, selection rules, weapon offsets, save data, replay data, and network state in their existing units.
- [x] Preserve the presenter's window-to-logical input mapping through VideoScaleInfo.GameWidth/GameHeight; physical framebuffer density must not divide already-logical input a second time.
- [x] Preserve final window fitting, aspect-ratio bars, and the existing nearest, linear, and pixel-art presentation filters after the higher-resolution frame has been composed.

### Phase 4: Support HD shape and interface assets

- [x] Define a versioned HD art manifest that maps a logical asset name to one or more scale variants and records frame rectangles, logical canvas size, anchors, alpha behavior, remap masks, shadow frames, and source precedence.
- [x] Use a lossless image representation for HD color and alpha, with house-color remapping stored as semantic mask data instead of inferred from generated RGB pixels.
- [x] Draw an exact-scale HD frame one-to-one into the physical framebuffer while drawing a missing classic frame through a deterministic scaled fallback.
- [x] Preserve SHP frame numbering, facings, sequence layout, theater selection, damaged states, buildup sequences, shadows, Z-shapes, and fixed-name interface behavior at the logical level.
- [x] Allow a variant to declare an integer temporal multiplier per sequence — K sub-frames per logical frame — and an extended facing count, while simulation stage advancement, logical frame counts, timing, and facing arithmetic stay untouched. Sub-frame selection happens at draw time from directed intra-stage progress — a render-only accessor exposing the countdown timer's elapsed fraction of its own programmed interval (stage rates can change mid-interval without resetting the countdown, so the stored rate is not the interval) and the step direction; the stage state does not own sequence topology, so the accessor must not claim it — combined with the smooth-motion render clock's intra-tick fraction. The selector never predicts the next logical frame: it holds the terminal sub-frame at an interval boundary until the simulation advances the stage, and a sequence that declares reverse or ping-pong playback must supply direction-specific sub-frame blocks, because a forward block played backward morphs toward the wrong neighbouring frame. Define the accessor with the manifest field, and give the pair its own visual acceptance criterion and synthetic fixtures covering at least the animation owner's reverse, ping-pong, and looping playback, building start/count stage windows, infantry sequence swaps, and a rate change during an active interval — asserting continuity at reverse steps, loop edges, and ping-pong flips, not merely that indices stay in range — before any pack relies on it.
- [x] Give world art and interface art separate scale domains so UI size can remain usable independently of world detail.
- [x] Add pack validation for frame counts, dimensions, anchors, masks, scale consistency, resource limits, and unsafe image sizes before an asset reaches a draw path.

### Phase 5: Generalize terrain rendering

- [x] Separate the logical 48 by 24 isometric cell geometry from the physical number of pixels used to render that diamond.
- [x] Generate terrain span and clipping tables from the active render scale instead of fixed 48 by 23 draw tables.
- [x] Define an HD terrain variant carrying color, alpha where applicable, extra cliff imagery, and scale-matched depth data while reusing legacy height, ramp, land type, and radar semantics.
- [x] Scale terrain overlays, shroud, fog, bridges, slopes, cliff shadows, deformation, Z adjustment, preview rendering, and redraw bounds from the same logical terrain geometry.
- [x] Permit individual HD tile sets to fall back to their classic theater files without creating seams or changing the map's cell data.

### Phase 6: Improve voxels and other visual families

- [x] Scale the existing voxel projection and caches into the physical framebuffer so current VXL/HVA models render cleanly at the selected HD scale.
- [x] Define how optional higher-density voxel variants preserve model-to-world size, pivots, layer transforms, normals, turret/barrel attachment, and shadows.
- [x] Keep VXL/HVA and VOXELS.VPL compatibility as the baseline rather than requiring a new 3D model format for the first HD release.
- [x] Define one render-facing bucket count that drives both the voxel transform quantization and every rasterization-cache key layout together — body, shadow, turret, barrel, and the frame and ramp bit packings — so finer rotation actually rasterizes finer orientations instead of re-keying the same 32, and bound the cache growth the finer count causes.
- [x] Evaluate full-color fonts, cursors, loading screens, menu PCX images, movies, and radar graphics separately because their layout and scaling requirements differ from world sprites.

### Phase 7: Add a reproducible asset-authoring pipeline

- [x] Provide a Windows-native command-line pack builder that validates manifests, imports lossless source images, builds runtime assets, and reports every fallback or lossy conversion.
- [x] Support high-resolution source masters and deterministic down-conversion so the same project can produce HD variants and legacy SHP-compatible fallbacks.
- [x] Provide preview tooling for animation sequences, facings, anchors, shadows, remap masks, palettes, and terrain seams before assets are packaged.
- [x] Keep proprietary source assets, derived game assets, and generated packs outside this repository; commit only format documentation, synthetic fixtures, and reusable tooling.
- [x] Treat generated imagery as an authoring input requiring frame consistency, perspective, mask, shadow, and animation review rather than as a directly shippable runtime asset.

### Phase 8: Validate, document, and land the feature

- [x] Add synthetic asset fixtures that require no proprietary game data and cover classic fallback, scale selection, clipping, anchors, alpha, remapping, animation frames, terrain depth, and malformed inputs.
- [x] Add deterministic image comparisons for representative world, UI, terrain, shadow, fog, lighting, and mixed classic/HD scenes at supported scales.
- [x] Measure CPU cost, memory use, cache churn, and frame presentation at representative logical resolutions, HD scales, resize states, and supported bgfx backends.
- [x] Build and run the relevant checks in all four supported Visual Studio 2022 configurations: Win32 Debug/Release and x64 Debug/Release, distinguishing build results from runtime visual evidence.
- [x] Document the art-pack format, configuration, authoring workflow, fallback rules, compatibility behavior, resource limits, and migration guidance in their owning manual sections.
- [x] Add a player-visible intentional-change record for HD rendering while stating that Classic mode preserves existing behavior.
- [x] Commit the completed implementation and documentation with an imperative subject of at most 72 characters.

## September 7 acceptance

The [build verification record](../docs/BUILDING.md#verification-boundary) owns the supported four-configuration result. Full forced builds used identical hashes across 1,051 source inputs; all native checks passed, with logger timing checked sequentially while idle. `python manual/tools/manage.py check` passed all extraction, authored-contract, test, build, search and link checks for 2,201 pages. Synthetic coverage includes pack validation and eviction/reload, retained source lifetimes, production startup ordering, physical surfaces and rings, actual animation-owner loop predicates, UI composition, dense voxels/terrain/lighting, and assembled image hashes at scales one through four.

The final Win32 Debug candidate matched all four original golden recordings in both Classic and HD modes: GDI, Nod, Firestorm GDI and Grand Canyon skirmish. The final x64 Debug candidate matched its native skirmish recording in both modes. These ten comparisons matched all 27,348 comparable sync-dump lines at frame 300. Replays establish the covered deterministic state; they do not replace live visual checks or establish every multiplayer/mod combination.

Fresh Win32/x64 Release missions mixed synthetic HD world sprites and a sidebar cameo with stock terrain, structures and voxels. Live checks covered terrain/object/shroud alignment, selection and movement, actual edge panning, a sweeping stock spotlight, forward/reverse/ping-pong subframes through loop boundaries, x64 save/reload, window resizing and selection after resizing, and ordinary menu exit on both architectures. Read-only process inspection confirmed matching density-two primary, world, UI, voxel scratch, depth and alpha storage at fresh startup. The cameo's opaque HD cyan and white pixel counts matched its source exactly; its red body is intentional palette remapping.

Performance samples used x64 Release, `SmoothMotion=yes`, nearest presentation, VSync disabled and a small stationary mixed scene, on Windows 11 with an i9-12900H, 32 GiB RAM, RTX A3000 Laptop GPU and Intel Iris Xe. Each foreground sample lasted approximately eight seconds with no concurrent builds/tests and no focus loss. All held 30 simulation ticks/s and recorded zero raster-cache rebuilds during sampling. CPU is total process CPU seconds divided by elapsed seconds; memory is mean working set, not an artwork-cache limit.

| Mode/density | Logical resolution | Backend | Render frames/s | CPU cores | Working set MiB |
| --- | --- | --- | ---: | ---: | ---: |
| Classic/1 | 1280×800 | Direct3D11 | 3348.8 | 1.03 | 246.8 |
| HD/1 | 1280×800 | Direct3D11 | 2627.9 | 1.05 | 274.0 |
| HD/2 | 1280×800 | Direct3D11 | 351.3 | 1.04 | 354.5 |
| HD/2 | 1280×800 | Direct3D12 | 281.9 | 0.94 | 1220.0 |
| HD/2 | 1280×800 | Vulkan | 280.3 | 1.04 | 384.1 |
| HD/2 | 1280×800 | OpenGL | 340.2 | 1.17 | 384.4 |
| HD/4 | 1024×768 | Direct3D11 | 66.6 | 1.02 | 470.8 |
| HD/2 | 1920×1080 | Direct3D11 | 151.8 | 1.05 | 399.6 |
| HD/2, resized | 1920×1080 | Direct3D11 | 151.9 | 1.01 | 394.5 |

Initial presentation was 1920×1200 except the 1024×768 case at 1536×1152. The final resize produced a 1520×921 client, 1520×855 content and 33-pixel top bar while retaining the 1920×1080 logical viewport. These measurements show costs for this scene and machine; they do not establish large-battle performance or every driver. HD pack effects unsupported by the compositor retain the documented Classic fallback. Live tests used local proprietary files, while committed tests and tooling require none.

Ignored local evidence is under `baseline/hd-validation-2026-09-06/` (case identities, fixture hashes, screenshots, temporal analyses, replay reports, per-second timing/memory/cache samples and `runtime-report.md`) and `baseline/hd-final-native-matrix.md` (exact commands, four executable hashes and build/test logs). The invalid density-mismatched measurement and provisional fixture mistakes remain explicitly distinguished from acceptance. Original `Run/` executable, language-library and settings hashes were preserved, and unrelated `assets/` content remains outside the feature commit.

## Recommended architecture

### Logical resolution and render scale

Today, increasing the game's render resolution exposes more logical pixels and usually more of the map. It does not make one unit consume more pixels. Genuine HD rendering should retain that useful behavior by leaving `ScreenWidth` and `ScreenHeight` as the logical frame size and introducing an independent integer render scale. For example, a logical 1920 by 1080 frame at scale 2 produces a 3840 by 2160 physical framebuffer while retaining the field of view and UI layout of the logical frame.

All existing simulation and layout code should continue to work in logical units. Scaling should occur at a rendering boundary rather than by multiplying constants throughout gameplay code. The render context should be the single owner of coordinate conversion so clipping, depth, dirty regions, mouse input, and asset selection cannot quietly disagree about the active scale.

World art and UI should initially default to the same scale, but their scale domains should be distinct in the design. This makes it possible to keep a readable UI while selecting a different world-detail level and prevents future accessibility controls from becoming another renderer rewrite.

### Asset variants and fallback

An HD asset needs both physical pixels and logical metadata. Its manifest should say that a 120 by 80 frame, for example, occupies the same logical canvas and anchor as a classic 60 by 40 frame at scale 2. Frame identity, sequence order, house-remap regions, shadows, and depth behavior must remain explicit because they cannot be reconstructed reliably from an RGBA image alone.

Resolution choice should happen after source precedence has been resolved. If a mod overrides a unit with a classic SHP, that override should win over a base game's HD rendition of the original unit. Within the winning source, the resolver should choose the exact requested scale, then a documented higher- or lower-scale fallback, and finally the classic asset.

The manifest and runtime representation should be versioned from the beginning. That permits later alpha, material, normal, or color-space extensions without reinterpreting old packs. It also allows the loader to reject unsupported semantics cleanly while still falling back to classic art.

### Temporal upscaling and the shared render clock

Animation stage is simulation state: it is serialized, folded into multiplayer checksums, and read by gameplay, so an HD pack cannot make animations advance faster. What a pack can do is carry more artwork per logical frame. A variant that declares a temporal multiplier supplies K sub-frames for each logical frame, and the renderer selects among them from directed intra-stage progress — the elapsed fraction of the countdown's own programmed interval, signed by the step direction, held when the stage holds; sequence topology stays with the sequence owners, so the selector never predicts the next logical frame and holds the terminal sub-frame at a boundary until the simulation advances, with reversible sequences supplying direction-specific sub-frame blocks — combined with a continuous intra-tick phase taken from the smooth-motion render clock defined in `plans/fps-fix.md`. Selection is a pure read of simulation state plus the render clock, so it cannot affect determinism. When smooth motion is disabled, the intra-tick phase pins and sub-frame selection degrades to tick-rate stepping. The same manifest mechanism carries extended facing counts for smoother rotation. Sub-frame and facing artwork is an authoring cost like any other HD art and flows through the same pipeline and review as the rest of a pack.

### Color, alpha, lighting, and remapping

Higher pixel density does not require abandoning the current palette pipeline immediately. The first implementation can render both classic and HD pixels into a scaled 16-bit framebuffer, preserving existing lighting and remapping while proving the coordinate and asset architecture. This is a genuine resolution improvement even though color precision remains classic.

Full-color RGBA composition should be a separate capability built on the same asset abstraction. It requires explicit equivalents for brightness, tinting, translucency, house colors, shadows, fog, alpha buffers, and palette-driven special effects. A parallel full-color path should not silently approximate these behaviors; each effect needs a defined result and a classic comparison fixture.

House colors must remain semantic data. Generated or painted art should provide a mask or indexed remap channel identifying recolorable pixels. Inferring that channel from final color would produce unstable results across image-generation runs and would make faction colors dependent on incidental shading.

### Terrain

Terrain is the largest compatibility boundary because the current 48 by 24 diamond is used by coordinate conversion, clipping, depth, fog, overlays, radar calculations, and redraw logic. The logical diamond should remain 48 by 24 so maps and simulation stay unchanged; only its physical raster should become 96 by 48 at scale 2, 192 by 96 at scale 4, and so on.

HD terrain cannot be represented by a color image alone. The variant must preserve or replace the depth stream used to sort objects against slopes and cliffs, while gameplay height, ramp, land type, and radar colors continue to come from the legacy semantic tile record. This separation ensures that installing an art pack cannot alter pathfinding, movement cost, placement, or deterministic simulation.

### Voxels

Existing voxels are geometry rather than fixed sprite frames, so they can benefit from a higher-resolution projection as soon as the voxel scratch surface, transforms, caches, and composition target honor render scale. Higher-density VXL variants are possible later, but they need controlled model-space normalization; simply doubling voxel dimensions would also double the vehicle unless its bounds and projection scale are adjusted together.

Image generation is useful for concept sheets, paint references, and sprite-based alternatives, but it does not directly produce valid voxel volumes, normals, pivots, or HVA animation. A genuine upgraded voxel asset should pass through a controlled voxel or 3D authoring pipeline.

## Compatibility and rollout constraints

- Classic mode is the reference for preserved behavior and remains available even when an HD pack is installed.
- HD rendering is an intentional visual change, but must not affect simulation checksums, saves, replays, network packets, object footprints, targeting, pathfinding, or map semantics.
- Mixed classic and HD assets are a supported state, not an error condition.
- Existing theater naming, archive search order, mod overrides, and fixed-name resources remain meaningful through the resolver.
- Visual pack identity may be recorded for diagnostics and screenshots, but must not become a network compatibility requirement unless a later feature deliberately makes visual data simulation-relevant.
- Automated fixtures and repository content must remain independent of proprietary Tiberian Sun assets and original executables.

## Suggested first proof

The first proof should implement a scale-2 physical framebuffer, preserved window-to-logical input mapping, one manifest-backed SHP asset, and classic fallback for everything else. It should demonstrate one world sprite with a house-remap mask, alpha, shadow frames, stable logical placement, correct clipping, and unchanged selection behavior at Classic and HD scales. This isolates the core architecture before terrain, full-color composition, or broad asset production expands the scope.

The second proof should mix one HD world sprite, one HD interface element, a scaled legacy voxel, and entirely classic terrain in the same frame. Success would demonstrate that scale domains, resolver precedence, fallback, input mapping, caches, and presentation can coexist before the fixed terrain raster is generalized.

## Material risks

- The inherited renderer frequently uses the same pixel constants for layout, projection, clipping, and buffer indexing; missing one path can produce subtle visual or memory errors.
- A 2x render scale uses roughly four times the pixels, while 4x uses roughly sixteen times, before extra caches and full-color buffers are counted.
- The portable software blitters assume specific pixel formats and unscaled source-to-destination copies, so a scaled or 32-bit path requires carefully bounded replacements rather than flag additions scattered across every call site. Production assembly was retired by the merged native x64 conversion.
- Asset-pack precedence can break existing mods if scale selection is performed before override selection.
- Generated animation frames can drift in geometry, facing, lighting, and identity; deterministic tooling and review remain necessary even when generation accelerates source-art creation.
- HD terrain depth, fog, shadows, and overlays can look correct in a still image while sorting incorrectly in motion, so runtime visual evidence is essential in addition to image comparisons.
