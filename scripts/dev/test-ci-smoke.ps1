<#
.SYNOPSIS
	Self-test of the CI game checks' lanes (#522, scripts/dev/ci-smoke-lib.ps1): no game, no network, no lock of the PC.

.DESCRIPTION
	In scratch folders ($env:ARXVR_LOCK_DIR and a fake %LOCALAPPDATA%; the real locks, evidence and digests of the PC
	are not touched):
	  - the evidence kept on the runner: the last 10 run folders stay, older ones go, ci-durations.json stays
	  - the per-sequence user directories: a root per working copy, never shared by two copies
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
	  - the impact map (scripts\dev\xrs\impact.txt): the selection of sample change sets (core, a mapped path, two
	    rules, a changed .xrs and read-back, a path no line matches and a harness path: everything), the coverage check
	    red on a sequence without a line, a name that is no sequence, a syntax error and a glob listed twice - and green
	    on the shipped map
	  - the workflow: the evidence is uploaded only on failure, the CI lane count is one ARXVR_MAX_GAMES (1-5, #563) in
	    the game checks step and ci-smoke.ps1 gets no -Lanes of its own (Get-ArxCiLaneCount, read by ci-local), the script
	    self-tests start before the build and are joined after it, the suite input of a manual run, no labeled trigger,
	    the label full-ci read live by the Scope step and its suite passed to the game checks
	  - #527 (written before the implementation, tests first): @solo, fail fast, the risk order and the failure history.
	    The interface they assume, in ci-smoke-lib.ps1:
	      Invoke-ArxPhaseSchedule ... -StopOnFail
	                         a pull request's run: after the first result with Verdict FAIL (a run or a Gate's result) no
	                         phase starts any more; the phases running then finish and their results are returned. Without
	                         the switch (the full suite: workflow_dispatch suite=full, the merge gate, ci-local -Full)
	                         everything runs as before
	      a phase's Solo     ($true; #436's c7dce4453, ported): the phase starts only when no lane is busy, and nothing
	                         starts while it runs; while it is due and waits, the phases behind it wait too
	      Get-ArxPhaseOrder $Units $Changed $Durations -Affected <phase names> -RecentFailures <phase names>
	                         tiers: a unit with a changed file, then a unit one of whose phases is in -Affected (the impact
	                         map's selection for the changed files, as xrs-<name>), then one in -RecentFailures, then the
	                         rest; inside a tier by duration, longest first (unknown as the longest), ties in the serial
	                         order; a read-back stays right after its sequence. Both default to none (the old order)
	      Get-ArxPhaseHistoryPath <durations file>   -> ci-history.json in the same folder
	      Add-ArxPhaseHistory <path> <results> [-Keep <n>]
	                         appends one run (its phases' verdicts) and keeps the last -Keep runs (the default is the
	                         worker's); a missing or broken file starts a new history (a warning, no throw)
	      Get-ArxRecentFailures <path> <runs>
	                         the phase names with Verdict FAIL in the last <runs> runs (SKIPPED is no failure); a missing
	                         or broken file: none
	    and ci-smoke.ps1 passes -Solo:$seq.Solo to the sequence phases, -StopOnFail:<pr run> to the scheduler, -Affected and
	    -RecentFailures to Get-ArxPhaseOrder, and records every run with Add-ArxPhaseHistory beside its durations file.
	    xrs-lib.ps1's @solo directive is tested in test-xrs.ps1; the lock side of #527 in test-ci-slots.ps1.
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

	# --- The per-sequence user directories ------------------------------------------------------------------------
	Write-Host '==> the per-sequence user directories'
	$rootA = Get-ArxCiUserDirRoot 'E:\dev\arx-fatalis-vr\.claude\worktrees\ARXVR-522-1-feat-ci-lanes'
	$rootB = Get-ArxCiUserDirRoot 'E:\dev\arx-fatalis-vr\.claude\worktrees\ARXVR-522-2-feat-harness-latency'
	Check 'the root is ci-userdirs\<folder>-<hash> of the working copy' ($rootA -like (Join-Path $localBase 'ArxFatalisVR\ci-userdirs\arxvr-522-1-feat-ci-lanes-*')) $rootA
	Check 'two working copies never share the root' ($rootA -ne $rootB)
	Check 'the same copy always gets the same root' ($rootA -eq (Get-ArxCiUserDirRoot 'E:\dev\arx-fatalis-vr\.claude\worktrees\ARXVR-522-1-feat-ci-lanes'))

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

	# --- The risk order (#527) ----------------------------------------------------------------------------------------
	# changed .xrs -> what the impact map selects for the changed files (-Affected) -> what failed in recent runs
	# (-RecentFailures) -> the rest by duration, longest first. A unit is in a tier when one of its phases is named.
	Write-Host '==> the risk order (#527)'
	try {
		$riskUnits = @((Unit 'a'), (Unit 'b'), (Unit 'c'), (Unit 'd' -Readback), (Unit 'e'), (Unit 'f'), (Unit 'g'))
		$riskDurs = @{ 'xrs-a' = 10; 'xrs-b' = 30; 'xrs-c' = 5; 'xrs-d' = 15; 'xrs-d-readback' = 5; 'xrs-e' = 50; 'xrs-g' = 40 }
		$riskOrder = @(Get-ArxPhaseOrder $riskUnits @('scripts/dev/xrs/c.xrs', 'src/vr/VRMelee.cpp') $riskDurs -Affected @('xrs-a', 'xrs-c') -RecentFailures @('xrs-d-readback', 'xrs-a'))
		$riskWant = 'xrs-c,xrs-a,xrs-d,xrs-d-readback,xrs-f,xrs-e,xrs-g,xrs-b'
		Check 'risk order: changed, then map-affected, then recently failed, then the rest by duration (unknown as the longest)' (($riskOrder -join ',') -eq $riskWant) ($riskOrder -join ',')
		Check 'risk order: a changed sequence before a longer map-affected one (c 5 s before a 10 s)' ($riskOrder.IndexOf('xrs-c') -ge 0 -and $riskOrder.IndexOf('xrs-c') -lt $riskOrder.IndexOf('xrs-a'))
		Check 'risk order: a map-affected sequence before a longer recently failed one (a 10 s before d 20 s)' ($riskOrder.IndexOf('xrs-a') -ge 0 -and $riskOrder.IndexOf('xrs-a') -lt $riskOrder.IndexOf('xrs-d'))
		Check 'risk order: a recently failed sequence before longer untouched ones (d before f unknown and e 50 s)' ($riskOrder.IndexOf('xrs-d') -ge 0 -and $riskOrder.IndexOf('xrs-d') -lt $riskOrder.IndexOf('xrs-f') -and $riskOrder.IndexOf('xrs-d') -lt $riskOrder.IndexOf('xrs-e'))
		Check 'risk order: a failed read-back brings its sequence, the read-back right after it' ($riskOrder.IndexOf('xrs-d-readback') -eq $riskOrder.IndexOf('xrs-d') + 1)
		$affectedOnly = @(Get-ArxPhaseOrder $riskUnits @() $riskDurs -Affected @('xrs-b'))
		Check 'risk order: a map-affected sequence alone goes first' ($affectedOnly.Count -gt 0 -and $affectedOnly[0] -eq 'xrs-b') ($affectedOnly -join ',')
		$failedOnly = @(Get-ArxPhaseOrder $riskUnits @() $riskDurs -RecentFailures @('xrs-c'))
		Check 'risk order: a recently failed sequence alone goes first' ($failedOnly.Count -gt 0 -and $failedOnly[0] -eq 'xrs-c') ($failedOnly -join ',')
		Check 'risk order: every phase exactly once' ($riskOrder.Count -eq 8 -and @($riskOrder | Sort-Object -Unique).Count -eq 8)
	} catch { Check 'risk order: the cases ran without an error' $false "$_" }

	# The history of failures beside ci-durations.json (#527): one record per run, the last -Keep runs kept
	Write-Host '==> the failure history (#527)'
	try {
		$histDur = Join-Path $root 'hist\ci-durations.json'
		$histFile = Join-Path $root 'hist\ci-history.json'
		Check 'the history file is ci-history.json beside ci-durations.json' ((Get-ArxPhaseHistoryPath $histDur) -eq $histFile) "$(Get-ArxPhaseHistoryPath $histDur)"
		Check 'no history: no recent failures' (@(Get-ArxRecentFailures $histFile 5).Count -eq 0)
		function Run([string[]]$Fail = @(), [string[]]$Pass = @(), [string[]]$Skip = @()) {
			@($Fail | ForEach-Object { [pscustomobject]@{ Name = $_; Exit = 1; Verdict = 'FAIL'; Seconds = 1.0 } }) +
			@($Pass | ForEach-Object { [pscustomobject]@{ Name = $_; Exit = 0; Verdict = 'PASS'; Seconds = 1.0 } }) +
			@($Skip | ForEach-Object { [pscustomobject]@{ Name = $_; Exit = 2; Verdict = 'SKIPPED'; Seconds = 900.0 } })
		}
		Add-ArxPhaseHistory $histFile (Run -Fail 'xrs-old' -Pass 'xrs-x')
		Add-ArxPhaseHistory $histFile (Run -Fail 'xrs-x' -Pass 'xrs-y')
		Add-ArxPhaseHistory $histFile (Run -Pass 'xrs-x', 'xrs-y')
		Add-ArxPhaseHistory $histFile (Run -Pass 'xrs-x', 'xrs-y')
		Add-ArxPhaseHistory $histFile (Run -Pass 'xrs-x', 'xrs-y')
		Add-ArxPhaseHistory $histFile (Run -Fail 'xrs-z' -Pass 'xrs-x' -Skip 'xrs-s')
		$recent = @(Get-ArxRecentFailures $histFile 5 | Sort-Object)
		Check 'recent failures: the FAILs of the last 5 runs, not older ones, not a SKIPPED' (($recent -join ',') -eq 'xrs-x,xrs-z') ($recent -join ',')
		Check 'recent failures: the last run only' ((@(Get-ArxRecentFailures $histFile 1) -join ',') -eq 'xrs-z') (@(Get-ArxRecentFailures $histFile 1) -join ',')
		Add-ArxPhaseHistory $histFile (Run -Pass 'xrs-x') -Keep 3
		$kept = @(Get-ArxRecentFailures $histFile 100 | Sort-Object)
		Check 'the history keeps the last -Keep runs' (($kept -join ',') -eq 'xrs-z') ($kept -join ',')
		Set-Content $histFile '{ not json'
		Check 'a broken history: no recent failures, no throw' (@(Get-ArxRecentFailures $histFile 5 3>$null).Count -eq 0)
		Add-ArxPhaseHistory $histFile (Run -Fail 'xrs-new') 3>$null
		Check 'a run written over a broken history starts it again' ((@(Get-ArxRecentFailures $histFile 5) -join ',') -eq 'xrs-new')
		$null = Remove-ArxOldCiEvidence (Split-Path $histFile) 0
		Check 'the history is not a run folder: the evidence cleanup leaves it' (Test-Path $histFile)
	} catch { Check 'the failure history: the cases ran without an error' $false "$_" }

	# --- The changed files ------------------------------------------------------------------------------------------
	Write-Host '==> the changed files of a branch'
	$repo = Join-Path $root 'repo'
	New-Item -ItemType Directory -Force (Join-Path $repo 'scripts\dev\xrs') | Out-Null
	$git = { git -C $repo -c user.name=t -c user.email=t@example.com -c core.autocrlf=false @args 2>&1 | Out-Null }
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

	# A solo phase (#527, from #436's c7dce4453: a sequence that measures frame pacing) runs alone: it starts only with
	# empty lanes, nothing starts while it runs; the others wait behind it, and still all run once
	Write-Host '==> @solo: a phase that runs alone (#527)'
	try {
		$script:runs.Clear()
		$soloPhases = @((Fake 'a' 200), (Fake 'b' 200), (Fake 'solo' 300), (Fake 'c' 100), (Fake 'd' 100))
		$soloPhases[2] | Add-Member -NotePropertyName Solo -NotePropertyValue $true
		$soloRuns = @(Invoke-ArxPhaseSchedule -Phases $soloPhases -Lanes 2 -Start $start -Finish $finish -LaneLimit { 2 } -PollMs 20)
		$soloByName = @{}
		foreach ($r in $script:runs) { $soloByName[$r.Name] = $r }
		Check 'a solo phase runs alone: nothing overlaps it' ($soloByName.ContainsKey('solo') -and -not @($script:runs | Where-Object { $_.Name -ne 'solo' -and (Test-Overlap $_ $soloByName['solo']) })) (($script:runs | ForEach-Object { "$($_.Name)@$($_.Lane)" }) -join ',')
		Check 'a solo phase starts only after the running phases ended' ($soloByName['solo'].Start -ge $soloByName['a'].End -and $soloByName['solo'].Start -ge $soloByName['b'].End)
		Check 'nothing starts while a solo phase runs: the phases after it start when it ended' ($soloByName['c'].Start -ge $soloByName['solo'].End -and $soloByName['d'].Start -ge $soloByName['solo'].End)
		Check 'a solo phase: the others still run, once each' ($script:runs.Count -eq 5 -and @($soloRuns | Where-Object Verdict -eq 'PASS').Count -eq 5)
		Check 'a solo phase: the phases before it ran together (two lanes)' (Test-Overlap $soloByName['a'] $soloByName['b'])
		$script:runs.Clear()
		$soloLast = @((Fake 'a' 150), (Fake 'b' 150), (Fake 'c' 150), (Fake 'solo' 200))
		$soloLast[3] | Add-Member -NotePropertyName Solo -NotePropertyValue $true
		$null = @(Invoke-ArxPhaseSchedule -Phases $soloLast -Lanes 2 -Start $start -Finish $finish -LaneLimit { 2 } -PollMs 20)
		$lastByName = @{}
		foreach ($r in $script:runs) { $lastByName[$r.Name] = $r }
		Check 'a solo phase that is due waits for a lane that is still busy (c), it does not join it' ($lastByName.ContainsKey('solo') -and $lastByName['solo'].Start -ge $lastByName['c'].End)
	} catch { Check '@solo: the scheduler cases ran without an error' $false "$_" }

	# Fail fast (#527): a pull request's run (-StopOnFail) starts nothing after the first FAIL; the phases already running
	# finish with their own verdict. The full suite (no -StopOnFail) runs everything.
	Write-Host '==> fail fast on a pull request run (#527)'
	try {
		function New-FailFastPhases { @((Fake 'ff-fail' 150 1), (Fake 'ff-long' 900), (Fake 'ff-after1' 100), (Fake 'ff-after2' 100), (Fake 'ff-after3' 100)) }
		$script:runs.Clear()
		$ff = @(Invoke-ArxPhaseSchedule -Phases (New-FailFastPhases) -Lanes 2 -Start $start -Finish $finish -LaneLimit { 2 } -PollMs 20 -StopOnFail)
		$ffRuns = @($script:runs)
		$ffByName = @{}
		foreach ($r in $ffRuns) { $ffByName[$r.Name] = $r }
		Check 'fail fast: nothing starts after the first FAIL' ($ffByName.ContainsKey('ff-fail') -and -not @($ffRuns | Where-Object { $_.Start -ge $ffByName['ff-fail'].End })) (($ffRuns | ForEach-Object Name) -join ',')
		Check 'fail fast: only the failed phase and the one running beside it ran' ((@($ffRuns | ForEach-Object Name | Sort-Object) -join ',') -eq 'ff-fail,ff-long') (($ffRuns | ForEach-Object Name) -join ',')
		Check 'fail fast: the phase that was running finishes with its own verdict' (@($ff | Where-Object { $_.Name -eq 'ff-long' -and $_.Verdict -eq 'PASS' }).Count -eq 1)
		Check 'fail fast: the failure is in the results' (@($ff | Where-Object { $_.Name -eq 'ff-fail' -and $_.Verdict -eq 'FAIL' }).Count -eq 1)
		$script:runs.Clear()
		$ffOne = @(Invoke-ArxPhaseSchedule -Phases (New-FailFastPhases) -Lanes 1 -Start $start -Finish $finish -PollMs 20 -StopOnFail)
		Check 'fail fast on one lane: the first phase fails, nothing else runs' ((@($script:runs | ForEach-Object Name) -join ',') -eq 'ff-fail') (($script:runs | ForEach-Object Name) -join ',')
		$script:runs.Clear()
		$full = @(Invoke-ArxPhaseSchedule -Phases (New-FailFastPhases) -Lanes 2 -Start $start -Finish $finish -LaneLimit { 2 } -PollMs 20)
		Check 'the full suite (no -StopOnFail) runs every phase after a FAIL' ($script:runs.Count -eq 5 -and @($full | Where-Object Verdict -eq 'FAIL').Count -eq 1) (($script:runs | ForEach-Object Name) -join ',')
	} catch { Check 'fail fast: the scheduler cases ran without an error' $false "$_" }

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

	# --- The impact map ---------------------------------------------------------------------------------------------
	Write-Host '==> the impact map: the selection of a pull request'
	$mapFile = Join-Path $root 'impact.txt'
	Set-Content $mapFile -Encoding utf8 -Value @(
		'# a sample map'
		'core: smoke-test first   # the start'
		'core: look'
		'scripts/dev/smoke-test.ps1 -> *'
		'src/vr/VRMelee.* -> melee-swing melee-recovery'
		'src/vr/VRWeapon.* -> weapon-hold melee-swing'
		'src/gui/* -> hud'
		'src/core/CommandChannel.* -> core'
		'docs/* -> core'
		'*.md -> core'
	)
	$map = Read-ArxImpactMap $mapFile
	Check 'the map: core lines add up, comments are dropped' (($map.Core -join ',') -eq 'smoke-test,first,look') ($map.Core -join ',')
	Check 'the map: one rule per glob line, no errors' ($map.Rules.Count -eq 7 -and $map.Errors.Count -eq 0) "$($map.Rules.Count) rules, $($map.Errors -join '; ')"
	function Select-Names($Changed) { $s = Get-ArxImpactSelection $map $Changed; if ($s.Full) { 'FULL' } else { $s.Names -join ',' } }
	Check 'no change: the core alone' ((Select-Names @()) -eq 'first,look,smoke-test') (Select-Names @())
	Check 'docs and a hub file: the core alone, not everything' ((Select-Names @('docs/vr/TESTING.md', 'README.md', 'src/core/CommandChannel.cpp')) -eq 'first,look,smoke-test') (Select-Names @('docs/vr/TESTING.md', 'README.md', 'src/core/CommandChannel.cpp'))
	Check 'a mapped source file: the core and its sequences' ((Select-Names @('src/vr/VRMelee.cpp')) -eq 'first,look,melee-recovery,melee-swing,smoke-test') (Select-Names @('src/vr/VRMelee.cpp'))
	Check 'two rules: their names add up, once each' ((Select-Names @('src/vr/VRMelee.h', 'src/vr/VRWeapon.cpp')) -eq 'first,look,melee-recovery,melee-swing,smoke-test,weapon-hold') (Select-Names @('src/vr/VRMelee.h', 'src/vr/VRWeapon.cpp'))
	Check 'a glob crosses folders and ignores case; a backslash path is a path' ((Select-Names @('SRC\GUI\widget\Widget.cpp')) -eq 'first,hud,look,smoke-test') (Select-Names @('SRC\GUI\widget\Widget.cpp'))
	Check 'a changed or added .xrs selects itself, without a line' ((Select-Names @('scripts/dev/xrs/brand-new.xrs', 'docs/x.md')) -eq 'brand-new,first,look,smoke-test') (Select-Names @('scripts/dev/xrs/brand-new.xrs'))
	Check 'a changed read-back selects its sequence' ((Select-Names @('scripts/dev/xrs/readback/menu-readback.xrs')) -eq 'first,look,menu,smoke-test') (Select-Names @('scripts/dev/xrs/readback/menu-readback.xrs'))
	$sel = Get-ArxImpactSelection $map @('src/vr/VRMelee.cpp', 'src/brand/New.cpp')
	Check 'a path no line matches: everything, and it is named' ($sel.Full -and ($sel.Unmapped -join ',') -eq 'src/brand/New.cpp' -and $sel.FullBecause[0] -match 'no rule matches') "$($sel.FullBecause -join '; ')"
	$sel = Get-ArxImpactSelection $map @('scripts/dev/smoke-test.ps1')
	Check 'a harness path (-> *): everything, with the line that says so' ($sel.Full -and -not $sel.Unmapped -and $sel.FullBecause[0] -match 'line 4') "$($sel.FullBecause -join '; ')"
	Check 'a selection says why each check is in it' ($sel.Reasons['first'] -eq 'core' -and (Get-ArxImpactSelection $map @('src/vr/VRMelee.cpp')).Reasons['melee-swing'] -eq 'src/vr/VRMelee.cpp')
	Check 'Full still lists the partial selection for the log' ((Get-ArxImpactSelection $map @('src/vr/VRMelee.cpp', 'x/y')).Names -contains 'melee-swing')

	Write-Host '==> the impact map: the coverage check'
	$universe = @('first', 'look', 'melee-swing', 'melee-recovery', 'weapon-hold', 'hud')
	$problems = @(Test-ArxImpactMap $map $universe)
	Check 'a check in no line (spell-drawcheck) is a problem' ($problems.Count -eq 1 -and $problems[0] -match "'spell-drawcheck' is in no core: line") "$problems"
	$problems = @(Test-ArxImpactMap $map ($universe + 'orphan'))
	Check 'a sequence no glob reaches and not in core: red, named' (@($problems | Where-Object { $_ -match "'orphan' is in no core: line and named by no rule" }).Count -eq 1) "$problems"
	$problems = @(Test-ArxImpactMap $map @($universe | Where-Object { $_ -ne 'hud' }))
	Check 'a name that is no sequence: red, with its line' (@($problems | Where-Object { $_ -match "line 7: 'hud' is no sequence" }).Count -eq 1) "$problems"
	$bad = Join-Path $root 'impact-bad.txt'
	Set-Content $bad -Encoding utf8 -Value @('core: first', 'src/a/* -> first', 'src/A/* -> first', 'src/b/* ->', 'src/c/* -> * first', 'just words')
	$badMap = Read-ArxImpactMap $bad
	$problems = @(Test-ArxImpactMap $badMap @('first'))
	Check 'syntax errors are problems, never skipped: a line naming nothing, * with a name, no arrow' ($badMap.Errors.Count -eq 3 -and @($problems | Where-Object { $_ -match '^line (4|5|6):' }).Count -eq 3) "$problems"
	Check 'a glob listed twice (any case) is a problem' (@($problems | Where-Object { $_ -match 'listed twice' }).Count -eq 1) "$problems"
	$shippedDir = Join-Path $PSScriptRoot 'xrs'
	$shipped = Read-ArxImpactMap (Join-Path $shippedDir 'impact.txt')
	$shippedNames = @(Get-ArxSequenceNames $shippedDir)
	$problems = @(Test-ArxImpactMap $shipped $shippedNames)
	Check "the shipped map covers all $($shippedNames.Count) shipped sequences and the two checks" ($problems.Count -eq 0 -and $shippedNames.Count -gt 0) ($problems -join '; ')
	Check 'the shipped map has a core with both checks' ($shipped.Core -contains 'smoke-test' -and $shipped.Core -contains 'spell-drawcheck')
	$problems = @(Test-ArxImpactMap $shipped ($shippedNames + 'a-sequence-without-a-line'))
	Check 'the shipped map is red for a sequence it has no line for' (@($problems | Where-Object { $_ -match "'a-sequence-without-a-line'" }).Count -eq 1) ($problems -join '; ')
	$hub = Get-ArxImpactSelection $shipped @('src/core/CommandChannel.cpp', 'CMakeLists.txt', 'docs/vr/TESTING.md', 'tests/vr/X.cpp')
	Check 'the shipped map: hub files, docs and host tests select the core alone' (-not $hub.Full -and $hub.Names.Count -eq $shipped.Core.Count) "$($hub.FullBecause -join '; ')"
	Check 'the shipped map: the harness selects everything' ((Get-ArxImpactSelection $shipped @('scripts/dev/smoke-test.ps1')).Full -and (Get-ArxImpactSelection $shipped @('src/vr/VRSimulator.cpp')).Full)
	Check 'the shipped map: a new source folder selects everything' ((Get-ArxImpactSelection $shipped @('src/brandnew/Thing.cpp')).Full)
	Check 'the change set of a diff that cannot be made is not Ok (everything runs), an empty one is' (-not (Get-ArxChangedFileSet $repo 'origin/nowhere').Ok -and -not (Get-ArxChangedFileSet $repo '').Ok -and (Get-ArxChangedFileSet $repo 'feature').Ok -and @((Get-ArxChangedFileSet $repo 'feature').Files).Count -eq 0)

	# --- The workflow ---------------------------------------------------------------------------------------------------
	Write-Host '==> the workflow'
	$wf = Get-Content -Raw (Join-Path $PSScriptRoot '..\..\.github\workflows\build-windows.yml')
	$upload = [regex]::Match($wf, '(?s)- name: Upload game check evidence.*?(?=\r?\n    - name:)').Value
	Check 'the game check evidence is uploaded only when the job failed' ($upload -match '\n\s+if: failure\(\)' -and $upload -notmatch 'always\(\)') $upload
	$checks = [regex]::Match($wf, '(?s)- name: Game checks.*?(?=\r?\n    - name:)').Value
	# The lane count is one setting (#563): one ARXVR_MAX_GAMES in the workflow, in the game checks step, 1 to 5 (the
	# owner's cap, 2026-10-20 - more is his call); ci-smoke.ps1 takes it from the game lock, no -Lanes of its own
	$wfPath = Join-Path $PSScriptRoot '..\..\.github\workflows\build-windows.yml'
	$ciLanes = Get-ArxCiLaneCount $wfPath
	Check 'the workflow sets the CI lane count once: one ARXVR_MAX_GAMES of 1 to 5' ($ciLanes -ge 1 -and $ciLanes -le 5) "$ciLanes"
	Check 'the game checks run with that ARXVR_MAX_GAMES into the evidence folder of the run' ($checks -match "ARXVR_MAX_GAMES: '$ciLanes'" -and $checks -match 'ci-evidence') $checks
	Check 'the game checks pass ci-smoke.ps1 no lane count of their own (the lock''s is the one)' ($checks -match 'ci-smoke\.ps1' -and $checks -notmatch 'ci-smoke\.ps1[^\r\n]*-Lanes') $checks
	$wfScratch = Join-Path $root 'workflow-lanes.yml'
	Set-Content -LiteralPath $wfScratch -Value "      env:`n        ARXVR_MAX_GAMES: '2'`n        ARXVR_MAX_GAMES: '3'"
	Check 'a workflow with two ARXVR_MAX_GAMES has no CI lane count' ($null -eq (Get-ArxCiLaneCount $wfScratch))
	Set-Content -LiteralPath $wfScratch -Value "      env:`n        ARXVR_CI_RUNNER: '1'"
	Check 'a workflow without ARXVR_MAX_GAMES has no CI lane count, nor a missing file' ($null -eq (Get-ArxCiLaneCount $wfScratch) -and $null -eq (Get-ArxCiLaneCount (Join-Path $root 'nowhere.yml')))
	$startAt = $wf.IndexOf('- name: Script self-tests (start)')
	$buildAt = $wf.IndexOf('- name: Build')
	$joinAt = $wf.IndexOf('- name: Script self-tests (join)')
	Check 'the script self-tests start before the build and are joined after it' ($startAt -ge 0 -and $startAt -lt $buildAt -and $joinAt -gt $buildAt)
	# A background process that inherits the step's output pipes is killed with its tree when the step ends (TRAPS.md)
	$startStep = [regex]::Match($wf, '(?s)- name: Script self-tests \(start\).*?(?=\r?\n    - name:)').Value
	Check 'the background self-tests are started without -RedirectStandard* (no inherited pipes) and redirect their own output' ($startStep -match 'Start-Process pwsh' -and $startStep -notmatch '-RedirectStandard' -and $startStep -match '\*> ') $startStep
	# The suites (#522): a manual run picks one (full is the merge gate), the Scope step decides, the game checks run it
	$on = [regex]::Match($wf, '(?s)\non:.*?(?=\r?\nconcurrency:)').Value
	Check 'a manual run takes the input suite: full (the default) or pr' ($on -match '(?s)workflow_dispatch:\s+inputs:\s+suite:.*options: \[ full, pr \].*default: full') $on
	Check 'no labeled trigger (a skipped run over a red check of the same commit)' ($on -notmatch 'types:[^\n]*labeled') $on
	$scope = [regex]::Match($wf, '(?s)- name: Scope.*?(?=\r?\n    - name:)').Value
	Check 'the Scope step reads the label full-ci live and outputs the suite' ($scope -match '/labels' -and $scope -match "'full-ci'" -and $scope -match 'suite=\$suite' -and $scope -match "\`$suite = 'pr'") $scope
	Check 'the game checks run the Scope step''s suite against main' ($checks -match "-Suite '\$\{\{ steps\.scope\.outputs\.suite \}\}'" -and $checks -match '-Base origin/main') $checks

	# --- ci-smoke.ps1 uses them (#527) ----------------------------------------------------------------------------------
	Write-Host '==> ci-smoke.ps1: @solo, fail fast, the risk order (#527)'
	$ci = Get-Content -Raw (Join-Path $PSScriptRoot 'ci-smoke.ps1')
	Check 'ci-smoke.ps1 passes a sequence''s @solo to its phase' ($ci -match '-Solo:\$seq\.Solo') 'no -Solo:$seq.Solo'
	$scheduleCall = [regex]::Match($ci, 'Invoke-ArxPhaseSchedule [^\r\n]*').Value
	Check 'ci-smoke.ps1 schedules with -StopOnFail bound to the suite (a pull request''s run stops, the full suite does not)' ($scheduleCall -match '-StopOnFail:') $scheduleCall
	Check 'ci-smoke.ps1 orders by the risk tiers: the impact selection (-Affected) and the recent failures (-RecentFailures)' ($ci -match 'Get-ArxPhaseOrder [^\r\n]*-Affected' -and $ci -match 'Get-ArxPhaseOrder [^\r\n]*-RecentFailures' -and $ci -match 'Get-ArxRecentFailures')
	Check 'ci-smoke.ps1 records each run in the failure history beside the durations' ($ci -match 'Add-ArxPhaseHistory' -and $ci -match 'Get-ArxPhaseHistoryPath')
} finally {
	Get-Job -ErrorAction SilentlyContinue | Remove-Job -Force -ErrorAction SilentlyContinue
	Remove-Item $root -Recurse -Force -ErrorAction SilentlyContinue
}

if ($script:failures) { Write-Host "test-ci-smoke: $($script:failures) check(s) FAILED" -ForegroundColor Red; exit 1 }
Write-Host 'test-ci-smoke: all checks passed' -ForegroundColor Green
exit 0
