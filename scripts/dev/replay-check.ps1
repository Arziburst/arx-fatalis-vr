<#
.SYNOPSIS
	Record and replay check (#550): record VR sessions on the simulated headset, replay them in fresh runs, compare.

.DESCRIPTION
	A dedicated check, not a CI sequence (coordinator Tests and CI rule 11: a recording and its replay are two launches,
	judged after the game quit). Run it by hand before the PR; every launch goes through smoke-test.ps1 / xrs-run.ps1 /
	replay.ps1 (D-014: the game lock, the fresh build, the launch ledger), this working copy's isolated user directory.
	It keeps the clips it compares in %LOCALAPPDATA%\ArxFatalisVR\replay-check\<working copy>\ (never in the repository).

	The interface it checks (the formats: tests/vr/ReplayClipTest.h, tests/vr/ReplayRingTest.h, scripts/dev/test-replay.ps1):
	  - <user dir>\arx-replay.log.gz after a VR session (no plain arx-replay.log left), header "# build: ", "# save: <save
	    folder> <name>" (the save made at the record start, in <user dir>\save), "# seed: " (--seed), "# rate: "
	  - arx.log "[replay] recording arx-replay.log: save <folder> "<name>", seed <n>, <hz> Hz"
	  - `replay mark <name> [<seconds>]` -> <user dir>\replay-marks\<utc>-<name>.vrsim, "# mark: <name> <ms>"; refused with
	    a name that is not letters, digits, - and _, and when nothing records (--no-replay-log, flat)
	  - status.json vr.sim.play {file, positionMs, framesServed, done, anchorsWaiting, drift {current, max}, eventsReplayed}
	  - the simulator's `play <file> [from <ms>] [to <ms>]` (a --vr-sim script line: from the timeline's frame 0)
	  - replay.ps1 <clip>: prints "Drift: current <m> m, max <m> m" and "Input events (<n>):" with one indented line each

	Cases (-Case, several allowed; 'all' = record, replay, anchor, cost, flat: 7 launches):
	  record   (1 launch) manual\replay-record.xrs with --seed 4242: the clip at the session's end, its header, the save,
	           at least 30 s of frames, <= 2 MB per minute uncompressed (and the compressed size, printed), the record
	           line in arx.log, input events in arx.log; the mark's clip: the last 30 s - its first frame >= 29.5 s before
	           the mark, its last within 100 ms of it; no user path or name in the clips. Keeps the clip, its arx.log and
	           its save for 'replay'.
	  replay   (1 launch) replay.ps1 on the recorded clip: exit 0; the drift printed; the input events of the replay's run
	           are the recording's, in the same order, and so is the list it prints; the replay's own clip has the
	           recording's head, grips and curls at equal display times (the recorded frames interpolated at the
	           replay's frame times): within 1 mm, 0.1 degree and 0.01, over at least 90 % of the recorded frames.
	  anchor   (2 launches) manual\replay-anchor-record.xrs: the clip has "anchor level 11"; its replay (a --vr-sim script
	           that plays it) holds at the anchor - anchorsWaiting 1, positionMs still - until the game plays level 11:
	           by itself within 10 s, else this check sends the recording's `loadlevel 11`; then anchorsWaiting 0,
	           done, area 11.
	  cost     (2 launches) two 30 s idle runs on the simulated headset, recording and --no-replay-log: the median busy
	           time per frame (mean - limit - vrwait of the perf log's seconds) may rise by less than 0.05 ms and the
	           99th percentile of the seconds' p95 busy time by less than 0.2 ms; the --no-replay-log run writes no clip
	           and refuses `replay mark`; in the recording run `replay mark bad/name` is refused and `replay mark short 5`
	           cuts 5 s. Prints the numbers for the PR body.
	  flat     (1 launch) a --no-vr run writes no clip, logs no record start and refuses `replay mark`.

	Exit code 0 on PASS, 1 on FAIL (also when replay.ps1 or clip-to-xrs.ps1 is missing: before any launch), 2 when a launch
	was skipped (the game lock stayed busy).

.PARAMETER Case
	The cases to run (default all). 'replay' needs a clip of 'record' (kept in the check folder from an earlier run).

.PARAMETER Rebuild
	Build this working copy first when its build is stale.

.EXAMPLE
	.\scripts\dev\replay-check.ps1
.EXAMPLE
	.\scripts\dev\replay-check.ps1 -Case record, replay
#>
[CmdletBinding()]
param(
	[ValidateSet('all', 'record', 'replay', 'anchor', 'cost', 'flat')]
	[string[]]$Case = @('all'),
	[switch]$Rebuild
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8
. (Join-Path $PSScriptRoot 'test-lib.ps1')
. (Join-Path $PSScriptRoot 'perf-summary-lib.ps1')

$replay = Join-Path $PSScriptRoot 'replay.ps1'
$converter = Join-Path $PSScriptRoot 'clip-to-xrs.ps1'
$xrsRun = Join-Path $PSScriptRoot 'xrs-run.ps1'
$smoke = Join-Path $PSScriptRoot 'smoke-test.ps1'
$manual = Join-Path $PSScriptRoot 'xrs\manual'
$pwsh = (Get-Process -Id $PID).Path
$inv = [Globalization.CultureInfo]::InvariantCulture

$cases = if ($Case -contains 'all') { @('record', 'replay', 'anchor', 'cost', 'flat') } else { @($Case) }

# Before any launch: the tools of the feature
foreach ($tool in $replay, $converter) {
	if (-not (Test-Path -LiteralPath $tool)) {
		Write-Host "replay check: FAIL before any launch - scripts/dev/$(Split-Path -Leaf $tool) does not exist (#550)" -ForegroundColor Red
		exit 1
	}
}

$userDir = Get-ArxUserDir
$testDir = Get-ArxTestDir
$work = Join-Path (Get-ArxAppDataDir) "replay-check\$(Get-WorkingCopyKey $ArxSourceDir)"
New-Item -ItemType Directory -Force $work | Out-Null
$perfLog = Join-Path $userDir 'arx-perf.log'
$logPath = Get-ArxLogPath $userDir
$clipPlain = Join-Path $userDir 'arx-replay.log'
$clipGz = Join-Path $userDir 'arx-replay.log.gz'
$marksDir = Join-Path $userDir 'replay-marks'

$script:failed = 0
$script:passed = 0
$script:skipped = $false
$script:rebuilt = $false
function Check([bool]$Ok, [string]$What) {
	if ($Ok) { Write-Host "  [ok]   $What" -ForegroundColor Green; $script:passed++ }
	else { Write-Host "  [FAIL] $What" -ForegroundColor Red; $script:failed++ }
}

# --- Personal data (as flight-recorder-check.ps1) -----------------------------------------------------------------------

$script:PrivateRules = @(@{ Kind = 'a user path'; Rx = [regex]'(?i)\b[A-Za-z]:[\\/]+(?:Users|Documents and Settings)\b' })
$generic = 'SYSTEM', 'Administrator', 'Admin', 'User', 'Public', 'Default', 'Guest', 'WORKGROUP'
foreach ($pair in @(@('the account name', $env:USERNAME), @('the machine name', $env:COMPUTERNAME))) {
	$name = "$($pair[1])".TrimEnd('$')
	if ($name.Length -ge 4 -and $name -notin $generic) {
		$script:PrivateRules += @{ Kind = $pair[0]; Rx = [regex]::new("(?i)(?<![A-Za-z0-9_-])$([regex]::Escape($name))(?![A-Za-z0-9_-])") }
	}
}

function Find-ArxPrivateText([string]$Text) {
	foreach ($rule in $script:PrivateRules) { if ($rule.Rx.IsMatch($Text)) { $rule.Kind } }
}

function Hide-ArxPrivateText([string]$Text) {
	foreach ($rule in $script:PrivateRules) { $Text = $rule.Rx.Replace($Text, '<private>') }
	$Text
}

# --- Clips -------------------------------------------------------------------------------------------------------------

function Read-ArxClipText([string]$Path) {
	if ($Path -like '*.gz') {
		$file = [IO.File]::OpenRead($Path)
		try {
			$gz = [IO.Compression.GZipStream]::new($file, [IO.Compression.CompressionMode]::Decompress)
			$reader = [IO.StreamReader]::new($gz)
			try { return $reader.ReadToEnd() } finally { $reader.Dispose() }
		} finally { $file.Dispose() }
	}
	$stream = [IO.FileStream]::new($Path, 'Open', 'Read', 'ReadWrite, Delete')
	try { ([IO.StreamReader]::new($stream)).ReadToEnd() } finally { $stream.Dispose() }
}

function Get-ArxClipHeader([string[]]$Lines) {
	$header = @{}
	foreach ($line in $Lines) {
		if ($line.StartsWith('@')) { break }
		if ($line -match '^# (build|save|seed|rate|mark): (.*)$') { $header[$Matches[1]] = $Matches[2].Trim() }
	}
	$header
}

function ConvertTo-ArxNumbers([string[]]$Words) { , @($Words | ForEach-Object { [double]::Parse($_, $inv) }) }

# The frames of a clip, each with the state after it: T (ms), Head / Left / Right (x y z yaw pitch roll), LeftCurls /
# RightCurls (5), Player (x y z yaw area); the anchors (T, Text)
function ConvertFrom-ArxClip([string[]]$Lines) {
	$state = @{}
	$frames = [Collections.Generic.List[object]]::new()
	$anchors = [Collections.Generic.List[object]]::new()
	foreach ($line in $Lines) {
		if ($line -notmatch '^@(\d+)ms\s+(.+)$') { continue }
		$t = [long]$Matches[1]
		foreach ($command in ($Matches[2] -split ';')) {
			$w = @($command.Trim() -split '\s+')
			if ($w[0] -eq 'anchor') { $anchors.Add([pscustomobject]@{ T = $t; Text = ($w -join ' ') }); continue }
			if ($w[0] -eq 'head' -and $w.Count -eq 7) { $state['Head'] = ConvertTo-ArxNumbers $w[1..6] }
			elseif ($w[0] -eq 'hand' -and $w.Count -eq 8 -and $w[2] -eq 'fingers') { $state[$(if ($w[1] -eq 'left') { 'LeftCurls' } else { 'RightCurls' })] = ConvertTo-ArxNumbers $w[3..7] }
			elseif ($w[0] -eq 'hand' -and $w.Count -eq 8) { $state[$(if ($w[1] -eq 'left') { 'Left' } else { 'Right' })] = ConvertTo-ArxNumbers $w[2..7] }
			elseif ($w[0] -eq 'player' -and $w.Count -eq 6) { $state['Player'] = ConvertTo-ArxNumbers $w[1..5] }
		}
		$frames.Add([pscustomobject]@{ T = $t; Head = $state['Head']; Left = $state['Left']; Right = $state['Right']
			LeftCurls = $state['LeftCurls']; RightCurls = $state['RightCurls']; Player = $state['Player'] })
	}
	[pscustomobject]@{ Frames = $frames.ToArray(); Anchors = $anchors.ToArray() }
}

function Get-ArxAngleDiff([double]$A, [double]$B) {
	$d = ($A - $B) % 360.0
	if ($d -gt 180) { $d -= 360 } elseif ($d -lt -180) { $d += 360 }
	[math]::Abs($d)
}

# A pose of a recorded frame pair interpolated at fraction $U: positions linear, angles along the short way
function Get-ArxLerpPose($P, $N, [double]$U) {
	$out = [double[]]::new($P.Count)
	for ($i = 0; $i -lt $P.Count; $i++) {
		$delta = $N[$i] - $P[$i]
		if ($i -ge 3 -and $P.Count -eq 6) {
			$delta = ($delta % 360.0)
			if ($delta -gt 180) { $delta -= 360 } elseif ($delta -lt -180) { $delta += 360 }
		}
		$out[$i] = $P[$i] + $U * $delta
	}
	, $out
}

# The replayed clip against the recorded one at the replayed frames' display times (#550: 1 mm, 0.1 degree, curls 0.01)
function Compare-ArxClips($Recorded, $Replayed, [long]$UntilMs = [long]::MaxValue) {
	$a = @($Recorded.Frames | Where-Object { $_.T -le $UntilMs -and $_.Head -and $_.Left -and $_.Right })
	$result = [pscustomobject]@{ Recorded = $a.Count; Compared = 0; Pos = 0.0; Deg = 0.0; Curl = 0.0; WorstT = -1 }
	if ($a.Count -lt 2) { return $result }
	$b = @($Replayed.Frames | Where-Object { $_.T -ge $a[0].T -and $_.T -le $a[-1].T -and $_.Head -and $_.Left -and $_.Right })
	$i = 0
	foreach ($f in $b) {
		while ($i + 1 -lt $a.Count -and $a[$i + 1].T -le $f.T) { $i++ }
		$p = $a[$i]
		$n = if ($i + 1 -lt $a.Count) { $a[$i + 1] } else { $p }
		$u = if ($n.T -gt $p.T) { ($f.T - $p.T) / [double]($n.T - $p.T) } else { 0.0 }
		$worst = 0.0
		foreach ($k in 'Head', 'Left', 'Right') {
			$want = Get-ArxLerpPose $p.$k $n.$k $u
			$got = $f.$k
			$pos = [math]::Sqrt([math]::Pow($want[0] - $got[0], 2) + [math]::Pow($want[1] - $got[1], 2) + [math]::Pow($want[2] - $got[2], 2))
			$deg = [math]::Max((Get-ArxAngleDiff $want[3] $got[3]), [math]::Max((Get-ArxAngleDiff $want[4] $got[4]), (Get-ArxAngleDiff $want[5] $got[5])))
			if ($pos -gt $result.Pos) { $result.Pos = $pos; $worst = 1 }
			if ($deg -gt $result.Deg) { $result.Deg = $deg; $worst = 1 }
		}
		foreach ($k in 'LeftCurls', 'RightCurls') {
			if (-not $p.$k -or -not $n.$k -or -not $f.$k) { continue }
			$want = Get-ArxLerpPose $p.$k $n.$k $u
			for ($j = 0; $j -lt 5; $j++) {
				$d = [math]::Abs($want[$j] - $f.$k[$j])
				if ($d -gt $result.Curl) { $result.Curl = $d; $worst = 1 }
			}
		}
		if ($worst) { $result.WorstT = $f.T }
		$result.Compared++
	}
	$result
}

# The input events of a run's arx.log, in order: "<event> (<binding>)" and "<hand> grip down|up"
function Get-ArxInputEvents([string[]]$LogLines) {
	@($LogLines | ForEach-Object {
		if ($_ -match '\[vr\] input: ([a-z_]+) \(([^()]*)\)\s*$') { "$($Matches[1]) ($($Matches[2]))" }
		elseif ($_ -match '\[vr\] input: (left|right) grip (down|up) at ') { "$($Matches[1]) grip $($Matches[2])" }
	})
}

function Read-ArxSharedLines([string]$Path) {
	if (-not (Test-Path -LiteralPath $Path)) { return @() }
	$stream = [IO.FileStream]::new($Path, 'Open', 'Read', 'ReadWrite, Delete')
	try { @(([IO.StreamReader]::new($stream)).ReadToEnd() -split "`r?`n" | Where-Object { $_ }) } finally { $stream.Dispose() }
}

# The session's clip of a run that started at $Since: arx-replay.log.gz written after it, and no plain file left
function Test-ArxSessionClip([string]$What, [DateTime]$Since) {
	$gz = Get-Item -LiteralPath $clipGz -ErrorAction SilentlyContinue
	$fresh = $gz -and $gz.LastWriteTime -ge $Since
	Check $fresh "$What`: arx-replay.log.gz written at the session's end"
	$plain = Get-Item -LiteralPath $clipPlain -ErrorAction SilentlyContinue
	Check (-not ($plain -and $plain.LastWriteTime -ge $Since)) "$What`: no plain arx-replay.log left (compressed at the end)"
	if ($fresh) { $gz.FullName } else { $null }
}

function Test-ArxNoClip([string]$What, [DateTime]$Since) {
	$written = @(Get-ChildItem -LiteralPath $userDir -Filter 'arx-replay*' -File -ErrorAction SilentlyContinue | Where-Object LastWriteTime -ge $Since)
	Check ($written.Count -eq 0) "$What`: no clip written ($(@($written | ForEach-Object Name) -join ', '))"
	$recording = @(Read-ArxSharedLines $logPath | Where-Object { $_ -match '\[replay\] recording' })
	Check ($recording.Count -eq 0) "$What`: arx.log has no record start"
}

function Test-ArxClipClean([string]$What, [string]$Text) {
	$found = @(Find-ArxPrivateText $Text | Select-Object -Unique)
	Check ($found.Count -eq 0) "$What`: no user path or name in it$(if ($found) { ' - found ' + ($found -join ', ') })"
}

# The newest mark clip of a name written after $Since
function Get-ArxMarkClip([string]$Name, [DateTime]$Since) {
	@(Get-ChildItem -LiteralPath $marksDir -Filter "*-$Name.vrsim" -File -ErrorAction SilentlyContinue |
		Where-Object LastWriteTime -ge $Since | Sort-Object LastWriteTime) | Select-Object -Last 1
}

# A mark's clip: its header names the mark; its frames span the window before it
function Test-ArxMarkClip([string]$What, $File, [string]$Name, [int]$WindowMs) {
	Check ($null -ne $File) "$What`: replay-marks\<utc>-$Name.vrsim written"
	if (-not $File) { return }
	$lines = @((Read-ArxClipText $File.FullName) -split "`r?`n" | Where-Object { $_ })
	$header = Get-ArxClipHeader $lines
	$markMs = if ("$($header['mark'])" -match "^$([regex]::Escape($Name)) (\d+)$") { [long]$Matches[1] } else { -1 }
	Check ($markMs -ge 0) "$What`: # mark: $Name <ms> ($($header['mark']))"
	Check ([bool]$header['seed'] -and [bool]$header['save'] -and [bool]$header['rate']) "$What`: the session's header (seed, save, rate)"
	$times = @($lines | ForEach-Object { if ($_ -match '^@(\d+)ms\s') { [long]$Matches[1] } })
	if ($times.Count -and $markMs -ge 0) {
		$span = "$($times[0])-$($times[-1]) ms, mark $markMs"
		Check ($times[0] -le $markMs - ($WindowMs - 500)) "$What`: the first frame at least $(($WindowMs - 500) / 1000) s before the mark ($span)"
		Check ($times[0] -ge $markMs - ($WindowMs + 1000)) "$What`: not much more than $($WindowMs / 1000) s ($span)"
		Check ($times[-1] -le $markMs -and $times[-1] -ge $markMs - 100) "$What`: the last frame within 100 ms of the mark ($span)"
	} else {
		Check $false "$What`: frames in the clip ($($times.Count))"
	}
	Test-ArxClipClean $What ($lines -join "`n")
}

# --- A run through smoke-test.ps1 on a thread of this process (as flight-recorder-check.ps1) -------------------------

function Start-ArxReplayRun([string]$What, [int]$Seconds, [string[]]$ArxArgs, [string]$VrSim, [string]$Save, [string]$SaveLibrary) {
	Clear-ArxLaunchMarker $testDir
	$params = @{ Seconds = $Seconds; LockTimeoutSec = 600; ArxArgs = $ArxArgs }
	if ($VrSim) { $params.VrSim = $VrSim }
	if ($Save) { $params.Save = $Save }
	if ($SaveLibrary) { $params.SaveLibrary = $SaveLibrary }
	if ($Rebuild -and -not $script:rebuilt) { $params.Rebuild = $true; $script:rebuilt = $true }
	$job = Start-ThreadJob -ScriptBlock {
		param($Smoke, $Params)
		& $Smoke @Params *>&1 | ForEach-Object { "$_" }
		"SMOKE-EXIT=$LASTEXITCODE"
	} -ArgumentList $smoke, $params
	$run = [pscustomobject]@{ What = $What; Job = $job; Launched = Get-Date; Output = @(); SmokeExit = $null; GameplayAt = $null }
	if (-not (Wait-ArxGameLaunched $job $testDir)) {
		Complete-ArxReplayRun $run
		if ($run.SmokeExit -eq 2) { Write-Host "  $What`: SKIPPED (the game lock stayed busy)" -ForegroundColor Yellow; $script:skipped = $true }
		else { Check $false "$What`: the game was launched (smoke-test exit $($run.SmokeExit))" }
		return $null
	}
	$run.Launched = Get-Date
	try {
		$null = Wait-ArxStatus { param($s) $s.state -eq 'GAMEPLAY' } -TimeoutSec 120 -Dir $testDir -What 'GAMEPLAY'
		$run.GameplayAt = Get-Date
	} catch {
		Check $false "$What`: the game reached GAMEPLAY ($_)"
		Complete-ArxReplayRun $run
		return $null
	}
	$run
}

function Complete-ArxReplayRun($Run, [int]$TimeoutSec = 300) {
	$null = Wait-Job $Run.Job -Timeout $TimeoutSec
	if ($Run.Job.State -eq 'Running') { Stop-Job $Run.Job }
	$Run.Output = @(Receive-Job $Run.Job -ErrorAction SilentlyContinue | ForEach-Object { "$_" })
	Remove-Job $Run.Job -Force -ErrorAction SilentlyContinue
	$exitLine = @($Run.Output | Where-Object { $_ -match '^SMOKE-EXIT=' }) | Select-Object -Last 1
	$Run.SmokeExit = if ($exitLine -match '^SMOKE-EXIT=(-?\d+)') { [int]$Matches[1] } else { $null }
	Write-Host "  --- smoke-test.ps1 ($($Run.What)), last lines:"
	$Run.Output | Where-Object { $_ -notmatch '^SMOKE-EXIT=' } | Select-Object -Last 12 | ForEach-Object { Write-Host "    $(Hide-ArxPrivateText $_)" }
}

function Get-ArxPlay {
	$s = Read-ArxStatus $testDir
	if ($s -and $s.vr -and $s.vr.sim -and $s.vr.sim.PSObject.Properties['play']) { $s.vr.sim.play } else { $null }
}

# A sequence of scripts\dev\xrs\manual through xrs-run.ps1: its exit code (2 = skipped)
function Invoke-ArxManualSequence([string]$Name) {
	$arguments = @((Join-Path $manual "$Name.xrs"))
	if ($Rebuild -and -not $script:rebuilt) { $arguments += '-Rebuild'; $script:rebuilt = $true }
	& $pwsh -NoProfile -File $xrsRun @arguments *>&1 | ForEach-Object { Write-Host "    $(Hide-ArxPrivateText "$_")" }
	$LASTEXITCODE
}

# --- The cases ---------------------------------------------------------------------------------------------------------

function Invoke-RecordCase {
	Write-Host '==> record: a 30 s walk-and-turn session, --seed 4242, a mark at its end' -ForegroundColor Cyan
	$since = Get-Date
	$exit = Invoke-ArxManualSequence 'replay-record'
	if ($exit -eq 2) { Write-Host '  record: SKIPPED (the game lock stayed busy)' -ForegroundColor Yellow; $script:skipped = $true; return }
	Check ($exit -eq 0) "record: the sequence passed (xrs-run exit $exit)"

	$log = Read-ArxSharedLines $logPath
	$start = @($log | Where-Object { $_ -match '\[replay\] recording arx-replay\.log: save (save\d+) "(.*)", seed (\d+), (\d+(\.\d+)?) Hz' }) | Select-Object -First 1
	Check ([bool]$start) "record: arx.log names the record start ($start)"
	$file = Test-ArxSessionClip 'record' $since
	if (-not $file) { return }
	$text = Read-ArxClipText $file
	$lines = @($text -split "`r?`n" | Where-Object { $_ })
	$header = Get-ArxClipHeader $lines
	Check ([bool]$header['build']) "record: # build: ($($header['build']))"
	Check ($header['seed'] -eq '4242') "record: # seed: 4242 ($($header['seed']))"
	Check ($header['rate'] -match '^90(\.0+)?$') "record: # rate: 90 ($($header['rate']))"
	$folder = if ("$($header['save'])" -match '^(save\d+)\s+\S') { $Matches[1] } else { $null }
	Check ([bool]$folder) "record: # save: <folder> <name> ($($header['save']))"
	$saveDir = if ($folder) { Join-Path $userDir "save\$folder" } else { $null }
	Check ($saveDir -and (Test-Path -LiteralPath (Join-Path $saveDir 'gsave.sav'))) "record: the save $folder made at the record start exists"

	$clip = ConvertFrom-ArxClip $lines
	$frames = $clip.Frames
	$seconds = if ($frames.Count -ge 2) { ($frames[-1].T - $frames[0].T) / 1000.0 } else { 0.0 }
	Check ($seconds -ge 30) ("record: at least 30 s of frames ({0:0.0} s, {1} frames)" -f $seconds, $frames.Count)
	Check ($frames.Count -ge $seconds * 80) ("record: a frame line every frame at 90 Hz ({0} frames in {1:0.0} s)" -f $frames.Count, $seconds)
	$bytes = [Text.Encoding]::UTF8.GetByteCount($text)
	$perMinute = if ($seconds -gt 0) { $bytes / ($seconds / 60.0) } else { [double]::MaxValue }
	$compressed = (Get-Item -LiteralPath $file).Length
	Write-Host ("  --- for the PR body: {0} frames, {1:0.0} s, {2:N0} bytes uncompressed = {3:0.00} MB/min, {4:N0} bytes gzip ({5:0.0}x)" -f
		$frames.Count, $seconds, $bytes, ($perMinute / 1e6), $compressed, ($bytes / [math]::Max(1, $compressed)))
	Check ($perMinute -le 2e6) ("record: <= 2 MB per minute uncompressed ({0:0.00} MB/min)" -f ($perMinute / 1e6))
	Test-ArxClipClean 'record: the clip' $text

	$events = @(Get-ArxInputEvents $log)
	Check ($events.Count -ge 2) "record: input events in arx.log ($($events -join ', '))"

	Test-ArxMarkClip 'record: the mark' (Get-ArxMarkClip 'check' $since) 'check' 30000

	# For the replay: the clip, its log and its save, out of the user directory the next run rotates
	Copy-Item -LiteralPath $file -Destination (Join-Path $work 'recorded.log.gz') -Force
	[IO.File]::WriteAllLines((Join-Path $work 'recorded-arx.log'), [string[]]$log)
	if ($saveDir -and (Test-Path -LiteralPath $saveDir)) {
		$saves = Join-Path $work 'recorded-saves'
		Remove-Item -Recurse -Force $saves -ErrorAction SilentlyContinue
		New-Item -ItemType Directory -Force $saves | Out-Null
		Copy-Item -Recurse -LiteralPath $saveDir -Destination (Join-Path $saves $folder)
	}
}

function Invoke-ReplayCase {
	Write-Host '==> replay: replay.ps1 on the recorded clip, compared' -ForegroundColor Cyan
	$recordedFile = Join-Path $work 'recorded.log.gz'
	if (-not (Test-Path -LiteralPath $recordedFile)) { Check $false 'replay: a recorded clip (run -Case record first)'; return }
	$recordedLines = @((Read-ArxClipText $recordedFile) -split "`r?`n" | Where-Object { $_ })
	$recorded = ConvertFrom-ArxClip $recordedLines
	$recordedEvents = @(Get-ArxInputEvents (Get-Content -LiteralPath (Join-Path $work 'recorded-arx.log')))
	$recordedSave = "$((Get-ArxClipHeader $recordedLines)['save'])"
	$folder = if ($recordedSave -match '^(save\d+)') { $Matches[1] } else { '' }

	$since = Get-Date
	$arguments = @($recordedFile)
	$savedCopy = Join-Path $work "recorded-saves\$folder"
	if ($folder -and (Test-Path -LiteralPath $savedCopy)) { $arguments += @('-Save', $savedCopy) }
	$output = @(& $pwsh -NoProfile -File $replay @arguments 2>&1 | ForEach-Object { "$_" })
	$exit = $LASTEXITCODE
	Write-Host '  --- replay.ps1 (last 30 lines):'
	$output | Select-Object -Last 30 | ForEach-Object { Write-Host "    $(Hide-ArxPrivateText $_)" }
	if ($exit -eq 2) { Write-Host '  replay: SKIPPED (the game lock stayed busy)' -ForegroundColor Yellow; $script:skipped = $true; return }
	Check ($exit -eq 0) "replay: replay.ps1 exit 0 - the clip played to its end ($exit)"
	$text = $output -join "`n"
	$drift = [regex]::Match($text, '(?m)^Drift: current (\d+(?:\.\d+)?) m, max (\d+(?:\.\d+)?) m')
	Check $drift.Success 'replay: replay.ps1 prints "Drift: current <m> m, max <m> m"'
	if ($drift.Success) { Write-Host "  --- for the PR body: the drift of the 30 s walk-and-turn clip: current $($drift.Groups[1].Value) m, max $($drift.Groups[2].Value) m" }
	$listed = @()
	$at = [array]::FindIndex([string[]]$output, [Predicate[string]] { param($l) $l -match '^Input events \((\d+)\):' })
	if ($at -ge 0) {
		for ($i = $at + 1; $i -lt $output.Count -and $output[$i] -match '^\s+(\S.*)$'; $i++) { $listed += $Matches[1].Trim() }
	}
	Check ($at -ge 0) 'replay: replay.ps1 prints "Input events (<n>):"'
	Check (($listed -join ' | ') -eq ($recordedEvents -join ' | ')) "replay: the events it lists are the recording's, in order ($($listed.Count) vs $($recordedEvents.Count))"
	$replayedEvents = @(Get-ArxInputEvents (Read-ArxSharedLines $logPath))
	Check (($replayedEvents -join ' | ') -eq ($recordedEvents -join ' | ')) "replay: the replay's arx.log has the recording's input events in the same order`n      recorded: $($recordedEvents -join ', ')`n      replayed: $($replayedEvents -join ', ')"

	# The replay's own clip (every VR session records) against the recording
	$file = Test-ArxSessionClip 'replay' $since
	if (-not $file) { return }
	$replayedText = Read-ArxClipText $file
	$replayed = ConvertFrom-ArxClip @($replayedText -split "`r?`n" | Where-Object { $_ })
	Check ((Get-ArxClipHeader @($replayedText -split "`r?`n" | Select-Object -First 20))['seed'] -eq '4242') 'replay: the replay ran with the recording''s seed (its clip''s # seed: 4242)'
	$cmp = Compare-ArxClips $recorded $replayed
	Write-Host ("  --- for the PR body: {0} replayed frames against {1} recorded; worst head/grip position {2:0.0000} m, angle {3:0.000} deg, curl {4:0.0000} (at {5} ms)" -f
		$cmp.Compared, $cmp.Recorded, $cmp.Pos, $cmp.Deg, $cmp.Curl, $cmp.WorstT)
	Check ($cmp.Compared -ge 0.9 * $cmp.Recorded) "replay: frames at the recorded display times, at least 90 % ($($cmp.Compared) of $($cmp.Recorded))"
	Check ($cmp.Pos -le 0.001) ("replay: head and grips within 1 mm ({0:0.0000} m)" -f $cmp.Pos)
	Check ($cmp.Deg -le 0.1) ("replay: head and grips within 0.1 degree ({0:0.000})" -f $cmp.Deg)
	Check ($cmp.Curl -le 0.01) ("replay: curls within 0.01 ({0:0.0000})" -f $cmp.Curl)
}

function Invoke-AnchorCase {
	Write-Host '==> anchor: a recording across loadlevel 11, replayed' -ForegroundColor Cyan
	$since = Get-Date
	$exit = Invoke-ArxManualSequence 'replay-anchor-record'
	if ($exit -eq 2) { Write-Host '  anchor: SKIPPED (the game lock stayed busy)' -ForegroundColor Yellow; $script:skipped = $true; return }
	Check ($exit -eq 0) "anchor: the recording's sequence passed (xrs-run exit $exit)"
	$file = Test-ArxSessionClip 'anchor' $since
	if (-not $file) { return }
	$text = Read-ArxClipText $file
	$lines = @($text -split "`r?`n" | Where-Object { $_ })
	$clip = ConvertFrom-ArxClip $lines
	$anchor = @($clip.Anchors | Where-Object Text -eq 'anchor level 11') | Select-Object -First 1
	Check ($null -ne $anchor) "anchor: the clip has 'anchor level 11' ($(@($clip.Anchors | ForEach-Object { "$($_.T) ms $($_.Text)" }) -join ', '))"
	$header = Get-ArxClipHeader $lines
	$folder = if ("$($header['save'])" -match '^(save\d+)') { $Matches[1] } else { $null }
	if (-not $anchor -or -not $folder) { Check ([bool]$folder) 'anchor: the clip names its save'; return }

	# The replay: the clip under the test directory, a script that plays it from the timeline's start, its save alone
	$saves = Join-Path $work 'anchor-saves'
	Remove-Item -Recurse -Force $saves -ErrorAction SilentlyContinue
	New-Item -ItemType Directory -Force $saves | Out-Null
	# A replay slot (save9000..) is never listed by the game: the replay starts from it as an ordinary slot, as replay.ps1 does
	Copy-Item -Recurse -LiteralPath (Join-Path $userDir "save\$folder") -Destination (Join-Path $saves 'save0000')
	$folder = 'save0000'
	$playDir = Join-Path $testDir 'replay'
	New-Item -ItemType Directory -Force $playDir | Out-Null
	[IO.File]::WriteAllText((Join-Path $playDir 'anchor.vrsim'), $text, [Text.UTF8Encoding]::new($false))
	$sim = Join-Path $work 'anchor-play.vrsim'
	[IO.File]::WriteAllLines($sim, @('# replay-check.ps1 -Case anchor (#550)', "rate $($header['rate']) cap", 'timeline gameplay', 'play replay\anchor.vrsim'))

	$run = Start-ArxReplayRun 'anchor (replay)' 90 @('--loadslot', '0', '--seed', $header['seed']) -VrSim $sim -Save $folder -SaveLibrary $saves
	if (-not $run) { return }
	try {
		$null = Wait-ArxStatus { param($s) $s.vr.sim.play -and [long]$s.vr.sim.play.framesServed -gt 0 } -TimeoutSec 30 -Dir $testDir -What 'vr.sim.play.framesServed > 0'
		$play = Get-ArxPlay
		Check ($play.file -eq 'anchor.vrsim') "anchor: vr.sim.play.file is the clip's name only ($($play.file))"
		$null = Wait-ArxStatus { param($s) [int]$s.vr.sim.play.anchorsWaiting -eq 1 } -TimeoutSec 60 -Dir $testDir -What 'vr.sim.play.anchorsWaiting = 1'
		$held = (Get-ArxPlay).positionMs
		Start-Sleep -Seconds 2
		$play = Get-ArxPlay
		Check ([int]$play.anchorsWaiting -eq 1 -and [math]::Abs([double]$play.positionMs - [double]$held) -lt 1) "anchor: the play holds at the anchor (positionMs $held -> $($play.positionMs))"
		Check ([math]::Abs([double]$play.positionMs - $anchor.T) -le 50) "anchor: it holds where the clip has the anchor ($($play.positionMs) ms, anchor at $($anchor.T) ms)"
		$resumed = $false
		try { $null = Wait-ArxStatus { param($s) [int]$s.vr.sim.play.anchorsWaiting -eq 0 } -TimeoutSec 10 -Dir $testDir -What 'the anchor resolved by itself'; $resumed = $true } catch { }
		if (-not $resumed) {
			Write-Host '  the replayed game did not change the level by itself: sending the recording''s loadlevel 11'
			$ack = Send-ArxCommand -Lines @('loadlevel 11') -Dir $testDir -TimeoutSec 120
			Check ($ack -match 'errors=0 ') "anchor: loadlevel 11 ($ack)"
		}
		$null = Wait-ArxStatus { param($s) [int]$s.vr.sim.play.anchorsWaiting -eq 0 } -TimeoutSec 60 -Dir $testDir -What 'vr.sim.play.anchorsWaiting = 0'
		$final = Wait-ArxStatus { param($s) $s.vr.sim.play.done -eq $true } -TimeoutSec 60 -Dir $testDir -What 'vr.sim.play.done'
		Check ([int]$final.vr.sim.play.anchorsWaiting -eq 0) 'anchor: the clip finished with anchorsWaiting back to 0'
		Check ([int]$final.area -eq 11) "anchor: the replayed game is in level 11 ($($final.area))"
		Check ([long]$final.vr.sim.play.framesServed -ge 0.9 * $clip.Frames.Count) "anchor: the frames served ($($final.vr.sim.play.framesServed) of $($clip.Frames.Count))"
		Check ($null -ne $final.vr.sim.play.drift.current -and $null -ne $final.vr.sim.play.drift.max) "anchor: vr.sim.play.drift current $($final.vr.sim.play.drift.current) m, max $($final.vr.sim.play.drift.max) m"
		Check ($null -ne $final.vr.sim.play.eventsReplayed) "anchor: vr.sim.play.eventsReplayed ($($final.vr.sim.play.eventsReplayed))"
	} catch {
		Check $false "anchor: $_"
	} finally {
		Complete-ArxReplayRun $run
	}
	Check ($run.SmokeExit -eq 0) "anchor: the replay run passes smoke-test.ps1 (exit $($run.SmokeExit))"
}

# The busy time of a run's seconds (the frame cost without the pacing waits), the first 3 s left out
function Get-ArxBusy([string[]]$Lines) {
	$log = Read-ArxPerfLog $Lines
	$seconds = @($log.Seconds | Where-Object { $_.t -gt 3 })
	$wait = { param($s) $w = 0.0; foreach ($k in 'limit', 'vrwait') { if ($s.Phases.Contains($k)) { $w += [double]$s.Phases[$k] } }; $w }
	$mean = @($seconds | ForEach-Object { [double]$_.mean - (& $wait $_) } | Sort-Object)
	$p95 = @($seconds | ForEach-Object { [double]$_.p95 - (& $wait $_) } | Sort-Object)
	$pick = { param($v, [double]$q) if ($v.Count) { $v[[math]::Min($v.Count - 1, [int][math]::Floor($q * ($v.Count - 1) + 0.5))] } else { [double]::NaN } }
	[pscustomobject]@{ Seconds = $seconds.Count; Median = (& $pick $mean 0.5); P99 = (& $pick $p95 0.99) }
}

function Invoke-CostCase {
	Write-Host '==> cost: 30 s idle with the recording, 30 s with --no-replay-log' -ForegroundColor Cyan
	$rows = @()
	foreach ($variant in 'recording', 'no-replay-log') {
		$arxArgs = @('--loadlevel', '10')
		if ($variant -eq 'no-replay-log') { $arxArgs += '--no-replay-log' }
		$since = Get-Date
		$run = Start-ArxReplayRun "cost ($variant)" 30 $arxArgs -VrSim 'idle'
		if (-not $run) { return }
		try {
			if ($variant -eq 'recording') {
				$ack = Send-ArxCommand -Lines @('replay mark bad/name') -Dir $testDir
				Check ($ack -notmatch 'errors=0 ') "cost (recording): replay mark with a bad name is refused ($ack)"
				Start-Sleep -Seconds 7
				$ack = Send-ArxCommand -Lines @('replay mark short 5') -Dir $testDir
				Check ($ack -match 'errors=0 ') "cost (recording): replay mark short 5 ($ack)"
			} else {
				$ack = Send-ArxCommand -Lines @('replay mark x') -Dir $testDir
				Check ($ack -notmatch 'errors=0 ') "cost (--no-replay-log): replay mark is refused, nothing records ($ack)"
			}
		} catch {
			Check $false "cost ($variant): $_"
		}
		Complete-ArxReplayRun $run
		Check ($run.SmokeExit -eq 0) "cost ($variant): the run passes smoke-test.ps1 (exit $($run.SmokeExit))"
		$busy = Get-ArxBusy (Read-ArxSharedLines $perfLog)
		$size = ''
		if ($variant -eq 'recording') {
			Test-ArxMarkClip 'cost (recording): the 5 s mark' (Get-ArxMarkClip 'short' $since) 'short' 5000
			$file = Test-ArxSessionClip 'cost (recording)' $since
			if ($file) {
				$text = Read-ArxClipText $file
				$size = '{0:N0} bytes, {1:N0} gzip' -f [Text.Encoding]::UTF8.GetByteCount($text), (Get-Item -LiteralPath $file).Length
			}
		} else {
			Test-ArxNoClip 'cost (--no-replay-log)' $since
		}
		$rows += [pscustomobject]@{ Variant = $variant; Seconds = $busy.Seconds; MedianBusyMs = [math]::Round($busy.Median, 3); P99BusyMs = [math]::Round($busy.P99, 3); Clip = $size }
	}
	Write-Host '  --- for the PR body:'
	$rows | Format-Table -AutoSize | Out-String -Width 200 | ForEach-Object { Write-Host $_ }
	if ($rows.Count -eq 2) {
		$median = $rows[0].MedianBusyMs - $rows[1].MedianBusyMs
		$p99 = $rows[0].P99BusyMs - $rows[1].P99BusyMs
		Check ($median -lt 0.05) ("cost: the recording adds {0:0.000} ms to the median busy time per frame (less than 0.05)" -f $median)
		Check ($p99 -lt 0.2) ("cost: the recording adds {0:0.000} ms to the 99th percentile (less than 0.2)" -f $p99)
	}
}

function Invoke-FlatCase {
	Write-Host '==> flat: --no-vr records nothing' -ForegroundColor Cyan
	$since = Get-Date
	$run = Start-ArxReplayRun 'flat' 8 @('--loadlevel', '10')
	if (-not $run) { return }
	try {
		$ack = Send-ArxCommand -Lines @('replay mark x') -Dir $testDir
		Check ($ack -notmatch 'errors=0 ') "flat: replay mark is refused ($ack)"
	} catch {
		Check $false "flat: $_"
	}
	Complete-ArxReplayRun $run
	Check ($run.SmokeExit -eq 0) "flat: the run passes smoke-test.ps1 (exit $($run.SmokeExit))"
	Test-ArxNoClip 'flat' $since
}

# --- Run ---------------------------------------------------------------------------------------------------------------

foreach ($c in $cases) {
	switch ($c) {
		'record' { Invoke-RecordCase }
		'replay' { Invoke-ReplayCase }
		'anchor' { Invoke-AnchorCase }
		'cost' { Invoke-CostCase }
		'flat' { Invoke-FlatCase }
	}
}

Write-Host ''
if ($script:failed -eq 0 -and $script:skipped) { Write-Host "replay check: SKIPPED (a launch could not take the game lock; $($script:passed) checks passed)" -ForegroundColor Yellow; exit 2 }
if ($script:failed -eq 0) { Write-Host "replay check: PASS ($($script:passed) checks)" -ForegroundColor Green; exit 0 }
Write-Host "replay check: FAIL ($($script:failed) of $($script:failed + $script:passed) checks)" -ForegroundColor Red
exit 1
