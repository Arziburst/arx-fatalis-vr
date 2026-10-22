# The debugger for crash dumps (#610): cdb.exe from Microsoft's WinDbg package, pinned and cached in the tools directory
# like the voice notes' tools (scripts/dev/transcribe-lib.ps1). Dot-sourced by scripts/dev/crash-report.ps1, which installs
# it only when asked (-InstallCdb: a 1.2 GB download, once); self-test: scripts/dev/test-crash-report.ps1.

# The WinDbg release, pinned: its version, the bundle's SHA-256 and URL (the versioned path of aka.ms/windbg/download).
# docs/vr/TESTING.md "Third-party tools downloaded by scripts" lists the same.
$script:CdbPin = @{
	Version = '1.2610.1001.0'
	Sha = '4662ae17bc0dc77db987b89cccea32c30058baa06268f861d09e0ab4e4f45c20'
	Url = 'https://windbg.download.prss.microsoft.com/dbazure/prod/1-2610-1001-0/windbg.msixbundle'
}

# The cache: %LOCALAPPDATA%\ArxFatalisVR\tools (windbg-<version>\amd64\cdb.exe; symbols\ for Microsoft's public symbols)
function Get-ArxDebuggerToolsDir {
	Join-Path $env:LOCALAPPDATA 'ArxFatalisVR\tools'
}

# The installed cdb.exe of the pin in $ToolsDir, or $null (never downloads)
function Get-ArxToolsCdb([string]$ToolsDir = (Get-ArxDebuggerToolsDir), $Pin = $script:CdbPin) {
	$cdb = Join-Path $ToolsDir "windbg-$($Pin.Version)\amd64\cdb.exe"
	if (Test-Path -LiteralPath $cdb -PathType Leaf) { return $cdb }
	$null
}

# A file signed by Microsoft (Authenticode, valid): the unpacked debugger is checked before it is used the first time
function Test-ArxMicrosoftSigned([string]$Path) {
	$signature = Get-AuthenticodeSignature -LiteralPath $Path
	$signature.Status -eq 'Valid' -and "$($signature.SignerCertificate.Subject)" -match '(?i)\bO=Microsoft Corporation\b'
}

# Install the pinned WinDbg's cdb.exe into $ToolsDir\windbg-<version>\: the bundle fetched (a file:/// URL copied), its
# SHA-256 checked, the amd64\ folder of its x64 package unpacked into a new, empty folder, cdb.exe's Microsoft signature
# checked, and only then given its name; the bundle is deleted afterwards. A mismatch or an unsigned cdb.exe deletes
# everything and throws. Returns cdb.exe's path (at once when it is installed already).
function Install-ArxCdb([string]$ToolsDir = (Get-ArxDebuggerToolsDir), $Pin = $script:CdbPin) {
	$installed = Get-ArxToolsCdb $ToolsDir $Pin
	if ($installed) { return $installed }
	New-Item -ItemType Directory -Force $ToolsDir | Out-Null
	$folder = Join-Path $ToolsDir "windbg-$($Pin.Version)"
	$bundle = Join-Path $ToolsDir "windbg-$($Pin.Version).msixbundle.part"
	$package = Join-Path $ToolsDir "windbg-$($Pin.Version)-x64.msix.part"
	$unpack = "$folder.part"
	Add-Type -AssemblyName System.IO.Compression.FileSystem
	try {
		$uri = [Uri]$Pin.Url
		if ($uri.IsFile) {
			Copy-Item -LiteralPath $uri.LocalPath -Destination $bundle -Force
		} else {
			Write-Host "Downloading WinDbg $($Pin.Version) (1.2 GB, once; cdb.exe is kept in the tools directory)"
			$progress = $ProgressPreference
			$ProgressPreference = 'SilentlyContinue'
			try { Invoke-WebRequest -Uri $Pin.Url -OutFile $bundle -UseBasicParsing } finally { $ProgressPreference = $progress }
		}
		$sha = (Get-FileHash -LiteralPath $bundle -Algorithm SHA256).Hash.ToLowerInvariant()
		if ($sha -ne $Pin.Sha) { throw "SHA-256 mismatch: the WinDbg bundle is $sha, the pin is $($Pin.Sha) - deleted, nothing installed" }

		# The bundle holds one package per architecture; the x64 one holds amd64\ (cdb.exe, dbgeng.dll, dbghelp.dll, ...)
		$zip = [IO.Compression.ZipFile]::OpenRead($bundle)
		try {
			$entry = @($zip.Entries | Where-Object { $_.FullName -match '(?i)^[^/]*x64\.msix$' }) | Select-Object -First 1
			if (-not $entry) { throw 'The WinDbg bundle has no x64 package - nothing installed' }
			[IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $package, $true)
		} finally { $zip.Dispose() }
		Remove-Item -Recurse -Force -LiteralPath $unpack -ErrorAction SilentlyContinue
		$zip = [IO.Compression.ZipFile]::OpenRead($package)
		try {
			foreach ($file in @($zip.Entries | Where-Object { $_.FullName -like 'amd64/*' -and $_.Name })) {
				# An entry name that climbs out (..) or is absolute would be written outside the folder: refused
				$name = $file.FullName -replace '/', '\'
				if ($name -match '(^|\\)\.\.(\\|$)' -or [IO.Path]::IsPathRooted($name) -or $name -match ':') {
					throw "The WinDbg package has an entry outside its folder ($($file.FullName)) - nothing installed"
				}
				$target = Join-Path $unpack $name
				New-Item -ItemType Directory -Force (Split-Path -Parent $target) | Out-Null
				[IO.Compression.ZipFileExtensions]::ExtractToFile($file, $target, $true)
			}
		} finally { $zip.Dispose() }
		$cdb = Join-Path $unpack 'amd64\cdb.exe'
		if (-not (Test-Path -LiteralPath $cdb)) { throw 'The WinDbg package has no amd64\cdb.exe - nothing installed' }
		if (-not (Test-ArxMicrosoftSigned $cdb)) { throw 'cdb.exe of the WinDbg package is not signed by Microsoft - deleted, nothing installed' }
		Remove-Item -Recurse -Force -LiteralPath $folder -ErrorAction SilentlyContinue
		Move-Item -LiteralPath $unpack -Destination $folder
	} finally {
		Remove-Item -Force -LiteralPath $bundle, $package -ErrorAction SilentlyContinue
		Remove-Item -Recurse -Force -LiteralPath $unpack -ErrorAction SilentlyContinue
	}
	Join-Path $folder 'amd64\cdb.exe'
}

# cdb's symbol path: the given folders (the archived arx.pdb), then Microsoft's public symbols (ntdll's internal frames)
# cached under tools\symbols
function Get-ArxCdbSymbolPath([string[]]$Folders, [string]$ToolsDir = (Get-ArxDebuggerToolsDir)) {
	$parts = @($Folders | Where-Object { $_ })
	$parts += "srv*$(Join-Path $ToolsDir 'symbols')*https://msdl.microsoft.com/download/symbols"
	$parts -join ';'
}
