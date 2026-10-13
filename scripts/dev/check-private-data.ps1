<#
.SYNOPSIS
	Fail when a tracked file of the project carries personal data or a game capture (#428).

.DESCRIPTION
	The repository is public. The project's own tracked files (docs, scripts, workflows, the coordinator rules, the
	root documents, the VR sources) must not hold:
	  - a personal home path (drive, Users, a name; or /Users/<name>, /home/<name>) - placeholders (<user>,
	    %USERPROFILE%, a Windows built-in profile name) are fine;
	  - a Steam userdata id (<Steam>\userdata\<digits>) or a 64-bit Steam id;
	  - an e-mail address other than an allowed one (example domains, GitHub noreply);
	  - an image file under docs/ (game captures live only in CI artifacts, owner's rule 2026-10-13).
	Third-party code (3rdparty, thirdparty, cmake, the engine's own sources) is not scanned: it carries the authors' mail.
	Files that must name such a value on purpose (the scrubber and its fixtures) are in $allowFiles.
	Exit code 0 when clean, 1 with one line per finding. Dot-sourced, it only defines the functions (the self-test,
	test-private-data.ps1, uses them).

.EXAMPLE
	.\scripts\dev\check-private-data.ps1
#>
[CmdletBinding()]
param(
	[string]$Root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
)

$ErrorActionPreference = 'Stop'

# Where the project's own text lives (git pathspecs relative to the root)
$scopes = @('docs', 'scripts', '.github', 'coordinator', 'src/vr', 'tests', '*.md')
# Files that hold such values on purpose: the scrubber's rules and the fixtures of the tests of the scrubber
$allowFiles = @(
	'scripts/dev/log-digest-lib.ps1'
	'scripts/dev/test-log-digest.ps1'
	'scripts/dev/test-private-data.ps1'
	'scripts/dev/check-private-data.ps1'
	'scripts/dev/hooks/test-safety-guard.ps1'
)
# Windows profile names that name no person
$genericUsers = 'Public', 'Default', 'Default User', 'All Users', 'user', 'users', 'name', 'username', 'you', 'me', 'owner', 'worker', 'runneradmin', 'runner', '…', '...'
# 76561197960265728 is the base of every 64-bit Steam id (account id 0), a constant of the id arithmetic, not an account
$steamBase = '76561197960265728'
$allowedMailDomains = 'example.com', 'example.org', 'example.net', 'users.noreply.github.com', 'noreply.github.com', 'anthropic.com'
$imageExt = '.png', '.jpg', '.jpeg', '.gif', '.bmp', '.webp', '.tga', '.dds', '.tif', '.tiff'
$binaryExt = '.dll', '.exe', '.zip', '.ogg', '.wav', '.ttf', '.ico', '.bin'

$rxHome = [regex]'(?i)(?:\b[A-Za-z]:[\\/]+Users[\\/]+|(?<![\w.~])/(?:Users|home)/)([^\\/"''`\s\[\]:;|=,+*?<>]+)'
$rxUserdata = [regex]'(?i)\buserdata[\\/]+(\d+)'
$rxSteamId = [regex]'\b7656119\d{10}\b'
$rxMail = [regex]'(?i)\b[a-z0-9._%+-]+@([a-z0-9-]+(?:\.[a-z0-9-]+)*\.[a-z]{2,})\b'

# The findings in one text: "kind: value"
function Find-PrivateInText([string]$Text) {
	foreach ($m in $rxHome.Matches($Text)) {
		$u = $m.Groups[1].Value
		if ($u -notmatch '^[<%$]' -and $u -notin $genericUsers) { "home path: $($m.Value)" }
	}
	foreach ($m in $rxUserdata.Matches($Text)) { "Steam userdata id: $($m.Value)" }
	foreach ($m in $rxSteamId.Matches($Text)) { if ($m.Value -ne $steamBase) { "Steam id: $($m.Value)" } }
	foreach ($m in $rxMail.Matches($Text)) {
		if ($m.Groups[1].Value.ToLowerInvariant() -notin $allowedMailDomains) { "e-mail: $($m.Value)" }
	}
}

# The findings in a list of tracked paths (text read through $ReadText); an image under docs/ is a finding by its name
function Find-PrivateInFiles([string[]]$Paths, [scriptblock]$ReadText) {
	foreach ($p in $Paths) {
		if ($p -in $allowFiles) { continue }
		$ext = [IO.Path]::GetExtension($p).ToLowerInvariant()
		if ($p -like 'docs/*' -and $ext -in $imageExt) { "${p}: image under docs/ (game captures live in CI artifacts only)"; continue }
		if ($ext -in $imageExt -or $ext -in $binaryExt) { continue }
		$text = & $ReadText $p
		if ($null -eq $text) { continue }
		foreach ($f in @(Find-PrivateInText $text)) { "${p}: $f" }
	}
}

if ($MyInvocation.InvocationName -eq '.') { return }

Push-Location $Root
try {
	$tracked = @(git ls-files -- $scopes) | Where-Object { $_ }
	$findings = @(Find-PrivateInFiles $tracked { param($p) if (Test-Path -LiteralPath $p -PathType Leaf) { Get-Content -LiteralPath $p -Raw -ErrorAction SilentlyContinue } })
} finally { Pop-Location }

if ($findings.Count) {
	Write-Host "check-private-data: $($findings.Count) finding(s) in tracked files:" -ForegroundColor Red
	$findings | ForEach-Object { Write-Host "  $_" -ForegroundColor Red }
	exit 1
}
Write-Host "check-private-data: clean ($($tracked.Count) tracked files scanned)" -ForegroundColor Green
exit 0
