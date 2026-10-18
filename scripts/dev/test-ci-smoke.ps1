<#
.SYNOPSIS
	Self-test of the CI game checks' lanes (#522, scripts/dev/ci-smoke-lib.ps1): no game, no network, no lock of the PC.

.DESCRIPTION
	In scratch folders ($env:ARXVR_LOCK_DIR and a fake %LOCALAPPDATA%; the real locks, evidence and digests of the PC
	are not touched):
	  - the evidence kept on the runner: the last 10 run folders stay, older ones go, ci-durations.json stays
	  - the durations file: missing or broken is an empty table; a run writes the phases that ran over the old table
	  - the order: changed sequences first, then the last run's duration, longest first, a phase without a recorded
	    duration as the longest, ties in the serial order, a read-back right after its sequence
	  - the changed files of a branch against its base (a scratch git repository)
	  - the scheduler over fake phases (thread jobs that sleep and record their lane and time): never more than the
	    lane count at once, every phase exactly once, the save library before every phase that needs a save, two
	    phases of one exclusive key never together, a read-back after its sequence and dropped when it failed, a
	    sequence whose save is missing failed without a run, and the summary of two lanes equal to one lane's
	  - the lane count: a free lock folder gives 2, a slot held by this copy 2, a slot held by another copy 1, a game
	    outside the lock that runs from another folder 1, one from this copy's build 2
	  - the run digest of a phase picked by its game's pid (status.json, else launched.txt), not the newest folder
	  - the workflow: the evidence is uploaded only on failure, the game checks get ARXVR_MAX_GAMES=2, the script
	    self-tests start before the build and are joined after it
	Exit code 0 when everything passed, 1 otherwise.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Join-Path ([IO.Path]::GetTempPath()) "arxvr-ci-smoke-test-$PID"
$lockDir = Join-Path $root 'machine\locks'
$localBase = Join-Path $root 'local'
New-Item -ItemType Directory -Force $lockDir, $localBase | Out-Null
$env:ARXVR_LOCK_DIR = $lockDir
$env:LOCALAPPDATA = $localBase
Remove-Item Env:ARXVR_MAX_GAMES -ErrorAction SilentlyContinue
. (Join-Path $PSScriptRoot '..\launch-lib.ps1')
. (Join-Path $PSScriptRoot 'ci-smoke-lib.ps1')

$script:failures = 0
function Check([string]$Name, [bool]$Ok, [string]$Detail = '') {
	if ($Ok) { Write-Host "  [ok]   $Name" -ForegroundColor Green }
	else { Write-Host "  [FAIL] $Name$(if ($Detail) { " - $Detail" })" -ForegroundColor Red; $script:failures++ }
}

try {
	# --- The evidence kept on the runner ----------------------------------------------------------------------------
	Write-Host '==> the evidence kept on the runner'
	Check 'the evidence root is ci-evidence in the app data folder' ((Get-ArxCiEvidenceRoot) -eq (Join-Path $localBase 'ArxFatalisVR\ci-evidence'))
	$evidence = Join-Path $root 'ci-evidence'
	New-Item -ItemType Directory -Force $evidence | Out-Null
	Set-Content (Join-Path $evidence 'ci-durations.json') '{}'
	$t0 = (Get-Date).AddHours(-1)
	for ($i = 1; $i -le 12; $i++) {
		$d = New-Item -ItemType Directory -Force (Join-Path $evidence "39000$($i.ToString('00'))-1")
		$d.CreationTimeUtc = $t0.AddMinutes($i).ToUniversalTime()
	}
	$removed = @(Remove-ArxOldCiEvidence $evidence 10)
	$left = @(Get-ChildItem $evidence -Directory | ForEach-Object Name | Sort-Object)
	Check 'twelve runs, keep 10: the two oldest are removed' (($removed | Sort-Object) -join ',' -eq '3900001-1,3900002-1') "$removed"
	Check 'the ten newest stay' ($left.Count -eq 10 -and $left[0] -eq '3900003-1' -and $left[-1] -eq '3900012-1') "$left"
	Check 'ci-durations.json is not a run and stays' (Test-Path (Join-Path $evidence 'ci-durations.json'))
	Check 'ten runs, keep 10: nothing is removed' (@(Remove-ArxOldCiEvidence $evidence 10).Count -eq 0 -and @(Get-ChildItem $evidence -Directory).Count -eq 10)
	Check 'a missing root removes nothing' (@(Remove-ArxOldCiEvidence (Join-Path $root 'nowhere') 10).Count -eq 0)

	# --- The durations file -----------------------------------------------------------------------------------------
	Write-Host '==> the durations file'
	$durFile = Join-Path $root 'dur\ci-durations.json'
	Check 'no file: an empty table' ((Read-ArxPhaseDurations $durFile).Count -eq 0)
	$old = @{ 'xrs-a' = 1.0; 'xrs-gone' = 9.0; 'xrs-notrun' = 7.0; 'xrs-skipped' = 5.0 }
	$ran = @(
		[pscustomobject]@{ Name = 'xrs-a'; Exit = 0; Verdict = 'PASS'; Seconds = 12.34 }
		[pscustomobject]@{ Name = 'xrs-b'; Exit = 1; Verdict = 'FAIL'; Seconds = 4.0 }
		[pscustomobject]@{ Name = 'xrs-skipped'; Exit = 2; Verdict = 'SKIPPED'; Seconds = 900.0 }
		[pscustomobject]@{ Name = 'xrs-notrun'; Exit = 0; Verdict = 'NOT RUN' }
		[pscustomobject]@{ Name = 'xrs-nosave'; Exit = 1; Verdict = 'FAIL' }
	)
	Write-ArxPhaseDurations $durFile $old $ran
	$read = Read-ArxPhaseDurations $durFile
	Check 'a phase that ran gets its new time' ($read['xrs-a'] -eq 12.3 -and $read['xrs-b'] -eq 4.0) "$($read | ConvertTo-Json -Compress)"
	Check 'a phase that did not run keeps its old time (not run, skipped, absent)' ($read['xrs-notrun'] -eq 7.0 -and $read['xrs-skipped'] -eq 5.0 -and $read['xrs-gone'] -eq 9.0)
	Check 'a failure without a run writes no time' (-not $read.ContainsKey('xrs-nosave'))
	Set-Content $durFile '{ not json'
	Check 'a broken file: an empty table' ((Read-ArxPhaseDurations $durFile 3>$null).Count -eq 0)

	# --- The order ----------------------------------------------------------------------------------------------------
	Write-Host '==> the order of the sequences'
	function Unit([string]$Name, [switch]$Readback) {
		$u = [pscustomobject]@{ Phases = @("xrs-$Name"); Paths = @("scripts\dev\xrs\$Name.xrs") }
		if ($Readback) { $u.Phases += "xrs-$Name-readback"; $u.Paths += "scripts\dev\xrs\readback\$Name-readback.xrs" }
		$u
	}
	$units = @((Unit 'a'), (Unit 'b'), (Unit 'c'), (Unit 'c2'), (Unit 'd' -Readback), (Unit 'e'), (Unit 'g' -Readback), (Unit 'h'))
	$durs = @{ 'xrs-a' = 10; 'xrs-b' = 30; 'xrs-d' = 20; 'xrs-d-readback' = 5; 'xrs-e' = 5; 'xrs-g' = 40; 'xrs-g-readback' = 2 }
	$changed = @('scripts/dev/xrs/e.xrs', 'scripts/dev/xrs/readback/g-readback.xrs', 'scripts/dev/xrs/h.xrs', 'src/core/Other.cpp')
	$order = @(Get-ArxPhaseOrder $units $changed $durs)
	$want = 'xrs-h,xrs-g,xrs-g-readback,xrs-e,xrs-c,xrs-c2,xrs-b,xrs-d,xrs-d-readback,xrs-a'
	Check 'changed first (unknown as the longest), then duration desc, unknown as the longest, ties serial, read-back after its sequence' (($order -join ',') -eq $want) ($order -join ',')
	Check 'a changed read-back makes its sequence changed' ($order.IndexOf('xrs-g') -lt $order.IndexOf('xrs-c'))
	Check 'a unit is its sequence plus its read-back (d: 20+5 = 25 before a: 10, after b: 30)' ($order.IndexOf('xrs-b') -lt $order.IndexOf('xrs-d') -and $order.IndexOf('xrs-d') -lt $order.IndexOf('xrs-a'))
	$plain = @(Get-ArxPhaseOrder $units @() @{})
	Check 'no durations and no change: the serial order' (($plain -join ',') -eq 'xrs-a,xrs-b,xrs-c,xrs-c2,xrs-d,xrs-d-readback,xrs-e,xrs-g,xrs-g-readback,xrs-h') ($plain -join ',')
	Check 'every phase exactly once' ($order.Count -eq 10 -and @($order | Sort-Object -Unique).Count -eq 10)
	Check 'no units: nothing' (@(Get-ArxPhaseOrder @() @() @{}).Count -eq 0)

	# --- The changed files ------------------------------------------------------------------------------------------
	Write-Host '==> the changed files of a branch'
	$repo = Join-Path $root 'repo'
	New-Item -ItemType Directory -Force (Join-Path $repo 'scripts\dev\xrs') | Out-Null
	$git = { git -C $repo -c user.name=t -c user.email=t@example.invalid -c core.autocrlf=false @args 2>&1 | Out-Null }
	& $git init -q -b main
	Set-Content (Join-Path $repo 'scripts\dev\xrs\a.xrs') 'mark a'
	Set-Content (Join-Path $repo 'scripts\dev\xrs\b.xrs') 'mark b'
	& $git add -A
	& $git commit -q -m base
	& $git checkout -q -b feature
	Set-Content (Join-Path $repo 'scripts\dev\xrs\b.xrs') 'mark b2'
	Set-Content (Join-Path $repo 'scripts\dev\xrs\n.xrs') 'mark n'
	& $git add -A
	& $git commit -q -m change
	$files = @(Get-ArxChangedFiles $repo 'main')
	Check 'git diff base...HEAD: the changed and the added .xrs' ((($files | Sort-Object) -join ',') -eq 'scripts/dev/xrs/b.xrs,scripts/dev/xrs/n.xrs') "$files"
	Check 'no base: nothing changed' (@(Get-ArxChangedFiles $repo '').Count -eq 0)
	Check 'an unknown base: nothing changed, no throw' (@(Get-ArxChangedFiles $repo 'origin/nowhere' 3>$null).Count -eq 0)

	# --- The scheduler --------------------------------------------------------------------------------------------------
	Write-Host '==> the scheduler over fake phases'
	$fakeJob = {
		param([string]$Name, [int]$Lane, [int]$Ms, [int]$Exit)
		$s = [DateTime]::UtcNow
		Start-Sleep -Milliseconds $Ms
		[pscustomobject]@{ Name = $Name; Lane = $Lane; Start = $s; End = [DateTime]::UtcNow; Exit = $Exit }
	}
	$script:runs = [Collections.Generic.List[object]]::new()
	$start = { param($Phase, $Lane) Start-ThreadJob -ScriptBlock $fakeJob -ArgumentList $Phase.Name, $Lane, $Phase.Ms, $Phase.FakeExit }
	$finish = {
		param($Phase, $Lane, $Job, $Seconds)
		$r = Receive-Job $Job -Wait
		Remove-Job $Job -Force
		$script:runs.Add($r)
		[pscustomobject]@{ Name = $Phase.Name; Exit = $r.Exit; Verdict = $(if ($r.Exit -eq 0) { 'PASS' } else { 'FAIL' }) }
	}
	$saveGate = { param($Done, $Phase) $l = $Done['save-library']; if ($l -and $l.Exit -ne 0) { [pscustomobject]@{ Name = $Phase.Name; Exit = 1; Verdict = 'FAIL' } } }
	$readbackGate = { param($Done, $Phase) $f = $Done[$Phase.DependsOn[0]]; if ($f -and $f.Exit -eq 0) { $null } else { 'omit' } }
	function Fake([string]$Name, [int]$Ms, [int]$Exit = 0, [string[]]$DependsOn = @(), [string]$Exclusive, $Gate) {
		[pscustomobject]@{ Name = $Name; Ms = $Ms; FakeExit = $Exit; DependsOn = $DependsOn; Exclusive = $(if ($Exclusive) { $Exclusive }); Gate = $Gate }
	}
	function New-FakePhases([int]$LibraryExit = 0) {
		@(
			(Fake 'save-library' 500 $LibraryExit)
			(Fake 'smoke-test' 300 -Exclusive 'copy-user-dir')
			(Fake 'spell-drawcheck' 300 -Exclusive 'copy-user-dir')
			(Fake 'xrs-long' 900)
			(Fake 'xrs-saved' 200 -DependsOn 'save-library' -Gate $saveGate)
			(Fake 'xrs-first' 300)
			(Fake 'xrs-first-readback' 200 -DependsOn 'xrs-first' -Gate $readbackGate)
			(Fake 'xrs-broken' 150 1)
			(Fake 'xrs-broken-readback' 150 -DependsOn 'xrs-broken' -Gate $readbackGate)
			(Fake 'xrs-saved2' 250 -DependsOn 'save-library' -Gate $saveGate)
			(Fake 'xrs-short' 100)
		)
	}
	$serialIndex = @{}
	$i = 0
	foreach ($n in 'smoke-test', 'spell-drawcheck', 'save-library', 'xrs-broken', 'xrs-broken-readback', 'xrs-first', 'xrs-first-readback', 'xrs-long', 'xrs-saved', 'xrs-saved2', 'xrs-short') { $serialIndex[$n] = $i++ }
	function Get-Summary($Results) { @($Results | Sort-Object { $serialIndex[$_.Name] } | ForEach-Object { "$($_.Verdict) $($_.Name)" }) -join '; ' }
	function Get-MaxOverlap($Runs) {
		$max = 0
		foreach ($r in $Runs) { $max = [Math]::Max($max, @($Runs | Where-Object { $_.Start -le $r.Start -and $_.End -gt $r.Start }).Count) }
		$max
	}
	function Test-Overlap($A, $B) { $A -and $B -and $A.Start -lt $B.End -and $B.Start -lt $A.End }

	$script:runs.Clear()
	$serial = @(Invoke-ArxPhaseSchedule -Phases (New-FakePhases) -Lanes 1 -Start $start -Finish $finish -PollMs 20)
	$serialSummary = Get-Summary $serial
	Check 'one lane: never two at once' ((Get-MaxOverlap $script:runs) -eq 1)

	$script:runs.Clear()
	$two = @(Invoke-ArxPhaseSchedule -Phases (New-FakePhases) -Lanes 2 -Start $start -Finish $finish -LaneLimit { 2 } -PollMs 20)
	$runs2 = @($script:runs)
	$byName = @{}
	foreach ($r in $runs2) { $byName[$r.Name] = $r }
	Check 'two lanes: never more than two at once' ((Get-MaxOverlap $runs2) -le 2) "max $(Get-MaxOverlap $runs2)"
	Check 'two lanes: two do run at once' ((Get-MaxOverlap $runs2) -eq 2)
	Check 'every phase that runs runs exactly once' ($runs2.Count -eq 10 -and @($runs2 | ForEach-Object Name | Sort-Object -Unique).Count -eq 10) (($runs2 | ForEach-Object Name) -join ',')
	Check 'the lanes are 1 and 2' (-not @($two | Where-Object { $_.Lane -notin 1, 2 }) -and @($two | ForEach-Object Lane | Sort-Object -Unique).Count -eq 2)
	Check 'the save library ends before a phase that needs a save starts' ($byName['xrs-saved'].Start -ge $byName['save-library'].End -and $byName['xrs-saved2'].Start -ge $byName['save-library'].End)
	Check 'smoke-test and spell-drawcheck (one exclusive key) never together' (-not (Test-Overlap $byName['smoke-test'] $byName['spell-drawcheck']))
	Check 'a read-back starts after its sequence ended' ($byName['xrs-first-readback'].Start -ge $byName['xrs-first'].End)
	Check 'the read-back of a failed sequence is dropped (no run, no line)' (-not $byName.ContainsKey('xrs-broken-readback') -and -not @($two | Where-Object Name -eq 'xrs-broken-readback'))
	Check 'the longest phase went first' ($byName['xrs-long'].Start -lt $byName['xrs-short'].Start)
	Check 'each result carries its lane, start and seconds' (-not @($two | Where-Object { $null -eq $_.Seconds -or $null -eq $_.Started }))
	Check 'the summary of two lanes equals one lane''s' ((Get-Summary $two) -eq $serialSummary) "$(Get-Summary $two) <> $serialSummary"
	Check 'the summary has one line per phase (the dropped read-back has none)' (@($two).Count -eq 10)

	$script:runs.Clear()
	$asked = 0
	$limited = @(Invoke-ArxPhaseSchedule -Phases (New-FakePhases) -Lanes 2 -Start $start -Finish $finish -LaneLimit { $script:asked++; 1 } -PollMs 20 -LimitCacheMs 0)
	Check 'a lane limit of 1 (another game holds a slot): never two at once' ((Get-MaxOverlap @($script:runs)) -eq 1)
	Check 'the limit is asked before a second lane is used' ($script:asked -gt 0)
	Check 'one lane by the limit: the same summary' ((Get-Summary $limited) -eq $serialSummary)

	$script:runs.Clear()
	$noLib = @(Invoke-ArxPhaseSchedule -Phases (New-FakePhases -LibraryExit 1) -Lanes 2 -Start $start -Finish $finish -PollMs 20)
	$ranNoLib = @($script:runs | ForEach-Object Name)
	Check 'the library failed: a sequence that needs a save fails without a run' ('xrs-saved' -notin $ranNoLib -and @($noLib | Where-Object { $_.Name -eq 'xrs-saved' -and $_.Verdict -eq 'FAIL' }).Count -eq 1)
	Check 'the library failed: the other sequences still run' ('xrs-long' -in $ranNoLib -and 'xrs-short' -in $ranNoLib)

	$cycle = @((Fake 'a' 10 -DependsOn 'b'), (Fake 'b' 10 -DependsOn 'a'))
	$threw = $false
	try { $null = Invoke-ArxPhaseSchedule -Phases $cycle -Lanes 2 -Start $start -Finish $finish -PollMs 20 } catch { $threw = $_ -match 'wait for each other' }
	Check 'phases that wait for each other: a clear error, no endless wait' $threw
	$script:runs.Clear()
	$outside = @(Invoke-ArxPhaseSchedule -Phases @((Fake 'x' 10 -DependsOn 'save-library')) -Lanes 2 -Start $start -Finish $finish -PollMs 20)
	Check 'a dependency that is not scheduled counts as done' ($outside.Count -eq 1 -and $outside[0].Verdict -eq 'PASS')

	# --- The lane count -----------------------------------------------------------------------------------------------
	Write-Host '==> the lane count from the game lock'
	$copy = 'C:\work\arx copy'
	$build = 'C:\build\arx-copy\Release'
	$script:fakeOutside = @()
	function Get-ArxGamesOutsideLock { $script:fakeOutside }
	Check 'a free lock folder: 2 lanes' ((Get-ArxLaneCount 2 $copy $build) -eq 2)
	$ours = Open-ArxLockFile (Join-Path $lockDir 'game-1.lock')
	Write-ArxLockFile $ours "pid=$PID game=- since=2026-10-18 10:00:00 what=smoke-test.ps1 copy=$copy branch=ARXVR-522"
	Check 'a slot held by this copy (the other lane): 2 lanes' ((Get-ArxLaneCount 2 $copy $build) -eq 2) "$(Get-ArxLockHolders)"
	$theirs = Open-ArxLockFile (Join-Path $lockDir 'game-2.lock')
	Write-ArxLockFile $theirs "pid=$PID game=4242 since=2026-10-18 10:00:00 what=run-windows.ps1 -Vr copy=E:\dev\other copy branch=main"
	Check 'a slot held by another copy: 1 lane' ((Get-ArxLaneCount 2 $copy $build) -eq 1)
	$foreign = @(Get-ArxForeignGames $copy $build)
	Check 'the foreign game is named' ($foreign.Count -eq 1 -and $foreign[0] -match '^game-2\.lock: .*copy=E:\\dev\\other copy') "$foreign"
	Close-ArxLockFiles @($theirs)
	Check 'the other copy is done: 2 lanes again' ((Get-ArxLaneCount 2 $copy $build) -eq 2)
	$script:fakeOutside = @([pscustomobject]@{ Id = 7; Path = "$build\arx.exe" })
	Check 'a game outside the lock from this copy''s build (just started, pid not recorded yet): 2 lanes' ((Get-ArxLaneCount 2 $copy $build) -eq 2)
	$script:fakeOutside = @([pscustomobject]@{ Id = 8; Path = 'D:\games\Arx\arx.exe' })
	Check 'a game outside the lock from another folder: 1 lane' ((Get-ArxLaneCount 2 $copy $build) -eq 1)
	$script:fakeOutside = @([pscustomobject]@{ Id = 9; Path = '' })
	Check 'a game of another account (no path visible): 1 lane' ((Get-ArxLaneCount 2 $copy $build) -eq 1)
	$script:fakeOutside = @()
	Close-ArxLockFiles @($ours)
	Check 'one lane asked for: 1' ((Get-ArxLaneCount 1 $copy $build) -eq 1)

	# --- The digest of a phase ----------------------------------------------------------------------------------------
	Write-Host '==> the run digest of a phase, by pid'
	$runsDir = Join-Path $root 'runs'
	foreach ($n in '20261018-090000-pid111', '20261018-100000-pid111', '20261018-100500-pid222', '20261018-101000-pid1111') { New-Item -ItemType Directory -Force (Join-Path $runsDir $n) | Out-Null }
	Check 'the digest of pid 111 (its newest), not the newest folder' ((Find-ArxRunDigest $runsDir 111).Name -eq '20261018-100000-pid111')
	Check 'pid 11 matches no folder of pid 111 / 1111' ($null -eq (Find-ArxRunDigest $runsDir 11))
	Check 'no pid: no digest' ($null -eq (Find-ArxRunDigest $runsDir $null))
	$chan = Join-Path $root 'channel'
	New-Item -ItemType Directory -Force $chan | Out-Null
	Check 'no status, no marker: no pid' ($null -eq (Get-ArxPhaseGamePid $chan))
	Set-Content (Join-Path $chan 'launched.txt') 'pid=444 run=1'
	Check 'the launch marker''s pid' ((Get-ArxPhaseGamePid $chan) -eq 444)
	Set-Content (Join-Path $chan 'status.json') '{ "state": "EXITED", "pid": 222 }'
	Check 'status.json''s pid first' ((Get-ArxPhaseGamePid $chan) -eq 222)

	# --- The workflow ---------------------------------------------------------------------------------------------------
	Write-Host '==> the workflow'
	$wf = Get-Content -Raw (Join-Path $PSScriptRoot '..\..\.github\workflows\build-windows.yml')
	$upload = [regex]::Match($wf, '(?s)- name: Upload game check evidence.*?(?=\r?\n    - name:)').Value
	Check 'the game check evidence is uploaded only when the job failed' ($upload -match '\n\s+if: failure\(\)' -and $upload -notmatch 'always\(\)') $upload
	$checks = [regex]::Match($wf, '(?s)- name: Game checks.*?(?=\r?\n    - name:)').Value
	Check 'the game checks run with ARXVR_MAX_GAMES=2 into the evidence folder of the run' ($checks -match "ARXVR_MAX_GAMES: '2'" -and $checks -match 'ci-evidence') $checks
	$startAt = $wf.IndexOf('- name: Script self-tests (start)')
	$buildAt = $wf.IndexOf('- name: Build')
	$joinAt = $wf.IndexOf('- name: Script self-tests (join)')
	Check 'the script self-tests start before the build and are joined after it' ($startAt -ge 0 -and $startAt -lt $buildAt -and $joinAt -gt $buildAt)
} finally {
	Get-Job -ErrorAction SilentlyContinue | Remove-Job -Force -ErrorAction SilentlyContinue
	Remove-Item $root -Recurse -Force -ErrorAction SilentlyContinue
}

if ($script:failures) { Write-Host "test-ci-smoke: $($script:failures) check(s) FAILED" -ForegroundColor Red; exit 1 }
Write-Host 'test-ci-smoke: all checks passed' -ForegroundColor Green
exit 0
