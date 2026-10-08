# Shared by build-windows.ps1 and run-windows.ps1. Dot-source it.
#
# Every working copy (the main clone, each git worktree) gets its own build directory: CMake refuses
# to reuse a build directory that was configured from a different source tree.

function Get-DefaultBuildDir([string]$SourceDir, [string]$Config) {
	$full = [IO.Path]::GetFullPath($SourceDir).TrimEnd('\').ToLowerInvariant()
	$sha = [Security.Cryptography.SHA1]::Create()
	$hash = -join ($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($full))[0..3] | ForEach-Object { $_.ToString('x2') })
	$name = "$(Split-Path -Leaf $full)-$hash"
	Join-Path $env:LOCALAPPDATA "ArxFatalisVR\build\$name\$Config"
}
