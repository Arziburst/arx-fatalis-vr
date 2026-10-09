<#
.SYNOPSIS
	Self-test of add-steam-shortcut.ps1 (#80): Steam's shortcuts.vdf on copies only - no Steam, no game.

.DESCRIPTION
	Builds a binary shortcuts.vdf by hand (two foreign shortcuts with tags, a non-ASCII name and an
	8-byte value) in a scratch folder and checks:
	  - parsing and writing it back is byte-identical
	  - the CRC32 and the shortcut app id follow Steam's formula
	  - adding "Arx Fatalis VR" appends one entry with OpenVR = 1, Exe, StartDir, icon, the app id;
	    the foreign entries stay byte for byte; a backup of the original is written; grid images appear
	  - a second run changes nothing; a run with other launch options updates in place, no duplicate
	  - a run against another arx.exe (another app id) moves the grid images to the new id
	  - -Remove restores the original bytes and deletes the grid images
	  - an empty file (13 bytes, as Steam writes it) and a missing file both work
	  - a read-only copy of this PC's real shortcuts.vdf round-trips (skipped without Steam)
	Exit code 0 when everything passed, 1 otherwise.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$scripts = Split-Path -Parent $PSScriptRoot
. (Join-Path $scripts 'build-paths.ps1')
. (Join-Path $scripts 'steam-shortcuts-lib.ps1')
$addShortcut = Join-Path $scripts 'add-steam-shortcut.ps1'

$root = Join-Path ([IO.Path]::GetTempPath()) "arxvr-shortcut-test-$PID"
if (Test-Path $root) { Remove-Item -Recurse -Force $root }
$config = Join-Path $root 'config'
$vdf = Join-Path $config 'shortcuts.vdf'
$grid = Join-Path $config 'grid'
$buildA = Join-Path $root 'build A'
$buildB = Join-Path $root 'build-b'
New-Item -ItemType Directory -Force $config, $buildA, $buildB | Out-Null
foreach ($dir in $buildA, $buildB) { Set-Content (Join-Path $dir 'arx.exe') 'not a real exe' }

$failures = 0
function Check([bool]$Ok, [string]$What) {
	if ($Ok) { Write-Host "  [ok]   $What" -ForegroundColor Green } else { Write-Host "  [FAIL] $What" -ForegroundColor Red; $script:failures++ }
}
function Same([byte[]]$A, [byte[]]$B) { [Linq.Enumerable]::SequenceEqual($A, $B) }
function Add([hashtable]$More = @{}) {
	$params = @{ ShortcutsFile = $vdf }
	& $addShortcut @params @More *>&1 | Out-Null
}

# --- A shortcuts.vdf written byte by byte (not by the code under test) ------------------------------
$bytes = [Collections.Generic.List[byte]]::new()
function B([int[]]$Values) { foreach ($v in $Values) { $bytes.Add([byte]$v) } }
function K([int]$Type, [string]$Key) { B $Type; $bytes.AddRange([Text.Encoding]::UTF8.GetBytes($Key)); B 0 }
function S([string]$Key, [string]$Value) { K 1 $Key; $bytes.AddRange([Text.Encoding]::UTF8.GetBytes($Value)); B 0 }
function I([string]$Key, [int]$Value) { K 2 $Key; $bytes.AddRange([BitConverter]::GetBytes($Value)) }
K 0 'shortcuts'
K 0 '0'
I 'appid' -123456789; S 'AppName' 'Some Game'; S 'Exe' '"D:\Games\Some\game.exe"'; S 'StartDir' '"D:\Games\Some\"'
S 'icon' ''; S 'LaunchOptions' '-fullscreen'; I 'OpenVR' 0; I 'LastPlayTime' 1700000000
K 0 'tags'; S '0' 'favorite'; S '1' 'Ролевые'; B 8
B 8
K 0 '1'
I 'appid' -2; S 'AppName' 'Ёлка ☃'; S 'Exe' '"C:\Program Files\Ёлка\ёлка.exe"'
K 7 'SomeUInt64'; B 1, 2, 3, 4, 5, 6, 7, 8
K 0 'tags'; B 8
B 8
B 8   # shortcuts
B 8   # file
[byte[]]$fixture = $bytes.ToArray()
$foreign = @{}

Write-Host 'Binary VDF'
$nodes = ConvertFrom-BinaryVdf $fixture
Check (Same $fixture (ConvertTo-BinaryVdf $nodes)) 'hand-written file round-trips byte-identical'
$map = Get-ShortcutsMap $nodes
Check ($map.Value.Count -eq 2) "two shortcuts parsed (got $($map.Value.Count))"
Check ((Get-VdfStringValue $map.Value[1] 'AppName') -eq 'Ёлка ☃') 'non-ASCII name decoded'
Check ((Get-VdfInt32Value $map.Value[0] 'LastPlayTime') -eq 1700000000) 'int32 decoded'
foreach ($entry in $map.Value) { $foreign[(Get-VdfStringValue $entry 'AppName')] = ConvertTo-BinaryVdf (, $entry) }
$truncated = $fixture[0..($fixture.Length - 4)]
$threw = $false; try { ConvertFrom-BinaryVdf $truncated | Out-Null } catch { $threw = $true }
Check $threw 'a truncated file is an error, not a silent partial parse'

Write-Host 'App id'
Check ((Get-Crc32 ([Text.Encoding]::ASCII.GetBytes('123456789'))) -eq 0xCBF43926L) 'CRC32 check value of "123456789" is cbf43926'
$id = Get-SteamShortcutAppId '"C:\arx.exe"' 'Arx Fatalis VR'
$expected = [long](Get-Crc32 ([Text.Encoding]::UTF8.GetBytes('"C:\arx.exe"Arx Fatalis VR'))) -bor 0x80000000L
Check ($id -eq $expected -and ($id -band 0x80000000L)) "app id = CRC32(exe + name) | 0x80000000 ($id)"
Check ((Get-SteamShortcutGameId $id) -eq (([uint64]$id -shl 32) + 0x02000000L)) 'rungameid = id << 32 | 0x02000000'

function Get-Ours {
	$m = Get-ShortcutsMap (ConvertFrom-BinaryVdf ([IO.File]::ReadAllBytes($vdf)))
	[pscustomobject]@{ Map = $m; Ours = @(Find-Shortcut $m 'Arx Fatalis VR') }
}
function Check-ForeignUntouched([string]$When) {
	$m = (Get-Ours).Map
	$ok = $true
	foreach ($name in $foreign.Keys) {
		$e = @(Find-Shortcut $m $name)
		if ($e.Count -ne 1 -or -not (Same $foreign[$name] (ConvertTo-BinaryVdf (, $e[0])))) { $ok = $false }
	}
	Check $ok "foreign shortcuts byte-identical $When"
}

Write-Host 'Add'
[IO.File]::WriteAllBytes($vdf, $fixture)
Add @{ BuildDir = $buildA }
$state = Get-Ours
$exeA = [IO.Path]::GetFullPath((Join-Path $buildA 'arx.exe'))
Check ($state.Map.Value.Count -eq 3 -and $state.Ours.Count -eq 1) 'one entry appended'
$ours = $state.Ours[0]
Check ($ours.Key -eq '2') "numbered after the others ('$($ours.Key)')"
Check ((Get-VdfInt32Value $ours 'OpenVR') -eq 1) 'OpenVR = 1 (VR library)'
Check ((Get-VdfStringValue $ours 'Exe') -eq "`"$exeA`"") 'Exe is the quoted arx.exe path'
Check ((Get-VdfStringValue $ours 'StartDir') -eq "`"$buildA\`"") 'StartDir is its folder'
Check ((Get-VdfStringValue $ours 'icon') -like '*\data\vr\ARXVR.ico') 'icon is data\vr\ARXVR.ico'
Check ((Get-VdfStringValue $ours 'LaunchOptions') -eq '') 'no launch options by default'
$idA = Get-SteamShortcutAppId "`"$exeA`"" 'Arx Fatalis VR'
Check ([BitConverter]::ToUInt32([BitConverter]::GetBytes([int](Get-VdfInt32Value $ours 'appid')), 0) -eq $idA) 'appid field = computed app id'
Check-ForeignUntouched 'after adding'
$backups = @(Get-ChildItem $config -Filter 'shortcuts.vdf.arxvr-*.bak')
Check ($backups.Count -eq 1 -and (Same $fixture ([IO.File]::ReadAllBytes($backups[0].FullName)))) 'backup holds the original bytes'
Check ((Test-Path (Join-Path $grid "$idA.png")) -and (Test-Path (Join-Path $grid "${idA}_hero.png"))) 'grid images under the app id'

Write-Host 'Update'
[byte[]]$afterAdd = [IO.File]::ReadAllBytes($vdf)
Add @{ BuildDir = $buildA }
Check (Same $afterAdd ([IO.File]::ReadAllBytes($vdf))) 'second run changes nothing'
Check (@(Get-ChildItem $config -Filter '*.bak').Count -eq 1) 'and writes no backup'
Add @{ BuildDir = $buildA; LaunchOptions = '--loadslot 0' }
$state = Get-Ours
Check ($state.Ours.Count -eq 1 -and (Get-VdfStringValue $state.Ours[0] 'LaunchOptions') -eq '--loadslot 0') 'launch options updated in place, no duplicate'
Check-ForeignUntouched 'after updating'
Add @{ BuildDir = $buildB }
$exeB = [IO.Path]::GetFullPath((Join-Path $buildB 'arx.exe'))
$idB = Get-SteamShortcutAppId "`"$exeB`"" 'Arx Fatalis VR'
$state = Get-Ours
Check ($state.Ours.Count -eq 1 -and (Get-VdfStringValue $state.Ours[0] 'Exe') -eq "`"$exeB`"") 'another arx.exe replaces the path'
Check ((Test-Path (Join-Path $grid "$idB.png")) -and -not (Test-Path (Join-Path $grid "$idA.png")) -and -not (Test-Path (Join-Path $grid "${idA}_hero.png"))) 'grid images moved to the new app id'

Write-Host 'Remove'
Add @{ Remove = $true }
Check (Same $fixture ([IO.File]::ReadAllBytes($vdf))) 'original bytes restored'
Check (-not (Test-Path (Join-Path $grid "$idB.png")) -and -not (Test-Path (Join-Path $grid "${idB}_hero.png"))) 'grid images deleted'

Write-Host 'Empty and missing file'
[byte[]]$empty = 0, 0x73, 0x68, 0x6F, 0x72, 0x74, 0x63, 0x75, 0x74, 0x73, 0, 8, 8
Check (Same $empty (ConvertTo-BinaryVdf (ConvertFrom-BinaryVdf $empty))) 'Steam''s empty file (13 bytes) round-trips'
[IO.File]::WriteAllBytes($vdf, $empty)
Add @{ BuildDir = $buildA }
Check ((Get-Ours).Ours.Count -eq 1) 'added to an empty file'
Add @{ Remove = $true }
Check (Same $empty ([IO.File]::ReadAllBytes($vdf))) 'removed again: back to the empty file'
Remove-Item $vdf
Add @{ BuildDir = $buildA }
Check ((Get-Ours).Ours.Count -eq 1) 'a missing file is created'

Write-Host 'This PC (read-only copy)'
$steam = Get-SteamPath
$real = if ($steam) { @(Get-ChildItem (Join-Path $steam 'userdata') -Directory -ErrorAction SilentlyContinue | ForEach-Object { Join-Path $_.FullName 'config\shortcuts.vdf' } | Where-Object { Test-Path $_ }) } else { @() }
if (-not $real.Count) {
	Write-Host '  [skip] no shortcuts.vdf on this PC'
}
foreach ($file in $real) {
	[byte[]]$copy = [IO.File]::ReadAllBytes($file)   # read only: the file itself is never opened for writing
	$n = Get-ShortcutsMap (ConvertFrom-BinaryVdf $copy)
	Check (Same $copy (ConvertTo-BinaryVdf (ConvertFrom-BinaryVdf $copy))) "$file round-trips ($($copy.Length) bytes, $($n.Value.Count) shortcuts)"
}

Remove-Item -Recurse -Force $root
if ($failures) { Write-Host "$failures check(s) failed" -ForegroundColor Red; exit 1 }
Write-Host 'All checks passed' -ForegroundColor Green
exit 0
