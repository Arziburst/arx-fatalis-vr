# Claude Code UserPromptSubmit hook (configured in .claude/settings.json).
#
# When a prompt starts with /next-task <n>, /accept-task <n> or /reject-task <n>, name the session after
# the task: "ARXVR-<n> <issue title>" (for a pull request number: the PR title, which already has that
# form). The coordinator's trigger ("новый цикл" or /new-cycle) names the session "ARXVR Coordinator".
# Any other prompt passes through untouched. Never blocks: on any problem it prints nothing.

$ErrorActionPreference = 'SilentlyContinue'
$repo = 'Arziburst/arx-fatalis-vr'
$esc = [char]27; $bel = [char]7

function Write-Title([string]$name) {
	@{
		hookSpecificOutput = @{ hookEventName = 'UserPromptSubmit'; sessionTitle = $name }
		terminalSequence   = "$esc]0;$name$bel"
	} | ConvertTo-Json -Compress -Depth 4
}

[Console]::InputEncoding = [Text.Encoding]::UTF8
$raw = [Console]::In.ReadToEnd()
try { $prompt = [string]($raw | ConvertFrom-Json).prompt } catch { exit 0 }
if ($prompt -match '^\s*(/coordinator\b|/new-cycle\b|новый цикл)') { Write-Title 'ARXVR Coordinator'; exit 0 }
if ($prompt -notmatch '^\s*/(next-task|accept-task|reject-task)\s+#?(\d+)') { exit 0 }
$command = $Matches[1]
$number = [int]$Matches[2]

$name = $null
# REST (the issues endpoint answers for pull requests too): the GraphQL quota is shared by all sessions (#186)
$issue = gh api "repos/$repo/issues/$number" 2>$null | ConvertFrom-Json
if ($issue -and $issue.title) {
	$name = if ($issue.pull_request) { $issue.title } else { "ARXVR-$number $($issue.title)" }
}
if (-not $name) { $name = "ARXVR-$number" }
if ($command -eq 'reject-task') { $name = "$name (fix)" }
if ($name.Length -gt 80) { $name = $name.Substring(0, 77) + '...' }
Write-Title $name
