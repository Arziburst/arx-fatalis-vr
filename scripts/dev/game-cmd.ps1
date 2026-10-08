<#
.SYNOPSIS
	Send commands to a running game through the test channel and print the acknowledgement.

.DESCRIPTION
	The game must have been started with --test-dir <dir> (smoke-test.ps1 does that; for a manual
	run: .\scripts\run-windows.ps1 -- --test-dir "$env:LOCALAPPDATA\ArxFatalisVR\test" --loadlevel 10).
	All lines go into one batch (one command.txt); the script waits until the game acknowledged it
	and prints the ack line: seq=N frame=F errors=E last=<last command> error=<message or ->.
	Exit code 1 if the last command of the batch was refused. Vocabulary: docs/vr/TESTING.md.

.PARAMETER Dir
	The test channel directory. Default: $env:ARXVR_TEST_DIR or %LOCALAPPDATA%\ArxFatalisVR\test.

.EXAMPLE
	.\scripts\dev\game-cmd.ps1 status
.EXAMPLE
	.\scripts\dev\game-cmd.ps1 'shot before' 'player.setplayerpos 0 0 0' 'shot after'
.EXAMPLE
	.\scripts\dev\game-cmd.ps1 quit
#>
[CmdletBinding(PositionalBinding = $false)]
param(
	[string]$Dir,
	[int]$TimeoutSec = 10,
	[Parameter(ValueFromRemainingArguments)]
	[string[]]$Lines
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8
. (Join-Path $PSScriptRoot 'test-lib.ps1')

if (-not $Dir) { $Dir = Get-ArxTestDir }
if (-not $Lines) { throw 'usage: game-cmd.ps1 "<command>" ["<command>" ...]' }
if (-not (Read-ArxStatus $Dir)) { throw "no status.json in $Dir - is the game running with --test-dir `"$Dir`"?" }

$ack = Send-ArxCommand -Lines $Lines -Dir $Dir -TimeoutSec $TimeoutSec
$ack
$status = Read-ArxStatus $Dir
if ($status.lastCmdError) {
	Write-Host "refused: $($status.lastCmdError)" -ForegroundColor Red
	exit 1
}
