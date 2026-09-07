# HD world rendering contracts

`HDWorldTest` compiles the production terrain raster, shroud/fog alpha writer, voxel stamp, facing/key helpers, and static voxel cache with small synthetic surfaces, palette tables and asset bindings. It requires no game assets.

The checks cover every 16-bit facing value in Classic and HD modes, collision-free ramp/facing/frame keys, all terrain span source offsets and coverage at densities 1–4, physical RGBA terrain detail and logical depth, shroud transparency and fog blending, fractional voxel projection and clipped depth writes, and density-aware RLE cache rows including long transparent runs, alignment and failed reservations.

`HDSceneTest` assembles one synthetic scene through real surfaces, clipping, alpha/depth rings, lighting tables, blitters and UI copying. It compiles the full production Classic/HD terrain raster and HD shape compositor with small decoded-metadata, converter and asset-lookup adapters, and combines them with the production voxel stamp and both spotlight brightening paths. The scene contains clipped tiles, shroud, fog, foreground and occluded shapes, semantic remapping, alpha/shadow coverage, an unreplaced classic sprite, fractional voxel placement, two light pools and an independently composed UI layer. The scene exercises composition; asset loading and backend presentation have separate checks.

The assembled checks compare every fallback sample against the Classic image at densities 1–4, verify unchanged composition after signed ring panning, assert independent UI/occlusion/voxel/spotlight pixels, and freeze eight complete image hashes. A changed-pixel negative control checks the digest guard. The optional output directory exports the same corpus as lossless BMP files for inspection; these generated files are not repository inputs.

A focused particle fixture exercises partial visibility within one logical pixel: depth occlusion, zero alpha, dimmed alpha and a fully lit sample coexist in the same physical block. Ring allocation tests reject maximum signed dimensions before multiplication, alongside the existing physical-address and pan checks.

```powershell
cmake --build build/Win32 --config Debug --target HDWorldTest HDSceneTest RingBuffersTest
ctest --test-dir build/Win32 -C Debug -R '^(hdworld|hdscene|ringbuffers)$' --output-on-failure
build/Win32/tests/hdworld/Debug/HDSceneTest.exe baseline/hd-scene-corpus
```

Use `Release` for optimized checks and `build/x64` for the native target. `RingBuffersTest` separately exercises the production circular alpha/depth storage at densities 1–4, including signed pan, wrapping, logical depth bias and copies. These are composition and memory contracts; they do not establish complete scene placement, model loading, gameplay compatibility, presentation performance or live visual acceptance.
