# Claude Code status line (installed into ~/.claude/settings.json -> statusLine by setup-coordinator.ps1).
# Model and effort | context used in this chat | 5-hour and weekly limits | task of the session.
# Keeps the last input in ~/.claude/statusline/last.json so the Arx Fatalis VR coordinator can read the limits.
# ASCII only: the console may not render other characters in the status line's encoding.
$ErrorActionPreference = 'SilentlyContinue'
$raw = [Console]::In.ReadToEnd()
$dir = Join-Path $env:USERPROFILE '.claude\statusline'
New-Item -ItemType Directory -Force $dir | Out-Null
$tmp = Join-Path $dir ("last.{0}.tmp" -f $PID)
Set-Content -Path $tmp -Value $raw -Encoding utf8 -NoNewline
Move-Item -Force $tmp (Join-Path $dir 'last.json')

try { $j = $raw | ConvertFrom-Json } catch { 'Claude'; exit 0 }

# Per-session ledger for the coordinator (D-021): which task, model, context and spend each session is at
if ($j.session_id) {
	$ledger = Join-Path $dir 'sessions'
	New-Item -ItemType Directory -Force $ledger | Out-Null
	$ledgerFile = Join-Path $ledger "$($j.session_id).json"
	$peak = [double]$j.context_window.used_percentage
	try { $old = Get-Content $ledgerFile -Raw | ConvertFrom-Json; if ($old.context_peak -gt $peak) { $peak = [double]$old.context_peak } } catch {}
	[ordered]@{
		session_id  = $j.session_id
		name        = $j.session_name
		branch      = $j.worktree.branch
		model       = $j.model.display_name
		effort      = $j.effort.level
		context_pct = $j.context_window.used_percentage
		context_peak = $peak
		cost_usd    = $j.cost.total_cost_usd
		lines_added = $j.cost.total_lines_added
		duration_ms = $j.cost.total_duration_ms
		updated     = (Get-Date).ToString('s')
	} | ConvertTo-Json -Compress | Set-Content $ledgerFile -Encoding utf8
}

# Usage sampler (#217): at most one line a minute in usage.jsonl - the limits and the cost of the live sessions -
# the history that usage-plan.ps1 calibrates "% of a limit per dollar" from. Never fails or slows the status line.
try {
	$rl0 = $j.rate_limits
	$stamp = Join-Path $dir 'usage.stamp'
	$nowEpoch = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds()
	$due = $true
	if (Test-Path $stamp) { $due = ($nowEpoch - [long](Get-Content $stamp -Raw)) -ge 60 }
	if ($due -and ($rl0.five_hour -or $rl0.seven_day)) {
		Set-Content $stamp $nowEpoch -NoNewline
		$live = [ordered]@{}
		foreach ($f in Get-ChildItem (Join-Path $dir 'sessions') -Filter '*.json' | Where-Object { $_.LastWriteTime -gt (Get-Date).AddMinutes(-10) }) {
			$s = Get-Content $f.FullName -Raw | ConvertFrom-Json
			if ($null -ne $s.cost_usd) { $live[$s.session_id] = [math]::Round([double]$s.cost_usd, 4) }
		}
		$line = [ordered]@{ t = $nowEpoch; fh = $rl0.five_hour.used_percentage; fhr = $rl0.five_hour.resets_at; wk = $rl0.seven_day.used_percentage; wkr = $rl0.seven_day.resets_at; s = $live } | ConvertTo-Json -Compress -Depth 4
		Add-Content -Path (Join-Path $dir 'usage.jsonl') -Value $line -Encoding utf8
	}
} catch {}

function Format-Tokens([double]$n) { if ($n -ge 1e6) { '{0:0.#}M' -f ($n / 1e6) } elseif ($n -ge 1e3) { '{0:0}k' -f ($n / 1e3) } else { "$n" } }

$parts = @()

$model = "$($j.model.display_name)"
if ($j.effort.level) { $model += " $($j.effort.level)" }
$parts += $model

$cw = $j.context_window
if ($cw -and $null -ne $cw.used_percentage) {
	$u = $cw.current_usage
	$used = [double]$u.input_tokens + [double]$u.cache_read_input_tokens + [double]$u.cache_creation_input_tokens
	$parts += ('ctx {0}% {1}/{2}' -f $cw.used_percentage, (Format-Tokens $used), (Format-Tokens $cw.context_window_size))
}

$rl = $j.rate_limits
if ($rl.five_hour) { $parts += ('5h {0:0}%' -f [double]$rl.five_hour.used_percentage) }
if ($rl.seven_day) { $parts += ('week {0:0}%' -f [double]$rl.seven_day.used_percentage) }

if ($j.worktree.branch -match '^(ARXVR-\d+)') { $parts += $Matches[1] }

$parts -join ' | '
