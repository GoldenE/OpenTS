# Vanilla baseline replay driver

`Invoke-VanillaBaseline.ps1` replays the recordings named in a golden manifest
against the local Debug build and compares the sync dumps the engine writes with
the golden dumps. [docs/TESTING.md](../../docs/TESTING.md) owns the procedure
and what the result means; this file owns the driver's usage.

The driver requires proprietary game data in `Run/` and a Debug build, because
recording playback exists only in Debug builds. It is not part of the CTest
suite and cannot run in continuous integration.

## Usage

```powershell
.\tools\baseline\Invoke-VanillaBaseline.ps1 [-Manifest <path>] [-OutputDirectory <path>]
                                            [-Capture] [-Session <name>...] [-TimeoutSeconds <n>]
```

| Parameter | Meaning |
| --- | --- |
| `-Manifest` | Golden manifest; defaults to `baseline/golden/manifest.json` |
| `-OutputDirectory` | Where dumps and `report.txt` go; defaults to `baseline/runs/<timestamp>` |
| `-Capture` | Write this run's dumps as the golden dumps instead of comparing |
| `-Session` | Run only the named sessions |
| `-TimeoutSeconds` | Kill a replay that has not exited in time; default 600 |

For each session the driver copies the recording to `Run/RECORD.BIN`, writes
`[SyncBug] PrintCRC` in `Run/SUN.INI`, runs `GameD.exe -WIN -XY` minimized,
waits for it to exit, and collects `Run/SYNC*.TXT`. The previous `PrintCRC`
value is restored afterwards.

Statuses: `MATCH`, `DIFF`, `CAPTURED`, `MISSING` (recording or golden dump),
`NODUMP` (the engine exited without writing one; read `Run/Debug` and
`Run/Exceptions`), `TIMEOUT`. The exit code is 1 unless every session is
`MATCH` or `CAPTURED`. It fails with a clear message when `Run/` holds no game
data or there is no Debug executable.

## Manifest

```json
{
  "pinnedCommit": "<full commit>",
  "toolchain": { "...": "..." },
  "dataManifest": "../manifest/run-data-files.csv",
  "printCrcFrame": 300,
  "sessions": [
    { "name": "ts-gdi01", "recording": "ts-gdi01.bin", "scenario": "GDI1A.MAP", "type": "campaign", "note": "..." }
  ]
}
```

Recordings and golden dumps (`<name>.sync.txt`) sit beside the manifest.
