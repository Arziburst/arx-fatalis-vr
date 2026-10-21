<#
.SYNOPSIS
	Self-test of scripts/dev/transcribe.ps1 (#574): a fake whisper, local fake tools, no network, no model.

.DESCRIPTION
	transcribe.ps1 turns the voice note of a mark (note.wav, tests/vr/VoiceNoteTest.h) into transcript.txt beside it,
	locally: whisper.cpp v1.9.2 (whisper-cli.exe from whisper-bin-x64.zip), the model ggml-large-v3-turbo-q8_0.bin and
	the VAD model ggml-silero-v6.2.0.bin, downloaded on first use from their pinned URLs (Hugging Face resolve/<revision>,
	never main), each checked against its pinned SHA-256, cached in the tools directory and never fetched again.
	Written before the implementation; the interface it expects:

	transcribe.ps1 (-Mark <folder name> | last) | -All  [-Force] [-UserDir <dir>] [-ToolsDir <dir>] [-Glossary <file>]
	               [-PinsFile <file>] [-WhisperCli <program or .ps1>]
	  -Mark        one mark of <user dir>\marks (its folder name, or last: the newest); -All: every mark with a
	               note.wav and no transcript.txt
	  -Force       transcribe again when transcript.txt is already there (without it such a mark is skipped, exit 0,
	               whisper not run)
	  -UserDir     as marks.ps1: this working copy's isolated user directory when it has marks, else the player's
	  -ToolsDir    the cache, default %LOCALAPPDATA%\ArxFatalisVR\tools (its layout is the script's own)
	  -Glossary    the prompt's vocabulary, default scripts/dev/transcribe-glossary.txt (UTF-8, editable, in the
	               repository): its lines that are neither empty nor start with '#', trimmed, joined by ", ". The
	               default file holds at least the Latin words B, trigger, grip, trackpad, radial, SteamVR, Index
	  -PinsFile    (tests) replaces the pinned table: lines "<role> <sha256> <url>" ('#' comments, blank lines
	               skipped), role whisper | model | vad; the file's name is the URL's last segment. A file:/// URL is
	               copied from the local file (the tests' source; the real table has only https URLs)
	  -WhisperCli  (tests) run this instead of the cached whisper-cli.exe, with the same arguments; a .ps1 is run by
	               pwsh. With it the whisper zip is not needed and not fetched
	Fetching: into the tools directory as a temporary file, SHA-256 checked; a mismatch deletes it (no file of that name
	stays anywhere under the tools directory, no .part either), prints a line with "SHA-256 mismatch" and the file's
	name, transcribes nothing and exits 1. A cached, verified file is used as it is: no fetch, no network.
	whisper's arguments: -m <model> -f <note.wav> -l ru -t <threads> --vad -vm <vad model> --prompt <glossary> -oj
	  -of <base>; it writes <base>.json. When that VAD run gives no segment with text, it runs once more without --vad
	  and -vm (quiet short notes come out empty with the VAD; 4 of 7 in the coordinator's run, 2026-10-21), and the
	  transcript is that run's.
	transcript.txt: UTF-8 without a BOM, one line per segment with text, "\n" after each: "[<m>:<ss.s>] <text>" - the
	  segment's start (offsets.from, ms) rounded DOWN to 0.1 s, minutes unpadded, seconds two digits; the text trimmed;
	  segments with no text left out.
	Exit code 0 when everything passed, 1 otherwise.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$failures = [Collections.Generic.List[string]]::new()
function Check([bool]$Condition, [string]$What) {
	if ($Condition) { Write-Host "  ok   $What" } else { Write-Host "  FAIL $What" -ForegroundColor Red; $failures.Add($What) }
}

$script = Join-Path $PSScriptRoot 'transcribe.ps1'
$defaultGlossary = Join-Path $PSScriptRoot 'transcribe-glossary.txt'
$root = Join-Path ([IO.Path]::GetTempPath()) "arxvr-transcribe-test-$PID"
$pwsh = (Get-Process -Id $PID).Path
$utf8 = [Text.UTF8Encoding]::new($false)

if (-not (Test-Path -LiteralPath $script)) {
	Write-Host "  FAIL scripts/dev/transcribe.ps1 does not exist" -ForegroundColor Red
	Write-Host 'test-transcribe: FAILED' -ForegroundColor Red
	exit 1
}

function Write-Utf8([string]$Path, [string]$Text) {
	New-Item -ItemType Directory -Force (Split-Path -Parent $Path) | Out-Null
	[IO.File]::WriteAllText($Path, $Text, $utf8)
}

# A mono 16-bit PCM WAV of $Seconds of silence at 16 kHz (the note's format)
function Write-Wav([string]$Path, [double]$Seconds) {
	$dataBytes = [int]([math]::Round($Seconds * 16000)) * 2
	$ms = [IO.MemoryStream]::new()
	$w = [IO.BinaryWriter]::new($ms)
	$w.Write([Text.Encoding]::ASCII.GetBytes('RIFF')); $w.Write([uint32](36 + $dataBytes))
	$w.Write([Text.Encoding]::ASCII.GetBytes('WAVE'))
	$w.Write([Text.Encoding]::ASCII.GetBytes('fmt ')); $w.Write([uint32]16)
	$w.Write([uint16]1); $w.Write([uint16]1); $w.Write([uint32]16000); $w.Write([uint32]32000)
	$w.Write([uint16]2); $w.Write([uint16]16)
	$w.Write([Text.Encoding]::ASCII.GetBytes('data')); $w.Write([uint32]$dataBytes)
	$w.Write([byte[]]::new($dataBytes))
	$w.Flush()
	New-Item -ItemType Directory -Force (Split-Path -Parent $Path) | Out-Null
	[IO.File]::WriteAllBytes($Path, $ms.ToArray())
}

function Get-Sha([string]$Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }

function Invoke-Transcribe([string[]]$Arguments) {
	$output = & $pwsh -NoProfile -File $script @Arguments 2>&1
	[pscustomobject]@{ Exit = $LASTEXITCODE; Text = (@($output | ForEach-Object { "$_" }) -join "`n") }
}

# The fake whisper's calls: one JSON array of arguments a line
function Get-Calls {
	if (-not (Test-Path -LiteralPath $env:FAKE_WHISPER_LOG)) { return @() }
	@([IO.File]::ReadAllLines($env:FAKE_WHISPER_LOG, $utf8) | Where-Object { $_ } | ForEach-Object { , @($_ | ConvertFrom-Json) })
}

# The value after an option in a call's arguments, $null without it
function Get-Option($Call, [string]$Name) {
	for ($i = 0; $i -lt $Call.Count - 1; $i++) { if ($Call[$i] -ceq $Name) { return $Call[$i + 1] } }
	$null
}

# Every file under the tools directory with this name (or its name with a suffix: a .part)
function Find-Tool([string]$Tools, [string]$Name) {
	if (-not (Test-Path -LiteralPath $Tools)) { return @() }
	@(Get-ChildItem -LiteralPath $Tools -Recurse -File | Where-Object { $_.Name -like "$Name*" })
}

# Cyrillic from code points, so the expectation does not depend on how this file is read
function U([int[]]$Codes) { -join ($Codes | ForEach-Object { [char]$_ }) }
$smotri = U 0x0421, 0x043C, 0x043E, 0x0442, 0x0440, 0x0438                             # Смотри
$ya = U 0x044F                                                                          # я
$nazhal = U 0x043D, 0x0430, 0x0436, 0x0430, 0x043B                                      # нажал
$iRu = U 0x0438                                                                           # и
$radial = U 0x0420, 0x0430, 0x0434, 0x0438, 0x0430, 0x043B, 0x044C, 0x043D, 0x043E, 0x0435 # Радиальное
$menu = U 0x043C, 0x0435, 0x043D, 0x044E                                                # меню
$ne = U 0x043D, 0x0435                                                                  # не
$opened = U 0x043E, 0x0442, 0x043A, 0x0440, 0x044B, 0x043B, 0x043E, 0x0441, 0x044C      # открылось
$na = U 0x043D, 0x0430                                                                  # на
$showed = U 0x043F, 0x043E, 0x043A, 0x0430, 0x0437, 0x0430, 0x043B                      # показал
$trackpadRu = U 0x0442, 0x0440, 0x0435, 0x043A, 0x043F, 0x0430, 0x0434                  # трекпад
$rune = U 0x0440, 0x0443, 0x043D, 0x0430                                                # руна

# whisper.cpp's -oj output (its shape in v1.9.2): Cyrillic and Latin mixed, a blank segment, a leading space on each
function New-WhisperJson([object[]]$Segments) {
	$transcription = @($Segments | ForEach-Object {
		[ordered]@{ timestamps = [ordered]@{ from = '00:00:00,000'; to = '00:00:00,000' }; offsets = [ordered]@{ from = $_[0]; to = $_[0] + 1000 }; text = $_[1] }
	})
	[ordered]@{
		systeminfo = 'AVX = 1 | AVX2 = 1'
		model = [ordered]@{ type = 'large v3'; multilingual = $true }
		params = [ordered]@{ model = 'ggml-large-v3-turbo-q8_0.bin'; language = 'ru'; translate = $false }
		result = [ordered]@{ language = 'ru' }
		transcription = $transcription
	} | ConvertTo-Json -Depth 6
}
$speech = New-WhisperJson @(
	, @(0, " $smotri, $ya $nazhal trigger $iRu grip.")
	, @(2580, '   ')
	, @(7290, " $radial $menu $ne $opened $na B.")
	, @(65400, " SteamVR $showed Index, $trackpadRu ok. ")
)
$expectedTranscript = "[0:00.0] $smotri, $ya $nazhal trigger $iRu grip.`n" +
	"[0:07.2] $radial $menu $ne $opened $na B.`n" +
	"[1:05.4] SteamVR $showed Index, $trackpadRu ok.`n"
$quiet = New-WhisperJson @(, @(400, ' Ok, B.'))
$empty = New-WhisperJson @()
$blank = New-WhisperJson @(, @(0, '  '))

# The fake whisper: logs its arguments, writes <-of>.json - the VAD run's answer or the plain one
$fake = @'
$log = [Collections.Generic.List[string]]::new()
foreach ($a in $args) { $log.Add([string]$a) }
$encoding = [Text.UTF8Encoding]::new($false)
[IO.File]::AppendAllText($env:FAKE_WHISPER_LOG, (ConvertTo-Json -Compress -InputObject @($log)) + "`n", $encoding)
$of = $null
for ($i = 0; $i -lt $log.Count - 1; $i++) { if ($log[$i] -ceq '-of') { $of = $log[$i + 1] } }
if (-not $of) { exit 3 }
$answer = if ($log -ccontains '--vad') { $env:FAKE_WHISPER_VAD_JSON } else { $env:FAKE_WHISPER_JSON }
[IO.File]::WriteAllBytes("$of.json", [IO.File]::ReadAllBytes($answer))
exit 0
'@

$previous = @{ log = $env:FAKE_WHISPER_LOG; vad = $env:FAKE_WHISPER_VAD_JSON; plain = $env:FAKE_WHISPER_JSON }
try {
	New-Item -ItemType Directory -Force $root | Out-Null
	$fakeCli = Join-Path $root 'fake\whisper-cli.ps1'
	Write-Utf8 $fakeCli $fake
	$answers = Join-Path $root 'answers'
	Write-Utf8 (Join-Path $answers 'speech.json') $speech
	Write-Utf8 (Join-Path $answers 'quiet.json') $quiet
	Write-Utf8 (Join-Path $answers 'empty.json') $empty
	Write-Utf8 (Join-Path $answers 'blank.json') $blank
	$env:FAKE_WHISPER_LOG = Join-Path $root 'calls.log'

	# The "upstream" files: a local source, pinned by their hashes
	$source = Join-Path $root 'source'
	$modelName = 'ggml-large-v3-turbo-q8_0.bin'
	$vadName = 'ggml-silero-v6.2.0.bin'
	$modelBytes = [Text.Encoding]::ASCII.GetBytes('fake model for test-transcribe #574')
	$vadBytes = [Text.Encoding]::ASCII.GetBytes('fake silero vad for test-transcribe #574')
	New-Item -ItemType Directory -Force $source | Out-Null
	[IO.File]::WriteAllBytes((Join-Path $source $modelName), $modelBytes)
	[IO.File]::WriteAllBytes((Join-Path $source $vadName), $vadBytes)
	$modelSha = Get-Sha (Join-Path $source $modelName)
	$vadSha = Get-Sha (Join-Path $source $vadName)
	$pins = Join-Path $root 'pins.txt'
	Write-Utf8 $pins (@(
		'# the tools of test-transcribe.ps1: local files'
		''
		"model $modelSha $(([Uri](Join-Path $source $modelName)).AbsoluteUri)"
		"vad $vadSha $(([Uri](Join-Path $source $vadName)).AbsoluteUri)"
	) -join "`n")
	$tools = Join-Path $root 'tools'

	# A user directory: a hold with a note, a press without one
	$user = Join-Path $root 'user'
	$hold = '20261021T101530Z-2'
	$press = '20261021T101500Z-1'
	$holdDir = Join-Path $user "marks\$hold"
	Write-Wav (Join-Path $holdDir 'note.wav') 6.8
	Write-Utf8 (Join-Path $holdDir 'meta.txt') "# moment mark (#551)`nmark=2`nkind=hold`npressMs=20000`nreleaseMs=27000`n"
	Write-Utf8 (Join-Path $user "marks\$press\meta.txt") "# moment mark (#551)`nmark=1`nkind=press`npressMs=12000`nreleaseMs=12200`n"
	$transcript = Join-Path $holdDir 'transcript.txt'

	# A glossary of the test's own: Latin and Cyrillic, a comment, a blank line, spaces around a word
	$glossary = Join-Path $root 'glossary.txt'
	Write-Utf8 $glossary "# the test's glossary`nZzyzx-574`n`n  trigger  `n$rune`n"
	$common = @('-UserDir', $user, '-ToolsDir', $tools, '-PinsFile', $pins, '-WhisperCli', $fakeCli)

	Write-Host '==> first run: fetched, verified, cached; the JSON turned into transcript.txt byte for byte'
	$env:FAKE_WHISPER_VAD_JSON = Join-Path $answers 'speech.json'
	$env:FAKE_WHISPER_JSON = Join-Path $answers 'quiet.json'
	Remove-Item -LiteralPath $env:FAKE_WHISPER_LOG -ErrorAction SilentlyContinue
	$run = Invoke-Transcribe (@('-Mark', $hold, '-Glossary', $glossary) + $common)
	Check ($run.Exit -eq 0) "exit 0 ($($run.Exit)): $($run.Text)"
	Check (Test-Path -LiteralPath $transcript) 'transcript.txt beside note.wav'
	if (Test-Path -LiteralPath $transcript) {
		$bytes = [IO.File]::ReadAllBytes($transcript)
		$want = $utf8.GetBytes($expectedTranscript)
		Check (-not ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF)) 'UTF-8 without a BOM'
		Check ([Convert]::ToBase64String($bytes) -eq [Convert]::ToBase64String($want)) "byte-exact: '[m:ss.s] text' a line, rounded down, blank segment left out, LF ends (got: $($utf8.GetString($bytes) -replace "`n", '\n'))"
	}
	$calls = @(Get-Calls)
	Check ($calls.Count -eq 1) "whisper ran once: the VAD run had text ($($calls.Count))"
	if ($calls.Count -ge 1) {
		$c = $calls[0]
		Check ((Get-Option $c '-l') -ceq 'ru') '-l ru'
		Check ($c -ccontains '--vad') '--vad'
		Check ($c -ccontains '-oj') '-oj'
		Check ((Get-Option $c '-f') -and ([IO.Path]::GetFullPath((Get-Option $c '-f')) -eq [IO.Path]::GetFullPath((Join-Path $holdDir 'note.wav')))) '-f the mark''s note.wav'
		$m = Get-Option $c '-m'
		Check ($m -and (Test-Path -LiteralPath $m) -and (Get-Sha $m) -eq $modelSha) '-m the cached, verified model'
		Check ($m -and [IO.Path]::GetFullPath($m).StartsWith([IO.Path]::GetFullPath($tools), [StringComparison]::OrdinalIgnoreCase)) '... in the tools directory'
		$vm = Get-Option $c '-vm'
		Check ($vm -and (Test-Path -LiteralPath $vm) -and (Get-Sha $vm) -eq $vadSha) '-vm the cached, verified VAD model'
		Check ((Get-Option $c '--prompt') -ceq "Zzyzx-574, trigger, $rune") "--prompt the glossary file's words joined by ', ' (got '$(Get-Option $c '--prompt')')"
	}
	Check ((Find-Tool $tools $modelName).Count -eq 1) 'the model cached once'
	Check ((Find-Tool $tools $vadName).Count -eq 1) 'the VAD model cached once'
	Check (@(Find-Tool $tools '*.part').Count -eq 0) 'no .part left'

	Write-Host '==> cached: the source gone, nothing is fetched again'
	Remove-Item -LiteralPath (Join-Path $source $modelName), (Join-Path $source $vadName)
	$cachedModel = (Find-Tool $tools $modelName | Select-Object -First 1)
	$cachedStamp = if ($cachedModel) { $cachedModel.LastWriteTimeUtc } else { $null }
	Remove-Item -LiteralPath $env:FAKE_WHISPER_LOG -ErrorAction SilentlyContinue
	$run = Invoke-Transcribe (@('-Mark', $hold, '-Glossary', $glossary) + $common)
	Check ($run.Exit -eq 0) "already transcribed, no -Force: exit 0 ($($run.Exit))"
	Check (@(Get-Calls).Count -eq 0) '... skipped: whisper not run'
	$run = Invoke-Transcribe (@('-Mark', 'last', '-Force', '-Glossary', $glossary) + $common)
	Check ($run.Exit -eq 0) "-Force with the tools cached and the source gone: exit 0 ($($run.Exit)): $($run.Text)"
	Check (@(Get-Calls).Count -eq 1) '... whisper ran (-Mark last is the newest mark, the hold)'
	$again = (Find-Tool $tools $modelName | Select-Object -First 1)
	Check ($again -and (Get-Sha $again.FullName) -eq $modelSha) 'the cached model is the verified one'
	Check ($again -and $cachedStamp -and $again.LastWriteTimeUtc -eq $cachedStamp) '... not written again'

	Write-Host '==> the VAD finds nothing: run again without it'
	$env:FAKE_WHISPER_VAD_JSON = Join-Path $answers 'empty.json'
	$env:FAKE_WHISPER_JSON = Join-Path $answers 'speech.json'
	Remove-Item -LiteralPath $env:FAKE_WHISPER_LOG -ErrorAction SilentlyContinue
	$run = Invoke-Transcribe (@('-Mark', $hold, '-Force', '-Glossary', $glossary) + $common)
	Check ($run.Exit -eq 0) "exit 0 ($($run.Exit)): $($run.Text)"
	$calls = @(Get-Calls)
	Check ($calls.Count -eq 2) "whisper ran twice ($($calls.Count))"
	if ($calls.Count -eq 2) {
		Check ($calls[0] -ccontains '--vad') 'first with --vad'
		Check (-not ($calls[1] -ccontains '--vad') -and -not ($calls[1] -ccontains '-vm')) 'then without --vad and -vm'
		Check ((Get-Option $calls[1] '-l') -ceq 'ru' -and (Get-Option $calls[1] '--prompt') -ceq "Zzyzx-574, trigger, $rune") '... the same language and prompt'
	}
	Check ((Test-Path -LiteralPath $transcript) -and [IO.File]::ReadAllText($transcript, $utf8) -ceq $expectedTranscript) 'the transcript is the run without the VAD'

	# Segments with only blanks count as empty too
	$env:FAKE_WHISPER_VAD_JSON = Join-Path $answers 'blank.json'
	$env:FAKE_WHISPER_JSON = Join-Path $answers 'quiet.json'
	Remove-Item -LiteralPath $env:FAKE_WHISPER_LOG -ErrorAction SilentlyContinue
	$run = Invoke-Transcribe (@('-Mark', $hold, '-Force', '-Glossary', $glossary) + $common)
	Check ($run.Exit -eq 0) "exit 0 ($($run.Exit))"
	Check (@(Get-Calls).Count -eq 2) 'a VAD result of blank segments is empty: run again'
	Check ((Test-Path -LiteralPath $transcript) -and [IO.File]::ReadAllText($transcript, $utf8) -ceq "[0:00.4] Ok, B.`n") 'the transcript is the run without the VAD'

	Write-Host '==> the glossary from the repository'
	Check (Test-Path -LiteralPath $defaultGlossary) 'scripts/dev/transcribe-glossary.txt exists'
	$env:FAKE_WHISPER_VAD_JSON = Join-Path $answers 'speech.json'
	Remove-Item -LiteralPath $env:FAKE_WHISPER_LOG -ErrorAction SilentlyContinue
	$run = Invoke-Transcribe (@('-Mark', $hold, '-Force') + $common)
	Check ($run.Exit -eq 0) "without -Glossary: exit 0 ($($run.Exit))"
	$calls = @(Get-Calls)
	$prompt = if ($calls.Count -ge 1) { [string](Get-Option $calls[0] '--prompt') } else { '' }
	foreach ($word in 'B', 'trigger', 'grip', 'trackpad', 'radial', 'SteamVR', 'Index') {
		Check ($prompt -cmatch "(?<![\w-])$([regex]::Escape($word))(?![\w-])") "the default prompt has the Latin word '$word'"
	}
	if (Test-Path -LiteralPath $defaultGlossary) {
		$words = @([IO.File]::ReadAllLines($defaultGlossary, $utf8) | ForEach-Object { $_.Trim() } | Where-Object { $_ -and -not $_.StartsWith('#') })
		Check ($prompt -ceq ($words -join ', ')) 'the default prompt is the file''s words joined by '', '''
	}

	Write-Host '==> a file that does not match its pin: refused and deleted'
	$badTools = Join-Path $root 'bad-tools'
	$badSource = Join-Path $root 'bad-source'
	New-Item -ItemType Directory -Force $badSource | Out-Null
	[IO.File]::WriteAllBytes((Join-Path $badSource $modelName), [Text.Encoding]::ASCII.GetBytes('a tampered model'))
	[IO.File]::WriteAllBytes((Join-Path $badSource $vadName), $vadBytes)
	$badPins = Join-Path $root 'bad-pins.txt'
	Write-Utf8 $badPins (@(
		"model $modelSha $(([Uri](Join-Path $badSource $modelName)).AbsoluteUri)"
		"vad $vadSha $(([Uri](Join-Path $badSource $vadName)).AbsoluteUri)"
	) -join "`n")
	$badUser = Join-Path $root 'bad-user'
	Write-Wav (Join-Path $badUser "marks\$hold\note.wav") 2.0
	Remove-Item -LiteralPath $env:FAKE_WHISPER_LOG -ErrorAction SilentlyContinue
	$run = Invoke-Transcribe @('-Mark', $hold, '-UserDir', $badUser, '-ToolsDir', $badTools, '-PinsFile', $badPins, '-WhisperCli', $fakeCli)
	Check ($run.Exit -eq 1) "exit 1 ($($run.Exit))"
	Check ($run.Text -match 'SHA-256 mismatch' -and $run.Text.Contains($modelName)) "a clear message: 'SHA-256 mismatch' and the file's name ($($run.Text))"
	Check ((Find-Tool $badTools $modelName).Count -eq 0) 'the wrong file deleted (no copy and no .part under the tools directory)'
	Check (@(Get-Calls).Count -eq 0) 'whisper not run'
	Check (-not (Test-Path -LiteralPath (Join-Path $badUser "marks\$hold\transcript.txt"))) 'no transcript.txt'
	# ... and again: still refused, nothing kept from the first try
	$run = Invoke-Transcribe @('-Mark', $hold, '-UserDir', $badUser, '-ToolsDir', $badTools, '-PinsFile', $badPins, '-WhisperCli', $fakeCli)
	Check ($run.Exit -eq 1 -and (Find-Tool $badTools $modelName).Count -eq 0) 'a second try refuses it again'

	Write-Host '==> -All: every note without a transcript'
	$allUser = Join-Path $root 'all-user'
	$a = '20261021T110000Z-1'; $b = '20261021T110100Z-2'; $c = '20261021T110200Z-3'
	Write-Wav (Join-Path $allUser "marks\$a\note.wav") 3.0
	Write-Wav (Join-Path $allUser "marks\$b\note.wav") 3.0
	Write-Utf8 (Join-Path $allUser "marks\$b\transcript.txt") "[0:00.0] done before`n"
	Write-Utf8 (Join-Path $allUser "marks\$c\meta.txt") "# moment mark (#551)`nkind=press`n"
	Remove-Item -LiteralPath $env:FAKE_WHISPER_LOG -ErrorAction SilentlyContinue
	$run = Invoke-Transcribe @('-All', '-UserDir', $allUser, '-ToolsDir', $tools, '-PinsFile', $pins, '-WhisperCli', $fakeCli)
	Check ($run.Exit -eq 0) "exit 0 ($($run.Exit))"
	Check (@(Get-Calls).Count -eq 1) 'one note transcribed (one already was, one mark has no note)'
	Check ((Test-Path -LiteralPath (Join-Path $allUser "marks\$a\transcript.txt"))) 'the new note got its transcript'
	Check ([IO.File]::ReadAllText((Join-Path $allUser "marks\$b\transcript.txt"), $utf8) -ceq "[0:00.0] done before`n") 'the old transcript is left as it was'
	Check (-not (Test-Path -LiteralPath (Join-Path $allUser "marks\$c\transcript.txt"))) 'no transcript without a note'
} finally {
	$env:FAKE_WHISPER_LOG = $previous.log
	$env:FAKE_WHISPER_VAD_JSON = $previous.vad
	$env:FAKE_WHISPER_JSON = $previous.plain
	Remove-Item -Recurse -Force $root -ErrorAction SilentlyContinue
}

if ($failures.Count) {
	Write-Host "test-transcribe: $($failures.Count) check(s) FAILED" -ForegroundColor Red
	exit 1
}
Write-Host 'test-transcribe: all checks passed' -ForegroundColor Green
exit 0
