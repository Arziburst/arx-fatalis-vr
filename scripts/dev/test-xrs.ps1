<#
.SYNOPSIS
	Self-test of the .xrs sequence runner and the screenshot comparison (xrs-lib.ps1, img-diff.ps1, #113).

.DESCRIPTION
	The parser (every directive, every kind of syntax error with its line), the conditions and value paths,
	{name} values, img-diff on generated pictures, and a run of whole sequences against a fake game: a
	background process that answers the test channel files (command.txt -> ack.txt, status.json, shot
	files) - passing steps, a failing assert that stops the run, a refused command, @pending (reported
	while it fails, an error once it passes), @log / @nolog / @mark on a fake arx.log, @shot and @diff.
	Also parses every sequence shipped in scripts\dev\xrs and scripts\dev\xrs\manual. No game, no network. Exit code 0 when everything
	passed, 1 otherwise.

	#527 (written before the implementation) assumes the directive @solo in xrs-lib.ps1, as #436's c7dce4453 has it: a
	line '@solo' (no argument) is launch information, not a step; Read-ArxSequence returns Solo = $true for it and
	$false without it; '@pending #<n> @solo' is refused ('@pending needs a step that can fail').

	#528 (written before the implementation) assumes a timestamp in status.json: every status carries
	  "wallMs": <the wall clock, Unix epoch milliseconds UTC (std::chrono::system_clock), when this status was BUILT>
	(src/core/CommandChannel.cpp buildStatus; the fake game below writes it the same way). After a @log match the next
	@assert reads a status built after the read of arx.log that found the line (wallMs at or after that moment), waiting
	for one if the status on disk is older; it then judges that status once, like any @assert. A status written to disk
	after the match but built before the line (the writer thread is late, #553) does not count: its wallMs is older.

	#494 (written before the implementation) assumes a function in xrs-lib.ps1 (or test-lib.ps1, which it dot-sources):
	  Resolve-ArxRunUserDir [-UserDir <string>] [-Shared] [-Sequence <string>]  ->  the user / config dir of one
	  smoke-test.ps1 run (one string). -Sequence is the sequence's name (Read-ArxSequence's .Name, e.g. first-run);
	  empty for a run without a sequence.
	    -UserDir given        that dir, as it is (ci-smoke's per-sequence dirs, a read-back's second launch)
	    -Shared               the player's dir (Get-ArxUserDir -Shared), nothing in it touched
	    -Sequence alone       a FRESH dir: created and empty, whatever an earlier run left there, under
	                          %LOCALAPPDATA% and keyed by the working copy (Get-WorkingCopyKey), never the copy's own
	                          isolated dir (Get-ArxUserDir), which it leaves untouched
	    nothing               the copy's isolated dir (Get-ArxUserDir), untouched
	  smoke-test.ps1 resolves its user dir with it, so xrs-run.ps1 and smoke-test.ps1 -Sequence run every sequence in a
	  fresh dir, as ci-smoke.ps1 does (#296).
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'xrs-lib.ps1')
Add-Type -AssemblyName System.Drawing

$script:failures = 0
$script:checks = 0
function Check([string]$Name, [bool]$Ok, $Detail = $null) {
	$script:checks++
	if ($Ok) { Write-Host "  ok    $Name" } else { $script:failures++; Write-Host "  FAIL  $Name$(if ($null -ne $Detail) { " - got: $Detail" })" -ForegroundColor Red }
}

$work = Join-Path ([IO.Path]::GetTempPath()) "arx-test-xrs-$PID"
New-Item -ItemType Directory -Force $work | Out-Null

function Write-Seq([string]$Name, [string]$Text) {
	$file = Join-Path $work "$Name.xrs"
	[IO.File]::WriteAllText($file, $Text)
	$file
}

function Get-Parse([string]$Text) {
	$file = Write-Seq 'parse' $Text
	try { Read-ArxSequence $file } catch { "$_" }
}

# --- the parser ---------------------------------------------------------------------------------------------
Write-Host 'parser'
$seq = Get-Parse @'
# a comment
@sim idle
@args --loadlevel 10
@wait 100          # trailing comment
@frames 5
@until state eq GAMEPLAY 20s
@assert events.byName."spell.cast" ge 1
@remember yaw0 camera.yaw
@assert camera.yaw eq {yaw0+45}
@mark
@log hello (world)? 3s
@nolog \[E\]
@shot a
@diff a b gt 2.5 right
@key Escape 2 100
@pending #24 @diff a b gt 3
vr-sim head rot 25 0 0; vr-sim shot x
mark one
'@
Check 'a sequence parses' ($seq -isnot [string]) $seq
Check 'launch directives are separated from the steps' ($seq.Sim -eq 'idle' -and "$($seq.Args)" -eq '--loadlevel 10' -and ($seq.Steps | Where-Object { $_.Kind -in 'sim', 'args' }).Count -eq 0)
$oneWord = Get-Parse "@sim idle`n@args --skiplogo`n@wait 1"
Check 'a one-word @args stays one argument (#178: it splatted as "- - s k i p l o g o")' ($oneWord -isnot [string] -and $oneWord.Args -is [array] -and $oneWord.Args.Count -eq 1 -and $oneWord.Args[0] -eq '--skiplogo') "$($oneWord.Args)"
Check 'no save unless one is named' ($null -eq $seq.Save)
$saved = Get-Parse "@sim idle`n@save mvp-new-game`n@args --loadslot 0`n@frames 1"
Check '@save names the start save and is not a step' ($saved -isnot [string] -and $saved.Save -eq 'mvp-new-game' -and @($saved.Steps).Count -eq 1) $saved
Check 'no playground unless asked' (-not $seq.Playground)
$inPlayground = Get-Parse "@sim idle`n@playground`n@frames 1"
Check '@playground starts in the playground and is not a step' ($inPlayground -isnot [string] -and $inPlayground.Playground -and @($inPlayground.Steps).Count -eq 1) $inPlayground
Check 'a development launch unless @player (#394)' (-not $seq.Player)
$asPlayer = Get-Parse "@sim idle`n@player`n@until state eq LOGO 60s"
Check '@player is a player''s launch and is not a step' ($asPlayer -isnot [string] -and $asPlayer.Player -and @($asPlayer.Steps).Count -eq 1 -and -not $asPlayer.Args) $asPlayer
$badPlayer = Get-Parse "@player now"
Check '@player takes no argument' ($badPlayer -is [string] -and $badPlayer -match 'malformed') $badPlayer
# @solo (#527, ported from #436's c7dce4453): launch information - the sequence measures frame pacing and CI runs it
# alone (Read-ArxSequence returns Solo = $true; ci-smoke.ps1 gives it a phase with Solo). Not a step, no argument, and
# @pending cannot wrap it (it cannot fail).
$soloSeq = Get-Parse "@sim idle`n@solo`n@frames 1"
Check '@solo marks the sequence to run alone and is not a step' ($soloSeq -isnot [string] -and $soloSeq.Solo -eq $true -and @($soloSeq.Steps).Count -eq 1) $soloSeq
Check 'no solo unless asked' ($seq.PSObject.Properties['Solo'] -and $seq.Solo -eq $false) "$($seq.PSObject.Properties['Solo'])"
$badSolo = Get-Parse "@solo now"
Check '@solo takes no argument' ($badSolo -is [string] -and $badSolo -match 'malformed') $badSolo
$pendingSolo = Get-Parse "@sim idle`n@pending #23 @solo`n@frames 1"
Check '@pending cannot wrap @solo' ($pendingSolo -is [string] -and $pendingSolo -match 'needs a step that can fail') $pendingSolo
$kinds = ($seq.Steps | ForEach-Object Kind) -join ' '
Check 'every directive has its kind' ($kinds -eq 'wait frames until assert remember assert mark log nolog shot diff key pending command command') $kinds
Check 'the trailing comment is dropped' ($seq.Steps[0].Ms -eq 100)
Check '@until has its timeout, @assert a quoted path' ($seq.Steps[2].Seconds -eq 20 -and $seq.Steps[3].Path -eq 'events.byName."spell.cast"')
Check '@log has its regex and seconds' ($seq.Steps[7].Regex -eq 'hello (world)?' -and $seq.Steps[7].Seconds -eq 3)
Check '@diff has its eye' ($seq.Steps[10].Eye -eq 'right' -and $seq.Steps[10].Value -eq '2.5')
$metric = Get-Parse "@diff a b max lt 64 left-layer`n@diff a b lt 1"
Check '@diff takes the max metric and a layer (#72), mean by default' ($metric -isnot [string] -and $metric.Steps[0].Metric -eq 'max' -and $metric.Steps[0].Eye -eq 'left-layer' -and $metric.Steps[0].Value -eq '64' -and $metric.Steps[1].Metric -eq 'mean' -and $metric.Steps[1].Eye -eq 'left') $metric
Check '@key has its count and delay' ($seq.Steps[11].Count -eq 2 -and $seq.Steps[11].DelayMs -eq 100)
Check '@pending wraps a step' ($seq.Steps[12].Issue -eq 24 -and $seq.Steps[12].Inner.Kind -eq 'diff')
Check "';' groups commands into one batch" ($seq.Steps[13].Commands.Count -eq 2 -and $seq.Steps[14].Commands.Count -eq 1)

$bad = Get-Parse "@wait soon`n@frobnicate 1`n@assert x between 1 2`n@key NoSuchKey`n@pending 24 @wait 1`n@log (`n@diff a b gt many`n@pending #1 @pending #2 mark x`nmark fine"
Check 'syntax errors are collected before anything runs' ($bad -is [string] -and $bad -match '8 error') $bad
foreach ($line in 1..8) { Check "  the error names line $line" ($bad -match "parse.xrs:$line ") }
Check 'a timeout without its s is refused, not taken for a value' ((Get-Parse '@until state eq GAMEPLAY 60') -match 'malformed')
Check 'an @assert cannot have a timeout' ((Get-Parse '@assert a eq 1 5s') -match 'no timeout')
Check 'a file without steps is refused' ((Get-Parse "# nothing`n") -match 'no steps')
Check 'an unknown sequence names the known ones' ($(try { Resolve-ArxSequence 'no-such-sequence' } catch { "$_" }) -match 'look-around')

# --- conditions and paths -------------------------------------------------------------------------------------
Write-Host 'conditions'
Check 'numbers compare as numbers' ((Test-XrsCondition 10 'gt' '9') -and -not (Test-XrsCondition 10 'lt' '9') -and (Test-XrsCondition 1.50 'eq' '1.5') -and (Test-XrsCondition 3 'ne' '4') -and (Test-XrsCondition 3 'le' '3') -and (Test-XrsCondition 3 'ge' '3'))
Check 'text compares as text, case-insensitive' ((Test-XrsCondition 'GAMEPLAY' 'eq' 'gameplay') -and (Test-XrsCondition 'MENU' 'ne' 'GAMEPLAY') -and -not (Test-XrsCondition 'MENU' 'gt' 'A'))
Check 'booleans compare as true / false' ((Test-XrsCondition $true 'eq' 'true') -and (Test-XrsCondition $false 'eq' 'False') -and (Test-XrsCondition $false 'ne' 'true'))
Check 'an absent value satisfies nothing' (-not (Test-XrsCondition $null 'eq' '0') -and -not (Test-XrsCondition $null 'ne' '0'))
$obj = '{"a":{"b":{"c":5},"list":1},"events":{"byName":{"spell.cast":2}},"flag":true}' | ConvertFrom-Json
Check 'dotted and quoted paths' ((Get-XrsValue $obj 'a.b.c') -eq 5 -and (Get-XrsValue $obj 'events.byName."spell.cast"') -eq 2 -and $null -eq (Get-XrsValue $obj 'a.x.y') -and (Get-XrsValue $obj 'flag') -eq $true)
$ctx = New-XrsContext $work $null
$ctx.Vars['yaw0'] = 10.5
Check '{name} and {name+N}' ((Expand-XrsValue $ctx '{yaw0}') -eq '10.5' -and (Expand-XrsValue $ctx '{yaw0+30}') -eq '40.5' -and (Expand-XrsValue $ctx '{yaw0-0.5}') -eq '10' -and (Expand-XrsValue $ctx '7') -eq '7')
Check '{name*K}: a relative threshold' ((Expand-XrsValue $ctx '{yaw0*2}') -eq '21' -and (Expand-XrsValue $ctx '{yaw0*0.9}') -eq '9.45')
Check 'an unknown {name} is an error' ($(try { Expand-XrsValue $ctx '{nope}' } catch { "$_" }) -match "no remembered value 'nope'")

# --- img-diff --------------------------------------------------------------------------------------------------
Write-Host 'img-diff'
function New-Bmp([string]$Path, [int]$R, [int]$G, [int]$B, [int]$HalfR = -1) {
	$bmp = [Drawing.Bitmap]::new(40, 20, [Drawing.Imaging.PixelFormat]::Format24bppRgb)
	$gfx = [Drawing.Graphics]::FromImage($bmp)
	$gfx.Clear([Drawing.Color]::FromArgb($R, $G, $B))
	if ($HalfR -ge 0) { $gfx.FillRectangle([Drawing.SolidBrush]::new([Drawing.Color]::FromArgb($HalfR, $G, $B)), 0, 0, 20, 20) }
	$gfx.Dispose()
	$bmp.Save($Path, [Drawing.Imaging.ImageFormat]::Bmp)
	$bmp.Dispose()
}
$imgDiff = Join-Path $PSScriptRoot 'img-diff.ps1'
New-Bmp (Join-Path $work 'a.bmp') 100 100 100
New-Bmp (Join-Path $work 'same.bmp') 100 100 100
New-Bmp (Join-Path $work 'plus10.bmp') 110 110 110
New-Bmp (Join-Path $work 'redhalf.bmp') 100 100 100 130
$d = & $imgDiff -A (Join-Path $work 'a.bmp') -B (Join-Path $work 'same.bmp') -PassThru
Check 'identical pictures: mean 0' ($d.Mean -eq 0 -and $d.Max -eq 0 -and $d.PctChanged -eq 0) ($d | ConvertTo-Json -Compress)
$d = & $imgDiff -A (Join-Path $work 'a.bmp') -B (Join-Path $work 'plus10.bmp') -PassThru
Check '+10 on every channel: mean 10, all channels over the threshold' ($d.Mean -eq 10 -and $d.Max -eq 10 -and $d.PctChanged -eq 100) ($d | ConvertTo-Json -Compress)
$d = & $imgDiff -A (Join-Path $work 'a.bmp') -B (Join-Path $work 'redhalf.bmp') -PassThru
Check 'red +30 on half of the picture: mean 5, a third of the channels of half the pixels' ($d.Mean -eq 5 -and $d.Max -eq 30 -and $d.PctChanged -eq 16.67) ($d | ConvertTo-Json -Compress)
$text = (& $imgDiff -A (Join-Path $work 'a.bmp') -B (Join-Path $work 'redhalf.bmp') -Grid 2) -join "`n"
Check 'the grid report names the changed half' ($text -match 'mean-abs-diff=5' -and $text -match 'coverage=50%') $text
New-Bmp (Join-Path $work 'other-size.bmp') 100 100 100
$big = [Drawing.Bitmap]::new(10, 10); $big.Save((Join-Path $work 'tiny.bmp'), [Drawing.Imaging.ImageFormat]::Bmp); $big.Dispose()
Check 'pictures of different sizes are refused' ($(try { & $imgDiff -A (Join-Path $work 'a.bmp') -B (Join-Path $work 'tiny.bmp') -PassThru; 'no error' } catch { "$_" }) -match 'SIZE MISMATCH')

# --- a whole run against a fake game ---------------------------------------------------------------------------
Write-Host 'a run against a fake game'
$testDir = Join-Path $work 'channel'
New-Item -ItemType Directory -Force (Join-Path $testDir 'capture') | Out-Null
$logPath = Join-Path $work 'arx.log'
[IO.File]::WriteAllText($logPath, "[I] old line before the sequence`n")

# The fake game answers command.txt like CommandChannel.cpp: ack.txt, status.json, shots
$fake = {
	param($Dir, $Source, $LogPath)
	# First thing: tell the test the job process is up. Start-Job's own startup can take long on a busy PC (#446).
	[IO.File]::WriteAllText((Join-Path $Dir 'started'), '1')
	Add-Type -AssemblyName System.Drawing
	$seq = 0; $frame = 0; $capture = 0; $yaw = 0.0; $shots = [ordered]@{}; $errors = 0
	# fake-scene-skip (#528): the scene skips logged so far (events.byName."vr.skip"), and a skip asked for by the batch
	$skips = 0; $sceneSkip = $false
	# A status as the game builds it: wallMs is the wall clock when it was BUILT (#528), not when it reached the disk
	$build = {
		@{ pid = 1; state = 'GAMEPLAY'; frame = $frame; errors = $errors; cmdSeq = $seq; captureSeq = $capture
			camera = @{ yaw = $yaw }; player = @{ controlsBlocked = $false }; events = @{ byName = @{ 'spell.cast' = 2; 'vr.skip' = $skips } }
			wallMs = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds() } | ConvertTo-Json -Depth 6 -Compress
	}
	$write = {
		param($Json)
		[IO.File]::WriteAllText((Join-Path $Dir 'status.json.tmp'), $Json)
		[IO.File]::Move((Join-Path $Dir 'status.json.tmp'), (Join-Path $Dir 'status.json'), $true)  # atomic replace, like the real game (#446)
	}
	# The running batch: kept while a waitframes in it holds the ack (#522), the rest of its lines run once the frames are there
	$batch = $null
	$stop = Join-Path $Dir 'stop'
	while (-not (Test-Path $stop)) {
		$cmd = Join-Path $Dir 'command.txt'
		if ($null -eq $batch -and (Test-Path $cmd)) {
			# A transient sharing violation (the test's own read, a scanner on the runner) must not lose the batch:
			# the file is removed only after it was read, and a failed read is tried again on the next pass (#275).
			$lines = $null
			try { $lines = @([IO.File]::ReadAllLines($cmd)); Remove-Item $cmd -Force -ErrorAction Stop } catch { $lines = $null }
			if ($null -eq $lines) { Start-Sleep -Milliseconds 30; continue }
			$batch = @{ Lines = @($lines | ForEach-Object { $_.Trim() } | Where-Object { $_ }); Next = 0; Errors = 0; Last = ''; Resume = 0 }
		}
		if ($batch -and $frame -ge $batch.Resume) {
			$waiting = $false
			while (-not $waiting -and $batch.Next -lt $batch.Lines.Count) {
				$l = $batch.Lines[$batch.Next]; $batch.Next++
				$batch.Last = $l
				if ($l -match '^waitframes (\d+)$') { $batch.Resume = $frame + [int]$Matches[1]; $waiting = $true }
				elseif ($l -match '^waitframes') { $batch.Errors++ }
				elseif ($l -match '^shot (\S+)') {
					$capture++
					$bmp = [Drawing.Bitmap]::new(40, 20, [Drawing.Imaging.PixelFormat]::Format24bppRgb)
					$g = [Drawing.Graphics]::FromImage($bmp); $g.Clear([Drawing.Color]::FromArgb(100 + [int]($yaw * 0.3), 100, 100)); $g.Dispose()
					$bmp.Save((Join-Path $Dir "capture\$($Matches[1]).bmp"), [Drawing.Imaging.ImageFormat]::Bmp); $bmp.Dispose()
				}
				elseif ($l -match '^vr-sim head rot (\S+)') { $yaw = [double]$Matches[1] }
				elseif ($l -match '^refuse') { $batch.Errors++ }
				elseif ($l -match '^fake-scene-skip$') { $sceneSkip = $true }
			}
			if (-not $waiting) {
				$seq++
				# The test reads ack.txt every 10 ms (#522); a write that hits its open handle is retried, never dropped (#275).
				for ($try = 0; $try -lt 50; $try++) {
					try { [IO.File]::WriteAllText((Join-Path $Dir 'ack.txt'), "seq=$seq frame=$frame errors=$($batch.Errors) last=$($batch.Last) error=-"); break } catch { Start-Sleep -Milliseconds 20 }
				}
				$batch = $null
			}
		}
		$frame += 3
		if ($sceneSkip) {
			# The #528 race, as CI run 39121707703 had it: the frame's status is built, THEN the skip's event line is
			# logged and reaches arx.log; the writer thread puts that older status on disk 400 ms later (after any
			# @log poll has seen the line), then the game stalls 2 s (no status, no ack). Only the status after the
			# stall counts the skip.
			$sceneSkip = $false
			$stale = & $build
			[IO.File]::AppendAllText($LogPath, "[I] [event] vr.skip what=SCENE`n")
			Start-Sleep -Milliseconds 400
			& $write $stale
			Start-Sleep -Milliseconds 2000
			$skips++
		}
		& $write (& $build)
		Start-Sleep -Milliseconds 30
	}
}
$job = Start-Job -ScriptBlock $fake -ArgumentList $testDir, $PSScriptRoot, $logPath
try {
	# The job's process start is not what is tested: wait for its marker (up to 3 min, or the job ending), then time the 15 s.
	$startDeadline = (Get-Date).AddMinutes(3)
	while (-not (Test-Path (Join-Path $testDir 'started')) -and $job.State -eq 'Running' -and (Get-Date) -lt $startDeadline) { Start-Sleep -Milliseconds 100 }
	$deadline = (Get-Date).AddSeconds(15)
	$firstStatus = $null
	while (-not ($firstStatus = Read-ArxStatus $testDir) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 100 }
	# The check uses the status the loop parsed: a second read could hit a rewrite in progress (#446).
	Check 'the fake game answers' ([bool]$firstStatus)
	$quiet = { param($Text, $Color) }

	function Run-Seq([string]$Text, [switch]$ContinueOnError) {
		$file = Write-Seq 'run' $Text
		Invoke-ArxSequence (Read-ArxSequence $file) -TestDir $testDir -LogPath $logPath -ContinueOnError:$ContinueOnError -Out $quiet
	}

	$r = Run-Seq @'
@until state eq GAMEPLAY 5s
@assert player.controlsBlocked eq false
@assert events.byName."spell.cast" ge 2
@remember yaw0 camera.yaw
vr-sim head rot 40 0 0
@frames 6
@assert camera.yaw eq {yaw0+40}
mark hello; mark again
@wait 50
'@
	Check 'a passing sequence passes' ($r.Passed -and $r.Ran -eq 9 -and $r.Failures.Count -eq 0) ($r | ConvertTo-Json -Compress)

	# @frames is the channel's waitframes (#522): the game holds the ack until it rendered the frames
	$framesOut = [Collections.Generic.List[string]]::new()
	$framesCtx = New-XrsContext $testDir $logPath { param($Text, $Color) $framesOut.Add($Text) }
	$frameBefore = [int64](Read-XrsStatus $testDir).frame
	Invoke-XrsStep $framesCtx (Read-ArxSequence (Write-Seq 'frames' '@frames 30')).Steps[0]
	$framesAck = @($framesOut | Where-Object { $_ -match 'seq=\d+ frame=' })[0]
	$ackFrame = if ($framesAck -match 'frame=(\d+)') { [int64]$Matches[1] } else { -1 }
	Check '@frames sends waitframes and its ack comes 30 or more frames later' ($framesAck -match 'last=waitframes 30 ' -and $ackFrame -ge $frameBefore + 30) "$framesAck (frame before: $frameBefore)"
	$ack = Send-ArxCommand -Lines @('waitframes 9', 'vr-sim head rot 7 0 0') -Dir $testDir
	$turned = try { [bool](Wait-ArxStatus { param($s) $s.camera.yaw -eq 7 } -TimeoutSec 5 -Dir $testDir) } catch { $false }
	Check 'the lines after a waitframes run once the frames are there, the ack after them' ($ack -match 'errors=0 last=vr-sim head rot 7 0 0 ' -and $turned) $ack
	# Back to the yaw the checks below expect
	$null = Send-ArxCommand -Lines @('vr-sim head rot 40 0 0') -Dir $testDir
	$null = Wait-ArxStatus { param($s) $s.camera.yaw -eq 40 } -TimeoutSec 5 -Dir $testDir

	$r = Run-Seq "@assert camera.yaw eq 999`n@wait 10"
	Check 'a failed assert stops the run and names the value' (-not $r.Passed -and $r.Ran -eq 1 -and $r.Failures[0] -match 'ASSERT FAILED: camera.yaw \(40\) eq 999') ($r | ConvertTo-Json -Compress)
	$r = Run-Seq "@assert camera.yaw eq 999`n@assert nope.x eq 1`n@wait 10" -ContinueOnError
	Check '-ContinueOnError runs on and lists every failure' (-not $r.Passed -and $r.Ran -eq 3 -and $r.Failures.Count -eq 2 -and $r.Failures[1] -match "nope.x \(<absent>\)") ($r | ConvertTo-Json -Compress)
	$r = Run-Seq "refuse this`n@wait 10"
	Check 'a command the game refuses fails the step' (-not $r.Passed -and $r.Ran -eq 1 -and $r.Failures[0] -match 'refused') ($r | ConvertTo-Json -Compress)
	$r = Run-Seq "@until camera.yaw gt 1000 1s"
	Check '@until times out with the last value' (-not $r.Passed -and $r.Failures[0] -match 'UNTIL FAILED: camera.yaw \(40\) never became gt 1000 within 1 s') ($r | ConvertTo-Json -Compress)

	$r = Run-Seq "@pending #32 @assert camera.yaw eq 999`n@assert camera.yaw eq 40"
	Check '@pending: a failing step is reported pending, the run passes' ($r.Passed -and $r.Pending.Count -eq 1 -and $r.Pending[0] -match '^#32: @assert camera.yaw eq 999') ($r | ConvertTo-Json -Compress)
	$r = Run-Seq "@pending #32 @assert camera.yaw eq 40"
	Check '@pending: a step that passes fails the run until the @pending is removed' (-not $r.Passed -and $r.Failures[0] -match 'PENDING PASSED: #32 now works') ($r | ConvertTo-Json -Compress)

	# the log: marks, order, absence
	$append = { param($t) [IO.File]::AppendAllText($logPath, $t) }
	$r = Run-Seq "@log never appears 1s"
	Check '@log fails when nothing matches in time' (-not $r.Passed -and $r.Failures[0] -match 'LOG FAILED') ($r | ConvertTo-Json -Compress)
	$r = Run-Seq "@log old line 1s"
	Check '@log ignores what the log held before the sequence' (-not $r.Passed) ($r | ConvertTo-Json -Compress)
	$ctx = New-XrsContext $testDir $logPath { param($t, $c) }
	& $append "[I] first`n[I] second`n[E] trouble`n"
	$logSteps = (Read-ArxSequence (Write-Seq 'log' "@log first 1s`n@log second 1s`n@nolog \[E\]")).Steps
	Invoke-XrsStep $ctx $logSteps[0]; Invoke-XrsStep $ctx $logSteps[1]
	Check '@log: consecutive lines assert an order' $true
	Check '@nolog finds a line after the mark' ($(try { Invoke-XrsStep $ctx $logSteps[2]; 'passed' } catch { "$_" }) -match 'NOLOG FAILED.*\[E\]')
	$ctx2 = New-XrsContext $testDir $logPath { param($t, $c) }
	& $append "[I] later`n"
	Check '@nolog passes once the mark is past the line' ($(try { Invoke-XrsStep $ctx2 $logSteps[2]; 'passed' } catch { "$_" }) -eq 'passed')
	$orderSteps = (Read-ArxSequence (Write-Seq 'order' "@log second 1s`n@log first 1s")).Steps
	$ctx3 = New-XrsContext $testDir $logPath { param($t, $c) }
	$ctx3.LogMark = 0
	Invoke-XrsStep $ctx3 $orderSteps[0]
	Check '@log: a line before the matched one cannot match later' ($(try { Invoke-XrsStep $ctx3 $orderSteps[1]; 'passed' } catch { "$_" }) -match 'LOG FAILED')
	# A pattern of look-aheads after another @log (#632): it matches with zero width where the last one stopped - on
	# that line's new line - and passes; a look-ahead that finds nothing still fails
	$aheadSteps = (Read-ArxSequence (Write-Seq 'ahead' "@log one 1s`n@log (?s)^(?=.*three)(?=.*two) 1s`n@log (?s)^(?=.*four) 1s")).Steps
	$ctx4 = New-XrsContext $testDir $logPath { param($t, $c) }
	& $append "[I] one`n[I] two`n[I] three`n"
	Invoke-XrsStep $ctx4 $aheadSteps[0]
	Check '@log: look-aheads right after another @log match' ($(try { Invoke-XrsStep $ctx4 $aheadSteps[1]; 'passed' } catch { "$_" }) -eq 'passed')
	Check '  ... and one that finds nothing fails' ($(try { Invoke-XrsStep $ctx4 $aheadSteps[2]; 'passed' } catch { "$_" }) -match 'LOG FAILED')

	# @assert right after a @log match (#528): the status on disk was built before the matched line (the fake's
	# fake-scene-skip, see above). The @assert must judge a status built after the line (wallMs), not that one.
	Write-Host '@assert after @log (#528)'
	$r = Run-Seq "fake-scene-skip`n@log vr\.skip what=SCENE 5s`n@assert events.byName.`"vr.skip`" eq 1"
	Check '@assert after a @log match reads a status built after the matched line' ($r.Passed -and $r.Ran -eq 3) ($r | ConvertTo-Json -Compress)
	# Still an @assert, not an @until: it judges that newer status once - a wrong value fails, and names the newer value
	$r = Run-Seq "fake-scene-skip`n@log vr\.skip what=SCENE 5s`n@assert events.byName.`"vr.skip`" eq 99"
	Check '  a wrong value still fails, judged on the newer status' (-not $r.Passed -and $r.Ran -eq 3 -and $r.Failures[0] -match 'ASSERT FAILED: events\.byName\."vr\.skip" \(2\) eq 99') ($r | ConvertTo-Json -Compress)

	# shots and the comparison
	$r = Run-Seq @'
vr-sim head rot 0 0 0
@shot still
@shot still2
@diff still still2 lt 0.5
vr-sim head rot 50 0 0
@shot turned
@diff still turned gt 3
@diff still turned lt 6
'@
	Check '@shot and @diff: still against still is quiet, a turn shows' ($r.Passed) ($r | ConvertTo-Json -Compress)
	$r = Run-Seq "vr-sim head rot 0 0 0`n@shot still`n@diff still turned lt 1"
	Check '@diff fails when the pictures differ more than allowed' (-not $r.Passed -and $r.Failures[0] -match 'DIFF FAILED: still vs turned \(left\) mean difference 5 lt 1') ($r | ConvertTo-Json -Compress)
	$r = Run-Seq "@diff still nothing-taken gt 1"
	Check '@diff names a missing shot' (-not $r.Passed -and $r.Failures[0] -match "no shot 'nothing-taken'") ($r | ConvertTo-Json -Compress)
} finally {
	New-Item -ItemType File (Join-Path $testDir 'stop') -Force | Out-Null
	$null = Wait-Job $job -Timeout 10
	Remove-Job $job -Force
}

# --- the shipped sequences -------------------------------------------------------------------------------------
Write-Host 'shipped sequences'
# xrs\manual (#529) is parsed too (parse and @save checks), never run in CI
$shipped = @(Get-ChildItem (Join-Path $PSScriptRoot 'xrs') -Filter '*.xrs') + @(Get-ChildItem (Join-Path $PSScriptRoot 'xrs\manual') -Filter '*.xrs')
Check 'sequences are shipped' ($shipped.Count -ge 4) $shipped.Count
foreach ($file in $shipped) {
	$parsed = try { Read-ArxSequence $file.FullName } catch { "$_" }
	Check "  $($file.Name) parses" ($parsed -isnot [string]) $parsed
	if ($parsed -isnot [string]) {
		# A simulator script, or a flat run said outright (--no-vr in @args, stand-up.xrs #263); the start is its @args
		# or the test playground (@playground, #321), or a player's launch (@player, #394: logos, then the main menu)
		Check "  $($file.Name) names its start" (($parsed.Sim -or $parsed.Args -contains '--no-vr') -and ($parsed.Args -or $parsed.Playground -or $parsed.Player))
		# CI gives every sequence a fresh user dir (#296): a slot to load exists only when the sequence says which save
		Check "  $($file.Name) declares the @save its --loadslot needs" (-not ($parsed.Args -contains '--loadslot') -or [bool]$parsed.Save)
		Check "  $($file.Name) names the issue behind every @pending" (@($parsed.Steps | Where-Object { $_.Kind -eq 'pending' -and $_.Issue -gt 0 }).Count -eq @($parsed.Steps | Where-Object Kind -eq 'pending').Count)
	}
}
Check 'the simulator script the sequences use exists' (Test-Path (Join-Path $PSScriptRoot 'vrsim\idle.vrsim'))

# --- a fresh user dir per sequence (#296) ------------------------------------------------------------------------
Write-Host 'fresh user dir per sequence'
# Two "sequences": the first leaves a file behind (a saved file, a controls ini), the second fails when it sees it
$ciDirs = Join-Path $work 'ci-userdirs'
$second = { param($dir) if (Test-Path (Join-Path $dir 'vr-controls-index.ini')) { 1 } else { 0 } }
$dirA = New-ArxSequenceUserDir $ciDirs 'vr-remap'
Set-Content (Join-Path $dirA 'vr-controls-index.ini') 'left over'
# One shared dir (the old behaviour): the second sequence sees the file of the first
Check 'a shared dir leaks the first sequence into the second' ((& $second $dirA) -eq 1)
# A fresh dir per sequence: it does not
$dirB = New-ArxSequenceUserDir $ciDirs 'vr-settings-flat'
Check 'the second sequence gets its own dir' ($dirB -ne $dirA)
Check 'the second sequence does not see the first one''s file' ((& $second $dirB) -eq 0)
# The same name again (a re-run) starts empty too
$dirA2 = New-ArxSequenceUserDir $ciDirs 'vr-remap'
Check 'a re-run of a sequence starts from an empty dir' ($dirA2 -eq $dirA -and -not (Test-Path (Join-Path $dirA2 'vr-controls-index.ini')))
$ciSource = Get-Content -Raw (Join-Path $PSScriptRoot 'ci-smoke.ps1')
Check 'ci-smoke runs every sequence in such a dir' ($ciSource -match 'New-ArxSequenceUserDir' -and $ciSource -match '\$p\.UserDir = \$seqUserDir')

# --- a fresh user dir per LOCAL sequence run too (#494) --------------------------------------------------------------
# first-run.xrs turns the crosshair off and the game saves it: in the copy's one shared user dir the next local run of
# any sequence starts with show_crosshair=false (first-run's own first check then fails). The interface is at the top.
Write-Host 'fresh user dir per local sequence run (#494)'
$savedLocalAppData = $env:LOCALAPPDATA
$fakeLocal = Join-Path $work 'localappdata'
$fakeShared = Join-Path $work 'saved-games\Arx Libertatis'
$fakeSharedDir = $fakeShared
New-Item -ItemType Directory -Force $fakeLocal, $fakeShared | Out-Null
# Nothing here may touch the real %LOCALAPPDATA% or the player's Saved Games
$env:LOCALAPPDATA = $fakeLocal
$realSharedUserDir = ${function:Get-SharedUserDir}
function Get-SharedUserDir { $fakeSharedDir }
try {
	$hasResolver = [bool](Get-Command Resolve-ArxRunUserDir -CommandType Function -ErrorAction SilentlyContinue)
	Check 'Resolve-ArxRunUserDir exists (xrs-lib.ps1 / test-lib.ps1)' $hasResolver 'no such function'
	if ($hasResolver) {
		$isolated = Get-ArxUserDir
		New-Item -ItemType Directory -Force $isolated | Out-Null
		Set-Content (Join-Path $isolated 'keep.txt') 'the copy''s own user dir: run-windows -Isolated, -Saves'
		Set-Content (Join-Path $fakeShared 'keep.txt') 'the player''s saves'
		$key = Get-WorkingCopyKey $ArxSourceDir

		$plain = Resolve-ArxRunUserDir
		Check 'a run without a sequence keeps the copy''s isolated dir' ($plain -eq $isolated -and (Test-Path (Join-Path $isolated 'keep.txt'))) $plain

		# first-run, as it leaves its dir behind
		$first = Resolve-ArxRunUserDir -Sequence 'first-run'
		Check 'a sequence run gets a dir that exists and is empty' ($first -is [string] -and (Test-Path -LiteralPath $first -PathType Container) -and @(Get-ChildItem -LiteralPath $first -Force).Count -eq 0) $first
		Check '  not the copy''s isolated dir' ($first -and [IO.Path]::GetFullPath($first).TrimEnd('\') -ne [IO.Path]::GetFullPath($isolated).TrimEnd('\')) $first
		Check '  under %LOCALAPPDATA%, keyed by the working copy' ($first -and [IO.Path]::GetFullPath($first).StartsWith([IO.Path]::GetFullPath($fakeLocal).TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase) -and $first -like "*$key*") $first
		if ($first -and (Test-Path -LiteralPath $first)) {
			Set-Content (Join-Path $first 'cfg.ini') "[interface]`nshow_crosshair=false"
			New-Item -ItemType Directory -Force (Join-Path $first 'save') | Out-Null
		}

		# the next local run: another sequence, then first-run again
		$next = Resolve-ArxRunUserDir -Sequence 'look-around'
		Check 'the next sequence does not see first-run''s cfg.ini' ($next -and (Test-Path -LiteralPath $next -PathType Container) -and @(Get-ChildItem -LiteralPath $next -Force).Count -eq 0) $next
		if ($next -and (Test-Path -LiteralPath $next)) { Set-Content (Join-Path $next 'cfg.ini') "[interface]`nshow_crosshair=false" }
		$again = Resolve-ArxRunUserDir -Sequence 'first-run'
		Check 'a re-run of first-run starts from an empty dir' ($again -and (Test-Path -LiteralPath $again -PathType Container) -and @(Get-ChildItem -LiteralPath $again -Force).Count -eq 0) $again

		# What must stay as it is: a dir given by the caller (ci-smoke, a read-back's second launch) and the player's dir
		$given = Join-Path $work 'ci-userdirs\vr-remap'
		New-Item -ItemType Directory -Force $given | Out-Null
		Set-Content (Join-Path $given 'cfg.ini') 'what the first launch wrote'
		$got = Resolve-ArxRunUserDir -UserDir $given -Sequence 'vr-remap-readback'
		Check '-UserDir is used as it is, its files kept' ($got -eq $given -and (Test-Path (Join-Path $given 'cfg.ini'))) $got
		$got = Resolve-ArxRunUserDir -Shared -Sequence 'first-run'
		Check '-Shared is the player''s dir, nothing in it deleted' ($got -eq $fakeShared -and (Test-Path (Join-Path $fakeShared 'keep.txt'))) $got
		Check 'the copy''s isolated dir survived every sequence run' (Test-Path (Join-Path $isolated 'keep.txt'))
	}
} finally {
	$env:LOCALAPPDATA = $savedLocalAppData
	${function:Get-SharedUserDir} = $realSharedUserDir
}
$smokeSource = Get-Content -Raw (Join-Path $PSScriptRoot 'smoke-test.ps1')
Check 'smoke-test.ps1 (and so xrs-run.ps1) resolves its user dir with Resolve-ArxRunUserDir' ($smokeSource -match '(?m)^[^#\r\n]*\bResolve-ArxRunUserDir\b')

Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
Write-Host ''
if ($script:failures) { Write-Host "test-xrs: $($script:failures) of $($script:checks) checks FAILED" -ForegroundColor Red; exit 1 }
Write-Host "test-xrs: all $($script:checks) checks passed" -ForegroundColor Green
exit 0
