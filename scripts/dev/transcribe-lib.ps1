# The voice notes' local transcription (#574): the pinned tools, their cache, whisper's JSON to transcript.txt.
# Dot-sourced by scripts/dev/transcribe.ps1 and scripts/dev/marks.ps1; self-test: scripts/dev/test-transcribe.ps1.

# The tools, pinned: role, SHA-256, URL (a release of whisper.cpp; Hugging Face at a fixed revision, never main).
# All MIT. docs/vr/TESTING.md "Third-party tools downloaded by scripts" lists the same.
$script:TranscribePins = @'
whisper 49dcc16de826f20bd53d44f947a1ae49dfa81f86cad67a64d80820cb192d674a https://github.com/ggml-org/whisper.cpp/releases/download/v1.9.2/whisper-bin-x64.zip
model 317eb69c11673c9de1e1f0d459b253999804ec71ac4c23c17ecf5fbe24e259a1 https://huggingface.co/ggerganov/whisper.cpp/resolve/5359861c739e955e79d9a303bcbc70fb988958b1/ggml-large-v3-turbo-q8_0.bin
vad 2aa269b785eeb53a82983a20501ddf7c1d9c48e33ab63a41391ac6c9f7fb6987 https://huggingface.co/ggml-org/whisper-vad/resolve/9ffd54a1e1ee413ddf265af9913beaf518d1639b/ggml-silero-v6.2.0.bin
'@

# The cache: %LOCALAPPDATA%\ArxFatalisVR\tools (whisper-bin-x64.zip and its whisper-<version>\, models\)
function Get-TranscribeToolsDir {
	Join-Path $env:LOCALAPPDATA 'ArxFatalisVR\tools'
}

# A pins table's lines "<role> <sha256> <url>" ('#' comments and blank lines skipped) as role -> @{ Sha; Url; Name }
function Read-TranscribePins([string]$Text) {
	$pins = @{}
	foreach ($line in $Text -split "`r?`n") {
		$line = $line.Trim()
		if (-not $line -or $line.StartsWith('#')) { continue }
		$parts = $line -split '\s+'
		if ($parts.Count -ne 3 -or $parts[1] -notmatch '^[0-9a-fA-F]{64}$') { throw "A pin line is not '<role> <sha256> <url>': $line" }
		$url = $parts[2]
		$pins[$parts[0]] = @{ Sha = $parts[1].ToLowerInvariant(); Url = $url; Name = ([Uri]$url).Segments[-1] }
	}
	$pins
}

function Get-TranscribePins {
	Read-TranscribePins $script:TranscribePins
}

function Get-FileSha256([string]$Path) {
	(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

# The pinned file of $Role in $Dir: the cached one when it matches its hash; else fetched (a file:/// URL copied),
# checked, and only then given its name. A mismatch deletes the file and throws "SHA-256 mismatch".
function Get-TranscribeTool($Pins, [string]$Role, [string]$Dir) {
	$pin = $Pins[$Role]
	if (-not $pin) { throw "No pinned $Role in the tools table" }
	$path = Join-Path $Dir $pin.Name
	if (Test-Path -LiteralPath $path) {
		if ((Get-FileSha256 $path) -eq $pin.Sha) { return $path }
		# A cached file that changed (a broken write, a tampered file): fetched again
		Remove-Item -LiteralPath $path -Force
	}
	New-Item -ItemType Directory -Force $Dir | Out-Null
	$part = "$path.part"
	try {
		$uri = [Uri]$pin.Url
		if ($uri.IsFile) {
			Copy-Item -LiteralPath $uri.LocalPath -Destination $part -Force
		} else {
			Write-Host "Downloading $($pin.Name) (first use; cached in the tools directory afterwards)"
			Invoke-WebRequest -Uri $pin.Url -OutFile $part -UseBasicParsing
		}
		$sha = Get-FileSha256 $part
		if ($sha -ne $pin.Sha) {
			throw "SHA-256 mismatch: $($pin.Name) is $sha, the pin is $($pin.Sha) - deleted, nothing transcribed"
		}
		Move-Item -LiteralPath $part -Destination $path -Force
	} finally {
		Remove-Item -LiteralPath $part -Force -ErrorAction SilentlyContinue
	}
	$path
}

# The whisper.cpp release of the pins, unpacked into its own folder whisper-<version>: its whisper-cli.exe
function Get-WhisperCli($Pins, [string]$ToolsDir) {
	$zip = Get-TranscribeTool $Pins 'whisper' $ToolsDir
	$version = if ($Pins['whisper'].Url -match '/v(\d+(?:\.\d+)+)/') { $Matches[1] } else { 'current' }
	$folder = Join-Path $ToolsDir "whisper-$version"
	$cli = Get-ChildItem -LiteralPath $folder -Recurse -File -Filter 'whisper-cli.exe' -ErrorAction SilentlyContinue | Select-Object -First 1
	if (-not $cli) {
		# Unpacked into a new, empty folder first: a half-unpacked one never counts as cached
		$unpack = "$folder.part"
		Remove-Item -Recurse -Force -LiteralPath $unpack -ErrorAction SilentlyContinue
		Expand-Archive -LiteralPath $zip -DestinationPath $unpack -Force
		Remove-Item -Recurse -Force -LiteralPath $folder -ErrorAction SilentlyContinue
		Move-Item -LiteralPath $unpack -Destination $folder
		$cli = Get-ChildItem -LiteralPath $folder -Recurse -File -Filter 'whisper-cli.exe' | Select-Object -First 1
		if (-not $cli) { throw "No whisper-cli.exe in $($Pins['whisper'].Name)" }
	}
	$cli.FullName
}

# The tools are cached (marks.ps1 transcribes new notes by itself only then: it never downloads unasked)
function Test-TranscribeToolsCached([string]$ToolsDir) {
	$pins = Get-TranscribePins
	foreach ($role in 'model', 'vad') {
		if (-not (Test-Path -LiteralPath (Join-Path (Join-Path $ToolsDir 'models') $pins[$role].Name))) { return $false }
	}
	if (-not (Test-Path -LiteralPath (Join-Path $ToolsDir $pins['whisper'].Name))) { return $false }
	$found = Get-ChildItem -LiteralPath $ToolsDir -Directory -Filter 'whisper-*' -ErrorAction SilentlyContinue |
		Where-Object { Get-ChildItem -LiteralPath $_.FullName -Recurse -File -Filter 'whisper-cli.exe' -ErrorAction SilentlyContinue }
	[bool]$found
}

# The system's ANSI code page: whisper-cli 1.9.2 reads its command line in it, so a path with a character outside it
# (an é under cp1251) reaches whisper changed ("input file not found")
function Get-AnsiCodePage {
	try { [int](Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\Nls\CodePage').ACP } catch { 1252 }
}

# $Text goes through the code page unchanged (no best-fit: an é is not an e)
function Test-AnsiText([string]$Text, [int]$CodePage) {
	if ($CodePage -eq 65001) { return $true }
	[Text.Encoding]::RegisterProvider([Text.CodePagesEncodingProvider]::Instance)
	$encoding = [Text.Encoding]::GetEncoding($CodePage, [Text.EncoderFallback]::ExceptionFallback, [Text.DecoderFallback]::ExceptionFallback)
	try { $null = $encoding.GetBytes($Text); $true } catch { $false }
}

# A path whisper-cli can open: as it is when the code page holds it, else its 8.3 short name; "" when there is none
# (8.3 names off on that volume)
function Get-AnsiSafePath([string]$Path, [int]$CodePage) {
	if (Test-AnsiText $Path $CodePage) { return $Path }
	try {
		$fso = New-Object -ComObject Scripting.FileSystemObject
		$short = if (Test-Path -LiteralPath $Path -PathType Container) { $fso.GetFolder($Path).ShortPath } else { $fso.GetFile($Path).ShortPath }
		if ($short -and (Test-AnsiText $short $CodePage)) { return $short }
	} catch { }
	''
}

# What went wrong in a whisper run: its error lines, not the usage text it prints after them; else its last lines
function Get-WhisperError($Output) {
	$lines = @($Output | ForEach-Object { "$_".Trim() } | Where-Object { $_ })
	$errors = @($lines | Where-Object { $_ -match '^error\b|\bfailed to\b|^whisper_\w+: .*\b(error|fail)' })
	if ($errors.Count) { $errors -join ' | ' } else { @($lines | Select-Object -Last 3) -join ' | ' }
}

# The glossary file's words for whisper's prompt: the lines that are neither empty nor a '#' comment, trimmed, ", "
function Get-GlossaryPrompt([string]$Path) {
	$words = @([IO.File]::ReadAllLines($Path, [Text.UTF8Encoding]::new($false)) | ForEach-Object { $_.Trim() } |
		Where-Object { $_ -and -not $_.StartsWith('#') })
	$words -join ', '
}

# whisper-cli's -oj JSON: its segments with text, as @{ FromMs; Text } (the text trimmed)
function Read-WhisperSegments([string]$Json) {
	$data = $Json | ConvertFrom-Json
	foreach ($segment in @($data.transcription)) {
		if ($null -eq $segment) { continue }
		$text = ([string]$segment.text).Trim()
		if ($text) { [pscustomobject]@{ FromMs = [long]$segment.offsets.from; Text = $text } }
	}
}

# A press-relative time in ms as "<m>:<ss.s>", rounded down to 0.1 s
function Format-TranscriptTime([long]$Ms) {
	$tenths = [math]::Floor([math]::Max($Ms, 0) / 100)
	$minutes = [math]::Floor($tenths / 600)
	$rest = $tenths - $minutes * 600
	'{0}:{1:00}.{2}' -f [long]$minutes, [long][math]::Floor($rest / 10), [long]($rest % 10)
}

# transcript.txt: "[m:ss.s] text" a segment, "\n" after each
function Format-Transcript($Segments) {
	-join @($Segments | ForEach-Object { "[$(Format-TranscriptTime $_.FromMs)] $($_.Text)`n" })
}
