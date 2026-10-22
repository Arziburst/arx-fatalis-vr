<#
.SYNOPSIS
	VR input module check (#74): press the VR buttons through the test channel in a flat run and give
	a PASS / FAIL verdict on what the game did - inventory, book, crouch, jump, game menu.

.DESCRIPTION
	#30's input-check.ps1 proves the control table resolves the controls into events; this one proves
	the game reacts to the events. Runs smoke-test.ps1 (flat, --loadlevel 10, isolated, every launch
	rule of D-014) with 'vr-input' commands - controls pressed as if a controller did it, no headset:
	  setplayercontrols on, cinemascope off      the level 10 start holds the player (TESTING.md); its scene's skip
	                                             ate A, B and trigger presses: '@until scriptedScene eq false' (#473)
	  tap left.a, tap left.a                     inventory closed, open (the level start shows the bag)
	  tap left.b, tap left.b                     book open, closed            (ui.book = 2)
	  tap right.stick_down x2                    crouch toggle on, off        (player.crouch = 2)
	  tap right.stick_up                         a jump                       (player.jump = 1)
	  stick left 0 1                             the move vector: forward 1   (vr.input.game.moveY)
	  tap right.b, tap right.b                   the game menu, and back      (game.state MENU, GAMEPLAY)
	The inventory is on the left A by default (#377); the book is an item of the radial menu (D-042, #376) and is
	bound here to the left B in place of the map, so each press is one
	change. The right B keeps its radial
	menu: a flat run has no head, so the menu cannot open and the press is the game menu key (#377) - the
	game menu's way when no radial menu can be shown.
	'mark' lines between the steps give the game frames to finish one before the next.
	The counts come from the final status.json (smoke-test -ExpectEvents); the order from arx.log:
	after the first '[vr] input: <event>' line of a button, its game events must change state twice,
	each the opposite of the one before (the menu: MENU, then GAMEPLAY); the move vector and the event
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

# ... and its scene skips every A, B and trigger press while it plays (#394): the presses wait until the status says it
# no longer plays and the controls are free (#473)
$commands = @('setplayercontrols on', 'cinemascope off', '@until scriptedScene eq false 10s', '@until player.controlsBlocked eq false 10s')
# The book is a radial menu item since D-042 and the left B is the map (#377): the book on its own button for this check
$commands += @('vr-input bind radial_left none', 'vr-input bind map none', 'vr-input bind inventory left.a', 'vr-input bind book left.b')
if ($Unbind) { $commands += 'vr-input bind inventory none' }
$commands += Step 'tap left.a' 'inventory open'
$commands += Step 'tap left.a' 'inventory closed'
$commands += Step 'tap left.b' 'book open'
$commands += Step 'tap left.b' 'book closed'
$commands += Step 'tap right.stick_down' 'crouch on'
$commands += Step 'tap right.stick_down' 'crouch off'
$commands += Step 'tap right.stick_up' 'jump'
$commands += Step 'stick left 0 1' 'move forward'
$commands += Step 'tap right.b' 'game menu'
$commands += Step 'tap right.b' 'back in the game'

# At least: the level start shows the inventory bag (one more ui.inventory); the order is checked below
$expectEvents = @('ui.inventory>=2', 'ui.book=2', 'player.crouch=2', 'player.jump=1')
# Event presses in the control table: the zeros catch a button that fired a second event
$expectedPresses = [ordered]@{
	inventory = 2; book = 2; crouch_toggle = 2; jump = 1; radial_right = 2
	interact_left = 0; interact_right = 0; use_left = 0; use_right = 0; stash_left = 0; stash_right = 0
	menu = 0; recenter = 0; magic_mode = 0; weapon = 0; map = 0
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

# What each button changed, in arx.log order: the game event lines after the button's first
# '[vr] input: <event>' line (the event is resolved, then the game reacts in the same frame)
function Get-StatesAfter([string[]]$lines, [string]$event, [string]$pattern) {
	$press = $lines | Select-String -SimpleMatch "[vr] input: $event (" | Select-Object -First 1
	if (-not $press) { return $null }
	$after = $lines[($press.LineNumber - 1)..($lines.Count - 1)]
	return @($after | Select-String -Pattern $pattern | ForEach-Object { $_.Matches[0].Groups[1].Value })
}

$log = Get-ArxLogPath (Get-ArxUserDir)
if (-not (Test-Path $log) -or (Get-Item $log).LastWriteTime -lt $launchStart) {
	$reasons += "no fresh arx.log at $log"
} else {
	$lines = Get-Content $log
	# The inventory and the book: two presses, two changes of state, each the opposite of the one before.
	# The level 10 start already shows the inventory bag (open) once the controls are given back.
	foreach ($check in @(@('inventory', 'ui\.inventory state=(\w+)'), @('book', 'ui\.book state=(\w+)'), @('crouch_toggle', 'player\.crouch state=(\w+)'))) {
		$states = Get-StatesAfter $lines $check[0] $check[1]
		Write-Host "$($check[0]) after its first press: $(if ($states) { $states -join ' -> ' } else { '(nothing)' })"
		if (-not $states -or $states.Count -ne 2 -or $states[0] -eq $states[1]) {
			$reasons += "$($check[0]): expected two opposite state changes after its presses, got '$($states -join ' -> ')'"
		}
	}
	# The game menu round trip through the radial menu's button (the game menu key, #377): MENU and back to GAMEPLAY
	$states = Get-StatesAfter $lines 'radial_right' 'game\.state to=(\w+)'
	Write-Host "game states after the radial menu's button: $(if ($states) { $states -join ' -> ' } else { '(nothing)' })"
	$keys = @($lines | Select-String -SimpleMatch 'vr.radial.gameMenuKey').Count
	if ($keys -ne 2) { $reasons += "the radial menu's button was the game menu key $keys times, expected 2" }
	$toMenu = if ($states) { [array]::IndexOf($states, 'MENU') } else { -1 }
	$back = if ($toMenu -ge 0) { [array]::IndexOf($states, 'GAMEPLAY', $toMenu) } else { -1 }
	if ($toMenu -lt 0) { $reasons += 'the menu button did not open the game menu (no game.state MENU)' }
	elseif ($back -lt 0) { $reasons += 'the second menu press did not return to the game (no game.state GAMEPLAY after MENU)' }
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
