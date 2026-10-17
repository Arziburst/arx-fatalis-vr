<#
.SYNOPSIS
	Compose a labelled sheet from capture shots: the picture a PR puts next to its baseline.

.DESCRIPTION
	Takes the named shots of a test directory's capture folder (the .bmp files a sequence's @shot
	wrote, one eye of them), lays them out in a grid with a label over each one, and writes one JPEG.
	How the baseline sheet of research section 16g was made, so a fix is judged against the same
	eight shots at the same scale (docs/vr/images/arms-298-baseline.jpg: 4 columns, the right eye).

.PARAMETER Shots
	Shot names in order, e.g. down-roll000, down-roll045. The file is <name>-<Eye>.bmp in -CaptureDir,
	or <name>.bmp for a flat capture.

.PARAMETER Labels
	One label per shot (default: the shot name).

.PARAMETER CaptureDir
	The capture folder. Default: the test channel directory of this working copy (build-paths.ps1) + capture.

.PARAMETER Out
	The JPEG to write (the folder is created).

.PARAMETER Columns
	Grid columns (default 4).

.PARAMETER Width
	Width of one tile in pixels (default 480); the height keeps the shot's aspect.

.PARAMETER Eye
	left, right (default) or quad (the flat frame); ignored when the shot has no per-eye files.

.EXAMPLE
	.\scripts\dev\shot-sheet.ps1 -Shots down-roll000,down-roll045,down-roll090,down-roll180,fwd-roll000,fwd-roll090,fwd-roll-090,fwd-roll180 `
	    -Labels 'hand down, roll 0','hand down, roll 45','hand down, roll 90','hand down, roll 180','hand forward, roll 0','hand forward, roll +90','hand forward, roll -90','hand forward, roll 180' `
	    -Out $env:LOCALAPPDATA\ArxFatalisVR\evidence\arms-roll.jpg
#>
[CmdletBinding()]
param(
	[Parameter(Mandatory)][string[]] $Shots,
	[string[]] $Labels,
	[string] $CaptureDir,
	[Parameter(Mandatory)][string] $Out,
	[int] $Columns = 4,
	[int] $Width = 480,
	[ValidateSet('left', 'right', 'quad')][string] $Eye = 'right',
	[int] $Quality = 85
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

if(-not $CaptureDir) {
	. (Join-Path $PSScriptRoot '..\build-paths.ps1')
	$sourceDir = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
	$CaptureDir = Join-Path (Get-DefaultTestDir $sourceDir) 'capture'
}
if(-not (Test-Path $CaptureDir)) {
	throw "capture folder not found: $CaptureDir"
}
if($Labels -and $Labels.Count -ne $Shots.Count) {
	throw "Labels ($($Labels.Count)) must match Shots ($($Shots.Count))"
}

$files = foreach($shot in $Shots) {
	$perEye = Join-Path $CaptureDir "$shot-$Eye.bmp"
	$flat = Join-Path $CaptureDir "$shot.bmp"
	if(Test-Path $perEye) { $perEye } elseif(Test-Path $flat) { $flat } else { throw "shot not found: $shot ($perEye)" }
}

$images = @($files | ForEach-Object { [System.Drawing.Image]::FromFile($_) })
try {
	$tileWidth = $Width
	$tileHeight = [int][math]::Round($Width * $images[0].Height / $images[0].Width)
	$rows = [int][math]::Ceiling($Shots.Count / $Columns)
	$sheet = New-Object System.Drawing.Bitmap ($tileWidth * $Columns), ($tileHeight * $rows)
	$graphics = [System.Drawing.Graphics]::FromImage($sheet)
	$graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
	$graphics.Clear([System.Drawing.Color]::Black)
	$font = New-Object System.Drawing.Font 'Segoe UI', ([int][math]::Max(10, $Width / 30)), ([System.Drawing.FontStyle]::Bold)
	$white = [System.Drawing.Brushes]::White
	$shade = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(160, 0, 0, 0))
	for($i = 0; $i -lt $images.Count; $i++) {
		$x = ($i % $Columns) * $tileWidth
		$y = [math]::Floor($i / $Columns) * $tileHeight
		$graphics.DrawImage($images[$i], $x, $y, $tileWidth, $tileHeight)
		$label = if($Labels) { $Labels[$i] } else { $Shots[$i] }
		$size = $graphics.MeasureString($label, $font)
		$graphics.FillRectangle($shade, $x, $y, $size.Width + 12, $size.Height + 6)
		$graphics.DrawString($label, $font, $white, $x + 6, $y + 3)
	}
	$outDir = Split-Path -Parent $Out
	if(-not $outDir) { $outDir = '.' }
	if(-not (Test-Path $outDir)) {
		New-Item -ItemType Directory -Force $outDir | Out-Null
	}
	$outPath = Join-Path (Resolve-Path -LiteralPath $outDir).Path (Split-Path -Leaf $Out)
	$codec = [System.Drawing.Imaging.ImageCodecInfo]::GetImageEncoders() | Where-Object { $_.MimeType -eq 'image/jpeg' }
	$params = New-Object System.Drawing.Imaging.EncoderParameters 1
	$params.Param[0] = New-Object System.Drawing.Imaging.EncoderParameter ([System.Drawing.Imaging.Encoder]::Quality), ([long]$Quality)
	$sheet.Save($outPath, $codec, $params)
	Write-Host ("sheet: {0} ({1} shots, {2}x{3})" -f $outPath, $images.Count, $sheet.Width, $sheet.Height)
	$graphics.Dispose()
	$sheet.Dispose()
} finally {
	foreach($image in $images) { $image.Dispose() }
}
