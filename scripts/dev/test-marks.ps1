<#
.SYNOPSIS
	Self-test of scripts/dev/marks.ps1 (#551, marks v2 #574) on a fixture marks folder: no game, no headset.

.DESCRIPTION
	The marks the game writes when both Index trackpads are pressed in VR (tests/vr/MarkMomentTest.h and
	tests/vr/MarkHoldTest.h have the game's side; scripts/dev/xrs/mark-moment.xrs checks a real one). Since #574 a
	short press writes left.png, right.png, recorder.txt and meta.txt only; a hold (up to 1 minute) adds clip.vrsim
	(10 s before the press to the release, a "# hold: <pressMs> <releaseMs>" header line), snap-+NN.Ns.jpg (the offset
	from the press), note.wav, and transcript.txt once transcribed (scripts/dev/transcribe.ps1; test-transcribe.ps1).
	meta.txt adds kind=press|hold, pressMs=, releaseMs= (the clip's clock). The #551 part of the listing below stays;
	the #574 part (written before the implementation):

	marks.ps1 [-UserDir <dir>] [-ToolsDir <dir>] [-Transcribe] [-Copy ...]
	  - -ToolsDir: transcribe.ps1's tools directory (default %LOCALAPPDATA%\ArxFatalisVR\tools). When the tools are
	    cached there, notes without transcript.txt are transcribed before the listing; when they are not, nothing is
	    fetched (the tools directory stays as it was) unless -Transcribe is given. This test always passes an empty one
	  - each mark's section adds, indented by two spaces like the #551 lines:
	      Kind: press | hold <s.s> s | unknown          hold: releaseMs - pressMs of meta.txt, one decimal; unknown
	                                                    without kind= (a #551 mark)
	      Clip: none | <len> s, <n> frames | <len> s (<before> s before the press), <n> frames
	                                                    len: the last "@<ms>ms" frame line's time less the first's;
	                                                    n: the frame lines; before: the "# hold:" press less the first
	                                                    frame's time; one decimal each
	      Snapshots: <n>                                n: the snap-*.jpg files; then, indented by four spaces, by offset:
	        +0.0 s  left.png, right.png (press)         the press's eyes (those of the two that are there)
	        +<s.s> s  <snap file name>[ (release)]      each snapshot, the offset from its name; the last of a hold
	                                                    marked (release)
	      Transcript: none                              no note.wav
	      Transcript: not transcribed (run marks.ps1 -Transcribe)   a note.wav without transcript.txt
	      Transcript:                                   then, indented by four spaces, each transcript.txt line and
	        [m:ss.s] <text>  <- <image>                 the press-relative image nearest its time (left.png for the
	                                                    press at +0.0 s, else a snapshot; a tie: the earlier one)
	  - the help text describes the trackpads, the hold, the snapshots and -Transcribe (not #551's stick clicks or
	    10 s note)

	The #551 listing (its marks were all of one kind):

	  <user dir>\marks\<yyyymmdd>T<hhmmss>Z-<n>\   the UTC time of the mark and its number in the session; names sort by time
	    left.png, right.png   the eye images of that frame
	    recorder.txt          the flight recorder's tail (#545)
	    clip.vrsim            the replay ring's last 30 s (#550)
	    meta.txt              "# moment mark (#551)", then key=value lines: mark, time (YYYY-MM-DDTHH:MM:SSZ), area,
	                          position (<x> <y> <z>), yaw, build, state, session
	    note.wav              the voice note, when one was recorded: mono 16-bit PCM, the 44-byte RIFF header

	marks.ps1 [-UserDir <dir>] [-Copy <mark folder name> | last -To <dir>]
	  - -UserDir defaults to this working copy's isolated user directory when it has marks, else the player's Saved
	    Games\Arx Libertatis (the owner's headset runs)
	  - without -Copy: lists every mark of <user dir>\marks, oldest first (by name), each section starting at the
	    beginning of a line:
	      Marks: <count>                                   0 when there is none (still exit 0)
	      Mark: <folder name>                              only the name, never the full path; then, indented:
	        Time: <meta.txt time>                          "unknown" when meta.txt is missing (also Area, Position)
	        Area: <meta.txt area>
	        Position: <meta.txt position>
	        Files: <the folder's files, sorted by name, ", " between>
	        Note: <s> s                                    note.wav's length from its header (data bytes / bytes a
	                                                       second), one decimal; "none" without note.wav, "broken"
	                                                       when its header is not a WAV header
	  - -Copy <name> -To <dir>: copies that mark's folder with every file into <dir>\<name> and prints
	    "Copied: <name>"; -Copy last copies the newest. A name that is no mark: exit 1
	  - nothing it prints holds a user path or the account name (Protect-ArxText of log-digest-lib.ps1), even when
	    meta.txt has one
	  - exit 0 when it listed or copied, 1 on an error
	Exit code 0 when everything passed, 1 otherwise.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$failures = [Collections.Generic.List[string]]::new()
function Check([bool]$Condition, [string]$What) {
	if ($Condition) { Write-Host "  ok   $What" } else { Write-Host "  FAIL $What" -ForegroundColor Red; $failures.Add($What) }
}

$script = Join-Path $PSScriptRoot 'marks.ps1'
$root = Join-Path ([IO.Path]::GetTempPath()) "arxvr-marks-test-$PID"
$pwsh = (Get-Process -Id $PID).Path

function Write-Fixture([string]$Path, [string[]]$Lines) {
	New-Item -ItemType Directory -Force (Split-Path -Parent $Path) | Out-Null
	[IO.File]::WriteAllLines($Path, $Lines, [Text.UTF8Encoding]::new($false))
}

# A mono 16-bit PCM WAV of $Seconds at $Rate: the 44-byte header, then silence
function Write-Wav([string]$Path, [double]$Seconds, [int]$Rate = 16000) {
	$dataBytes = [int]([math]::Round($Seconds * $Rate)) * 2
	$ms = [IO.MemoryStream]::new()
	$w = [IO.BinaryWriter]::new($ms)
	$w.Write([Text.Encoding]::ASCII.GetBytes('RIFF')); $w.Write([uint32](36 + $dataBytes))
	$w.Write([Text.Encoding]::ASCII.GetBytes('WAVE'))
	$w.Write([Text.Encoding]::ASCII.GetBytes('fmt ')); $w.Write([uint32]16)
	$w.Write([uint16]1); $w.Write([uint16]1); $w.Write([uint32]$Rate); $w.Write([uint32]($Rate * 2))
	$w.Write([uint16]2); $w.Write([uint16]16)
	$w.Write([Text.Encoding]::ASCII.GetBytes('data')); $w.Write([uint32]$dataBytes)
	$w.Write([byte[]]::new($dataBytes))
	$w.Flush()
	[IO.File]::WriteAllBytes($Path, $ms.ToArray())
}

# A marker folder as the game writes it: the five files, meta.txt from the fields given, optionally a note
function New-Mark([string]$Dir, [hashtable]$Meta, [double]$NoteSeconds = -1, [switch]$NoMeta) {
	New-Item -ItemType Directory -Force $Dir | Out-Null
	# Not real pictures: marks.ps1 only lists them
	[IO.File]::WriteAllBytes((Join-Path $Dir 'left.png'), [byte[]](0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A) + [byte[]]::new(32))
	[IO.File]::WriteAllBytes((Join-Path $Dir 'right.png'), [byte[]](0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A) + [byte[]]::new(32))
	Write-Fixture (Join-Path $Dir 'recorder.txt') @('# flight recorder (#545): fixture', '# thread 1000 main thread: 2 events, 0 overwritten', '0.011 1000 frame 1', '0.022 1000 frame 2')
	Write-Fixture (Join-Path $Dir 'clip.vrsim') @('# arx-replay (#550): fixture', '# rate: 90', '@0ms head 0.0000 0.0000 0.0000 0.00 0.00 0.00')
	if (-not $NoMeta) {
		Write-Fixture (Join-Path $Dir 'meta.txt') @(
			'# moment mark (#551)'
			"mark=$($Meta.mark)"
			"time=$($Meta.time)"
			"area=$($Meta.area)"
			"position=$($Meta.position)"
			"yaw=$($Meta.yaw)"
			"build=$($Meta.build)"
			"state=$($Meta.state)"
			"session=$($Meta.session)"
		)
	}
	if ($NoteSeconds -ge 0) { Write-Wav (Join-Path $Dir 'note.wav') $NoteSeconds }
}

#574: every run gets an empty tools directory of its own, so a machine with the transcription tools cached never
# transcribes a fixture note (and none is ever fetched)
$noTools = Join-Path $root 'no-tools'
# The transcripts are Russian: the child's output is read as UTF-8 where the console allows it
try { [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false); $OutputEncoding = [Text.UTF8Encoding]::new($false) } catch { }

function Invoke-Marks([string[]]$Arguments) {
	New-Item -ItemType Directory -Force $noTools | Out-Null
	$output = & $pwsh -NoProfile -File $script @Arguments -ToolsDir $noTools 2>&1
	[pscustomobject]@{ Exit = $LASTEXITCODE; Text = (@($output | ForEach-Object { "$_" }) -join "`n"); Lines = @($output | ForEach-Object { "$_" }) }
}

# The lines of one mark's section: from "Mark: <name>" to the next "Mark:" or the end
function Get-MarkSection($Run, [string]$Name) {
	$lines = $Run.Lines
	$start = -1
	for ($i = 0; $i -lt $lines.Count; $i++) { if ($lines[$i] -match "^Mark: $([regex]::Escape($Name))\s*$") { $start = $i; break } }
	if ($start -lt 0) { return '' }
	$section = @()
	for ($i = $start + 1; $i -lt $lines.Count -and $lines[$i] -notmatch '^\S'; $i++) { $section += $lines[$i] }
	$section -join "`n"
}

function Test-Clean($Run, [string]$What) {
	Check ($Run.Text -notmatch '(?i)\b[A-Za-z]:[\\/]+(?:Users|Documents and Settings)\b') "$What`: no user path in the output"
	Check ($Run.Text -notmatch '(?i)(?<![\w-])fixtureuser(?![\w-])') "$What`: no account name in the output"
	Check (-not $Run.Text.Contains($root)) "$What`: no full path of the user directory, only the marks' names"
}

if (-not (Test-Path -LiteralPath $script)) {
	Write-Host "  FAIL scripts/dev/marks.ps1 does not exist" -ForegroundColor Red
	Write-Host 'test-marks: FAILED' -ForegroundColor Red
	exit 1
}

# A home path of the made-up account, built here so the file itself names none (check-private-data.ps1)
$fixtureHome = 'C:\Users\' + 'fixtureuser'
$previousUser = $env:USERNAME
# The account name the scrubber hides; the fixtures carry it where a careless build string would
$env:USERNAME = 'fixtureuser'
try {
	# --- One user directory: three marks of a headset run (one with a note, one without, one with a 10 s note) ----------
	$user = Join-Path $root 'user'
	$marks = Join-Path $user 'marks'
	$first = '20261020T153012Z-1'
	$second = '20261020T153544Z-2'
	$third = '20261020T160105Z-3'
	# Written out of order: the listing sorts by name (= by time)
	New-Mark (Join-Path $marks $third) @{ mark = 3; time = '2026-10-20T16:01:05Z'; area = 1; position = '-12.0 0.5 999.9'; yaw = '0.0'
		build = 'Arx Fatalis VR 1.3-dev abc1234 Release'; state = 'GAMEPLAY'; session = 'focused' } -NoteSeconds 10
	New-Mark (Join-Path $marks $first) @{ mark = 1; time = '2026-10-20T15:30:12Z'; area = 10; position = '1234.5 -150.0 4321.3'; yaw = '90.0'
		build = "Arx Fatalis VR 1.3-dev abc1234 Release ($fixtureHome\src\arx)"; state = 'GAMEPLAY'; session = 'focused' } -NoteSeconds 4.5
	New-Mark (Join-Path $marks $second) @{ mark = 2; time = '2026-10-20T15:35:44Z'; area = 11; position = '200.0 -100.0 300.0'; yaw = '180.0'
		build = 'Arx Fatalis VR 1.3-dev abc1234 Release'; state = 'GAMEPLAY'; session = 'visible' }
	# Something that is not a mark: a stray file in marks\
	Write-Fixture (Join-Path $marks 'readme.txt') @('not a mark')

	Write-Host '==> the listing'
	$run = Invoke-Marks @('-UserDir', $user)
	Check ($run.Exit -eq 0) "exit 0 ($($run.Exit))"
	Check ($run.Text -match '(?m)^Marks: 3\s*$') 'Marks: 3 (the stray file is no mark)'
	$order = @($run.Lines | Where-Object { $_ -match '^Mark: ' } | ForEach-Object { ($_ -replace '^Mark: ', '').Trim() })
	Check (($order -join ' ') -eq "$first $second $third") "the marks oldest first, by name ($($order -join ' '))"

	$s1 = Get-MarkSection $run $first
	Check ($s1 -match '(?m)^\s+Time: 2026-10-20T15:30:12Z\s*$') 'the first mark: Time'
	Check ($s1 -match '(?m)^\s+Area: 10\s*$') 'the first mark: Area'
	Check ($s1 -match '(?m)^\s+Position: 1234\.5 -150\.0 4321\.3\s*$') 'the first mark: Position'
	Check ($s1 -match '(?m)^\s+Files: clip\.vrsim, left\.png, meta\.txt, note\.wav, recorder\.txt, right\.png\s*$') 'the first mark: Files, sorted'
	Check ($s1 -match '(?m)^\s+Note: 4\.5 s\s*$') "the first mark: Note: 4.5 s (from the WAV header)"

	$s2 = Get-MarkSection $run $second
	Check ($s2 -match '(?m)^\s+Time: 2026-10-20T15:35:44Z\s*$') 'the second mark: Time'
	Check ($s2 -match '(?m)^\s+Area: 11\s*$') 'the second mark: Area'
	Check ($s2 -match '(?m)^\s+Position: 200\.0 -100\.0 300\.0\s*$') 'the second mark: Position'
	Check ($s2 -match '(?m)^\s+Files: clip\.vrsim, left\.png, meta\.txt, recorder\.txt, right\.png\s*$') 'the second mark: Files without a note'
	Check ($s2 -match '(?m)^\s+Note: none\s*$') 'the second mark: Note: none'

	$s3 = Get-MarkSection $run $third
	Check ($s3 -match '(?m)^\s+Note: 10(\.0)? s\s*$') 'the third mark: Note: 10.0 s'
	Check ($s3 -match '(?m)^\s+Position: -12\.0 0\.5 999\.9\s*$') 'the third mark: Position'
	Test-Clean $run 'the listing'

	Write-Host '==> a broken mark: no meta.txt, a cut-off note'
	$user2 = Join-Path $root 'user2'
	$broken = '20261021T090000Z-1'
	New-Mark (Join-Path $user2 "marks\$broken") @{} -NoMeta
	[IO.File]::WriteAllBytes((Join-Path $user2 "marks\$broken\note.wav"), [byte[]](0x52, 0x49, 0x46, 0x46, 0x00))
	$run = Invoke-Marks @('-UserDir', $user2)
	Check ($run.Exit -eq 0) "exit 0 ($($run.Exit))"
	Check ($run.Text -match '(?m)^Marks: 1\s*$') 'Marks: 1'
	$sb = Get-MarkSection $run $broken
	Check ($sb -match '(?m)^\s+Time: unknown\s*$') 'Time: unknown without meta.txt'
	Check ($sb -match '(?m)^\s+Area: unknown\s*$') 'Area: unknown'
	Check ($sb -match '(?m)^\s+Position: unknown\s*$') 'Position: unknown'
	Check ($sb -match '(?m)^\s+Files: clip\.vrsim, left\.png, note\.wav, recorder\.txt, right\.png\s*$') 'Files: what is there'
	Check ($sb -match '(?m)^\s+Note: broken\s*$') 'Note: broken for a cut-off WAV'

	Write-Host '==> no marks'
	$empty = Join-Path $root 'empty'
	New-Item -ItemType Directory -Force $empty | Out-Null
	$run = Invoke-Marks @('-UserDir', $empty)
	Check ($run.Exit -eq 0) "exit 0 without a marks folder ($($run.Exit))"
	Check ($run.Text -match '(?m)^Marks: 0\s*$') 'Marks: 0'

	Write-Host '==> copying a mark out'
	$out = Join-Path $root 'out'
	New-Item -ItemType Directory -Force $out | Out-Null
	$run = Invoke-Marks @('-UserDir', $user, '-Copy', $first, '-To', $out)
	Check ($run.Exit -eq 0) "exit 0 ($($run.Exit))"
	Check ($run.Text -match "(?m)^Copied: $([regex]::Escape($first))\s*$") "Copied: $first"
	$copied = Join-Path $out $first
	$names = @(Get-ChildItem -LiteralPath $copied -File -ErrorAction SilentlyContinue | Sort-Object Name | ForEach-Object Name)
	Check (($names -join ',') -eq 'clip.vrsim,left.png,meta.txt,note.wav,recorder.txt,right.png') "every file copied ($($names -join ','))"
	$same = $true
	foreach ($n in $names) {
		$a = [IO.File]::ReadAllBytes((Join-Path (Join-Path $marks $first) $n))
		$b = [IO.File]::ReadAllBytes((Join-Path $copied $n))
		if ([Convert]::ToBase64String($a) -ne [Convert]::ToBase64String($b)) { $same = $false }
	}
	Check $same 'the copies are byte-equal'
	Check (Test-Path -LiteralPath (Join-Path $marks $first)) 'the mark stays where it was'
	Test-Clean $run 'a copy'

	$run = Invoke-Marks @('-UserDir', $user, '-Copy', 'last', '-To', $out)
	Check ($run.Exit -eq 0) "-Copy last: exit 0 ($($run.Exit))"
	Check ($run.Text -match "(?m)^Copied: $([regex]::Escape($third))\s*$") "-Copy last copies the newest ($third)"
	Check (Test-Path -LiteralPath (Join-Path (Join-Path $out $third) 'note.wav')) '... with its note'
	Test-Clean $run 'a copy of the last'

	$run = Invoke-Marks @('-UserDir', $user, '-Copy', '20991231T000000Z-9', '-To', $out)
	Check ($run.Exit -eq 1) "a name that is no mark: exit 1 ($($run.Exit))"
	Check (-not (Test-Path -LiteralPath (Join-Path $out '20991231T000000Z-9'))) '... and nothing copied'
	Test-Clean $run 'an unknown mark'

	# --- #574: a short press, a hold with its clip, snapshots and transcript, a hold not transcribed yet ------------
	Write-Host '==> marks v2: kind, clip, snapshots, transcript with the nearest picture'
	$v2 = Join-Path $root 'user-v2'
	$v2marks = Join-Path $v2 'marks'
	$pressMark = '20261021T101500Z-1'
	$holdMark = '20261021T101530Z-2'
	$rawMark = '20261021T102000Z-3'
	$jpeg = [byte[]](0xFF, 0xD8, 0xFF, 0xE0) + [byte[]]::new(32) + [byte[]](0xFF, 0xD9)
	$png = [byte[]](0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A) + [byte[]]::new(32)
	$v2meta = { param($n, $kind, $press, $release)
		@('# moment mark (#551)', "mark=$n", 'time=2026-10-21T10:15:00Z', 'area=10', 'position=1234.5 -150.0 4321.3',
			'yaw=90.0', "build=Arx Fatalis VR 1.3-dev abc1234 Release ($fixtureHome\src\arx)", 'state=GAMEPLAY', 'session=focused',
			"kind=$kind", "pressMs=$press", "releaseMs=$release", 'head=0.0123 1.6512 -0.0456 12.50 -3.25 0.00',
			'left=-0.2000 1.2000 -0.3500 10.00 -20.00 5.00 on', 'right=0.2500 1.1000 -0.3000 -15.75 30.00 0.00 off',
			'curlsLeft=0.10 0.20 0.30 0.40 0.50', 'curlsRight=1.00 1.00 1.00 0.00 0.00')
	}

	# The short press: the eyes, the recorder, meta - nothing else
	$dir = Join-Path $v2marks $pressMark
	New-Item -ItemType Directory -Force $dir | Out-Null
	[IO.File]::WriteAllBytes((Join-Path $dir 'left.png'), $png)
	[IO.File]::WriteAllBytes((Join-Path $dir 'right.png'), $png)
	Write-Fixture (Join-Path $dir 'recorder.txt') @('# flight recorder (#545): fixture')
	Write-Fixture (Join-Path $dir 'meta.txt') (& $v2meta 1 'press' 12000 12200)

	# The 7 s hold: its clip from 10 s before the press (18 frame lines, a second apart), four snapshots, a transcript
	$dir = Join-Path $v2marks $holdMark
	New-Item -ItemType Directory -Force $dir | Out-Null
	[IO.File]::WriteAllBytes((Join-Path $dir 'left.png'), $png)
	[IO.File]::WriteAllBytes((Join-Path $dir 'right.png'), $png)
	Write-Fixture (Join-Path $dir 'recorder.txt') @('# flight recorder (#545): fixture')
	Write-Fixture (Join-Path $dir 'meta.txt') (& $v2meta 2 'hold' 20000 27000)
	$clip = @('# arx-replay 1: a VR session as a .vrsim clip (#550) - scripts/dev/replay.ps1 plays it', '# build: Arx Fatalis VR 1.3.0-dev',
		'# save: none', '# seed: 7', '# rate: 90', "# mark: $holdMark 27000", '# hold: 20000 27000', 'ipd 0.0640')
	for ($ms = 10000; $ms -le 27000; $ms += 1000) { $clip += "@${ms}ms head 0.0000 0.0000 0.0000 0.00 0.00 0.00" }
	Write-Fixture (Join-Path $dir 'clip.vrsim') $clip
	foreach ($snap in 'snap-+06.0s.jpg', 'snap-+02.0s.jpg', 'snap-+07.0s.jpg', 'snap-+04.0s.jpg') {
		[IO.File]::WriteAllBytes((Join-Path $dir $snap), $jpeg)
	}
	Write-Wav (Join-Path $dir 'note.wav') 6.8
	$cyr1 = -join (@(0x0421, 0x043C, 0x043E, 0x0442, 0x0440, 0x0438, 0x002C, 0x0020, 0x044F, 0x0020, 0x0443, 0x0431, 0x0440,
		0x0430, 0x043B, 0x0020, 0x043C, 0x0435, 0x0447, 0x002E) | ForEach-Object { [char]$_ })   # Смотри, я убрал меч.
	$cyr2 = -join (@(0x0420, 0x0430, 0x0434, 0x0438, 0x0430, 0x043B, 0x044C, 0x043D, 0x043E, 0x0435, 0x0020, 0x043C, 0x0435,
		0x043D, 0x044E) | ForEach-Object { [char]$_ }) + ' trigger'                                   # Радиальное меню trigger
	$cyr3 = -join (@(0x041A, 0x043E, 0x043D, 0x0435, 0x0446, 0x002E) | ForEach-Object { [char]$_ })    # Конец.
	[IO.File]::WriteAllText((Join-Path $dir 'transcript.txt'), "[0:00.4] $cyr1`n[0:03.1] $cyr2`n[0:05.0] Tie line, B.`n[0:06.9] $cyr3`n", [Text.UTF8Encoding]::new($false))

	# A hold with a note not transcribed yet (and no tools cached): only the note's state
	$dir = Join-Path $v2marks $rawMark
	New-Item -ItemType Directory -Force $dir | Out-Null
	[IO.File]::WriteAllBytes((Join-Path $dir 'left.png'), $png)
	Write-Fixture (Join-Path $dir 'meta.txt') (& $v2meta 3 'hold' 40000 42500)
	[IO.File]::WriteAllBytes((Join-Path $dir 'snap-+02.0s.jpg'), $jpeg)
	[IO.File]::WriteAllBytes((Join-Path $dir 'snap-+02.5s.jpg'), $jpeg)
	Write-Wav (Join-Path $dir 'note.wav') 2.4

	$run = Invoke-Marks @('-UserDir', $v2)
	Check ($run.Exit -eq 0) "exit 0 ($($run.Exit))"
	Check ($run.Text -match '(?m)^Marks: 3\s*$') 'Marks: 3'

	$sp = Get-MarkSection $run $pressMark
	Check ($sp -match '(?m)^  Kind: press\s*$') 'the press: Kind: press'
	Check ($sp -match '(?m)^  Clip: none\s*$') 'the press: Clip: none'
	Check ($sp -match '(?m)^  Snapshots: 0\s*$') 'the press: Snapshots: 0'
	Check ($sp -match '(?m)^    \+0\.0 s  left\.png, right\.png \(press\)\s*$') 'the press: its eyes at +0.0 s'
	Check ($sp -match '(?m)^  Transcript: none\s*$') 'the press: Transcript: none (no note)'
	Check ($sp -match '(?m)^  Files: left\.png, meta\.txt, recorder\.txt, right\.png\s*$') 'the press: Files'

	$sh = Get-MarkSection $run $holdMark
	Check ($sh -match '(?m)^  Kind: hold 7\.0 s\s*$') "the hold: Kind: hold 7.0 s`n$sh"
	Check ($sh -match '(?m)^  Clip: 17\.0 s \(10\.0 s before the press\), 18 frames\s*$') 'the hold: Clip: 17.0 s (10.0 s before the press), 18 frames'
	Check ($sh -match '(?m)^  Note: 6\.8 s\s*$') 'the hold: Note: 6.8 s'
	Check ($sh -match '(?m)^  Snapshots: 4\s*$') 'the hold: Snapshots: 4'
	$images = @($sh -split "`n" | Where-Object { $_ -match '^    \+\d' })
	$wantImages = @(
		'    +0.0 s  left.png, right.png (press)'
		'    +2.0 s  snap-+02.0s.jpg'
		'    +4.0 s  snap-+04.0s.jpg'
		'    +6.0 s  snap-+06.0s.jpg'
		'    +7.0 s  snap-+07.0s.jpg (release)'
	)
	Check ((($images | ForEach-Object { $_.TrimEnd() }) -join '|') -ceq ($wantImages -join '|')) "the hold: the pictures by offset, the release marked (got: $($images -join ' | '))"
	$lines = @($sh -split "`n")
	$at = [array]::FindIndex([string[]]$lines, [Predicate[string]] { param($l) $l -match '^  Transcript:\s*$' })
	Check ($at -ge 0) 'the hold: "Transcript:" then its lines'
	$said = if ($at -ge 0) { @($lines[($at + 1)..([math]::Min($at + 4, $lines.Count - 1))] | ForEach-Object { $_.TrimEnd() }) } else { @() }
	Check ($said.Count -eq 4) "the hold: four transcript lines ($($said.Count))"
	if ($said.Count -eq 4) {
		Check ($said[0] -match '^    \[0:00\.4\] .+  <- left\.png$') "0:00.4 is nearest the press's eyes: $($said[0])"
		Check ($said[1] -match '^    \[0:03\.1\] .*trigger  <- snap-\+04\.0s\.jpg$') "0:03.1 is nearest the 4 s snapshot: $($said[1])"
		Check ($said[2] -ceq '    [0:05.0] Tie line, B.  <- snap-+04.0s.jpg') "0:05.0, between 4 and 6 s: the earlier: $($said[2])"
		Check ($said[3] -match '^    \[0:06\.9\] .+  <- snap-\+07\.0s\.jpg$') "0:06.9 is nearest the release: $($said[3])"
		if ([Console]::OutputEncoding.CodePage -eq 65001) {
			Check ($said[0] -ceq "    [0:00.4] $cyr1  <- left.png") 'the Russian text as it is (UTF-8)'
			Check ($said[1] -ceq "    [0:03.1] $cyr2  <- snap-+04.0s.jpg") '... mixed with Latin'
		}
	}

	$sr = Get-MarkSection $run $rawMark
	Check ($sr -match '(?m)^  Kind: hold 2\.5 s\s*$') 'the untranscribed hold: Kind: hold 2.5 s'
	Check ($sr -match '(?m)^  Clip: none\s*$') 'the untranscribed hold: no clip.vrsim - Clip: none'
	Check ($sr -match '(?m)^    \+0\.0 s  left\.png \(press\)\s*$') 'only the eye that is there'
	Check ($sr -match '(?m)^    \+2\.5 s  snap-\+02\.5s\.jpg \(release\)\s*$') 'the release at +2.5 s'
	Check ($sr -match '(?m)^  Transcript: not transcribed \(run marks\.ps1 -Transcribe\)\s*$') 'a note without a transcript: not transcribed'
	Check (-not (Test-Path -LiteralPath (Join-Path (Join-Path $v2marks $rawMark) 'transcript.txt'))) '... and none written'
	Check (@(Get-ChildItem -LiteralPath $noTools -Recurse -Force -ErrorAction SilentlyContinue).Count -eq 0) 'no tools cached: nothing fetched into the tools directory'
	Test-Clean $run 'the v2 listing'

	# The #551 marks have no kind
	$run = Invoke-Marks @('-UserDir', $user)
	Check ((Get-MarkSection $run $first) -match '(?m)^  Kind: unknown\s*$') 'a #551 mark: Kind: unknown'

	Write-Host '==> the help text is current'
	$help = [IO.File]::ReadAllText($script)
	Check ($help -notmatch '(?i)thumbstick click') 'no stick clicks (the gesture is both trackpads)'
	Check ($help -notmatch '(?i)up to 10 s') 'no 10 s note'
	Check ($help -match '(?i)trackpad') 'the trackpads'
	Check ($help -match '(?i)\bhold\b') 'the hold'
	Check ($help -match '-Transcribe') '-Transcribe'
	Check ($help -match 'snap-') 'the snapshots'
} finally {
	$env:USERNAME = $previousUser
	Remove-Item -Recurse -Force $root -ErrorAction SilentlyContinue
}

if ($failures.Count) {
	Write-Host "test-marks: $($failures.Count) check(s) FAILED" -ForegroundColor Red
	exit 1
}
Write-Host 'test-marks: all checks passed' -ForegroundColor Green
exit 0
