<#
.SYNOPSIS
	Self-test of the paths transcribe.ps1 hands whisper-cli (#574): the ANSI code page and whisper's error line.

.DESCRIPTION
	whisper-cli 1.9.2 reads its command line in the system's ANSI code page: a path with a character outside it (an é
	under cp1251) reaches it changed and the note is "not found". transcribe.ps1 passes such a path as its 8.3 short
	name, or copies the note to a plain temporary path when its folder has none, and on a failure prints whisper's own
	error line instead of the usage text that follows it. A fake whisper, no network, no model; the code page is set
	with -AnsiCodePage so the result does not depend on the machine's.
	Exit code 0 when everything passed, 1 otherwise.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$failures = [Collections.Generic.List[string]]::new()
function Check([bool]$Condition, [string]$What) {
	if ($Condition) { Write-Host "  ok   $What" } else { Write-Host "  FAIL $What" -ForegroundColor Red; $failures.Add($What) }
}

. (Join-Path $PSScriptRoot 'transcribe-lib.ps1')
$script = Join-Path $PSScriptRoot 'transcribe.ps1'
$root = Join-Path ([IO.Path]::GetTempPath()) "arxvr-transcribe-paths-$PID"
$pwsh = (Get-Process -Id $PID).Path
$utf8 = [Text.UTF8Encoding]::new($false)

# Characters from code points, so the test does not depend on how this file is read
$eAcute = [string][char]0x00E9                                                    # é
$dima = -join (@(0x0414, 0x0438, 0x043C, 0x0430) | ForEach-Object { [char]$_ })   # Дима

Write-Host '==> the code page'
Check (Test-AnsiText "C:\Users\$dima\marks" 1251) 'Cyrillic is in cp1251'
Check (-not (Test-AnsiText "C:\Users\Jos$eAcute\marks" 1251)) 'an e-acute is not in cp1251 (no best fit to e)'
Check (Test-AnsiText "C:\Users\Jos$eAcute\marks" 1252) '... but in cp1252'
Check (-not (Test-AnsiText "C:\Users\$dima\marks" 1252)) 'Cyrillic is not in cp1252'
Check (Test-AnsiText "C:\Users\Jos$eAcute\$dima" 65001) 'a UTF-8 code page holds everything'
Check ((Get-AnsiSafePath 'C:\plain\note.wav' 1251) -ceq 'C:\plain\note.wav') 'a plain path stays as it is'

Write-Host '==> whisper''s error line, not its usage'
$sample = @(
	'load_backend: loaded CPU backend from C:\tools\whisper-1.9.2\Release\ggml-cpu-haswell.dll'
	"error: input file not found 'C:\Users\Jose\note.wav'"
	'error: no input files specified'
	''
	'usage: whisper-cli.exe [options] file0 file1 ...'
	'  -h,        --help              [default] show this help message and exit'
)
$message = Get-WhisperError $sample
Check ($message -match "error: input file not found 'C:\\Users\\Jose\\note\.wav'") "the error line ($message)"
Check ($message -notmatch 'usage:|--help') '... not the usage text'
Check ((Get-WhisperError @('a', 'b', 'c', 'last words')) -ceq 'b | c | last words') 'no error line: the last three lines'

# A fake whisper: logs its arguments and whether the -f file was there, writes <-of>.json - or fails like the real one
$fake = @'
$log = [Collections.Generic.List[string]]::new()
foreach ($a in $args) { $log.Add([string]$a) }
$of = $null; $f = $null
for ($i = 0; $i -lt $log.Count - 1; $i++) { if ($log[$i] -ceq '-of') { $of = $log[$i + 1] }; if ($log[$i] -ceq '-f') { $f = $log[$i + 1] } }
$entry = [ordered]@{ args = @($log); input = [bool]($f -and (Test-Path -LiteralPath $f)) }
[IO.File]::AppendAllText($env:FAKE_PATHS_LOG, (ConvertTo-Json -Compress -InputObject $entry) + "`n", [Text.UTF8Encoding]::new($false))
if ($env:FAKE_PATHS_FAIL) {
	Write-Output 'load_backend: loaded CPU backend'
	Write-Output "error: input file not found '$f'"
	Write-Output 'usage: whisper-cli.exe [options] file0 file1 ...'
	Write-Output '  -h,        --help              [default] show this help message and exit'
	exit 2
}
$json = '{"transcription":[{"offsets":{"from":1200,"to":2000},"text":" Ok, B."}]}'
[IO.File]::WriteAllText("$of.json", $json, [Text.UTF8Encoding]::new($false))
exit 0
'@

# A 1 s mono 16-bit WAV of silence
function Write-Wav([string]$Path) {
	$data = 32000
	$ms = [IO.MemoryStream]::new(); $w = [IO.BinaryWriter]::new($ms)
	$w.Write([Text.Encoding]::ASCII.GetBytes('RIFF')); $w.Write([uint32](36 + $data)); $w.Write([Text.Encoding]::ASCII.GetBytes('WAVE'))
	$w.Write([Text.Encoding]::ASCII.GetBytes('fmt ')); $w.Write([uint32]16); $w.Write([uint16]1); $w.Write([uint16]1)
	$w.Write([uint32]16000); $w.Write([uint32]32000); $w.Write([uint16]2); $w.Write([uint16]16)
	$w.Write([Text.Encoding]::ASCII.GetBytes('data')); $w.Write([uint32]$data); $w.Write([byte[]]::new($data)); $w.Flush()
	New-Item -ItemType Directory -Force (Split-Path -Parent $Path) | Out-Null
	[IO.File]::WriteAllBytes($Path, $ms.ToArray())
}

$previous = @{ log = $env:FAKE_PATHS_LOG; fail = $env:FAKE_PATHS_FAIL }
try {
	New-Item -ItemType Directory -Force $root | Out-Null
	$fakeCli = Join-Path $root 'fake\whisper-cli.ps1'
	New-Item -ItemType Directory -Force (Split-Path -Parent $fakeCli) | Out-Null
	[IO.File]::WriteAllText($fakeCli, $fake, $utf8)
	$env:FAKE_PATHS_LOG = Join-Path $root 'calls.log'

	# The tools: local files pinned by their hashes, in a plain folder
	$source = Join-Path $root 'source'
	New-Item -ItemType Directory -Force $source | Out-Null
	[IO.File]::WriteAllBytes((Join-Path $source 'ggml-large-v3-turbo-q8_0.bin'), [Text.Encoding]::ASCII.GetBytes('fake model #574 paths'))
	[IO.File]::WriteAllBytes((Join-Path $source 'ggml-silero-v6.2.0.bin'), [Text.Encoding]::ASCII.GetBytes('fake vad #574 paths'))
	$pins = Join-Path $root 'pins.txt'
	$pinLines = foreach ($name in 'ggml-large-v3-turbo-q8_0.bin', 'ggml-silero-v6.2.0.bin') {
		$file = Join-Path $source $name
		"$(if ($name -like '*silero*') { 'vad' } else { 'model' }) $((Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant()) $(([Uri]$file).AbsoluteUri)"
	}
	[IO.File]::WriteAllText($pins, ($pinLines -join "`n"), $utf8)
	$tools = Join-Path $root 'tools'

	# The owner's user directory under a name with an e-acute
	$user = Join-Path $root "Jos$eAcute\user"
	$mark = '20261021T101530Z-2'
	$markDir = Join-Path $user "marks\$mark"
	Write-Wav (Join-Path $markDir 'note.wav')
	$common = @('-Mark', $mark, '-Force', '-UserDir', $user, '-ToolsDir', $tools, '-PinsFile', $pins, '-WhisperCli', $fakeCli, '-AnsiCodePage', '1251')

	Write-Host '==> a mark folder outside the code page'
	Remove-Item -LiteralPath $env:FAKE_PATHS_LOG -ErrorAction SilentlyContinue
	$env:FAKE_PATHS_FAIL = ''
	$output = & $pwsh -NoProfile -File $script @common 2>&1
	$exit = $LASTEXITCODE
	Check ($exit -eq 0) "exit 0 ($exit): $(@($output) -join ' | ')"
	$calls = @(if (Test-Path -LiteralPath $env:FAKE_PATHS_LOG) { [IO.File]::ReadAllLines($env:FAKE_PATHS_LOG, $utf8) | Where-Object { $_ } | ForEach-Object { $_ | ConvertFrom-Json } })
	Check ($calls.Count -ge 1) "whisper ran ($($calls.Count))"
	foreach ($call in $calls) {
		$bad = @($call.args | Where-Object { -not (Test-AnsiText ([string]$_) 1251) })
		Check ($bad.Count -eq 0) "every argument survives cp1251 ($(@($bad) -join ', '))"
		Check $call.input 'the -f file whisper got is there (its 8.3 name, or a copy)'
	}
	$transcript = Join-Path $markDir 'transcript.txt'
	Check ((Test-Path -LiteralPath $transcript) -and [IO.File]::ReadAllText($transcript, $utf8) -ceq "[0:01.2] Ok, B.`n") 'transcript.txt beside note.wav, in the folder with the e-acute'

	Write-Host '==> whisper fails: its error, not its usage'
	Remove-Item -LiteralPath $transcript -ErrorAction SilentlyContinue
	$env:FAKE_PATHS_FAIL = '1'
	$output = @(& $pwsh -NoProfile -File $script @common 2>&1 | ForEach-Object { "$_" })
	$exit = $LASTEXITCODE
	$text = $output -join "`n"
	Check ($exit -eq 1) "exit 1 ($exit)"
	Check ($text -match 'whisper failed on 20261021T101530Z-2 \(exit 2\): error: input file not found') "whisper's error line ($text)"
	Check ($text -notmatch 'usage:|--help') '... not its usage text'
	Check (-not (Test-Path -LiteralPath $transcript)) 'no transcript.txt'
} finally {
	$env:FAKE_PATHS_LOG = $previous.log
	$env:FAKE_PATHS_FAIL = $previous.fail
	Remove-Item -Recurse -Force $root -ErrorAction SilentlyContinue
}

if ($failures.Count) {
	Write-Host "test-transcribe-paths: $($failures.Count) check(s) FAILED" -ForegroundColor Red
	exit 1
}
Write-Host 'test-transcribe-paths: all checks passed' -ForegroundColor Green
exit 0
