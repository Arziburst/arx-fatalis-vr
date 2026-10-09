<#
.SYNOPSIS
	Control table check (#30): drive the VR controls' table through the test channel in a flat run and
	give a PASS / FAIL verdict on the event counts in status.json.

.DESCRIPTION
	Runs smoke-test.ps1 (flat, --loadlevel 10, isolated, every launch rule of D-014) with 'vr-input'
	commands - controls pressed as if a controller did it, no headset needed:
	  tap right.a                               jump (a single binding fires on the press)
	  down right.grip, tap right.trigger, up    stash_right (an ordered chord; no click, no use)
	  tap right.grip                            use_right (the chord's leader taps on the release)
	  bind jump left.b+left.a                   remap jump to a chord led by the book's button
	  down left.b, tap left.a, up left.b        jump again (the chord; the book does not fire)
	  tap left.b                                book (now a tap on the release)
	  tap right.stick_down                      crouch_toggle (a stick direction)
	  reset                                     the default layout again
	Then reads the final status.json (written when the game exits) and compares vr.input.presses
	and vr.input.layout with the expected numbers.

	-Unbind sends 'vr-input bind jump none' first: the broken state on purpose, the check must FAIL.
	Exit code 0 on PASS, 1 on FAIL, 2 when the smoke test was skipped (the game lock stayed busy).
	One game launch.

.PARAMETER Unbind
	Unbind jump before the commands - the check must FAIL (proves it can).

.PARAMETER Rebuild
	Build this working copy first when its build is stale (passed to smoke-test.ps1).

.EXAMPLE
	.\scripts\dev\input-check.ps1
.EXAMPLE
	.\scripts\dev\input-check.ps1 -Unbind     # must FAIL: jump never fires
#>
[CmdletBinding()]
param(
	[switch]$Unbind,
	[switch]$Rebuild
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8
. (Join-Path $PSScriptRoot 'test-lib.ps1')

$commands = @()
if ($Unbind) { $commands += 'vr-input bind jump none' }
$commands += @(
	'vr-input tap right.a',
	'vr-input down right.grip', 'vr-input tap right.trigger', 'vr-input up right.grip',
	'vr-input tap right.grip',
	'vr-input bind jump left.b+left.a',
	'vr-input down left.b', 'vr-input tap left.a', 'vr-input up left.b',
	'vr-input tap left.b',
	'vr-input tap right.stick_down',
	'vr-input reset'
)

# Events and how often each must have fired; the zeros catch a press that counts as two events
$expected = [ordered]@{
	jump = 2; stash_right = 1; use_right = 1; interact_right = 0; book = 1; crouch_toggle = 1
	interact_left = 0; use_left = 0; stash_left = 0; inventory = 0; menu = 0; recenter = 0; magic_mode = 0
}

$smokeArgs = @{ Commands = $commands; Seconds = 1 }
if ($Rebuild) { $smokeArgs.Rebuild = $true }

Write-Host "==> control table check: $($commands.Count) vr-input commands$(if ($Unbind) { ' (jump unbound on purpose: must FAIL)' })"
& (Join-Path $PSScriptRoot 'smoke-test.ps1') @smokeArgs
$smokeExit = $LASTEXITCODE
if ($smokeExit -eq 2) {
	Write-Host '==> SKIPPED: the smoke test was skipped'
	exit 2
}

$reasons = @()
if ($smokeExit -ne 0) { $reasons += "smoke test exit code $smokeExit" }

$status = Read-ArxStatus (Get-ArxTestDir)
$vrInput = if ($status -and $status.vr) { $status.vr.input } else { $null }
if (-not $vrInput) {
	$reasons += 'no vr.input in the final status.json'
} else {
	Write-Host ''
	Write-Host ("{0,-16} {1,8} {2,8}" -f 'event', 'expected', 'presses')
	foreach ($name in $expected.Keys) {
		$presses = [int]$vrInput.presses.$name
		$mark = if ($presses -eq $expected[$name]) { '' } else { '  <-- wrong' }
		Write-Host ("{0,-16} {1,8} {2,8}{3}" -f $name, $expected[$name], $presses, $mark)
		if ($presses -ne $expected[$name]) { $reasons += "$name fired $presses times, expected $($expected[$name])" }
	}
	Write-Host "layout after reset: jump = $($vrInput.layout.jump), book = $($vrInput.layout.book); controllers: $($vrInput.controllers); down: '$($vrInput.down)'"
	if ($vrInput.layout.jump -ne 'right.a') { $reasons += "jump is bound to '$($vrInput.layout.jump)' after reset, expected right.a" }
	if ($vrInput.down) { $reasons += "controls still held at the end: $($vrInput.down)" }
}

Write-Host ''
if ($reasons.Count -eq 0) {
	Write-Host "==> PASS control table check: every event fired as often as expected ($($expected.Keys.Count) events), the remap and the reset took effect"
	exit 0
}
Write-Host "==> FAIL control table check: $($reasons -join '; ')"
exit 1
