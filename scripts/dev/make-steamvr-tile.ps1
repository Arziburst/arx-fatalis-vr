<#
.SYNOPSIS
	Derive the 460x215 SteamVR tile (data\vr\steamvr-tile.png) from the key art (data\vr\key-art.png).

.DESCRIPTION
	460x215 is the size SteamVR and Steam use for application tiles ("header" images). The key art is
	16:9, so it is scaled to 460 px wide and cropped vertically; the crop offset keeps the lettering
	whole and cuts the dark ceiling and the ground instead. Re-run this after the key art changes.

	Requires ImageMagick 7 (magick.exe on PATH, or the default install location).

.PARAMETER Offset
	Vertical crop offset in pixels after scaling (default 14).
#>
[CmdletBinding()]
param(
	[int]$Offset = 14
)

$ErrorActionPreference = 'Stop'
$SourceDir = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$KeyArt = Join-Path $SourceDir 'data\vr\key-art.png'
$Tile = Join-Path $SourceDir 'data\vr\steamvr-tile.png'

$magick = (Get-Command magick -ErrorAction SilentlyContinue).Source
if (-not $magick) {
	$magick = Get-ChildItem -Path "$env:ProgramFiles\ImageMagick-*\magick.exe" -ErrorAction SilentlyContinue |
		Select-Object -First 1 -ExpandProperty FullName
}
if (-not $magick) { throw 'ImageMagick (magick.exe) not found.' }
if (-not (Test-Path $KeyArt)) { throw "$KeyArt not found." }

& $magick $KeyArt -resize 460x -gravity North -crop "460x215+0+$Offset" +repage -strip $Tile
if ($LASTEXITCODE) { throw 'magick failed.' }
Write-Host "Wrote $Tile"
& $magick identify $Tile
