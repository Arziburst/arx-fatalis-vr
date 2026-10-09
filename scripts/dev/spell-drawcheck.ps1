<#
.SYNOPSIS
	Spell draw check (#155): cast the spells of #71 for real, then draw the scene twice per frame with
	the same camera (drawcheck) and give a PASS / FAIL verdict on the numbers.

.DESCRIPTION
	Runs smoke-test.ps1 (flat, --loadlevel 10, isolated, every launch rule of D-014) in a background
	job with the commands
	  invulnerability on
	  <Setup>
	  spellcast -<Flags> 5 <spell> self                      for every spell of -Spells
	  <NpcCaster>.spellcast -<Flags> 5 <spell> player        for every spell of -NpcSpells
	  drawcheck <Frames>
	Meanwhile this script samples status.json every 100 ms: spells.launchedByType (spells started since
	the level was loaded) and, while the draw check runs, spells.activeByType (what was on screen).

	PASS when the smoke test passed (its draw check gate included: 0 differing frames), every spell
	of -Spells and -NpcSpells was launched at least once, and at least one spell was active while the
	check ran. Exit code 0 on PASS, 1 on FAIL, 2 when the smoke test was skipped (the game lock stayed
	busy).

	The flags matter: on the player a spellcast launches at once only with -s and -m (NOANIM + NODRAW);
	with -fs alone it is queued for the rune drawing, which skips the player - nothing launches
	(TRAPS.md). '-Flags fs' is the broken state on purpose: the check must FAIL with it.

.PARAMETER Spells
	Spells the player casts on itself (script names, as in 'spellcast'). Default: 23 of the 26 of #71,
	the long-lived ones first and the projectiles last, so the projectiles are still in flight when the
	draw check starts.

.PARAMETER NpcCaster
	The entity that casts -NpcSpells on the player (default human_base_0033, an NPC of level 10).

.PARAMETER NpcSpells
	The other 3 of #71, cast by an NPC: fake_summon launches only for a non-player caster
	(FakeSummonSpell::CanLaunch), and summon_creature / raise_dead cast by the player on level 10 find
	no valid spot 3 m ahead (ARX_INTERACTIVE_ConvertToValidPosForIO) - at the start point, in all four
	directions, and next to this NPC. The draw code (the fissure) is the same for any caster.

.PARAMETER Setup
	Channel commands sent after 'invulnerability on', before the casts.

.PARAMETER Flags
	The spellcast flags without the dash (default fsm).

.PARAMETER Frames
	Frames of the draw check (default 240).

.PARAMETER Rebuild
	Build this working copy first when its build is stale (passed to smoke-test.ps1).

.PARAMETER DataDir
	The Arx Fatalis install folder (passed to smoke-test.ps1; default: found in the Steam libraries).

.PARAMETER LockTimeoutSec
	How long to wait for the game lock before the check is skipped (passed to smoke-test.ps1, default 300).

.EXAMPLE
	.\scripts\dev\spell-drawcheck.ps1
.EXAMPLE
	.\scripts\dev\spell-drawcheck.ps1 -Flags fs     # must FAIL: no spell launches
#>
[CmdletBinding()]
param(
	[string[]]$Spells = @(
		'speed', 'heal', 'levitate', 'bless', 'fire_field', 'ice_field', 'rune_of_guarding', 'create_field',
		'repel_undead', 'curse', 'confuse', 'harm', 'mana_drain',
		'life_drain', 'cure_poison', 'create_food', 'explosion',
		'magic_missile', 'fireball', 'ice_projectile', 'poison_projectile', 'lightning_strike',
		'mass_lightning_strike'),
	[string]$NpcCaster = 'human_base_0033',
	[string[]]$NpcSpells = @('summon_creature', 'raise_dead', 'fake_summon'),
	[string[]]$Setup = @(),
	[string]$Flags = 'fsm',
	[int]$Frames = 240,
	[switch]$Rebuild,
	[string]$DataDir,
	[int]$LockTimeoutSec = 300
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8
. (Join-Path $PSScriptRoot 'test-lib.ps1')

$testDir = Get-ArxTestDir
# The NPC's summons last: their fissures are short (fake_summon 4 s), so they are still open when the check runs
$commands = @('invulnerability on') + $Setup +
            @($Spells | ForEach-Object { "spellcast -$Flags 5 $_ self" }) +
            @($NpcSpells | ForEach-Object { "$NpcCaster.spellcast -$Flags 5 $_ player" }) + @("drawcheck $Frames")
$allSpells = @($NpcSpells) + @($Spells)

$smokeTest = Join-Path $PSScriptRoot 'smoke-test.ps1'
$jobArgs = @{ Commands = $commands; Seconds = 2 }
if ($Rebuild) { $jobArgs.Rebuild = $true }
if ($DataDir) { $jobArgs.DataDir = $DataDir }
$jobArgs.LockTimeoutSec = $LockTimeoutSec

Write-Host "==> spell draw check: $($Spells.Count) spells cast on the player and $($NpcSpells.Count) by $NpcCaster with -$Flags, drawcheck $Frames, sampling $testDir\status.json"
$job = Start-Job -ScriptBlock {
	param($Script, $Params)
	& $Script @Params 2>&1
	"SMOKE_EXIT=$LASTEXITCODE"
} -ArgumentList $smokeTest, $jobArgs

# Sample while the game runs
$launched = @{}
$launchLog = New-Object System.Collections.Generic.List[string]
$activeDuringCheck = @{}
$maxActiveDuringCheck = 0
$checkSamples = 0
$lastCheck = $null
$lastFrame = -1
while ($job.State -eq 'Running') {
	$s = Read-ArxStatus $testDir
	if ($s -and $s.state -eq 'GAMEPLAY' -and $s.spells -and [int]$s.frame -ne $lastFrame) {
		$lastFrame = [int]$s.frame
		foreach ($p in $s.spells.launchedByType.PSObject.Properties) {
			$n = [int]$p.Value
			$before = if ($launched.ContainsKey($p.Name)) { $launched[$p.Name] } else { 0 }
			if ($n -ne $before) { $launchLog.Add("frame $($s.frame): $($p.Name) launched x$n (last command: $($s.lastCmd))") }
			$launched[$p.Name] = $n
		}
		if ($s.drawCheck) { $lastCheck = $s.drawCheck }
		if ($s.drawCheck -and $s.drawCheck.running) {
			$checkSamples++
			$maxActiveDuringCheck = [math]::Max($maxActiveDuringCheck, [int]$s.spells.active)
			foreach ($p in $s.spells.activeByType.PSObject.Properties) {
				$activeDuringCheck[$p.Name] = [math]::Max([int]$activeDuringCheck[$p.Name], [int]$p.Value)
			}
		}
	}
	Start-Sleep -Milliseconds 100
}

$output = @(Receive-Job $job)
Remove-Job $job
$smokeExit = -1
foreach ($line in $output) {
	if ("$line" -match '^SMOKE_EXIT=(-?\d+)$') { $smokeExit = [int]$Matches[1] } else { Write-Host $line }
}

if ($smokeExit -eq 2) {
	Write-Host '==> SKIPPED: the smoke test was skipped'
	exit 2
}

$reasons = @()
if ($smokeExit -ne 0) { $reasons += "smoke test exit code $smokeExit" }

Write-Host ''
Write-Host 'launches seen in status.json (a sample can cover several commands):'
$launchLog | ForEach-Object { Write-Host "  $_" }
Write-Host ''
Write-Host ("{0,-24} {1,9} {2,22}" -f 'spell', 'launched', 'active during check')
foreach ($spell in $allSpells) {
	$n = if ($launched.ContainsKey($spell)) { $launched[$spell] } else { 0 }
	$a = if ($activeDuringCheck.ContainsKey($spell)) { $activeDuringCheck[$spell] } else { 0 }
	Write-Host ("{0,-24} {1,9} {2,22}" -f $spell, $n, $a)
}
$others = @($launched.Keys | Where-Object { $_ -notin $allSpells } | Sort-Object)
if ($others) { Write-Host "also launched (sub-spells, NPCs): $(($others | ForEach-Object { "$_=$($launched[$_])" }) -join ', ')" }

$notLaunched = @($allSpells | Where-Object { -not $launched.ContainsKey($_) -or $launched[$_] -lt 1 })
if ($notLaunched) { $reasons += "$($notLaunched.Count) of $($allSpells.Count) spells never launched: $($notLaunched -join ', ')" }
if ($checkSamples -eq 0) { $reasons += 'no status sample while the draw check ran' }
elseif ($maxActiveDuringCheck -eq 0) { $reasons += 'no spell was active while the draw check ran' }
if (-not $lastCheck) { $reasons += 'no drawCheck in status.json' }

$checkLine = if ($lastCheck) { "$($lastCheck.framesDiffering) of $($lastCheck.frames) frames differ (max $($lastCheck.maxPixelsDiffering) pixels, max delta $($lastCheck.maxDelta))" } else { 'no result' }
Write-Host ''
if ($reasons.Count -eq 0) {
	Write-Host "==> PASS spell draw check: $($allSpells.Count) of $($allSpells.Count) spells launched, up to $maxActiveDuringCheck spells ($($activeDuringCheck.Count) types) active during the check ($checkSamples samples), drawcheck $checkLine"
	exit 0
}
Write-Host "==> FAIL spell draw check: $($reasons -join '; '); drawcheck $checkLine"
exit 1
