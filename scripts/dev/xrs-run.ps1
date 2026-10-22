<#
.SYNOPSIS
	Run a scripted VR test sequence (.xrs, research rows F-05 / F-14, #113) on the game.

.DESCRIPTION
	A sequence is test-channel commands with asserts, log checks and shots (the directives are listed at the
	top of xrs-lib.ps1 and in docs/vr/TESTING.md, "Scripted sequences"). By default this script launches the
	game for the sequence through smoke-test.ps1 -Sequence - on the simulated headset (--vr-sim, no runtime,
	no SteamVR), with the start the sequence asks for (@sim, @args, @save, @playground, @player for a player's launch; default idle, a development launch and
	--loadlevel 10) - and
	so also checks exit code, clean shutdown, crash reports and [E] lines like every smoke test.

	  -Check    only parse the sequences (all of scripts\dev\xrs without a name): syntax errors with their
	            lines, no game, no launch
	  -Attach   run the sequence against a game that is already running with --test-dir (no launch, no
	            quit): for writing a sequence step by step

	Exit code 0 when every step passed, 1 when one failed, 2 when the run was skipped (the game lock was busy).

.PARAMETER Sequence
	A sequence by name (scripts\dev\xrs\<name>.xrs) or by path.

.PARAMETER TestDir
	The test channel directory of the running game (-Attach). Default: this working copy's.

.PARAMETER UserDir
	The user and config directory of the run (smoke-test.ps1 -UserDir). Default: a fresh one per sequence run, as in CI
	(#494); a read-back (xrs\readback\<name>-readback.xrs) gets the dir its sequence's run just before it left.

.PARAMETER Rebuild
	Build this working copy first when its build is stale.

.EXAMPLE
	.\scripts\dev\xrs-run.ps1 look-around
.EXAMPLE
	.\scripts\dev\xrs-run.ps1 -Check
.EXAMPLE
	.\scripts\dev\xrs-run.ps1 snap-turn -- --loadslot 0
	# arguments after -- replace the sequence's @args
.EXAMPLE
	.\scripts\dev\xrs-run.ps1 recenter -Attach
#>
[CmdletBinding()]
param(
	[Parameter(Position = 0)]
	[string]$Sequence,
	[switch]$Check,
	[switch]$Attach,
	[switch]$ContinueOnError,
	[string]$TestDir,
	[string]$UserDir,
	[switch]$Rebuild,
	[Parameter(ValueFromRemainingArguments)]
	[string[]]$ArxArgs
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8
. (Join-Path $PSScriptRoot 'xrs-lib.ps1')

if ($Check) {
	$names = if ($Sequence) { @($Sequence) } else { @(Get-ChildItem (Join-Path $PSScriptRoot 'xrs') -Filter '*.xrs' | ForEach-Object FullName) }
	$bad = 0
	foreach ($name in $names) {
		try {
			$parsed = Read-ArxSequence $name
			Write-Host ("  [ok]   {0,-14} {1} steps{2}" -f $parsed.Name, $parsed.Steps.Count, $(if ($parsed.Sim) { ", sim $($parsed.Sim)" } else { '' })) -ForegroundColor Green
		} catch {
			$bad++
			Write-Host "  [FAIL] $_" -ForegroundColor Red
		}
	}
	exit $(if ($bad) { 1 } else { 0 })
}
if (-not $Sequence) { throw 'name a sequence (scripts\dev\xrs\*.xrs), or use -Check' }
$parsed = Read-ArxSequence $Sequence

if ($Attach) {
	if (-not $TestDir) { $TestDir = Get-ArxTestDir }
	if (-not (Read-ArxStatus $TestDir)) { throw "no running game answers in $TestDir (start it with --test-dir, or leave out -Attach)" }
	$result = Invoke-ArxSequence $parsed -TestDir $TestDir -LogPath $(if ($UserDir) { Get-ArxLogPath $UserDir } else { Get-ArxLogPath }) -ContinueOnError:$ContinueOnError
	foreach ($p in $result.Pending) { Write-Host "  pending $p" -ForegroundColor Yellow }
	if ($result.Passed) { Write-Host "sequence $($result.Name): PASS" -ForegroundColor Green; exit 0 }
	Write-Host "sequence $($result.Name): FAIL ($($result.Failures.Count))" -ForegroundColor Red
	exit 1
}

$smokeArgs = @{ Sequence = $parsed.Path; Rebuild = $Rebuild }
# A flat sequence says so: --no-vr in its @args and no @sim (stand-up.xrs); every other one runs on the idle simulated headset
if ($parsed.Sim) { $smokeArgs.VrSim = $parsed.Sim } elseif ($parsed.Args -notcontains '--no-vr') { $smokeArgs.VrSim = 'idle' }
if ($TestDir) { $smokeArgs.TestDir = $TestDir }
if ($UserDir) { $smokeArgs.UserDir = $UserDir }
if ($parsed.Save) { $smokeArgs.Save = $parsed.Save }
if ($parsed.Playground) { $smokeArgs.Playground = $true }
if ($parsed.Player) { $smokeArgs.Player = $true }
$launchArgs = @(if ($ArxArgs) { $ArxArgs } elseif ($parsed.Args) { $parsed.Args })
& (Join-Path $PSScriptRoot 'smoke-test.ps1') @smokeArgs @launchArgs
exit $LASTEXITCODE
