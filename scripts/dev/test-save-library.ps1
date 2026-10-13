<#
.SYNOPSIS
	Self-test of the test save library tooling (#70): the scenario parser, the verification steps and slot order, and
	the library helpers of build-paths.ps1 (Get-ArxSaveLibrary, Copy-ArxSaves, Use-ArxSave).

.DESCRIPTION
	No game, no network: fixtures in a scratch folder. Checks that
	  - the real scripts/dev/test-saves/scenarios.txt parses: its six saves, only known directives
	  - the parser reads comments, blank lines, directives and commands, keeps line numbers and the order of the saves
	  - a mistake that would only show after a minute in the game is an error naming the line: an unknown directive, a
	    missing or non-numeric argument, a save name used twice or not usable as a folder name, no save at all
	  - `savegame x` is a channel command, not a save (the channel command is `save`)
	  - the verification steps load slot n through the channel (none for a slot loaded by the command line), then check
	    the level, the position (formatted the same in every culture), the free controls, and take a screenshot
	  - the slot order is newest save file first, and a save missing from the library is left out
	  - Use-ArxSave makes the named save the ONLY save of a user directory (older ones are gone), keeps the file time that
	    the slot order depends on, and a missing name fails naming what the library has
	  - the library folder is $env:ARXVR_SAVE_LIBRARY, else %LOCALAPPDATA%\ArxFatalisVR\savelib
	Exit code 0 when everything passed, 1 otherwise. Run by run-script-tests.ps1, so CI runs it.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Join-Path ([IO.Path]::GetTempPath()) "arxvr-savelib-test-$PID"
New-Item -ItemType Directory -Force $root | Out-Null
. (Join-Path $PSScriptRoot '..\build-paths.ps1')
. (Join-Path $PSScriptRoot 'test-saves\scenario-lib.ps1')

$script:failed = 0
function Check([bool]$Ok, [string]$Text, $Got = $null) {
	if ($Ok) { Write-Host "  ok    $Text" }
	else { $script:failed++; Write-Host "  FAIL  $Text$(if ($null -ne $Got) { " - got: $Got" })" -ForegroundColor Red }
}
# The message of the error a script block throws, or $null when it does not throw
function Throws([scriptblock]$Block) { try { & $Block | Out-Null; $null } catch { "$($_.Exception.Message)" } }

try {
	Write-Host 'The real scenario file'
	$real = Read-ArxScenario (Get-Content (Join-Path $PSScriptRoot 'test-saves\scenarios.txt')) 'scenarios.txt'
	Check ((@($real.SaveNames) -join ',') -eq 'mvp-new-game,mvp-items,mvp-goblin-fight,mvp-lever,mvp-door,mvp-chest') 'the six saves, in order' ($real.SaveNames -join ',')
	Check (@($real.Steps | Where-Object { $_.Kind -notin ($script:ArxScenarioDirectives + 'command') }).Count -eq 0) 'every step is a known directive or a command'
	Check (@($real.Steps | Where-Object { $_.Kind -eq 'command' -and $_.Text -eq 'newgame' }).Count -eq 1) 'one newgame'
	Check (@($real.Steps | Where-Object { $_.Kind -eq 'until' -and $_.Text -like 'player.controlsBlocked False*' }).Count -ge 1) 'it waits for the controls before the first save (TRAPS.md: the awakening)'

	Write-Host 'Parsing a fixture'
	$fixture = @(
		'# a comment', '', '   ', 'newgame', '@wait 1.5', '@describe First save, in words', 'save a-1',
		'  @state GAMEPLAY 120', '@level 1', '@until player.controlsBlocked False 60', '@expect area 1', '@near 100.5 -20 50',
		'@entity short_sword_0030 show=6', '@print player.x player.z', 'savegame not-a-save', 'save b.2_x')
	$p = Read-ArxScenario $fixture 'fx'
	Check ((@($p.SaveNames) -join ',') -eq 'a-1,b.2_x') 'save names in order; "savegame" is not a save' ($p.SaveNames -join ',')
	Check ($p.Steps.Count -eq 13) '13 steps: comments and blank lines skipped' $p.Steps.Count
	Check (($p.Steps | Where-Object { $_.Kind -eq 'describe' }).Text -eq 'First save, in words') '@describe keeps its text'
	Check (($p.Steps | Where-Object { $_.Kind -eq 'state' }).Line -eq 8) 'line numbers count every line (the @state is line 8)' (($p.Steps | Where-Object { $_.Kind -eq 'state' }).Line)
	Check (($p.Steps | Where-Object { $_.Kind -eq 'command' } | ForEach-Object Text) -contains 'savegame not-a-save') '"savegame ..." stays an ordinary channel command'

	Write-Host 'Mistakes that must be caught before the game starts'
	$cases = @(
		@{ Name = 'an unknown directive'; Lines = @('@wiat 1', 'save a'); Expect = 'fx:1: unknown directive @wiat' },
		@{ Name = 'a non-numeric @wait'; Lines = @('save a', '@wait soon'); Expect = 'fx:2: @wait needs a number' },
		@{ Name = '@near without a radius'; Lines = @('@near 1 2', 'save a'); Expect = 'fx:1: @near needs x z radius' },
		@{ Name = '@level with a word'; Lines = @('@level one', 'save a'); Expect = 'fx:1: @level needs a level number' },
		@{ Name = '@expect without a value'; Lines = @('@expect area', 'save a'); Expect = 'fx:1: @expect needs' },
		@{ Name = '@until with one argument'; Lines = @('@until player.controlsBlocked', 'save a'); Expect = 'fx:1: @until needs' },
		@{ Name = '@entity without a text'; Lines = @('@entity short_sword_0030', 'save a'); Expect = 'fx:1: @entity needs' },
		@{ Name = '@describe without a text'; Lines = @('@describe', 'save a'); Expect = 'fx:1: @describe needs' },
		@{ Name = 'a save name used twice'; Lines = @('save a', 'wait', 'save a'); Expect = "fx:3: save name 'a' is used twice" },
		@{ Name = 'a save name with a space'; Lines = @('save my save'); Expect = "fx:1: save name 'my save'" },
		@{ Name = 'a save name that climbs out of the library'; Lines = @('save ../x'); Expect = "fx:1: save name '../x'" },
		@{ Name = 'a save without a name'; Lines = @('save'); Expect = 'fx:1: save needs a name' },
		@{ Name = 'no save at all'; Lines = @('newgame', '@wait 1'); Expect = 'fx has no save command' })
	foreach ($c in $cases) {
		$message = Throws { Read-ArxScenario $c.Lines 'fx' }
		Check ($message -and $message -like "*$($c.Expect)*") "$($c.Name) is an error naming the line" $message
	}

	Write-Host 'The verification steps'
	$entries = @(
		[pscustomobject]@{ name = 'one'; area = 1; player = [pscustomobject]@{ x = 8540.5; z = 8460 } },
		[pscustomobject]@{ name = 'two'; area = 2; player = [pscustomobject]@{ x = 10700; z = 11400.25 } })
	function Format-Steps($steps) { ($steps | ForEach-Object { "$($_.Kind):$($_.Text)" }) -join ' | ' }
	$culture = [Threading.Thread]::CurrentThread.CurrentCulture
	try {
		# A comma-decimal culture must not turn 8540.5 into "8540,5": the near step is parsed with the invariant culture
		[Threading.Thread]::CurrentThread.CurrentCulture = [Globalization.CultureInfo]::GetCultureInfo('de-DE')
		$v = @(New-ArxVerifySteps $entries @(-1, 1))
	} finally { [Threading.Thread]::CurrentThread.CurrentCulture = $culture }
	Check (@($v | Where-Object { $_.Kind -eq 'command' -and $_.Text -like 'loadslot*' }).Count -eq 1) 'one loadslot: slot -1 is already loaded by the command line' (Format-Steps $v)
	Check (($v | Where-Object { $_.Text -eq 'loadslot 1' }) -ne $null) 'the other is loadslot 1'
	Check (($v | Where-Object { $_.Kind -eq 'near' } | ForEach-Object Text) -join ';' -eq '8540.5 8460 100;10700 11400.25 100') 'the position, invariant format, radius 100 (a metre)' (($v | Where-Object { $_.Kind -eq 'near' } | ForEach-Object Text) -join ';')
	Check ((($v | Where-Object { $_.Kind -eq 'level' } | ForEach-Object Text) -join ',') -eq '1,2') 'each level is waited for'
	Check (@($v | Where-Object { $_.Kind -eq 'expect' -and $_.Text -eq 'player.controlsBlocked False' }).Count -eq 2) 'the controls are checked for each save'
	Check ((($v | Where-Object { $_.Kind -eq 'command' -and $_.Text -like 'shot*' } | ForEach-Object Text) -join ',') -eq 'shot verify-one,shot verify-two') 'a screenshot per save'
	Check (@($v | Where-Object { $_.Kind -eq 'wait' -and $_.Text -eq '4' }).Count -eq 2) 'a 4 s wait before the check (the level fades in)'
	$order = @($v | ForEach-Object { $_.Kind })
	Check ($order.IndexOf('level') -lt $order.IndexOf('near') -and $order.IndexOf('near') -lt $order.IndexOf('expect')) 'level, then position, then controls'

	Write-Host 'The slot order'
	$lib = Join-Path $root 'lib'
	foreach ($n in 'old', 'mid', 'new') { New-Item -ItemType Directory -Force (Join-Path $lib $n) | Out-Null; Set-Content (Join-Path $lib "$n\gsave.sav") $n }
	(Get-Item (Join-Path $lib 'old\gsave.sav')).LastWriteTime = [datetime]'2026-10-01 10:00:00'
	(Get-Item (Join-Path $lib 'mid\gsave.sav')).LastWriteTime = [datetime]'2026-10-02 10:00:00'
	(Get-Item (Join-Path $lib 'new\gsave.sav')).LastWriteTime = [datetime]'2026-10-03 10:00:00'
	$manifest = @('mid', 'gone', 'old', 'new') | ForEach-Object { [pscustomobject]@{ name = $_ } }
	Check ((Get-ArxVerifyOrder $manifest $lib | ForEach-Object name) -join ',' -eq 'new,mid,old') 'newest save file first, whatever the manifest order' ((Get-ArxVerifyOrder $manifest $lib | ForEach-Object name) -join ',')
	Check (@(Get-ArxVerifyOrder $manifest $lib | Where-Object name -eq 'gone').Count -eq 0) 'a manifest entry without a save in the library is left out'

	Write-Host 'The library helpers'
	$savedEnv = $env:ARXVR_SAVE_LIBRARY
	try {
		$env:ARXVR_SAVE_LIBRARY = $null
		Check ((Get-ArxSaveLibrary) -eq (Join-Path $env:LOCALAPPDATA 'ArxFatalisVR\savelib')) 'the default library is %LOCALAPPDATA%\ArxFatalisVR\savelib' (Get-ArxSaveLibrary)
		$env:ARXVR_SAVE_LIBRARY = 'D:\somewhere\saves'
		Check ((Get-ArxSaveLibrary) -eq 'D:\somewhere\saves') '$env:ARXVR_SAVE_LIBRARY overrides it'
	} finally { $env:ARXVR_SAVE_LIBRARY = $savedEnv }

	$userDir = Join-Path $root 'user'
	New-Item -ItemType Directory -Force (Join-Path $userDir 'save\stale0000'), (Join-Path $userDir 'save\stale0001') | Out-Null
	Set-Content (Join-Path $userDir 'save\stale0000\gsave.sav') 'stale'
	$null = Use-ArxSave -Name 'mid' -UserDir $userDir -Library $lib 6>&1
	$have = @(Get-ChildItem (Join-Path $userDir 'save') -Directory | Select-Object -ExpandProperty Name)
	Check (($have -join ',') -eq 'mid') 'Use-ArxSave leaves exactly one save in the user directory' ($have -join ',')
	Check ((Get-Content (Join-Path $userDir 'save\mid\gsave.sav')) -eq 'mid') 'and it is the named one, with its content'
	Check ((Get-Item (Join-Path $userDir 'save\mid\gsave.sav')).LastWriteTime -eq [datetime]'2026-10-02 10:00:00') 'the copy keeps the file time (the slot order depends on it)'
	$null = Use-ArxSave -Name 'new' -UserDir $userDir -Library $lib 6>&1
	Check ((@(Get-ChildItem (Join-Path $userDir 'save') -Directory | Select-Object -ExpandProperty Name) -join ',') -eq 'new') 'a second Use-ArxSave replaces the first (only one save at a time)'
	$message = Throws { Use-ArxSave -Name 'nonexistent' -UserDir $userDir -Library $lib 6>&1 }
	Check ($message -and $message -like "*save 'nonexistent' not found in $lib*" -and $message -like '*available: *mid*new*old*') 'a missing save fails naming the library and what it has' $message
	$message = Throws { Copy-ArxSaves -Names @('mid', 'nonexistent') -UserDir (Join-Path $root 'user2') -Library $lib 6>&1 }
	Check ($message -like "*save 'nonexistent' not found*") 'Copy-ArxSaves fails the same way for the second of two names'
	$empty = Join-Path $root 'empty'
	$message = Throws { Use-ArxSave -Name 'x' -UserDir $userDir -Library $empty 6>&1 }
	Check ($message -like '*available: none*make-test-saves.ps1*') 'an empty library says to run make-test-saves.ps1' $message

	# --loadslot of a save that is not there (#24: the owner's headset run with no saves showed a black world)
	$noSaves = Join-Path $root 'nosaves'
	Check ($null -eq (Get-ArxLoadSlotProblem -ArxArgs @('--skiplogo') -UserDir $noSaves)) 'no --loadslot: no problem'
	$problem = Get-ArxLoadSlotProblem -ArxArgs @('--skiplogo', '--loadslot', '0') -UserDir $noSaves
	Check ($problem -like '--loadslot 0:*has 0 save(s)*-Save mvp-new-game*') '--loadslot 0 without saves is refused and names the test save' $problem
	New-Item -ItemType Directory -Force (Join-Path $noSaves 'save\empty-folder') | Out-Null
	Check ($null -ne (Get-ArxLoadSlotProblem -ArxArgs @('--loadslot', '0') -UserDir $noSaves)) 'a save folder without gsave.sav is not a save'
	$oneSave = Join-Path $root 'onesave'
	$null = Use-ArxSave -Name 'mid' -UserDir $oneSave -Library $lib 6>&1
	Check ($null -eq (Get-ArxLoadSlotProblem -ArxArgs @('--skiplogo', '--loadslot', '0') -UserDir $oneSave)) '--loadslot 0 with one save is fine'
	$problem = Get-ArxLoadSlotProblem -ArxArgs @('--loadslot', '1') -UserDir $oneSave
	Check ($problem -like '--loadslot 1:*has 1 save(s)*') '--loadslot 1 with one save is refused' $problem
	Check ((Get-ArxLoadSlotProblem -ArxArgs @('--loadslot') -UserDir $oneSave) -eq '--loadslot needs a slot number') '--loadslot without a number is refused'
} finally {
	Remove-Item -Recurse -Force $root -ErrorAction SilentlyContinue
}

Write-Host ''
if ($script:failed) { Write-Host "$script:failed check(s) FAILED" -ForegroundColor Red; exit 1 }
Write-Host 'all checks passed' -ForegroundColor Green
