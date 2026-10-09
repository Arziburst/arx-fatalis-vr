# The run digest (#131): read one game run's arx.log and crash reports, group the problems by signature,
# remember which signatures were seen before, and queue GitHub issues for the new ones (docs/vr/TESTING.md,
# "The run digest"). Shared by scripts/dev/log-digest.ps1 and its self-test. Dot-source it.
#
# Two phases, so the owner never waits for the network:
#   1. Invoke-ArxRunDigest   local and fast: parse, digest.md / digest.json / log copy in
#                            %LOCALAPPDATA%\ArxFatalisVR\runs\<run>\, update signatures.json, and queue what
#                            should be reported as files in %LOCALAPPDATA%\ArxFatalisVR\pending-reports\.
#   2. Invoke-ArxReportFlush sends the queue to GitHub - REST only (`gh api`), one call per issue or comment,
#                            never GraphQL (its quota is shared by every session). A failure leaves the item
#                            queued; the next run retries it. Runs detached after a launch (-Detach).
#
# $env:ARXVR_DIGEST_ROOT replaces %LOCALAPPDATA%\ArxFatalisVR (the self-test); $script:ArxGitHubInvoker
# replaces the `gh api` call (the self-test's fake GitHub).

. (Join-Path $PSScriptRoot '..\launch-lib.ps1')

$script:ArxDigestRepo = 'Arziburst/arx-fatalis-vr'
$script:ArxCommentThrottleHours = 24
$script:ArxMaxNewIssuesPerRun = 5
$script:ArxRareFamilyCount = 6      # logdig (F-112): a family seen fewer times is printed line by line

function Get-ArxDigestRoot { if ($env:ARXVR_DIGEST_ROOT) { $env:ARXVR_DIGEST_ROOT } else { Get-ArxAppDataDir } }
function Get-ArxRunsDir { Join-Path (Get-ArxDigestRoot) 'runs' }
function Get-ArxSignatureStorePath { Join-Path (Get-ArxDigestRoot) 'signatures.json' }
function Get-ArxPendingDir { Join-Path (Get-ArxDigestRoot) 'pending-reports' }

# --- Reading a log --------------------------------------------------------------------------------------------

# All lines of a file that another process may still hold open for writing (a live arx.log)
function Read-ArxLines([string]$Path) {
	$stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete)
	try {
		$reader = [IO.StreamReader]::new($stream, [Text.Encoding]::UTF8)
		$lines = [Collections.Generic.List[string]]::new()
		while ($null -ne ($l = $reader.ReadLine())) { $lines.Add($l) }
		, $lines.ToArray()
	} finally { $stream.Dispose() }
}

# A log line: "[I] Context:123           message". Lines that do not start like that continue the previous entry.
$script:ArxLogLine = [regex]'^\[([DIWEC])\]\s+([^\s:]+):(\d+)\s+(.*)$'

# The entries of the last run in the file (the log can hold several when a rotation failed). Returns
# Entries (Index, Level, Context, Line, Text, Raw), the Banner values and the path.
function Read-ArxLogRun([string]$Path) {
	$all = [Collections.Generic.List[object]]::new()
	foreach ($raw in (Read-ArxLines $Path)) {
		$m = $script:ArxLogLine.Match($raw)
		if ($m.Success) {
			$all.Add([pscustomobject]@{ Index = 0; Level = $m.Groups[1].Value; Context = $m.Groups[2].Value; Line = [int]$m.Groups[3].Value; Text = $m.Groups[4].Value; Raw = $raw })
		} elseif ($all.Count -gt 0) {
			$last = $all[$all.Count - 1]
			$last.Text += "`n" + $raw
			$last.Raw += "`n" + $raw
		}
	}
	$start = 0
	for ($i = $all.Count - 1; $i -ge 0; $i--) {
		if ($all[$i].Context -eq 'Startup' -and $all[$i].Text -match '^Starting ') { $start = $i; break }
	}
	$entries = [Collections.Generic.List[object]]::new()
	for ($i = $start; $i -lt $all.Count; $i++) { $all[$i].Index = $entries.Count; $entries.Add($all[$i]) }

	$banner = [ordered]@{ Version = ''; Commit = ''; Branch = ''; Config = ''; Built = ''; RunLocal = $null; Pid = 0; CommandLine = ''; Raw = @() }
	foreach ($e in $entries | Select-Object -First 12) {
		if ($e.Context -ne 'Startup') { continue }
		if ($e.Text -match '^Starting (.+)$') { $banner.Version = $Matches[1]; $banner.Raw += $e.Raw }
		elseif ($e.Text -match '^Build: (\w+) on (.+?), (\w[\w ]*?),') { $banner.Commit = $Matches[1]; $banner.Branch = $Matches[2]; $banner.Config = $Matches[3]; $banner.Raw += $e.Raw }
		elseif ($e.Text -match '^Executable: .*built from (.+)$') { $banner.Built = $Matches[1]; $banner.Raw += $e.Raw }
		elseif ($e.Text -match '^Run: (\d{4}-\d\d-\d\d \d\d:\d\d:\d\d) local .*pid (\d+), command line: ?(.*)$') {
			$banner.RunLocal = [DateTime]::ParseExact($Matches[1], 'yyyy-MM-dd HH:mm:ss', [Globalization.CultureInfo]::InvariantCulture)
			$banner.Pid = [int]$Matches[2]; $banner.CommandLine = $Matches[3]; $banner.Raw += $e.Raw
		}
		elseif ($e.Text -match '^Log: ') { $banner.Raw += $e.Raw }
	}
	[pscustomobject]@{ Path = $Path; Entries = $entries; Banner = [pscustomobject]$banner }
}

# --- Signatures -------------------------------------------------------------------------------------------------

# Numbers, hex, ids and paths replaced, so "entity human_base_0033 not found" and "..._0034 ..." are one problem.
function ConvertTo-ArxNormalizedText([string]$Text) {
	$t = $Text
	$t = [regex]::Replace($t, '"[^"\r\n]*[\\/][^"\r\n]*"', '"<path>"')
	$t = [regex]::Replace($t, "'[^'\r\n]*[\\/][^'\r\n]*'", "'<path>'")
	$t = [regex]::Replace($t, '[A-Za-z]:[\\/][^\s"'']+', '<path>')
	$t = [regex]::Replace($t, '0x[0-9a-fA-F]+', 'N')
	$t = [regex]::Replace($t, '\b[0-9a-fA-F]{8,}\b', 'N')
	$t = [regex]::Replace($t, '\d+(\.\d+)*', 'N')
	$t = [regex]::Replace($t, '[ \t]+', ' ')
	$t.Trim()
}

function Get-ArxHash([string]$Text) {
	$sha = [Security.Cryptography.SHA1]::Create()
	-join ($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($Text))[0..5] | ForEach-Object { $_.ToString('x2') })
}

# Machine-specific parts out of anything that leaves the machine (an issue body)
function Protect-ArxText([string]$Text) {
	$t = [regex]::Replace($Text, '(?i)([A-Za-z]):\\Users\\[^\\/"''\r\n]+', '$1:\Users\<user>')
	[regex]::Replace($t, '(?i)(/Users/)[^/"''\r\n]+', '$1<user>')
}

# Warnings, errors and critical errors of a run grouped by signature. The signature is the level, the source
# file and the normalised message - not the line number, which moves with every edit of the source.
function Get-ArxLogProblems($Run) {
	$groups = [ordered]@{}
	foreach ($e in $Run.Entries) {
		if ($e.Level -notin 'W', 'E', 'C') { continue }
		$norm = ConvertTo-ArxNormalizedText $e.Text
		$key = "$($e.Level)|$($e.Context)|$norm"
		if (-not $groups.Contains($key)) {
			$groups[$key] = [pscustomobject]@{
				Hash = Get-ArxHash $key; Kind = 'log'; Level = $e.Level; Source = "$($e.Context):$($e.Line)"; Context = $e.Context
				Message = $e.Text; Normalized = $norm; Count = 0; First = $e.Index; Lines = $e.Raw
				Allowed = $false; IsNew = $false; Runs = 0; Issue = $null; Action = ''
			}
		}
		$groups[$key].Count++
	}
	@($groups.Values)
}

# --- Crash reports --------------------------------------------------------------------------------------------

# crash.txt of the game's crash processor: title, "Exception code:", "Callstack:" with ` module!0xADDR  File.cpp:12`
function Read-ArxCrashReport([string]$Dir) {
	$file = Join-Path $Dir 'crash.txt'
	if (-not (Test-Path $file)) { return $null }
	$lines = Read-ArxLines $file
	$title = ($lines | Where-Object { $_.Trim() } | Select-Object -First 1)
	$exception = ''
	$frames = @()
	$inStack = $false
	foreach ($l in $lines) {
		if ($l -match '^\s*Exception code:\s*(.+)$') { $exception = $Matches[1].Trim() }
		if ($l -match '^\s*Callstack:') { $inStack = $true; continue }
		if ($inStack) {
			if (-not $l.Trim()) { $inStack = $false; continue }
			$m = [regex]::Match($l, '^\s*(\S+?)!(?:0x[0-9a-fA-F]+)?\s*(.*)$')
			if ($m.Success) { $frames += [pscustomobject]@{ Module = $m.Groups[1].Value; Text = $m.Groups[2].Value.Trim(); Raw = "$($m.Groups[1].Value)!$($m.Groups[2].Value.Trim())" } }   # addresses are ASLR noise: left out
			else { $frames += [pscustomobject]@{ Module = ''; Text = $l.Trim(); Raw = $l.Trim() } }
		}
	}
	$ours = @($frames | Where-Object { $_.Module -like 'arx*' -and $_.Text })
	$top = @(($ours | Select-Object -First 4) | ForEach-Object { $_.Text -replace ':\d+\b', '' })
	$key = "crash|$title|$exception|$($top -join '>')"
	$where = if ($ours) { $ours[0].Text } elseif ($frames) { $frames[0].Raw } else { 'unknown location' }
	[pscustomobject]@{
		Name = Split-Path -Leaf $Dir; Dir = $Dir; Title = $title; Exception = $exception; Frames = $frames; Top = $ours | Select-Object -First 8
		Hash = Get-ArxHash $key; Normalized = $key; Kind = 'crash'; Level = 'C'; Source = $where; Message = "$title ($exception) at $where"; Count = 1
		Text = ($lines -join "`n")
	}
}

# Crash report folders are named after their UTC time: yyyy-MM-dd-HH-mm-ss-NN
function Get-ArxCrashTime([System.IO.DirectoryInfo]$Dir) {
	$m = [regex]::Match($Dir.Name, '^(\d{4})-(\d\d)-(\d\d)-(\d\d)-(\d\d)-(\d\d)')
	if ($m.Success) {
		$g = $m.Groups
		return [DateTime]::new([int]$g[1].Value, [int]$g[2].Value, [int]$g[3].Value, [int]$g[4].Value, [int]$g[5].Value, [int]$g[6].Value, [DateTimeKind]::Utc).ToLocalTime()
	}
	$Dir.LastWriteTime
}

function Get-ArxNewCrashReports([string]$CrashDir, [DateTime]$Since) {
	if (-not (Test-Path $CrashDir)) { return @() }
	$reports = @()
	foreach ($d in Get-ChildItem $CrashDir -Directory -ErrorAction SilentlyContinue) {
		if ((Get-ArxCrashTime $d) -lt $Since.AddSeconds(-2)) { continue }
		$r = Read-ArxCrashReport $d.FullName
		if ($r) { $reports += $r }
	}
	$reports
}

# --- The allow-list ---------------------------------------------------------------------------------------------

# scripts/dev/log-digest-allowlist.txt: one .NET regex per line (# comments), tested against "[W] Context:12  text"
function Read-ArxAllowList([string]$Path = (Join-Path $PSScriptRoot 'log-digest-allowlist.txt')) {
	if (-not (Test-Path $Path)) { return @() }
	@(Get-Content $Path | ForEach-Object { $_.Trim() } | Where-Object { $_ -and -not $_.StartsWith('#') } | ForEach-Object { [regex]::new($_) })
}

# --- The signature store -----------------------------------------------------------------------------------------

# signatures.json is shared by the digest of one run and the detached flush of another: every read-modify-write
# holds a named mutex.
function Use-ArxStore([scriptblock]$Body) {
	$mutex = [Threading.Mutex]::new($false, 'Global\ArxFatalisVR-signatures')
	$held = $false
	try {
		try { $held = $mutex.WaitOne(30000) } catch [Threading.AbandonedMutexException] { $held = $true }
		if (-not $held) { throw 'signatures.json is locked by another process for more than 30 s' }
		$path = Get-ArxSignatureStorePath
		$store = [ordered]@{}
		if (Test-Path $path) {
			try {
				$json = Get-Content $path -Raw | ConvertFrom-Json -AsHashtable
				if ($json -and $json.signatures) { foreach ($k in $json.signatures.Keys) { $store[$k] = $json.signatures[$k] } }
			} catch { Write-Warning "signatures.json is unreadable ($($_.Exception.Message)); starting a new store" }
		}
		$result = & $Body $store
		New-Item -ItemType Directory -Force (Split-Path -Parent $path) | Out-Null
		$tmp = "$path.tmp"
		[IO.File]::WriteAllText($tmp, (@{ version = 1; signatures = $store } | ConvertTo-Json -Depth 6), [Text.UTF8Encoding]::new($false))
		Move-Item -LiteralPath $tmp -Destination $path -Force
		$result
	} finally {
		if ($held) { $mutex.ReleaseMutex() }
		$mutex.Dispose()
	}
}

# --- Decisions: what to report ------------------------------------------------------------------------------------

# Epoch seconds: ConvertFrom-Json turns date strings into DateTime objects, numbers survive the round trip
function Get-ArxNow { [DateTimeOffset]::UtcNow.ToUnixTimeSeconds() }
function ConvertFrom-ArxEpoch($Seconds) { if ($Seconds -and [long]$Seconds -gt 0) { [DateTimeOffset]::FromUnixTimeSeconds([long]$Seconds).UtcDateTime } else { [DateTime]::MinValue } }

# Update the store with this run's signatures and decide for each: new?, allowed?, queue a report?
# A run that is digested twice (same $RunId) counts once. Errors and crashes are filed when new; warnings only when
# new and seen in a second run. A known signature with an issue gets a "seen again" comment at most once a day.
function Update-ArxSignatures($Problems, [string]$RunId, $Banner, $AllowList, [switch]$NoReport) {
	Use-ArxStore {
		param($store)
		foreach ($p in $Problems) {
			$line = "[$($p.Level)] $($p.Source)  $($p.Message)"
			$p.Allowed = [bool]($AllowList | Where-Object { $_.IsMatch($line) } | Select-Object -First 1)
			$known = $store.Contains($p.Hash)
			$entry = if ($known) { $store[$p.Hash] } else {
				@{ kind = $p.Kind; level = $p.Level; source = $p.Source; message = $p.Normalized; sample = $p.Message
				   firstSeen = Get-ArxNow; firstBuild = "$($Banner.Commit) on $($Banner.Branch)"; runs = 0; total = 0; issue = $null
				   lastRun = ''; lastCommentAt = 0; reportQueued = $false }
			}
			$p.IsNew = -not $known
			if ($entry.lastRun -ne $RunId) { $entry.runs = [int]$entry.runs + 1; $entry.total = [int]$entry.total + $p.Count; $entry.lastRun = $RunId }
			$entry.lastSeen = Get-ArxNow
			$entry.lastBuild = "$($Banner.Commit) on $($Banner.Branch)"
			$p.Runs = [int]$entry.runs
			$p.Issue = $entry.issue

			if ($p.Allowed) { $p.Action = 'allow-listed'; $store[$p.Hash] = $entry; continue }
			if ($NoReport) { $p.Action = 'reporting off'; $store[$p.Hash] = $entry; continue }
			if ($entry.issue) {
				$last = ConvertFrom-ArxEpoch $entry.lastCommentAt
				$p.Action = if (([DateTime]::UtcNow - $last).TotalHours -ge $script:ArxCommentThrottleHours) { 'comment' } else { 'seen again (comment throttled)' }
			} elseif ($entry.reportQueued -and (Test-Path (Join-Path (Get-ArxPendingDir) "$($p.Hash).json"))) {
				$p.Action = 'queued'
			} elseif ($p.Level -eq 'W' -and [int]$entry.runs -lt 2) {
				$p.Action = 'new warning - filed when it repeats in a second run'
			} else {
				$p.Action = 'file'
			}
			$store[$p.Hash] = $entry
		}
	}
}

# --- The digest ----------------------------------------------------------------------------------------------------

$script:ArxTimelinePatterns = @(
	'^Starting ', '^Starting in VR', '^OpenXR session (created|state)', '^OpenXR session destroyed', '^Loading level', '^Done loading level',
	'^Clean shutdown', '^VR headset:', '^Using OpenXR runtime', '^Application window is being destroyed'
)

function Get-ArxTimeline($Run) {
	$pattern = [regex]($script:ArxTimelinePatterns -join '|')
	# Every game event of D2 (#130) is a timeline entry: "[I] GameEvent:N  <name> key=value ... t=<s>"
	@($Run.Entries | Where-Object { $_.Level -ne 'D' -and ($_.Context -eq 'GameEvent' -or $pattern.IsMatch($_.Text)) })
}

# logdig (F-112): families of lines (numbers, hex, paths normalised); rare ones in full and in order, the common
# ones as "count x last sample" - a 9,000-line log in a screenful
function Get-ArxCondensed($Run, [int]$MaxCommon = 40) {
	$families = [ordered]@{}
	foreach ($e in $Run.Entries) {
		$key = "$($e.Level)|$($e.Context)|$(ConvertTo-ArxNormalizedText $e.Text)"
		if (-not $families.Contains($key)) { $families[$key] = [pscustomobject]@{ Count = 0; Entries = [Collections.Generic.List[object]]::new(); Last = $null } }
		$f = $families[$key]; $f.Count++; $f.Last = $e
		if ($f.Count -lt $script:ArxRareFamilyCount) { $f.Entries.Add($e) }
	}
	$rare = [Collections.Generic.List[object]]::new()
	$common = [Collections.Generic.List[object]]::new()
	foreach ($f in $families.Values) {
		if ($f.Count -lt $script:ArxRareFamilyCount) { foreach ($e in $f.Entries) { $rare.Add($e) } } else { $common.Add($f) }
	}
	[pscustomobject]@{
		Rare = @($rare | Sort-Object Index)
		Common = @($common | Sort-Object Count -Descending | Select-Object -First $MaxCommon)
		CommonTotal = $common.Count; FamilyCount = $families.Count; LineCount = $Run.Entries.Count
	}
}

function Format-ArxEntry($e) { $e.Raw -replace "`n.*", ' ...' }

function Get-ArxExcerpt($Run, [int]$Index, [int]$Around = 6) {
	$from = [Math]::Max(0, $Index - $Around)
	$to = [Math]::Min($Run.Entries.Count - 1, $Index + $Around)
	($Run.Entries[$from..$to] | ForEach-Object { $_.Raw }) -join "`n"
}

function Format-ArxDuration([double]$Sec) { if ($Sec -ge 60) { '{0}m {1:0}s' -f [int][Math]::Floor($Sec / 60), ($Sec % 60) } else { '{0:0.0}s' -f $Sec } }

# digest.md - what a person (or a coordinator) reads instead of the log
function Format-ArxDigest($Info) {
	$b = $Info.Run.Banner
	$o = [Text.StringBuilder]::new()
	$null = $o.AppendLine("# Run digest $($Info.RunId)").AppendLine()
	$null = $o.AppendLine("- **Build:** $(if ($b.Commit) { "$($b.Commit) on ``$($b.Branch)``, $($b.Config)$(if ($b.Built -match 'dirty') { ' (dirty working tree)' }) - $($b.Version)" } else { "unknown (this log has no build banner, #115) - $($b.Version)" })")
	$null = $o.AppendLine("- **Started by:** $($Info.Source); command line: ``$($b.CommandLine)``")
	$null = $o.AppendLine("- **Result:** exit code $(if ($null -ne $Info.ExitCode) { $Info.ExitCode } else { 'unknown' }), $(if ($Info.CleanShutdown) { 'clean shutdown' } else { '**no ''Clean shutdown'' line** (crash, kill or hang)' }), duration $(Format-ArxDuration $Info.DurationSec)")
	$null = $o.AppendLine("- **Log:** $($Info.Run.Path) ($($Info.Run.Entries.Count) lines of this run); copy: ``arx.log`` next to this file")
	$errors = @($Info.Problems | Where-Object { $_.Level -in 'E', 'C' })
	$warnings = @($Info.Problems | Where-Object { $_.Level -eq 'W' })
	$null = $o.AppendLine("- **Problems:** $($errors.Count) error signature(s) ($(@($errors | Where-Object IsNew).Count) new), $($warnings.Count) warning signature(s) ($(@($warnings | Where-Object IsNew).Count) new), $(@($Info.Crashes).Count) crash report(s)").AppendLine()

	if ($Info.Crashes) {
		$null = $o.AppendLine('## Crash reports').AppendLine()
		foreach ($c in $Info.Crashes) {
			$null = $o.AppendLine("### $($c.Name): $($c.Title) ($($c.Exception))").AppendLine()
			$null = $o.AppendLine('```')
			foreach ($f in ($c.Frames | Select-Object -First 12)) { $null = $o.AppendLine($f.Raw) }
			$null = $o.AppendLine('```').AppendLine("Signature ``$($c.Hash)``: $($c.Action)$(if ($c.Issue) { ", issue #$($c.Issue)" })").AppendLine()
		}
	}

	$null = $o.AppendLine('## Errors and warnings by signature').AppendLine()
	if ($Info.Problems) {
		$null = $o.AppendLine('| | level | count | source | message | signature | what happens |').AppendLine('|---|---|---|---|---|---|---|')
		foreach ($p in $Info.Problems) {
			$msg = (Format-ArxEntry ([pscustomobject]@{ Raw = $p.Message })) -replace '\|', '\|'
			if ($msg.Length -gt 140) { $msg = $msg.Substring(0, 137) + '...' }
			$null = $o.AppendLine("| $(if ($p.IsNew) { '**NEW**' } else { "seen in $($p.Runs) runs" }) | $($p.Level) | $($p.Count) | $($p.Source) | $msg | ``$($p.Hash)`` | $($p.Action)$(if ($p.Issue) { ", #$($p.Issue)" }) |")
		}
		$null = $o.AppendLine()
	} else {
		$null = $o.AppendLine('None - no [W], [E] or [C] line in this run.').AppendLine()
	}

	$null = $o.AppendLine('## Timeline (log order; game events carry t= seconds since startup)').AppendLine().AppendLine('```')
	foreach ($e in (Get-ArxTimeline $Info.Run | Select-Object -First 60)) { $null = $o.AppendLine(("{0,6}  {1}" -f $e.Index, (Format-ArxEntry $e))) }
	$null = $o.AppendLine('```').AppendLine()

	$c = $Info.Condensed
	$null = $o.AppendLine("## The log in a screenful ($($c.LineCount) lines, $($c.FamilyCount) families)").AppendLine()
	$null = $o.AppendLine("Rare lines (families seen fewer than $($script:ArxRareFamilyCount) times) in full and in order, then the common ones as ``count x last sample``.").AppendLine().AppendLine('```')
	foreach ($e in ($c.Rare | Select-Object -First 150)) { $null = $o.AppendLine((Format-ArxEntry $e)) }
	if ($c.Rare.Count -gt 150) { $null = $o.AppendLine("... $($c.Rare.Count - 150) more rare lines in arx.log") }
	$null = $o.AppendLine('--- common families ---')
	foreach ($f in $c.Common) { $null = $o.AppendLine(("{0,6} x {1}" -f $f.Count, (Format-ArxEntry $f.Last))) }
	if ($c.CommonTotal -gt $c.Common.Count) { $null = $o.AppendLine("... $($c.CommonTotal - $c.Common.Count) more common families") }
	$null = $o.AppendLine('```')
	$o.ToString()
}

function Remove-ArxOldRuns([int]$Keep) {
	$dirs = @(Get-ChildItem (Get-ArxRunsDir) -Directory -ErrorAction SilentlyContinue | Sort-Object Name -Descending)
	foreach ($d in ($dirs | Select-Object -Skip $Keep)) { Remove-Item $d.FullName -Recurse -Force -ErrorAction SilentlyContinue }
}

# --- Phase 1: the digest of one run ---------------------------------------------------------------------------------

# Digest the last run in $UserDir's arx.log (or in $LogPath). Returns the info object, or $null when there is
# no log written since $Since. Writes the digest, updates the store, queues reports.
function Invoke-ArxRunDigest(
	[string]$UserDir, [string]$LogPath, $Since, [string]$Source = 'log-digest.ps1',
	$ExitCode = $null, [double]$DurationSec = -1, [switch]$NoReport, [int]$Keep = 30, [string]$Copy = '', [int]$CrashWaitSec = 20
) {
	if (-not $LogPath) { $LogPath = Join-Path $UserDir 'arx.log' }
	if (-not $UserDir) { $UserDir = Split-Path -Parent $LogPath }
	if (-not (Test-Path $LogPath)) { return $null }
	$logItem = Get-Item $LogPath
	if ($Since -and $logItem.LastWriteTime -lt $Since) { return $null }

	$run = Read-ArxLogRun $LogPath
	if ($run.Entries.Count -eq 0) { return $null }
	$banner = $run.Banner
	$started = if ($banner.RunLocal) { $banner.RunLocal } elseif ($Since) { $Since } else { $logItem.CreationTime }
	$runId = "{0:yyyyMMdd-HHmmss}-pid{1}" -f $started, $banner.Pid
	if ($DurationSec -lt 0) { $DurationSec = [Math]::Max(0, ($logItem.LastWriteTime - $started).TotalSeconds) }
	$clean = [bool]($run.Entries | Where-Object { $_.Text -eq 'Clean shutdown' } | Select-Object -First 1)

	# The crash processor (arx.exe --crashinfo) writes the report after the game process is gone: after an unclean
	# exit give it time to finish before looking for the report
	$crashDir = Join-Path $UserDir 'crashes'
	$unclean = -not $clean -and $null -ne $ExitCode -and [int]$ExitCode -ne 0
	if ($unclean -and $CrashWaitSec -gt 0) {
		$until = (Get-Date).AddSeconds($CrashWaitSec)
		do {
			$processing = [bool](Get-CimInstance Win32_Process -Filter "Name='arx.exe'" -ErrorAction SilentlyContinue | Where-Object { $_.CommandLine -match '--crashinfo' })
			$found = @(Get-ArxNewCrashReports $crashDir $started).Count -gt 0
			if ($found -and -not $processing) { break }
			Start-Sleep -Milliseconds 500
		} while ((Get-Date) -lt $until)
	}
	$problems = @(Get-ArxLogProblems $run)
	$crashes = @(Get-ArxNewCrashReports $crashDir $started)
	$all = @($problems) + @($crashes | ForEach-Object { $_ | Add-Member -NotePropertyName Allowed -NotePropertyValue $false -PassThru | Add-Member -NotePropertyName IsNew -NotePropertyValue $false -PassThru | Add-Member -NotePropertyName Runs -NotePropertyValue 0 -PassThru | Add-Member -NotePropertyName Issue -NotePropertyValue $null -PassThru | Add-Member -NotePropertyName Action -NotePropertyValue '' -PassThru | Add-Member -NotePropertyName First -NotePropertyValue -1 -PassThru })
	$allow = Read-ArxAllowList
	Update-ArxSignatures -Problems $all -RunId $runId -Banner $banner -AllowList $allow -NoReport:$NoReport | Out-Null

	# One root cause often logs several lines, and a broken build can log hundreds: new issues per run are capped;
	# the rest stay in the digest and are filed by a later run if they come back
	$newIssues = 0
	foreach ($p in (@($crashes) + @($problems))) {
		if ($p.Action -eq 'file' -and ++$newIssues -gt $script:ArxMaxNewIssuesPerRun) {
			$p.Action = "new, not filed: more than $script:ArxMaxNewIssuesPerRun new problems in one run (a later run files it if it comes back)"
		}
	}

	$info = [pscustomobject]@{
		RunId = $runId; Run = $run; Source = $Source; ExitCode = $ExitCode; DurationSec = $DurationSec; CleanShutdown = $clean
		Problems = $problems; Crashes = $crashes; All = $all; Condensed = Get-ArxCondensed $run; Dir = ''; Queued = 0; Copy = $Copy
	}

	# The run folder
	$dir = Join-Path (Get-ArxRunsDir) $runId
	New-Item -ItemType Directory -Force $dir | Out-Null
	$info.Dir = $dir
	Copy-Item -LiteralPath $LogPath -Destination (Join-Path $dir 'arx.log') -Force
	foreach ($c in $crashes) {
		$cd = Join-Path $dir "crash-$($c.Name)"
		New-Item -ItemType Directory -Force $cd | Out-Null
		Copy-Item -LiteralPath (Join-Path $c.Dir 'crash.txt') -Destination $cd -Force
	}
	[IO.File]::WriteAllText((Join-Path $dir 'digest.md'), (Format-ArxDigest $info), [Text.UTF8Encoding]::new($false))
	$json = [ordered]@{
		runId = $runId; source = $Source; exitCode = $ExitCode; durationSec = [Math]::Round($DurationSec, 1); cleanShutdown = $clean
		build = @{ commit = $banner.Commit; branch = $banner.Branch; config = $banner.Config; built = $banner.Built }; commandLine = $banner.CommandLine
		problems = @($all | ForEach-Object { [ordered]@{ signature = $_.Hash; kind = $_.Kind; level = $_.Level; source = $_.Source; count = $_.Count; message = $_.Message; new = $_.IsNew; runs = $_.Runs; issue = $_.Issue; action = $_.Action } })
	}
	[IO.File]::WriteAllText((Join-Path $dir 'digest.json'), ($json | ConvertTo-Json -Depth 6), [Text.UTF8Encoding]::new($false))

	# Queue the reports
	foreach ($p in (@($crashes) + @($problems))) {
		if ($p.Action -notin 'file', 'comment') { continue }
		Add-ArxPendingReport $info $p
		$info.Queued++
	}
	Remove-ArxOldRuns -Keep $Keep
	$info
}

# --- The queue ------------------------------------------------------------------------------------------------------

function Get-ArxIssueTitle($P) {
	$msg = (ConvertTo-ArxNormalizedText ($P.Message -replace "`n.*", '')) -replace '<path>', '...'
	if ($msg.Length -gt 90) { $msg = $msg.Substring(0, 87) + '...' }
	if ($P.Kind -eq 'crash') { "Crash: $($P.Title) at $(Protect-ArxText $P.Source)" } else { "[$($P.Level)] $($P.Context): $msg" }
}

function Get-ArxIssueBody($Info, $P) {
	$b = $Info.Run.Banner
	$o = [Text.StringBuilder]::new()
	$null = $o.AppendLine("<!-- arxvr-auto-report: sig=$($P.Hash) -->")
	$null = $o.AppendLine("Filed automatically by ``scripts/dev/log-digest.ps1`` (#131) after a game run - the first time this signature was seen. Triage: confirm or close as noise (and add the line to ``scripts/dev/log-digest-allowlist.txt``), link to the right epic, set the milestone.").AppendLine()
	$null = $o.AppendLine('| | |').AppendLine('|---|---|')
	$null = $o.AppendLine("| signature | ``$($P.Hash)`` ($($P.Kind), level $($P.Level)) |")
	$null = $o.AppendLine("| first seen in | build ``$($b.Commit)`` on branch ``$($b.Branch)``, $($b.Config)$(if ($b.Built -match 'dirty') { ', dirty working tree' }) |")
	$null = $o.AppendLine("| started by | $($Info.Source); command line ``$(Protect-ArxText $b.CommandLine)`` |")
	$null = $o.AppendLine("| this run | exit code $(if ($null -ne $Info.ExitCode) { $Info.ExitCode } else { 'unknown' }), $(if ($Info.CleanShutdown) { 'clean shutdown' } else { 'no clean shutdown' }), $(Format-ArxDuration $Info.DurationSec) |")
	$null = $o.AppendLine("| how often | $($P.Count) time(s) in this run, $($P.Runs) run(s) so far |").AppendLine()
	if ($P.Kind -eq 'crash') {
		$null = $o.AppendLine("## Crash: $($P.Title) ($($P.Exception))").AppendLine().AppendLine('```')
		$null = $o.AppendLine((Protect-ArxText (($P.Frames | Select-Object -First 16 | ForEach-Object { $_.Raw }) -join "`n")))
		$null = $o.AppendLine('```').AppendLine()
		$null = $o.AppendLine("The full report (``crash.txt``, ``crash.dmp``) and the log are in the reporter's run folder ``runs\$($Info.RunId)``; ask the owner if you need them.").AppendLine()
	} else {
		$null = $o.AppendLine('## Log excerpt (around the first occurrence)').AppendLine().AppendLine('```')
		$null = $o.AppendLine((Protect-ArxText (Get-ArxExcerpt $Info.Run $P.First)))
		$null = $o.AppendLine('```').AppendLine()
	}
	$null = $o.AppendLine('## Digest of the run').AppendLine().AppendLine('<details><summary>digest</summary>').AppendLine()
	$digest = Protect-ArxText (Format-ArxDigest $Info)
	if ($digest.Length -gt 30000) { $digest = $digest.Substring(0, 30000) + "`n... (cut)" }
	$null = $o.AppendLine($digest).AppendLine().AppendLine('</details>')
	$o.ToString()
}

function Get-ArxSeenAgainText($Info, $P) {
	$b = $Info.Run.Banner
	"Seen again: build ``$($b.Commit)`` on ``$($b.Branch)``, $($P.Count) time(s) in the run started by $($Info.Source), $($P.Runs) run(s) so far, exit code $(if ($null -ne $Info.ExitCode) { $Info.ExitCode } else { 'unknown' }).$(if ($P.Kind -eq 'crash') { ' Crash: ' + (Protect-ArxText $P.Source) })"
}

function Add-ArxPendingReport($Info, $P) {
	$dir = Get-ArxPendingDir
	New-Item -ItemType Directory -Force $dir | Out-Null
	$item = [ordered]@{
		signature = $P.Hash; action = $P.Action; kind = $P.Kind; level = $P.Level; queuedAt = Get-ArxNow; attempts = 0; lastError = ''
		title = Get-ArxIssueTitle $P; body = Get-ArxIssueBody $Info $P; comment = Get-ArxSeenAgainText $Info $P
	}
	$path = Join-Path $dir "$($P.Hash).json"
	[IO.File]::WriteAllText($path, ($item | ConvertTo-Json -Depth 4), [Text.UTF8Encoding]::new($false))
	if ($P.Action -eq 'file') {
		Use-ArxStore { param($s) if ($s.Contains($P.Hash)) { $s[$P.Hash].reportQueued = $true } } | Out-Null
	}
}

# --- Phase 2: GitHub (REST only) --------------------------------------------------------------------------------------

# One REST call: `gh api` with a JSON body from a file. Returns the parsed response; throws with gh's message.
function Invoke-ArxGitHub([string]$Method, [string]$Path, $Body = $null) {
	if ($script:ArxGitHubInvoker) { return (& $script:ArxGitHubInvoker $Method $Path $Body) }
	$gh = Get-Command gh -ErrorAction SilentlyContinue
	if (-not $gh) { throw 'gh (GitHub CLI) was not found' }
	$ghArgs = @('api', '-X', $Method, $Path)
	$tmp = $null
	try {
		if ($null -ne $Body) {
			$tmp = [IO.Path]::GetTempFileName()
			[IO.File]::WriteAllText($tmp, ($Body | ConvertTo-Json -Depth 5), [Text.UTF8Encoding]::new($false))
			$ghArgs += '--input', $tmp
		}
		$out = & gh @ghArgs 2>&1
		if ($LASTEXITCODE) { throw "gh api $Method $Path failed: $(($out | Out-String).Trim())" }
		$text = ($out | Out-String).Trim()
		if ($text) { $text | ConvertFrom-Json } else { $null }
	} finally { if ($tmp) { Remove-Item $tmp -Force -ErrorAction SilentlyContinue } }
}

# The open or closed auto-reported issue that carries a signature's marker, or $null. The issue list is read once per flush.
function Find-ArxAutoIssue([string]$Hash, [ref]$Cache) {
	if ($null -eq $Cache.Value) {
		$issues = @()
		for ($page = 1; $page -le 5; $page++) {
			$batch = @(Invoke-ArxGitHub GET "repos/$script:ArxDigestRepo/issues?labels=auto-reported&state=all&per_page=100&page=$page")
			$issues += $batch
			if ($batch.Count -lt 100) { break }
		}
		$Cache.Value = $issues
	}
	$Cache.Value | Where-Object { $_.body -and $_.body.Contains("arxvr-auto-report: sig=$Hash") } | Select-Object -First 1
}

# Send the queued reports. Returns what was done. Waits for another flush (up to 60 s) so two processes never
# create the same issue; an item that fails stays queued with its error. Stops at the first sign of no network or a
# spent rate limit.
function Invoke-ArxReportFlush {
	$result = [pscustomobject]@{ Created = @(); Commented = @(); Reopened = @(); Dropped = @(); Failed = @(); Pending = 0 }
	$dir = Get-ArxPendingDir
	if (-not (Test-Path $dir) -or -not (Get-ChildItem $dir -Filter '*.json' -ErrorAction SilentlyContinue)) { return $result }

	New-Item -ItemType Directory -Force (Get-ArxDigestRoot) | Out-Null
	$lockPath = Join-Path (Get-ArxDigestRoot) 'flush.lock'
	$lock = $null
	$deadline = (Get-Date).AddSeconds(60)
	while (-not $lock -and (Get-Date) -lt $deadline) {
		try { $lock = [IO.File]::Open($lockPath, [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None) }
		catch [IO.IOException] { Start-Sleep -Milliseconds 500 }
	}
	if (-not $lock) { $result.Pending = @(Get-ChildItem $dir -Filter '*.json').Count; return $result }
	try {
		$items = @(Get-ChildItem $dir -Filter '*.json' | Sort-Object LastWriteTime)
		if (-not $items) { return $result }
		# Only into the private repository
		$repo = Invoke-ArxGitHub GET "repos/$script:ArxDigestRepo"
		if (-not $repo -or $repo.private -ne $true) { throw "$script:ArxDigestRepo is not a private repository - nothing is uploaded" }
		$cache = $null
		foreach ($file in $items) {
			$item = Get-Content $file.FullName -Raw | ConvertFrom-Json -AsHashtable
			try {
				$hash = $item.signature
				$entry = Use-ArxStore { param($s) if ($s.Contains($hash)) { $s[$hash] } else { $null } }
				$issue = if ($entry -and $entry.issue) { [int]$entry.issue } else { 0 }
				if (-not $issue) {
					$found = Find-ArxAutoIssue $hash ([ref]$cache)
					if ($found) { $issue = [int]$found.number }
				}
				$sendComment = $false
				if (-not $issue) {
					$created = Invoke-ArxGitHub POST "repos/$script:ArxDigestRepo/issues" @{ title = $item.title; body = $item.body; labels = @('bug', 'auto-reported') }
					$issue = [int]$created.number
					$result.Created += $issue
					if ($null -ne $cache) { $cache += $created }
					Use-ArxStore { param($s) if ($s.Contains($hash)) { $s[$hash].issue = $issue; $s[$hash].reportQueued = $false; $s[$hash].lastCommentAt = Get-ArxNow } } | Out-Null
				} else {
					$sendComment = $true
					Use-ArxStore { param($s) if ($s.Contains($hash)) { $s[$hash].issue = $issue; $s[$hash].reportQueued = $false } } | Out-Null
				}
				if ($sendComment) {
					$last = if ($entry) { ConvertFrom-ArxEpoch $entry.lastCommentAt } else { [DateTime]::MinValue }
					# A closed issue whose problem came back is reopened (a regression, or noise nobody put on the allow-list),
					# whatever the throttle says; an open one gets its comment at most once a day
					$current = Invoke-ArxGitHub GET "repos/$script:ArxDigestRepo/issues/$issue"
					$reopen = $current -and $current.state -eq 'closed'
					if (-not $reopen -and ([DateTime]::UtcNow - $last).TotalHours -lt $script:ArxCommentThrottleHours) {
						$result.Dropped += $issue
					} else {
						if ($reopen) {
							$null = Invoke-ArxGitHub PATCH "repos/$script:ArxDigestRepo/issues/$issue" @{ state = 'open' }
							$item.comment = "Reopened: this problem came back after the issue was closed. If it is harmless noise, add the line to ``scripts/dev/log-digest-allowlist.txt`` and close the issue again.`n`n" + $item.comment
							$result.Reopened += $issue
						}
						$null = Invoke-ArxGitHub POST "repos/$script:ArxDigestRepo/issues/$issue/comments" @{ body = $item.comment }
						$result.Commented += $issue
						Use-ArxStore { param($s) if ($s.Contains($hash)) { $s[$hash].lastCommentAt = Get-ArxNow } } | Out-Null
					}
				}
				Remove-Item $file.FullName -Force
			} catch {
				$item.attempts = [int]$item.attempts + 1
				$item.lastError = "$($_.Exception.Message)"
				[IO.File]::WriteAllText($file.FullName, ($item | ConvertTo-Json -Depth 4), [Text.UTF8Encoding]::new($false))
				$result.Failed += "$($item.signature): $($item.lastError)"
				break    # no network, rate limit or a rejected request: the next run retries the whole queue
			}
		}
	} catch {
		$result.Failed += "$($_.Exception.Message)"
	} finally {
		$lock.Dispose()
		Remove-Item $lockPath -Force -ErrorAction SilentlyContinue
	}
	$result.Pending = @(Get-ChildItem $dir -Filter '*.json' -ErrorAction SilentlyContinue).Count
	$result
}

# A few lines for the console after a run
function Write-ArxDigestSummary($Info) {
	$b = $Info.Run.Banner
	$errors = @($Info.All | Where-Object { $_.Level -in 'E', 'C' })
	$warnings = @($Info.All | Where-Object { $_.Level -eq 'W' })
	Write-Host "==> Run digest: $(Join-Path $Info.Dir 'digest.md')" -ForegroundColor Cyan
	Write-Host "    $(if ($b.Commit) { "$($b.Commit.Substring(0, [Math]::Min(9, $b.Commit.Length))) on $($b.Branch)" } else { 'build unknown (no banner)' }), exit $(if ($null -ne $Info.ExitCode) { $Info.ExitCode } else { '?' }), $(Format-ArxDuration $Info.DurationSec), $(if ($Info.CleanShutdown) { 'clean shutdown' } else { 'NO clean shutdown' }); errors $($errors.Count) ($(@($errors | Where-Object { $_.IsNew -and -not $_.Allowed }).Count) new), warnings $($warnings.Count) ($(@($warnings | Where-Object { $_.IsNew -and -not $_.Allowed }).Count) new), crashes $(@($Info.Crashes).Count)"
	foreach ($p in ($Info.All | Where-Object { $_.IsNew -and -not $_.Allowed } | Select-Object -First 5)) {
		$msg = $p.Message -replace "`n.*", ''
		if ($msg.Length -gt 100) { $msg = $msg.Substring(0, 97) + '...' }
		Write-Host "    NEW [$($p.Level)] $($p.Source) x$($p.Count): $msg -> $($p.Action)" -ForegroundColor Yellow
	}
	if ($Info.Queued) { Write-Host "    $($Info.Queued) report(s) queued for GitHub (sent in the background, or by the next run if there is no network)" }
}
