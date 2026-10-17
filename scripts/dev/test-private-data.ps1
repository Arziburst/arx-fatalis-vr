<#
.SYNOPSIS
	Self-test of check-private-data.ps1 (#428): made-up personal values fail, placeholders and allowed files pass.
#>
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'check-private-data.ps1')

$script:failed = 0
function Check([bool]$Ok, [string]$What) {
	if ($Ok) { Write-Host "  ok   $What" } else { Write-Host "  FAIL $What" -ForegroundColor Red; $script:failed++ }
}

# One fixture file per case: path -> text. All values are made up.
function Run([hashtable]$Files) {
	$read = { param($p) $Files[$p] }
	@(Find-PrivateInFiles @($Files.Keys) $read)
}

$bad = [ordered]@{
	'a personal home path'      = @{ 'docs/vr/x.md' = 'see C:\Users\someone\work\file.txt' }
	'a forward-slash home path' = @{ 'docs/vr/x.md' = 'see C:/Users/someone/work' }
	'a unix home path'          = @{ 'scripts/x.ps1' = 'cd /home/someone/src' }
	'a Steam userdata id'       = @{ 'docs/vr/x.md' = 'C:\Steam\userdata\12345678\config' }
	'a 64-bit Steam id'         = @{ 'docs/vr/x.md' = 'id 76561198012345678' }
	'an e-mail address'         = @{ 'README.md' = 'write to someone@somewhere.net' }
	'a docs image'              = @{ 'docs/vr/images/shot.jpg' = 'not read' }
	'a docs png'                = @{ 'docs/shot.PNG' = 'not read' }
}
foreach ($case in $bad.Keys) { Check ((Run $bad[$case]).Count -ge 1) "fails: $case" }

$good = [ordered]@{
	'placeholder user'        = @{ 'docs/vr/x.md' = 'C:\Users\<user>\arx and C:\Users\<name>' }
	'an environment variable' = @{ 'docs/vr/x.md' = '%USERPROFILE%\arx-fatalis-vr-wt and C:\Users\%USERNAME%' }
	'a built-in profile'      = @{ 'docs/vr/x.md' = 'C:\Users\Public\Documents, C:\Users\Default' }
	'an elided path'          = @{ 'docs/vr/x.md' = "C:\Users\$([char]0x2026)\Release\arx.exe" }
	'a placeholder userdata'  = @{ 'docs/vr/x.md' = '<Steam>\userdata\<account>\config' }
	'the Steam id base'       = @{ 'scripts/x.ps1' = '$id - 76561197960265728' }
	'an example e-mail'       = @{ 'docs/vr/x.md' = 'someone@example.com, 1+someone@users.noreply.github.com' }
	'an allowed fixture file' = @{ 'scripts/dev/log-digest-lib.ps1' = 'C:\Users\someone 76561198012345678 someone@somewhere.net' }
	'a CC0 asset outside docs' = @{ 'data/core/vr/tile.png' = 'binary' }
}
foreach ($case in $good.Keys) {
	$f = Run $good[$case]
	Check ($f.Count -eq 0) "passes: $case $($f -join '; ')"
}

if ($script:failed) { Write-Host "test-private-data: $script:failed failed" -ForegroundColor Red; exit 1 }
Write-Host 'test-private-data: all passed' -ForegroundColor Green
exit 0
