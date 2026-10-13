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
	The headset verdict (OK, or what is wrong) goes to the coordinator, with the build id.

.PARAMETER Pr
	The pull request number.

.PARAMETER Rebuild
	Build even when the stamp says the build is current.

.PARAMETER NoLaunch
	Prepare (worktree, build) and print the test question, but do not start the game.

.PARAMETER DryRun
	Like a launch, but run-windows.ps1 only prints the arx.exe command line (it has sound: no --mute, #345). The worktree, the fast-forward and the build still happen first; the game, SteamVR and the saves are not touched.

.PARAMETER StartSteamVR
	Start SteamVR when it is not running, then launch in VR (the owner's switch - it turns the headset on).

.PARAMETER StopSteamVR
	After the game has exited, quit SteamVR the way its own "Exit SteamVR" does (stop-steamvr.ps1), so the headset
	powers down. The owner's switch, like -StartSteamVR; workers never pass it (D-014).

.PARAMETER Branch
	The PR's branch, when even git cannot reach GitHub: the local branch (or origin/<branch>) is tested as it is,
	without asking GitHub anything. Normally not needed: when the GitHub API does not answer (rate limit, no
	network), the script resolves the PR with `git ls-remote` and the local worktree by itself and says so (#171).

.PARAMETER Isolated
	Launch with the worktree's isolated user directory instead of the owner's saves
	(run-windows.ps1 -Isolated); add -Saves to copy saves into it.

.PARAMETER Playground
	Launch the test playground (run-windows.ps1 -Playground, #321) instead of the default save.

.PARAMETER Player
	Launch as a player does (run-windows.ps1 -Player, #394): no --dev-launch and no default arguments - the
	publisher logos, the main menu, and the intro cinematic after New game. Without it the launch is a development
	launch (logos and cinematics skipped) of the default save.

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
.EXAMPLE
	.\scripts\dev\test-pr.ps1 131 -Player
	# the first run as a player sees it: logos, main menu, New game, the intro cinematic
#>
[CmdletBinding(PositionalBinding = $false)]
param(
	[Parameter(Mandatory, Position = 0)] [int]$Pr,
	[switch]$Rebuild,
	[switch]$NoLaunch,
	[switch]$DryRun,
	[switch]$StartSteamVR,
	[switch]$StopSteamVR,
	[string]$Branch,
	[switch]$Isolated,
	[switch]$Playground,
	[switch]$Player,
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

# The headset test must not depend on the GitHub API (#171: the shared quota ran out and the owner could not
# test a PR whose worktree and build were on disk). When the API does not answer, the PR is resolved with git
# alone: `git ls-remote` reads refs/pull/<n>/head over the git protocol (no API quota), and the branch is the
# one whose tip is that commit. -Branch skips even that (no network at all: the local branch is tested as it is).
function Resolve-PullWithoutGitHub([int]$Number, [string]$Why) {
	Write-Host "    GitHub did not answer ($Why) - resolving PR #$Number with git and the local worktree" -ForegroundColor Yellow
	$sha = $null
	$lines = @(git ls-remote origin "refs/pull/$Number/head" 2>$null)
	if ($lines -and "$($lines[0])" -match '^([0-9a-f]{40})\s') { $sha = $Matches[1] }
	if (-not $sha) { throw "cannot resolve PR #${Number}: GitHub is unreachable ($Why) and so is the git remote. Pass the PR's branch: -Branch ARXVR-<issue>-<type>-<name> (the local branch is tested as it is)." }
	git fetch -q origin 2>$null | Out-Null
	$names = @(git for-each-ref --points-at $sha --format '%(refname:short)' refs/remotes/origin 2>$null |
		ForEach-Object { $_ -replace '^origin/', '' } | Where-Object { $_ -match '^ARXVR-\d+-' })
	if ($names.Count -ne 1) { throw "cannot resolve PR #$Number offline: head $(Short $sha) is the tip of $($names.Count) ARXVR branches on origin ($($names -join ', ')) - pass -Branch" }
	[pscustomobject]@{ number = $Number; title = '(GitHub unavailable)'; url = "https://github.com/$Repo/pull/$Number"; body = ''; state = 'UNKNOWN'
		headRefName = $names[0]; headRefOid = $sha; labels = @(); offline = $true }
}

if ($Branch) {
	$tip = (git rev-parse --verify -q "origin/$Branch" 2>$null)
	if (-not $tip) { $tip = (git rev-parse --verify -q $Branch 2>$null) }
	if ($LASTEXITCODE -ne 0 -or -not $tip) { throw "no branch '$Branch' here (neither origin/$Branch nor a local one)" }
	Write-Host '    -Branch given: not asking GitHub' -ForegroundColor Yellow
	$pull = [pscustomobject]@{ number = $Pr; title = '(GitHub not asked)'; url = "https://github.com/$Repo/pull/$Pr"; body = ''; state = 'UNKNOWN'
		headRefName = $Branch; headRefOid = "$tip".Trim(); labels = @(); offline = $true }
} else {
	try {
		$pull = Get-GhPull $Pr # REST (github-lib.ps1, #186)
	} catch {
		$pull = Resolve-PullWithoutGitHub $Pr (($_.Exception.Message -split "`n" | Select-Object -First 1))
	}
}
$branch = $pull.headRefName
$labels = @($pull.labels.name)

Write-Host "==> PR #$($pull.number) $($pull.title)" -ForegroundColor Cyan
Write-Host "    $($pull.url)  [$($pull.state.ToLower())$(if ($labels) { ', ' + ($labels -join ', ') })]  branch $branch @ $(Short $pull.headRefOid)"
if ($pull.state -notin 'OPEN', 'UNKNOWN') { Write-Host "    note: this PR is $($pull.state.ToLower())" -ForegroundColor Yellow }

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
if ($how) { $how -split '\r?\n' | ForEach-Object { Write-Host "    $_" } }
elseif ($pull.offline) { Write-Host "    (GitHub unavailable - read the PR's How to test at $($pull.url))" -ForegroundColor Yellow }
else { Write-Host '    (the PR has no "How to test" section)' -ForegroundColor Yellow }
Write-Host ''
Write-Host "==> Build id: $(Short $head)$(if ($dirty) { ' + uncommitted changes' }) ($branch) - quote it in the verdict" -ForegroundColor Cyan

# --- Launch ------------------------------------------------------------------------------------------

if ($NoLaunch) { return }
if (-not $ArxArgs -and -not $Playground -and -not $Player) { $ArxArgs = @('--skiplogo', '--loadslot', '0') }
if (-not $ArxArgs) { $ArxArgs = @() }
$runArgs = @{ Config = $Config; ArxArgs = $ArxArgs; Sound = $true } # the owner's launches have sound (#345)
if ($Player) {
	# A branch from before #394 has no player launch: its run-windows.ps1 would refuse the switch less clearly
	if (-not (Select-String -LiteralPath (Join-Path $wt 'scripts\run-windows.ps1') -Pattern '\[switch\]\$Player' -Quiet)) {
		throw "-Player: the PR's run-windows.ps1 has no player launch (#394) - merge main into its branch first"
	}
	$runArgs.Player = $true
}
if ($DryRun) { $runArgs.DryRun = $true }
if ($StartSteamVR) { $runArgs.StartSteamVR = $true }
elseif ($ArxArgs -notcontains '--no-vr' -and -not (Get-Process vrserver -ErrorAction SilentlyContinue)) {
	Write-Host '    SteamVR is not running: the game will start flat. For the headset start SteamVR first, or pass -StartSteamVR.' -ForegroundColor Yellow
}
if ($Playground) { $runArgs.Playground = $true }
if ($Isolated) { $runArgs.Isolated = $true }
if ($Saves) { $runArgs.Isolated = $true; $runArgs.Saves = $Saves }
& (Join-Path $wt 'scripts\run-windows.ps1') @runArgs
$gameExit = $LASTEXITCODE

# The owner's switch: SteamVR quits the way its own "Exit SteamVR" does, so the headset powers down (#171)
if ($StopSteamVR) {
	. (Join-Path $wt 'scripts\launch-lib.ps1')
	Stop-SteamVr
}
exit $gameExit
