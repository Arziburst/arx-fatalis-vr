<#
.SYNOPSIS
	Self-test of the safety hook (safety-guard.ps1, #185): sample commands piped through it.

.DESCRIPTION
	Builds a fixture of fake repositories in a temp folder (the owner's copy, a worktree, a worktree inside
	the owner's copy, a job tmp dir - only .git files, the hook reads nothing else), runs a table of
	commands through the rules and prints the verdict of each: BLOCK with the rule, or allow. Then it
	runs the real hook process a few times: the JSON contract of a denial, silence for an allowed call,
	fail-open on garbage input, the denial log, and the time per call (the limit is 300 ms).
	Exit code 1 when any row differs from its expectation.

.EXAMPLE
	.\scripts\dev\hooks\test-safety-guard.ps1
#>
[CmdletBinding()]
param([int]$TimingRuns = 8)

$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$guard = Join-Path $here 'safety-guard.ps1'

# --- Fixture ---------------------------------------------------------------------------------------------

$root = Join-Path ([IO.Path]::GetTempPath()) ("safety-guard-test-" + [Guid]::NewGuid().ToString('N').Substring(0, 8))
$O = Join-Path $root 'arx-fatalis-vr'                          # the owner's copy, on main
$W = Join-Path $root 'arx-fatalis-vr-wt\ARXVR-1-feat-x'   # a worker's worktree, on a task branch
$I = Join-Path $O '.claude\worktrees\inner'           # a worktree inside the owner's folder (EnterWorktree puts them there)
$E = Join-Path $root 'elsewhere'                      # any other folder
$J = Join-Path $root 'job'                            # CLAUDE_JOB_DIR; its tmp subfolder is allowed

function New-Fixture-Worktree([string]$Dir, [string]$Name, [string]$Branch) {
	$gitDir = Join-Path $O ".git\worktrees\$Name"
	New-Item -ItemType Directory -Force $gitDir, $Dir | Out-Null
	Set-Content -LiteralPath (Join-Path $gitDir 'HEAD') "ref: refs/heads/$Branch"
	Set-Content -LiteralPath (Join-Path $gitDir 'commondir') '../..'
	Set-Content -LiteralPath (Join-Path $Dir '.git') ("gitdir: " + ($gitDir -replace '\\', '/'))
}
New-Item -ItemType Directory -Force (Join-Path $O '.git'), $E, (Join-Path $J 'tmp') | Out-Null
Set-Content -LiteralPath (Join-Path $O '.git\HEAD') 'ref: refs/heads/main'
New-Fixture-Worktree $W 'wt1' 'ARXVR-1-feat-x'
New-Fixture-Worktree $I 'inner' 'ARXVR-2-fix-y'

$savedLocal = $env:LOCALAPPDATA; $savedJob = $env:CLAUDE_JOB_DIR
$env:LOCALAPPDATA = Join-Path $root 'local'           # the denial log goes into the fixture
$env:CLAUDE_JOB_DIR = $J
. $guard

$cwds = @{ W = $W; O = $O; I = $I }

# Cwd, expectation (allow, or the rule that must fire), tool, command. {O} {W} {I} {E} {J} are the fixture folders.
$cases = @(
	# --- pushes to main / master, force-pushes -----------------------------------------------------------
	@('W', 'push-main', 'git push origin main'),
	@('W', 'push-main', 'git push origin HEAD:main'),
	@('W', 'push-main', 'git push origin HEAD:refs/heads/master'),
	@('W', 'push-main', 'git push https://github.com/Arziburst/arx-fatalis-vr.git ARXVR-1-feat-x:main'),
	@('W', 'push-main', 'git push git@github.com:Arziburst/arx-fatalis-vr.git main'),
	@('W', 'push-main', 'git push upstream master'),
	@('W', 'push-main', 'git push origin :main'),
	@('W', 'push-main', 'git push origin --delete main'),
	@('W', 'push-main', 'git push -d origin master'),
	@('W', 'push-main', 'git push --all'),
	@('W', 'push-main', 'git -C {W} push origin main'),
	@('W', 'push-main', 'cd {W}; git push origin main'),
	@('W', 'push-main', 'git status && git push -u origin main'),
	@('W', 'push-main', 'bash -c "git push origin main"'),
	@('W', 'push-main', 'pwsh -NoProfile -Command "git push origin main"'),
	@('W', 'push-main', 'gh api repos/Arziburst/arx-fatalis-vr/git/refs/heads/main -X DELETE'),
	@('W', 'push-main', 'gh api repos/Arziburst/arx-fatalis-vr/git/refs/heads/main -X PATCH -f sha=abc -F force=true'),
	@('O', 'push-main', 'git push'),
	@('O', 'push-main', 'git push origin'),
	@('W', 'force-push', 'git push --force origin ARXVR-1-feat-x'),
	@('W', 'force-push', 'git push -f origin ARXVR-1-feat-x'),
	@('W', 'force-push', 'git push -fu origin ARXVR-1-feat-x'),
	@('W', 'force-push', 'git push --force-with-lease'),
	@('W', 'force-push', 'git push origin +ARXVR-1-feat-x'),
	@('W', 'force-push', 'git push --mirror origin'),
	@('W', 'allow', 'git push -u origin ARXVR-1-feat-x'),
	@('W', 'allow', 'git push'),
	@('W', 'allow', 'git push origin HEAD'),
	@('W', 'allow', 'git push origin ARXVR-1-feat-x:ARXVR-1-feat-x'),
	@('W', 'allow', 'git push --follow-tags --dry-run origin ARXVR-1-feat-x'),
	@('W', 'allow', 'git push origin --delete ARXVR-1-feat-x'),
	@('W', 'allow', 'git push origin main-fix'),
	@('W', 'allow', 'gh api repos/Arziburst/arx-fatalis-vr/git/refs/heads/main'),
	@('W', 'allow', 'gh api -X DELETE repos/Arziburst/arx-fatalis-vr/git/refs/heads/ARXVR-1-feat-x'),
	@('W', 'allow', 'gh api repos/Arziburst/arx-fatalis-vr/pulls/12/merge -X PUT -f merge_method=merge'),
	@('W', 'allow', 'gh pr merge 12 --merge --delete-branch --subject "ARXVR-1 feat: x (#12)"'),
	@('W', 'allow', 'gh pr create --title "ARXVR-1 feat: x" --body "the hook blocks git push --force and rm -rf /"'),
	# --- recursive deletes outside the worktree ----------------------------------------------------------
	@('W', 'delete-outside', 'rm -rf {E}/x'),
	@('W', 'delete-outside', 'Remove-Item -Recurse -Force {E}'),
	@('W', 'delete-outside', 'Remove-Item -Force -Path {E}\a -Recurse'),
	@('W', 'delete-outside', 'Remove-Item {E}\* -Recurse'),
	@('W', 'delete-outside', 'ri -r -fo {E}'),
	@('W', 'delete-outside', 'rd /s /q {E}'),
	@('W', 'delete-outside', 'cmd /c rd /s /q {E}'),
	@('W', 'delete-outside', 'rm -rf /'),
	@('W', 'delete-outside', 'rm -rf ~'),
	@('W', 'delete-outside', 'rm -rf $env:USERPROFILE\foo'),
	@('W', 'delete-outside', 'rm -rf "$env:LOCALAPPDATA\ArxFatalisVR"'),
	@('W', 'delete-outside', 'rm -rf {W}\..'),
	@('W', 'delete-outside', 'rm -r -f {O}'),
	@('W', 'delete-outside', 'pwsh -c "Remove-Item -Recurse {E}"'),
	@('W', 'delete-outside', 'cd {E}; rm -rf .'),
	@('W', 'delete-outside', 'Write-Host hi; if ($true) { Remove-Item -Recurse -Force {E} }'),
	@('W', 'allow', 'rm -rf {W}\build'),
	@('W', 'allow', 'Remove-Item -Recurse -Force .\build'),
	@('W', 'allow', 'rm -rf ./out/*'),
	@('W', 'allow', 'rm -rf {W}/../ARXVR-1-feat-x/x'),
	@('W', 'allow', 'rm -rf {J}\tmp\x'),
	@('W', 'allow', 'rd /s /q {W}\build'),
	@('W', 'allow', 'Remove-Item -Recurse -Force $dir'),
	@('W', 'allow', 'foreach ($d in $dirs) { Remove-Item -Recurse -Force $d }'),
	@('W', 'allow', 'rm {E}\f.txt'),
	@('W', 'allow', 'Remove-Item {E}\f.txt'),
	@('W', 'allow', 'git clean -fdx'),
	@('W', 'allow', '.\scripts\build-windows.ps1 -Clean'),
	@('O', 'allow', '.\scripts\dev\new-worktree.ps1 -Issue 5 -Remove'),
	@('O', 'allow', 'git worktree remove {E}\x'),
	@('I', 'allow', 'rm -rf {I}\x'),
	@('O', 'allow', 'rm -rf {O}\build'),
	# --- writes into the owner's copy from another worktree ----------------------------------------------
	@('W', 'owner-write', 'Set-Content -Path {O}\a.txt -Value x'),
	@('W', 'owner-write', 'echo x > {O}\a.txt'),
	@('W', 'owner-write', 'echo x>>{O}/a.txt'),
	@('W', 'owner-write', '.\run.ps1 2>&1 | Out-File {O}\log.txt'),
	@('W', 'owner-write', 'Copy-Item .\a {O}\b'),
	@('W', 'owner-write', 'cp a {O}/b'),
	@('W', 'owner-write', 'Copy-Item -Path .\a -Destination {O}\b'),
	@('W', 'owner-write', 'mv a {O}\b'),
	@('W', 'owner-write', 'New-Item -ItemType File {O}\x'),
	@('W', 'owner-write', 'sed -i s/a/b/ {O}/README.md'),
	@('W', 'owner-write', 'Remove-Item {O}\README.md'),
	@('W', 'owner-write', 'cd {O}; echo hi > f.txt'),
	@('W', 'owner-write', 'Set-Location {O}; Set-Content f.txt x'),
	@('W', 'owner-write', 'git -C {O} commit -m x'),
	@('W', 'owner-write', 'git -C {O} add .'),
	@('W', 'owner-write', 'git -C {O} pull'),
	@('I', 'owner-write', 'Set-Content {O}\a x'),
	@('W', 'owner-write', 'Edit:{O}\README.md'),
	@('W', 'owner-write', 'Write:{O}\docs\x.md'),
	@('W', 'allow', 'Edit:{W}\README.md'),
	@('I', 'allow', 'Write:{I}\x.md'),
	@('O', 'allow', 'Edit:{O}\README.md'),
	@('W', 'allow', 'Copy-Item {O}\a .\a'),
	@('W', 'allow', 'cat {O}\README.md'),
	@('W', 'allow', 'Get-Content {O}\a > .\out.txt'),
	@('W', 'allow', 'git -C {O} status'),
	@('W', 'allow', 'git -C {O} log --oneline -3'),
	@('W', 'allow', 'git -C {O} fetch --prune origin'),
	@('W', 'allow', 'ls {O}'),
	@('W', 'allow', 'echo x > {W}\a'),
	@('W', 'allow', 'Set-Content {W}\a x'),
	@('W', 'allow', '.\scripts\build-windows.ps1 2>&1 | Out-File .\log.txt'),
	@('W', 'allow', 'git add -A && git commit -m "ARXVR-1 feat: x"'),
	@('I', 'allow', 'Set-Content {I}\a x'),
	@('O', 'allow', 'Set-Content {O}\a x'),
	@('O', 'allow', 'git switch main; git pull --ff-only'),
	# --- discarding work in the owner's copy -------------------------------------------------------------
	@('O', 'owner-discard', 'git reset --hard'),
	@('O', 'owner-discard', 'git reset --hard origin/main'),
	@('O', 'owner-discard', 'git clean -fd'),
	@('O', 'owner-discard', 'git clean -fdx'),
	@('O', 'owner-discard', 'git clean --force -d'),
	@('O', 'owner-discard', 'git checkout -- .'),
	@('O', 'owner-discard', 'git checkout .'),
	@('O', 'owner-discard', 'git restore .'),
	@('O', 'owner-discard', 'git checkout -f main'),
	@('W', 'owner-discard', 'git -C {O} reset --hard'),
	@('W', 'owner-discard', 'cd {O}; git clean -fd'),
	@('O', 'allow', 'git status'),
	@('O', 'allow', 'git fetch --prune origin'),
	@('O', 'allow', 'git clean -n'),
	@('O', 'allow', 'git clean -nd'),
	@('O', 'allow', 'git checkout main'),
	@('O', 'allow', 'git restore --staged .'),
	@('O', 'allow', 'git reset --soft HEAD~1'),
	@('O', 'allow', 'git branch -D old-branch'),
	@('W', 'allow', 'git reset --hard origin/main'),
	@('W', 'allow', 'git clean -fd'),
	@('W', 'allow', 'git checkout -- .'),
	@('I', 'allow', 'git reset --hard'),
	# --- normal work: nothing here may be blocked --------------------------------------------------------
	@('W', 'allow', '.\scripts\build-windows.ps1'),
	@('W', 'allow', '.\scripts\run-windows.ps1 -- --skiplogo --loadslot 0'),
	@('W', 'allow', '.\scripts\dev\smoke-test.ps1 -Commands ''shot a'',''shot b'''),
	@('W', 'allow', 'cmake --build build --config Release 2>&1 | Select-Object -Last 20'),
	@('W', 'allow', 'git commit -m "docs: the hook denies git push origin main and rm -rf /"'),
	@('W', 'allow', "git commit -m @'`nfix: don't run `rm -rf C:\`nthen git push --force origin main`n'@"),
	@('W', 'allow', "git commit -m `"`$(cat <<'EOF'`nfix: the hook blocks`ngit push origin main`nEOF`n)`""),
	@('W', 'allow', "cat > {W}\msg.txt <<'EOF'`ngit push --force origin main`nrm -rf /`nEOF`ngit status"),
	@('W', 'allow', 'gh issue comment 185 --body-file {J}\tmp\c.md'),
	@('W', 'allow', 'gh issue view 185 --json title # git push origin main'),
	@('W', 'allow', 'git fetch origin && git merge origin/main'),
	@('W', 'allow', 'git branch --show-current'),
	@('W', 'allow', 'Get-ChildItem -Recurse | Select-String x'),
	@('W', 'allow', 'Start-Process notepad'),
	@('W', 'allow', 'echo $(date) > /dev/null'),
	@('E', 'allow', 'rm -rf {E}\x')
)
$cwds['E'] = $E

function Expand-Fixture([string]$Text) { $Text.Replace('{O}', $O).Replace('{W}', $W).Replace('{I}', $I).Replace('{E}', $E).Replace('{J}', $J) }

# The bash front door (safety-guard.sh): its decision for a payload, "fast" or "slow"
$gitBash = Join-Path (Split-Path (Split-Path (Get-Command git).Source)) 'bin\bash.exe'
if (-not (Test-Path $gitBash)) { $gitBash = 'C:\Program Files\Git\bin\bash.exe' }
$front = Join-Path $here 'safety-guard.sh'
function Get-Prefilter([string]$Json) {
	$env:SAFETY_GUARD_PREFILTER_ONLY = '1'
	try { ($Json | & $gitBash $front) -join '' } finally { Remove-Item Env:SAFETY_GUARD_PREFILTER_ONLY }
}

$rows = [Collections.Generic.List[object]]::new()
$failures = 0
foreach ($case in $cases) {
	$cmd = Expand-Fixture $case[2]
	$tool = 'Bash'
	$toolInput = @{ command = $cmd }
	if ($cmd -match '^(Edit|Write):(.+)$') { $tool = $Matches[1]; $toolInput = @{ file_path = $Matches[2] } }
	$payload = [pscustomobject]@{ cwd = $cwds[$case[0]]; tool_name = $tool; tool_input = [pscustomobject]$toolInput; session_id = 'test' }
	$deny = Test-ToolCall $payload
	$got = if ($deny) { $deny.Rule } else { 'allow' }
	$pre = Get-Prefilter ($payload | ConvertTo-Json -Compress -Depth 5)
	$ok = $got -eq $case[1] -and ($got -eq 'allow' -or $pre -eq 'slow')
	if (-not $ok) { $failures++ }
	$shown = ($case[2] -replace '\s+', ' ')
	if ($shown.Length -gt 70) { $shown = $shown.Substring(0, 67) + '...' }
	$rows.Add([pscustomobject]@{ Ok = if ($ok) { 'ok' } else { 'FAIL' }; Cwd = $case[0]; Want = $case[1]; Got = $got; Front = $pre; Command = $shown })
}

$rows | Format-Table Ok, Cwd, Want, Got, Front, Command -AutoSize -Wrap | Out-String -Width 220 | Write-Host
$blocked = @($rows | Where-Object Got -ne 'allow').Count
$fastAllowed = @($rows | Where-Object { $_.Got -eq 'allow' -and $_.Front -eq 'fast' }).Count
Write-Host ("{0} cases: {1} blocked, {2} allowed ({3} of them never reach PowerShell), {4} differ from the expectation" -f $rows.Count, $blocked, ($rows.Count - $blocked), $fastAllowed, $failures)
Write-Host "Front = the bash front door's verdict: every blocked case must be 'slow' (sent to the PowerShell rules)."

# --- The real hook, as .claude/settings.json runs it: bash safety-guard.sh ----------------------------------

function Invoke-Hook([string]$StdIn) {
	$psi = [Diagnostics.ProcessStartInfo]::new($gitBash)
	$psi.ArgumentList.Add($front)
	$psi.RedirectStandardInput = $true; $psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true
	$psi.UseShellExecute = $false
	$psi.Environment['LOCALAPPDATA'] = $env:LOCALAPPDATA
	$psi.Environment['CLAUDE_JOB_DIR'] = $J
	$sw = [Diagnostics.Stopwatch]::StartNew()
	$p = [Diagnostics.Process]::Start($psi)
	$p.StandardInput.Write($StdIn); $p.StandardInput.Close()
	$out = $p.StandardOutput.ReadToEnd(); [void]$p.StandardError.ReadToEnd()
	$p.WaitForExit()
	[pscustomobject]@{ Out = $out; Exit = $p.ExitCode; Ms = $sw.ElapsedMilliseconds }
}

function Assert-That([bool]$Condition, [string]$What) {
	Write-Host ("{0,-5} {1}" -f $(if ($Condition) { 'ok' } else { 'FAIL' }), $What)
	if (-not $Condition) { $script:failures++ }
}

Write-Host "`nThe hook process:"
$denyIn = @{ cwd = $W; tool_name = 'Bash'; session_id = 'proc-test'; tool_input = @{ command = 'git push --force origin main' } } | ConvertTo-Json -Compress
$r = Invoke-Hook $denyIn
$json = $r.Out | ConvertFrom-Json
Assert-That ($r.Exit -eq 0 -and $json.hookSpecificOutput.hookEventName -eq 'PreToolUse' -and $json.hookSpecificOutput.permissionDecision -eq 'deny') 'a dangerous call: exit 0 and a PreToolUse deny'
Write-Host ("      reason: " + $json.hookSpecificOutput.permissionDecisionReason)
$slowIn = @{ cwd = $W; tool_name = 'Bash'; tool_input = @{ command = 'git push -u origin ARXVR-1-feat-x' } } | ConvertTo-Json -Compress
$fastIn = @{ cwd = $W; tool_name = 'Bash'; tool_input = @{ command = 'cmake --build build --config Release' } } | ConvertTo-Json -Compress
$r = Invoke-Hook $slowIn
Assert-That ($r.Exit -eq 0 -and -not $r.Out) 'a branch push goes through the rules and passes: exit 0, no output'
$r = Invoke-Hook $fastIn
Assert-That ($r.Exit -eq 0 -and -not $r.Out) 'a build passes: exit 0, no output'
$r = Invoke-Hook '{ this is not json, git push origin main'
Assert-That ($r.Exit -eq 0 -and -not $r.Out) 'garbage input fails open: exit 0, no output'
$r = Invoke-Hook ''
Assert-That ($r.Exit -eq 0 -and -not $r.Out) 'empty input fails open: exit 0, no output'
$log = Get-Content -LiteralPath (Join-Path $env:LOCALAPPDATA 'ArxFatalisVR\safety-hook.log') -Raw
Assert-That ($log -match 'DENY rule=force-push session=proc-test') 'the denial is in the log'
Assert-That ($log -match 'ERROR \(failed open\)') 'the failed-open error is in the log'

$fastTimes = 1..$TimingRuns | ForEach-Object { (Invoke-Hook $fastIn).Ms }
$slowTimes = 1..([Math]::Max(3, [int]($TimingRuns / 2))) | ForEach-Object { (Invoke-Hook $slowIn).Ms }
$fastAvg = [int](($fastTimes | Measure-Object -Average).Average); $fastMax = ($fastTimes | Measure-Object -Maximum).Maximum
$slowAvg = [int](($slowTimes | Measure-Object -Average).Average); $slowMax = ($slowTimes | Measure-Object -Maximum).Maximum
Assert-That ($fastAvg -lt 300) "time per call, ordinary command (never reaches PowerShell): average $fastAvg ms, max $fastMax ms (limit 300 ms)"
Write-Host "      time per call, command that goes through the rules (PowerShell start included): average $slowAvg ms, max $slowMax ms"

$env:LOCALAPPDATA = $savedLocal; $env:CLAUDE_JOB_DIR = $savedJob
Remove-Item -Recurse -Force $root -ErrorAction SilentlyContinue
if ($failures) { Write-Host "`n$failures check(s) FAILED" -ForegroundColor Red; exit 1 }
Write-Host "`nall checks passed" -ForegroundColor Green
