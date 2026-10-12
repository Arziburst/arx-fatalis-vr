# Claude Code UserPromptSubmit hook (configured in .claude/settings.json).
#
# The coordinator's trigger (/coordinator, /new-cycle or "новый цикл") names the session "Coordinator",
# the name the coordinator core uses (CORE §2) and workers report to (start-worker -ReportTo). Workers
# are named by start-worker ("#<n> worker: <title>"), not here. Any other prompt passes through
# untouched. Never blocks: on any problem it prints nothing.

$ErrorActionPreference = 'SilentlyContinue'
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
if ($prompt -match '^\s*(/coordinator\b|/new-cycle\b|новый цикл)') { Write-Title 'Coordinator' }
exit 0
