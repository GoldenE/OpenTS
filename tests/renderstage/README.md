# Render-stage sampling

`RenderStageTest` compiles the production `StageClass`, countdown timer, directed render sampler, and HD frame selector. It exercises programmed intervals, rate changes during an active countdown, held/unsupported steps, interval saturation, owner overrides, nested scope restoration, and read-only serialized stage bytes. Building cache checks require redraw on a changed subframe, avoid repeated redraw for the same one, and expire absent owners.

The owner-continuity slice extracts the actual ping-pong and loop-boundary predicates from `AnimClass::AI` at configuration. It advances the production stage timer, applies those owner transitions, and checks synthetic color continuity through forward loops, the reverse loop's actual `3,2,1` cycle, and both ping-pong flips. Reverse frame zero is excluded by that owner's reset predicate; the authored reverse block for frame one therefore joins frame three. A generic reverse-modulo test cannot establish that contract.

The test does not execute the rest of animation AI, gameplay damage, object lifetime, rendering, or a live save/load operation. Those require the integrated renderer and runtime evidence. All input artwork and serialized snapshots are synthetic.

```powershell
cmake --build build/Win32 --config Debug --target RenderStageTest
ctest --test-dir build/Win32 -C Debug -R '^renderstage$' --output-on-failure
```

Use the corresponding x64 tree or Release configuration for the other supported targets.
