<#
.SYNOPSIS
	Stress check of refresh-rate.xrs (#553): the sequence N times on two games at once beside a busy third game; FAIL
	on any failed run. The hand-run proof that its frame-count guards (motion.records.<x>.frames ge N) hold under load.

.DESCRIPTION
	The refresh-rate sequence (#436, D-057) counts the frames of every timed record since its `motion reset` and wants
	at least 90 % of the rate's frames. It failed three times under load (turn90.frames 76-80 < 81, turn72.frames
	62 < 64). The cause seen in the #558 gate (CI run 39359033513, arx-perf.log of xrs-refresh-rate, run alone): one
	frame inside turn72's window took 173.8 ms, `cause=channel` with channel=159.6 ms - the test channel's status.json
	write on the main thread (built, written to status.json.tmp, renamed over status.json, arx.log flushed, every
	100 ms and after every batch) - and the simulated headset, paced to the display clock (`vr-sim rate N clock`),
	skipped the 11 display slots that frame missed, as a headset does: 73 - 11 = 62 frames. The other frames of the
	run were on time (`S t=21 frames=72 missed=0`); the routine status writes cost 6-9 ms of the 13.9 ms frame.

	The check, by default: -Runs runs (10) of the sequence, -Lanes (2) at once, while a third game - the playground on
	the idle simulated headset - runs the whole time (-NoBusy: without it). Each run is smoke-test.ps1 with the
	sequence's own @sim / @save / @args, a fresh user directory and its lane's own test channel, as ci-smoke.ps1 runs a
	phase (@solo is CI's scheduling and is not followed here). Exit 0 when every run passed, 1 when one failed, 2 when
	a run was skipped (the game lock stayed busy).

	The variants run a copy of the sequence, made in -OutDir (the shipped file is never changed):
	  -SlowStatusMs <n> the slow file system of the failing run, every time: a helper process holds an oplock (read and
	                    handle caching, as a virus scanner or an indexer takes one) on each lane's status.json; the
	                    game's next replacement of the file breaks it and waits until the helper lets go, n ms later;
	                    -SlowStatusEveryMs (700) later the helper takes the next one. Readers of status.json (the test
	                    runner) are not slowed. As shipped, 150 ms fails a frames guard every time (one such write in
	                    every second of display time, ~13 display slots at 90 Hz); a game that never waits for that
	                    write on the frame passes it. Each lane's helper log is oplock-lane<N>.log in -OutDir.
	  -NegativeControl  the broken game the check must still catch: the 90 Hz block's `vr-sim rate 90 clock` becomes
	                    `vr-sim rate 45 clock` - the simulated headset paced at half the rate the block's guards
	                    count for, every record of the block taken over half its frames. Every run must then FAIL on a
	                    motion.records.<x>.frames guard of the sequence itself; a run that passes, or fails elsewhere,
	                    fails this script: the guards would miss a record that did not run at its rate.
	After every run the records' frame counts (arx.log `[channel] record`) and the channel's hitches of 50 ms and more
	(arx-perf.log `cause=channel`) are printed; run<NN>-arx.log and run<NN>-arx-perf.log are kept in -OutDir.

	Launches: one per run plus the busy game (default 11). The launch budget is 10 per task (WORKFLOW.md): agree a
	stress run with the coordinator first. Every launch follows D-014 (scripts/launch-lib.ps1); the game lock is
	raised to -Lanes + 1 games for this script's own launches only (ARXVR_MAX_GAMES), and CI's games still come first.

.PARAMETER Runs
	How many runs of the sequence (default 10).

.PARAMETER Lanes
	How many runs at once (default 2, as CI runs them).

.PARAMETER NoBusy
	No busy third game beside the lanes.

.PARAMETER SlowStatusMs
	How long each replacement of a lane's status.json waits behind the helper's oplock (default 0: no helper).

.PARAMETER SlowStatusEveryMs
	How long the helper waits after letting go before it takes the next oplock (default 700).

.PARAMETER NegativeControl
	The 90 Hz block paced at 45 Hz (see above): every run must FAIL on a frames guard.

.PARAMETER Sequence
	The sequence (default refresh-rate).

.PARAMETER OutDir
	Where the transcripts, the logs, the evidence of failed runs and the copied sequence go
	(default %TEMP%\refresh-rate-stress-<time>).

.PARAMETER DryRun
	Write and parse the sequence copy, compile the oplock helper, print what would run, launch nothing.

.EXAMPLE
	.\scripts\dev\refresh-rate-stress.ps1
	# the contract's check: 10 runs, 2 lanes, a busy third game
.EXAMPLE
	.\scripts\dev\refresh-rate-stress.ps1 -Runs 1 -NoBusy -SlowStatusMs 150
	# every status.json replacement the helper catches waits 150 ms: the CI failure, every time
.EXAMPLE
	.\scripts\dev\refresh-rate-stress.ps1 -Runs 1 -NoBusy -NegativeControl
	# the 90 Hz block paced at 45 Hz: every run must fail on a frames guard
#>
[CmdletBinding()]
param(
	[ValidateRange(1, 100)]
	[int]$Runs = 10,
	[ValidateRange(1, 4)]
	[int]$Lanes = 2,
	[switch]$NoBusy,
	[ValidateRange(0, 2000)]
	[int]$SlowStatusMs = 0,
	[ValidateRange(0, 60000)]
	[int]$SlowStatusEveryMs = 700,
	[switch]$NegativeControl,
	[string]$Sequence = 'refresh-rate',
	[ValidateSet('Release', 'RelWithDebInfo', 'Debug')]
	[string]$Config = 'Release',
	[string]$DataDir = $env:ARXVR_DATA_DIR,
	[int]$LockTimeoutSec = 600,
	[string]$OutDir,
	[switch]$DryRun
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8
. (Join-Path $PSScriptRoot 'test-lib.ps1')
. (Join-Path $PSScriptRoot 'xrs-lib.ps1')

# --- The oplock helper ------------------------------------------------------------------------------------------
# A read-handle oplock on status.json (FSCTL_REQUEST_OPLOCK): a rename onto the file breaks it and waits until the
# holder closes its handle - what a scanner that has the file open does to a writer. Readers that only read are not
# broken. Run() ends when the stop file exists and returns the breaks it held.
$oplockSource = @'
using System;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;
using Microsoft.Win32.SafeHandles;

public static class ArxStatusOplock {
	const uint GENERIC_READ = 0x80000000;
	const uint FILE_SHARE_ALL = 7;
	const uint OPEN_EXISTING = 3;
	const uint FILE_FLAG_OVERLAPPED = 0x40000000;
	const uint FSCTL_REQUEST_OPLOCK = 0x00090240;
	const int ERROR_IO_PENDING = 997;
	const uint OPLOCK_LEVEL_CACHE_READ = 1;
	const uint OPLOCK_LEVEL_CACHE_HANDLE = 2;
	const uint REQUEST_OPLOCK_INPUT_FLAG_REQUEST = 1;

	[StructLayout(LayoutKind.Sequential)]
	struct InBuf { public ushort Version; public ushort Length; public uint Level; public uint Flags; }

	[DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
	static extern SafeFileHandle CreateFileW(string name, uint access, uint share, IntPtr sa, uint disposition, uint flags, IntPtr template);
	[DllImport("kernel32.dll", SetLastError = true)]
	static extern bool DeviceIoControl(SafeFileHandle handle, uint code, ref InBuf input, int inputSize, IntPtr output, int outputSize, out uint returned, IntPtr overlapped);
	[DllImport("kernel32.dll", SetLastError = true)]
	static extern bool GetOverlappedResult(SafeFileHandle handle, IntPtr overlapped, out uint transferred, bool wait);
	[DllImport("kernel32.dll", SetLastError = true)]
	static extern bool CancelIoEx(SafeFileHandle handle, IntPtr overlapped);

	public static int Run(string path, int delayMs, int everyMs, string stopFile, string logFile) {
		int breaks = 0;
		int overlappedSize = Marshal.SizeOf(typeof(NativeOverlapped));
		while (!File.Exists(stopFile)) {
			SafeFileHandle handle = CreateFileW(path, GENERIC_READ, FILE_SHARE_ALL, IntPtr.Zero, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, IntPtr.Zero);
			if (handle.IsInvalid) { Thread.Sleep(2); continue; }
			IntPtr overlapped = Marshal.AllocHGlobal(overlappedSize);
			IntPtr output = Marshal.AllocHGlobal(64);
			bool broken = false;
			using (ManualResetEvent done = new ManualResetEvent(false)) {
				for (int i = 0; i < overlappedSize; i++) Marshal.WriteByte(overlapped, i, 0);
				for (int i = 0; i < 64; i++) Marshal.WriteByte(output, i, 0);
				NativeOverlapped ov = new NativeOverlapped();
				ov.EventHandle = done.SafeWaitHandle.DangerousGetHandle();
				Marshal.StructureToPtr(ov, overlapped, false);
				Marshal.WriteInt16(output, 0, 1);  // REQUEST_OPLOCK_OUTPUT_BUFFER: version 1, 24 bytes
				Marshal.WriteInt16(output, 2, 24);
				InBuf input = new InBuf();
				input.Version = 1;
				input.Length = (ushort)Marshal.SizeOf(typeof(InBuf));
				input.Level = OPLOCK_LEVEL_CACHE_READ | OPLOCK_LEVEL_CACHE_HANDLE;
				input.Flags = REQUEST_OPLOCK_INPUT_FLAG_REQUEST;
				uint returned;
				bool ok = DeviceIoControl(handle, FSCTL_REQUEST_OPLOCK, ref input, Marshal.SizeOf(typeof(InBuf)), output, 24, out returned, overlapped);
				int error = Marshal.GetLastWin32Error();
				if (!ok && error == ERROR_IO_PENDING) {
					// Granted: the request completes when a writer breaks it
					bool stopped = false;
					while (!done.WaitOne(20)) {
						if (File.Exists(stopFile)) { CancelIoEx(handle, overlapped); stopped = true; break; }
					}
					uint transferred;
					GetOverlappedResult(handle, overlapped, out transferred, true);
					broken = !stopped;
				}
				if (broken) {
					Stopwatch held = Stopwatch.StartNew();
					Thread.Sleep(delayMs);
					handle.Dispose();
					breaks++;
					if (logFile != null) {
						File.AppendAllText(logFile, DateTime.Now.ToString("HH:mm:ss.fff") + " break " + breaks + ": the writer waited "
						                   + held.ElapsedMilliseconds + " ms\n");
					}
				} else {
					handle.Dispose();
				}
			}
			Marshal.FreeHGlobal(output);
			Marshal.FreeHGlobal(overlapped);
			Thread.Sleep(broken ? everyMs : 2);
		}
		return breaks;
	}
}
'@

# --- The sequence copy ------------------------------------------------------------------------------------------
# Each line of the copy with the line of the original it is (0: a line added here), so a failure names the original
$source = Resolve-ArxSequence $Sequence
$name = [IO.Path]::GetFileNameWithoutExtension($source)
if (-not $OutDir) { $OutDir = Join-Path $env:TEMP ("$name-stress-" + (Get-Date -Format 'yyyyMMdd-HHmmss')) }
New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path -LiteralPath $OutDir).Path

$lines = [Collections.Generic.List[string]]::new()
$origin = [Collections.Generic.List[int]]::new()
function Add-Line([string]$Text, [int]$From = 0) { $lines.Add($Text); $origin.Add($From) }
$n = 0
$controlHooked = $false
foreach ($raw in [IO.File]::ReadAllLines($source)) {
	$n++
	$code = ($raw -replace '\s+#(\s.*)?$', '').Trim()
	if ($NegativeControl -and -not $controlHooked -and $code -match '^vr-sim\s+rate\s+90\s+clock$') {
		# The 90 Hz block's records taken at half its rate: its frames guards count for 90
		Add-Line 'vr-sim rate 45 clock' $n
		$controlHooked = $true
		continue
	}
	Add-Line $raw $n
}
if ($NegativeControl -and -not $controlHooked) {
	throw "$source has no 'vr-sim rate 90 clock' line: the negative control has nothing to hook into - the sequence changed, ask the coordinator how the 90 Hz block sets its rate now"
}
$copyDir = Join-Path $OutDir 'sequence'
New-Item -ItemType Directory -Force $copyDir | Out-Null
$copy = Join-Path $copyDir "$name.xrs"
[IO.File]::WriteAllLines($copy, $lines)
$parsed = Read-ArxSequence $copy   # a typo must not cost a launch
if ($SlowStatusMs -gt 0) { Add-Type -TypeDefinition $oplockSource }   # a compile error must not cost one either

# The original's line of a line of the copy; 0 for a line added here
function Get-OriginalLine([int]$CopyLine) {
	if ($CopyLine -lt 1 -or $CopyLine -gt $origin.Count) { return 0 }
	$origin[$CopyLine - 1]
}

$variant = @()
if ($SlowStatusMs) { $variant += "status.json replacements held $SlowStatusMs ms by an oplock, the next one $SlowStatusEveryMs ms later" }
if ($NegativeControl) { $variant += 'negative control: the 90 Hz block paced at 45 Hz' }
$variantText = if ($variant) { $variant -join '; ' } else { 'as shipped' }
$busy = -not $NoBusy
$maxGames = $Lanes + [int]$busy
Write-Host "stress: $name ($variantText), $Runs run(s) on $Lanes lane(s)$(if ($busy) { ' beside a busy third game' }); $($Runs + [int]$busy) launch(es)"
Write-Host "stress: sequence copy $copy ($($parsed.Steps.Count) steps)"
Write-Host "stress: evidence in $OutDir"
if ($DryRun) { Write-Host 'stress: dry run - nothing launched'; exit 0 }

# --- The games --------------------------------------------------------------------------------------------------
# The lanes' jobs and their games die with this script (#526)
$null = Register-ArxLauncherChain
$env:ARXVR_MAX_GAMES = "$maxGames"
$env:ARXVR_AUTOREPORT = '0'
$smoke = Join-Path $PSScriptRoot 'smoke-test.ps1'
$key = Get-WorkingCopyKey $ArxSourceDir
$userRoot = Join-Path (Get-ArxAppDataDir) "userdirs\$key-stress"
$testRoot = Get-ArxTestDir
$dataArgs = @{}
if ($DataDir) { $dataArgs.DataDir = $DataDir }

$job = {
	param([string]$Smoke, [hashtable]$Params, [string]$Transcript, [string]$TestDir)
	$ErrorActionPreference = 'Stop'
	[Console]::OutputEncoding = [Text.Encoding]::UTF8
	$env:ARXVR_TEST_DIR = $TestDir
	$global:LASTEXITCODE = 1
	try {
		& $Smoke @Params *>&1 | ForEach-Object { "$_" } | Out-File -LiteralPath $Transcript -Encoding utf8
		$LASTEXITCODE
	} catch {
		"stress: the run threw: $_" | Out-File -LiteralPath $Transcript -Append -Encoding utf8
		1
	}
}

function Clear-Channel([string]$Dir) {
	New-Item -ItemType Directory -Force $Dir | Out-Null
	foreach ($f in 'status.json', 'ack.txt', 'command.txt', 'stdout.txt', 'stderr.txt', 'launched.txt', 'hang.txt') {
		Remove-Item (Join-Path $Dir $f) -Force -ErrorAction SilentlyContinue
	}
}

# The records' frames and the channel's long frames of one run, from its arx.log and arx-perf.log
function Get-RunSummary([string]$UserDir, [int]$Run) {
	$parts = @()
	$log = Join-Path $UserDir 'arx.log'
	$perf = Join-Path $UserDir 'arx-perf.log'
	if (Test-Path -LiteralPath $log) {
		Copy-Item -LiteralPath $log -Destination (Join-Path $OutDir ('run{0:00}-arx.log' -f $Run))
		$records = foreach ($m in [regex]::Matches((Get-Content -LiteralPath $log -Raw), '\[channel\] record (\w+): \{[^\r\n]*?"frames":(\d+),"seconds":([\d.]+)')) {
			if ($m.Groups[1].Value -match '^(head|walk|turn|span)\d+$') { '{0}={1}' -f $m.Groups[1].Value, $m.Groups[2].Value }
		}
		if ($records) { $parts += 'frames: ' + ($records -join ' ') }
	}
	if (Test-Path -LiteralPath $perf) {
		Copy-Item -LiteralPath $perf -Destination (Join-Path $OutDir ('run{0:00}-arx-perf.log' -f $Run))
		$long = @(Get-Content -LiteralPath $perf | Where-Object { $_ -match '^H .*\bms=([\d.]+) .*\bcause=channel\b' -and [double]$Matches[1] -ge 50 })
		$worst = 0.0
		foreach ($l in $long) { if ($l -match '\bms=([\d.]+)') { $worst = [Math]::Max($worst, [double]$Matches[1]) } }
		$parts += ('channel hitches >= 50 ms: {0}{1}' -f $long.Count, $(if ($long.Count) { " (worst $worst ms)" }))
	}
	$parts -join '; '
}

# The busy third game: the playground on the idle simulated headset, for as long as the runs take
$busyJob = $null
$busyDir = "$testRoot-stress-busy"
$holders = @()
$holderStop = Join-Path $OutDir 'oplock-stop'
Remove-Item -LiteralPath $holderStop -Force -ErrorAction SilentlyContinue
if ($busy) {
	Clear-Channel $busyDir
	$p = @{ Config = $Config; Playground = $true; VrSim = 'idle'; Seconds = 7200; LockTimeoutSec = $LockTimeoutSec
		UserDir = (New-ArxSequenceUserDir $userRoot 'busy'); TestDir = $busyDir } + $dataArgs
	$busyJob = Start-Job -ScriptBlock $job -ArgumentList $smoke, $p, (Join-Path $OutDir 'busy.log'), $busyDir
	Write-Host "stress: busy game starting (channel $busyDir)"
	$deadline = (Get-Date).AddSeconds($LockTimeoutSec + 120)
	while ($true) {
		$state = $null
		try { $state = (Get-Content -LiteralPath (Join-Path $busyDir 'status.json') -Raw -ErrorAction Stop | ConvertFrom-Json).state } catch { }
		if ($state -eq 'GAMEPLAY') { break }
		if ($busyJob.State -ne 'Running') { Write-Host "stress: the busy game ended before it was playing - see $OutDir\busy.log" -ForegroundColor Red; exit 2 }
		if ((Get-Date) -gt $deadline) { Write-Host 'stress: the busy game is not playing after the lock timeout' -ForegroundColor Red; exit 2 }
		Start-Sleep -Milliseconds 500
	}
	Write-Host 'stress: busy game playing'
}

$results = @()
$running = @{}   # lane -> { Run; Job; Started; UserDir }
$next = 1
try {
	if ($SlowStatusMs -gt 0) {
		# One helper per lane, on the lane's status.json, for as long as the runs take (a missing file is waited for)
		foreach ($lane in 1..$Lanes) {
			$laneStatus = Join-Path "$testRoot-stress-lane$lane" 'status.json'
			$holderLog = Join-Path $OutDir "oplock-lane$lane.log"
			$holders += Start-Job -ScriptBlock {
				param([string]$Source, [string]$Path, [int]$Delay, [int]$Every, [string]$Stop, [string]$Log)
				Add-Type -TypeDefinition $Source
				[ArxStatusOplock]::Run($Path, $Delay, $Every, $Stop, $Log)
			} -ArgumentList $oplockSource, $laneStatus, $SlowStatusMs, $SlowStatusEveryMs, $holderStop, $holderLog
		}
		Write-Host "stress: oplock helpers on $Lanes lane(s): each status.json replacement they catch waits $SlowStatusMs ms"
	}
	while ($next -le $Runs -or $running.Count) {
		foreach ($lane in @($running.Keys)) {
			$r = $running[$lane]
			if ($r.Job.State -eq 'Running') { continue }
			$out = @(Receive-Job $r.Job -ErrorAction SilentlyContinue)
			Remove-Job $r.Job -Force
			$running.Remove($lane)
			$exit = if ($out.Count -and $out[-1] -is [int]) { [int]$out[-1] } else { 1 }
			$transcript = Join-Path $OutDir ('run{0:00}.log' -f $r.Run)
			$text = if (Test-Path -LiteralPath $transcript) { Get-Content -LiteralPath $transcript -Raw } else { '' }
			$reason = ''
			$line = 0
			if ($text -match "=> FAIL sequence [^:]+: line (\d+) '([^']*)': ([^\r\n]*)") {
				$line = Get-OriginalLine ([int]$Matches[1])
				$where = if ($line) { "line $line" } else { 'a step added by this script' }
				$reason = "$where '$($Matches[2])': $($Matches[3])"
			} elseif ($exit -ne 0) {
				$reason = (@($text -split "`r?`n" | Where-Object { $_ -match 'FAIL|SKIPPED|threw' }) | Select-Object -Last 1)
			}
			$verdict = switch ($exit) { 0 { 'PASS' } 2 { 'SKIPPED' } default { 'FAIL' } }
			$seconds = ((Get-Date) - $r.Started).TotalSeconds
			Write-Host ('stress: run {0,2} lane {1}: {2} ({3:0} s){4}' -f $r.Run, $lane, $verdict, $seconds, $(if ($reason) { " - $reason" })) `
				-ForegroundColor $(if ($verdict -eq 'PASS') { 'Green' } elseif ($verdict -eq 'SKIPPED') { 'Yellow' } else { 'Red' })
			$summary = Get-RunSummary $r.UserDir $r.Run
			if ($summary) { Write-Host "         $summary" }
			$results += [pscustomobject]@{ Run = $r.Run; Lane = $lane; Verdict = $verdict; Line = $line; Reason = $reason; Seconds = [int]$seconds; Summary = $summary }
		}
		while ($next -le $Runs -and $running.Count -lt $Lanes) {
			$lane = @(1..$Lanes | Where-Object { -not $running.ContainsKey($_) })[0]
			$laneDir = "$testRoot-stress-lane$lane"
			Clear-Channel $laneDir
			$runUserDir = New-ArxSequenceUserDir $userRoot ('run{0:00}' -f $next)
			$p = @{ Config = $Config; Sequence = $copy; LockTimeoutSec = $LockTimeoutSec; TestDir = $laneDir
				UserDir = $runUserDir
				EvidenceDir = (Join-Path $OutDir ('run{0:00}-evidence' -f $next)) } + $dataArgs
			if ($parsed.Sim) { $p.VrSim = $parsed.Sim } elseif ($parsed.Args -notcontains '--no-vr') { $p.VrSim = 'idle' }
			if ($parsed.Playground) { $p.Playground = $true }
			if ($parsed.Save) { $p.Save = $parsed.Save }
			if ($parsed.Args) { $p.ArxArgs = @($parsed.Args) }
			$j = Start-Job -ScriptBlock $job -ArgumentList $smoke, $p, (Join-Path $OutDir ('run{0:00}.log' -f $next)), $laneDir
			$running[$lane] = [pscustomobject]@{ Run = $next; Job = $j; Started = Get-Date; UserDir = $runUserDir }
			Write-Host "stress: run $next started on lane $lane"
			$next++
		}
		if ($running.Count) { Start-Sleep -Milliseconds 500 }
	}
} finally {
	if ($holders) {
		New-Item -ItemType File -Force $holderStop | Out-Null
		foreach ($h in $holders) {
			$breaks = @(Receive-Job $h -Wait -ErrorAction SilentlyContinue | Where-Object { $_ -is [int] })
			Remove-Job $h -Force -ErrorAction SilentlyContinue
			if ($breaks.Count) { Write-Host "stress: an oplock helper held $($breaks[-1]) status.json replacement(s)" }
		}
	}
	if ($busyJob) {
		# Quit the busy game through its channel; whatever is left dies with this script's job object
		try { & (Join-Path $PSScriptRoot 'game-cmd.ps1') -Dir $busyDir quit *> $null } catch { }
		$null = Wait-Job $busyJob -Timeout 60
		Remove-Job $busyJob -Force -ErrorAction SilentlyContinue
	}
}

# --- The verdict ------------------------------------------------------------------------------------------------
$results = @($results | Sort-Object Run)
$passed = @($results | Where-Object Verdict -eq 'PASS').Count
$failed = @($results | Where-Object Verdict -eq 'FAIL')
$skipped = @($results | Where-Object Verdict -eq 'SKIPPED').Count
Write-Host ''
Write-Host "stress: $name ($variantText): $passed passed, $($failed.Count) failed, $skipped skipped of $Runs"
foreach ($f in $failed) { Write-Host ('  run {0,2}: {1}' -f $f.Run, $f.Reason) }
if ($skipped) { Write-Host 'stress: SKIPPED - a run did not get the game lock; a check that did not run is not a pass' -ForegroundColor Yellow; exit 2 }
if ($NegativeControl) {
	# Caught: the run failed on a frames guard of the sequence itself, not on a step this script added
	$caught = @($failed | Where-Object { $_.Line -gt 0 -and $_.Reason -match "'@assert motion\.records\.\w+\.frames ge " }).Count
	if ($caught -eq $Runs) {
		Write-Host 'stress: PASS - every run failed on a frames guard: they see a record that did not run at its rate' -ForegroundColor Green
		exit 0
	}
	Write-Host "stress: FAIL - $($Runs - $caught) of $Runs run(s) did not fail on a frames guard with the 90 Hz block paced at 45 Hz: the guards miss it" -ForegroundColor Red
	exit 1
}
if ($failed.Count) {
	Write-Host "stress: FAIL - $($failed.Count) of $Runs run(s) failed; transcripts run<NN>.log, evidence run<NN>-evidence in $OutDir" -ForegroundColor Red
	exit 1
}
Write-Host "stress: PASS - $Runs of $Runs" -ForegroundColor Green
exit 0
