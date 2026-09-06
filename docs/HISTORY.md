# History

This document records where the OpenTS source came from and how it was
reconstructed. The controlling license text and additional terms are in
[LICENSE.md](../LICENSE.md); [ACKNOWLEDGEMENTS.md](../ACKNOWLEDGEMENTS.md)
thanks the people and projects named here.

## The original executable

Westwood Studios released *Command & Conquer: Tiberian Sun* in 1999 and the
*Firestorm* expansion in 2000. Compiling a large C++ game discards most of
what made its source understandable: names, types, comments, file structure,
and intent. For decades, changing Tiberian Sun therefore meant examining
disassembly or decompiled pseudocode, inferring the intended mechanics, and
injecting patches into the retail executable. That approach enabled years of
community work, but every change was expensive and fragile.

OpenTS replaces that model with ordinary source development: read the
implementation, edit it, compile it, and test the result.

The reconstruction's reference target is the latest English (US) executable:

```text
GAME.EXE; v2.03[EN]; Monday 5th June, 2000 (21:26:42)
MD5: C2C58CBBF83AF0458DC44EF64A3C011F
```

## Source foundation

Westwood shared code and libraries across game generations, so Electronic
Arts' source releases for related Command & Conquer titles provide much of
the foundation Tiberian Sun was built on, though not the Tiberian Sun-specific
engine in finished form. The reconstruction began from the GPL-licensed Red
Alert source included with the
[2020 Command & Conquer Remastered Collection](https://github.com/electronicarts/CnC_Remastered_Collection),
was later rebased onto the fuller
[Red Alert source](https://github.com/electronicarts/CnC_Red_Alert) Electronic
Arts released in 2025 alongside The Ultimate Collection, and incorporates
applicable shared code from the
[Tiberian Dawn](https://github.com/electronicarts/CnC_Tiberian_Dawn),
[Renegade](https://github.com/electronicarts/CnC_Renegade), and
[Generals and Zero Hour](https://github.com/electronicarts/CnC_Generals_Zero_Hour)
repositories.

The missing Tiberian Sun-specific code and behavior were reconstructed from
those public releases and the shipped game through reverse engineering; no
other Westwood source material was involved. Files derived from Electronic
Arts source carry the applicable Electronic Arts notices, and the additional
GPL Section 7 terms in [LICENSE.md](../LICENSE.md) apply to that material.

## Function-level matching

The current reconstruction effort began in 2024. Functions were reconstructed
in C++, compiled with the historical compiler environment used for matching,
and compared instruction by instruction against the corresponding functions
in the reference executable, using decompilers and disassemblers, objdiff,
custom build and comparison tooling, and runtime testing. Exact machine-code
matching is stronger evidence than observing that a feature appears to work,
while runtime testing remains important for whole-game behavior.

All approximately 11,150 known functions are implemented. Around 500 are
non-exact: they compile into different instruction sequences and have not
been proven identical, although they may still be behaviorally correct, and
no user-visible divergence is currently known in them. The archived baseline
reaches approximately 98% weighted, penalty-adjusted instruction-level
matching against the reference executable. That score is instruction-based
and function-level: it is not a byte-similarity measure, not a percentage of
game features, and not the percentage of functions that match exactly. The
tooling behind the matching, how the original module layout was recovered,
and the technical reasons the remaining functions and data resist matching
are recorded in [Rationale](RATIONALE.md).

AI models — Codex, Claude, and DeepSeek — substantially accelerated the final
phase. They were given disassembly, decompiled pseudocode, surrounding
reconstructed source, engine context, compiler errors, and binary-diff
feedback, and helped interpret functions, generate candidate implementations,
and iterate toward closer matches. Their output was never treated as
authoritative: every candidate was reviewed by developers, compiled, compared
against the reference executable.

## From TibSun to OpenTS

The reconstruction is preserved in two repositories with deliberately
different purposes.

The [TibSun archive](https://github.com/OpenTS-Developers/TibSun) freezes the
cleaned matching baseline as a code-only archive. Its purpose is historical
reference, future matching work, and comparison against the original game.
OpenTS derives from that baseline; "derives from" describes source
provenance, not Git topology, and the OpenTS history does not claim the
archive as a Git ancestor.

OpenTS is the active project: C++20, CMake, and Visual Studio 2022, the
repository that builds releases, accepts contributions, and hosts the manual.
It intentionally diverges from the original executable as modernization
proceeds, so binary matching changes roles here: instruction-level comparison
remains the strongest way to establish what inherited behavior is, but a
proposed OpenTS change is judged by the project's goals and its compatibility
effects, with matching used to establish the starting behavior — not as a
correctness gate the result must pass.

OpenTS published its first release, 0.1.0, on 27 August 2026.

## Native x64 development

The active tree adds native x64 Windows builds while retaining Win32. Portable C++ replaces the remaining production x86 assembly, with synthetic input/output records captured from the Win32 implementations before retirement. Pointer-width corrections keep runtime addresses separate from fixed-width game-data records; saves, recordings, and network identities distinguish incompatible native layouts. The supported configurations and verification boundary are owned by [Building OpenTS](BUILDING.md), and [Runtime testing](TESTING.md) owns live validation.
