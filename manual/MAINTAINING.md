# Maintaining the manual system

This document covers the contracts behind extraction, validation, releases,
routes, and publication. Ordinary content work should begin with
[Authoring](AUTHORING.md).

## Authorities

| Concern | Authority |
| --- | --- |
| Python version and packages | `tools/.python-version` and `tools/requirements.txt` |
| Node and npm versions | `site/.nvmrc` and `site/package.json` |
| Generated INI, scripting, and command records | Current engine source plus the extractors invoked by `tools/manage.py` |
| Data and frontmatter shapes | JSON Schemas under `schema/` |
| Releases | `data/releases.yaml` |
| Engine lifecycle | Markdown records directly under `changes/` |
| Removed public entities | `data/tombstones.yaml` |
| Numeric scripting compatibility paths | `data/scripting-route-aliases.yaml` |
| Direct controls and launch parser classification | `data/command-adapters.yaml` |
| Exceptional INI read classification | `data/adjudications.yaml` and `data/ini-read-exclusions.yaml` |
| Site dependency graph | `site/package-lock.json` |

Python and Astro consume the same schemas through adapters. A schema change is
a manual-format change and must update both consumers and their tests. Do not
change a schema, extractor, adapter, exclusion, route registry, or tombstone as
an indirect way to silence validation.

## Generation and validation

`tools/manage.py` is the contributor interface. Its `update` transaction runs
`extract.py`, `scripting.py`, and `commands.py`, verifies that all three outputs
were produced, then replaces the tracked catalogs with rollback on an
operating-system failure. Keep this transaction atomic when changing the
launcher.

`check` generates the same three files in a temporary directory and compares
them with the tracked copies before running authored-data validation and the
site checks. Preserve that read-only relationship to tracked catalogs.

The repository-wide typed-INI inventory is fail-closed. Add an exclusion only
for a genuinely non-public or exceptional read and provide a durable reason. An
`excluded` rule also keeps its read out of the catalog where an enrolled reader
owns the site, so assembly and classification always agree. Correct public scope
disagreements in the extractor or an explicit adjudication.

An entry name is whatever stands to the left of `=`, digits included; the
extractor and the inventory share one contract for it, and a read's entry name
is always the argument after the section. Keep the scanners on that shared
contract, or a site can be extracted without ever being inventoried.
`tools/extraction_history.py` depends on it too, so a change to either scanner
also decides how a catalog delta is classified.

Generated provenance records stable identities, never source positions. A scope
cites the file holding its read together with the class and member it fills, and
a command cites its registering class. Line numbers stay extraction diagnostics
and are never serialized, so an engine commit that only moves code cannot drift
a tracked catalog. `tools/extraction_history.py` relocates a key's reads by
scanning the cited file with the same inventory scanner both revisions share and
comparing accessor statement text, which tells an extractor coverage correction
from a genuine engine change without depending on where a read sits.

A key's scopes are settled on recorded content rather than extraction order. Two
readers that make the same read for one concrete type fold into the declaration
covering the widest family, so adding a unit cannot split or merge a published
scope; `tools/tests/test_scope_coalescing.py` re-extracts with the unit lists
reversed and holds every key to its published scopes.

A reading is identified by its scope route, and only `allKeys` settles those
routes, because it is what distinguishes two readings that would otherwise
share one. Every table listing a section resolves its rows through `allKeys`
rather than deriving a route of its own; a row carrying an underived route
takes the first reading's omission and effect instead of its own. Where one
section reads a spelling more than once, the reading's authored `label:` is
what tells the rows apart, and a reading that does nothing is left out while
the spelling still works there, so a live key is never presented as dead.

Command discovery is also fail-closed. Objects registered through
`AllCommands` form the rebindable command catalog. Every discovered direct key
handler and launch-parser branch must have exactly one public adapter or one
reasoned exclusion. Preserve exact command ID case, and do not infer default
bindings from a declaration or nearby code.

Enums are authored selections backed by explicit source adapters. Adding a page
for an existing fixed domain is documentation work. An adapter must preserve
the domain's constants, stored values, public tokens, and order. Dynamic
registries are not enum adapters.

Formats are authored contracts. A format's structured fields own filenames,
extensions, registrations, positional fields, companions, and key-scope
selectors. The four established AI records keep their compatibility routes:

- `/mapping/team-types/`
- `/mapping/task-forces/`
- `/mapping/scripts/`
- `/mapping/ai-triggers/`

Other format routes default to `/formats/<filename-stem>/`.

## Releases and lifecycle

`data/releases.yaml` contains complete SemVer 2.0 versions without build
metadata. It must contain exactly one `development` version, that version must
be the highest entry, and only `released` entries carry an ISO date. The numeric
core of the development version must match the version in CMake's
`project(OpenTS VERSION ...)` declaration, and its prerelease label must match
CMake's `OPENTS_VERSION_PRERELEASE`, which is empty when the development version
has no label. The private npm package version is tooling metadata, not the
OpenTS release.

The engine stamps its saves and network sessions with that version and the target architecture. Opening a cycle therefore retires the previous cycle's saves, while snapshots produced during one active cycle and architecture share a stamp and have no interoperability promise.
Several compatibility-breaking changes may accumulate before that cycle is
released. See [Compatibility boundaries](../CONTRIBUTING.md).

To publish a release:

1. Confirm the development entry names the version being released and that the
   commit to be tagged carries everything the release ships.
2. Create and publish the GitHub release from a tag `v<version>` on that
   commit. The `Engine release` workflow builds the tag for Win32 and x64, attaches separate `OpenTS-<tag>-Win32.zip` and `OpenTS-<tag>-x64.zip` archives, and appends the output of
   `python manual/tools/manage.py release-notes <version>` to the release
   body.
3. Open the next development cycle only after tagging, so the tag points at a
   commit whose CMake version still names the released version.

`release-notes` renders the change records assigned to one release as
Markdown on standard output: breaking changes with their migration steps
first, then the remaining records grouped by category. It reads the same
records the lifecycle checks validate and refuses a version no record
targets.

To open the next development cycle:

1. Mark the current development entry `released` and add its ISO release date.
2. Add one higher development version.
3. Update the CMake project version when the numeric core changes, and the CMake
   prerelease label when the label changes.
4. Run `python manual/tools/manage.py check`.

Do not move existing change records to the new cycle. A record's release
assignment is stable. After release, its category, targets, breaking state, and
migration steps are immutable; released registry entries and dates are also
immutable.

The catalog present when structured lifecycle tracking began is the baseline.
Baseline entities have no addition event. New engine entities and deliberate
behavior changes require records in the current development release. Removed
entities require one authoritative removal target and a tombstone at their
established route. Active indexes omit tombstones, while direct navigation and
search can still explain the removed identity.

## Public routes

Artifact checks derive their expectations from the content tree, `data/`, and
`site/src/i18n/en.mjs`, and compare them against the built HTML. Write an
expectation out by hand only where the value is a judgement rather than a
consequence: retired copy that must not return, which settings deserve which
badge, the order of the top-level views, and route-stability contracts. Adding a
page must never require editing a check, and a check that has to be edited
alongside content is reporting the edit rather than a regression.

Published manual routes are stable. Moving or removing a page must preserve its
established URL with the appropriate redirect, alias, or tombstone and an
artifact-level test. A title change must not change its route accidentally.

Numeric scripting indices are serialized engine identities. Their compatibility
paths are tracked in `data/scripting-route-aliases.yaml`; never reassign a
reserved numeric path to a different engine ID. Removed keys, scripting
entities, formats, enums, systems, and commands use tombstones. A tombstone's
removal version comes from its lifecycle record rather than duplicated data.

Every route change must be deliberate and reviewed with the rendered route
diff.

## Publication

The site reads these build-time settings:

| Variable | Meaning |
| --- | --- |
| `DOCS_SITE` | Deployment origin without a path |
| `DOCS_BASE` | Repository or preview path prefix |
| `DOCS_REPOSITORY_URL` | Source repository URL used for source and feedback links |
| `DOCS_REVISION` | Revision used in source links and feedback metadata |
| `DOCS_DEMO` | Explicitly marks an alternate build as a demo |

The Pages workflow derives the repository URL and project path from GitHub's
repository context so the same workflow works in staging and the final OpenTS
repository. Builds under `/Docs-Demo`, or builds with `DOCS_DEMO=1`, omit the
community link intended only for the official publication.

CI runs the complete manual check, then installs production-only dependencies,
rebuilds, and verifies the artifact before upload. The check builds with the
synthetic removed-entity fixtures, which are the only tombstone pages the manual
has, so its artifact checks cover a superset of the published pages. Proving
those pages stay out of a publishable artifact belongs to the workflow rebuild,
which is the only build that runs without them; the variable that admits them is
covered on its own by a test that needs no build. Keep those two halves
together when changing either.

## Maintainer validation

The rule [Public routes](#public-routes) states for artifact checks governs the
Python and node contract suites too: an expectation that mirrors generated data
is derived from it, and a value is written out only where it is a judgement, such
as a frozen route, a stored enum value, or a forced key binding. A record count is
never such a value; assert what every record satisfies instead.

Run the narrowest affected tests first. Before handoff, run:

```powershell
python manual/tools/manage.py check
```

For a visible site change, also inspect representative desktop and narrow
layouts through `serve`. For governance changes, run the repository Markdown
link checker and confirm that no rendered source, content, generated data, or
route inventory changed. Report exact results and material checks not run.
