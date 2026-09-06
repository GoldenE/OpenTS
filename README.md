<p align="center">
  <img src="https://raw.githubusercontent.com/OpenTS-Developers/.github/main/assets/opents-logo.png" alt="OpenTS" width="512">
</p>

<p align="center">
  <a href="https://github.com/OpenTS-Developers/OpenTS/releases"><img src="https://img.shields.io/github/downloads/OpenTS-Developers/OpenTS/total?label=downloads" alt="Downloads"></a>
  <a href="https://github.com/OpenTS-Developers/OpenTS/actions/workflows/engine.yml"><img src="https://github.com/OpenTS-Developers/OpenTS/actions/workflows/engine.yml/badge.svg" alt="Engine build"></a>
  <a href="https://opents-developers.github.io/OpenTS/"><img src="https://github.com/OpenTS-Developers/OpenTS/actions/workflows/manual-pages.yml/badge.svg" alt="Manual"></a>
  <a href="https://www.patreon.com/c/ZivDero"><img src="https://img.shields.io/badge/Patreon-ZivDero-F96854?logo=patreon&logoColor=white" alt="Patreon"></a>
</p>

# OpenTS

OpenTS is a community-led, open-source reconstruction of *Command & Conquer:
Tiberian Sun*. It forgoes patching and extensions to rebuild the original
engine standalone from scratch. The project gives equal weight to maintaining
a playable engine and providing a capable platform for modding and engine
development.

OpenTS is:

- an independent, community-led source reconstruction targeting the behavior
  of Tiberian Sun 2.03 Firestorm;
- a playable engine built from Electronic Arts' GPL-released source for
  related Command & Conquer games and Tiberian Sun-specific reverse
  engineering;
- the active foundation for continued maintenance, documentation, technical
  modernization, bug fixes, and new modding capabilities.

OpenTS is not:

- a remaster or a remake;
- an official Electronic Arts source release;
- a distribution of the original game assets.

OpenTS is an independent community project and is not affiliated with or
endorsed by Electronic Arts.

## Community

- Discord: <https://opents.net/discord>
- Bug reports and proposals:
  [GitHub issues](https://github.com/OpenTS-Developers/OpenTS/issues)

## Downloads

- **Releases** are the stable, recommended builds, published on the
  [releases page](https://github.com/OpenTS-Developers/OpenTS/releases). Each
  release carries a zip with `Game.exe`, `Language.dll`, and the `Game.pdb`
  symbol file.
- **Nightly builds** are development snapshots produced by the
  [Engine nightly](https://github.com/OpenTS-Developers/OpenTS/actions/workflows/engine-nightly.yml)
  workflow on days with new work. The latest nightly can be downloaded
  without a GitHub account through
  [nightly.link](https://nightly.link/OpenTS-Developers/OpenTS/workflows/engine-nightly/main).
  Nightlies carry the latest merged changes without release validation and
  expire after 90 days. Use them for testing, at your own risk.

## Installing

1. Install Tiberian Sun from Command & Conquer The Ultimate Collection on
   Steam or the EA App.
2. Extract the release zip into the Tiberian Sun game directory.
3. Run `Game.exe`.

OpenTS supplies the engine, not the game data: the installation above
provides the original assets, which OpenTS does not distribute. There is no
installer, and no additional runtime libraries or launch arguments are
required. Windows is the supported platform; Wine is expected to work but is
not officially supported, and there is no native Linux build.

## Documentation

The [OpenTS manual](https://opents-developers.github.io/OpenTS/) documents
setup, runtime behavior, INI configuration, mapping, and source-level
internals.

## State and plans

Release 0.1.0 delivers the complete Tiberian Sun 2.03 Firestorm game,
together with the bug fixes and improvements listed in its release notes.
The GDI and Nod campaigns, the Firestorm campaigns, skirmish, and saving and
loading are
functional and have received full play-through testing; LAN multiplayer is
functional with more limited testing. No user-visible regression from the
original game is currently known. The renderer is built on
[bgfx](https://github.com/bkaradzic/bgfx) and supports modern resolutions
through 4K, including ultrawide.

Development continues toward:

1. CnCNet and CnCNet client support, including porting the parts of
   [ts-patches](https://github.com/CnCNet/ts-patches) this requires.
2. Feature parity with
   [Vinifera](https://github.com/Vinifera-Developers/Vinifera) and the rest
   of ts-patches.
3. Extending Tiberian Sun with new features, striving toward feature parity
   with Red Alert 2 and Yuri's Revenge, and growing engine capabilities that
   match or exceed the popular Yuri's Revenge engine extensions.

Alongside these goals, the engine is modernized incrementally toward an
entity-component architecture, and new development is shaped so that
migration stays possible. [Project direction](docs/DIRECTION.md) records the
reasoning.

## Building

OpenTS builds Win32 and native x64 Windows targets with Visual Studio 2022 and CMake.
[Building OpenTS](docs/BUILDING.md) documents the exact requirements,
commands, and outputs.

## Contributing

Bug reports, proposals, documentation improvements, and focused pull requests
are welcome. Read [CONTRIBUTING.md](CONTRIBUTING.md) before starting a
change. Source conventions are in [Style](docs/STYLE.md).

## Origins

OpenTS continues the community reconstruction preserved in the
[TibSun archive](https://github.com/OpenTS-Developers/TibSun), built from
Electronic Arts' published source for related Command & Conquer games and
completed through reverse engineering against the original executable.
[History](docs/HISTORY.md) records the reconstruction's lineage and methods.

## License and acknowledgements

OpenTS is licensed under the GNU General Public License, version 3 or later.
Material derived from Electronic Arts source remains subject to the additional
GPL Section 7 terms in [LICENSE.md](LICENSE.md).

[ACKNOWLEDGEMENTS.md](ACKNOWLEDGEMENTS.md) thanks the people, projects, and
communities whose work made OpenTS possible.
