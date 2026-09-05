#Requires -Version 7.0
<#
.SYNOPSIS
Developer harness for launching and driving a local OpenTS build.

.DESCRIPTION
Launches a built engine from Run/, finds the debug log the run writes, waits for
log lines, captures the game window to PNG, and sends keyboard and mouse input in
the game's own logical pixel coordinates. Everything here is a local convenience
for play testing and requires a populated Run/ tree; nothing in the repository
build or test suite depends on it.
#>

Set-StrictMode -Version Latest

$script:RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$script:RunDir = Join-Path $script:RepoRoot 'Run'

Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms

Add-Type -Namespace OpenTSDev -Name Native -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
[DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hWnd, out RECT rect);
[DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr hWnd, ref POINT point);
[DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
[DllImport("user32.dll")] public static extern bool IsIconic(IntPtr hWnd);
[DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int cmd);
[DllImport("user32.dll")] public static extern short VkKeyScan(char ch);
[DllImport("user32.dll")] public static extern uint MapVirtualKey(uint code, uint mapType);
[DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr context);
[DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
[DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdc, uint flags);
[DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);
[DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);
[DllImport("user32.dll")] public static extern IntPtr GetParent(IntPtr hWnd);
[DllImport("user32.dll")] public static extern int GetDlgCtrlID(IntPtr hWnd);
[DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
[DllImport("user32.dll", CharSet = CharSet.Auto)] public static extern int GetWindowText(IntPtr hWnd, System.Text.StringBuilder text, int max);
[DllImport("user32.dll", CharSet = CharSet.Auto)] public static extern int GetClassName(IntPtr hWnd, System.Text.StringBuilder text, int max);
public delegate bool EnumProc(IntPtr hWnd, IntPtr lParam);
[DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr parent, EnumProc proc, IntPtr lParam);
[DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc proc, IntPtr lParam);
[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint pid);
public static IntPtr FindGameWindow(uint pid, string className) {
	IntPtr found = IntPtr.Zero;
	EnumWindows((h, l) => {
		uint owner; GetWindowThreadProcessId(h, out owner);
		if (owner != pid) return true;
		var name = new System.Text.StringBuilder(256); GetClassName(h, name, 256);
		if (name.ToString() == className) { found = h; return false; }
		return true;
	}, IntPtr.Zero);
	return found;
}
public static System.Collections.Generic.List<IntPtr> Children(IntPtr parent) {
	var list = new System.Collections.Generic.List<IntPtr>();
	EnumChildWindows(parent, (h, l) => { list.Add(h); return true; }, IntPtr.Zero);
	return list;
}

[StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
[StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }

public const uint WM_KEYDOWN = 0x0100, WM_KEYUP = 0x0101, WM_MOUSEMOVE = 0x0200;
public const uint WM_LBUTTONDOWN = 0x0201, WM_LBUTTONUP = 0x0202, WM_RBUTTONDOWN = 0x0204, WM_RBUTTONUP = 0x0205;
public const uint PW_RENDERFULLCONTENT = 0x0002;
'@

# Window geometry, cursor placement, and screen capture must all use physical
# pixels, which only holds when this process is per-monitor DPI aware.
[OpenTSDev.Native]::SetProcessDpiAwarenessContext([IntPtr] -4) | Out-Null

function Get-OpenTSRunDir {
	<# .SYNOPSIS Returns the Run/ directory the harness launches from. #>
	return $script:RunDir
}

function Get-OpenTSExecutable {
	<#
	.SYNOPSIS
	Returns the path of the built executable for a configuration.
	#>
	param([ValidateSet('Debug', 'Release')][string] $Configuration = 'Release')
	$name = if ($Configuration -eq 'Debug') { 'GameD.exe' } else { 'Game.exe' }
	return Join-Path $script:RunDir $name
}

function Get-OpenTSSetting {
	<#
	.SYNOPSIS
	Reads one value from Run/SUN.INI.
	#>
	param([Parameter(Mandatory)][string] $Section, [Parameter(Mandatory)][string] $Key, $Default = $null)
	$ini = Join-Path $script:RunDir 'SUN.INI'
	if (-not (Test-Path $ini)) { return $Default }
	$inSection = $false
	foreach ($line in Get-Content $ini) {
		$t = $line.Trim()
		if ($t -match '^\[(.+)\]$') { $inSection = ($Matches[1] -eq $Section); continue }
		if ($inSection -and $t -match '^([^=;]+?)\s*=\s*(.*)$' -and $Matches[1].Trim() -eq $Key) { return $Matches[2].Trim() }
	}
	return $Default
}

function Set-OpenTSSetting {
	<#
	.SYNOPSIS
	Writes one value into Run/SUN.INI, adding the section or key when missing.
	#>
	param([Parameter(Mandatory)][string] $Section, [Parameter(Mandatory)][string] $Key, [Parameter(Mandatory)][string] $Value)
	$ini = Join-Path $script:RunDir 'SUN.INI'
	$lines = [System.Collections.Generic.List[string]]::new()
	if (Test-Path $ini) { $lines.AddRange([string[]] @(Get-Content $ini)) }
	$sectionStart = -1; $sectionEnd = $lines.Count
	for ($i = 0; $i -lt $lines.Count; $i++) {
		$t = $lines[$i].Trim()
		if ($t -match '^\[(.+)\]$') {
			if ($sectionStart -ge 0) { $sectionEnd = $i; break }
			if ($Matches[1] -eq $Section) { $sectionStart = $i }
		}
	}
	if ($sectionStart -lt 0) {
		if ($lines.Count -gt 0 -and $lines[$lines.Count - 1].Trim() -ne '') { $lines.Add('') }
		$lines.Add("[$Section]"); $lines.Add("$Key=$Value")
	} else {
		$done = $false
		for ($i = $sectionStart + 1; $i -lt $sectionEnd; $i++) {
			if ($lines[$i] -match '^([^=;]+?)\s*=' -and $Matches[1].Trim() -eq $Key) { $lines[$i] = "$Key=$Value"; $done = $true; break }
		}
		if (-not $done) {
			$insertAt = $sectionEnd
			while ($insertAt -gt $sectionStart + 1 -and $lines[$insertAt - 1].Trim() -eq '') { $insertAt-- }
			$lines.Insert($insertAt, "$Key=$Value")
		}
	}
	Set-Content -Path $ini -Value $lines -Encoding ascii
}

function Start-OpenTS {
	<#
	.SYNOPSIS
	Launches the engine from Run/ and returns a session object.

	.DESCRIPTION
	The session object carries the process, the configuration, and the debug log the
	run created. Pass it to the other harness functions. The window is opened in
	windowed mode by default so it can be captured and driven.

	.PARAMETER Arguments
	Extra command line arguments after the defaults. Use -NoDefaultArguments to
	suppress the default "-WIN" switch.
	#>
	[CmdletBinding()]
	param(
		[ValidateSet('Debug', 'Release')][string] $Configuration = 'Release',
		[string[]] $Arguments = @(),
		[switch] $NoDefaultArguments,
		[switch] $Console,
		[int] $LogTimeoutSeconds = 30
	)
	$exe = Get-OpenTSExecutable $Configuration
	if (-not (Test-Path $exe)) { throw "No $Configuration executable at $exe. Build it first." }
	$args = @()
	if (-not $NoDefaultArguments) { $args += '-WIN' }
	if ($Console -and $Configuration -eq 'Release') { $args += '-XC' }
	$args += $Arguments

	$logDir = Join-Path $script:RunDir 'Debug'
	$before = @{}
	if (Test-Path $logDir) { Get-ChildItem $logDir -Filter 'DEBUG_*.LOG' | ForEach-Object { $before[$_.FullName] = $true } }

	$started = Get-Date
	$process = Start-Process -FilePath $exe -ArgumentList $args -WorkingDirectory $script:RunDir -PassThru

	$log = $null
	$deadline = $started.AddSeconds($LogTimeoutSeconds)
	while ((Get-Date) -lt $deadline) {
		if (Test-Path $logDir) {
			$new = Get-ChildItem $logDir -Filter 'DEBUG_*.LOG' | Where-Object { -not $before.ContainsKey($_.FullName) } | Sort-Object LastWriteTime -Descending | Select-Object -First 1
			if ($new) { $log = $new.FullName; break }
		}
		if ($process.HasExited) { break }
		Start-Sleep -Milliseconds 200
	}

	return [pscustomobject]@{
		PSTypeName    = 'OpenTS.Session'
		Process       = $process
		Configuration = $Configuration
		Executable    = $exe
		Arguments     = $args
		Started       = $started
		Log           = $log
	}
}

function Get-OpenTSSession {
	<#
	.SYNOPSIS
	Reattaches to an already running engine process by id, or finds the newest one.
	#>
	param([int] $ProcessId = 0)
	$process = if ($ProcessId -gt 0) { Get-Process -Id $ProcessId -ErrorAction Stop }
	else { Get-Process -Name 'Game', 'GameD' -ErrorAction SilentlyContinue | Sort-Object StartTime -Descending | Select-Object -First 1 }
	if (-not $process) { return $null }
	$configuration = if ($process.Name -eq 'GameD') { 'Debug' } else { 'Release' }
	$logDir = Join-Path $script:RunDir 'Debug'
	$log = $null
	if (Test-Path $logDir) {
		$log = Get-ChildItem $logDir -Filter 'DEBUG_*.LOG' | Where-Object { $_.CreationTime -ge $process.StartTime.AddSeconds(-2) } |
			Sort-Object CreationTime | Select-Object -First 1 | ForEach-Object FullName
	}
	return [pscustomobject]@{
		PSTypeName    = 'OpenTS.Session'
		Process       = $process
		Configuration = $configuration
		Executable    = $process.Path
		Arguments     = @()
		Started       = $process.StartTime
		Log           = $log
	}
}

function Stop-OpenTS {
	<#
	.SYNOPSIS
	Terminates a session's process if it is still running.
	#>
	param([Parameter(Mandatory, ValueFromPipeline)] $Session)
	process {
		if (-not $Session.Process.HasExited) {
			Stop-Process -Id $Session.Process.Id -Force -ErrorAction SilentlyContinue
			$Session.Process.WaitForExit(5000) | Out-Null
		}
	}
}

function Get-OpenTSLog {
	<#
	.SYNOPSIS
	Returns the session's debug log lines, optionally only the tail.
	#>
	param([Parameter(Mandatory)] $Session, [int] $Tail = 0)
	if (-not $Session.Log -or -not (Test-Path $Session.Log)) { return @() }
	# The engine holds the log open for writing, so read it with sharing allowed.
	$stream = [System.IO.File]::Open($Session.Log, 'Open', 'Read', 'ReadWrite')
	try {
		$reader = [System.IO.StreamReader]::new($stream)
		$text = $reader.ReadToEnd()
	} finally { $stream.Dispose() }
	$lines = $text -split "`r?`n"
	if ($Tail -gt 0 -and $lines.Count -gt $Tail) { return $lines[($lines.Count - $Tail)..($lines.Count - 1)] }
	return $lines
}

function Wait-OpenTSLog {
	<#
	.SYNOPSIS
	Waits until the debug log contains a line matching a pattern, or the process exits.

	.OUTPUTS
	The first matching line, or $null on timeout or exit.
	#>
	param([Parameter(Mandatory)] $Session, [Parameter(Mandatory)][string] $Pattern, [int] $TimeoutSeconds = 60)
	$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
	while ((Get-Date) -lt $deadline) {
		$hit = Get-OpenTSLog $Session | Where-Object { $_ -match $Pattern } | Select-Object -First 1
		if ($hit) { return $hit }
		if ($Session.Process.HasExited) { return $null }
		Start-Sleep -Milliseconds 250
	}
	return $null
}

function Wait-OpenTSExit {
	<#
	.SYNOPSIS
	Waits for the process to exit and returns its exit code, or $null on timeout.
	#>
	param([Parameter(Mandatory)] $Session, [int] $TimeoutSeconds = 300)
	if ($Session.Process.WaitForExit($TimeoutSeconds * 1000)) { return $Session.Process.ExitCode }
	return $null
}

function Get-OpenTSWindow {
	<#
	.SYNOPSIS
	Returns the main window handle and its client geometry, plus the logical game
	viewport inside it as the engine's own aspect-fit computes it.
	#>
	param([Parameter(Mandatory)] $Session)
	# Debug builds also own a console window, so the game window is found by class.
	$hwnd = [OpenTSDev.Native]::FindGameWindow([uint32] $Session.Process.Id, 'Tiberian Sun')
	if ($hwnd -eq [IntPtr]::Zero) { return $null }
	$rect = New-Object OpenTSDev.Native+RECT
	[OpenTSDev.Native]::GetClientRect($hwnd, [ref] $rect) | Out-Null
	$origin = New-Object OpenTSDev.Native+POINT
	[OpenTSDev.Native]::ClientToScreen($hwnd, [ref] $origin) | Out-Null
	$clientW = $rect.Right - $rect.Left; $clientH = $rect.Bottom - $rect.Top

	$gameW = [int](Get-OpenTSSetting Video ScreenWidth 640)
	$gameH = [int](Get-OpenTSSetting Video ScreenHeight 400)
	$integer = (Get-OpenTSSetting Video IntegerScaling 'no') -match '^(yes|true|1)$'
	$scale = [Math]::Min($clientW / $gameW, $clientH / $gameH)
	if ($integer -and $scale -ge 1.0) { $scale = [Math]::Floor($scale) }
	$destW = [int]($gameW * $scale); $destH = [int]($gameH * $scale)
	$destX = [int](($clientW - $destW) / 2); $destY = [int](($clientH - $destH) / 2)

	return [pscustomobject]@{
		Handle       = $hwnd
		ClientOrigin = $origin
		ClientWidth  = $clientW
		ClientHeight = $clientH
		GameWidth    = $gameW
		GameHeight   = $gameH
		DestX        = $destX
		DestY        = $destY
		DestWidth    = $destW
		DestHeight   = $destH
		Scale        = $scale
	}
}

function ConvertTo-OpenTSScreenPoint {
	<#
	.SYNOPSIS
	Maps a logical game pixel to a screen pixel through the window's viewport.
	#>
	param([Parameter(Mandatory)] $Session, [Parameter(Mandatory)][int] $X, [Parameter(Mandatory)][int] $Y)
	$w = Get-OpenTSWindow $Session
	if (-not $w) { throw 'The game window is not available.' }
	return [pscustomobject]@{
		X = $w.ClientOrigin.X + $w.DestX + [int]($X * $w.Scale)
		Y = $w.ClientOrigin.Y + $w.DestY + [int]($Y * $w.Scale)
	}
}

function Wait-OpenTSWindow {
	<#
	.SYNOPSIS
	Waits for the game window to exist and returns its geometry, or $null on timeout.
	#>
	param([Parameter(Mandatory)] $Session, [int] $TimeoutSeconds = 30)
	$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
	while ((Get-Date) -lt $deadline) {
		$w = Get-OpenTSWindow $Session
		if ($w) { return $w }
		if ($Session.Process.HasExited) { return $null }
		Start-Sleep -Milliseconds 250
	}
	return $null
}

function Show-OpenTSWindow {
	<#
	.SYNOPSIS
	Restores and foregrounds the game window so input and capture reach it.
	Movies pause while the window lacks focus, so a playback run wants this once.
	#>
	param([Parameter(Mandatory)] $Session)
	$w = Wait-OpenTSWindow $Session
	if (-not $w) { throw 'The game window is not available.' }
	if ([OpenTSDev.Native]::IsIconic($w.Handle)) { [OpenTSDev.Native]::ShowWindow($w.Handle, 9) | Out-Null }
	[OpenTSDev.Native]::SetForegroundWindow($w.Handle) | Out-Null
	Start-Sleep -Milliseconds 150
}

function Save-OpenTSScreenshot {
	<#
	.SYNOPSIS
	Captures the game viewport into a PNG and returns its path.

	.DESCRIPTION
	The window is rendered through PrintWindow, so it need not be in front or even
	visible on screen and no focus changes. By default only the logical viewport is
	kept, scaled back to the logical game size; use -Raw to keep the whole client
	area at native size.
	#>
	param([Parameter(Mandatory)] $Session, [Parameter(Mandatory)][string] $Path, [switch] $Raw)
	$w = Get-OpenTSWindow $Session
	if (-not $w) { throw 'The game window is not available.' }
	$rect = New-Object OpenTSDev.Native+RECT
	[OpenTSDev.Native]::GetWindowRect($w.Handle, [ref] $rect) | Out-Null
	$winW = $rect.Right - $rect.Left; $winH = $rect.Bottom - $rect.Top
	$whole = [System.Drawing.Bitmap]::new($winW, $winH)
	$g = [System.Drawing.Graphics]::FromImage($whole)
	try {
		$hdc = $g.GetHdc()
		try { [OpenTSDev.Native]::PrintWindow($w.Handle, $hdc, [OpenTSDev.Native]::PW_RENDERFULLCONTENT) | Out-Null }
		finally { $g.ReleaseHdc($hdc) }
	} finally { $g.Dispose() }

	# Crop the client area out of the frame, then the viewport out of the client area.
	$cropX = $w.ClientOrigin.X - $rect.Left; $cropY = $w.ClientOrigin.Y - $rect.Top
	$cropW = $w.ClientWidth; $cropH = $w.ClientHeight
	if (-not $Raw) { $cropX += $w.DestX; $cropY += $w.DestY; $cropW = $w.DestWidth; $cropH = $w.DestHeight }
	$outW = if ($Raw) { $cropW } else { $w.GameWidth }
	$outH = if ($Raw) { $cropH } else { $w.GameHeight }
	$out = [System.Drawing.Bitmap]::new($outW, $outH)
	$g2 = [System.Drawing.Graphics]::FromImage($out)
	try {
		$g2.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
		$g2.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
		$src = [System.Drawing.Rectangle]::new($cropX, $cropY, $cropW, $cropH)
		$dst = [System.Drawing.Rectangle]::new(0, 0, $outW, $outH)
		$g2.DrawImage($whole, $dst, $src, [System.Drawing.GraphicsUnit]::Pixel)
	} finally { $g2.Dispose(); $whole.Dispose() }
	$full = [System.IO.Path]::GetFullPath($Path)
	New-Item -ItemType Directory -Force -Path (Split-Path $full) | Out-Null
	$out.Save($full, [System.Drawing.Imaging.ImageFormat]::Png)
	$out.Dispose()
	return $full
}

function ConvertTo-OpenTSClientPoint {
	<#
	.SYNOPSIS
	Maps a logical game pixel to a client-area pixel through the window's viewport.
	#>
	param([Parameter(Mandatory)] $Session, [Parameter(Mandatory)][int] $X, [Parameter(Mandatory)][int] $Y)
	$w = Get-OpenTSWindow $Session
	if (-not $w) { throw 'The game window is not available.' }
	return [pscustomobject]@{ X = $w.DestX + [int]($X * $w.Scale); Y = $w.DestY + [int]($Y * $w.Scale) }
}

function Send-OpenTSMouse {
	<#
	.SYNOPSIS
	Delivers a mouse move and optional click at a logical game pixel.

	.DESCRIPTION
	The real cursor is moved to the point, because the engine hit-tests menus and
	the tactical view against the system cursor position rather than the message,
	and the button events are then posted to the game window as messages. Focus is
	left alone; windowed mode processes posted input without it.
	#>
	param(
		[Parameter(Mandatory)] $Session,
		[Parameter(Mandatory)][int] $X,
		[Parameter(Mandatory)][int] $Y,
		[ValidateSet('None', 'Left', 'Right')][string] $Button = 'Left',
		[int] $HoldMilliseconds = 60
	)
	$w = Get-OpenTSWindow $Session
	if (-not $w) { throw 'The game window is not available.' }
	$p = ConvertTo-OpenTSClientPoint $Session $X $Y
	$screen = ConvertTo-OpenTSScreenPoint $Session $X $Y
	[OpenTSDev.Native]::SetCursorPos($screen.X, $screen.Y) | Out-Null
	$l = [IntPtr] ((($p.Y -band 0xFFFF) -shl 16) -bor ($p.X -band 0xFFFF))
	[OpenTSDev.Native]::PostMessage($w.Handle, [OpenTSDev.Native]::WM_MOUSEMOVE, [IntPtr] 0, $l) | Out-Null
	Start-Sleep -Milliseconds 120
	if ($Button -eq 'None') { return }
	$down = if ($Button -eq 'Left') { [OpenTSDev.Native]::WM_LBUTTONDOWN } else { [OpenTSDev.Native]::WM_RBUTTONDOWN }
	$up = if ($Button -eq 'Left') { [OpenTSDev.Native]::WM_LBUTTONUP } else { [OpenTSDev.Native]::WM_RBUTTONUP }
	$mk = if ($Button -eq 'Left') { 1 } else { 2 }
	[OpenTSDev.Native]::PostMessage($w.Handle, $down, [IntPtr] $mk, $l) | Out-Null
	Start-Sleep -Milliseconds $HoldMilliseconds
	[OpenTSDev.Native]::PostMessage($w.Handle, $up, [IntPtr] 0, $l) | Out-Null
	Start-Sleep -Milliseconds $HoldMilliseconds
}

function Get-OpenTSControl {
	<#
	.SYNOPSIS
	Lists the visible child controls of the game window with class, text, and rectangle.
	#>
	param([Parameter(Mandatory)] $Session, [string] $Text = $null, [string] $Class = $null)
	$w = Get-OpenTSWindow $Session
	if (-not $w) { return @() }
	foreach ($h in [OpenTSDev.Native]::Children($w.Handle)) {
		if (-not [OpenTSDev.Native]::IsWindowVisible($h)) { continue }
		$t = [System.Text.StringBuilder]::new(256); [OpenTSDev.Native]::GetWindowText($h, $t, 256) | Out-Null
		$c = [System.Text.StringBuilder]::new(256); [OpenTSDev.Native]::GetClassName($h, $c, 256) | Out-Null
		if ($Text -and $t.ToString() -notlike $Text) { continue }
		if ($Class -and $c.ToString() -notlike $Class) { continue }
		$r = New-Object OpenTSDev.Native+RECT
		[OpenTSDev.Native]::GetWindowRect($h, [ref] $r) | Out-Null
		[pscustomobject]@{
			Handle = $h; Class = $c.ToString(); Text = $t.ToString(); ControlId = [OpenTSDev.Native]::GetDlgCtrlID($h)
			Parent = [OpenTSDev.Native]::GetParent($h); Left = $r.Left; Top = $r.Top; Width = $r.Right - $r.Left; Height = $r.Bottom - $r.Top
		}
	}
}

function Invoke-OpenTSButton {
	<#
	.SYNOPSIS
	Presses a dialog button by its caption, as if clicked.
	#>
	param([Parameter(Mandatory)] $Session, [Parameter(Mandatory)][string] $Text)
	$button = Get-OpenTSControl $Session -Text $Text -Class 'Button' | Select-Object -First 1
	if (-not $button) { throw "No visible button captioned '$Text'." }
	# BM_CLICK drives the button's own down/up handling and notifies its parent.
	[OpenTSDev.Native]::SendMessage($button.Handle, 0x00F5, [IntPtr] 0, [IntPtr] 0) | Out-Null
}

function Set-OpenTSTrackbar {
	<#
	.SYNOPSIS
	Sets a dialog trackbar's position by control id and notifies its dialog.
	#>
	param([Parameter(Mandatory)] $Session, [Parameter(Mandatory)][int] $ControlId, [Parameter(Mandatory)][int] $Value)
	$bar = Get-OpenTSControl $Session -Class 'msctls_trackbar32' | Where-Object ControlId -eq $ControlId | Select-Object -First 1
	if (-not $bar) { throw "No visible trackbar with control id $ControlId." }
	$min = [int][OpenTSDev.Native]::SendMessage($bar.Handle, 0x0401, [IntPtr] 0, [IntPtr] 0)
	$max = [int][OpenTSDev.Native]::SendMessage($bar.Handle, 0x0402, [IntPtr] 0, [IntPtr] 0)
	# The engine's owner-drawn sliders report no range, so only a real range is enforced.
	if ($max -gt $min -and ($Value -lt $min -or $Value -gt $max)) { throw "Value $Value is outside the trackbar range $min..$max." }
	[OpenTSDev.Native]::SendMessage($bar.Handle, 0x0405, [IntPtr] 1, [IntPtr] $Value) | Out-Null
	# WM_HSCROLL with TB_ENDTRACK is what the dialog handles to read the new position.
	[OpenTSDev.Native]::SendMessage($bar.Parent, 0x0114, [IntPtr] 8, $bar.Handle) | Out-Null
	return [int][OpenTSDev.Native]::SendMessage($bar.Handle, 0x0400, [IntPtr] 0, [IntPtr] 0)
}

function Send-OpenTSKey {
	<#
	.SYNOPSIS
	Presses and releases a key by posting key messages to the game window.

	.PARAMETER Key
	A System.Windows.Forms.Keys name such as 'Escape', 'Return', 'F8', or a single
	character such as 'q'.
	#>
	param([Parameter(Mandatory)] $Session, [Parameter(Mandatory)][string] $Key, [int] $HoldMilliseconds = 60)
	$w = Get-OpenTSWindow $Session
	if (-not $w) { throw 'The game window is not available.' }
	$vk = 0
	if ($Key.Length -eq 1) { $vk = [OpenTSDev.Native]::VkKeyScan([char] $Key) -band 0xFF }
	else { $vk = [int][System.Windows.Forms.Keys]::Parse([System.Windows.Forms.Keys], $Key, $true) }
	$scan = [OpenTSDev.Native]::MapVirtualKey([uint32] $vk, 0)
	$downL = [IntPtr] ((($scan -band 0xFF) -shl 16) -bor 1)
	$upL = [IntPtr] ([int64] 0xC0000000 -bor (($scan -band 0xFF) -shl 16) -bor 1)
	[OpenTSDev.Native]::PostMessage($w.Handle, [OpenTSDev.Native]::WM_KEYDOWN, [IntPtr] $vk, $downL) | Out-Null
	Start-Sleep -Milliseconds $HoldMilliseconds
	[OpenTSDev.Native]::PostMessage($w.Handle, [OpenTSDev.Native]::WM_KEYUP, [IntPtr] $vk, $upL) | Out-Null
	Start-Sleep -Milliseconds $HoldMilliseconds
}

function Send-OpenTSText {
	<#
	.SYNOPSIS
	Types a string one key at a time.
	#>
	param([Parameter(Mandatory)] $Session, [Parameter(Mandatory)][string] $Text, [int] $DelayMilliseconds = 40)
	foreach ($ch in $Text.ToCharArray()) {
		Send-OpenTSKey $Session ([string] $ch) -HoldMilliseconds 30
		Start-Sleep -Milliseconds $DelayMilliseconds
	}
}

Export-ModuleMember -Function Get-OpenTSRunDir, Get-OpenTSExecutable, Get-OpenTSSetting, Set-OpenTSSetting,
	Start-OpenTS, Get-OpenTSSession, Stop-OpenTS, Get-OpenTSLog, Wait-OpenTSLog, Wait-OpenTSExit, Get-OpenTSWindow, Wait-OpenTSWindow,
	ConvertTo-OpenTSScreenPoint, ConvertTo-OpenTSClientPoint, Show-OpenTSWindow, Save-OpenTSScreenshot,
	Send-OpenTSMouse, Send-OpenTSKey, Send-OpenTSText, Get-OpenTSControl, Invoke-OpenTSButton, Set-OpenTSTrackbar
