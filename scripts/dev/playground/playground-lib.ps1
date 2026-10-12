# The test playground (#321): the generated level and the switch that starts it.
# Dot-sourced by run-windows.ps1, test-pr.ps1 and smoke-test.ps1 (-Playground). The generator project is this
# folder (arx-level-generator, build.mjs); the level is generated on the player's own machine from the original
# game's textures into %LOCALAPPDATA%\ArxFatalisVR\playground\<folder>-<hash>\ - one per working copy, like the
# build dirs - and never enters the repository.

# The area the playground loads as: PlaygroundArea in src/game/Levels.h (layout.mjs LEVEL_INDEX)
$script:ArxPlaygroundArea = 30

# The generator's inputs: the level is up to date when their hash is the one it was generated from
$script:ArxPlaygroundSources = @('build.mjs', 'layout.mjs', 'package.json', 'package-lock.json')

# This working copy's output folder: <folder>-<hash of the working copy's full path>, so parallel worktrees
# (and the integration branch) never overwrite each other's level
function Get-ArxPlaygroundDir {
	$base = if ($env:LOCALAPPDATA) { $env:LOCALAPPDATA } else { [Environment]::GetFolderPath('LocalApplicationData') }
	$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path.TrimEnd('\')
	$sha = [Security.Cryptography.SHA1]::Create()
	$hash = ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($root.ToLowerInvariant()))) -replace '-', '').Substring(0, 8).ToLowerInvariant()
	Join-Path (Join-Path (Join-Path $base 'ArxFatalisVR') 'playground') ((Split-Path $root -Leaf).ToLowerInvariant() + "-$hash")
}

# A hash over the generator's source files (names and contents)
function Get-ArxPlaygroundSourceHash {
	$stream = [IO.MemoryStream]::new()
	foreach ($name in $script:ArxPlaygroundSources) {
		$nameBytes = [Text.Encoding]::UTF8.GetBytes($name + "`n")
		$stream.Write($nameBytes, 0, $nameBytes.Length)
		$content = [IO.File]::ReadAllBytes((Join-Path $PSScriptRoot $name))
		$stream.Write($content, 0, $content.Length)
	}
	$stream.Position = 0
	([BitConverter]::ToString([Security.Cryptography.SHA256]::Create().ComputeHash($stream)) -replace '-', '').ToLowerInvariant()
}

# Generates the level when it is missing or was generated from other sources; returns the folder (a --data-dir)
function Initialize-ArxPlaygroundLevel {
	$project = $PSScriptRoot
	$out = Get-ArxPlaygroundDir
	$level = Join-Path $out "game\graph\levels\level$script:ArxPlaygroundArea\fast.fts"
	$stamp = Join-Path $out 'sources.sha256'
	$hash = Get-ArxPlaygroundSourceHash
	if ((Test-Path -LiteralPath $level) -and (Test-Path -LiteralPath $stamp) -and ((Get-Content -LiteralPath $stamp -Raw).Trim() -eq $hash)) {
		Write-Host "    playground level: up to date ($out)"
		return $out
	}
	if (-not (Get-Command node -ErrorAction SilentlyContinue)) { throw 'the playground level is generated with node (https://nodejs.org, v20 or newer): node was not found' }
	Write-Host "==> Generating the playground level into $out" -ForegroundColor Cyan
	Push-Location $project
	try {
		if (-not (Test-Path -LiteralPath (Join-Path $project 'node_modules\arx-level-generator'))) {
			& npm ci --no-audit --no-fund | Out-Host
			if ($LASTEXITCODE -ne 0) { throw "npm ci failed (exit $LASTEXITCODE) in $project" }
		}
		& node build.mjs $out | Out-Host
		if ($LASTEXITCODE -ne 0) { throw "the playground generator failed (exit $LASTEXITCODE)" }
	} finally { Pop-Location }
	if (-not (Test-Path -LiteralPath $level)) { throw "the generator did not write $level" }
	[IO.File]::WriteAllText($stamp, $hash, [Text.UTF8Encoding]::new($false))
	$out
}

# The arx.exe arguments of the playground: the level's folder as a data dir (it is searched before the Steam data),
# and a new game in its area
function Get-ArxPlaygroundArgs([string]$LevelDir) {
	@('--data-dir', $LevelDir, '--loadlevel', "$script:ArxPlaygroundArea")
}
