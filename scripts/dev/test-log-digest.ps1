<#
.SYNOPSIS
	Self-test of the run digest and the automatic bug filing (#131): fixture logs, a fake GitHub, no game, no network.

.DESCRIPTION
	Runs the digest (scripts/dev/log-digest-lib.ps1) on generated logs and crash reports in a scratch folder
	($env:ARXVR_DIGEST_ROOT) against an in-memory GitHub, and checks the rules of the task:
	  - an injected error produces exactly one issue (labels bug + auto-reported, signature marker in the body)
	  - digesting the same run again, or another run with the same error (other numbers), adds no duplicate
	  - the "seen again" comment is throttled to once a day per issue
	  - a lost signatures.json does not duplicate: the issue is found again by its marker
	  - a warning is filed only when it repeats in a second run; an allow-listed line never
	  - a crash is filed with its stack; paths of the user profile are redacted
	  - without a network the report stays queued and is sent exactly once later
	  - nothing is uploaded when the repository is not private
	Exit code 0 when everything passed, 1 otherwise.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'log-digest-lib.ps1')

$root = Join-Path ([IO.Path]::GetTempPath()) "arxvr-digest-test-$PID"
$env:ARXVR_DIGEST_ROOT = Join-Path $root 'app'
$userDir = Join-Path $root 'user'
New-Item -ItemType Directory -Force $userDir, $env:ARXVR_DIGEST_ROOT | Out-Null

# --- The fake GitHub -----------------------------------------------------------------------------------------------
$fake = @{ Issues = [Collections.Generic.List[object]]::new(); Calls = [Collections.Generic.List[string]]::new(); Offline = $false; Private = $true; Next = 100 }
$script:ArxGitHubInvoker = {
	param($Method, $Path, $Body)
	$fake.Calls.Add("$Method $Path")
	if ($fake.Offline) { throw 'gh api: network is unreachable' }
	if ($Path -match 'graphql') { throw 'GraphQL must never be used' }
	if ($Method -eq 'GET' -and $Path -eq "repos/$script:ArxDigestRepo") { return [pscustomobject]@{ private = $fake.Private } }
	if ($Method -eq 'GET' -and $Path -match '/issues\?') { return $fake.Issues.ToArray() }
	if ($Method -eq 'POST' -and $Path -match '/issues$') {
		$issue = [pscustomobject]@{ number = $fake.Next++; state = 'open'; title = $Body.title; body = $Body.body; labels = $Body.labels; comments = [Collections.Generic.List[string]]::new() }
		$fake.Issues.Add($issue)
		return $issue
	}
	if ($Method -eq 'GET' -and $Path -match '/issues/(\d+)$') { return ($fake.Issues | Where-Object number -eq ([int]$Matches[1])) }
	if ($Method -eq 'PATCH' -and $Path -match '/issues/(\d+)$') {
		$i = $fake.Issues | Where-Object number -eq ([int]$Matches[1])
		$i.state = $Body.state
		return $i
	}
	if ($Method -eq 'POST' -and $Path -match '/issues/(\d+)/comments$') {
		($fake.Issues | Where-Object number -eq ([int]$Matches[1])).comments.Add($Body.body)
		return [pscustomobject]@{ id = 1 }
	}
	throw "fake GitHub: unexpected $Method $Path"
}

$failures = 0
function Check([bool]$Ok, [string]$What) {
	if ($Ok) { Write-Host "  [ok]   $What" -ForegroundColor Green } else { Write-Host "  [FAIL] $What" -ForegroundColor Red; $script:failures++ }
}
function Creates { @($fake.Calls | Where-Object { $_ -match '^POST .*/issues$' }).Count }
function Comments { @($fake.Calls | Where-Object { $_ -match '^POST .*/comments$' }).Count }
function Reset-Calls { $fake.Calls.Clear() }

# --- Fixture logs --------------------------------------------------------------------------------------------------
$script:runNo = 0
function New-FixtureRun([string[]]$Lines, [switch]$NoClean) {
	$script:runNo++
	$start = (Get-Date).AddSeconds(-20)
	$pidNo = 1000 + $script:runNo
	$head = @(
		'[I] Startup:164           Starting Arx Fatalis VR 0.1-dev + abc1234',
		'[I] Startup:111           Build: abc1234def5678abc1234def5678abc1234def56 on ARXVR-999-feat-fixture, Release, MSVC 19.44, x86_64',
		'[I] Startup:141           Executable: "C:\Users\tester\AppData\Local\x\arx.exe", linked 2026-10-09 14:02:55, built from abc1234def5678abc1234def5678abc1234def56 dirty',
		"[I] Startup:163           Run: $($start.ToString('yyyy-MM-dd HH:mm:ss')) local ($($start.ToUniversalTime().ToString('HH:mm:ss')) UTC), pid $pidNo, command line: --windowed --no-vr --loadlevel 10",
		'[I] Startup:208           Log: "C:\Users\tester\arx.log", previous runs kept as arx.1.log .. arx.9.log',
		'[I] VRSystem:707          Starting in VR',
		'[I] LoadLevel:511         Done loading level'
	)
	$body = @(1..40 | ForEach-Object { "[I] PakReader:574         Loaded PAK ""C:\Users\tester\data$_.pak""" }) + $Lines
	if (-not $NoClean) { $body += '[I] ArxGame:997           Clean shutdown' }
	[IO.File]::WriteAllLines((Join-Path $userDir 'arx.log'), ($head + $body), [Text.UTF8Encoding]::new($false))
	$start
}
function Digest($Exit = 0, [switch]$NoReport) {
	Invoke-ArxRunDigest -UserDir $userDir -Source 'test-log-digest.ps1' -ExitCode $Exit -CrashWaitSec 0 -NoReport:$NoReport
}
function Pending { @(Get-ChildItem (Get-ArxPendingDir) -Filter '*.json' -ErrorAction SilentlyContinue).Count }
function Store { Use-ArxStore { param($s) $copy = [ordered]@{}; foreach ($k in $s.Keys) { $copy[$k] = $s[$k] }; $copy } }

Write-Host '1. a clean run' -ForegroundColor Cyan
$null = New-FixtureRun @()
$info = Digest
Check ($null -ne $info) 'the digest ran'
Check ((Test-Path (Join-Path $info.Dir 'digest.md')) -and (Test-Path (Join-Path $info.Dir 'digest.json')) -and (Test-Path (Join-Path $info.Dir 'arx.log'))) 'digest.md, digest.json and a copy of the log are in the run folder'
Check ($info.CleanShutdown -and $info.Problems.Count -eq 0 -and $info.Queued -eq 0) 'no problems, nothing queued'
Check ($info.Run.Banner.Commit -like 'abc1234*' -and $info.Run.Banner.Branch -eq 'ARXVR-999-feat-fixture') 'the build and branch come from the banner'
$digestText = Get-Content (Join-Path $info.Dir 'digest.md') -Raw
Check ($digestText -match 'Loaded PAK' -and $digestText -match '40 x') 'the common family is one line: 40 x Loaded PAK'
Check ((Invoke-ArxReportFlush).Created.Count -eq 0 -and $fake.Calls.Count -eq 0) 'an empty queue makes no GitHub call at all'

Write-Host '2. an injected error: exactly one issue' -ForegroundColor Cyan
$null = New-FixtureRun @('[E] Entity:259           Entity human_base_0033 not found at "C:\Users\tester\x\level10.dlf" (code 0x80004005)')
$info = Digest
Check ($info.Problems.Count -eq 1 -and $info.Problems[0].IsNew -and $info.Queued -eq 1) 'a new error signature is queued'
$hash = $info.Problems[0].Hash
$r = Invoke-ArxReportFlush
Check ($r.Created.Count -eq 1 -and (Creates) -eq 1) 'one issue created'
$issue = $fake.Issues[0]
Check (($issue.labels -join ',') -eq 'bug,auto-reported') 'labels bug + auto-reported'
Check ($issue.body.Contains("arxvr-auto-report: sig=$hash")) 'the signature marker is in the body'
Check ($issue.title -like '`[E`] Entity: Entity human_base_N not found*') "the title comes from the signature: $($issue.title)"
Check (-not ($issue.body -match 'tester') -or -not ($issue.body -match '\\Users\\tester')) 'the user profile name is redacted in the body'
Check ($issue.body -match 'ARXVR-999-feat-fixture' -and $issue.body -match 'Log excerpt') 'the body has the branch and a log excerpt'
Check ((Pending) -eq 0) 'the queue is empty after the flush'
Check (-not ($fake.Calls -match 'graphql')) 'no GraphQL call'

Write-Host '3. no duplicates' -ForegroundColor Cyan
Reset-Calls
$info = Digest
Check ($info.Queued -eq 0) 'digesting the same run again queues nothing'
$null = New-FixtureRun @('[E] Entity:261           Entity human_base_0099 not found at "D:\other\level11.dlf" (code 0x80004006)')
$info = Digest
Check ($info.Problems[0].Hash -eq $hash -and -not $info.Problems[0].IsNew) 'another run with the same error (other numbers, other line, other path) is the same signature'
Check ($info.Queued -eq 0 -and $info.Problems[0].Action -like 'seen again*') "seen again within a day is throttled: $($info.Problems[0].Action)"
$r = Invoke-ArxReportFlush
Check ((Creates) -eq 0 -and (Comments) -eq 0) 'no second issue and no comment'
Check ($fake.Issues.Count -eq 1) 'one issue in total'

Write-Host '4. the daily "seen again" comment' -ForegroundColor Cyan
Use-ArxStore { param($s) $s[$hash].lastCommentAt = (Get-ArxNow) - 25 * 3600 } | Out-Null
$null = New-FixtureRun @('[E] Entity:259           Entity human_base_0005 not found at "D:\a\b.dlf" (code 0x1)')
$info = Digest
Check ($info.Queued -eq 1 -and $info.Problems[0].Action -eq 'comment') 'after a day a comment is queued'
Reset-Calls
$r = Invoke-ArxReportFlush
Check ((Creates) -eq 0 -and (Comments) -eq 1 -and $r.Commented.Count -eq 1) 'one comment, no new issue'
Check ($fake.Issues[0].comments[0] -like 'Seen again*') 'the comment says "Seen again"'
$null = New-FixtureRun @('[E] Entity:259           Entity human_base_0006 not found at "D:\a\b.dlf" (code 0x1)')
Check ((Digest).Queued -eq 0) 'the next run the same day queues nothing'

Write-Host '4b. a closed issue whose problem came back' -ForegroundColor Cyan
$fake.Issues[0].state = 'closed'
Use-ArxStore { param($s) $s[$hash].lastCommentAt = (Get-ArxNow) - 25 * 3600 } | Out-Null
$null = New-FixtureRun @('[E] Entity:259           Entity human_base_0008 not found at "D:\a\b.dlf" (code 0x1)')
$info = Digest
Reset-Calls
$r = Invoke-ArxReportFlush
Check ($fake.Issues[0].state -eq 'open' -and $r.Reopened.Count -eq 1 -and (Creates) -eq 0) 'the closed issue is reopened, no new one'
Check ($fake.Issues[0].comments[$fake.Issues[0].comments.Count - 1] -like 'Reopened:*') 'the comment says why'

Write-Host '5. a lost store does not duplicate' -ForegroundColor Cyan
Remove-Item (Get-ArxSignatureStorePath) -Force
$null = New-FixtureRun @('[E] Entity:259           Entity human_base_0007 not found at "D:\a\b.dlf" (code 0x1)')
$info = Digest
Check ($info.Problems[0].IsNew -and $info.Queued -eq 1) 'with no store the signature looks new'
Reset-Calls
$r = Invoke-ArxReportFlush
Check ((Creates) -eq 0 -and $fake.Issues.Count -eq 1) 'the existing issue is found by its marker: no duplicate'
Check ((Store)[$hash].issue -eq $fake.Issues[0].number) 'the store learned the issue number again'

Write-Host '6. warnings: only new and repeated; the allow-list' -ForegroundColor Cyan
Reset-Calls
$null = New-FixtureRun @('[W] VRSystem:500         Controller profile 7 not found, using the default', '[W] ArxGame:364          Could not read config files cfg.ini and cfg_default.ini, using defaults')
$info = Digest
$w = $info.Problems | Where-Object { $_.Message -like 'Controller*' }
$a = $info.Problems | Where-Object { $_.Message -like 'Could not read*' }
Check ($w.IsNew -and $w.Action -like 'new warning*' -and $info.Queued -eq 0) 'a new warning is not filed at once'
Check ($a.Allowed -and $a.Action -eq 'allow-listed') 'the allow-listed line is shown but never filed'
$null = New-FixtureRun @('[W] VRSystem:500         Controller profile 9 not found, using the default', '[W] ArxGame:364          Could not read config files cfg.ini and cfg_default.ini, using defaults')
$info = Digest
Check ($info.Queued -eq 1) 'it is filed when it repeats in a second run'
$r = Invoke-ArxReportFlush
Check ($r.Created.Count -eq 1 -and $fake.Issues.Count -eq 2 -and $fake.Issues[1].title -like '`[W`]*') 'one more issue, for the warning'

Write-Host '7. a crash' -ForegroundColor Cyan
Reset-Calls
$null = New-FixtureRun @() -NoClean
$crashDir = Join-Path $userDir ('crashes\' + (Get-Date).ToUniversalTime().ToString('yyyy-MM-dd-HH-mm-ss') + '-00')
New-Item -ItemType Directory -Force $crashDir | Out-Null
@'
Illegal storage access

Exception code: ACCESS_VIOLATION


 Instruction address: 0x7ff773860206
 Memory accessed: 0x2ca7acf7cb0

Callstack:
 arx.exe!0x7ff773860206  Entity.cpp:259
 arx.exe!0x7ff7738605d5  Entity.cpp:352
 arx.exe!0x7ff77385fc0a  Entity.cpp:184
 arx.exe!0x7ff7739726f1  LoadLevel.cpp:544
 KERNEL32.DLL!BaseThreadInitThunk()

Process information:
- path: C:\Users\tester\AppData\Local\x\arx.exe
'@ | Set-Content (Join-Path $crashDir 'crash.txt')
$info = Digest 1
Check ($info.Crashes.Count -eq 1 -and -not $info.CleanShutdown) 'the crash report and the missing clean shutdown are found'
Check ($info.Queued -eq 1 -and $info.Crashes[0].Action -eq 'file') 'a crash is filed at once'
$r = Invoke-ArxReportFlush
$crashIssue = $fake.Issues[$fake.Issues.Count - 1]
Check ($r.Created.Count -eq 1 -and $crashIssue.title -like 'Crash: Illegal storage access at Entity.cpp:259') "title: $($crashIssue.title)"
Check ($crashIssue.body -match 'LoadLevel\.cpp:544' -and $crashIssue.body -notmatch '0x7ff773860206') 'the stack is in the body, without addresses'
$null = New-FixtureRun @() -NoClean
$info = Digest 1
Check ($info.Crashes.Count -eq 1 -and -not $info.Crashes[0].IsNew -and $info.Queued -eq 0) 'the same crash again: known, throttled'

Write-Host '8. no network: queued, then sent once' -ForegroundColor Cyan
Reset-Calls
$null = New-FixtureRun @('[E] Audio:77             OpenAL device 3 lost (error 0xA004)')
$info = Digest
$fake.Offline = $true
$r = Invoke-ArxReportFlush
Check ($r.Created.Count -eq 0 -and $r.Failed.Count -ge 1 -and (Pending) -eq 1) 'offline: the report stays queued'
$null = New-FixtureRun @('[E] Audio:77             OpenAL device 4 lost (error 0xA005)')
$info = Digest
Check ($info.Queued -eq 0 -and (Pending) -eq 1) 'another run while offline queues nothing new'
$fake.Offline = $false
$before = $fake.Issues.Count
$r = Invoke-ArxReportFlush
Check ($r.Created.Count -eq 1 -and $fake.Issues.Count -eq $before + 1 -and (Pending) -eq 0) 'online again: sent exactly once'

Write-Host '9. reporting off, and a repository that is not private' -ForegroundColor Cyan
$null = New-FixtureRun @('[E] Input:12             Device 5 vanished')
$info = Digest -NoReport
Check ($info.Queued -eq 0 -and $info.Problems[0].Action -eq 'reporting off') '-NoReport queues nothing'
$null = New-FixtureRun @('[E] Input:12             Device 6 vanished', '[E] Input:13             Second problem 1')
$info = Digest
Check ($info.Queued -eq 2) 'two new signatures, two reports'
$fake.Private = $false
Reset-Calls
$before = $fake.Issues.Count
$r = Invoke-ArxReportFlush
Check ($fake.Issues.Count -eq $before -and $r.Failed.Count -ge 1 -and (Pending) -eq 2) 'a repository that is not private receives nothing'
$fake.Private = $true

Write-Host '10. a flood of new problems in one run' -ForegroundColor Cyan
Get-ChildItem (Get-ArxPendingDir) -Filter '*.json' | Remove-Item -Force
$null = New-FixtureRun (@('alpha', 'beta', 'gamma', 'delta', 'epsilon', 'zeta', 'eta') | ForEach-Object { "[E] Flood:1              Problem $_ happened" })
$info = Digest
Check ($info.Problems.Count -eq 7 -and $info.Queued -eq $script:ArxMaxNewIssuesPerRun) "7 new error signatures, at most $script:ArxMaxNewIssuesPerRun are queued"
Check (@($info.Problems | Where-Object { $_.Action -like 'new, not filed*' }).Count -eq 2) 'the other two are in the digest, marked as not filed'

Write-Host '11. the command line modes (-Id, -Find, -Condense, -Print) on a fixture log' -ForegroundColor Cyan
# The script runs in a child process, as a person or CI runs it; the scratch store is inherited from the environment
$pwsh = (Get-Process -Id $PID).Path
$digestScript = Join-Path $PSScriptRoot 'log-digest.ps1'
function Invoke-DigestCli([string[]]$CliArgs) {
	$out = & $pwsh -NoProfile -File $digestScript @CliArgs 2>&1 | ForEach-Object { "$_" }
	[pscustomobject]@{ Exit = $LASTEXITCODE; Text = ($out -join "`n") }
}
function Count-Matching($Text, [string]$Pattern) { @($Text -split "`n" | Where-Object { $_ -match $Pattern }).Count }
$null = New-FixtureRun @(
	'[E] Entity:259           Entity human_base_0033 not found at "C:\Users\tester\x\level10.dlf" (code 0x80004005)',
	'[W] Audio:40             Sample 17 is silent',
	'[I] Game:1               Rare line that appears once')
$logFile = Join-Path $userDir 'arx.log'
$cli = Invoke-DigestCli @('-Log', $logFile, '-Id')
Check ($cli.Exit -eq 0 -and $cli.Text -match 'Starting Arx Fatalis VR 0\.1-dev' -and $cli.Text -match 'ARXVR-999-feat-fixture' -and $cli.Text -match 'command line: --windowed --no-vr --loadlevel 10') '-Id prints the build, the branch and the command line'
Check ($cli.Text -match '\d+ lines in this run') '-Id says how many lines the run has'
$cli = Invoke-DigestCli @('-Log', $logFile, '-Find', 'not found|silent', '-Last', '5')
Check ($cli.Exit -eq 0 -and $cli.Text -match '2 match\(es\) for' -and $cli.Text -match 'Entity human_base_0033 not found' -and $cli.Text -match 'Sample 17 is silent' -and $cli.Text -notmatch 'Loaded PAK') '-Find prints the matching lines and only those'
$cli = Invoke-DigestCli @('-Log', $logFile, '-Find', 'PAK', '-Last', '3')
Check ($cli.Exit -eq 0 -and $cli.Text -match '40 match\(es\) for' -and (Count-Matching $cli.Text 'Loaded PAK') -eq 3) '-Find -Last 3 prints the last 3 of 40 matches'
$cli = Invoke-DigestCli @('-Log', $logFile, '-Find', 'no such line anywhere')
Check ($cli.Exit -eq 0 -and $cli.Text -match '0 match\(es\)') '-Find with no match says 0 matches'
$cli = Invoke-DigestCli @('-Log', $logFile, '-Condense')
Check ($cli.Exit -eq 0 -and $cli.Text -match 'Rare line that appears once' -and $cli.Text -match 'Sample 17 is silent' -and $cli.Text -match '--- common families ---') '-Condense prints the rare lines in full, then the common families'
Check ($cli.Text -match '\s40 x .*Loaded PAK' -and (Count-Matching $cli.Text 'Loaded PAK') -eq 1) '-Condense folds the 40 PAK lines into one family line'
$cli = Invoke-DigestCli @('-UserDir', $userDir, '-NoReport', '-Print', '-Quiet')
Check ($cli.Exit -eq 0 -and $cli.Text -match 'Entity' -and $cli.Text -match 'ARXVR-999-feat-fixture') '-Print prints digest.md with the error and the branch'
$cli = Invoke-DigestCli @('-Log', (Join-Path $userDir 'no-such.log'), '-Id')
Check ($cli.Exit -ne 0 -and $cli.Text -match 'no log at') 'a missing log is an error, not an empty answer'

Write-Host '8. the signatures mutex is open to every Windows account (#272)' -ForegroundColor Cyan
# The CI runner service is another account than the owner and the workers: a mutex created with the default
# security is denied to it. While the store is held, the mutex must carry an Everyone rule.
$acl = Use-ArxStore { param($s)
	$m = [Threading.MutexAcl]::OpenExisting('Global\ArxFatalisVR-signatures', [Security.AccessControl.MutexRights]::ReadPermissions)
	try { [System.Threading.ThreadingAclExtensions]::GetAccessControl($m).GetAccessRules($true, $false, [Security.Principal.SecurityIdentifier]) | ForEach-Object { "$($_.IdentityReference.Value) $($_.AccessControlType)" } } finally { $m.Dispose() }
}
Check (@($acl) -contains 'S-1-1-0 Allow') 'the mutex grants Everyone (S-1-1-0) access'
Remove-Item $root -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item Env:ARXVR_DIGEST_ROOT
if ($failures) { Write-Host "log digest self-test: $failures check(s) FAILED" -ForegroundColor Red; exit 1 }
Write-Host 'log digest self-test: all checks passed' -ForegroundColor Green
exit 0
