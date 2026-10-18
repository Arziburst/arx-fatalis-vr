<#
.SYNOPSIS
	Run the PowerShell host tests of the dev scripts (scripts/dev/test-*.ps1) and fail if any of them fails (#127).

.DESCRIPTION
	The self-tests of the dev tooling need no game, no network and no headset: they run in seconds, in CI on every
	pull request (.github/workflows/build-windows.yml) and by hand. Each runs in its own pwsh process; the output
	of every test is printed, then one verdict line per test and a total. Exit code 0 when every test passed, 1 otherwise.

	A new test is a scripts/dev/test-<what>.ps1 that exits non-zero on a failure. Add it to $tests below. A test-*.ps1
	file in this folder that is neither in $tests nor in $notTests fails this script, so a new test cannot be forgotten
	and never run in CI.

.EXAMPLE
	.\scripts\dev\run-script-tests.ps1
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

# The self-tests. Keep the list sorted.
$tests = @(
	'test-ci-smoke.ps1'       # the CI game checks' lanes (#522): the order, the scheduler over fake phases, the lane count, durations, evidence; the impact map's selection and coverage
	'test-ci-slots.ps1'       # CI owns the game slots (#527, #526): the runner marker's priority, holder lines, orphans stopped, games die with their launcher
	'test-eye-check.ps1'     # the two-eyes check's judge (#112, F-06): every leg passes on good eyes, fails on its broken case
	'test-github-lib.ps1'     # the GitHub access of the dev scripts (#186): REST first, the board cache, rate limits, the move queue
	'test-launch-lib.ps1'     # the machine-wide game lock (#205): the lock folder and its ACL, old and new location, holders, stale files
	'test-log-digest.ps1'    # the run digest and the automatic bug filing (#131): fixture logs, a fake GitHub, the command line modes
	'test-steam-shortcut.ps1' # the non-Steam VR shortcut (#80): binary shortcuts.vdf round-trip, add / update / remove on copies
	'test-save-library.ps1'  # the test save library (#70): the scenario parser, the verification steps and slot order, Use-ArxSave
	'test-number-decisions.ps1' # decision numbers at merge (#274): two branches adding D-NEW get consecutive numbers; the clash gate
	'test-perf-summary.ps1'  # the performance log summary (#327): a fixture arx-perf.log, the totals, worst seconds, hitches, the script
	'test-private-data.ps1' # the personal-data check of tracked files (#428): made-up paths, ids, e-mails, docs images fail; placeholders pass
	'test-plan-next.ps1'    # the coordinator's task order (#187): MVP.md parsing and every start rule
	'test-wait-launch.ps1'   # the launch marker (#258): checks wait out the game lock before their gameplay timeout; a lock skip is exit 2
	'test-xrs.ps1'           # the .xrs sequence runner and img-diff (#113): parser, conditions, log marks, @pending, shots, a fake game
	'test-tree-fingerprint.ps1' # the build stamp's tree fingerprint (#306): an edit after a build makes it stale, no change stays fresh
	'test-usage-plan.ps1'   # the usage accounting (#217): calibration, burn, task costs, the worker-count recommendation, the core's status JSON (#346)
	'test-start-steamvr.ps1' # starting and quitting SteamVR on the owner's switches (#171): stubbed processes and clock, SteamVR is not touched
)
# test-*.ps1 files that are libraries or tools, not tests
$notTests = @(
	'test-lib.ps1'            # shared by smoke-test.ps1, game-cmd.ps1 and game-status.ps1
	'test-pr.ps1'             # the owner's test station: builds and launches a PR
)

$present = @(Get-ChildItem $PSScriptRoot -Filter 'test-*.ps1' -File | Select-Object -ExpandProperty Name)
$unknown = @($present | Where-Object { $_ -notin $tests -and $_ -notin $notTests })
$missing = @($tests | Where-Object { $_ -notin $present })
if ($unknown -or $missing) {
	if ($unknown) { Write-Host "run-script-tests: not listed in `$tests or `$notTests: $($unknown -join ', ')" -ForegroundColor Red }
	if ($missing) { Write-Host "run-script-tests: listed but missing: $($missing -join ', ')" -ForegroundColor Red }
	exit 1
}

$pwsh = (Get-Process -Id $PID).Path
$results = @()
foreach ($test in $tests) {
	Write-Host "==> $test" -ForegroundColor Cyan
	$started = Get-Date
	& $pwsh -NoProfile -File (Join-Path $PSScriptRoot $test)
	$results += [pscustomobject]@{ Test = $test; Exit = $LASTEXITCODE; Seconds = [math]::Round(((Get-Date) - $started).TotalSeconds, 1) }
}

Write-Host ''
$failed = @($results | Where-Object Exit -ne 0)
foreach ($r in $results) {
	$ok = $r.Exit -eq 0
	Write-Host ("  [{0}] {1} ({2} s{3})" -f $(if ($ok) { 'ok' } else { 'FAIL' }), $r.Test, $r.Seconds, $(if ($ok) { '' } else { ", exit $($r.Exit)" })) -ForegroundColor $(if ($ok) { 'Green' } else { 'Red' })
}
if ($failed) { Write-Host "script tests: $($failed.Count) of $($results.Count) FAILED" -ForegroundColor Red; exit 1 }
Write-Host "script tests: all $($results.Count) passed" -ForegroundColor Green
exit 0
