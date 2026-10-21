<#
.SYNOPSIS
	Self-test of scripts/dev/marks.ps1 (#551) on a fixture marks folder: no game, no headset.

.DESCRIPTION
	The marks the game writes when both thumbstick clicks are held 3 s in VR (tests/vr/MarkMomentTest.h has the game's
	side; scripts/dev/xrs/mark-moment.xrs checks a real one):

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

function Invoke-Marks([string[]]$Arguments) {
	$output = & $pwsh -NoProfile -File $script @Arguments 2>&1
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
