# Clean up after a merged task (Windows; macOS / Linux: cleanup-worker.sh - the same checks and codes):
# stop and remove the worker session, remove its worktree and branch - only if the worktree exists, is
# clean, every commit is on the remote and a merged PR has exactly the pushed head (a reused branch name
# with newer commits does not count) (CORE section 8). Otherwise report and change nothing.
#   powershell -File scripts\cleanup-worker.ps1 -Worktree <path> [-Session <id>] [-DryRun]
# Exit codes: 0 cleaned (or would be, with -DryRun); 1 usage error or a failed removal;
# 3 a check failed, nothing changed.
param([string]$Worktree, [string]$Session, [switch]$DryRun)
function StopHere([string]$m) { [Console]::Error.WriteLine("$m - left as is"); exit 3 }
function Run([string]$exe, [string[]]$a) {
  if ($DryRun) { "[dry-run] $exe $($a -join ' ')"; $global:LASTEXITCODE = 0 } else { & $exe @a }
}
if (-not $Worktree) { [Console]::Error.WriteLine('usage: -Worktree <path> [-Session <id>] [-DryRun]'); exit 1 }

# The main repository is found from the worktree itself, so this runs from any folder.
if (-not (Test-Path -LiteralPath $Worktree -PathType Container)) { StopHere "NO WORKTREE: $Worktree does not exist (check the path)" }
$common = git -C $Worktree rev-parse --path-format=absolute --git-common-dir 2>$null
if ($LASTEXITCODE -ne 0) { StopHere "NOT A WORKTREE: $Worktree is not a git working copy" }
$gitdir = git -C $Worktree rev-parse --path-format=absolute --git-dir
if ("$gitdir" -eq "$common") { StopHere "MAIN CHECKOUT: $Worktree is the repository itself, not a worker worktree" }
$root = Split-Path -Parent $common
if (git -C $Worktree status --porcelain) { StopHere "NOT CLEAN: $Worktree has uncommitted changes" }
$branch = (git -C $Worktree rev-parse --abbrev-ref HEAD)
git -C $Worktree rev-parse -q --verify '@{u}' *> $null
if ($LASTEXITCODE -ne 0) { StopHere "NO UPSTREAM: $branch was never pushed" }
git -C $Worktree fetch -q
if ($LASTEXITCODE -ne 0) { StopHere "FETCH FAILED: cannot confirm $branch is on the remote" }
if (git -C $Worktree log --oneline '@{u}..') { StopHere "UNPUSHED: $branch has commits not on its remote" }
$up = "$(git -C $Worktree rev-parse '@{u}')".Trim()
if (-not (Get-Command gh -ErrorAction SilentlyContinue)) { StopHere "NO gh: cannot verify the PR for $branch is merged" }
Push-Location -LiteralPath $Worktree
$heads = @(gh pr list --head $branch --state merged --json headRefOid --jq '.[].headRefOid' 2>$null)
$ghExit = $LASTEXITCODE
Pop-Location
if ($ghExit -ne 0) { StopHere "UNKNOWN: could not check the PR for $branch" }
$heads = @($heads | ForEach-Object { "$_".Trim() } | Where-Object { $_ })
if (-not $heads.Count) { StopHere "NOT MERGED: no merged PR for $branch" }
if ($heads -notcontains $up) { StopHere "NOT MERGED: no merged PR for $branch has its pushed head $($up.Substring(0, [Math]::Min(7, $up.Length))) (branch name reused?)" }

if ($Session) {
  Run claude @('stop', $Session)
  Run claude @('rm', $Session)
}
Run git @('-C', $root, 'worktree', 'remove', $Worktree)
if ($LASTEXITCODE -ne 0) { [Console]::Error.WriteLine("REMOVE FAILED: git worktree remove $Worktree"); exit 1 }
Run git @('-C', $root, 'branch', '-d', $branch)
if ($LASTEXITCODE -ne 0) { "branch $branch kept (not fully merged locally)" }
"cleaned: $(if ($Session) { $Session } else { 'no session' }) $Worktree"
exit 0
