<#
.SYNOPSIS
	Self-test of the .xrs sequence runner and the screenshot comparison (xrs-lib.ps1, img-diff.ps1, #113).

.DESCRIPTION
	The parser (every directive, every kind of syntax error with its line), the conditions and value paths,
	{name} values, img-diff on generated pictures, and a run of whole sequences against a fake game: a
	background process that answers the test channel files (command.txt -> ack.txt, status.json, shot
	files) - passing steps, a failing assert that stops the run, a refused command, @pending (reported
	while it fails, an error once it passes), @log / @nolog / @mark on a fake arx.log, @shot and @diff.
	Also parses every sequence shipped in scripts\dev\xrs. No game, no network. Exit code 0 when everything
	passed, 1 otherwise.
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
$kinds = ($seq.Steps | ForEach-Object Kind) -join ' '
Check 'every directive has its kind' ($kinds -eq 'wait frames until assert remember assert mark log nolog shot diff key pending command command') $kinds
Check 'the trailing comment is dropped' ($seq.Steps[0].Ms -eq 100)
Check '@until has its timeout, @assert a quoted path' ($seq.Steps[2].Seconds -eq 20 -and $seq.Steps[3].Path -eq 'events.byName."spell.cast"')
Check '@log has its regex and seconds' ($seq.Steps[7].Regex -eq 'hello (world)?' -and $seq.Steps[7].Seconds -eq 3)
Check '@diff has its eye' ($seq.Steps[10].Eye -eq 'right' -and $seq.Steps[10].Value -eq '2.5')
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
	param($Dir, $Source)
	Add-Type -AssemblyName System.Drawing
	$seq = 0; $frame = 0; $capture = 0; $yaw = 0.0; $shots = [ordered]@{}; $errors = 0
	$stop = Join-Path $Dir 'stop'
	while (-not (Test-Path $stop)) {
		$last = ''; $lineErrors = 0
		$cmd = Join-Path $Dir 'command.txt'
		if (Test-Path $cmd) {
			$lines = @(Get-Content $cmd)
			Remove-Item $cmd -Force
			foreach ($l in $lines) {
				$l = $l.Trim(); if (-not $l) { continue }
				$last = $l
				if ($l -match '^shot (\S+)') {
					$capture++
					$bmp = [Drawing.Bitmap]::new(40, 20, [Drawing.Imaging.PixelFormat]::Format24bppRgb)
					$g = [Drawing.Graphics]::FromImage($bmp); $g.Clear([Drawing.Color]::FromArgb(100 + [int]($yaw * 0.3), 100, 100)); $g.Dispose()
					$bmp.Save((Join-Path $Dir "capture\$($Matches[1]).bmp"), [Drawing.Imaging.ImageFormat]::Bmp); $bmp.Dispose()
				}
				elseif ($l -match '^vr-sim head rot (\S+)') { $yaw = [double]$Matches[1] }
				elseif ($l -match '^refuse') { $lineErrors++ }
			}
			$seq++
			Set-Content (Join-Path $Dir 'ack.txt') "seq=$seq frame=$frame errors=$lineErrors last=$last error=-"
		}
		$frame += 3
		$status = @{ pid = 1; state = 'GAMEPLAY'; frame = $frame; errors = $errors; cmdSeq = $seq; captureSeq = $capture
			camera = @{ yaw = $yaw }; player = @{ controlsBlocked = $false }; events = @{ byName = @{ 'spell.cast' = 2 } } }
		$json = $status | ConvertTo-Json -Depth 6 -Compress
		[IO.File]::WriteAllText((Join-Path $Dir 'status.json.tmp'), $json)
		Move-Item (Join-Path $Dir 'status.json.tmp') (Join-Path $Dir 'status.json') -Force
		Start-Sleep -Milliseconds 30
	}
}
$job = Start-Job -ScriptBlock $fake -ArgumentList $testDir, $PSScriptRoot
try {
	$deadline = (Get-Date).AddSeconds(15)
	while (-not (Read-ArxStatus $testDir) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 100 }
	Check 'the fake game answers' ([bool](Read-ArxStatus $testDir))
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
$shipped = @(Get-ChildItem (Join-Path $PSScriptRoot 'xrs') -Filter '*.xrs')
Check 'sequences are shipped' ($shipped.Count -ge 4) $shipped.Count
foreach ($file in $shipped) {
	$parsed = try { Read-ArxSequence $file.FullName } catch { "$_" }
	Check "  $($file.Name) parses" ($parsed -isnot [string]) $parsed
	if ($parsed -isnot [string]) {
		Check "  $($file.Name) names its start" ($parsed.Sim -and $parsed.Args)
		Check "  $($file.Name) names the issue behind every @pending" (@($parsed.Steps | Where-Object { $_.Kind -eq 'pending' -and $_.Issue -gt 0 }).Count -eq @($parsed.Steps | Where-Object Kind -eq 'pending').Count)
	}
}
Check 'the simulator script the sequences use exists' (Test-Path (Join-Path $PSScriptRoot 'vrsim\idle.vrsim'))

Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
Write-Host ''
if ($script:failures) { Write-Host "test-xrs: $($script:failures) of $($script:checks) checks FAILED" -ForegroundColor Red; exit 1 }
Write-Host "test-xrs: all $($script:checks) checks passed" -ForegroundColor Green
exit 0
