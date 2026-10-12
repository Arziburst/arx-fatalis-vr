# The test playground (#321): the generated level and the switch that starts it.
# Dot-sourced by run-windows.ps1, test-pr.ps1 and smoke-test.ps1 (-Playground). The generator project is this
# folder (arx-level-generator, build.mjs); the level is generated on the player's own machine from the original
# game's textures into %LOCALAPPDATA%\ArxFatalisVR\playground\level and never enters the repository.

# The area the playground loads as: PlaygroundArea in src/game/Levels.h (layout.mjs LEVEL_INDEX)
$script:ArxPlaygroundArea = 30

function Get-ArxPlaygroundDir {
	$base = if ($env:LOCALAPPDATA) { $env:LOCALAPPDATA } else { [Environment]::GetFolderPath('LocalApplicationData') }
	Join-Path (Join-Path $base 'ArxFatalisVR') 'playground\level'
}

# Generates the level when it is missing or older than the generator's sources; returns the folder (a --data-dir)
function Initialize-ArxPlaygroundLevel {
	$project = $PSScriptRoot
	$out = Get-ArxPlaygroundDir
	$level = Join-Path $out "game\graph\levels\level$script:ArxPlaygroundArea\fast.fts"
	$sources = @('build.mjs', 'layout.mjs', 'package.json', 'package-lock.json') | ForEach-Object { Get-Item -LiteralPath (Join-Path $project $_) }
	$newest = ($sources | Measure-Object LastWriteTime -Maximum).Maximum
	if ((Test-Path -LiteralPath $level) -and (Get-Item -LiteralPath $level).LastWriteTime -gt $newest) {
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
	$out
}

# The arx.exe arguments of the playground: the level's folder as a data dir (it is searched before the Steam data),
# and a new game in its area
function Get-ArxPlaygroundArgs([string]$LevelDir) {
	@('--data-dir', $LevelDir, '--loadlevel', "$script:ArxPlaygroundArea")
}
