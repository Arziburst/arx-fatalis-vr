<#
.SYNOPSIS
	Quit SteamVR the way its own "Exit SteamVR" does, so the headset powers down. The owner's switch.

.DESCRIPTION
	Sends `vrmonitor://quit` to the running SteamVR status window (vrmonitor.exe) - the same quit request as the
	menu item - and waits until vrmonitor, vrserver, vrcompositor, vrdashboard and vrservice have exited by
	themselves (default 60 s). Nothing is killed: stopping the processes from outside left the Valve Index on
	(#171). When SteamVR does not exit in time the script lists what still runs and fails; exit it from its own
	menu then. Base stations and controllers follow SteamVR's own power settings.

	This turns the owner's headset off: workers never run it (D-014). `test-pr.ps1 -StopSteamVR` runs it after the
	game has exited.

.PARAMETER TimeoutSec
	How long to wait for SteamVR to exit.

.PARAMETER SettleSec
	Extra seconds after the exit, for the headset to power down.

.EXAMPLE
	.\scripts\dev\stop-steamvr.ps1
#>
[CmdletBinding()]
param(
	[int]$TimeoutSec = 60,
	[int]$SettleSec = 3
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8
. (Join-Path $PSScriptRoot '..\launch-lib.ps1')

Stop-SteamVr -TimeoutSec $TimeoutSec -SettleSec $SettleSec
