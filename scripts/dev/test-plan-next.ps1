<#
.SYNOPSIS
	Self-test of plan-next.ps1 (#187): the parsing of docs/vr/MVP.md and every rule that decides whether a task may start.

.DESCRIPTION
	Runs plan-next-lib.ps1 on a fixture plan with the awkward cells of the real tables (remarks, "no longer", "everything else",
	arrow chains) and on small made-up boards, agent lists and PR lists - no network, no `claude`, no board cache. Also parses the
	real docs/vr/MVP.md (so a table edit that breaks the parser fails CI) and runs plan-next.ps1 itself on fixture files (-Offline).
	CI runs it after the build (.github/workflows/build-windows.yml). Exit code 0 when everything passed, 1 otherwise.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'plan-next-lib.ps1')

$script:failures = 0
$script:checks = 0
function Check([string]$Name, [bool]$Ok, $Detail = $null) {
	$script:checks++
	if ($Ok) { Write-Host "  ok    $Name" } else { $script:failures++; Write-Host "  FAIL  $Name$(if ($null -ne $Detail) { " - got: $Detail" })" -ForegroundColor Red }
}
function Same($A, $B) { (@($A | Sort-Object) -join ',') -eq (@($B | Sort-Object) -join ',') }
function Get-Task($Plan, [int]$N) { $Plan.Tasks | Where-Object Number -eq $N }
function Item([int]$N, [string]$Status, [string[]]$Labels = @('task')) { [pscustomobject]@{ Number = $N; Title = "Task $N"; Status = $Status; Labels = $Labels } }
function Agent([string]$Id, [string]$Name, [string]$State = 'working') { [pscustomobject]@{ id = $Id; kind = 'background'; name = $Name; status = 'busy'; state = $State } }
function Pull([int]$Number, [string]$Branch) { [pscustomobject]@{ number = $Number; headRefName = $Branch } }
function Ready($R) { @($R.Ready | ForEach-Object Number) }
function Reason($R, [int]$N) { (@($R.Waiting | Where-Object Number -eq $N | ForEach-Object Reasons) -join ' | ') }

$fixture = @'
# MVP
## Task queue
### A
1. #10 - base
2. #11 - frame loop
3. #12 - mirror
7. #14 - research
7. #15 - sim
## Parallel plan
### Dependencies
| Task | Area | Lane | Depends on |
|---|---|---|---|
| #10 base | build | plain | — |
| #11 frame | vr-runtime | VR | — |
| #12 mirror | vr-runtime | VR | #11 |
| #13 render | render | VR | #11, #14 (accepted via #15) |
| #14 research | docs | plain | — (decisions before #16) |
| #15 sim | build | plain | #10 |
| #16 body | render | VR | #12, decisions on #14 |
| #17 input | combat | VR | #14 (no longer #16, D-016) |
| #18, #19 runes | magic | VR | #16 |
| #20 video | docs | VR | everything else |
| #21 docs | docs | plain | late, once the tooling settles |
### Waves
| Wave | VR lane | Plain lane |
|---|---|---|
| **0** — sequential, until #10 is merged | — | #10 |
| **1** | #11 → #12 | #14, #15 *(after the owner's decisions on #10 / #12)* |
| **2** | #13 | — |
| **3** | #16, #17 | — |
| **4** | #18 → #19 | #21 |
| **5** | #20 | — |
## Estimate
Nothing here is a table.
'@

Write-Host 'Parsing the fixture plan'
$plan = Read-MvpPlan $fixture
Check 'twelve tasks, from both tables' ($plan.Tasks.Count -eq 12) $plan.Tasks.Count
Check 'queue order' (Same $plan.Queue @(10, 11, 12, 14, 15)) ($plan.Queue -join ',')
Check 'area and lane come from the Dependencies row' ((Get-Task $plan 12).Area -eq 'vr-runtime' -and (Get-Task $plan 12).Lane -eq 'VR')
Check '"—" is no dependency' ((Get-Task $plan 10).DependsOn.Count -eq 0)
Check 'numbers in the remark after "—" are not dependencies (#14)' ((Get-Task $plan 14).DependsOn.Count -eq 0)
Check '"(accepted via #15)" keeps #15 as a dependency' (Same (Get-Task $plan 13).DependsOn @(11, 14, 15)) ((Get-Task $plan 13).DependsOn -join ',')
Check '"decisions on #14" keeps #14' (Same (Get-Task $plan 16).DependsOn @(12, 14)) ((Get-Task $plan 16).DependsOn -join ',')
Check '"(no longer #16)" is dropped' (Same (Get-Task $plan 17).DependsOn @(14)) ((Get-Task $plan 17).DependsOn -join ',')
Check 'one row, two tasks: both get the dependencies' ((Same (Get-Task $plan 18).DependsOn @(16)) -and (Get-Task $plan 19).DependsOn -contains 16)
Check 'an arrow chain makes the later task depend on the earlier (#19 on #18, #12 on #11)' (((Get-Task $plan 19).DependsOn -contains 18) -and ((Get-Task $plan 12).DependsOn -contains 11))
Check 'an arrow does not leak past a comma (#14 does not depend on #12)' (-not ((Get-Task $plan 14).DependsOn -contains 12))
Check '"everything else" = every other task' (Same (Get-Task $plan 20).DependsOn @(10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 21)) ((Get-Task $plan 20).DependsOn -join ',')
Check 'text without numbers = no dependency' ((Get-Task $plan 21).DependsOn.Count -eq 0)
Check 'waves from the lane cells' ((Get-Task $plan 10).Wave -eq 0 -and (Get-Task $plan 11).Wave -eq 1 -and (Get-Task $plan 19).Wave -eq 4 -and (Get-Task $plan 20).Wave -eq 5)
Check 'a number inside a "*(remark)*" does not move the task to that wave (#10 stays in wave 0)' ((Get-Task $plan 10).Wave -eq 0)

Write-Host 'Parsing the real docs/vr/MVP.md'
$real = Read-MvpPlan (Get-Content -Raw -Encoding UTF8 (Join-Path $PSScriptRoot '..\..\docs\vr\MVP.md'))
function RealDeps([int]$N) { @((Get-Task $real $N).DependsOn) }
Check 'more than 40 tasks and a queue' ($real.Tasks.Count -gt 40 -and $real.Queue.Count -gt 40) "$($real.Tasks.Count) tasks, $($real.Queue.Count) queued"
Check '#36 depends on #72 #24 #74 and the research #118 #120' ((RealDeps 36) -contains 72 -and (RealDeps 36) -contains 24 -and (RealDeps 36) -contains 74 -and (RealDeps 36) -contains 118 -and (RealDeps 36) -contains 120) ((RealDeps 36) -join ',')
Check '#37 depends on #74 and #166, not on #36 (D-016)' ((RealDeps 37) -contains 74 -and (RealDeps 37) -contains 166 -and (RealDeps 37) -notcontains 36) ((RealDeps 37) -join ',')
Check '#166 (research) has no dependency' ((RealDeps 166).Count -eq 0) ((RealDeps 166) -join ',')
Check '#19 depends on #71 #20 and the simulator #112' ((RealDeps 19) -contains 71 -and (RealDeps 19) -contains 20 -and (RealDeps 19) -contains 112) ((RealDeps 19) -join ',')
Check '#44 follows #43 and #45 follows #44 (the arrow chain of wave 7)' ((RealDeps 44) -contains 43 -and (RealDeps 45) -contains 44)
Check '#77 (the video) depends on everything' ((RealDeps 77).Count -gt 40)
Check 'every task of the Dependencies table has an area, a lane and a wave' (@($real.Tasks | Where-Object { $_.Area -and (-not $_.Lane -or $null -eq $_.Wave) }).Count -eq 0) (($real.Tasks | Where-Object { $_.Area -and (-not $_.Lane -or $null -eq $_.Wave) } | ForEach-Object Number) -join ',')

Write-Host 'Dependencies, waves and the order'
$board = @(
	(Item 10 'Done'), (Item 11 'Done'),
	(Item 12 'Todo' @('task', 'area:vr-runtime', 'lane:vr')), (Item 14 'Todo' @('task', 'area:docs')), (Item 15 'Todo' @('task', 'area:build')),
	(Item 13 'Todo' @('task', 'area:render', 'lane:vr')), (Item 21 'Backlog' @('task', 'area:docs'))
)
$r = Get-NextPlan -Plan $plan -Items $board -Agents @()
Check 'the three wave-1 tasks of different areas are ready, in queue order' ((Ready $r) -join ',' -eq '12,14,15') ((Ready $r) -join ',')
Check 'a task with open dependencies waits and names them with their status' ((Reason $r 13) -match 'waits for #14 \(Todo\), #15 \(Todo\)') (Reason $r 13)
Check 'a later wave is not open while an earlier one has work (#13)' ((Reason $r 13) -match 'wave 2 is not open yet \(wave 1 still has #12, #14, #15\)') (Reason $r 13)
Check 'Backlog tasks are not listed at all' (-not (@($r.Waiting) + @($r.Ready) | Where-Object Number -eq 21))
Check 'the earliest open wave is reported' ($r.Limits.CurrentWave -eq 1)
$r = Get-NextPlan -Plan $plan -Items (@((Item 12 'Done' @('task')), (Item 14 'Done'), (Item 15 'Done')) + $board[0, 1, 5]) -Agents @()
Check 'all dependencies Done and wave 1 finished: #13 is ready' ((Ready $r) -contains 13) ((Ready $r) -join ',')
$r = Get-NextPlan -Plan $plan -Items (@((Item 14 'Done'), (Item 15 'Done')) + $board[0, 1, 2, 5]) -Agents @() -WaveLookahead 0
Check 'with #12 still Todo, #13 waits for the wave even though #14 #15 are Done' ((Reason $r 13) -match 'wave 2 is not open yet') (Reason $r 13)
$r = Get-NextPlan -Plan $plan -Items (@((Item 14 'Done'), (Item 15 'Done'), (Item 12 'Done')) + $board[0, 1] + (Item 13 'Todo' @('task', 'area:render', 'lane:vr')) + (Item 16 'Todo' @('task', 'area:render', 'lane:vr'))) -Agents @()
Check 'a Done dependency chain opens the next wave: #13 ready, #16 (same area, wave 3) waits' ((Ready $r) -join ',' -eq '13') ((Ready $r) -join ',')
$r = Get-NextPlan -Plan $plan -Items (@((Item 14 'Done'), (Item 15 'Done'), (Item 12 'Done')) + $board[0, 1] + (Item 13 'Todo' @('task', 'area:render', 'lane:vr')) + (Item 17 'Todo' @('task', 'area:combat', 'lane:vr'))) -Agents @() -WaveLookahead 0
Check 'wave 3 is not open while wave 2 has #13' ((Reason $r 17) -match 'wave 3 is not open yet \(wave 2 still has #13\)') (Reason $r 17)
$r = Get-NextPlan -Plan $plan -Items (@((Item 14 'Done'), (Item 15 'Done'), (Item 12 'Done')) + $board[0, 1] + (Item 13 'Todo' @('task', 'area:render', 'lane:vr')) + (Item 17 'Todo' @('task', 'area:combat', 'lane:vr'))) -Agents @() -WaveLookahead 1
Check '-WaveLookahead 1 lets the next wave start' ((Ready $r) -join ',' -eq '13,17') ((Ready $r) -join ',')
$r = Get-NextPlan -Plan $plan -Items @((Item 10 'Done'), (Item 13 'Todo' @('task', 'area:render'))) -Agents @()
Check 'a dependency that is not on the board waits as "not on the board"' ((Reason $r 13) -match '#11 \(not on the board\)') (Reason $r 13)

Write-Host 'The owner-design gate (D-017)'
$r = Get-NextPlan -Plan $plan -Items @((Item 10 'Done'), (Item 15 'Todo' @('task', 'area:build', 'needs-design')), (Item 14 'Todo' @('task', 'area:docs'))) -Agents @()
Check 'needs-design is never ready even with every dependency done' ((Ready $r) -notcontains 15) ((Ready $r) -join ',')
Check 'and says why' ((Reason $r 15) -match "owner's design talk") (Reason $r 15)
Check 'other tasks are not held back by it' ((Ready $r) -contains 14)

Write-Host 'Areas'
$r = Get-NextPlan -Plan $plan -Items @((Item 10 'Done'), (Item 11 'Done'), (Item 12 'In Progress' @('task', 'area:vr-runtime', 'lane:vr')), (Item 30 'Todo' @('bug', 'area:vr-runtime'))) -Agents @()
Check 'an area used by an In Progress task is busy, naming the task' ((Reason $r 30) -match 'area vr-runtime busy \(#12\)') (Reason $r 30)
Check 'an unplanned task is noted as such' ((@($r.Waiting | Where-Object Number -eq 30).Notes -join '') -match 'not in the MVP plan')
$r = Get-NextPlan -Plan $plan -Items @((Item 10 'Done'), (Item 14 'Todo' @('task', 'area:docs')), (Item 30 'Todo' @('bug', 'area:docs'))) -Agents @()
Check 'two ready tasks of one area: the first in order is chosen, the other waits for it' (((Ready $r) -join ',' -eq '14') -and ((Reason $r 30) -match 'area docs taken by #14')) "$(Ready $r) / $(Reason $r 30)"
$r = Get-NextPlan -Plan $plan -Items @((Item 10 'Done'), (Item 30 'Todo' @('bug')), (Item 14 'Todo' @('task', 'area:docs'))) -Agents @()
Check 'an unplanned task without an area label cannot be judged and waits' ((Reason $r 30) -match 'no area:\* label') (Reason $r 30)
$r = Get-NextPlan -Plan $plan -Items @((Item 10 'Done'), (Item 14 'Todo' @('task')), (Item 15 'Todo' @('task'))) -Agents @()
Check 'a planned task without an area label falls back to the table (#14 docs, #15 build)' ((Ready $r) -join ',' -eq '14,15') ((Ready $r) -join ',')

Write-Host 'Workers, pull requests, the VR lane'
$base = @((Item 10 'Done'), (Item 14 'Todo' @('task', 'area:docs')), (Item 15 'Todo' @('task', 'area:build')))
$agents = @(
	(Agent 'c0' 'ARXVR Coordinator' 'done'), (Agent 'r1' 'ARXVR-12 review (PR #5)'), (Agent 'w2' 'ARXVR-14 research stuff' 'working'),
	(Agent 'w3' 'ARXVR-15 sim stuff' 'done'), (Agent 'w4' 'ARXVR-16 body' 'blocked')
)
$r = Get-NextPlan -Plan $plan -Items ($base + (Item 16 'Testing' @('task', 'area:render', 'lane:vr'))) -Agents $agents
Check 'a live worker on a Todo task: not ready, running' ((Reason $r 14) -match 'a worker is already running on it \(w2\)' -and (@($r.Running | ForEach-Object Number) -contains 14)) (Reason $r 14)
Check 'coordinator, reviewer sessions and finished workers do not count' ((@($r.Running | ForEach-Object Number) -join ',') -eq '14') (@($r.Running | ForEach-Object Number) -join ',')
Check 'a worker waiting on a task in Testing holds no slot and no area, and is shown under Testing' (($r.Limits.Workers -eq 1) -and (@($r.Testing)[0].Worker -match 'w4'))
Check 'the finished worker on #15 does not stop it' ((Ready $r) -contains 15) ((Ready $r) -join ',')
$r = Get-NextPlan -Plan $plan -Items $base -PullRequests @((Pull 7 'ARXVR-15-build-sim')) -Agents @()
Check 'an open PR of a task: not ready, and its area is busy' (((Reason $r 15) -match 'a pull request is already open \(#7\)') -and ((Ready $r) -contains 14)) (Reason $r 15)
$r = Get-NextPlan -Plan $plan -Items ($base + (Item 30 'Todo' @('bug', 'area:build'))) -PullRequests @((Pull 7 'ARXVR-15-build-sim')) -Agents @()
Check 'the area of an open PR is busy for others' ((Reason $r 30) -match 'area build busy \(#15\)') (Reason $r 30)
$r = Get-NextPlan -Plan $plan -Items (@((Item 10 'Done'), (Item 15 'Testing' @('task', 'area:build')), (Item 30 'Todo' @('bug', 'area:build')))) -PullRequests @((Pull 7 'ARXVR-15-build-sim')) -Agents @()
Check 'a PR waiting in Testing does not hold its area' ((Ready $r) -contains 30) ((Ready $r) -join ',')
$items3 = @((Item 10 'Done'), (Item 40 'In Progress' @('task', 'area:core')), (Item 41 'In Progress' @('task', 'area:input')), (Item 42 'In Progress' @('task', 'area:camera')), (Item 14 'Todo' @('task', 'area:docs')))
$r = Get-NextPlan -Plan $plan -Items $items3 -Agents @() -MaxWorkers 3
Check 'three workers running, -MaxWorkers 3: no free slot' ((Reason $r 14) -match 'no free worker slot \(3 of 3 in use\)') (Reason $r 14)
$r = Get-NextPlan -Plan $plan -Items $items3 -Agents @() -MaxWorkers 4
Check '-MaxWorkers 4 frees one' ((Ready $r) -contains 14) ((Ready $r) -join ',')
$r = Get-NextPlan -Plan $plan -Items @((Item 10 'Done'), (Item 40 'In Progress' @('task', 'area:core')), (Item 41 'In Progress' @('task', 'area:input')), (Item 14 'Todo' @('task', 'area:docs')), (Item 15 'Todo' @('task', 'area:build'))) -Agents @() -MaxWorkers 3
Check 'the slots are counted with the tasks chosen in the same run (two running + #14 fills the last, #15 waits)' (((Ready $r) -join ',' -eq '14') -and (Reason $r 15) -match 'no free worker slot') "$(Ready $r) / $(Reason $r 15)"
$r = Get-NextPlan -Plan $plan -Items @((Item 10 'Done'), (Item 40 'In Progress' @('task', 'area:core')), (Item 14 'Todo' @('task', 'area:docs'))) -Agents $null
Check 'unreadable sessions are reported, not read as "no workers"' (($r.Warnings -join ' ') -match 'could not be read' -and (Ready $r) -contains 14)
$r = Get-NextPlan -Plan $plan -Items @((Item 10 'Done'), (Item 40 'In Progress' @('task', 'area:core')), (Item 14 'Todo' @('task', 'area:docs'))) -Agents @()
Check 'In Progress without any live worker is flagged' (($r.Warnings -join ' ') -match '#40 is In Progress on the board but no live worker')
$vr = @('task', 'lane:vr')
$r = Get-NextPlan -Plan $plan -Items @((Item 10 'Done'), (Item 50 'In Progress' ($vr + 'area:render')), (Item 51 'In Progress' ($vr + 'area:combat')), (Item 52 'Todo' ($vr + 'area:magic')), (Item 53 'Todo' @('task', 'area:docs'))) -Agents @() -MaxWorkers 6
Check 'two VR tasks in progress: a third waits, a plain one goes' (((Reason $r 52) -match 'VR lane full: 2 of 2 in progress') -and (Ready $r) -contains 53) "$(Reason $r 52) / $(Ready $r)"
$r = Get-NextPlan -Plan $plan -Items @((Item 10 'Done'), (Item 60 'Testing' ($vr + 'area:render')), (Item 61 'Testing' ($vr + 'area:combat')), (Item 62 'Testing' ($vr + 'area:input')), (Item 63 'Testing' ($vr + 'area:camera')), (Item 64 'Todo' ($vr + 'area:magic')), (Item 65 'Todo' @('task', 'area:docs'))) -Agents @()
Check 'four PRs in Testing pause the VR lane, the plain lane goes on' (((Reason $r 64) -match 'VR lane paused: 4 PRs wait in Testing') -and (Ready $r) -contains 65) "$(Reason $r 64) / $(Ready $r)"
Check 'a VR task per the table counts as VR even without the label (#12)' (((Get-NextPlan -Plan $plan -Items @((Item 11 'Done'), (Item 12 'Todo' @('task', 'area:vr-runtime')), (Item 70 'In Progress' $vr), (Item 71 'In Progress' $vr)) -Agents @()).Waiting | Where-Object Number -eq 12).Reasons -join '' -match 'VR lane full')

Write-Host 'Model and effort (D-015)'
$r = Get-NextPlan -Plan $plan -Items @((Item 10 'Done'), (Item 14 'Todo' @('task', 'area:docs')), (Item 15 'Todo' @('task', 'area:build')), (Item 30 'Todo' @('bug', 'area:core')), (Item 31 'Todo' @('task', 'area:render', 'model:fable'))) -Agents @() -MaxWorkers 6
$by = @{}; foreach ($t in $r.Ready) { $by[$t.Number] = $t }
Check 'docs: Sonnet, medium, type docs' ($by[14].Model -eq 'sonnet' -and $by[14].Effort -eq 'medium' -and $by[14].Type -eq 'docs') "$($by[14].Model) $($by[14].Effort) $($by[14].Type)"
Check 'build: Sonnet, the start line is complete' ($by[15].StartLine -eq '.\scripts\dev\start-worker.ps1 15 -Type build -Model sonnet -Effort medium') $by[15].StartLine
Check 'engine work: Opus, high; a bug is type fix' ($by[30].Model -eq 'opus' -and $by[30].Effort -eq 'high' -and $by[30].Type -eq 'fix') "$($by[30].Model) $($by[30].Effort) $($by[30].Type)"
Check 'Fable only with the model:fable label: xhigh' ($by[31].Model -eq 'fable' -and $by[31].Effort -eq 'xhigh' -and $by[31].StartLine -match '-Model fable -Effort xhigh') $by[31].StartLine
Check 'no model:fable label, no Fable (even for hard areas)' ($by[30].Model -ne 'fable')
Check 'build plus engine area: Opus' ((Get-WorkerModelAdvice @('task') @('build', 'render')).Model -eq 'opus')
Check 'no area at all: Opus' ((Get-WorkerModelAdvice @('task') @()).Model -eq 'opus')

Write-Host 'plan-next.ps1 end to end (-Offline)'
$dir = Join-Path ([IO.Path]::GetTempPath()) "arxvr-plan-next-test-$PID"
New-Item -ItemType Directory -Force $dir | Out-Null
try {
	$mvp = Join-Path $dir 'MVP.md'
	[IO.File]::WriteAllText($mvp, $fixture, [Text.UTF8Encoding]::new($false))
	[IO.File]::WriteAllText((Join-Path $dir 'items.json'), (@((Item 10 'Done'), (Item 14 'Todo' @('task', 'area:docs')), (Item 15 'Todo' @('task', 'area:build', 'needs-design')), (Item 40 'In Progress' @('task', 'area:core'))) | ConvertTo-Json -Depth 4), [Text.UTF8Encoding]::new($false))
	[IO.File]::WriteAllText((Join-Path $dir 'agents.json'), (@((Agent 'w1' 'ARXVR-40 core work')) | ConvertTo-Json -Depth 4), [Text.UTF8Encoding]::new($false))
	$pwsh = (Get-Process -Id $PID).Path
	$out = & $pwsh -NoProfile -File (Join-Path $PSScriptRoot 'plan-next.ps1') -Offline $dir -Mvp $mvp 2>&1 | ForEach-Object { "$_" }
	$text = $out -join "`n"
	Check 'exit code 0' ($LASTEXITCODE -eq 0) $LASTEXITCODE
	Check 'text: READY lists #14 with its start-worker line' ($text -match '(?s)READY \(1\).*#14 Task 14.*start-worker\.ps1 14 -Type docs -Model sonnet -Effort medium') $text
	Check 'text: RUNNING shows the live worker of #40' ($text -match 'RUNNING \(1\)[\s\S]*#40 Task 40[\s\S]*worker w1') $text
	Check 'text: WAITING names the design gate for #15' ($text -match "#15 Task 15\s+- waits for the owner's design talk") $text
	$json = (& $pwsh -NoProfile -File (Join-Path $PSScriptRoot 'plan-next.ps1') -Offline $dir -Mvp $mvp -Json) -join "`n" | ConvertFrom-Json
	Check '-Json: the same answer as data' ((@($json.Ready | ForEach-Object Number) -join ',' -eq '14') -and $json.Limits.Workers -eq 1 -and $json.Limits.MaxWorkers -eq 0) ($json | ConvertTo-Json -Compress -Depth 3)
} finally { Remove-Item -Recurse -Force $dir -ErrorAction SilentlyContinue }

Write-Host ''
if ($script:failures) { Write-Host "$script:failures of $script:checks checks FAILED" -ForegroundColor Red; exit 1 }
Write-Host "all $script:checks checks passed" -ForegroundColor Green

