#Requires -Version 7.0
<#
.SYNOPSIS
Replays the vanilla baseline recordings against a Debug build and compares the
resulting sync dumps with the golden dumps.

.DESCRIPTION
For each session named in the manifest, the recording is copied to Run/RECORD.BIN,
the CRC dump frame is written to Run/SUN.INI, and GameD.exe is run minimized with
-XY so the engine replays the recording, writes SYNC0.TXT at that frame, and
exits. The dump is stored under the run directory and compared with the golden
dump line by line, ignoring the build-identity header. A difference reports the
first differing line and, for a CRC ring entry, the simulation frame it covers.

This is a local contributor tool. It needs a populated Run/ tree built from
legitimately owned game data and a Debug build, since recording playback exists
only in Debug builds. It is not part of the CTest suite and cannot run in
continuous integration.

.PARAMETER Manifest
Path of the golden manifest. Defaults to baseline/golden/manifest.json.

.PARAMETER OutputDirectory
Where this run's dumps and report go. Defaults to baseline/runs/<timestamp>.

.PARAMETER Capture
Write this run's dumps as the new golden dumps instead of comparing.

.PARAMETER Session
Run only the named sessions.

.PARAMETER TimeoutSeconds
How long one replay may take before it is killed and reported as failed.
#>
[CmdletBinding()]
param(
	[string] $Manifest,
	[string] $OutputDirectory,
	[switch] $Capture,
	[string[]] $Session,
	[int] $TimeoutSeconds = 600
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
Import-Module (Join-Path $repo 'tools\dev\OpenTS.Dev.psm1') -Force

if (-not $Manifest) { $Manifest = Join-Path $repo 'baseline\golden\manifest.json' }
if (-not (Test-Path $Manifest)) { throw "Manifest not found: $Manifest. Capture a golden set first." }
$manifestDir = Split-Path (Resolve-Path $Manifest)
$data = Get-Content $Manifest -Raw | ConvertFrom-Json

$runDir = Get-OpenTSRunDir
if (-not (Test-Path (Join-Path $runDir 'TIBSUN.MIX'))) {
	throw "Run/ is not populated with game data (no TIBSUN.MIX in $runDir). Stage the retail files first."
}
$exe = Get-OpenTSExecutable Debug
if (-not (Test-Path $exe)) {
	throw "No Debug executable at $exe. Playback (-XY) exists only in Debug builds; build Debug first."
}

if (-not $OutputDirectory) {
	$OutputDirectory = Join-Path $repo ('baseline\runs\' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
}
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null

# Lines whose content depends on the machine or the build rather than the simulation.
$ignoredHeader = '^(Version |Internal Version |Release Build: |CPU vendor: |Average FPS: |Local address: |Name: |Address: |Max avg round trip: |Max round trip: |Resends: |Frame sync stalls: |Command cound stalls: |Lost: |Percent lost: )'

function Get-ComparableLines([string] $path) {
	return @(Get-Content $path | Where-Object { $_ -notmatch $ignoredHeader })
}

function Get-FrameForCrcSlot([int] $slot, [int] $printFrame) {
	# CRC[i] holds the most recent frame f <= printFrame with (f & 255) == i.
	$frame = $printFrame
	while (($frame -band 255) -ne $slot) { $frame-- }
	return $frame
}

$sessions = @($data.sessions)
if ($Session) { $sessions = @($sessions | Where-Object { $Session -contains $_.name }) }
if ($sessions.Count -eq 0) { throw 'No sessions selected.' }

$printFrame = [int] $data.printCrcFrame
$previousPrintCrc = Get-OpenTSSetting SyncBug PrintCRC
$recordPath = Join-Path $runDir 'RECORD.BIN'
# The engine names the dump SYNC<n>.TXT after the player's house, so any one counts.
$syncGlob = Join-Path $runDir 'SYNC*.TXT'
$results = @()

try {
	Set-OpenTSSetting SyncBug PrintCRC $printFrame
	foreach ($s in $sessions) {
		$name = $s.name
		$recording = Join-Path $manifestDir $s.recording
		$golden = Join-Path $manifestDir "$name.sync.txt"
		$output = Join-Path $OutputDirectory "$name.sync.txt"
		$result = [ordered]@{ Session = $name; Status = ''; Detail = ''; Seconds = 0 }

		if (-not (Test-Path $recording)) {
			$result.Status = 'MISSING'; $result.Detail = "recording not found: $recording"
			$results += [pscustomobject] $result; continue
		}
		if (-not $Capture -and -not (Test-Path $golden)) {
			$result.Status = 'MISSING'; $result.Detail = "golden dump not found: $golden (run with -Capture)"
			$results += [pscustomobject] $result; continue
		}

		Copy-Item $recording $recordPath -Force
		Remove-Item $syncGlob -ErrorAction SilentlyContinue
		$started = Get-Date
		$process = Start-Process -FilePath $exe -ArgumentList '-WIN', '-XY' -WorkingDirectory $runDir -WindowStyle Minimized -PassThru
		if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
			Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
			$result.Status = 'TIMEOUT'; $result.Detail = "no exit within $TimeoutSeconds s"
			$results += [pscustomobject] $result; continue
		}
		$result.Seconds = [int] ((Get-Date) - $started).TotalSeconds

		$syncPath = Get-ChildItem $syncGlob -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1 -ExpandProperty FullName
		if (-not $syncPath) {
			$result.Status = 'NODUMP'; $result.Detail = "exit code $($process.ExitCode) and no SYNC*.TXT; check Run/Debug and Run/Exceptions"
			$results += [pscustomobject] $result; continue
		}
		Copy-Item $syncPath $output -Force

		if ($Capture) {
			Copy-Item $syncPath $golden -Force
			$result.Status = 'CAPTURED'; $result.Detail = $golden
			$results += [pscustomobject] $result; continue
		}

		$expected = Get-ComparableLines $golden
		$actual = Get-ComparableLines $output
		$limit = [Math]::Min($expected.Count, $actual.Count)
		$firstDiff = -1
		for ($i = 0; $i -lt $limit; $i++) { if ($expected[$i] -ne $actual[$i]) { $firstDiff = $i; break } }
		if ($firstDiff -lt 0 -and $expected.Count -ne $actual.Count) { $firstDiff = $limit }

		if ($firstDiff -lt 0) {
			$result.Status = 'MATCH'
		} else {
			$exp = if ($firstDiff -lt $expected.Count) { $expected[$firstDiff] } else { '<end>' }
			$act = if ($firstDiff -lt $actual.Count) { $actual[$firstDiff] } else { '<end>' }
			$detail = "first difference at comparable line $($firstDiff + 1): expected '$exp' got '$act'"
			$crcSlots = @()
			for ($i = 0; $i -lt $limit; $i++) {
				if ($expected[$i] -ne $actual[$i] -and $expected[$i] -match '^CRC\[(\d+)\]=') { $crcSlots += [int] $Matches[1] }
			}
			if ($crcSlots.Count -gt 0) {
				$frames = $crcSlots | ForEach-Object { Get-FrameForCrcSlot $_ $printFrame } | Sort-Object
				$detail += "; earliest divergent CRC frame $($frames[0]) of $printFrame"
			}
			$result.Status = 'DIFF'; $result.Detail = $detail
		}
		$results += [pscustomobject] $result
	}
} finally {
	if ($null -ne $previousPrintCrc) { Set-OpenTSSetting SyncBug PrintCRC $previousPrintCrc } else { Set-OpenTSSetting SyncBug PrintCRC 2147483647 }
}

$report = Join-Path $OutputDirectory 'report.txt'
$lines = @("Vanilla baseline replay $(Get-Date -Format s)", "Executable: $exe", "Manifest: $Manifest", "PrintCRC frame: $printFrame", '')
$lines += $results | ForEach-Object { '{0,-20} {1,-9} {2,4}s  {3}' -f $_.Session, $_.Status, $_.Seconds, $_.Detail }
$lines | Set-Content $report
$lines | Write-Output

$failed = @($results | Where-Object { $_.Status -notin @('MATCH', 'CAPTURED') })
if ($failed.Count -gt 0) { exit 1 }
exit 0
