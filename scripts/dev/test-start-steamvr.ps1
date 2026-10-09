<#
.SYNOPSIS
	Self-test of how the test station starts SteamVR (#171) - without starting it.

.DESCRIPTION
	Runs Start-SteamVr / Resolve-ArxVrMode of scripts/launch-lib.ps1 against stubbed Start-Process, Get-Process,
	Get-Date and Start-Sleep (a fake clock, no waiting, no process, no headset), so the logic can be proven by a
	worker, who never starts SteamVR (D-014). Checks:
	  - SteamVR already running: nothing is started
	  - the Steam URL brings it up: only the URL is opened, then the settle wait
	  - the Steam URL does nothing: SteamVR's own vrstartup.exe is started as the fallback
	  - nothing brings it up: an error names the timeout and which of vrserver / vrcompositor runs
	  - vrserver up but no vrcompositor is not "up"
	  - without -StartSteamVR a launch stays flat and starts nothing (the D-014 rule is unchanged)
	  - with -StartSteamVR SteamVR is started explicitly and the launch is VR
	Exit code 0 when everything passed, 1 otherwise.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\launch-lib.ps1')

# --- The fake machine ----------------------------------------------------------------------------------------------
$fake = @{ Now = [DateTime]'2026-01-01T00:00:00'; Started = [Collections.Generic.List[string]]::new()
	Server = $false; Compositor = $false; Steam = ''; Launcher = ''; UpAfterUrl = $null; UpAfterLauncher = $null; UrlThrows = $false; Pending = $null }

function Get-Date { $fake.Now }
function Start-Sleep { param([int]$Seconds, [int]$Milliseconds) $fake.Now = $fake.Now.AddSeconds($Seconds).AddMilliseconds($Milliseconds); Update-Fake }
function Find-SteamVrStartup { if ($fake.Launcher) { $fake.Launcher } else { $null } }
function Write-Host { param([Parameter(ValueFromRemainingArguments)]$Rest, $ForegroundColor) }
function Get-Process {
	[CmdletBinding()] param([string[]]$Name)
	foreach ($n in $Name) {
		if ($n -eq 'vrserver' -and $fake.Server) { [pscustomobject]@{ Name = 'vrserver' } }
		elseif ($n -eq 'vrcompositor' -and $fake.Compositor) { [pscustomobject]@{ Name = 'vrcompositor' } }
		elseif ($n -in $fake.Extra) { [pscustomobject]@{ Name = $n } }
	}
}
function Stop-Process { [CmdletBinding()] param([Parameter(ValueFromRemainingArguments)]$Rest) $fake.Killed++ }
function Find-SteamVrBinary([string]$Name) { if ($fake.Monitor) { $fake.Monitor } else { $null } }
function Start-Process {
	[CmdletBinding()] param([string]$FilePath, [string[]]$ArgumentList, [switch]$PassThru)
	$fake.Started.Add($(if ($ArgumentList) { "$FilePath $ArgumentList" } else { $FilePath }))
	if ($FilePath -like '*vrmonitor*') {
		if ($null -ne $fake.QuitAfter) { $fake.QuitAt = $fake.Now.AddSeconds($fake.QuitAfter) }
	} elseif ($FilePath -like 'steam://*') {
		if ($fake.UrlThrows) { throw 'no handler for steam://' }
		if ($null -ne $fake.UpAfterUrl) { $fake.Pending = @{ At = $fake.Now.AddSeconds($fake.UpAfterUrl); Compositor = $true } }
	} elseif ($null -ne $fake.UpAfterLauncher) { $fake.Pending = @{ At = $fake.Now.AddSeconds($fake.UpAfterLauncher); Compositor = $true } }
}
# the start-up of the fake SteamVR: vrserver first, vrcompositor 3 s later (the real order)
function Update-Fake {
	if ($fake.QuitAt -and $fake.Now -ge $fake.QuitAt) { $fake.Server = $false; $fake.Compositor = $false; $fake.Extra = @(); $fake.QuitAt = $null; $fake.Pending = $null }
	if ($fake.Pending) {
		if ($fake.Now -ge $fake.Pending.At) { $fake.Server = $true }
		if ($fake.Now -ge $fake.Pending.At.AddSeconds(3)) { $fake.Compositor = $fake.Pending.Compositor }
	}
}
function Reset-Fake { $fake.Extra = @(); $fake.Monitor = ''; $fake.QuitAfter = $null; $fake.QuitAt = $null; $fake.Killed = 0; $fake.Started.Clear(); $fake.Server = $false; $fake.Compositor = $false; $fake.Launcher = ''; $fake.UpAfterUrl = $null; $fake.UpAfterLauncher = $null; $fake.UrlThrows = $false; $fake.Pending = $null }

$failures = 0
function Check([string]$Name, [bool]$Ok, [string]$Detail = '') {
	if ($Ok) { Microsoft.PowerShell.Utility\Write-Host "ok    $Name" -ForegroundColor Green }
	else { Microsoft.PowerShell.Utility\Write-Host "FAIL  $Name $Detail" -ForegroundColor Red; $script:failures++ }
}
function Try-Start { try { Start-SteamVr -TimeoutSec 60 -SettleSec 1; $null } catch { $_.Exception.Message } }

# 1. already running
Reset-Fake; $fake.Server = $true
$err = Try-Start
Check 'already running: nothing started' ($fake.Started.Count -eq 0 -and -not $err) "$($fake.Started -join ',') $err"

# 2. the Steam URL works
Reset-Fake; $fake.UpAfterUrl = 8; $fake.Launcher = 'C:\vr\vrstartup.exe'
$err = Try-Start
Check 'Steam URL works: only the URL is opened' ($fake.Started.Count -eq 1 -and $fake.Started[0] -eq 'steam://rungameid/250820' -and -not $err) "$($fake.Started -join ',') $err"

# 3. the URL does nothing, the launcher works
Reset-Fake; $fake.UpAfterLauncher = 5; $fake.Launcher = 'C:\vr\vrstartup.exe'
$err = Try-Start
Check 'URL does nothing: vrstartup.exe is the fallback' ($fake.Started.Count -eq 2 -and $fake.Started[1] -eq 'C:\vr\vrstartup.exe' -and -not $err) "$($fake.Started -join ',') $err"

# 4. the URL cannot be opened at all
Reset-Fake; $fake.UrlThrows = $true; $fake.UpAfterLauncher = 5; $fake.Launcher = 'C:\vr\vrstartup.exe'
$err = Try-Start
Check 'URL throws: vrstartup.exe is the fallback' ($fake.Started.Count -eq 2 -and -not $err) "$($fake.Started -join ',') $err"

# 5. nothing works: a clear error with the state
Reset-Fake; $fake.Launcher = 'C:\vr\vrstartup.exe'
$err = Try-Start
Check 'nothing works: timeout error names the state' ($err -match 'did not come up within 60 s' -and $err -match 'vrserver\.exe does not run' -and $err -match 'vrcompositor\.exe does not run' -and $err -match 'by hand') $err

# 6. no launcher installed: still waits and fails cleanly
Reset-Fake
$err = Try-Start
Check 'no launcher: still a clear error' ($err -match 'did not come up' -and $fake.Started.Count -eq 1) "$($fake.Started -join ',') $err"

# 7. vrserver without vrcompositor is not up
Reset-Fake; $fake.Server = $true; $fake.Compositor = $false
# (Test-SteamVrRunning is true, so Start-SteamVr returns at once - that is the contract; the wait helper is the strict one)
Check 'Wait-SteamVrUp needs both processes' (-not (Wait-SteamVrUp 5)) ''

# 8. D-014: no switch, no start
Reset-Fake
$mode = Resolve-ArxVrMode -ArxArgs @('--skiplogo')
Check 'no -StartSteamVR: flat, nothing started' (-not $mode.Vr -and $mode.Args -contains '--no-vr' -and $fake.Started.Count -eq 0) "$($fake.Started -join ',')"

# 9. -StartSteamVR: started explicitly, launch in VR
Reset-Fake; $fake.UpAfterUrl = 8
$mode = Resolve-ArxVrMode -ArxArgs @('--skiplogo') -StartSteamVR
Check '-StartSteamVR: SteamVR started, launch is VR' ($mode.Vr -and $mode.Args -notcontains '--no-vr' -and $fake.Started.Count -eq 1) "$($fake.Started -join ',')"

# --- Stopping (Stop-SteamVr): the quit request, a patient wait, no killing --------------------------------------------
function Try-Stop { try { Stop-SteamVr -TimeoutSec 60 -SettleSec 1; $null } catch { $_.Exception.Message } }
function Start-FakeSteamVr { $fake.Server = $true; $fake.Compositor = $true; $fake.Extra = @('vrmonitor', 'vrdashboard', 'vrservice') }

# 10. SteamVR exits by itself after the quit request
Reset-Fake; Start-FakeSteamVr; $fake.Monitor = 'C:\vr\vrmonitor.exe'; $fake.QuitAfter = 14
$err = Try-Stop
Check 'stop: vrmonitor://quit sent once, SteamVR exits, nothing killed' ($fake.Started.Count -eq 1 -and $fake.Started[0] -eq 'C:\vr\vrmonitor.exe vrmonitor://quit' -and $fake.Killed -eq 0 -and -not $err -and -not (Test-SteamVrRunning)) "$($fake.Started -join ',') killed=$($fake.Killed) $err"

# 11. not running: nothing to do
Reset-Fake
$err = Try-Stop
Check 'stop: SteamVR not running - nothing started' ($fake.Started.Count -eq 0 -and -not $err) "$($fake.Started -join ',') $err"

# 12. no vrmonitor.exe found: the registered URL is opened instead
Reset-Fake; Start-FakeSteamVr; $fake.QuitAfter = 5
$err = Try-Stop
Check 'stop: no vrmonitor.exe - the vrmonitor:// URL is opened' ($fake.Started.Count -eq 1 -and $fake.Started[0] -eq 'vrmonitor://quit' -and -not $err) "$($fake.Started -join ',') $err"

# 13. SteamVR ignores the request: a clear error, still nothing killed
Reset-Fake; Start-FakeSteamVr; $fake.Monitor = 'C:\vr\vrmonitor.exe'
$err = Try-Stop
Check 'stop: timeout error lists the processes, nothing killed' ($err -match 'did not exit within 60 s' -and $err -match 'vrserver' -and $err -match 'vrmonitor' -and $err -match 'Nothing was killed' -and $fake.Killed -eq 0) "killed=$($fake.Killed) $err"

# 14. the quit request cannot be sent
Reset-Fake; Start-FakeSteamVr; $fake.UrlThrows = $true; $fake.Monitor = ''
function Start-Process { [CmdletBinding()] param([string]$FilePath, [string[]]$ArgumentList) throw 'no handler' }
$err = Try-Stop
Check 'stop: unsendable request is an error pointing to the menu' ($err -match 'could not send the quit request' -and $err -match 'Exit SteamVR') $err

if ($failures) { Microsoft.PowerShell.Utility\Write-Host "$failures check(s) failed" -ForegroundColor Red; exit 1 }
Microsoft.PowerShell.Utility\Write-Host 'all checks passed' -ForegroundColor Green
exit 0
