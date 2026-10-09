# Read and write Steam's non-Steam game shortcuts (<Steam>\userdata\<account>\config\shortcuts.vdf).
# Used by add-steam-shortcut.ps1 and scripts/dev/test-steam-shortcut.ps1. Dot-source it.
#
# shortcuts.vdf is a binary KeyValues file: every node is one type byte, a NUL-terminated UTF-8 key
# and a value; 0x00 opens a map (its children follow, 0x08 closes it), 0x01 is a NUL-terminated
# string, 0x02 a 32-bit integer. The root is the map "shortcuts" whose children "0", "1", ... are the
# shortcuts. Leaf values are kept as their raw bytes, so a file read and written back unchanged is
# byte-identical and entries we do not touch stay exactly as Steam wrote them.

$script:VdfMap = 0x00
$script:VdfString = 0x01
$script:VdfInt32 = 0x02
$script:VdfEnd = 0x08
# Fixed-size leaf types Steam's binary KeyValues may contain (float, pointer, colour: 4 bytes;
# uint64, int64: 8 bytes). shortcuts.vdf has none today; they are carried through unchanged.
$script:VdfFixedSize = @{ 0x03 = 4; 0x04 = 4; 0x06 = 4; 0x07 = 8; 0x0A = 8 }

function New-VdfNode([int]$Type, [string]$Key, $Value) {
	[pscustomobject]@{ Type = $Type; Key = $Key; Value = $Value }
}

function Read-VdfCString([byte[]]$Bytes, [ref]$Pos) {
	$start = $Pos.Value
	$end = [Array]::IndexOf($Bytes, [byte]0, $start)
	if ($end -lt 0) { throw "binary VDF: unterminated string at offset $start" }
	$Pos.Value = $end + 1
	# $a[$i..($i - 1)] would count down, not be empty
	if ($end -eq $start) { return , [byte[]]@() }
	, [byte[]]$Bytes[$start..($end - 1)]
}

function Read-VdfChildren([byte[]]$Bytes, [ref]$Pos) {
	$children = [Collections.Generic.List[object]]::new()
	while ($true) {
		if ($Pos.Value -ge $Bytes.Length) { throw 'binary VDF: map not closed before the end of the file' }
		$type = $Bytes[$Pos.Value]
		$Pos.Value++
		if ($type -eq $script:VdfEnd) { return , $children }
		$key = [Text.Encoding]::UTF8.GetString((Read-VdfCString $Bytes $Pos))
		if ($type -eq $script:VdfMap) {
			$children.Add((New-VdfNode $type $key (Read-VdfChildren $Bytes $Pos)))
		} elseif ($type -eq $script:VdfString) {
			$children.Add((New-VdfNode $type $key (Read-VdfCString $Bytes $Pos)))
		} elseif ($type -eq $script:VdfInt32 -or $script:VdfFixedSize.ContainsKey([int]$type)) {
			$size = if ($type -eq $script:VdfInt32) { 4 } else { $script:VdfFixedSize[[int]$type] }
			if ($Pos.Value + $size -gt $Bytes.Length) { throw "binary VDF: value of '$key' runs past the end of the file" }
			$children.Add((New-VdfNode $type $key ([byte[]]$Bytes[$Pos.Value..($Pos.Value + $size - 1)])))
			$Pos.Value += $size
		} else {
			throw ('binary VDF: unknown type 0x{0:x2} at offset {1}' -f $type, ($Pos.Value - 1))
		}
	}
}

# Parse binary VDF bytes into a list of top-level nodes.
function ConvertFrom-BinaryVdf([byte[]]$Bytes) {
	$pos = 0
	$nodes = [Collections.Generic.List[object]]::new()
	while ($pos -lt $Bytes.Length) {
		$type = $Bytes[$pos]
		if ($type -eq $script:VdfEnd) { $pos++; continue }   # the file's own closing byte
		if ($type -ne $script:VdfMap) { throw ('binary VDF: top-level node of type 0x{0:x2}, expected a map' -f $type) }
		$pos++
		$key = [Text.Encoding]::UTF8.GetString((Read-VdfCString $Bytes ([ref]$pos)))
		$nodes.Add((New-VdfNode $type $key (Read-VdfChildren $Bytes ([ref]$pos))))
	}
	, $nodes
}

function Write-VdfNodes([IO.Stream]$Stream, $Nodes) {
	foreach ($node in $Nodes) {
		$Stream.WriteByte([byte]$node.Type)
		$key = [Text.Encoding]::UTF8.GetBytes($node.Key)
		$Stream.Write($key, 0, $key.Length)
		$Stream.WriteByte(0)
		if ($node.Type -eq $script:VdfMap) {
			Write-VdfNodes $Stream $node.Value
			$Stream.WriteByte([byte]$script:VdfEnd)
		} else {
			[byte[]]$value = $node.Value
			$Stream.Write($value, 0, $value.Length)
			if ($node.Type -eq $script:VdfString) { $Stream.WriteByte(0) }
		}
	}
}

# Serialize top-level nodes back to binary VDF bytes (the file ends with one extra 0x08, as Steam writes it).
function ConvertTo-BinaryVdf($Nodes) {
	$stream = [IO.MemoryStream]::new()
	Write-VdfNodes $stream $Nodes
	$stream.WriteByte([byte]$script:VdfEnd)
	, $stream.ToArray()
}

function New-VdfString([string]$Key, [string]$Value) { New-VdfNode $script:VdfString $Key ([Text.Encoding]::UTF8.GetBytes($Value)) }
function New-VdfInt32([string]$Key, [int]$Value) { New-VdfNode $script:VdfInt32 $Key ([BitConverter]::GetBytes($Value)) }
function New-VdfMap([string]$Key, $Children) {
	$list = [Collections.Generic.List[object]]::new()
	foreach ($child in $Children) { $list.Add($child) }
	New-VdfNode $script:VdfMap $Key $list
}

# The child of a map node with this key (keys compared case-insensitively, as Steam does), or $null.
function Get-VdfChild($Map, [string]$Key) {
	foreach ($child in $Map.Value) { if ($child.Key -ieq $Key) { return $child } }
	$null
}

function Get-VdfStringValue($Map, [string]$Key) {
	$child = Get-VdfChild $Map $Key
	if ($child -and $child.Type -eq $script:VdfString) { [Text.Encoding]::UTF8.GetString([byte[]]$child.Value) }
}

function Get-VdfInt32Value($Map, [string]$Key) {
	$child = Get-VdfChild $Map $Key
	if ($child -and $child.Type -eq $script:VdfInt32) { [BitConverter]::ToInt32([byte[]]$child.Value, 0) }
}

# Replace the child with the node's key in place (keeps the position), or append it.
function Set-VdfChild($Map, $Node) {
	for ($i = 0; $i -lt $Map.Value.Count; $i++) {
		if ($Map.Value[$i].Key -ieq $Node.Key) { $Map.Value[$i] = $Node; return }
	}
	$Map.Value.Add($Node)
}

# --- Shortcut app id -------------------------------------------------------------------------------

$script:Crc32Table = $null

function Get-Crc32([byte[]]$Bytes) {
	# In [long]: PowerShell's bitwise operators on 32-bit values go through signed int.
	if (-not $script:Crc32Table) {
		$script:Crc32Table = [long[]]::new(256)
		for ([long]$n = 0; $n -lt 256; $n++) {
			$c = $n
			for ($k = 0; $k -lt 8; $k++) {
				$c = if ($c -band 1) { 0xEDB88320L -bxor ($c -shr 1) } else { $c -shr 1 }
			}
			$script:Crc32Table[$n] = $c
		}
	}
	[long]$crc = 0xFFFFFFFFL
	foreach ($b in $Bytes) { $crc = $script:Crc32Table[($crc -bxor $b) -band 0xFF] -bxor ($crc -shr 8) }
	[uint32]($crc -bxor 0xFFFFFFFFL)
}

# The 32-bit app id Steam gives a non-Steam shortcut: CRC32 of the Exe value (as stored, with its
# quotes) followed by the app name, with the top bit set. It names the grid images
# (config\grid\<id>.png) and, shifted, the steam://rungameid/ link (Get-SteamShortcutGameId).
function Get-SteamShortcutAppId([string]$Exe, [string]$AppName) {
	[uint32]([long](Get-Crc32 ([Text.Encoding]::UTF8.GetBytes($Exe + $AppName))) -bor 0x80000000L)
}

# The 64-bit game id for steam://rungameid/<id>.
function Get-SteamShortcutGameId([uint32]$AppId) {
	([uint64]$AppId -shl 32) -bor [uint64]0x02000000L
}

# --- Shortcuts -------------------------------------------------------------------------------------

# The "shortcuts" map of parsed nodes; created when the file had none.
function Get-ShortcutsMap($Nodes) {
	foreach ($node in $Nodes) { if ($node.Key -ieq 'shortcuts' -and $node.Type -eq $script:VdfMap) { return $node } }
	$map = New-VdfMap 'shortcuts' @()
	$Nodes.Add($map)
	$map
}

# Shortcut entries whose AppName equals $AppName (case-insensitive).
function Find-Shortcut($ShortcutsMap, [string]$AppName) {
	@($ShortcutsMap.Value | Where-Object { $_.Type -eq $script:VdfMap -and (Get-VdfStringValue $_ 'AppName') -ieq $AppName })
}

# Steam numbers the entries "0", "1", ... in order; keep that after a removal.
function Update-ShortcutIndexes($ShortcutsMap) {
	for ($i = 0; $i -lt $ShortcutsMap.Value.Count; $i++) { $ShortcutsMap.Value[$i].Key = "$i" }
}

# Add the shortcut, or update the existing one with the same AppName in place (fields Steam keeps
# for itself - LastPlayTime, tags, anything unknown - stay as they are). Returns the app id.
function Set-Shortcut($ShortcutsMap, [string]$AppName, [string]$ExePath, [string]$IconPath, [string]$LaunchOptions, [bool]$OpenVR) {
	$exe = '"' + $ExePath + '"'
	$startDir = '"' + (Split-Path -Parent $ExePath) + '\"'
	[uint32]$appId = Get-SteamShortcutAppId $exe $AppName
	$fields = @(
		(New-VdfInt32 'appid' ([BitConverter]::ToInt32([BitConverter]::GetBytes($appId), 0))),
		(New-VdfString 'AppName' $AppName),
		(New-VdfString 'Exe' $exe),
		(New-VdfString 'StartDir' $startDir),
		(New-VdfString 'icon' $IconPath),
		(New-VdfString 'LaunchOptions' $LaunchOptions),
		(New-VdfInt32 'OpenVR' ([int]$OpenVR))
	)
	$existing = @(Find-Shortcut $ShortcutsMap $AppName)
	if ($existing.Count -gt 0) {
		foreach ($field in $fields) { Set-VdfChild $existing[0] $field }
		# Duplicates (from editing by hand) would show as two library entries: keep the first.
		foreach ($dup in ($existing | Select-Object -Skip 1)) { [void]$ShortcutsMap.Value.Remove($dup) }
		Update-ShortcutIndexes $ShortcutsMap
		return $appId
	}
	$entry = New-VdfMap "$($ShortcutsMap.Value.Count)" ($fields + @(
		(New-VdfString 'ShortcutPath' ''),
		(New-VdfInt32 'IsHidden' 0),
		(New-VdfInt32 'AllowDesktopConfig' 1),
		(New-VdfInt32 'AllowOverlay' 1),
		(New-VdfInt32 'Devkit' 0),
		(New-VdfString 'DevkitGameID' ''),
		(New-VdfInt32 'DevkitOverrideAppID' 0),
		(New-VdfInt32 'LastPlayTime' 0),
		(New-VdfString 'FlatpakAppID' ''),
		(New-VdfMap 'tags' @())
	))
	$ShortcutsMap.Value.Add($entry)
	$appId
}

# Remove every shortcut with this AppName; returns the app ids they had (for their grid images).
function Remove-Shortcut($ShortcutsMap, [string]$AppName) {
	$ids = @()
	foreach ($entry in @(Find-Shortcut $ShortcutsMap $AppName)) {
		$id = Get-VdfInt32Value $entry 'appid'
		if ($null -ne $id) { $ids += [BitConverter]::ToUInt32([BitConverter]::GetBytes([int]$id), 0) }
		[void]$ShortcutsMap.Value.Remove($entry)
	}
	Update-ShortcutIndexes $ShortcutsMap
	, $ids
}
