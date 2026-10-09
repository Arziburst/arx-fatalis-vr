<#
.SYNOPSIS
	VR input module check (#74): press the VR buttons through the test channel in a flat run and give
	a PASS / FAIL verdict on what the game did - inventory, book, crouch, jump, game menu.

.DESCRIPTION
	#30's input-check.ps1 proves the control table resolves the controls into events; this one proves
	the game reacts to the events. Runs smoke-test.ps1 (flat, --loadlevel 10, isolated, every launch
	rule of D-014) with 'vr-input' commands - controls pressed as if a controller did it, no headset:
	  setplayercontrols on, cinemascope off      the level 10 start holds the player (TESTING.md)
	  tap right.b, tap right.b                   inventory open, closed       (ui.inventory = 2)
	  tap left.b, tap left.b                     book open, closed            (ui.book = 2)
	  tap right.stick_down x2                    crouch toggle on, off        (player.crouch = 2)
	  tap right.a                                a jump                       (player.jump = 1)
	  stick left 0 1                             the move vector: forward 1   (vr.input.game.moveY)
	  tap left.a, tap left.a                     the game menu, and back      (game.state MENU, GAMEPLAY)
	'mark' lines between the steps give the game frames to finish one before the next.
	The counts come from the final status.json (smoke-test -ExpectEvents), the menu round trip from
	the game.state lines after the 'mark game menu' line in arx.log, the move vector and the event
	presses from vr.input in the final status.json.

	-Unbind sends 'vr-input bind inventory none' first: the broken state on purpose, the check must FAIL.
	Exit code 0 on PASS, 1 on FAIL, 2 when the smoke test was skipped (the game lock stayed busy).
	One game launch.

.PARAMETER Unbind
	Unbind the inventory before the commands - the check must FAIL (proves it can).

.PARAMETER Rebuild
	Build this working copy first when its build is stale (passed to smoke-test.ps1).

.EXAMPLE
	.\scripts\dev\game-input-check.ps1
.EXAMPLE
	.\scripts\dev\game-input-check.ps1 -Unbind     # must FAIL: the inventory never opens
#>
[CmdletBinding()]
param(
	[switch]$Unbind,
	[switch]$Rebuild
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8
. (Join-Path $PSScriptRoot 'test-lib.ps1')

function Step([string]$command, [string]$what) {
	# The tap, then two round trips of the channel: the game sees it, then finishes what it started
	return @("vr-input $command", "mark $what", "mark $what (settled)")
}

$commands = @('setplayercontrols on', 'cinemascope off')
if ($Unbind) { $commands += 'vr-input bind inventory none' }
$commands += Step 'tap right.b' 'inventory open'
$commands += Step 'tap right.b' 'inventory closed'
$commands += Step 'tap left.b' 'book open'
$commands += Step 'tap left.b' 'book closed'
$commands += Step 'tap right.stick_down' 'crouch on'
$commands += Step 'tap right.stick_down' 'crouch off'
$commands += Step 'tap right.a' 'jump'
$commands += Step 'stick left 0 1' 'move forward'
$commands += Step 'tap left.a' 'game menu'
$commands += Step 'tap left.a' 'back in the game'

$expectEvents = @('ui.inventory=2', 'ui.book=2', 'player.crouch=2', 'player.jump=1')
# Event presses in the control table: the zeros catch a button that fired a second event
$expectedPresses = [ordered]@{
	inventory = 2; book = 2; crouch_toggle = 2; jump = 1; menu = 2
	interact_left = 0; interact_right = 0; use_left = 0; use_right = 0; stash_left = 0; stash_right = 0
	recenter = 0; magic_mode = 0
}

$smokeArgs = @{ Commands = $commands; Seconds = 1; ExpectEvents = $expectEvents }
if ($Rebuild) { $smokeArgs.Rebuild = $true }

Write-Host "==> VR input module check: $($commands.Count) commands$(if ($Unbind) { ' (inventory unbound on purpose: must FAIL)' })"
$launchStart = Get-Date
& (Join-Path $PSScriptRoot 'smoke-test.ps1') @smokeArgs
$smokeExit = $LASTEXITCODE
if ($smokeExit -eq 2) {
	Write-Host '==> SKIPPED: the smoke test was skipped'
	exit 2
}

$reasons = @()
if ($smokeExit -ne 0) { $reasons += "smoke test exit code $smokeExit (see its game events line)" }

# The game menu round trip: after the 'mark game menu' line, the state goes to MENU and back to GAMEPLAY
$log = Get-ArxLogPath (Get-ArxUserDir)
if (-not (Test-Path $log) -or (Get-Item $log).LastWriteTime -lt $launchStart) {
	$reasons += "no fresh arx.log at $log"
} else {
	$lines = Get-Content $log
	$markLine = ($lines | Select-String -SimpleMatch '[mark] game menu' | Select-Object -First 1)
	if (-not $markLine) {
		$reasons += "no '[mark] game menu' line in arx.log"
	} else {
		$after = $lines[($markLine.LineNumber)..($lines.Count - 1)]
		$states = @($after | Select-String -Pattern 'game\.state to=(\w+)' | ForEach-Object { $_.Matches[0].Groups[1].Value })
		Write-Host "game states after the menu button: $($states -join ' -> ')"
		$toMenu = [array]::IndexOf($states, 'MENU')
		$back = if ($toMenu -ge 0) { [array]::IndexOf($states, 'GAMEPLAY', $toMenu) } else { -1 }
		if ($toMenu -lt 0) { $reasons += 'the menu button did not open the game menu (no game.state MENU)' }
		elseif ($back -lt 0) { $reasons += 'the second menu press did not return to the game (no game.state GAMEPLAY after MENU)' }
	}
}

$status = Read-ArxStatus (Get-ArxTestDir)
$vrInput = if ($status -and $status.vr) { $status.vr.input } else { $null }
if (-not $vrInput -or -not $vrInput.game) {
	$reasons += 'no vr.input.game in the final status.json'
} else {
	Write-Host ''
	Write-Host ("{0,-16} {1,8} {2,8}" -f 'event', 'expected', 'presses')
	foreach ($name in $expectedPresses.Keys) {
		$presses = [int]$vrInput.presses.$name
		$mark = if ($presses -eq $expectedPresses[$name]) { '' } else { '  <-- wrong' }
		Write-Host ("{0,-16} {1,8} {2,8}{3}" -f $name, $expectedPresses[$name], $presses, $mark)
		if ($presses -ne $expectedPresses[$name]) { $reasons += "$name fired $presses times, expected $($expectedPresses[$name])" }
	}
	$move = $vrInput.game
	Write-Host "move vector at the end: x=$($move.moveX) y=$($move.moveY), turn=$($move.turn), held: '$($move.held)'"
	if ([math]::Abs([double]$move.moveY - 1) -gt 0.001 -or [math]::Abs([double]$move.moveX) -gt 0.001) {
		$reasons += "the move vector is ($($move.moveX), $($move.moveY)), expected (0, 1) with the left stick forward"
	}
	if ($move.held) { $reasons += "the engine is still given '$($move.held)' at the end" }
}

Write-Host ''
if ($reasons.Count -eq 0) {
	Write-Host '==> PASS VR input module check: inventory, book, crouch toggle, jump and the game menu reacted to their buttons, the move vector follows the stick'
	exit 0
}
Write-Host "==> FAIL VR input module check: $($reasons -join '; ')"
exit 1
