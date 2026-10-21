<#
.SYNOPSIS
	Transcribes the voice notes of the marks of the moment (#574) locally: note.wav -> transcript.txt beside it.

.DESCRIPTION
	A hold of both Index trackpads records a voice note (note.wav, 16 kHz mono) in its marker folder
	(<user dir>\marks\<yyyymmdd>T<hhmmss>Z-<n>\, docs/vr/TESTING.md "Marks of the moment"). This script turns it into
	transcript.txt, on this machine - nothing is uploaded anywhere:

	  [<m>:<ss.s>] <text>      one line per segment, the time from the note's start = from the press (the snapshots'
	                           timeline), rounded down to 0.1 s; UTF-8 without a BOM

	The engine is whisper.cpp (whisper-cli.exe) with the model ggml-large-v3-turbo-q8_0.bin, in Russian (-l ru) with
	a prompt of the project's words (scripts/dev/transcribe-glossary.txt: B, trigger, grip, trackpad, radial,
	SteamVR, Index, ... - editable) so mixed Russian and English comes out right, and the Silero VAD model; when the
	VAD finds no speech (quiet short notes) it runs once more without it.

	First use: the three files are downloaded from their pinned URLs (a release of whisper.cpp; Hugging Face at a fixed
	revision, never main) into the tools directory, each checked against its pinned SHA-256 - a mismatch deletes the
	file and fails with "SHA-256 mismatch". Once cached they are used as they are: no network. About 900 MB in all;
	docs/vr/TESTING.md has the table (versions, licences, hashes).

.PARAMETER Mark
	One mark: its folder name, or "last" for the newest.

.PARAMETER All
	Every mark with a note.wav and no transcript.txt.

.PARAMETER Force
	Transcribe again when transcript.txt is already there (without it such a mark is skipped).

.PARAMETER UserDir
	The game's user directory. Default: this working copy's isolated user directory when it has marks, else the
	player's Saved Games\Arx Libertatis (the owner's headset runs) - as marks.ps1.

.PARAMETER ToolsDir
	Where whisper.cpp and the models are cached. Default: %LOCALAPPDATA%\ArxFatalisVR\tools.

.PARAMETER Glossary
	The prompt's words: a UTF-8 file, one word or phrase a line, '#' comments. Default: scripts/dev/transcribe-glossary.txt.

.PARAMETER PinsFile
	(tests) Replaces the pinned table: lines "<role> <sha256> <url>", role whisper | model | vad; a file:/// URL is
	copied from the local file.

.PARAMETER WhisperCli
	(tests) Runs this program (or .ps1, with pwsh) instead of the cached whisper-cli.exe; the whisper zip is then not
	needed and not fetched.

.EXAMPLE
	.\scripts\dev\transcribe.ps1 -All
	.\scripts\dev\transcribe.ps1 -Mark last -Force
#>
[CmdletBinding(DefaultParameterSetName = 'One')]
param(
	[Parameter(ParameterSetName = 'One', Mandatory = $true)]
	[string]$Mark,
	[Parameter(ParameterSetName = 'All', Mandatory = $true)]
	[switch]$All,
	[switch]$Force,
	[string]$UserDir,
	[string]$ToolsDir,
	[string]$Glossary,
	[string]$PinsFile,
	[string]$WhisperCli
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

. (Join-Path $PSScriptRoot '..\build-paths.ps1')
. (Join-Path $PSScriptRoot 'log-digest-lib.ps1')
. (Join-Path $PSScriptRoot 'transcribe-lib.ps1')

$utf8 = [Text.UTF8Encoding]::new($false)

# What may be printed: scrubbed of user paths and the account and machine names
function Write-Clean([string]$Text) {
	Write-Output (Protect-ArxText $Text)
}

if (-not $UserDir) {
	$isolated = Get-DefaultUserDir (Join-Path $PSScriptRoot '..\..')
	$UserDir = if (Test-Path -LiteralPath (Join-Path $isolated 'marks')) { $isolated } else { Get-SharedUserDir }
}
if (-not $ToolsDir) { $ToolsDir = Get-TranscribeToolsDir }
if (-not $Glossary) { $Glossary = Join-Path $PSScriptRoot 'transcribe-glossary.txt' }

# The marks to transcribe
$marksDir = Join-Path $UserDir 'marks'
$marks = @()
if (Test-Path -LiteralPath $marksDir -PathType Container) {
	$marks = @(Get-ChildItem -LiteralPath $marksDir -Directory | Where-Object { $_.Name -match '^\d{8}T\d{6}Z-\d+$' } | Sort-Object Name)
}
if ($All) {
	$chosen = @($marks | Where-Object {
		(Test-Path -LiteralPath (Join-Path $_.FullName 'note.wav')) -and ($Force -or -not (Test-Path -LiteralPath (Join-Path $_.FullName 'transcript.txt')))
	})
} else {
	$one = if ($Mark -eq 'last') { $marks | Select-Object -Last 1 } else { $marks | Where-Object { $_.Name -eq $Mark } | Select-Object -First 1 }
	if (-not $one) {
		Write-Clean "No mark $Mark in the user directory's marks folder"
		exit 1
	}
	if (-not (Test-Path -LiteralPath (Join-Path $one.FullName 'note.wav'))) {
		Write-Clean "Nothing to transcribe: $($one.Name) has no note.wav"
		exit 0
	}
	if (-not $Force -and (Test-Path -LiteralPath (Join-Path $one.FullName 'transcript.txt'))) {
		Write-Clean "Already transcribed: $($one.Name) (-Force to do it again)"
		exit 0
	}
	$chosen = @($one)
}
if ($chosen.Count -eq 0) {
	Write-Clean 'Nothing to transcribe: no note without a transcript'
	exit 0
}

# The tools: cached and verified, fetched on first use
try {
	$pins = if ($PinsFile) { Read-TranscribePins ([IO.File]::ReadAllText($PinsFile, $utf8)) } else { Get-TranscribePins }
	$model = Get-TranscribeTool $pins 'model' (Join-Path $ToolsDir 'models')
	$vad = Get-TranscribeTool $pins 'vad' (Join-Path $ToolsDir 'models')
	$cli = if ($WhisperCli) { $WhisperCli } else { Get-WhisperCli $pins $ToolsDir }
} catch {
	Write-Clean $_.Exception.Message
	exit 1
}

$prompt = Get-GlossaryPrompt $Glossary
$threads = [math]::Max(1, [math]::Min([Environment]::ProcessorCount, 8))
$work = Join-Path ([IO.Path]::GetTempPath()) "arxvr-transcribe-$PID"
$failed = 0
try {
	New-Item -ItemType Directory -Force $work | Out-Null
	foreach ($folder in $chosen) {
		$note = Join-Path $folder.FullName 'note.wav'
		$base = Join-Path $work $folder.Name
		$common = @('-m', $model, '-f', $note, '-l', 'ru', '-t', "$threads", '--prompt', $prompt, '-oj', '-of', $base)
		$segments = $null
		foreach ($withVad in $true, $false) {
			$arguments = if ($withVad) { $common + @('--vad', '-vm', $vad) } else { $common }
			Remove-Item -LiteralPath "$base.json" -ErrorAction SilentlyContinue
			$output = if ($cli -like '*.ps1') {
				& (Get-Process -Id $PID).Path -NoProfile -File $cli @arguments 2>&1
			} else {
				& $cli @arguments 2>&1
			}
			if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath "$base.json")) {
				$tail = @($output | ForEach-Object { "$_" } | Select-Object -Last 5) -join ' | '
				throw "whisper failed on $($folder.Name) (exit $LASTEXITCODE): $tail"
			}
			$segments = @(Read-WhisperSegments ([IO.File]::ReadAllText("$base.json", $utf8)))
			# Quiet short notes come out empty with the VAD: once more without it
			if ($segments.Count -gt 0) { break }
		}
		[IO.File]::WriteAllText((Join-Path $folder.FullName 'transcript.txt'), (Format-Transcript $segments), $utf8)
		Write-Clean "Transcribed: $($folder.Name) ($($segments.Count) line(s))"
	}
} catch {
	Write-Clean $_.Exception.Message
	$failed = 1
} finally {
	Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
}
exit $failed
