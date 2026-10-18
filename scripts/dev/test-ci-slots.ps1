<#
.SYNOPSIS
	Self-test of CI's ownership of the game slots (#527, #526; scripts/launch-lib.ps1): the runner's priority, the holder
	reports, orphan games, and games that die with their launcher. No game, no network, no lock of the PC.

.DESCRIPTION
	Everything runs in scratch folders ($env:ARXVR_LOCK_DIR and a fake %LOCALAPPDATA%: the real locks of the PC are not
	touched). A copy of PING.EXE named arx.exe stands in for a game (the way test-launch-lib.ps1 does it); arx.exe is
	never started. Holders, waiters and launchers are child pwsh processes.

	The interface this test assumes. It was written before the implementation (contract #527, tests first); the worker
	builds it in scripts/launch-lib.ps1 unless a line says otherwise:

	The runner marker (priority: while CI runs a job, every other launch waits without taking a slot)
	  Get-ArxRunnerMarkerPath                  -> <Get-ArxLockDir>\ci-runner.marker
	  Set-ArxRunnerMarker -RunnerPid <int> -What <string>
	                                           writes the marker: 'pid=<RunnerPid> since=<yyyy-MM-dd HH:mm:ss> what=<What>'.
	                                           RunnerPid is a process that lives as long as the CI job (in the workflow the
	                                           job's process, Runner.Worker.exe), so a job that dies leaves a stale marker
	  Clear-ArxRunnerMarker                    deletes the marker
	  Get-ArxRunnerMarker                      -> { Pid; Since; What } while the marker exists and its pid is alive, else
	                                           $null (a marker of a dead process is stale and counts as none)
	  $env:ARXVR_CI_RUNNER (non-empty)         the launch is the runner's own and ignores the marker; the workflow sets it
	                                           for the job (.github/workflows/build-windows.yml), like ARXVR_MAX_GAMES
	  Enter-ArxGameLock without ARXVR_CI_RUNNER while Get-ArxRunnerMarker is not $null: takes no slot and keeps waiting
	                                           (up to -TimeoutSec); its busy line contains 'CI runner' and the marker's
	                                           what=; once the marker is cleared or stale, it takes a slot as before. A
	                                           game that already holds a slot is never stopped or preempted: it finishes
	  build-windows.yml                        Set-ArxRunnerMarker in a step before the Build step, Clear-ArxRunnerMarker
	                                           in a step with 'if: always()', ARXVR_CI_RUNNER set

	The holder reports (no silent waits)
	  Enter-ArxGameLock -ReportSec <int>       how often a waiting launch prints the holders (the first time at once):
	                                           '  held: <file>: pid=<n> game=<n|-> since=<time> what=<..> copy=<..> branch=<..>'
	                                           (the existing line format)
	  on timeout                               before it returns $null, one line
	                                           'game lock: timed out after <TimeoutSec> s - held: <holder line>[; <holder line>]'
	                                           (the test asks for 'timed out after <n> s' and the holder's pid= in one line)
	  an orphan                                a lock file that is no longer held (its launcher pid is gone) and names
	                                           game=<pid> of a running arx.exe: Enter-ArxGameLock stops that arx.exe and
	                                           writes one line containing 'orphan' and the game's pid, e.g.
	                                           'game lock: stopped the orphan arx.exe pid 1234 - its launcher pid 999 is gone (game-1.lock: <holder line>)'
	                                           Never stopped: a process that is not arx.exe (the pid was reused), the game of
	                                           a live holder, an arx.exe no lock file names (the owner's game)
	  scripts/dev/ci-smoke.ps1                 the default -LockTimeoutSec is at most 300 (was 900)

	Games die with their launcher (#526)
	  Register-ArxGameProcess <Process>        called by the launcher right after Start-Process: puts the game into a job
	                                           object of the launcher's process with JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE, so
	                                           the game ends when the launcher process ends, killed or not
	  scripts/dev/smoke-test.ps1 and make-test-saves.ps1 call it for every game they start (the games of ci-smoke and
	  ci-local are started by those two)

	Exit code 0 when everything passed, 1 otherwise.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Join-Path ([IO.Path]::GetTempPath()) "arxvr-ci-slots-test-$PID"
$lockDir = Join-Path $root 'machine\locks'
$localBase = Join-Path $root 'local'
New-Item -ItemType Directory -Force $lockDir, $localBase | Out-Null
$env:ARXVR_LOCK_DIR = $lockDir
$env:LOCALAPPDATA = $localBase
Remove-Item Env:ARXVR_MAX_GAMES, Env:ARXVR_CI_RUNNER -ErrorAction SilentlyContinue
$legacyDir = Join-Path $localBase 'ArxFatalisVR'
$libPath = (Resolve-Path (Join-Path $PSScriptRoot '..\launch-lib.ps1')).Path
. $libPath

$script:failures = 0
function Check([string]$Name, [bool]$Ok, [string]$Detail = '') {
	if ($Ok) { Write-Host "  [ok]   $Name" -ForegroundColor Green }
	else { Write-Host "  [FAIL] $Name$(if ($Detail) { " - $Detail" })" -ForegroundColor Red; $script:failures++ }
}

$pwsh = (Get-Process -Id $PID).Path
$script:children = @()
$scripts = Split-Path -Parent $PSScriptRoot
$text = { param($Path) [IO.File]::ReadAllText((Join-Path $scripts $Path)) }

# A child pwsh with the library. Like test-launch-lib.ps1's holders it does not look at the real games of the PC (#224).
# $Code is one line (statements joined with ';'), single quotes only.
function Start-Child([string]$Code) {
	$p = Start-Process $pwsh -PassThru -WindowStyle Hidden -ArgumentList @('-NoProfile', '-Command', ". '$libPath'; function Get-ArxGamesOutsideLock { @() }; $Code")
	$script:children += $p
	$p
}
function Wait-Until([scriptblock]$Condition, [int]$Sec = 20) {
	$deadline = (Get-Date).AddSeconds($Sec)
	while ((Get-Date) -lt $deadline) { if (& $Condition) { return $true }; Start-Sleep -Milliseconds 200 }
	$false
}
# A call whose host lines are wanted: { Value (the first output object); Lines (the Write-Host text); Error (a throw) }
function Invoke-Captured([scriptblock]$Call) {
	$value = @()
	$lines = @()
	$err = $null
	try {
		foreach ($item in @(& $Call 6>&1)) {
			if ($item -is [Management.Automation.InformationRecord]) { $lines += "$($item.MessageData)" } else { $value += $item }
		}
	} catch { $err = "$_" }
	[pscustomobject]@{ Value = $(if ($value.Count) { $value[0] } else { $null }); Lines = $lines; Error = $err }
}
# A call that may not exist yet: its result, or $null with the error kept for the detail of the next check
$script:lastError = ''
function Invoke-Safe([scriptblock]$Call) {
	try { & $Call } catch { $script:lastError = "$_"; $null }
}
function Get-GameHolders { @(Get-ArxLockHolders | Where-Object { $_ -match '^game-\d+\.lock: ' }) }
function Test-Alive($Process) { $Process -and -not $Process.HasExited }
function Stop-Children {
	foreach ($c in $script:children) { try { Stop-Process -Id $c.Id -Force -ErrorAction SilentlyContinue } catch { } }
	$script:children = @()
	Start-Sleep -Milliseconds 300
	foreach ($dir in $lockDir, $legacyDir) { Remove-Item (Join-Path $dir '*.lock'), (Join-Path $dir '*.marker') -Force -ErrorAction SilentlyContinue }
}

# The stand-in games: PING.EXE copied as arx.exe (a game) and as notarx.exe (a process that is not a game); each pings
# for two minutes. The games outside the lock are the real WMI list, limited to the stand-ins: a real game on this PC
# (the owner's) must not change the result.
$fakeDir = Join-Path $root 'fake'
New-Item -ItemType Directory -Force $fakeDir | Out-Null
$fakeArx = Join-Path $fakeDir 'arx.exe'
$fakeOther = Join-Path $fakeDir 'notarx.exe'
Copy-Item (Join-Path $env:SystemRoot 'System32\PING.EXE') $fakeArx
Copy-Item (Join-Path $env:SystemRoot 'System32\PING.EXE') $fakeOther
function Start-FakeGame([string]$Exe = $fakeArx) {
	$p = Start-Process $Exe -PassThru -WindowStyle Hidden -ArgumentList '-n', '120', '127.0.0.1'
	$script:children += $p
	$p
}
$realOutsideLock = ${function:Get-ArxGamesOutsideLock}
function Get-ArxGamesOutsideLock { @(& $realOutsideLock | Where-Object { $_.Path -and $_.Path.StartsWith($fakeDir, [StringComparison]::OrdinalIgnoreCase) }) }

try {
	# --- Priority: the runner marker ------------------------------------------------------------------------------
	Write-Host '==> priority: while the CI runner runs a job, other launches wait without a slot'
	Check 'the runner marker is ci-runner.marker in the lock folder' ((Invoke-Safe { Get-ArxRunnerMarkerPath }) -eq (Join-Path $lockDir 'ci-runner.marker')) $script:lastError
	Check 'no marker: no runner' ($null -eq (Invoke-Safe { Get-ArxRunnerMarker }))
	$runnerJob = Start-Child 'Start-Sleep 120'
	$null = Invoke-Safe { Set-ArxRunnerMarker -RunnerPid $runnerJob.Id -What 'CI run 39109130415' }
	Check 'Set-ArxRunnerMarker writes the marker file' (Test-Path (Join-Path $lockDir 'ci-runner.marker')) $script:lastError
	$marker = Invoke-Safe { Get-ArxRunnerMarker }
	Check 'a marker of a live process is the runner: its pid and what' ($marker -and $marker.Pid -eq $runnerJob.Id -and $marker.What -eq 'CI run 39109130415') "$($marker | ConvertTo-Json -Compress) $script:lastError"

	$sw = [Diagnostics.Stopwatch]::StartNew()
	$r = Invoke-Captured { Enter-ArxGameLock -What 'smoke-test.ps1' -Copy 'E:\work\worker' -Branch 'ARXVR-1-feat-x' -TimeoutSec 2 }
	Check 'a worker launch while the runner marker is up gets no slot (it waits out its timeout)' ($null -eq $r.Value -and $sw.Elapsed.TotalSeconds -ge 1.8) "slot $($r.Value.Slot), $([int]$sw.Elapsed.TotalSeconds) s $($r.Error)"
	if ($r.Value) { Exit-ArxGameLock $r.Value }
	Check 'its busy line names the CI runner and the marker''s job' (@($r.Lines | Where-Object { $_ -match 'CI runner' -and $_ -match 'CI run 39109130415' }).Count -ge 1) ($r.Lines -join ' | ')
	Check 'no game slot is held after that wait' ((Get-GameHolders).Count -eq 0) "$(Get-GameHolders)"

	$got = Join-Path $root 'waiter-got.txt'
	$waiter = Start-Child "`$l = Enter-ArxGameLock -What 'ci-local smoke-test.ps1' -Copy 'E:\work\worker2' -Branch 'ARXVR-2-feat-y' -TimeoutSec 30; if (`$l) { Set-Content -LiteralPath '$got' -Value `$l.Slot }; Start-Sleep 60"
	Start-Sleep -Seconds 3
	Check 'a waiting worker (ci-local) holds no slot while the marker is up' (-not (Test-Path $got) -and @(Get-GameHolders | Where-Object { $_ -match "pid=$($waiter.Id) " }).Count -eq 0) "$(Get-GameHolders)"
	$null = Invoke-Safe { Clear-ArxRunnerMarker }
	Check 'Clear-ArxRunnerMarker removes the marker' (-not (Test-Path (Join-Path $lockDir 'ci-runner.marker'))) $script:lastError
	Remove-Item (Join-Path $lockDir 'ci-runner.marker') -Force -ErrorAction SilentlyContinue
	Check 'the marker cleared: the waiting worker takes a slot within 5 s' (Wait-Until { Test-Path $got } 5)
	Stop-Children

	$runnerJob = Start-Child 'Start-Sleep 120'
	$null = Invoke-Safe { Set-ArxRunnerMarker -RunnerPid $runnerJob.Id -What 'CI run 2' }
	$env:ARXVR_CI_RUNNER = '1'
	$env:ARXVR_MAX_GAMES = '2'
	$lane1 = Enter-ArxGameLock -What 'smoke-test.ps1' -Copy 'C:\runner\copy' -Branch 'ARXVR-527' -TimeoutSec 0 6>$null
	$lane2 = Enter-ArxGameLock -What 'smoke-test.ps1' -Copy 'C:\runner\copy' -Branch 'ARXVR-527' -TimeoutSec 0 6>$null
	Check 'the runner''s own launches (ARXVR_CI_RUNNER) ignore the marker: both lanes take slots 1 and 2' ($lane1 -and $lane2 -and $lane1.Slot -eq 1 -and $lane2.Slot -eq 2)
	Exit-ArxGameLock $lane1
	Exit-ArxGameLock $lane2
	Remove-Item Env:ARXVR_CI_RUNNER, Env:ARXVR_MAX_GAMES -ErrorAction SilentlyContinue
	Stop-Children

	# A worker's game that runs when the job starts finishes its phase; the runner takes the other slot at once and the
	# worker's slot as soon as it frees
	$worker = Start-Child "`$l = Enter-ArxGameLock -What 'smoke-test.ps1' -Copy 'E:\work\worker' -Branch 'ARXVR-1-feat-x' -TimeoutSec 0; Start-Sleep 120"
	$null = Wait-Until { @(Get-GameHolders | Where-Object { $_ -match "pid=$($worker.Id) " }).Count -eq 1 }
	$runnerJob = Start-Child 'Start-Sleep 120'
	$null = Invoke-Safe { Set-ArxRunnerMarker -RunnerPid $runnerJob.Id -What 'CI run 3' }
	$env:ARXVR_CI_RUNNER = '1'
	$env:ARXVR_MAX_GAMES = '2'
	$runnerA = Enter-ArxGameLock -What 'smoke-test.ps1' -Copy 'C:\runner\copy' -Branch 'ARXVR-527' -TimeoutSec 0 6>$null
	Check 'the runner takes the free slot beside a running worker game' ($runnerA -and $runnerA.Slot -eq 2) "slot $($runnerA.Slot)"
	Start-Sleep -Seconds 2
	Check 'the worker''s running game is not preempted: its process and its slot stay' (-not $worker.HasExited -and @(Get-GameHolders | Where-Object { $_ -match "pid=$($worker.Id) " }).Count -eq 1) "$(Get-GameHolders)"
	Check 'both slots held: a third runner launch gets none' ($null -eq (Enter-ArxGameLock -What 'smoke-test.ps1' -Copy 'C:\runner\copy' -Branch 'ARXVR-527' -TimeoutSec 0 6>$null))
	Stop-Process -Id $worker.Id -Force
	$script:runnerB = $null
	Check 'the worker''s phase ended: the runner takes its slot within 5 s' (Wait-Until { $script:runnerB = Enter-ArxGameLock -What 'smoke-test.ps1' -Copy 'C:\runner\copy' -Branch 'ARXVR-527' -TimeoutSec 0 6>$null; [bool]$script:runnerB } 5)
	Exit-ArxGameLock $runnerA
	Exit-ArxGameLock $script:runnerB
	Remove-Item Env:ARXVR_CI_RUNNER, Env:ARXVR_MAX_GAMES -ErrorAction SilentlyContinue
	Stop-Children

	# A job that died leaves its marker behind: it must not block the PC
	$deadJob = Start-Child 'Start-Sleep 120'
	$null = Invoke-Safe { Set-ArxRunnerMarker -RunnerPid $deadJob.Id -What 'CI run 4' }
	Stop-Process -Id $deadJob.Id -Force
	$null = Wait-Until { $deadJob.HasExited } 5
	Check 'a marker whose runner process is gone is stale: no runner' ((Test-Path (Join-Path $lockDir 'ci-runner.marker')) -and $null -eq (Invoke-Safe { Get-ArxRunnerMarker })) $script:lastError
	$afterStale = Enter-ArxGameLock -What 'smoke-test.ps1' -Copy 'E:\work\worker' -Branch 'ARXVR-1-feat-x' -TimeoutSec 0 6>$null
	Check 'and a worker launch takes a slot at once' ($null -ne $afterStale)
	Exit-ArxGameLock $afterStale
	Stop-Children

	$wf = Get-Content -Raw (Join-Path $PSScriptRoot '..\..\.github\workflows\build-windows.yml')
	$buildAt = $wf.IndexOf('- name: Build')
	$setAt = $wf.IndexOf('Set-ArxRunnerMarker')
	Check 'the workflow writes the runner marker before the Build step' ($setAt -ge 0 -and $buildAt -ge 0 -and $setAt -lt $buildAt) "Set-ArxRunnerMarker at $setAt, Build at $buildAt"
	$clearStep = [regex]::Match($wf, '(?s)- name:(?:(?!\r?\n    - name:).)*?Clear-ArxRunnerMarker').Value
	Check 'the workflow clears the marker in a step that always runs' ($clearStep -match '\n\s+if: always\(\)') $clearStep
	Check 'the workflow marks its launches as the runner''s (ARXVR_CI_RUNNER)' ($wf -match 'ARXVR_CI_RUNNER:')

	# --- The holder reports ---------------------------------------------------------------------------------------
	Write-Host '==> a busy slot names its holder; no silent waits'
	$holder = Start-Child "`$l = Enter-ArxGameLock -What 'holder' -Copy 'E:\work\other' -Branch 'other-branch' -TimeoutSec 0; Start-Sleep 120"
	Check 'the holder process took the slot' (Wait-Until { @(Get-GameHolders | Where-Object { $_ -match "pid=$($holder.Id) " }).Count -eq 1 })
	$r = Invoke-Captured { Enter-ArxGameLock -What 'waiter' -Copy 'c' -Branch 'b' -TimeoutSec 0 }
	$heldLine = "held: game-1\.lock: pid=$($holder.Id) game=\S+ since=\d{4}-\d\d-\d\d \d\d:\d\d:\d\d what=holder copy=E:\\work\\other branch=other-branch"
	Check 'at once: the holder is named with pid, what, copy and since' (@($r.Lines | Where-Object { $_ -match $heldLine }).Count -ge 1) ($r.Lines -join ' | ')
	$r = Invoke-Captured { Enter-ArxGameLock -What 'waiter' -Copy 'c' -Branch 'b' -TimeoutSec 3 -ReportSec 1 }
	Check 'a live holder is waited on with periodic holder lines (-ReportSec 1, 3 s: at least 2)' ($null -eq $r.Error -and @($r.Lines | Where-Object { $_ -match $heldLine }).Count -ge 2) "$($r.Error) $($r.Lines -join ' | ')"
	$r = Invoke-Captured { Enter-ArxGameLock -What 'waiter' -Copy 'c' -Branch 'b' -TimeoutSec 2 }
	Check 'the timeout gives nothing' ($null -eq $r.Value)
	Check 'the timeout line names the holder (timed out after 2 s ... pid=<holder>)' (@($r.Lines | Where-Object { $_ -match 'timed out after 2 s' -and $_ -match "pid=$($holder.Id) " }).Count -eq 1) ($r.Lines -join ' | ')
	Stop-Children
	$ciDefault = [regex]::Match((& $text 'dev\ci-smoke.ps1'), '\[int\]\$LockTimeoutSec = (\d+)')
	Check 'the CI wait limit is short: ci-smoke.ps1 -LockTimeoutSec defaults to at most 300 s' ($ciDefault.Success -and [int]$ciDefault.Groups[1].Value -le 300) $ciDefault.Value

	# --- Orphans --------------------------------------------------------------------------------------------------
	Write-Host '==> an orphan game (its launcher is gone) is stopped and logged'
	$orphan = Start-FakeGame
	[IO.File]::WriteAllText((Join-Path $lockDir 'game-1.lock'), "pid=999999 game=$($orphan.Id) since=2026-10-18 09:00:00 what=smoke-test.ps1 copy=E:\work\killed branch=ARXVR-9")
	$r = Invoke-Captured { Enter-ArxGameLock -What 'smoke-test.ps1' -Copy 'c' -Branch 'b' -TimeoutSec 5 }
	Check 'the orphan arx.exe is stopped within 5 s' (Wait-Until { $orphan.HasExited } 5)
	Check 'and a line says so: orphan, with its pid' (@($r.Lines | Where-Object { $_ -match 'orphan' -and $_ -match "\b$($orphan.Id)\b" }).Count -ge 1) ($r.Lines -join ' | ')
	Check 'the launch then gets the slot' ($null -ne $r.Value) $r.Error
	if ($r.Value) { Exit-ArxGameLock $r.Value }
	Stop-Children

	# What must never be stopped
	$notGame = Start-FakeGame $fakeOther
	[IO.File]::WriteAllText((Join-Path $lockDir 'game-1.lock'), "pid=999999 game=$($notGame.Id) since=2026-10-18 09:00:00 what=smoke-test.ps1 copy=E:\work\killed branch=ARXVR-9")
	$l = Enter-ArxGameLock -What 'smoke-test.ps1' -Copy 'c' -Branch 'b' -TimeoutSec 2 6>$null
	Start-Sleep -Seconds 1
	Check 'a stale lock naming a process that is not arx.exe (a reused pid): that process is not stopped' (Test-Alive $notGame)
	if ($l) { Exit-ArxGameLock $l }
	Stop-Children

	$liveGame = Start-FakeGame
	$liveHolder = Start-Child "`$l = Enter-ArxGameLock -What 'smoke-test.ps1' -Copy 'E:\work\live' -Branch 'ARXVR-8' -TimeoutSec 0; Set-ArxGameLockGame `$l $($liveGame.Id); Start-Sleep 120"
	$null = Wait-Until { @(Get-GameHolders | Where-Object { $_ -match "game=$($liveGame.Id) " }).Count -eq 1 }
	$l = Enter-ArxGameLock -What 'smoke-test.ps1' -Copy 'c' -Branch 'b' -TimeoutSec 2 6>$null
	Start-Sleep -Seconds 1
	Check 'the game of a live holder is not stopped and keeps its slot' ((Test-Alive $liveGame) -and $null -eq $l)
	if ($l) { Exit-ArxGameLock $l }
	Stop-Children

	$ownerGame = Start-FakeGame
	$null = Wait-Until { @(Get-ArxGamesOutsideLock | Where-Object Id -eq $ownerGame.Id).Count -eq 1 }
	$l = Enter-ArxGameLock -What 'smoke-test.ps1' -Copy 'c' -Branch 'b' -TimeoutSec 2 6>$null
	Start-Sleep -Seconds 1
	Check 'an arx.exe no lock names (the owner''s game) is never stopped, and a launch waits for it' ((Test-Alive $ownerGame) -and $null -eq $l)
	if ($l) { Exit-ArxGameLock $l }
	Stop-Children

	# --- A game dies with its launcher (#526) --------------------------------------------------------------------
	Write-Host '==> a game started through the launcher dies with it (#526)'
	$pidFile = Join-Path $root 'game-pid.txt'
	$readyFile = Join-Path $root 'launcher-ready.txt'
	$launcher = Start-Child ("`$l = Enter-ArxGameLock -What 'smoke-test.ps1' -Copy 'E:\work\launcher' -Branch 'ARXVR-7' -TimeoutSec 0; " +
		"`$p = Start-Process -FilePath '$fakeArx' -ArgumentList '-n','120','127.0.0.1' -PassThru -WindowStyle Hidden; " +
		"Set-Content -LiteralPath '$pidFile' -Value `$p.Id; Register-ArxGameProcess `$p; Set-ArxGameLockGame `$l `$p.Id; " +
		"Set-Content -LiteralPath '$readyFile' -Value 'ready'; Start-Sleep 120")
	Check 'the launcher started the stand-in game' (Wait-Until { (Test-Path $readyFile) -and (Test-Path $pidFile) })
	$game = $null
	if (Test-Path $pidFile) {
		$game = Get-Process -Id ([int](Get-Content $pidFile -Raw).Trim()) -ErrorAction SilentlyContinue
		if ($game) { $script:children += $game }
	}
	Start-Sleep -Seconds 1
	Check 'the game runs while its launcher runs' (Test-Alive $game)
	$errFile = Join-Path $root 'register-check.txt'
	$check = Start-Process $pwsh -PassThru -WindowStyle Hidden -Wait -ArgumentList @('-NoProfile', '-Command', ". '$libPath'; Set-Content -LiteralPath '$errFile' -Value ([bool](Get-Command Register-ArxGameProcess -ErrorAction SilentlyContinue))")
	Check 'launch-lib.ps1 has Register-ArxGameProcess' ((Get-Content $errFile -Raw).Trim() -eq 'True')
	Stop-Process -Id $launcher.Id -Force
	Check 'the launcher killed: the game is gone within 5 s' (Wait-Until { -not (Test-Alive $game) } 5)
	$script:freed = $null
	Check 'and the slot is free within 5 s' (Wait-Until { $script:freed = Enter-ArxGameLock -What 'next' -Copy 'c' -Branch 'b' -TimeoutSec 0 6>$null; [bool]$script:freed } 5)
	if ($script:freed) { Exit-ArxGameLock $script:freed }
	Stop-Children
	foreach ($s in 'dev\smoke-test.ps1', 'dev\make-test-saves.ps1') {
		$t = & $text $s
		$startAt = $t.IndexOf('Start-Process -FilePath')
		$registerAt = $t.IndexOf('Register-ArxGameProcess', [Math]::Max(0, $startAt))
		Check "$s registers the game it starts with its launcher (Register-ArxGameProcess after Start-Process)" ($startAt -ge 0 -and $registerAt -gt $startAt)
	}
} finally {
	Remove-Item Env:ARXVR_CI_RUNNER, Env:ARXVR_MAX_GAMES -ErrorAction SilentlyContinue
	foreach ($c in $script:children) { try { Stop-Process -Id $c.Id -Force -ErrorAction SilentlyContinue } catch { } }
	Start-Sleep -Milliseconds 300
	Remove-Item $root -Recurse -Force -ErrorAction SilentlyContinue
}

if ($script:failures) { Write-Host "test-ci-slots: $($script:failures) check(s) FAILED" -ForegroundColor Red; exit 1 }
Write-Host 'test-ci-slots: all checks passed' -ForegroundColor Green
exit 0
