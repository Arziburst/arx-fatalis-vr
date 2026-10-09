# Claude Code PreToolUse hook (configured in .claude/settings.json): the safety hook of D-021 / #185.
#
# Workers run with permission prompts off (D-011), but hooks still run. This one denies - with a reason that
# names the safe alternative - the few commands that can destroy shared state:
#   push-main        git push to main / master (any remote spelling, refspec or delete form)
#   force-push       any force-push (--force, --force-with-lease, -f, +refspec, --mirror)
#   delete-outside   a recursive delete (rm -rf, Remove-Item -Recurse, rd /s) of a path outside the session's
#                    own worktree and its job tmp dir
#   owner-write      a write into the owner's copy (the repository's main working copy) from a session whose
#                    working directory is another worktree
#   owner-discard    git reset --hard / git clean -fd / git checkout -- . in the owner's copy
# Everything else passes: branch pushes, gh pr merge, builds, game launches, writes inside the own worktree.
#
# It never stops a task: it denies only the one command, fails open on its own errors (logged), and appends
# every denial to %LOCALAPPDATA%\ArxFatalisVR\safety-hook.log (the coordinator reads it). It does not start
# git: the repository is found by reading .git files. What it cannot see (variables it cannot resolve, a path
# built by a pipeline) it lets through - it is a guard rail against mistakes, not a sandbox.
#
# Test: scripts/dev/hooks/test-safety-guard.ps1

$ErrorActionPreference = 'Stop'
$script:LogFile = Join-Path $env:LOCALAPPDATA 'ArxFatalisVR\safety-hook.log'
$script:Unresolved = $false

function Write-GuardLog([string]$Line) {
	try {
		$dir = [IO.Path]::GetDirectoryName($script:LogFile)
		if (-not [IO.Directory]::Exists($dir)) { [void][IO.Directory]::CreateDirectory($dir) }
		if ([IO.File]::Exists($script:LogFile) -and ([IO.FileInfo]$script:LogFile).Length -gt 512KB) {
			$tail = [IO.File]::ReadAllLines($script:LogFile) | Select-Object -Last 500
			[IO.File]::WriteAllLines($script:LogFile, $tail)
		}
		[IO.File]::AppendAllText($script:LogFile, ('{0} {1}{2}' -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'), $Line, [Environment]::NewLine))
	} catch { }
}

# --- Paths ---------------------------------------------------------------------------------------------

function Get-FullPath([string]$Path) {
	$full = [IO.Path]::GetFullPath($Path)
	if ($full.Length -gt 3) { $full = $full.TrimEnd('\') }
	$full
}

function Test-Inside([string]$Path, [string]$Root) {
	if (-not $Path -or -not $Root) { return $false }
	$Root = $Root.TrimEnd('\')
	$Path.Equals($Root, 'OrdinalIgnoreCase') -or $Path.StartsWith($Root + '\', 'OrdinalIgnoreCase')
}

# A path as typed in a command -> an absolute path, or $null when it cannot be known (an unset variable, a
# subexpression). Wildcards cut the path back to the directory before them.
function Resolve-GuardPath([string]$Raw, [string]$Base) {
	try {
		$p = $Raw.Trim()
		if ($p -eq '') { return $null }
		if ($p -match '^/([A-Za-z])(/|$)') { $p = $Matches[1].ToUpper() + ':' + $p.Substring(2) }   # Git Bash /c/Users/...
		if ($p -match '^~($|[\\/])') { $p = $env:USERPROFILE + $p.Substring(1) }
		$script:Unresolved = $false
		$p = [regex]::Replace($p, '\$env:(\w+)|%(\w+)%|\$(\w+)', {
			param($m)
			$name = ($m.Groups[1].Value + $m.Groups[2].Value + $m.Groups[3].Value)
			$value = [Environment]::GetEnvironmentVariable($name)
			if (-not $value -and $name -eq 'HOME') { $value = $env:USERPROFILE }
			if (-not $value) { $script:Unresolved = $true; return $m.Value }
			$value
		})
		if ($script:Unresolved -or $p -match '[$`]|@\(') { return $null }
		$star = $p.IndexOfAny([char[]]'*?[')
		if ($star -ge 0) {
			$p = $p.Substring(0, $star)
			$p = $p.Substring(0, $p.LastIndexOfAny([char[]]'\/') + 1)
			if ($p -eq '') { $p = '.' }
		}
		if (-not [IO.Path]::IsPathRooted($p) -or $p -match '^[A-Za-z]:[^\\/]') {
			if (-not $Base) { return $null }
			$p = Join-Path $Base $p
		}
		Get-FullPath $p
	} catch { $null }
}

# --- Repository layout, read from .git files (no git process) -------------------------------------------

# Top = the working copy containing $Dir; GitDir = its git directory; Owner = the repository's main
# working copy (the owner's copy), whichever worktree $Dir is in.
function Get-RepoInfo([string]$Dir) {
	$d = $Dir
	while ($d) {
		$dotgit = [IO.Path]::Combine($d, '.git')
		if ([IO.Directory]::Exists($dotgit)) {
			return [pscustomobject]@{ Top = $d; GitDir = $dotgit; Owner = $d }
		}
		if ([IO.File]::Exists($dotgit)) {
			$line = ([IO.File]::ReadAllLines($dotgit) | Select-Object -First 1)
			if ($line -notmatch '^gitdir:\s*(.+)$') { return $null }
			$gitDir = Get-FullPath ([IO.Path]::Combine($d, $Matches[1].Trim().Replace('/', '\')))
			$common = $gitDir
			$commonFile = [IO.Path]::Combine($gitDir, 'commondir')
			if ([IO.File]::Exists($commonFile)) {
				$common = Get-FullPath ([IO.Path]::Combine($gitDir, ([IO.File]::ReadAllText($commonFile).Trim().Replace('/', '\'))))
			}
			$owner = $null
			if ([IO.Path]::GetFileName($common) -eq '.git') { $owner = [IO.Path]::GetDirectoryName($common) }
			return [pscustomobject]@{ Top = $d; GitDir = $gitDir; Owner = $owner }
		}
		$parent = [IO.Path]::GetDirectoryName($d)
		if (-not $parent -or $parent -eq $d) { break }
		$d = $parent
	}
	$null
}

function Get-CurrentBranch([string]$Dir) {
	$info = Get-RepoInfo $Dir
	if (-not $info) { return $null }
	$head = [IO.Path]::Combine($info.GitDir, 'HEAD')
	if (-not [IO.File]::Exists($head)) { return $null }
	if (([IO.File]::ReadAllText($head)) -match '^ref:\s*refs/heads/(\S+)') { return $Matches[1] }
	$null
}

# Does $Path belong to the owner's copy (and not to a worktree that happens to live inside its folder)?
function Test-OwnerPath([string]$Path, $Ctx) {
	if (-not $Ctx.Owner -or -not $Path -or -not (Test-Inside $Path $Ctx.Owner)) { return $false }
	$dir = $Path
	if (-not [IO.Directory]::Exists($dir)) { $dir = [IO.Path]::GetDirectoryName($Path) }
	while ($dir -and -not [IO.Directory]::Exists($dir)) { $dir = [IO.Path]::GetDirectoryName($dir) }
	$info = Get-RepoInfo $dir
	[bool]($info -and $info.Top.Equals($Ctx.Owner, 'OrdinalIgnoreCase'))
}

function Test-InOwnerCopy([string]$Dir, $Ctx) {
	if (-not $Ctx.Owner -or -not $Dir) { return $false }
	$info = Get-RepoInfo $Dir
	[bool]($info -and $info.Top.Equals($Ctx.Owner, 'OrdinalIgnoreCase'))
}

# --- Command text -> segments of tokens ------------------------------------------------------------------

# Splits a shell command line into segments (at ; | || && newline and around ( ) { }) of tokens {T, Q}
# (Q = was quoted). Quoted text, here-strings and heredoc bodies stay inside one token / are dropped, so a
# commit message that mentions "git push --force" is not a command.
function Split-Segments([string]$Text) {
	$Text = [regex]::Replace($Text, '\$\{([^}]*)\}', '$$$1')
	$segments = [Collections.Generic.List[object]]::new()
	$cur = [Collections.Generic.List[object]]::new()
	$sb = [Text.StringBuilder]::new()
	$inTok = $false; $quoted = $false
	$heredocs = [Collections.Generic.List[string]]::new()
	$endTok = {
		if ($inTok) { $cur.Add([pscustomobject]@{ T = $sb.ToString(); Q = $quoted }) }
		[void]$sb.Clear(); $inTok = $false; $quoted = $false
	}
	$endSeg = {
		. $endTok
		if ($cur.Count) { $segments.Add($cur.ToArray()); $cur.Clear() }
	}
	$n = $Text.Length
	$i = 0
	while ($i -lt $n) {
		$c = $Text[$i]
		if ($c -eq "'" -or $c -eq '"') {
			# here-string @' ... '@
			if ($inTok -and $sb.ToString() -eq '@' -and $i + 1 -lt $n -and ($Text[$i + 1] -eq "`n" -or $Text[$i + 1] -eq "`r")) {
				$m = [regex]::Match($Text.Substring($i), '\r?\n' + [regex]::Escape([string]$c) + '@')
				$i = if ($m.Success) { $i + $m.Index + $m.Length } else { $n }
				[void]$sb.Clear(); $quoted = $true
				continue
			}
			$j = $i + 1
			while ($j -lt $n) {
				if ($Text[$j] -eq $c) { break }
				if ($c -eq '"' -and $Text[$j] -eq '`' -and $j + 1 -lt $n) { $j++ }
				$j++
			}
			[void]$sb.Append($Text.Substring($i + 1, [Math]::Min($j, $n) - $i - 1))
			$inTok = $true; $quoted = $true
			$i = $j + 1
			continue
		}
		if ($c -eq '<' -and $i + 1 -lt $n -and $Text[$i + 1] -eq '<' -and ($i + 2 -ge $n -or $Text[$i + 2] -ne '<')) {
			$m = [regex]::Match($Text.Substring($i), '^<<-?\s*([''"]?)(\w+)\1')
			if ($m.Success) { $heredocs.Add($m.Groups[2].Value); $i += $m.Length; continue }
		}
		if ($c -eq "`n" -or $c -eq "`r") {
			. $endSeg
			if ($c -eq "`n" -and $heredocs.Count) {
				foreach ($word in $heredocs) {
					$m = [regex]::Match($Text.Substring($i + 1), '(?m)^[ \t]*' + [regex]::Escape($word) + '[ \t]*\r?$')
					$i = if ($m.Success) { $i + 1 + $m.Index + $m.Length } else { $n }
				}
				$heredocs.Clear()
				continue
			}
			$i++; continue
		}
		if ($c -eq ';' -or $c -eq '|' -or $c -eq '(' -or $c -eq ')' -or $c -eq '{' -or $c -eq '}') { . $endSeg; $i++; continue }
		if ($c -eq '&') {
			if ($i + 1 -lt $n -and $Text[$i + 1] -eq '&') { . $endSeg; $i += 2; continue }
			if ($inTok -and $i -gt 0 -and ($Text[$i - 1] -eq '>' -or $Text[$i - 1] -eq '<')) { [void]$sb.Append($c); $i++; continue }
			if (-not $inTok -and -not $cur.Count) { $i++; continue }     # call operator: & "path"
			. $endSeg; $i++; continue
		}
		if ($c -eq '#' -and -not $inTok) {
			while ($i -lt $n -and $Text[$i] -ne "`n") { $i++ }
			continue
		}
		if ([char]::IsWhiteSpace($c)) { . $endTok; $i++; continue }
		[void]$sb.Append($c); $inTok = $true
		$i++
	}
	. $endSeg
	, $segments
}

# --- Rules -----------------------------------------------------------------------------------------------

function New-Deny([string]$Rule, [string]$Reason) { [pscustomobject]@{ Rule = $Rule; Reason = $Reason } }

$script:Footer = ' (safety hook scripts/dev/hooks/safety-guard.ps1, denials are logged in %LOCALAPPDATA%\ArxFatalisVR\safety-hook.log; if this is a false positive, tell the coordinator.)'

function Test-GitPush([string[]]$Args2, [string]$Dir) {
	$positional = [Collections.Generic.List[string]]::new()
	$force = $false; $all = $false
	for ($k = 0; $k -lt $Args2.Count; $k++) {
		$a = $Args2[$k]
		if ($a -eq '--') { continue }
		if ($a -match '^--(force|force-with-lease|force-if-includes)(=.*)?$' -or $a -eq '--mirror') { $force = $true; continue }
		if ($a -eq '--all') { $all = $true; continue }
		if ($a -match '^-[a-zA-Z]+$' -and $a -cmatch 'f') { $force = $true; continue }
		if ($a -in '-o', '--push-option', '--repo', '--receive-pack', '--exec') { $k++; continue }
		if ($a.StartsWith('-')) { continue }
		$positional.Add($a)
	}
	foreach ($p in $positional) { if ($p.StartsWith('+')) { $force = $true } }
	if ($force) {
		return New-Deny 'force-push' ('Force-push is blocked. Bring your branch up to date with `git fetch origin` + `git merge origin/main` and push normally. If a remote branch really has to be rewritten, ask the coordinator.' + $script:Footer)
	}
	$current = $null
	$refspecs = 0
	foreach ($p in $positional) {
		$dst = $p.TrimStart('+')
		if ($dst.Contains(':')) { $dst = $dst.Substring($dst.LastIndexOf(':') + 1) }
		$dst = $dst -replace '^refs/heads/', ''
		if ($dst -eq 'HEAD') {
			if (-not $current) { $current = Get-CurrentBranch $Dir }
			$dst = $current
		}
		if ($dst -eq 'main' -or $dst -eq 'master' -or $dst -eq '*') {
			return New-Deny 'push-main' ('Pushing to (or deleting) main / master is blocked: nothing goes there directly. Push your task branch (`git push -u origin <your branch>`) and open a PR; merge it with `gh pr merge <pr> --merge --delete-branch` once CI is green (docs/vr/WORKFLOW.md).' + $script:Footer)
		}
		$refspecs++
	}
	if ($all) {
		return New-Deny 'push-main' ('`git push --all` would push main too. Push your task branch by name: `git push -u origin <your branch>`.' + $script:Footer)
	}
	if ($positional.Count -le 1) {
		$current = Get-CurrentBranch $Dir
		if ($current -eq 'main' -or $current -eq 'master') {
			return New-Deny 'push-main' ("The working copy is on '$current', so this push goes to $current. Never push main / master: create a task branch (`git switch -c ARXVR-<n>-<type>-<name>`), push that and open a PR." + $script:Footer)
		}
	}
	$null
}

function Test-Git($Tokens, $Ctx) {
	$dir = $Ctx.Cwd
	$k = 1
	while ($k -lt $Tokens.Count) {
		$t = $Tokens[$k].T
		if ($t -ceq '-C') {
			$k++
			if ($k -lt $Tokens.Count) { $dir = Resolve-GuardPath $Tokens[$k].T $dir }
		} elseif ($t -ceq '-c' -or $t -eq '--exec-path' -or $t -eq '--namespace') { $k++ }
		elseif ($t.StartsWith('-')) { }
		else { break }
		$k++
	}
	if ($k -ge $Tokens.Count) { return $null }
	$sub = $Tokens[$k].T.ToLowerInvariant()
	$rest = @(for ($m = $k + 1; $m -lt $Tokens.Count; $m++) { $Tokens[$m].T })

	if ($sub -eq 'push') { return Test-GitPush $rest $dir }

	$inOwner = $dir -and (Test-InOwnerCopy $dir $Ctx)
	if ($inOwner) {
		$restLower = @($rest | ForEach-Object { $_.ToLowerInvariant() })
		$discard = $false
		switch ($sub) {
			'reset' { $discard = $restLower -contains '--hard' }
			'clean' {
				$force = [bool]($rest | Where-Object { $_ -ceq '--force' -or $_ -cmatch '^-[a-zA-Z]*f[a-zA-Z]*$' })
				$dry = [bool]($rest | Where-Object { $_ -ceq '--dry-run' -or $_ -cmatch '^-[a-zA-Z]*n[a-zA-Z]*$' })
				$discard = $force -and -not $dry
			}
			{ $_ -in 'checkout', 'switch' } {
				$discard = [bool]($rest | Where-Object { $_ -in '-f', '--force', '--discard-changes' -or $_ -in '.', ':/', '*' })
			}
			'restore' {
				$whole = [bool]($rest | Where-Object { $_ -in '.', ':/', '*' })
				$stagedOnly = ($restLower -contains '--staged' -or $restLower -contains '-s') -and -not ($restLower -contains '--worktree' -or $restLower -contains '-w')
				$discard = $whole -and -not $stagedOnly
			}
		}
		if ($discard) {
			return New-Deny 'owner-discard' ("`git $sub` here would throw away uncommitted work in the owner's copy ($($Ctx.Owner)). Never discard anything there: report what is uncommitted to the coordinator, or work in your own worktree." + $script:Footer)
		}
	}
	if ($inOwner -and $Ctx.Top -and -not $Ctx.Top.Equals($Ctx.Owner, 'OrdinalIgnoreCase') -and
		$sub -in 'add', 'commit', 'checkout', 'switch', 'reset', 'clean', 'merge', 'rebase', 'pull', 'stash', 'restore', 'rm', 'mv', 'cherry-pick', 'apply', 'am', 'revert') {
		return New-Deny 'owner-write' ("`git $sub` would change the owner's copy ($($Ctx.Owner)) from the worktree session in $($Ctx.Top). Run git in your own worktree instead; the coordinator syncs the owner's copy." + $script:Footer)
	}
	$null
}

function Test-Gh($Tokens) {
	if ($Tokens.Count -lt 3 -or $Tokens[1].T -ne 'api') { return $null }
	$endpoint = $Tokens | Where-Object { $_.T -match 'git/refs/heads/(main|master)(\W|$)' } | Select-Object -First 1
	if (-not $endpoint) { return $null }
	$method = $null; $fields = $false
	for ($k = 2; $k -lt $Tokens.Count; $k++) {
		$t = $Tokens[$k].T
		if ($t -in '-X', '--method' -and $k + 1 -lt $Tokens.Count) { $method = $Tokens[$k + 1].T.ToUpperInvariant() }
		if ($t -in '-f', '-F', '--field', '--raw-field', '--input') { $fields = $true }
	}
	if (($method -and $method -ne 'GET') -or $fields) {
		return New-Deny 'push-main' ('Changing the main / master ref through the API is blocked. Merge PRs with `gh pr merge <pr> --merge --delete-branch`; reading the ref (a plain `gh api` GET) is fine.' + $script:Footer)
	}
	$null
}

$script:ValueParams = 'include', 'exclude', 'filter', 'erroraction', 'errorvariable', 'warningaction', 'warningvariable', 'informationaction', 'outvariable', 'outbuffer', 'pipelinevariable', 'stream', 'credential', 'ea', 'ev', 'wa', 'wv'

# Positional path arguments (and -Path / -LiteralPath values) of a file command, as typed.
function Get-PathArgs($Tokens, [int]$From = 1) {
	$out = [Collections.Generic.List[string]]::new()
	$afterDashes = $false
	for ($k = $From; $k -lt $Tokens.Count; $k++) {
		$t = $Tokens[$k].T
		if (-not $afterDashes -and $t -eq '--') { $afterDashes = $true; continue }
		if (-not $afterDashes -and -not $Tokens[$k].Q -and $t.StartsWith('-')) {
			$name = $t.TrimStart('-').ToLowerInvariant()
			if ($name -in 'path', 'literalpath', 'pspath', 'lp', 'destination') {
				if ($k + 1 -lt $Tokens.Count) { $out.Add($Tokens[$k + 1].T); $k++ }
			} elseif ($name -match '^(path|literalpath|destination):(.+)$') { $out.Add($Matches[2]) }
			elseif ($name -in $script:ValueParams) { $k++ }
			continue
		}
		if (-not $afterDashes -and $t -match '^/[A-Za-z]$') { continue }          # cmd switches: /s /q /f
		foreach ($part in $t.Split(',')) { if ($part) { $out.Add($part) } }
	}
	, $out
}

function Test-Delete($Tokens, $Ctx) {
	if (-not $Ctx.Top) { return $null }
	$recursive = $false
	foreach ($t in $Tokens | Select-Object -Skip 1) {
		if ($t.Q) { continue }
		if ($t.T -match '^-r(e(c(u(r(s(e)?)?)?)?)?)?(:\$?true)?$' -or $t.T -ceq '--recursive' -or $t.T -cmatch '^-[rRfivIdPx]*[rR][rRfivIdPx]*$' -or $t.T -match '^/s$') { $recursive = $true }
	}
	if (-not $recursive) { return $null }
	foreach ($raw in (Get-PathArgs $Tokens)) {
		$abs = Resolve-GuardPath $raw $Ctx.Cwd
		if (-not $abs) { Write-GuardLog "note=unresolved-delete-path path=$raw"; continue }
		if ((Test-Inside $abs $Ctx.Top) -or ($Ctx.JobTmp -and (Test-Inside $abs $Ctx.JobTmp))) { continue }
		$tmpNote = if ($Ctx.JobTmp) { " and your job tmp dir ($($Ctx.JobTmp))" } else { '' }
		return New-Deny 'delete-outside' ("Recursive delete of '$abs' is blocked: it is outside your worktree ($($Ctx.Top))$tmpNote. Delete only inside those. Build output: .\scripts\build-windows.ps1 -Clean. A finished worktree is removed by the coordinator with scripts/dev/new-worktree.ps1 -Remove." + $script:Footer)
	}
	$null
}

$script:WriteAll = 'out-file', 'set-content', 'add-content', 'clear-content', 'new-item', 'ni', 'remove-item', 'ri', 'rm', 'del', 'erase', 'rd', 'rmdir', 'tee', 'tee-object', 'touch', 'mkdir', 'md', 'truncate', 'sc', 'ac', 'clc'
$script:WriteDest = 'copy-item', 'cp', 'cpi', 'copy', 'xcopy', 'robocopy', 'install', 'ln'
$script:WriteBoth = 'move-item', 'mv', 'mi', 'move', 'rename-item', 'ren', 'rni'

function Test-OwnerWrite($Tokens, $Name, $Ctx) {
	$targets = [Collections.Generic.List[string]]::new()
	# redirections: > file, >> file, 2> file, >file
	for ($k = 0; $k -lt $Tokens.Count; $k++) {
		if ($Tokens[$k].Q -or $Tokens[$k].T -cnotlike '*>*') { continue }
		if ($Tokens[$k].T -notmatch '^[^>]*>>?\|?(.*)$') { continue }
		$target = $Matches[1]
		if (-not $target -and $k + 1 -lt $Tokens.Count) { $target = $Tokens[$k + 1].T }
		if ($target -and $target -notmatch '^(&\d|\$null|nul|/dev/)') { $targets.Add($target) }
	}
	if ($Name -in $script:WriteAll -or $Name -in $script:WriteBoth -or ($Name -eq 'sed' -and ($Tokens | Where-Object { -not $_.Q -and ($_.T -cmatch '^-[a-zA-Z]*i' -or $_.T -match '^--in-place') }))) {
		foreach ($p in (Get-PathArgs $Tokens)) { $targets.Add($p) }
	} elseif ($Name -in $script:WriteDest) {
		$paths = Get-PathArgs $Tokens
		$dest = $null
		$destIdx = [array]::FindIndex($Tokens, [Predicate[object]] { param($t) -not $t.Q -and $t.T.TrimStart('-').ToLowerInvariant() -eq 'destination' })
		if ($destIdx -ge 0 -and $destIdx + 1 -lt $Tokens.Count) { $dest = $Tokens[$destIdx + 1].T }
		elseif ($Name -eq 'robocopy' -and $paths.Count -ge 2) { $dest = $paths[1] }
		elseif ($paths.Count -ge 2) { $dest = $paths[$paths.Count - 1] }
		if ($dest) { $targets.Add($dest) }
	}
	foreach ($raw in $targets) {
		$abs = Resolve-GuardPath $raw $Ctx.Cwd
		if ($abs -and (Test-OwnerPath $abs $Ctx)) {
			return New-Deny 'owner-write' ("Writing to '$abs' is blocked: it is in the owner's copy ($($Ctx.Owner)), and this session works in the worktree $($Ctx.Top). Write only inside your own worktree (or your job tmp dir); the owner's copy is the test station." + $script:Footer)
		}
	}
	$null
}

$script:Wrappers = 'sudo', 'command', 'time', 'nohup', 'env', 'exec', 'call'
$script:DeleteNames = 'rm', 'del', 'erase', 'rd', 'rmdir', 'remove-item', 'ri'

function Test-Command([string]$Text, $Ctx, [int]$Depth) {
	if ($Depth -gt 3) { return $null }
	foreach ($seg in (Split-Segments $Text)) {
		$tokens = @($seg)
		while ($tokens.Count -and (($tokens[0].T -match '^[A-Za-z_]\w*=' -and -not $tokens[0].Q) -or $tokens[0].T -in '&', '.' -or ([IO.Path]::GetFileName($tokens[0].T).ToLowerInvariant() -in $script:Wrappers))) {
			$tokens = @($tokens | Select-Object -Skip 1)
		}
		if (-not $tokens.Count) { continue }
		$name = ([IO.Path]::GetFileName($tokens[0].T)).ToLowerInvariant() -replace '\.(exe|cmd|bat|ps1)$', ''

		# a shell run from the shell: look inside its command string
		$inner = $null
		if ($name -in 'bash', 'sh', 'zsh', 'dash') {
			for ($k = 1; $k -lt $tokens.Count - 1; $k++) { if ($tokens[$k].T -cmatch '^-[a-z]*c$') { $inner = $tokens[$k + 1].T; break } }
		} elseif ($name -eq 'cmd') {
			for ($k = 1; $k -lt $tokens.Count; $k++) { if ($tokens[$k].T -match '^/[ck]$') { $inner = ($tokens | Select-Object -Skip ($k + 1) | ForEach-Object { $_.T }) -join ' '; break } }
		} elseif ($name -in 'pwsh', 'powershell') {
			for ($k = 1; $k -lt $tokens.Count; $k++) {
				$t = $tokens[$k].T.ToLowerInvariant()
				if ($t -match '^-c(o(m(m(a(nd?)?)?)?)?)?$') { $inner = ($tokens | Select-Object -Skip ($k + 1) | ForEach-Object { $_.T }) -join ' '; break }
				if ($t -match '^-e(n(c(o(d(e(d(c(o(m(m(a(nd?)?)?)?)?)?)?)?)?)?)?)?)?$' -and $k + 1 -lt $tokens.Count) {
					try { $inner = [Text.Encoding]::Unicode.GetString([Convert]::FromBase64String($tokens[$k + 1].T)) } catch { }
					break
				}
			}
		} elseif ($name -in 'iex', 'invoke-expression') {
			$inner = ($tokens | Select-Object -Skip 1 | ForEach-Object { $_.T }) -join ' '
		}
		if ($inner) {
			$deny = Test-Command $inner $Ctx ($Depth + 1)
			if ($deny) { return $deny }
			continue
		}

		if ($name -in 'cd', 'chdir', 'sl', 'set-location', 'pushd', 'push-location') {
			$target = $tokens | Select-Object -Skip 1 | Where-Object { $_.T -notmatch '^(-|/d$)' -or $_.Q } | Select-Object -First 1
			$Ctx.Cwd = if ($target) { Resolve-GuardPath $target.T $Ctx.Cwd } else { $Ctx.Cwd }
			continue
		}

		$deny = $null
		if ($name -eq 'git') { $deny = Test-Git $tokens $Ctx }
		elseif ($name -eq 'gh') { $deny = Test-Gh $tokens }
		if (-not $deny -and $name -in $script:DeleteNames) { $deny = Test-Delete $tokens $Ctx }
		if (-not $deny -and $Ctx.Owner -and $Ctx.Top -and -not $Ctx.Top.Equals($Ctx.Owner, 'OrdinalIgnoreCase')) { $deny = Test-OwnerWrite $tokens $name $Ctx }
		if ($deny) { return $deny }
	}
	$null
}

# Entry point of the rules: the parsed hook input -> a deny object or $null.
function Test-ToolCall($Payload) {
	$cwd = [string]$Payload.cwd
	if (-not $cwd) { $cwd = (Get-Location).Path }
	$repo = Get-RepoInfo $cwd
	$jobTmp = if ($env:CLAUDE_JOB_DIR) { Join-Path $env:CLAUDE_JOB_DIR 'tmp' } else { $null }
	$ctx = @{
		Cwd    = Get-FullPath $cwd
		Top    = if ($repo) { Get-FullPath $repo.Top } else { $null }
		Owner  = if ($repo -and $repo.Owner) { Get-FullPath $repo.Owner } else { $null }
		JobTmp = if ($jobTmp) { Get-FullPath $jobTmp } else { $null }
	}
	$tool = [string]$Payload.tool_name
	if ($tool -eq 'Bash' -or $tool -eq 'PowerShell') {
		$command = [string]$Payload.tool_input.command
		if (-not $command) { return $null }
		return Test-Command $command $ctx 0
	}
	if ($tool -in 'Edit', 'Write', 'NotebookEdit' -and $ctx.Owner -and $ctx.Top -and -not $ctx.Top.Equals($ctx.Owner, 'OrdinalIgnoreCase')) {
		$file = [string]$Payload.tool_input.file_path
		if (-not $file) { $file = [string]$Payload.tool_input.notebook_path }
		$abs = if ($file) { Resolve-GuardPath $file $ctx.Cwd } else { $null }
		if ($abs -and (Test-OwnerPath $abs $ctx)) {
			return New-Deny 'owner-write' ("Editing '$abs' is blocked: it is in the owner's copy ($($ctx.Owner)), and this session works in the worktree $($ctx.Top). Edit the same file inside your own worktree." + $script:Footer)
		}
	}
	$null
}

# --- Hook entry ------------------------------------------------------------------------------------------

if ($MyInvocation.InvocationName -eq '.') { return }

try {
	$reader = [IO.StreamReader]::new([Console]::OpenStandardInput(), [Text.UTF8Encoding]::new($false))
	$payload = $reader.ReadToEnd() | ConvertFrom-Json
	$deny = Test-ToolCall $payload
	if ($deny) {
		$what = if ($payload.tool_input.command) { [string]$payload.tool_input.command } else { [string]$payload.tool_input.file_path }
		$what = ($what -replace '\s+', ' ')
		if ($what.Length -gt 300) { $what = $what.Substring(0, 300) + '...' }
		Write-GuardLog ("DENY rule=$($deny.Rule) session=$($payload.session_id) cwd=$($payload.cwd) tool=$($payload.tool_name) cmd=$what")
		[Console]::Out.Write((@{
			hookSpecificOutput = @{
				hookEventName            = 'PreToolUse'
				permissionDecision       = 'deny'
				permissionDecisionReason = "Blocked [$($deny.Rule)]: $($deny.Reason)"
			}
		} | ConvertTo-Json -Compress -Depth 4))
	}
} catch {
	Write-GuardLog "ERROR (failed open) $($_.Exception.Message) at $($_.InvocationInfo.ScriptLineNumber)"
}
exit 0
