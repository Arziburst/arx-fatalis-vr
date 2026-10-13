<#
.SYNOPSIS
	The run digest: one game run's arx.log and crash reports in a screenful, and automatic bug filing (#131).

.DESCRIPTION
	Reads the last run in arx.log (the banner of #115 gives the build, branch and command line), groups the
	warnings and errors by signature (level + source file + message with numbers, hex and paths normalised),
	reads the new crash reports, and writes %LOCALAPPDATA%\ArxFatalisVR\runs\<run>\ with digest.md, digest.json,
	a copy of the log and of the crash reports (the last -Keep runs are kept). Never `cat` the log - read the digest.

	Runs automatically after every game exit started by our tools (run-windows.ps1, so the desktop shortcut and
	test-pr.ps1 too, and smoke-test.ps1). It never blocks the owner: it runs after the game closed, the GitHub
	part is sent by a detached background process, and without a network the reports wait in
	%LOCALAPPDATA%\ArxFatalisVR\pending-reports\ for the next run.

	Bug filing (GitHub REST only, never GraphQL; the repository is public, so every report is scrubbed of personal
	data first and one that cannot be made clean stays queued, #387): a new error or critical
	signature or a crash becomes an issue labeled `bug` + `auto-reported`; a new warning only once it shows up in a
	second run; a known signature gets a "seen again" comment at most once a day. signatures.json remembers the
	signatures, existing auto-reported issues are searched by the marker in their body, so a signature is never
	filed twice. Lines that are known to be harmless go to scripts/dev/log-digest-allowlist.txt.

.PARAMETER UserDir
	The user directory holding arx.log and crashes\. Default: this working copy's isolated one when it has an
	arx.log, otherwise the player's Saved Games\Arx Libertatis.

.PARAMETER Log
	A log file to digest (e.g. an older arx.3.log); its folder is the user directory.

.PARAMETER Since
	Only a log written at or after this time counts (the launcher passes its start time); crash reports newer than the
	run's start are the run's.

.PARAMETER Source
	Who started the run (run-windows.ps1, smoke-test.ps1), for the digest and the issue.

.PARAMETER ExitCode
	The game's exit code, if known.

.PARAMETER DurationSec
	The run's duration, if known (otherwise: from the banner's start to the last write of the log).

.PARAMETER NoReport
	Digest only: queue nothing and file nothing (also: $env:ARXVR_AUTOREPORT = 0).

.PARAMETER DryRun
	Digest without touching signatures.json, the queue or the run folder: print what would happen.

.PARAMETER Detach
	After the digest start the GitHub part as a hidden background process and return at once.

.PARAMETER FlushNow
	After the digest send the queue to GitHub in this process.

.PARAMETER Flush
	Only send the queue (retries what an earlier run could not send). No digest.

.PARAMETER Preview
	Only print the queued reports as they would be uploaded (scrubbed) and what the final check refuses. Sends
	nothing, changes nothing. No digest.

.PARAMETER Quiet
	No console summary.

.PARAMETER Print
	Print digest.md.

.PARAMETER Id
	Print the run's identity (the banner lines) and exit.

.PARAMETER Find
	Print the last -Last lines of the run that match this regex and exit.

.PARAMETER Condense
	Print the log in a screenful (rare lines in full, common ones as count x last sample) and exit.

.EXAMPLE
	.\scripts\dev\log-digest.ps1 -Print          # the last run of this working copy, digest on screen
.EXAMPLE
	.\scripts\dev\log-digest.ps1 -Find 'OpenXR' -Last 5
.EXAMPLE
	.\scripts\dev\log-digest.ps1 -DryRun -Log "$env:USERPROFILE\Saved Games\Arx Libertatis\arx.2.log"
#>
[CmdletBinding(PositionalBinding = $false)]
param(
	[string]$UserDir,
	[string]$Log,
	[Nullable[DateTime]]$Since,
	[string]$Source = 'log-digest.ps1',
	[Nullable[int]]$ExitCode,
	[double]$DurationSec = -1,
	[string]$Copy = '',
	[int]$Keep = 30,
	[switch]$NoReport,
	[switch]$DryRun,
	[switch]$Detach,
	[switch]$FlushNow,
	[switch]$Flush,
	[switch]$Preview,
	[switch]$Quiet,
	[switch]$Print,
	[switch]$Id,
	[string]$Find,
	[int]$Last = 10,
	[switch]$Condense,
	[int]$CrashWaitSec = 20
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8
. (Join-Path $PSScriptRoot 'log-digest-lib.ps1')
$SourceDir = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

function Write-FlushResult($r) {
	if ($r.Created.Count) { Write-Host "    filed: $(($r.Created | ForEach-Object { "#$_" }) -join ', ')" -ForegroundColor Yellow }
	if ($r.Commented.Count) { Write-Host "    seen-again comment: $(($r.Commented | ForEach-Object { "#$_" }) -join ', ')" }
	if ($r.Reopened.Count) { Write-Host "    reopened (the problem came back): $(($r.Reopened | ForEach-Object { "#$_" }) -join ', ')" -ForegroundColor Yellow }
	if ($r.Dropped.Count) { Write-Host "    seen again, comment throttled (once a day): $(($r.Dropped | ForEach-Object { "#$_" }) -join ', ')" }
	foreach ($f in $r.Failed) { Write-Warning "report not sent, it stays queued: $f" }
	foreach ($f in $r.Refused) { Write-Warning "report not sent, personal data the scrubber cannot remove - it stays queued: $f" }
}

if ($Flush) {
	Write-FlushResult (Invoke-ArxReportFlush)
	exit 0
}

if ($Preview) {
	$files = @(Get-ChildItem (Get-ArxPendingDir) -Filter '*.json' -ErrorAction SilentlyContinue | Sort-Object LastWriteTime)
	Write-Host "$($files.Count) queued report(s) in $(Get-ArxPendingDir), as they would be uploaded:"
	foreach ($f in $files) {
		$upload = Get-ArxUploadItem (Get-Content $f.FullName -Raw | ConvertFrom-Json -AsHashtable)
		$i = $upload.Item
		Write-Host "==> $($f.Name): $($i.action), $(if ($upload.Leaks) { "REFUSED: $($upload.Leaks -join ', ')" } else { 'clean' })" -ForegroundColor $(if ($upload.Leaks) { 'Yellow' } else { 'Cyan' })
		Write-Host "--- title`n$($i.title)"
		if ($i.action -eq 'file') { Write-Host "--- body`n$($i.body)" }
		Write-Host "--- comment (when the issue exists)`n$($i.comment)"
	}
	exit 0
}

if (-not $Log -and -not $UserDir) {
	$isolated = Get-DefaultUserDir $SourceDir
	$UserDir = if (Test-Path (Join-Path $isolated 'arx.log')) { $isolated } else { Get-SharedUserDir }
}
$logPath = if ($Log) { $Log } else { Join-Path $UserDir 'arx.log' }
if (-not $UserDir) { $UserDir = Split-Path -Parent $logPath }

# Reading modes: no files written, nothing filed
if ($Id -or $Find -or $Condense) {
	if (-not (Test-Path $logPath)) { throw "no log at $logPath" }
	$run = Read-ArxLogRun $logPath
	if ($Id) { $run.Banner.Raw | ForEach-Object { Write-Host $_ }; Write-Host "$($run.Entries.Count) lines in this run" }
	if ($Find) {
		$re = [regex]::new($Find, 'IgnoreCase')
		$hits = @($run.Entries | Where-Object { $re.IsMatch($_.Raw) })
		Write-Host "$($hits.Count) match(es) for /$Find/; the last $([Math]::Min($Last, $hits.Count)):"
		$hits | Select-Object -Last $Last | ForEach-Object { Write-Host $_.Raw }
	}
	if ($Condense) {
		$c = Get-ArxCondensed $run
		$c.Rare | ForEach-Object { Write-Host $_.Raw }
		Write-Host '--- common families ---'
		$c.Common | ForEach-Object { Write-Host ('{0,6} x {1}' -f $_.Count, $_.Last.Raw) }
	}
	exit 0
}

$noReport = $NoReport -or $DryRun -or ($env:ARXVR_AUTOREPORT -eq '0')
if ($DryRun) {
	# Digest in a scratch root so the real store, queue and runs are untouched
	$env:ARXVR_DIGEST_ROOT = Join-Path ([IO.Path]::GetTempPath()) "arxvr-digest-dryrun-$PID"
}
$info = Invoke-ArxRunDigest -UserDir $UserDir -LogPath $logPath -Since $Since -Source $Source -ExitCode $ExitCode `
	-DurationSec $DurationSec -NoReport:$noReport -Keep $Keep -Copy $Copy -CrashWaitSec $CrashWaitSec
if (-not $info) {
	if (-not $Quiet) { Write-Host "run digest: no log written since the launch at $logPath - nothing to digest" }
	exit 0
}
if (-not $Quiet) { Write-ArxDigestSummary $info }
if ($Print) { Get-Content (Join-Path $info.Dir 'digest.md') -Encoding UTF8 | ForEach-Object { Write-Host $_ } }
if ($DryRun) {
	foreach ($p in $info.All) { Write-Host "    would: [$($p.Level)] $($p.Source) x$($p.Count) -> $($p.Action)" }
	Remove-Item $env:ARXVR_DIGEST_ROOT -Recurse -Force -ErrorAction SilentlyContinue
	exit 0
}

if ($FlushNow) {
	Write-FlushResult (Invoke-ArxReportFlush)
} elseif ($Detach -and @(Get-ChildItem (Get-ArxPendingDir) -Filter '*.json' -ErrorAction SilentlyContinue).Count) {
	# Not waited for: the owner's console returns at once
	$pwsh = (Get-Process -Id $PID).Path
	Start-Process -FilePath $pwsh -WindowStyle Hidden -ArgumentList @('-NoProfile', '-File', "`"$PSCommandPath`"", '-Flush') | Out-Null
}
exit 0
