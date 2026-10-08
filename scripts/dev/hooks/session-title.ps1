# Claude Code UserPromptSubmit hook (configured in .claude/settings.json).
#
# When a prompt starts with /next-task <n>, /accept-task <n> or /reject-task <n>, name the session after
# the task: "ARXVR-<n> <issue title>" (for a pull request number: the PR title, which already has that
# form). Any other prompt passes through untouched. Never blocks: on any problem it prints nothing.

$ErrorActionPreference = 'SilentlyContinue'
$repo = 'Arziburst/arx-fatalis-vr'

$raw = [Console]::In.ReadToEnd()
try { $prompt = [string]($raw | ConvertFrom-Json).prompt } catch { exit 0 }
if ($prompt -notmatch '^\s*/(next-task|accept-task|reject-task)\s+#?(\d+)') { exit 0 }
$command = $Matches[1]
$number = [int]$Matches[2]

$name = $null
$issue = gh issue view $number -R $repo --json title,url 2>$null | ConvertFrom-Json
if ($issue -and $issue.url -match '/issues/') {
	$name = "ARXVR-$number $($issue.title)"
} else {
	$pr = gh pr view $number -R $repo --json title 2>$null | ConvertFrom-Json
	if ($pr) { $name = $pr.title }
}
if (-not $name) { $name = "ARXVR-$number" }
if ($command -eq 'reject-task') { $name = "$name (fix)" }
if ($name.Length -gt 80) { $name = $name.Substring(0, 77) + '...' }

$esc = [char]27; $bel = [char]7
@{
	hookSpecificOutput = @{ hookEventName = 'UserPromptSubmit'; sessionTitle = $name }
	terminalSequence   = "$esc]0;$name$bel"
} | ConvertTo-Json -Compress -Depth 4
