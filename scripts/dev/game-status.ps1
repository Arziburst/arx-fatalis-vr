<#
.SYNOPSIS
	Print the running game's status.json (test channel), once or continuously.

.DESCRIPTION
	The game must have been started with --test-dir <dir>. Without -Watch the whole status is
	printed as JSON. With -Watch one line is printed whenever the state, the session state or the
	error count changes, plus the frame counter every second, until Ctrl+C. See docs/vr/TESTING.md.

.PARAMETER Dir
	The test channel directory. Default: $env:ARXVR_TEST_DIR or %LOCALAPPDATA%\ArxFatalisVR\test.

.EXAMPLE
	.\scripts\dev\game-status.ps1
.EXAMPLE
	.\scripts\dev\game-status.ps1 -Watch
#>
[CmdletBinding()]
param(
	[string]$Dir,
	[switch]$Watch
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8
. (Join-Path $PSScriptRoot 'test-lib.ps1')

if (-not $Dir) { $Dir = Get-ArxTestDir }

if (-not $Watch) {
	$status = Read-ArxStatus $Dir
	if (-not $status) { throw "no status.json in $Dir - is the game running with --test-dir `"$Dir`"?" }
	$status | ConvertTo-Json -Depth 5
	return
}

$last = ''
while ($true) {
	$status = Read-ArxStatus $Dir
	if ($status) {
		$line = "state=$($status.state) area=$($status.area) vr=$($status.vr.sessionState) errors=$($status.errors) warnings=$($status.warnings) cmdSeq=$($status.cmdSeq)"
		if ($line -ne $last) {
			$last = $line
			Write-Host ("{0:HH:mm:ss}  frame {1,7}  {2,5:0.0} fps  {3}" -f (Get-Date), $status.frame, [double]$status.fps, $line)
		}
	} else {
		Write-Host ("{0:HH:mm:ss}  no status.json in {1}" -f (Get-Date), $Dir)
	}
	Start-Sleep -Seconds 1
}
