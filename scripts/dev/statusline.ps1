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
	[ordered]@{
		session_id  = $j.session_id
		name        = $j.session_name
		branch      = $j.worktree.branch
		model       = $j.model.display_name
		effort      = $j.effort.level
		context_pct = $j.context_window.used_percentage
		cost_usd    = $j.cost.total_cost_usd
		lines_added = $j.cost.total_lines_added
		duration_ms = $j.cost.total_duration_ms
		updated     = (Get-Date).ToString('s')
	} | ConvertTo-Json -Compress | Set-Content (Join-Path $ledger "$($j.session_id).json") -Encoding utf8
}

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
