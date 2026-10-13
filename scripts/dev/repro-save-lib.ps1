# A reported save made playable (#417): the parts of scripts/dev/repro-save.ps1 that need no game. Dot-source it:
#   . (Join-Path $PSScriptRoot 'repro-save-lib.ps1')
# Host-tested by scripts/dev/test-repro-save.ps1 (run-script-tests.ps1, CI).

Add-Type -AssemblyName System.IO.Compression.FileSystem

# Find the save in what a player sent: the zip of "Export a save for a bug report" (<slot>/gsave.sav, arx.log,
# arx-perf.log, cfg.ini), a save folder (one holding gsave.sav), or a gsave.sav itself. Returns Name (the slot
# folder), Bytes (the save) and Time (its write time, which the game's slot order reads), or throws saying why.
function Read-ArxReportedSave([string]$Path) {
	if (-not (Test-Path -LiteralPath $Path)) { throw "not found: $Path" }
	$item = Get-Item -LiteralPath $Path
	if ($item.PSIsContainer) {
		$file = Join-Path $item.FullName 'gsave.sav'
		if (-not (Test-Path -LiteralPath $file)) { throw "no gsave.sav in $Path (give the zip, a save folder or its gsave.sav)" }
		$item = Get-Item -LiteralPath $file
	}
	if ($item.Name -ieq 'gsave.sav') {
		return [pscustomobject]@{ Name = $item.Directory.Name; Bytes = [IO.File]::ReadAllBytes($item.FullName); Time = $item.LastWriteTime }
	}
	if ($item.Extension -ine '.zip') { throw "$Path is neither a zip, a save folder nor a gsave.sav" }
	$zip = [IO.Compression.ZipFile]::OpenRead($item.FullName)
	try {
		$saves = @($zip.Entries | Where-Object { $_.FullName -match '(^|/)([^/]+)/gsave\.sav$' })
		if ($saves.Count -ne 1) { throw "$Path holds $($saves.Count) saves (<slot>/gsave.sav), expected exactly one" }
		$entry = $saves[0]
		$null = $entry.FullName -match '(^|/)([^/]+)/gsave\.sav$'
		$name = $Matches[2]
		$stream = $entry.Open()
		try {
			$buffer = [IO.MemoryStream]::new()
			$stream.CopyTo($buffer)
			[pscustomobject]@{ Name = $name; Bytes = $buffer.ToArray(); Time = $entry.LastWriteTime.LocalDateTime }
		} finally { $stream.Dispose() }
	} finally { $zip.Dispose() }
}

# Make the reported save the ONLY save of $UserDir (so --loadslot 0 is it); everything else of the user dir is
# emptied first - log, config, saves of an earlier repro. Never copies the reporter's cfg.ini: the developer's
# settings stay theirs. Returns the save's folder.
function Install-ArxReportedSave($Save, [string]$UserDir) {
	if (Test-Path -LiteralPath $UserDir) { Remove-Item -LiteralPath $UserDir -Recurse -Force }
	$folder = Join-Path $UserDir "save\$($Save.Name)"
	New-Item -ItemType Directory -Force $folder | Out-Null
	$file = Join-Path $folder 'gsave.sav'
	[IO.File]::WriteAllBytes($file, $Save.Bytes)
	(Get-Item -LiteralPath $file).LastWriteTime = $Save.Time
	$folder
}

# The arx.exe arguments of a repro run: its own user, config and test channel directories, the save as slot 0, and
# the simulated headset when one is named. Without one the run is VR on the real headset (the save is a VR save).
function Get-ArxReproArgs([string]$UserDir, [string]$TestDir, [string]$SimScript, [string]$DataDir) {
	$argList = @('--windowed', '--user-dir', "`"$UserDir`"", '--config-dir', "`"$UserDir`"", '--test-dir', "`"$TestDir`"",
		'--loadslot', '0')
	if ($SimScript) { $argList += '--vr-sim', "`"$SimScript`"", '--mute' }
	if ($DataDir) { $argList += '--data-dir', "`"$DataDir`"" }
	$argList
}

# Where the loaded game stands, from status.json: "area 1, player at x 10700.0 y 2935.0 z 11400.0 (yaw 72.0)"
function Format-ArxReproPosition($Status) {
	if (-not $Status -or -not $Status.player) { return "area $($Status.area), no player" }
	$c = [Globalization.CultureInfo]::InvariantCulture
	'area {0}, player at x {1} y {2} z {3} (yaw {4})' -f $Status.area, ([double]$Status.player.x).ToString('0.0', $c),
		([double]$Status.player.y).ToString('0.0', $c), ([double]$Status.player.z).ToString('0.0', $c), ([double]$Status.player.yaw).ToString('0.0', $c)
}
