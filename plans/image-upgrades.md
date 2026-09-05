# Genuine HD image upgrades

## Summary

The goal is to let OpenTS render genuinely higher-density artwork without changing the apparent size, placement, or gameplay meaning of objects and terrain. The recommended solution is to separate the engine's existing logical pixel coordinate system from the physical render resolution, introduce versioned HD asset variants with explicit scale and semantic metadata, and retain the current assets and renderer as a complete compatibility fallback.

The feature should be incremental rather than a renderer replacement. A first usable version can keep the existing software composition model and palette behavior while rendering into a larger framebuffer; later work can add full-color assets, improved alpha, HD terrain, higher-detail models, and temporally upsampled animation frames and facings driven by the smooth-motion render clock from `plans/fps-fix.md`. Classic mode must remain capable of reproducing current rendering, and an incomplete HD pack must be allowed to replace one asset while every other asset continues through the legacy path.

## Implementation tracker

### Phase 1: Establish the rendering and compatibility contract

- [ ] Define logical resolution, physical render resolution, world-art scale, and UI scale as distinct concepts.
- [ ] Define Classic and HD rendering modes, with Classic preserving the current asset selection and drawing behavior.
- [ ] Define how `ScreenWidth`, `ScreenHeight`, window size, and the new render scale combine without changing the current meaning of existing settings when HD mode is disabled.
- [ ] Define asset precedence so a higher-priority mod's classic replacement is not silently shadowed by a lower-priority HD asset.
- [ ] Classify all art-derived data as either visual or simulation-relevant and prohibit HD variants from changing simulation-relevant data.

### Phase 2: Introduce an asset-resolution layer

- [ ] Add an engine-owned asset identifier and resolver that can represent the current theater naming, archive precedence, loose-file behavior, and HD variants without exposing format-specific pointers to new code.
- [ ] Adapt legacy SHP, theater tile, PCX, font, VXL/HVA, palette, and fixed-name lookups to the resolver incrementally while preserving their existing lifetime and fallback behavior.
- [ ] Add owned decoded-asset and scaled-asset caches with explicit archive/theater invalidation and bounded memory use.
- [ ] Make a missing, malformed, or unsupported HD variant fall back to the selected source's classic asset with a useful diagnostic.

### Phase 3: Add logical-to-physical rendering scale

- [ ] Introduce a render context that converts logical draw coordinates, clipping rectangles, dirty rectangles, and depths into physical framebuffer coordinates.
- [ ] Accept world-space leptons (or an equivalent sub-pixel fixed-point form) at the render context's world-draw interface and perform the lepton-to-physical conversion in a single divide — the scale multiplies before the `CELL_LEPTON` division — so sub-logical-pixel positions, including the sub-tick render offsets from `plans/fps-fix.md`, survive scaling instead of being truncated to logical pixels first.
- [ ] Render HD mode into a framebuffer whose dimensions are logical resolution multiplied by the selected render scale.
- [ ] Scale the alpha buffer, depth buffer, voxel scratch surfaces, cached composites, and dirty-region tracking consistently with the physical framebuffer.
- [ ] Keep simulation coordinates, map cells, object footprints, selection rules, weapon offsets, save data, replay data, and network state in their existing units.
- [ ] Convert mouse and window coordinates back through presentation scale and render scale before existing hit testing receives them.
- [ ] Preserve final window fitting, aspect-ratio bars, and the existing nearest, linear, and pixel-art presentation filters after the higher-resolution frame has been composed.

### Phase 4: Support HD shape and interface assets

- [ ] Define a versioned HD art manifest that maps a logical asset name to one or more scale variants and records frame rectangles, logical canvas size, anchors, alpha behavior, remap masks, shadow frames, and source precedence.
- [ ] Use a lossless image representation for HD color and alpha, with house-color remapping stored as semantic mask data instead of inferred from generated RGB pixels.
- [ ] Draw an exact-scale HD frame one-to-one into the physical framebuffer while drawing a missing classic frame through a deterministic scaled fallback.
- [ ] Preserve SHP frame numbering, facings, sequence layout, theater selection, damaged states, buildup sequences, shadows, Z-shapes, and fixed-name interface behavior at the logical level.
- [ ] Allow a variant to declare an integer temporal multiplier per sequence — K sub-frames per logical frame — and an extended facing count, while simulation stage advancement, logical frame counts, timing, and facing arithmetic stay untouched. Sub-frame selection happens at draw time from directed intra-stage progress — a render-only accessor exposing the countdown timer's elapsed fraction of its own programmed interval (stage rates can change mid-interval without resetting the countdown, so the stored rate is not the interval) and the step direction; the stage state does not own sequence topology, so the accessor must not claim it — combined with the smooth-motion render clock's intra-tick fraction. The selector never predicts the next logical frame: it holds the terminal sub-frame at an interval boundary until the simulation advances the stage, and a sequence that declares reverse or ping-pong playback must supply direction-specific sub-frame blocks, because a forward block played backward morphs toward the wrong neighbouring frame. Define the accessor with the manifest field, and give the pair its own visual acceptance criterion and synthetic fixtures covering at least the animation owner's reverse, ping-pong, and looping playback, building start/count stage windows, infantry sequence swaps, and a rate change during an active interval — asserting continuity at reverse steps, loop edges, and ping-pong flips, not merely that indices stay in range — before any pack relies on it.
- [ ] Give world art and interface art separate scale domains so UI size can remain usable independently of world detail.
- [ ] Add pack validation for frame counts, dimensions, anchors, masks, scale consistency, resource limits, and unsafe image sizes before an asset reaches a draw path.

### Phase 5: Generalize terrain rendering

- [ ] Separate the logical 48 by 24 isometric cell geometry from the physical number of pixels used to render that diamond.
- [ ] Generate terrain span and clipping tables from the active render scale instead of fixed 48 by 23 draw tables.
- [ ] Define an HD terrain variant carrying color, alpha where applicable, extra cliff imagery, and scale-matched depth data while reusing legacy height, ramp, land type, and radar semantics.
- [ ] Scale terrain overlays, shroud, fog, bridges, slopes, cliff shadows, deformation, Z adjustment, preview rendering, and redraw bounds from the same logical terrain geometry.
- [ ] Permit individual HD tile sets to fall back to their classic theater files without creating seams or changing the map's cell data.

### Phase 6: Improve voxels and other visual families

- [ ] Scale the existing voxel projection and caches into the physical framebuffer so current VXL/HVA models render cleanly at the selected HD scale.
- [ ] Define how optional higher-density voxel variants preserve model-to-world size, pivots, layer transforms, normals, turret/barrel attachment, and shadows.
- [ ] Keep VXL/HVA and VOXELS.VPL compatibility as the baseline rather than requiring a new 3D model format for the first HD release.
- [ ] Define one render-facing bucket count that drives both the voxel transform quantization and every rasterization-cache key layout together — body, shadow, turret, barrel, and the frame and ramp bit packings — so finer rotation actually rasterizes finer orientations instead of re-keying the same 32, and bound the cache growth the finer count causes.
- [ ] Evaluate full-color fonts, cursors, loading screens, menu PCX images, movies, and radar graphics separately because their layout and scaling requirements differ from world sprites.

### Phase 7: Add a reproducible asset-authoring pipeline

- [ ] Provide a Windows-native command-line pack builder that validates manifests, imports lossless source images, builds runtime assets, and reports every fallback or lossy conversion.
- [ ] Support high-resolution source masters and deterministic down-conversion so the same project can produce HD variants and legacy SHP-compatible fallbacks.
- [ ] Provide preview tooling for animation sequences, facings, anchors, shadows, remap masks, palettes, and terrain seams before assets are packaged.
- [ ] Keep proprietary source assets, derived game assets, and generated packs outside this repository; commit only format documentation, synthetic fixtures, and reusable tooling.
- [ ] Treat generated imagery as an authoring input requiring frame consistency, perspective, mask, shadow, and animation review rather than as a directly shippable runtime asset.

### Phase 8: Validate, document, and land the feature

- [ ] Add synthetic asset fixtures that require no proprietary game data and cover classic fallback, scale selection, clipping, anchors, alpha, remapping, animation frames, terrain depth, and malformed inputs.
- [ ] Add deterministic image comparisons for representative world, UI, terrain, shadow, fog, lighting, and mixed classic/HD scenes at supported scales.
- [ ] Measure CPU cost, memory use, cache churn, and frame presentation at representative logical resolutions, HD scales, resize states, and supported bgfx backends.
- [ ] Build and run the relevant checks in the supported Visual Studio 2022 Win32 Debug and Release configurations, distinguishing build results from runtime visual evidence.
- [ ] Document the art-pack format, configuration, authoring workflow, fallback rules, compatibility behavior, resource limits, and migration guidance in their owning manual sections.
- [ ] Add a player-visible intentional-change record for HD rendering while stating that Classic mode preserves existing behavior.
- [ ] Commit the completed implementation and documentation with an imperative subject of at most 72 characters.

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

The first proof should implement a scale-2 physical framebuffer, inverse-scaled input, one manifest-backed SHP asset, and classic fallback for everything else. It should demonstrate one world sprite with a house-remap mask, alpha, shadow frames, stable logical placement, correct clipping, and unchanged selection behavior at Classic and HD scales. This isolates the core architecture before terrain, full-color composition, or broad asset production expands the scope.

The second proof should mix one HD world sprite, one HD interface element, a scaled legacy voxel, and entirely classic terrain in the same frame. Success would demonstrate that scale domains, resolver precedence, fallback, input mapping, caches, and presentation can coexist before the fixed terrain raster is generalized.

## Material risks

- The inherited renderer frequently uses the same pixel constants for layout, projection, clipping, and buffer indexing; missing one path can produce subtle visual or memory errors.
- A 2x render scale uses roughly four times the pixels, while 4x uses roughly sixteen times, before extra caches and full-color buffers are counted.
- The current software blitters and assembly paths assume specific pixel formats and unscaled source-to-destination copies, so a scaled or 32-bit path requires carefully bounded replacements rather than flag additions scattered across every call site.
- Asset-pack precedence can break existing mods if scale selection is performed before override selection.
- Generated animation frames can drift in geometry, facing, lighting, and identity; deterministic tooling and review remain necessary even when generation accelerates source-art creation.
- HD terrain depth, fog, shadows, and overlays can look correct in a still image while sorting incorrectly in motion, so runtime visual evidence is essential in addition to image comparisons.
