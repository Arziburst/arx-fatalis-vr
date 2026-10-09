# Shared by smoke-test.ps1, game-cmd.ps1 and game-status.ps1. Dot-source it:
#   . (Join-Path $PSScriptRoot 'test-lib.ps1')
#
# Where the game's user files live, where the test channel lives (docs/vr/TESTING.md), and how to
# talk to a running game through it: status.json (read), command.txt (write), ack.txt (read).
# Paths are per working copy (build-paths.ps1), so parallel workers never share them.

. (Join-Path $PSScriptRoot '..\build-paths.ps1')
. (Join-Path $PSScriptRoot '..\launch-lib.ps1')

# The working copy these scripts belong to (the repository root above scripts\dev)
$ArxSourceDir = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

# The user directory a test run writes to: this working copy's isolated one (arx.log, cfg.ini,
# saves, crashes), or with -Shared the player's %USERPROFILE%\Saved Games\Arx Libertatis
function Get-ArxUserDir([switch]$Shared) {
	if ($Shared) { Get-SharedUserDir } else { Get-DefaultUserDir $ArxSourceDir }
}

function Get-ArxLogPath([string]$UserDir = (Get-ArxUserDir)) { Join-Path $UserDir 'arx.log' }

function Get-ArxCrashDir([string]$UserDir = (Get-ArxUserDir)) { Join-Path $UserDir 'crashes' }

# The test channel directory the game is started with (--test-dir): $env:ARXVR_TEST_DIR if set,
# otherwise %LOCALAPPDATA%\ArxFatalisVR\test\<folder>-<hash> of this working copy
function Get-ArxTestDir { Get-DefaultTestDir $ArxSourceDir }

# A simulated headset script (--vr-sim, #112): a path, or a name in scripts\dev\vrsim (".vrsim" optional)
function Resolve-ArxSimScript([string]$Name) {
	$candidates = @($Name, (Join-Path $ArxSourceDir "scripts\dev\vrsim\$Name"), (Join-Path $ArxSourceDir "scripts\dev\vrsim\$Name.vrsim"))
	foreach ($candidate in $candidates) {
		if (Test-Path -LiteralPath $candidate -PathType Leaf) { return (Resolve-Path -LiteralPath $candidate).Path }
	}
	$known = @(Get-ChildItem (Join-Path $ArxSourceDir 'scripts\dev\vrsim') -Filter '*.vrsim' -ErrorAction SilentlyContinue | ForEach-Object BaseName)
	throw "no simulator script '$Name' (a path, or one of: $($known -join ', '))"
}

# status.json as an object, or $null while it does not exist or is being replaced right now
function Read-ArxStatus([string]$Dir = (Get-ArxTestDir)) {
	$file = Join-Path $Dir 'status.json'
	for ($i = 0; $i -lt 5; $i++) {
		try {
			if (-not (Test-Path $file)) { return $null }
			$text = [IO.File]::ReadAllText($file)
			if ($text) { return ($text | ConvertFrom-Json) }
		} catch {
			Start-Sleep -Milliseconds 50
		}
	}
	return $null
}

# The marker smoke-test.ps1 writes into the test channel folder right after it started the game (#258): until
# it exists the smoke test may still be waiting for the game lock (D-014, up to -LockTimeoutSec), which is not
# the game's start-up. A check that drives the game from outside starts its own timeouts only after the marker.
function Get-ArxLaunchMarker([string]$Dir = (Get-ArxTestDir)) { Join-Path $Dir 'launched.txt' }

# Delete the marker of an earlier run; a check does it before it starts the smoke test job
function Clear-ArxLaunchMarker([string]$Dir = (Get-ArxTestDir)) {
	Remove-Item (Get-ArxLaunchMarker $Dir) -Force -ErrorAction SilentlyContinue
}

# Wait until the game was started (the marker exists): $true. Returns $false when the job ended first - the
# smoke test was skipped (lock busy, exit 2) or failed before the launch; the caller then goes on to the job's
# verdict instead of waiting for a game that never starts. No timeout of its own: the lock wait is bounded by
# smoke-test.ps1 (-LockTimeoutSec), and $Job leaving the Running state is the end of it.
function Wait-ArxGameLaunched($Job, [string]$Dir = (Get-ArxTestDir), [int]$PollMs = 200) {
	$marker = Get-ArxLaunchMarker $Dir
	while ($true) {
		if (Test-Path -LiteralPath $marker) { return $true }
		if ($Job.State -ne 'Running') { return (Test-Path -LiteralPath $marker) }
		Start-Sleep -Milliseconds $PollMs
	}
}

# Poll status.json until $Until (a script block that gets the status) returns true.
# Returns the status; throws after $TimeoutSec seconds.
function Wait-ArxStatus(
	[scriptblock]$Until,
	[int]$TimeoutSec = 60,
	[string]$Dir = (Get-ArxTestDir),
	[string]$What = 'condition'
) {
	$deadline = (Get-Date).AddSeconds($TimeoutSec)
	$s = $null
	do {
		$s = Read-ArxStatus $Dir
		if ($s -and (& $Until $s)) { return $s }
		Start-Sleep -Milliseconds 200
	} while ((Get-Date) -lt $deadline)
	$last = if ($s) { $s | ConvertTo-Json -Compress -Depth 4 } else { 'none' }
	throw "timed out after $TimeoutSec s waiting for $What (last status: $last)"
}

# Write one batch of commands (one per line) and wait until the game acknowledged it
# (cmdSeq in status.json advanced). Returns the ack line: seq=N frame=F errors=E last=... error=...
# The file is written next to its final name and renamed, so the game never reads a half-written batch.
function Send-ArxCommand(
	[string[]]$Lines,
	[string]$Dir = (Get-ArxTestDir),
	[int]$TimeoutSec = 10
) {
	# The game writes ack.txt first and status.json later, so status.json can still show the batch before the
	# previous one: a baseline taken from it alone lets that late update (and the previous ack) pass for the
	# answer to this batch (#250). The baseline is the newest seq of both files; the answer is the ack file itself.
	$before = Read-ArxStatus $Dir
	$seq = if ($before) { [int]$before.cmdSeq } else { 0 }
	$seq = [Math]::Max($seq, (Read-ArxAckSeq $Dir))
	$file = Join-Path $Dir 'command.txt'
	$tmp = Join-Path $Dir 'command.txt.tmp'
	$text = ($Lines -join "`n") + "`n"
	$written = $false
	for ($i = 0; $i -lt 30 -and -not $written; $i++) {
		try {
			[IO.File]::WriteAllText($tmp, $text, [Text.UTF8Encoding]::new($false))
			Move-Item -LiteralPath $tmp -Destination $file -Force
			$written = $true
		} catch {
			Start-Sleep -Milliseconds 100
		}
	}
	if (-not $written) { throw "could not write $file" }
	$deadline = (Get-Date).AddSeconds($TimeoutSec)
	do {
		$ack = Read-ArxAck $Dir
		if ($ack -and [int]$ack.Seq -gt $seq) { return $ack.Text }
		Start-Sleep -Milliseconds 50
	} while ((Get-Date) -lt $deadline)
	$last = if ($before) { $before | ConvertTo-Json -Compress -Depth 4 } else { 'none' }
	throw "timed out after $TimeoutSec s waiting for the game to acknowledge '$($Lines -join '; ')' (last ack seq $seq; status before: $last)"
}

# ack.txt as { Seq; Text }, or $null when it is missing, half-written or unreadable.
function Read-ArxAck([string]$Dir) {
	try {
		$text = (Get-Content (Join-Path $Dir 'ack.txt') -Raw -ErrorAction Stop).Trim()
		if ($text -match '^seq=(\d+) ') { return [pscustomobject]@{ Seq = [int]$Matches[1]; Text = $text } }
	} catch { }
	$null
}

function Read-ArxAckSeq([string]$Dir) {
	$ack = Read-ArxAck $Dir
	if ($ack) { $ack.Seq } else { 0 }
}
