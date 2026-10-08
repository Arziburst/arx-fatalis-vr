# The VR launch lock: one game in VR at a time on this machine (docs/vr/WORKFLOW.md, "Parallel work").
# Shared by run-windows.ps1 and scripts/dev/test-lib.ps1. Dot-source it.
#
# The lock is %LOCALAPPDATA%\ArxFatalisVR\vr.lock opened for writing without write sharing: whoever
# holds the handle holds the lock, and Windows releases the handle when the process ends, also after
# a crash or a kill, so there is no stale lock file to clean up. The holder writes who it is into
# the file (pid, since when, what), readable by everyone who finds the lock busy. Flat launches
# (--no-vr) do not take the lock.

function Get-VrLockPath { Join-Path $env:LOCALAPPDATA 'ArxFatalisVR\vr.lock' }

# Who holds the lock right now, as written into the file ("pid=... since=... what=..."), or a note.
function Get-VrLockHolder {
	$path = Get-VrLockPath
	try {
		$stream = [IO.File]::Open($path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
		try {
			$text = [IO.StreamReader]::new($stream).ReadToEnd().Trim()
			if ($text -match 'pid=(\d+)') {
				$alive = Get-Process -Id ([int]$Matches[1]) -ErrorAction SilentlyContinue
				if (-not $alive) { $text += ' (that process is gone)' }
			}
			if ($text) { $text } else { 'unknown (empty lock file)' }
		} finally { $stream.Dispose() }
	} catch {
		"unknown (cannot read $path`: $($_.Exception.Message))"
	}
}

# Take the lock, waiting up to $TimeoutSec seconds. Returns the lock object (keep it and pass it to
# Exit-VrLock) or $null when the lock stayed busy; prints the holder once while waiting.
function Enter-VrLock([string]$What, [int]$TimeoutSec = 0) {
	$path = Get-VrLockPath
	New-Item -ItemType Directory -Force (Split-Path -Parent $path) | Out-Null
	$deadline = (Get-Date).AddSeconds($TimeoutSec)
	$reported = $false
	do {
		try {
			$stream = [IO.File]::Open($path, [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::Read)
			$stream.SetLength(0)
			$writer = [IO.StreamWriter]::new($stream, [Text.UTF8Encoding]::new($false))
			$writer.WriteLine("pid=$PID since=$((Get-Date).ToString('yyyy-MM-dd HH:mm:ss')) what=$What")
			$writer.Flush()
			return [pscustomobject]@{ Path = $path; Stream = $stream; Writer = $writer }
		} catch [IO.IOException] {
			if (-not $reported) {
				Write-Host "VR lock busy: $(Get-VrLockHolder)$(if ($TimeoutSec -gt 0) { " - waiting up to $TimeoutSec s" })" -ForegroundColor Yellow
				$reported = $true
			}
			if ($TimeoutSec -gt 0) { Start-Sleep -Milliseconds 500 }
		}
	} while ((Get-Date) -lt $deadline)
	return $null
}

# Release a lock taken with Enter-VrLock (also happens by itself when the process ends).
function Exit-VrLock($Lock) {
	if (-not $Lock) { return }
	try { $Lock.Writer.Dispose() } catch { }
	try { $Lock.Stream.Dispose() } catch { }
	Remove-Item $Lock.Path -Force -ErrorAction SilentlyContinue
}
