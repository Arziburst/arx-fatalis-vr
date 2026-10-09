<#
.SYNOPSIS
	The owner's test station: build and launch the working copy of a pull request, with its test question.

.DESCRIPTION
	Run from the owner's working copy (any working copy of the repository works). The script
	  1. reads the PR (branch, head, body) with gh;
	  2. finds the worktree of the PR's branch under the worktree root, or creates one with
	     new-worktree.ps1 (tracking origin/<branch>);
	  3. fast-forwards a clean worktree to the PR's head (a worktree with uncommitted changes is left
	     as it is, with a note);
	  4. builds it when the build stamp (built-from.txt, written by build-windows.ps1) does not match
	     HEAD, or -Rebuild is given;
	  5. prints the PR's "How to test" section and the build id (the commit to quote in the verdict);
	  6. launches the game from that worktree: run-windows.ps1 -- --skiplogo --loadslot 0 by default,
	     i.e. with the owner's own saves (shared user directory) and in VR when SteamVR is running;
	     arguments after -- replace the default ones. Launch control applies (D-014): with SteamVR off
	     the game starts flat unless -StartSteamVR, the owner's switch, starts SteamVR first; the game
	     lock makes it wait for any other game on this PC (one at a time).
	The verdict goes to /accept-task <pr> or /reject-task <pr> <what is wrong>, with the build id.

.PARAMETER Pr
	The pull request number.

.PARAMETER Rebuild
	Build even when the stamp says the build is current.

.PARAMETER NoLaunch
	Prepare (worktree, build) and print the test question, but do not start the game.

.PARAMETER StartSteamVR
	Start SteamVR when it is not running, then launch in VR (the owner's switch - it turns the headset on).

.PARAMETER Isolated
	Launch with the worktree's isolated user directory instead of the owner's saves
	(run-windows.ps1 -Isolated); add -Saves to copy saves into it.

.PARAMETER Saves
	With -Isolated: save slots to copy into the isolated user directory (e.g. save0000).

.PARAMETER Config
	Which build: Release (default), RelWithDebInfo or Debug.

.EXAMPLE
	.\scripts\dev\test-pr.ps1 131
.EXAMPLE
	.\scripts\dev\test-pr.ps1 131 -- --skiplogo --loadslot 2
.EXAMPLE
	.\scripts\dev\test-pr.ps1 131 -NoLaunch
#>
[CmdletBinding(PositionalBinding = $false)]
param(
	[Parameter(Mandatory, Position = 0)] [int]$Pr,
	[switch]$Rebuild,
	[switch]$NoLaunch,
	[switch]$StartSteamVR,
	[switch]$Isolated,
	[string[]]$Saves,
	[ValidateSet('Release', 'RelWithDebInfo', 'Debug')] [string]$Config = 'Release',
	[Parameter(ValueFromRemainingArguments)] [string[]]$ArxArgs
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8
. (Join-Path $PSScriptRoot 'board.ps1')
. (Join-Path $PSScriptRoot '..\build-paths.ps1')

function Short([string]$Sha) { if ($Sha -and $Sha.Length -gt 9) { $Sha.Substring(0, 9) } else { $Sha } }

# --- The pull request --------------------------------------------------------------------------------
# ($pull, not $pr: PowerShell variable names ignore case, and $Pr is the [int] parameter)

$pull = gh pr view $Pr -R $Repo --json number,title,headRefName,headRefOid,body,state,labels,url | ConvertFrom-Json
if ($LASTEXITCODE -or -not $pull) { throw "could not read PR #$Pr" }
$branch = $pull.headRefName
$labels = @($pull.labels.name)

Write-Host "==> PR #$($pull.number) $($pull.title)" -ForegroundColor Cyan
Write-Host "    $($pull.url)  [$($pull.state.ToLower())$(if ($labels) { ', ' + ($labels -join ', ') })]  branch $branch @ $(Short $pull.headRefOid)"
if ($pull.state -ne 'OPEN') { Write-Host "    note: this PR is $($pull.state.ToLower())" -ForegroundColor Yellow }

# --- The worktree of its branch -------------------------------------------------------------------

$wt = & (Join-Path $PSScriptRoot 'new-worktree.ps1') -Branch $branch -PassThru | Select-Object -Last 1
if (-not $wt -or -not (Test-Path $wt)) { throw "no worktree for $branch" }
Write-Host "    worktree: $wt"

git -C $wt fetch -q origin 2>$null
$dirty = @(git -C $wt status --porcelain)
$head = (git -C $wt rev-parse HEAD).Trim()
if ($head -ne $pull.headRefOid) {
	if ($dirty) {
		Write-Host "    worktree has uncommitted changes - left at $(Short $head), the PR head is $(Short $pull.headRefOid)" -ForegroundColor Yellow
	} else {
		git -C $wt merge --ff-only $pull.headRefOid 2>&1 | Out-Null
		if ($LASTEXITCODE) {
			Write-Host "    could not fast-forward $(Short $head) to the PR head $(Short $pull.headRefOid) (unpushed commits?) - testing $(Short $head)" -ForegroundColor Yellow
		} else {
			$head = $pull.headRefOid
			Write-Host "    fast-forwarded to the PR head $(Short $head)"
		}
	}
}

# --- Build when the stamp does not match HEAD ----------------------------------------------------

$buildDir = Get-DefaultBuildDir $wt $Config
$exe = Join-Path $buildDir 'arx.exe'
$stampFile = Join-Path $buildDir 'built-from.txt'
$stamp = if (Test-Path $stampFile) { (Get-Content $stampFile -Raw).Trim() } else { '' }
$reason = if ($Rebuild) { '-Rebuild' }
	elseif (-not (Test-Path $exe)) { 'no arx.exe yet' }
	elseif ($dirty) { 'uncommitted changes in the worktree' }
	elseif ($stamp -ne $head) { "built from '$(Short $stamp)', HEAD is $(Short $head)" }
if ($reason) {
	Write-Host "    building: $reason" -ForegroundColor Cyan
	& (Join-Path $wt 'scripts\build-windows.ps1') -Config $Config
} else {
	Write-Host "    build is current: $exe (built from $(Short $head))"
}

# --- The one question ---------------------------------------------------------------------------------

$how = [regex]::Match([string]$pull.body, '(?ms)^#{2,}\s*How to test\s*\r?\n(.*?)(?=^#{2,}\s|\z)').Groups[1].Value.Trim()
Write-Host ''
Write-Host '==> How to test (from the PR)' -ForegroundColor Cyan
if ($how) { $how -split '\r?\n' | ForEach-Object { Write-Host "    $_" } } else { Write-Host '    (the PR has no "How to test" section)' -ForegroundColor Yellow }
Write-Host ''
Write-Host "==> Build id: $(Short $head)$(if ($dirty) { ' + uncommitted changes' }) ($branch) - quote it in the verdict" -ForegroundColor Cyan

# --- Launch ------------------------------------------------------------------------------------------

if ($NoLaunch) { return }
if (-not $ArxArgs) { $ArxArgs = @('--skiplogo', '--loadslot', '0') }
$runArgs = @{ Config = $Config; ArxArgs = $ArxArgs }
if ($StartSteamVR) { $runArgs.StartSteamVR = $true }
elseif ($ArxArgs -notcontains '--no-vr' -and -not (Get-Process vrserver -ErrorAction SilentlyContinue)) {
	Write-Host '    SteamVR is not running: the game will start flat. For the headset start SteamVR first, or pass -StartSteamVR.' -ForegroundColor Yellow
}
if ($Isolated) { $runArgs.Isolated = $true }
if ($Saves) { $runArgs.Isolated = $true; $runArgs.Saves = $Saves }
& (Join-Path $wt 'scripts\run-windows.ps1') @runArgs
exit $LASTEXITCODE
