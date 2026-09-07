# HD asset checks

The HD checks require no game files. Build `HDAssetsTest`, `HDRuntimeTest`, and `HDShapeTest`, then run their CTest entries together with the optional Python authoring test:

```powershell
cmake --build baseline/build-hd-assets --config Debug --target HDAssetsTest HDRuntimeTest HDShapeTest
ctest --test-dir baseline/build-hd-assets -C Debug -R '^hd(assets|runtime|shape|pack)$' --output-on-failure
```

Use a supported [build configuration](../../docs/BUILDING.md), replacing the tree and configuration as appropriate. The authoring CTest is registered when Python 3.10 or newer is available.

`HDAssetsTest` links the production codec, SHA-256 and cache. It checks deterministic little-endian round trips, every pack truncation, unsupported versions, unsafe names, resource/plane errors, missing semantic blocks, selected-source identity, policy/generation/digest distinctions, pinned invalidation, and directed temporal/facing selection. Its synthetic VXL covers all truncations and invalid normals, runs, reverse counts and unsupported motion payloads. `--validate <pack.HDP>` validates an external pack with the production decoder and requires an identical re-encoding; the authoring tool uses that mode.

`HDRuntimeTest` compiles the production registry function bodies with in-memory archive/raw-file transport substitutes. It checks the Classic gate, exact archive selection, captured loose paths, pointer release, archive/theater invalidation, cache hits, pinned accounting through reconfiguration, pressure retries without repeated reads while pins prevent insertion, source token/size queries, parsed-owner aliases, temporary palette scopes, retained file-family capture, read-position preservation, the binding ceiling, and a late static-destructor callback. It does not exercise the real `CCFileClass::Open` implementation or disk MIX parsing; those require integration evidence.

`HDShapeTest` compiles the production compositor with small surfaces, bounded alpha/depth rings, converter tables, and shape metadata substitutes. It compares exact pixel values for one-to-one HD samples, centering, clipping/residuals, straight alpha, semantic remap/shadow, packed translucency, logical depth tests/writes, fractional gradient recurrence, signed legacy Z-shape values, metadata mismatch and unsupported-effect fallback. These checks exercise the helper arithmetic, not the real surface backend, complete game palettes, or live-world composition.

`hdpack` invokes the standard-library Python tests in [the pack tooling](../../tools/hdpack/README.md) with the built native validator. It verifies PNG CRC/byte preservation, stable binary encoding, rejected masks/partial temporal blocks, paired SHP headers/envelopes, explicit absent-frame fallback reporting, identical lossless preview exports, raw/RLE palette-applied fallback frames and swatches, assembled terrain seam continuity and depth/alpha composition, classic terrain palette fallback, layout limits, deliberate legacy conversion reporting, retained SHP metadata, and the digest relationship between a generated fallback and its HDP.

The reusable `tools/hdpack/make_proof.py` generator creates wholly synthetic world/cameo SHPs, HDP variants, PNG masters/previews and an unencrypted MIX at an explicitly supplied output directory. It never stages a runtime directory. Real rendering, UI input, archive loader integration, backend behavior, performance, and replay/simulation preservation remain separate runtime acceptance work.

## Retained source adapters

`Query_Source(owner)` returns a value token containing canonical name, captured source identity, original lookup policy, registration generation and original-file SHA-256. Repeated retrieval of the same cached binding preserves its token; replacement changes generation. `Query_Source_Size(owner)` returns the full byte extent only for an original raw whole-file buffer. Parsed surfaces, fonts, palettes/converters and HVA/VPL owners carry provenance without exposing a fictitious raw span.

`Register_Stream(file, owner)` captures a bounded whole-file digest from an already-open `CCFileClass`, restores its read position, and binds a parsed owner. It does not change the consumer's open/close policy or replace any classic data. `Rebind` transfers a completed raw registration to a parsed owner; `Alias` copies provenance to another independently owned decoded object. `ScopedOwner` cleans temporary palette aliases. Owners call `Forget` at replacement/destruction; copied decoded provenance can survive a temporary source object's scope, but archive/theater invalidation removes all dependent bindings. The registry and its recursive mutex deliberately live until process termination, so late static destructors can unregister safely.

PCX decoded surfaces, cached FNT wrappers, HVA libraries, retained palette/converter owners and VPL outputs use these adapters. Classic consumers and representations stay authoritative; these registrations do not introduce HD replacement formats for PCX, legacy FNT, PAL/VPL, or HVA. `PaletteClass` retains its 768-byte layout, trivial copy construction/destruction and raw-overlay compatibility; palette registrations live in actual load/release owners rather than changing its object model. `ShapeSet` remains eight bytes. Production compile assertions protect those layouts.
