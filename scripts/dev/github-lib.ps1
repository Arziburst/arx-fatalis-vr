# GitHub access for the dev scripts (#186, D-021): REST first, GraphQL only for the Projects v2 board.
# Dot-source from the other scripts in this folder (board.ps1 does it).
#
# Why: the GraphQL quota is shared by every session of the owner's account; `gh issue view`, `gh pr view`,
# `gh pr list`, `gh issue list` and every `gh project ...` command are GraphQL. With several sessions in
# parallel it ran out and the board scripts failed (TRAPS.md). Rules here:
#   - issues, labels, milestones, comments, PRs, checks and runs go through `gh api repos/...` (REST, its own quota);
#   - the board (Projects v2 has no REST API) is one batched GraphQL query per 100 items, cached for a few
#     minutes (board.ps1), and a status move is one mutation;
#   - a rate-limit error is recognised, explained with its reset time, retried where it is safe and short, and then
#     surfaced as an error that callers can tell apart (Test-GhRateLimitError) and degrade on.
#
# Test hooks (never needed in normal use):
#   $script:ArxGhInvoker   scriptblock { param($GhArgs, $InputText) } returning @{ Exit = <int>; Text = <string> }
#   $script:ArxGhSleep     scriptblock { param($Seconds) } instead of Start-Sleep
#   $env:ARXVR_FORCE_GRAPHQL_ERROR=1  every GraphQL call fails as if the quota were spent (evidence runs)
#   $env:ARXVR_GH_STATE_DIR           where the board cache and the move queue live

$script:ArxGhMaxWaitSeconds = 60          # a rate-limit reset farther away than this is not waited for
# After a rate-limit rejection the resource is marked blocked until its reset (a few minutes for a secondary limit), in
# gh-blocked.json next to the board cache, so every script of every session fails fast instead of hammering GitHub.
# Not written in the forced-error evidence mode. Clear-ArxGhBlock lifts it (set-status.ps1 -Flush).
function Get-ArxGhBlockedUntil([string]$Resource) {
	$path = Join-Path (Get-ArxGhStateDir) 'gh-blocked.json'
	if (-not (Test-Path $path)) { return 0 }
	try { [long](Get-Content -Raw -Encoding UTF8 $path | ConvertFrom-Json).$Resource } catch { 0 }
}

function Set-ArxGhBlocked([string]$Resource, [long]$UntilEpoch) {
	if ($env:ARXVR_FORCE_GRAPHQL_ERROR) { $script:ArxGhBlockedMemory[$Resource] = $UntilEpoch; return }
	try {
		Use-ArxGhLock {
			$path = Join-Path (Get-ArxGhStateDir) 'gh-blocked.json'
			$cur = if (Test-Path $path) { Get-Content -Raw -Encoding UTF8 $path | ConvertFrom-Json } else { [pscustomobject]@{} }
			$cur | Add-Member -NotePropertyName $Resource -NotePropertyValue $UntilEpoch -Force
			Write-ArxGhFile $path $cur
		}
	} catch { $script:ArxGhBlockedMemory[$Resource] = $UntilEpoch }
}

function Clear-ArxGhBlock {
	Remove-Item -Force (Join-Path (Get-ArxGhStateDir) 'gh-blocked.json') -ErrorAction SilentlyContinue
	$script:ArxGhBlockedMemory = @{}
}
$script:ArxGhBlockedMemory = @{}

function Get-ArxGhStateDir {
	$dir = if ($env:ARXVR_GH_STATE_DIR) { $env:ARXVR_GH_STATE_DIR } else { Join-Path $env:LOCALAPPDATA 'ArxFatalisVR' }
	if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Force $dir | Out-Null }
	$dir
}

function Get-ArxEpoch { [DateTimeOffset]::UtcNow.ToUnixTimeSeconds() }
function ConvertFrom-ArxEpoch([long]$Epoch) { [DateTimeOffset]::FromUnixTimeSeconds($Epoch).LocalDateTime }

function Wait-ArxGh([double]$Seconds) {
	if ($script:ArxGhSleep) { & $script:ArxGhSleep $Seconds } else { Start-Sleep -Seconds ([math]::Ceiling($Seconds)) }
}

# Cross-process lock around the cache and queue files (several sessions run these scripts at once).
function Use-ArxGhLock([scriptblock]$Action) {
	$mutex = [Threading.Mutex]::new($false, 'Global\ArxVrGithubState')
	$held = $false
	try {
		try { $held = $mutex.WaitOne(15000) } catch [Threading.AbandonedMutexException] { $held = $true }
		& $Action
	} finally { if ($held) { try { $mutex.ReleaseMutex() } catch {} }; $mutex.Dispose() }
}

function Write-ArxGhFile([string]$Path, $Object) {
	$tmp = "$Path.$PID.tmp"
	[IO.File]::WriteAllText($tmp, ($Object | ConvertTo-Json -Depth 10), [Text.UTF8Encoding]::new($false))
	Move-Item -Force $tmp $Path
}

# --- One gh call ---------------------------------------------------------------------------------------------------------

function Invoke-ArxGhRaw([string[]]$GhArgs, [string]$InputText) {
	if ($script:ArxGhInvoker) { return (& $script:ArxGhInvoker $GhArgs $InputText) }
	if ($env:ARXVR_FORCE_GRAPHQL_ERROR -and $GhArgs.Count -ge 2 -and $GhArgs[1] -eq 'graphql') {
		return @{ Exit = 1; Text = 'GraphQL: API rate limit already exceeded for user ID 0. (forced by ARXVR_FORCE_GRAPHQL_ERROR)' }
	}
	if (-not (Get-Command gh -ErrorAction SilentlyContinue)) { return @{ Exit = 127; Text = 'gh (GitHub CLI) was not found' } }
	$out = if ($null -ne $InputText -and $InputText -ne '') { $InputText | & gh @GhArgs 2>&1 } else { & gh @GhArgs 2>&1 }
	@{ Exit = $LASTEXITCODE; Text = (($out | ForEach-Object { "$_" }) -join "`n").Trim() }
}

function Test-ArxGhRateLimitText([string]$Text) { $Text -match '(?i)rate limit|RATE_LIMITED|abuse detection' }
function Test-ArxGhTransientText([string]$Text) {
	$Text -match '(?i)HTTP 5\d\d|timed? ?out|connection (reset|refused|closed)|unexpected EOF|temporar|network is unreachable|no such host|TLS handshake'
}

# `gh api rate_limit` does not count against any quota. Returns Remaining / Reset (local time) of one resource, or $null.
function Get-ArxGhRateInfo([string]$Resource) {
	try {
		$r = Invoke-ArxGhRaw @('api', 'rate_limit') $null
		if ($r.Exit -ne 0) { return $null }
		$res = ($r.Text | ConvertFrom-Json).resources.$Resource
		if (-not $res) { return $null }
		[pscustomobject]@{ Remaining = [int]$res.remaining; Limit = [int]$res.limit; ResetEpoch = [long]$res.reset; Reset = (ConvertFrom-ArxEpoch ([long]$res.reset)) }
	} catch { $null }
}

function New-ArxGhRateLimitError([string]$Resource, [string]$Detail, $ResetEpoch) {
	$when = if ($ResetEpoch) { "resets at $((ConvertFrom-ArxEpoch $ResetEpoch).ToString('HH:mm:ss'))" } else { 'a secondary limit - try again in a few minutes' }
	$ex = [Exception]::new("GitHub $Resource rate limit ($when). $Detail".Trim())
	$ex.Data['ArxGhRateLimit'] = $Resource
	$ex.Data['ResetEpoch'] = [long]$(if ($ResetEpoch) { $ResetEpoch } else { (Get-ArxEpoch) + 300 })
	$ex
}

# True when the error (an ErrorRecord, as $_ in a catch block) is a GitHub rate-limit error from this library.
function Test-ArxGhRateLimitError($ErrorRecord) {
	$ex = if ($ErrorRecord -is [Management.Automation.ErrorRecord]) { $ErrorRecord.Exception } else { $ErrorRecord }
	[bool]($ex -and $ex.Data -and $ex.Data.Contains('ArxGhRateLimit'))
}

function Get-ArxGhRateLimitResetEpoch($ErrorRecord) {
	$ex = if ($ErrorRecord -is [Management.Automation.ErrorRecord]) { $ErrorRecord.Exception } else { $ErrorRecord }
	[long]$ex.Data['ResetEpoch']
}

# Runs gh with the library's policy. $Safe: the call may be repeated after a transient failure (reads, idempotent writes).
# A rate-limit rejection is always safe to repeat (nothing was executed); it is waited for when the reset is near,
# otherwise it throws the rate-limit error. After one, further calls of the same resource in this process fail fast.
function Invoke-ArxGhCall([string[]]$GhArgs, [string]$InputText, [string]$Resource, [bool]$Safe) {
	$until = [math]::Max([long]$script:ArxGhBlockedMemory[$Resource], $(if ($env:ARXVR_FORCE_GRAPHQL_ERROR) { 0 } else { Get-ArxGhBlockedUntil $Resource }))
	if ((Get-ArxEpoch) -lt $until) {
		throw (New-ArxGhRateLimitError $Resource '(an earlier call already hit it - not asking GitHub again before then)' $until)
	}
	$transient = 0
	$rateRetried = $false
	while ($true) {
		$r = Invoke-ArxGhRaw $GhArgs $InputText
		if ($r.Exit -eq 0) { return $r.Text }
		if (Test-ArxGhRateLimitText $r.Text) {
			$forced = $env:ARXVR_FORCE_GRAPHQL_ERROR -and $Resource -eq 'graphql'
			$info = if ($forced) { [pscustomobject]@{ Remaining = 0; ResetEpoch = (Get-ArxEpoch) + 1800 } } else { Get-ArxGhRateInfo $Resource }
			$primary = $info -and $info.Remaining -le 0
			$wait = if ($primary) { [math]::Max(1, $info.ResetEpoch - (Get-ArxEpoch) + 2) } else { 20 }
			if (-not $rateRetried -and $wait -le $script:ArxGhMaxWaitSeconds) {
				$rateRetried = $true
				Write-Warning "GitHub $Resource rate limit hit ($(if ($primary) { 'primary' } else { 'secondary: the hourly counter still shows ' + $info.Remaining + ' left' })) - waiting $([int]$wait) s, then one retry"
				Wait-ArxGh $wait
				continue
			}
			$resetEpoch = if ($primary) { $info.ResetEpoch } else { $null }
			Set-ArxGhBlocked $Resource $(if ($resetEpoch) { $resetEpoch } else { (Get-ArxEpoch) + 120 })
			throw (New-ArxGhRateLimitError $Resource ($r.Text -split "`n" | Select-Object -First 1) $resetEpoch)
		}
		if ($Safe -and $transient -lt 3 -and (Test-ArxGhTransientText $r.Text)) {
			$delay = 2 * [math]::Pow(2, $transient); $transient++
			Write-Warning "gh $($GhArgs[0..1] -join ' ') failed transiently ($(($r.Text -split "`n")[0])) - retry $transient of 3 in $delay s"
			Wait-ArxGh $delay
			continue
		}
		throw "gh $($GhArgs -join ' ') failed (exit $($r.Exit)): $($r.Text)"
	}
}

# --- REST ----------------------------------------------------------------------------------------------------------------

# One REST call; returns the parsed JSON ($null for an empty answer). -Idempotent marks a write that may be repeated.
function Invoke-ArxGhRest([string]$Path, [string]$Method = 'GET', $Body = $null, [switch]$Idempotent) {
	$ghArgs = @('api', '-X', $Method, $Path)
	$inputText = $null
	if ($null -ne $Body) { $inputText = $Body | ConvertTo-Json -Depth 8 -Compress; $ghArgs += '--input', '-' }
	$text = Invoke-ArxGhCall $ghArgs $inputText 'core' ($Method -eq 'GET' -or [bool]$Idempotent)
	if ($text) { $text | ConvertFrom-Json } else { $null }
}

# A REST list, page by page (100 per page). -Property: the array inside an object answer (e.g. check_runs).
function Invoke-ArxGhRestPages([string]$Path, [string]$Property, [int]$MaxPages = 10) {
	$sep = if ($Path.Contains('?')) { '&' } else { '?' }
	$all = [Collections.Generic.List[object]]::new()
	for ($page = 1; $page -le $MaxPages; $page++) {
		$r = Invoke-ArxGhRest "$Path${sep}per_page=100&page=$page"
		if ($null -eq $r) { break }
		$items = @(if ($Property) { $r.$Property } else { $r }) | Where-Object { $null -ne $_ }
		foreach ($i in $items) { $all.Add($i) }
		if (@($items).Count -lt 100) { break }
	}
	, $all.ToArray()
}

# --- Issues, pull requests, checks, runs (REST views shaped like the `gh ... --json` fields the scripts used) --------------

function Get-GhIssue([int]$Number) {
	$i = Invoke-ArxGhRest "repos/$Repo/issues/$Number"
	if (-not $i) { throw "could not read #$Number" }
	[pscustomobject]@{
		Number = [int]$i.number; Title = $i.title; State = $i.state.ToUpper(); Url = $i.html_url; NodeId = $i.node_id
		IsPullRequest = [bool]$i.pull_request; Body = $i.body
		Labels = @($i.labels | ForEach-Object { $_.name }); Milestone = $i.milestone.title
	}
}

function ConvertTo-GhPull($p) {
	[pscustomobject]@{
		number = [int]$p.number; title = $p.title; url = $p.html_url; body = $p.body
		state = if ($p.merged_at -or $p.merged) { 'MERGED' } else { $p.state.ToUpper() }
		headRefName = $p.head.ref; headRefOid = $p.head.sha; isDraft = [bool]$p.draft
		labels = @($p.labels | ForEach-Object { [pscustomobject]@{ name = $_.name } })
		mergeable = if ($null -eq $p.mergeable) { 'UNKNOWN' } elseif ($p.mergeable) { 'MERGEABLE' } else { 'CONFLICTING' }
		updatedAt = $p.updated_at
	}
}

function Get-GhPull([int]$Number) {
	$p = Invoke-ArxGhRest "repos/$Repo/pulls/$Number"
	if (-not $p) { throw "could not read PR #$Number" }
	ConvertTo-GhPull $p
}

# State: open | closed | all. The list answer has no `mergeable`; -WithMergeable reads each open PR once more for it.
function Get-GhPulls([string]$State = 'open', [int]$MaxPages = 3, [switch]$WithMergeable) {
	$list = Invoke-ArxGhRestPages "repos/$Repo/pulls?state=$State" '' $MaxPages
	foreach ($p in $list) {
		if ($WithMergeable -and $p.state -eq 'open') { $p = Invoke-ArxGhRest "repos/$Repo/pulls/$($p.number)" }
		ConvertTo-GhPull $p
	}
}

# All issues (not pull requests), open and closed. State: open | closed | all.
function Get-GhIssues([string]$State = 'all', [int]$MaxPages = 10) {
	$list = Invoke-ArxGhRestPages "repos/$Repo/issues?state=$State" '' $MaxPages
	foreach ($i in ($list | Where-Object { -not $_.pull_request })) {
		[pscustomobject]@{
			number = [int]$i.number; state = $i.state.ToUpper(); title = $i.title
			labels = @($i.labels | ForEach-Object { [pscustomobject]@{ name = $_.name } })
			milestone = if ($i.milestone) { $i.milestone.title } else { $null }
		}
	}
}

# CI of one commit from the check runs and the commit statuses: State is none | running | failed | green.
function Get-GhCommitChecks([string]$Sha) {
	$runs = @((Invoke-ArxGhRestPages "repos/$Repo/commits/$Sha/check-runs" 'check_runs' 3) | Where-Object { $_ })
	$status = Invoke-ArxGhRest "repos/$Repo/commits/$Sha/status"
	$contexts = @(if ($status.total_count -gt 0) { $status.statuses })
	$state = if (-not $runs -and -not $contexts) { 'none' }
		elseif (($runs | Where-Object { $_.status -ne 'completed' }) -or ($contexts | Where-Object state -eq 'pending')) { 'running' }
		elseif (($runs | Where-Object { $_.conclusion -notin 'success', 'skipped', 'neutral' }) -or ($contexts | Where-Object { $_.state -in 'failure', 'error' })) { 'failed' }
		else { 'green' }
	[pscustomobject]@{
		State = $state
		Checks = @($runs | ForEach-Object { [pscustomobject]@{ Name = $_.name; Status = $_.status; Conclusion = $_.conclusion } }) +
			@($contexts | ForEach-Object { [pscustomobject]@{ Name = $_.context; Status = if ($_.state -eq 'pending') { 'in_progress' } else { 'completed' }; Conclusion = $_.state } })
	}
}

# The latest workflow run of a branch, or $null.
function Get-GhLatestRun([string]$Branch) {
	$r = Invoke-ArxGhRest "repos/$Repo/actions/runs?branch=$Branch&per_page=1"
	$run = @($r.workflow_runs) | Select-Object -First 1
	if ($run) { [pscustomobject]@{ Status = $run.status; Conclusion = $run.conclusion; HeadSha = $run.head_sha; Title = $run.display_title; Id = $run.id } }
}

# --- GraphQL (Projects v2 only) ------------------------------------------------------------------------------------------

# One GraphQL call; returns .data. Queries and the idempotent mutations of board.ps1 are safe to repeat.
function Invoke-ArxGhGraphql([string]$Query, [hashtable]$Variables = @{}) {
	$body = @{ query = $Query; variables = $Variables } | ConvertTo-Json -Depth 10 -Compress
	$text = Invoke-ArxGhCall @('api', 'graphql', '--input', '-') $body 'graphql' $true
	$obj = $text | ConvertFrom-Json
	if ($obj.errors) {
		$msg = ($obj.errors | ForEach-Object { if ($_.type) { "$($_.type): $($_.message)" } else { $_.message } }) -join '; '
		if (Test-ArxGhRateLimitText $msg) { throw (New-ArxGhRateLimitError 'graphql' $msg (Get-ArxGhRateInfo 'graphql').ResetEpoch) }
		throw "GraphQL error: $msg"
	}
	$obj.data
}
