<#
.SYNOPSIS
	Run one check of a script self-test many times under the load of the other script tests (and an optional CPU
	load) and count how often it fails (#624): the reproduction of a flaky check and its negative control.

.DESCRIPTION
	The target test (default test-ci-smoke.ps1) runs -Runs times, one run after the other, each in its own pwsh. While
	it runs, -Parallel other script tests from run-script-tests.ps1's list run beside it in their own pwsh processes,
	refilled as they end (the parallel load of the script tests); -Load starts k busy loops that end by themselves
	after -LoadSeconds (a compile on the CI runner). Each run is judged by one check line of the target's output:
	  [ok]   <Check>      passed
	  [FAIL] <Check> ...  failed (the line is printed)
	  neither             missing (the section threw or the check was renamed) - counts as not passed
	The access point (#624): the check keeps its name "fail fast: nothing starts after the first FAIL" and prints
	the usual [ok] / [FAIL] line; how it judges is the worker's.

	-Mutant runs the target from a scratch copy of scripts\ (and .github\workflows\build-windows.yml) whose
	ci-smoke-lib.ps1 Invoke-ArxPhaseSchedule is changed; the tree of the working copy is not touched:
	  none             the working copy as it is. Expect: the check passes in every run
	  late-start       with -StopOnFail the scheduler is slow to come back from starting a phase (400 ms, longer
	                   than ff-fail's 150 ms): the phase beside the failing one is started before the FAIL is seen,
	                   but its job begins after the failing job ended - the order the flake has under load, every
	                   time. A correct fail fast. Expect: the check passes in every run
	  start-after-fail the negative control: fail fast leaves the first queued phase in the queue, so the scheduler
	                   starts one phase after it saw the FAIL. Expect: the check FAILS in every run
	Exit code 0 when every run met the expectation, 1 otherwise.

	Run it only while the PC is otherwise idle (no CI, gate, ci-local, game): it refuses while arx.exe runs.

.EXAMPLE
	.\scripts\dev\repeat-script-test.ps1                       # 20 runs beside 6 other script tests
	.\scripts\dev\repeat-script-test.ps1 -Load 16              # and 16 busy loops
	.\scripts\dev\repeat-script-test.ps1 -Mutant late-start -Runs 5
	.\scripts\dev\repeat-script-test.ps1 -Mutant start-after-fail -Runs 3 -Parallel 0
#>
[CmdletBinding()]
param(
	[int]$Runs = 20,
	[string]$Test = 'test-ci-smoke.ps1',
	[string]$Check = 'fail fast: nothing starts after the first FAIL',
	[int]$Parallel = 6,
	[int]$Load = 0,
	[int]$LoadSeconds = 0,
	[ValidateSet('none', 'late-start', 'start-after-fail')][string]$Mutant = 'none',
	# script tests that start or stop processes, hold the game lock or need a build: never part of the load
	[string[]]$Exclude = @('test-dump-helper.ps1', 'test-ci-slots.ps1', 'test-launch-lib.ps1', 'test-start-steamvr.ps1',
		'test-steam-shortcut.ps1', 'test-wait-launch.ps1', 'test-stress-sequence.ps1')
)

$ErrorActionPreference = 'Stop'
if (Get-Process arx -ErrorAction SilentlyContinue) { Write-Host 'repeat-script-test: a game runs on this PC - run it when the PC is idle' -ForegroundColor Red; exit 1 }

$pwsh = (Get-Process -Id $PID).Path
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$work = Join-Path ([IO.Path]::GetTempPath()) "arxvr-repeat-script-test-$PID"
$logs = Join-Path $work 'logs'
New-Item -ItemType Directory -Force $logs | Out-Null

# The target: the working copy's, or a scratch copy with a changed scheduler
$testDir = $PSScriptRoot
if ($Mutant -ne 'none') {
	$copy = Join-Path $work 'copy'
	Copy-Item -Recurse (Join-Path $repo 'scripts') (Join-Path $copy 'scripts')
	New-Item -ItemType Directory -Force (Join-Path $copy '.github\workflows') | Out-Null
	Copy-Item (Join-Path $repo '.github\workflows\build-windows.yml') (Join-Path $copy '.github\workflows')
	$testDir = Join-Path $copy 'scripts\dev'
	$lib = Join-Path $testDir 'ci-smoke-lib.ps1'
	$text = Get-Content -Raw $lib
	$anchor, $with = switch ($Mutant) {
		'late-start' {
			'Job = (& $Start $next $free); Started = Get-Date }'
			'Job = (& $Start $next $free); Started = Get-Date }; if ($StopOnFail) { Start-Sleep -Milliseconds 400 } # MUTANT late-start'
		}
		'start-after-fail' {
			'$queue.Clear()'
			'if ($queue.Count -gt 1) { $queue.RemoveRange(1, $queue.Count - 1) } # MUTANT start-after-fail: one phase starts after the FAIL'
		}
	}
	$at = $text.IndexOf($anchor)
	if ($at -lt 0 -or $text.IndexOf($anchor, $at + 1) -ge 0) { Write-Host "repeat-script-test: the mutant's anchor is not in ci-smoke-lib.ps1 exactly once: $anchor" -ForegroundColor Red; exit 1 }
	[IO.File]::WriteAllText($lib, $text.Substring(0, $at) + $with + $text.Substring($at + $anchor.Length), [Text.UTF8Encoding]::new($false))
}
$target = Join-Path $testDir $Test
$expectPass = $Mutant -ne 'start-after-fail'

# The load: the other script tests of run-script-tests.ps1's list
$listed = ((Get-Content -Raw (Join-Path $PSScriptRoot 'run-script-tests.ps1')) -split '(?m)^\$tests = @\(|^\$notTests = @\(')[1]
$pool = @([regex]::Matches($listed, "'(test-[\w-]+\.ps1)'") | ForEach-Object { $_.Groups[1].Value } |
	Where-Object { $_ -ne $Test -and $_ -notin $Exclude } | Select-Object -Unique)
if ($Parallel -gt 0 -and -not $pool) { Write-Host 'repeat-script-test: no script tests for the load' -ForegroundColor Red; exit 1 }

$loaders = @()
$background = [Collections.Generic.List[object]]::new()
$next = 0
$bgStarted = 0
function Start-Background {
	while ($background.Count -lt $Parallel) {
		$name = $pool[$script:next % $pool.Count]
		$script:next++
		$out = Join-Path $logs "bg-$($script:next)-$name.txt"
		$p = Start-Process $pwsh -ArgumentList '-NoProfile', '-File', "`"$(Join-Path $PSScriptRoot $name)`"" -WindowStyle Hidden -PassThru `
			-RedirectStandardOutput $out -RedirectStandardError "$out.err"
		$background.Add($p)
		$script:bgStarted++
	}
}
function Update-Background {
	foreach ($p in @($background)) { if ($p.HasExited) { $null = $background.Remove($p) } }
	Start-Background
}

$checkRe = [regex]::Escape($Check)
$passed = 0; $failed = 0; $missing = 0
$failLines = [Collections.Generic.List[string]]::new()
try {
	if ($Load -gt 0) {
		$seconds = if ($LoadSeconds -gt 0) { $LoadSeconds } else { $Runs * 45 + 120 }
		$busy = "`$end = (Get-Date).AddSeconds($seconds); while ((Get-Date) -lt `$end) { }"
		$loaders = @(1..$Load | ForEach-Object { Start-Process $pwsh -ArgumentList '-NoProfile', '-Command', $busy -WindowStyle Hidden -PassThru })
	}
	Write-Host "repeat-script-test: $Test x $Runs, check '$Check', mutant $Mutant, $Parallel script test(s) beside it, $Load busy loop(s)"
	if ($Parallel -gt 0) { Start-Background }
	for ($i = 1; $i -le $Runs; $i++) {
		$out = Join-Path $logs "run-$i.txt"
		$started = Get-Date
		$p = Start-Process $pwsh -ArgumentList '-NoProfile', '-File', "`"$target`"" -WindowStyle Hidden -PassThru `
			-RedirectStandardOutput $out -RedirectStandardError "$out.err"
		$null = $p.Handle # keeps the exit code readable
		while (-not $p.HasExited) {
			if ($Parallel -gt 0) { Update-Background }
			Start-Sleep -Milliseconds 200
		}
		$p.WaitForExit()
		$lines = @(Get-Content $out) + @(Get-Content "$out.err" -ErrorAction SilentlyContinue)
		$fail = @($lines | Where-Object { $_ -match "^\s*\[FAIL\]\s+$checkRe(\s+-\s.*)?\s*$" })
		$ok = @($lines | Where-Object { $_ -match "^\s*\[ok\]\s+$checkRe\s*$" })
		$verdict = if ($fail) { 'FAIL'; $failed++; $failLines.Add("run $($i): $($fail[0].Trim())") }
			elseif ($ok) { 'ok'; $passed++ }
			else { 'MISSING'; $missing++ }
		$met = if ($expectPass) { $verdict -eq 'ok' } else { $verdict -eq 'FAIL' }
		# The other checks that failed in this run: not judged here, named so a second flaky check is seen
		$others = @($lines | Where-Object { $_ -match '^\s*\[FAIL\]\s+(.*)$' -and $_ -notmatch "^\s*\[FAIL\]\s+$checkRe" } | ForEach-Object { $_.Trim() -replace '^\[FAIL\]\s+' })
		Write-Host ("  run {0,2}/{1}: {2,-7} (test exit {3}, {4:0} s){5}" -f $i, $Runs, $verdict, $p.ExitCode, ((Get-Date) - $started).TotalSeconds,
			$(if ($fail) { " - $($fail[0].Trim())" })) -ForegroundColor $(if ($met) { 'Green' } else { 'Red' })
		if ($others) { Write-Host "             other checks that failed in this run: $($others -join ' | ')" -ForegroundColor Yellow }
		if ($met -and -not $others) { Remove-Item $out, "$out.err" -ErrorAction SilentlyContinue }
	}
} finally {
	foreach ($p in @($loaders) + @($background)) { if ($p -and -not $p.HasExited) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue } }
	if ($Mutant -ne 'none') { Remove-Item -Recurse -Force (Join-Path $work 'copy') -ErrorAction SilentlyContinue }
	Get-ChildItem $logs -Filter 'bg-*' -ErrorAction SilentlyContinue | Remove-Item -ErrorAction SilentlyContinue
}

Write-Host ''
Write-Host "repeat-script-test: '$Check' failed $failed of $Runs run(s) (ok $passed, missing $missing); mutant $Mutant; $bgStarted script test run(s) beside it, $Load busy loop(s)"
$good = if ($expectPass) { $passed -eq $Runs } else { $failed -eq $Runs }
if (@(Get-ChildItem $logs -Filter 'run-*.txt').Count) { Write-Host "  the outputs of the runs that missed the expectation or had other failed checks: $logs" }
else { Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue }
if ($good) {
	Write-Host "repeat-script-test: as expected - $(if ($expectPass) { 'the check passed every run' } else { 'the check caught the broken scheduler every run' })" -ForegroundColor Green
	exit 0
}
Write-Host "repeat-script-test: NOT as expected - $(if ($expectPass) { 'the check must pass every run' } else { 'the check must fail every run on the broken scheduler' })" -ForegroundColor Red
exit 1
