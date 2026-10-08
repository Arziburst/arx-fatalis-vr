# Shared by smoke-test.ps1, game-cmd.ps1 and game-status.ps1. Dot-source it:
#   . (Join-Path $PSScriptRoot 'test-lib.ps1')
#
# Where the game's user files live, where the test channel lives (docs/vr/TESTING.md), and how to
# talk to a running game through it: status.json (read), command.txt (write), ack.txt (read).

. (Join-Path $PSScriptRoot '..\build-paths.ps1')

# %USERPROFILE%\Saved Games\Arx Libertatis - arx.log, cfg.ini, saves, crash reports
function Get-ArxUserDir {
	Join-Path ([Environment]::GetFolderPath('UserProfile')) 'Saved Games\Arx Libertatis'
}

function Get-ArxLogPath { Join-Path (Get-ArxUserDir) 'arx.log' }

function Get-ArxCrashDir { Join-Path (Get-ArxUserDir) 'crashes' }

# The test channel directory the game is started with (--test-dir): $env:ARXVR_TEST_DIR if set,
# otherwise %LOCALAPPDATA%\ArxFatalisVR\test
function Get-ArxTestDir {
	if ($env:ARXVR_TEST_DIR) { return $env:ARXVR_TEST_DIR }
	Join-Path $env:LOCALAPPDATA 'ArxFatalisVR\test'
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
	$before = Read-ArxStatus $Dir
	$seq = if ($before) { [int]$before.cmdSeq } else { 0 }
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
	$null = Wait-ArxStatus { param($s) [int]$s.cmdSeq -gt $seq } -TimeoutSec $TimeoutSec -Dir $Dir `
		-What "the game to acknowledge '$($Lines -join '; ')'"
	(Get-Content (Join-Path $Dir 'ack.txt') -Raw -ErrorAction SilentlyContinue).Trim()
}
