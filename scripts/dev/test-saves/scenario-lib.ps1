# The pure parts of make-test-saves.ps1 (#70): parsing a scenario file, the steps that verify saves and the
# order in which a fresh game numbers them. No game, no files except where a function says so, so
# scripts/dev/test-save-library.ps1 can check them. Dot-source it.

# The directives of a scenario file (`@name args`); everything else is one channel command.
$script:ArxScenarioDirectives = @('describe', 'wait', 'state', 'level', 'expect', 'until', 'near', 'entity', 'print')

# Parse the lines of a scenario file into steps and the names of the saves it makes.
# Returns @{ Steps = list of { Kind; Text; Line }; SaveNames = names in order }.
# Throws, naming the line, for what would only fail after a minute in the game: an unknown directive, a
# missing or non-numeric argument, a save name used twice, a name that cannot be a folder, no save at all.
function Read-ArxScenario([string[]]$Lines, [string]$Source = 'scenario') {
	$steps = [Collections.Generic.List[object]]::new()
	$saveNames = [Collections.Generic.List[string]]::new()
	$number = [Globalization.NumberStyles]::Float
	$invariant = [Globalization.CultureInfo]::InvariantCulture
	$isNumber = { param($text) $v = 0.0; [double]::TryParse($text, $number, $invariant, [ref]$v) }
	$lineNo = 0
	foreach ($raw in $Lines) {
		$lineNo++
		$line = "$raw".Trim()
		if (-not $line -or $line.StartsWith('#')) { continue }
		$where = "$Source`:$lineNo"
		if ($line.StartsWith('@')) {
			$word, $rest = $line -split '\s+', 2
			$kind = $word.Substring(1).ToLowerInvariant()
			$text = "$rest".Trim()
			if ($kind -notin $script:ArxScenarioDirectives) { throw "$where`: unknown directive @$kind (known: $($script:ArxScenarioDirectives -join ', '))" }
			$parts = @($text -split '\s+' | Where-Object { $_ })
			switch ($kind) {
				'describe' { if (-not $text) { throw "$where`: @describe needs a text" } }
				'wait' { if ($parts.Count -ne 1 -or -not (& $isNumber $parts[0])) { throw "$where`: @wait needs a number of seconds, got '$text'" } }
				'state' { if ($parts.Count -lt 1 -or $parts.Count -gt 2) { throw "$where`: @state needs a state and an optional timeout, got '$text'" } }
				'level' { if ($parts.Count -lt 1 -or $parts.Count -gt 2 -or -not (& $isNumber $parts[0])) { throw "$where`: @level needs a level number and an optional timeout, got '$text'" } }
				'expect' { if ($parts.Count -lt 2) { throw "$where`: @expect needs a status path and a value, got '$text'" } }
				'until' { if ($parts.Count -lt 2 -or $parts.Count -gt 3) { throw "$where`: @until needs a status path, a value and an optional timeout, got '$text'" } }
				'near' { if ($parts.Count -ne 3 -or ($parts | Where-Object { -not (& $isNumber $_) })) { throw "$where`: @near needs x z radius, got '$text'" } }
				'entity' { if ($parts.Count -lt 2) { throw "$where`: @entity needs an entity id and the text to find, got '$text'" } }
				'print' { if ($parts.Count -lt 1) { throw "$where`: @print needs a status path" } }
			}
			$steps.Add([pscustomobject]@{ Kind = $kind; Text = $text; Line = $lineNo })
		} else {
			$steps.Add([pscustomobject]@{ Kind = 'command'; Text = $line; Line = $lineNo })
			if ($line -match '^save(\s+(.*))?$') {
				$name = "$($Matches[2])".Trim()
				if (-not $name) { throw "$where`: save needs a name" }
				if ($name -notmatch '^[A-Za-z0-9._-]+$') { throw "$where`: save name '$name': use letters, digits, . _ - only (it is a folder name)" }
				if ($saveNames -contains $name) { throw "$where`: save name '$name' is used twice" }
				$saveNames.Add($name)
			}
		}
	}
	if ($saveNames.Count -eq 0) { throw "$Source has no save command" }
	@{ Steps = $steps; SaveNames = $saveNames }
}

# The steps that check saves: for each entry load slot $slots[k] through the channel (a negative slot: it is
# loaded already, by the command line), then the level, the player where it was saved, the controls free and
# a screenshot. $entries have area, player.x, player.z and name (the manifest.json entries).
function New-ArxVerifySteps($entries, [int[]]$slots) {
	for ($k = 0; $k -lt @($entries).Count; $k++) {
		$e = @($entries)[$k]
		if ($slots[$k] -ge 0) { [pscustomobject]@{ Kind = 'command'; Text = "loadslot $($slots[$k])"; Line = 0 } }
		[pscustomobject]@{ Kind = 'level'; Text = "$($e.area)"; Line = 0 }
		[pscustomobject]@{ Kind = 'near'; Text = ('{0} {1} 100' -f [string]::Format([Globalization.CultureInfo]::InvariantCulture, '{0}', $e.player.x), [string]::Format([Globalization.CultureInfo]::InvariantCulture, '{0}', $e.player.z)); Line = 0 }
		# A level fades in from black after a load: an earlier screenshot is black (TRAPS.md)
		[pscustomobject]@{ Kind = 'wait'; Text = '4'; Line = 0 }
		[pscustomobject]@{ Kind = 'expect'; Text = 'player.controlsBlocked False'; Line = 0 }
		[pscustomobject]@{ Kind = 'command'; Text = "shot verify-$($e.name)"; Line = 0 }
		[pscustomobject]@{ Kind = 'wait'; Text = '1'; Line = 0 }
	}
}

# The manifest entries whose save exists in the library, in the order a game numbers its slots: newest save
# file first (SaveGameList sorts by the file's time, at one-second resolution). A copy keeps the time.
function Get-ArxVerifyOrder($Entries, [string]$Library) {
	@($Entries | Where-Object { Test-Path (Join-Path $Library "$($_.name)\gsave.sav") } |
		Sort-Object { (Get-Item (Join-Path $Library "$($_.name)\gsave.sav")).LastWriteTime } -Descending)
}
