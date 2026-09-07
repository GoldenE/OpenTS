# Logical and physical rendering contracts

`RenderCoreTest` builds the production render context, software and GDI surfaces, SHP fallback blitters, alpha/depth rings, and color routines. Its fixtures generate their own pixels and compressed rows and require no game assets. The UI fixtures extract the production cache and owner-drawn painters; the extraction fails configuration when a named function cannot be found.

The checks cover scale validation and resource limits; source/UI/world density domains; physical allocations and fills; mixed-density and overlapping copies; clipped source alignment; preserved physical detail; RLE transparency; all four legacy logical depth gradients; world-origin residuals and nested screen-space restoration; multiply-before-divide projection; physical depth-line writes; GDI coordinate mapping; and UI masks, blending, gradients, bevels and edge thickness at densities 1 through 4.

The startup regression derives the order of configuration, render-settings, video, primary-surface, working-surface and game initialization calls from the production startup source. It follows that order using real primary/working surfaces and A/Z rings at Classic and HD scales 1–4, rejecting a late settings load or mismatched allocation densities. This covers the new-scenario path separately from save loading, which recreates the surfaces.

Registered immutable SHP frames reuse owned, decoded and horizontally scaled rows. Keys include the registered source generation and content digest, logical frame, dimensions, source density, target density and pixel representation. Archive/theater invalidation epochs make prior entries unreachable; active readers keep their owned pixels until their draw finishes. The cache retains at most the smaller of 64 MiB and one quarter of `ArtCacheMB`, with a 4,096-entry cap. These are additional rendering working-set limits; `ArtCacheMB` itself continues to bound decoded HD packs.

Unregistered or mutable raster inputs rebuild their pixels on every draw using reusable row scratch. Four nested scratch slots retain at most 256 KiB each; larger working buffers and deeper calls release their storage at the end of the draw. UI mask normalization reuses up to eight scratch surfaces, retaining at most the smaller of 16 MiB and one sixteenth of `ArtCacheMB`. Every lease refills from the current source and mask. Oversize or concurrently leased surfaces remain temporary. Tests cover immutable cache hits, replacement generations, invalidation, pinning and eviction, malformed input, cached clipping, mutable-source/mask changes, and nested scratch ownership.

Build and run with a supported Visual Studio 2022 tree:

```powershell
cmake --build baseline/build-hd-core --config Debug --target RenderCoreTest
ctest --test-dir baseline/build-hd-core -C Debug -R '^rendercore$' --output-on-failure
```

Run the same target in Release and both supported architectures for a final rendering change. These are synthetic composition and bounds checks, not live game or backend presentation evidence. The owning HD settings and asset contracts are documented in the manual.
