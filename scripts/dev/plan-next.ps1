# retired 2026-10-22: the plan is the sprints in docs/vr/MVP.md / labels sprint-N
<#
.SYNOPSIS
	The coordinator's task order by rule (#187, D-021): which MVP tasks may start now, and why the others may not.

.DESCRIPTION
	Reads docs/vr/MVP.md (the Dependencies and Waves tables, the task queue), the board (statuses and labels, from the
	board cache of #186), the open pull requests and the running sessions (`claude agents --json`), and prints
	  READY    the tasks to start now, in wave and queue order, each with the start-worker.ps1 line (model and effort by D-015)
	  RUNNING  what already runs: board In Progress, live worker sessions, open task PRs - with the worker's state
	  TESTING  tasks waiting for the owner's VR test
	  WAITING  every other Todo task with the reason ("waits for #20 (In Progress)", "area render busy (#19)", ...)
	A task is ready when: it is Todo; it has no `needs-design` label (D-017 - the owner's design talk comes first);
	no PR and no worker is on it; every dependency is Done; its wave is the earliest open wave (+ -WaveLookahead); no
	running or already chosen task shares its area; the VR lane has room (at most 2 in progress and 4 in Testing); and
	(only with -MaxWorkers) fewer workers than that run. Model: Sonnet for area:build / area:docs only, Opus otherwise, Fable only with the
	`model:fable` label the coordinator sets (D-015).

	GitHub is read the way the other dev scripts do (github-lib.ps1, #186): REST for the PRs, the board from its cache.
	When the open PRs or the sessions cannot be read the plan is still printed, with a warning of what is unknown;
	a board that cannot be read at all stops the script (it never plans on an empty board).

.PARAMETER MaxWorkers
	Optional worker cap; default 0 = none (D-027: every runnable task runs until the usage caps stop it).

.PARAMETER WaveLookahead
	Waves after the earliest open one that may start already (default 0: the rule of the plan).

.PARAMETER Json
	Print the whole answer as JSON instead of text.

.PARAMETER Offline
	A folder with items.json (board items: Number, Title, Status, Labels), pulls.json and agents.json (optional), read
	instead of GitHub and `claude agents` - for the self-test (test-plan-next.ps1) and for what-if runs.

.PARAMETER Mvp
	The MVP.md to read. Default: docs/vr/MVP.md of this working copy.
#>
[CmdletBinding()]
param(
	[int]$MaxWorkers = 0,
	[int]$WaveLookahead = 0,
	[switch]$Json,
	[string]$Offline,
	[string]$Mvp = (Join-Path $PSScriptRoot '..\..\docs\vr\MVP.md')
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8
. (Join-Path $PSScriptRoot 'plan-next-lib.ps1')

$plan = Read-MvpPlan (Get-Content -Raw -Encoding UTF8 $Mvp)
if (-not $plan.Tasks.Count) { throw "no tasks found in the Dependencies table of $Mvp" }

$problems = [Collections.Generic.List[string]]::new()
if ($Offline) {
	$read = { param($name, $required) $f = Join-Path $Offline $name; if (Test-Path $f) { @(Get-Content -Raw -Encoding UTF8 $f | ConvertFrom-Json) } elseif ($required) { throw "$f not found" } else { @() } }
	$items = & $read 'items.json' $true
	$pulls = & $read 'pulls.json' $false
	$agents = & $read 'agents.json' $false
} else {
	. (Join-Path $PSScriptRoot 'board.ps1')
	$items = @(Get-BoardItems) # a board that cannot be read throws (or comes from the cache, with a warning) - never "empty"
	if (-not $items) { throw 'the board has no items - refusing to plan on an empty board' }
	$pulls = @()
	try { $pulls = @(Get-GhPulls 'open' 3) } catch { $problems.Add("open pull requests could not be read ($($_.Exception.Message)) - tasks that already have a PR may be listed as ready") }
	$agents = $null
	try {
		$out = & claude agents --json 2>$null
		if ($LASTEXITCODE -eq 0 -and $out) { $agents = @($out | ConvertFrom-Json) }
	} catch {}
}

$result = Get-NextPlan -Plan $plan -Items $items -PullRequests $pulls -Agents $agents -MaxWorkers $MaxWorkers -WaveLookahead $WaveLookahead
$result.Warnings = @($problems) + @($result.Warnings)

if ($Json) { $result | ConvertTo-Json -Depth 8; return }

function Format-Task($t) { "#$($t.Number) $($t.Title)" }
$l = $result.Limits
"PLAN  workers $($l.Workers)$(if ($l.MaxWorkers) { "/$($l.MaxWorkers)" }) | VR in progress $($l.VrInProgress)/$($l.MaxVrInProgress) | VR in Testing $($l.VrTesting)/$($l.MaxVrTesting) | earliest open wave: $(if ($null -ne $l.CurrentWave) { $l.CurrentWave } else { 'none' })$(if ($l.WaveLookahead) { " (+$($l.WaveLookahead))" })"
foreach ($w in $result.Warnings) { "WARNING $w" }
''
"READY ($($result.Ready.Count))"
if (-not $result.Ready.Count) { '  none' }
foreach ($t in $result.Ready) {
	"  $(Format-Task $t)   [area:$($t.Areas -join ',area:'), $(if ($t.Vr) { 'VR lane' } else { 'plain lane' }), wave $(if ($null -ne $t.Wave) { $t.Wave } else { '-' })]"
	"      $($t.StartLine)"
	foreach ($n in $t.Notes) { "      note: $n" }
}
''
"RUNNING ($($result.Running.Count))"
if (-not $result.Running.Count) { '  none' }
foreach ($t in $result.Running) {
	"  $(Format-Task $t)   [$(if ($t.Areas.Count) { 'area:' + ($t.Areas -join ',area:') } else { 'no area' }), $(if ($t.Vr) { 'VR lane' } else { 'plain lane' })] $($t.Sources -join '; ')$(if ($t.Worker) { " - worker $($t.Worker)" })"
}
''
"TESTING ($($result.Testing.Count))"
if (-not $result.Testing.Count) { '  none' }
foreach ($t in $result.Testing) { "  $(Format-Task $t)$(if ($t.Vr) { '   [VR lane]' })$(if ($t.Worker) { " - worker $($t.Worker) waits for the verdict (holds no slot)" })" }
''
"WAITING ($($result.Waiting.Count))"
if (-not $result.Waiting.Count) { '  none' }
foreach ($t in $result.Waiting) {
	"  $(Format-Task $t)"
	foreach ($r in $t.Reasons) { "      - $r" }
	foreach ($n in $t.Notes) { "      note: $n" }
}
