<#
.SYNOPSIS
	Self-test of the crash signatures of the run digest (#616): fixture crash.txt texts, no game, no network.

.DESCRIPTION
	The run digest (scripts/dev/log-digest-lib.ps1) files one GitHub issue per crash signature. A signature keyed on
	the module offsets (arx.exe+0x...) changes with every build, so the same crash filed a second issue (#611 duplicated
	#601: 0x1c3826 vs 0x1c34a6). This test fixes the rule:

	The interface (scripts/dev/log-digest-lib.ps1, dot-sourced):
	  Get-ArxCrashSignature -Text <string>
	    -Text   the whole crash.txt text (LF or CRLF line ends): the title line (the fault location, e.g.
	            "arx.exe+0x1c34a6"), "Exception code: ...", "Callstack:" and the frames.
	    returns an object with
	      .Hash  the signature: 12 lowercase hex characters (like every digest signature, Get-ArxHash)
	      .Key   the text that was hashed
	  Read-ArxCrashReport <dir> keeps its shape; its .Hash is Get-ArxCrashSignature's .Hash of the folder's crash.txt.

	The rule:
	  - A frame of the dump helper's stack (#545) is "  #<i> <module>+0x<offset> [<function> [(<file>:<line>)]]".
	    A report with symbols is keyed on its top N = 5 symbolized frames, each as function + file: no offsets, no
	    line numbers, and not the title's offset either. So the same crash on another build (other offsets, other line
	    numbers) is one signature; another function or file in the top 5 frames is another signature; frames below the
	    top 5 do not count.
	  - Only a report without symbols (offsets only) falls back to the offsets: the same offsets give one signature,
	    other offsets another.
	  - An empty stack ("Callstack:" with no frame, or no "Callstack:" line at all) is keyed on the exception code and
	    the title (the fault location, its offset included: there is nothing else). No error is thrown.
	  - The crash processor's older format ("  arx.exe!0x<address>  File.cpp:12", no function names) keeps its
	    current behaviour: keyed on the files, no addresses, no line numbers (a guard; it holds today).
	Not specified here: the exception code's part in a symbolized signature, unsymbolized frames between symbolized
	ones, a stack shorter than 5 frames compared with a longer one.

	Exit code 0 when everything passed, 1 otherwise.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'log-digest-lib.ps1')

$failures = 0
function Check([bool]$Ok, [string]$What) {
	if ($Ok) { Write-Host "  [ok]   $What" -ForegroundColor Green } else { Write-Host "  [FAIL] $What" -ForegroundColor Red; $script:failures++ }
}

$script:errorShown = $false
function Sig([string]$Text) {
	try { Get-ArxCrashSignature -Text $Text }
	catch {
		if (-not $script:errorShown) { Write-Host "  Get-ArxCrashSignature: $($_.Exception.Message)" -ForegroundColor Red; $script:errorShown = $true }
		$null
	}
}
function Hash([string]$Text) { $s = Sig $Text; if ($s) { [string]$s.Hash } else { $null } }
function Same([string]$A, [string]$B) { $ha = Hash $A; $hb = Hash $B; [bool]($ha -and $hb -and $ha -eq $hb) }
function Differ([string]$A, [string]$B) { $ha = Hash $A; $hb = Hash $B; [bool]($ha -and $hb -and $ha -ne $hb) }

# A crash.txt as the dump helper writes it (src/platform/DumpHelper.cpp, writeReportFolder): the fault location, the
# exception code, the callstack (two spaces before every frame)
function New-CrashText([string]$Title, [string]$Exception, [string[]]$Frames, [string]$Eol = "`n") {
	$lines = @($Title)
	if ($Exception) { $lines += "Exception code: $Exception" }
	$lines += 'Callstack:'
	$i = 0
	foreach ($f in $Frames) { $lines += "  #$i $f"; $i++ }
	($lines -join $Eol) + $Eol
}
$av = '0xc0000005 EXCEPTION_ACCESS_VIOLATION'

# The stack of #601 (build 0e559f4)
$frames601 = @(
	'arx.exe+0x1c34a6 FrustrumsClipSphere (Scene.cpp:247)'
	'arx.exe+0x1c359f VisibleSphere (Scene.cpp:270)'
	'arx.exe+0xdcba0 CCreateField::Update (Field.cpp:162)'
	'arx.exe+0xb94e3 CreateFieldSpell::Launch (SpellsLvl06.cpp:338)'
	'arx.exe+0xa5829 ARX_SPELLS_Launch (Spells.cpp:1093)'
	'arx.exe+0xa5efa TryToCastSpell (Spells.cpp:1199)'
	'arx.exe+0x20825a script::`anonymous namespace''::SpellcastCommand::execute (ScriptedNPC.cpp:276)'
	'arx.exe+0x1d09e5 ScriptEvent::send (ScriptEvent.cpp:331)'
	'arx.exe+0x1ce3ea SendIOScriptEvent (Script.cpp:1609)'
	'arx.exe+0x1c9761 SendMsgToAllIO (Script.cpp:289)'
	'arx.exe+0x72165 levelInit (Core.cpp:430)'
	'arx.exe+0x36196 ArxGame::doFrame (ArxGame.cpp:1700)'
	'arx.exe+0x3477e ArxGame::run (ArxGame.cpp:1355)'
	'arx.exe+0x717e9 runGame (Core.cpp:258)'
	'arx.exe+0x817e7 utf8_main (Startup.cpp:450)'
	'arx.exe+0x195e0a WinMain (WindowsMainGUI.cpp:48)'
)
# The same crash on another build (#611, bed926c): other offsets; an edit above the crash moved the line numbers too
$frames611 = @(
	'arx.exe+0x1c3826 FrustrumsClipSphere (Scene.cpp:251)'
	'arx.exe+0x1c391f VisibleSphere (Scene.cpp:274)'
	'arx.exe+0xdcf10 CCreateField::Update (Field.cpp:165)'
	'arx.exe+0xb9853 CreateFieldSpell::Launch (SpellsLvl06.cpp:341)'
	'arx.exe+0xa5b99 ARX_SPELLS_Launch (Spells.cpp:1096)'
	'arx.exe+0xa626a TryToCastSpell (Spells.cpp:1199)'
	'arx.exe+0x2085da script::`anonymous namespace''::SpellcastCommand::execute (ScriptedNPC.cpp:276)'
	'arx.exe+0x1d0d65 ScriptEvent::send (ScriptEvent.cpp:331)'
	'arx.exe+0x1ce76a SendIOScriptEvent (Script.cpp:1609)'
	'arx.exe+0x1c9ae1 SendMsgToAllIO (Script.cpp:289)'
	'arx.exe+0x724d5 levelInit (Core.cpp:430)'
	'arx.exe+0x361b6 ArxGame::doFrame (ArxGame.cpp:1712)'
	'arx.exe+0x3479e ArxGame::run (ArxGame.cpp:1367)'
	'arx.exe+0x71b59 runGame (Core.cpp:258)'
	'arx.exe+0x81b57 utf8_main (Startup.cpp:450)'
	'arx.exe+0x19618a WinMain (WindowsMainGUI.cpp:48)'
)
$crash601 = New-CrashText 'arx.exe+0x1c34a6' $av $frames601
$crash611 = New-CrashText 'arx.exe+0x1c3826' $av $frames611
function With([string[]]$Frames, [int]$Index, [string]$Frame) { $c = @($Frames); $c[$Index] = $Frame; $c }

Write-Host '1. the interface' -ForegroundColor Cyan
Check ([bool](Get-Command Get-ArxCrashSignature -ErrorAction SilentlyContinue)) 'log-digest-lib.ps1 defines Get-ArxCrashSignature'
$s = Sig $crash601
Check ($s -and $s.Hash -cmatch '^[0-9a-f]{12}$') "Get-ArxCrashSignature -Text returns .Hash, 12 lowercase hex: $(if ($s) { $s.Hash })"
Check ($s -and [string]$s.Key) "and .Key, the text that was hashed: $(if ($s) { $s.Key })"

Write-Host '2. the same crash on two builds is one signature (#601 / #611)' -ForegroundColor Cyan
Check (Same $crash601 $crash611) 'other offsets (title and frames) and other line numbers: one signature'
$offsets = @('1c34a6') + @($frames601 | ForEach-Object { ([regex]::Match($_, '\+0x([0-9a-f]+)')).Groups[1].Value })
Check ($s -and $s.Key -notmatch '\+0x' -and -not ($offsets | Where-Object { $s.Key.Contains($_) })) "the key holds no offset (the title's or a frame's): $(if ($s) { $s.Key })"
Check ($s -and $s.Key -notmatch '\.(cpp|h):\d') 'the key holds no line number'
Check ($s -and $s.Key.Contains('FrustrumsClipSphere') -and $s.Key.Contains('Scene.cpp') -and $s.Key.Contains('ARX_SPELLS_Launch') -and $s.Key.Contains('Spells.cpp')) 'the key holds the functions and files of the top 5 frames'
Check ($s -and -not $s.Key.Contains('TryToCastSpell') -and -not $s.Key.Contains('WinMain')) 'the key leaves out the frames below the top 5'
Check (Same $crash601 (New-CrashText 'arx.exe+0x1c34a6' $av $frames601 "`r`n")) 'CRLF line ends: the same signature'
Check (Same $crash601 (New-CrashText 'arx.exe+0x1c34a6' $av ($frames601 | ForEach-Object { $_ -replace '^arx\.exe\+0x[0-9a-f]+', 'arx.exe+0x1' }))) 'the frames'' offsets do not count'

Write-Host '3. different crashes are different signatures' -ForegroundColor Cyan
Check (Differ $crash601 (New-CrashText 'arx.exe+0x1c34a6' $av (With $frames601 0 'arx.exe+0x1c34a6 FrustrumsClipBBox3D (Scene.cpp:247)'))) 'another top function: another signature'
Check (Differ $crash601 (New-CrashText 'arx.exe+0x1c34a6' $av (With $frames601 0 'arx.exe+0x1c34a6 FrustrumsClipSphere (Frustum.cpp:247)'))) 'the same top function in another file: another signature'
Check (Differ $crash601 (New-CrashText 'arx.exe+0x1c34a6' $av (With $frames601 2 'arx.exe+0xdcba0 CCreateField::Render (Field.cpp:162)'))) 'another function at frame #2: another signature'
Check (Differ $crash601 (New-CrashText 'arx.exe+0x1c34a6' $av (With $frames601 4 'arx.exe+0xa5829 ARX_SPELLS_Precast_Launch (Precast.cpp:51)'))) 'another function at frame #4 (the 5th, inside the top 5): another signature'
$otherCaller = @($frames601[0..4]) + @(
	'arx.exe+0xa6100 ARX_SPELLS_Precast_Launch (Spells.cpp:1260)'
	'arx.exe+0x8d2a1 ARX_PLAYER_Manage_Visual (Player.cpp:1650)'
	'arx.exe+0x36196 ArxGame::updateLevel (ArxGame.cpp:1500)'
	'arx.exe+0x3477e ArxGame::run (ArxGame.cpp:1355)'
)
Check (Same $crash601 (New-CrashText 'arx.exe+0x1c34a6' $av $otherCaller)) 'a stack that differs only below the top 5 frames: the same signature'

Write-Host '4. a report without symbols falls back to the offsets' -ForegroundColor Cyan
$bare601 = @($frames601 | ForEach-Object { ($_ -split ' ')[0] })
$bare611 = @($frames611 | ForEach-Object { ($_ -split ' ')[0] })
$u1 = New-CrashText 'arx.exe+0x1c34a6' $av $bare601
$u1b = New-CrashText 'arx.exe+0x1c34a6' $av $bare601
$u2 = New-CrashText 'arx.exe+0x1c3826' $av $bare611
$u3 = New-CrashText 'arx.exe+0x1c34a6' $av (With $bare601 1 'arx.exe+0x1c3600')
Check (Same $u1 $u1b) 'the same offsets: the same signature'
Check (Differ $u1 $u2) 'other offsets (another build or another crash): another signature'
Check (Differ $u1 $u3) 'another offset at frame #1: another signature'

Write-Host '5. an empty stack: the exception code and the fault location' -ForegroundColor Cyan
$e1 = New-CrashText 'arx.exe+0x1c34a6' $av @()
$e1b = New-CrashText 'arx.exe+0x1c34a6' $av @()
$e2 = New-CrashText 'arx.exe+0x1c34a6' '0xc00000fd EXCEPTION_STACK_OVERFLOW' @()
$e3 = New-CrashText 'arx.exe+0x2f0010' $av @()
$e4 = "arx.exe+0x1c34a6`nException code: $av`n"
$es = Sig $e1
Check ($es -and $es.Hash -cmatch '^[0-9a-f]{12}$') "no frame: a signature all the same: $(if ($es) { $es.Hash })"
Check (Same $e1 $e1b) 'the same exception code and fault location: the same signature'
Check (Differ $e1 $e2) 'another exception code: another signature'
Check (Differ $e1 $e3) 'another fault location: another signature'
Check (Same $e1 $e4) 'no "Callstack:" line at all is the same as an empty stack'
$z = Sig ''
Check ($z -and $z.Hash -cmatch '^[0-9a-f]{12}$') 'an empty crash.txt: a signature, no error'

Write-Host '6. the crash processor''s older format (a guard: as today)' -ForegroundColor Cyan
$oldA = "Illegal storage access`n`nException code: ACCESS_VIOLATION`n`nCallstack:`n arx.exe!0x7ff773860206  Entity.cpp:259`n arx.exe!0x7ff7738605d5  Entity.cpp:352`n arx.exe!0x7ff77385fc0a  Entity.cpp:184`n arx.exe!0x7ff7739726f1  LoadLevel.cpp:544`n KERNEL32.DLL!BaseThreadInitThunk()`n"
$oldB = "Illegal storage access`n`nException code: ACCESS_VIOLATION`n`nCallstack:`n arx.exe!0x7ff6120a0216  Entity.cpp:261`n arx.exe!0x7ff6120a05e5  Entity.cpp:354`n arx.exe!0x7ff61209fc1a  Entity.cpp:186`n arx.exe!0x7ff6121b2701  LoadLevel.cpp:546`n KERNEL32.DLL!BaseThreadInitThunk()`n"
$oldC = "Illegal storage access`n`nException code: ACCESS_VIOLATION`n`nCallstack:`n arx.exe!0x7ff773860206  Inventory.cpp:259`n arx.exe!0x7ff7738605d5  Entity.cpp:352`n arx.exe!0x7ff77385fc0a  Entity.cpp:184`n arx.exe!0x7ff7739726f1  LoadLevel.cpp:544`n KERNEL32.DLL!BaseThreadInitThunk()`n"
Check (Same $oldA $oldB) 'other addresses and line numbers: the same signature'
Check (Differ $oldA $oldC) 'another file at the top: another signature'

Write-Host '7. the digest uses it: Read-ArxCrashReport' -ForegroundColor Cyan
$root = Join-Path ([IO.Path]::GetTempPath()) "arxvr-crash-signature-test-$PID"
$dirs = @{}
foreach ($pair in @(@('2026-10-22-00-29-20-00', $crash601), @('2026-10-22-04-45-13-00', $crash611), @('2026-10-22-05-00-00-00', (New-CrashText 'arx.exe+0x1c34a6' $av (With $frames601 0 'arx.exe+0x1c34a6 FrustrumsClipBBox3D (Scene.cpp:247)'))))) {
	$d = Join-Path $root $pair[0]
	New-Item -ItemType Directory -Force $d | Out-Null
	[IO.File]::WriteAllText((Join-Path $d 'crash.txt'), $pair[1], [Text.UTF8Encoding]::new($false))
	$dirs[$pair[0]] = $d
}
try {
	$r601 = Read-ArxCrashReport $dirs['2026-10-22-00-29-20-00']
	$r611 = Read-ArxCrashReport $dirs['2026-10-22-04-45-13-00']
	$rOther = Read-ArxCrashReport $dirs['2026-10-22-05-00-00-00']
	Check ($r601 -and $r611 -and $r601.Hash -eq $r611.Hash) "the #601 and #611 reports: one signature ($($r601.Hash) / $($r611.Hash))"
	Check ($r601 -and $s -and $r601.Hash -eq $s.Hash) 'Read-ArxCrashReport''s .Hash is Get-ArxCrashSignature''s'
	Check ($r601 -and $rOther -and $r601.Hash -ne $rOther.Hash) 'another top function in a report folder: another signature'
} finally {
	Remove-Item $root -Recurse -Force -ErrorAction SilentlyContinue
}

if ($failures) { Write-Host "crash signature self-test: $failures check(s) FAILED" -ForegroundColor Red; exit 1 }
Write-Host 'crash signature self-test: all checks passed' -ForegroundColor Green
exit 0
