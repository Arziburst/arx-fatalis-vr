# Arx Fatalis VR — Traps

Things that already cost us time: misleading symptoms, stale state, instruments that lied. Read this
before debugging something that "makes no sense". **Add an entry in the same PR as the work that hit
the trap**; never delete entries (mark them *resolved* if the cause is gone). Newest first.

Format: **symptom** — what is really going on — what to do.

---

## 2026-10-09 — The CI game is another Windows account: its log, lock and build are not in your profile (#127)

**Symptom:** a CI game check failed (or passed) but `%LOCALAPPDATA%\ArxFatalisVR\userdirs\…\arx.log` and `launches.log`
show nothing about it; or a smoke test of yours and the CI game ran together although "one game at a time" holds.
**Cause:** the runner service is `NT AUTHORITY\NETWORK SERVICE` (session 0). Its `%LOCALAPPDATA%` is
`C:\Windows\ServiceProfiles\NetworkService\AppData\Local`, so CI has its own build, user directory, channel, game lock and
ledger; the lock does not span accounts (#205). It also has no Steam registry entry of the owner, hence `ARXVR_DATA_DIR`.
The game itself runs fine there: no desktop, window and OpenGL context are created anyway (measured: `GAMEPLAY` after ~1 s).
**Do:** read the evidence from the workflow artifact `game-checks` (logs, `status.json`, crash reports, digest), not from
your profile. Do not "fix" a CI failure by starting an interactive runner before the artifact says the window was the problem.

## 2026-10-09 — Backslashes vanish from Windows paths written through sed or a shell string (#186)

**Symptom:** a doc or script line reads `.scriptsdevpr-checks.ps1` instead of `.\scripts\dev\pr-checks.ps1`.
**Cause:** `sed 's#...#.\scripts\dev\x#'` in Git Bash, or any unquoted/double-quoted shell string, treats `\s`, `\d`, `\p`
as escapes and drops the backslash; the edit still "succeeds".
**Do:** edit files with the Edit/Write tools, not `sed` with Windows paths; after any scripted edit grep the changed
lines for the path (`git diff -U0 | grep pr-checks`) before committing.

## 2026-10-09 — "GraphQL limit" errors from commands that look like plain REST (#186)

**Symptom:** `API rate limit already exceeded` / `unknown owner type` from `gh issue view`, `gh pr view`, `gh pr list`,
`gh pr checks --watch`, `gh issue list` or a dev script, while `gh api repos/...` calls work and
`gh api rate_limit` shows `core` untouched; `gh api rate_limit` may even show thousands of `graphql` points left.
**Cause:** every high-level `gh` command except `gh api` with a `repos/...` path is GraphQL (`gh project ...` the
heaviest), and the GraphQL quota (hourly points plus a secondary, short-term limit) is shared by every session of the
account. A board query that fails is easy to read as "nothing there" (`next-task.ps1` said the queue was empty).
**Do:** use `gh api repos/Arziburst/arx-fatalis-vr/...` (REST) or the helpers in `scripts/dev/github-lib.ps1`
(`Get-GhIssue`, `Get-GhPull`, `Get-GhCommitChecks`, ...) and `scripts/dev/pr-checks.ps1 <pr> -Watch`; the board comes from
the 5-minute cache (`board-cache.json`) and a spent limit queues the move (`board-queue.json`, `set-status.ps1 -Flush`).
`ARXVR_FORCE_GRAPHQL_ERROR=1` makes every GraphQL call fail like a spent limit, to try the degraded paths. Also:
PowerShell 7 turns ISO date strings of `ConvertFrom-Json` into `DateTime` — the cache stores epoch seconds instead.

## 2026-10-09 — A reference project's manual and settings screen describe a feature its code no longer has

**Symptom:** research on OpenMW VR finds two accounts of the same facts. The manual says controllers "vibrate in
response to successful hits, and to being hit", and the game's VR page has a *haptics enabled* setting; it also says
the bindings screen "does NOT do any sanity checking". The code: the hit haptics are commented out (`TODO: Port to
lua`), the setting is read by nothing, no shipped script calls the haptics function; and the rebinding code refuses
a conflicting binding with a message. A config comment says the hand-offset sliders run "0 = -1 meters, 1 = 1
meters"; the code computes `(v - 0.5)` m. The `player height` setting is read into a scale that nothing applies.
**Cause:** the manual, the settings window and the config comments were written before a port or refactor (combat
code moving to Lua, the bindings screen becoming a script page) and not updated; a screen that lists a setting
proves nothing about what the code does with it.
**Do:** for every catalog row that rests on "the project does X", search the code for the *use* of the setting or the
call, not its declaration, and say which of manual, screen and code the row rests on; when they disagree the row
follows the code and names the disagreement (RESEARCH-reference-mods.md, F-44, F-167, F-171, F-174).

## 2026-10-09 — On `--loadlevel 10` the inventory never closes, `dodamage player` does nothing, and the game goes to the menu after a minute

**Symptom:** a smoke test on the default `--loadlevel 10` opens the inventory through the channel
(`ui inventory open`), but `ui inventory close` never produces `ui.inventory state=closed`;
`dodamage player 1000` leaves the player alive; a `shot` shows a letterboxed picture; and a run
longer than about a minute ends with `game.state to=MENU` at t=58 s although nobody touched the menu.
**Cause:** `--loadlevel 10` starts a **new game**, and level 10 is the scripted intro, not a playable
start: the script blocks the player's controls (`BLOCK_PLAYER_CONTROLS`, so damage to the player is
not applied) and shows cinema borders (`cinemascope`, so `renderInterface()` skips the HUD whose
update finishes the inventory's closing animation — `player.Interface` keeps `INTER_INVENTORY`). The
intro (`cinematic.skip` at t=29.55, NPC spells at t=52–56) ends in the main menu at t=58.4. Damage
from the player's own `dodamage` is also ignored: the source must be another entity.
**Do:** for anything that needs the player in control, send `setplayercontrols on` and
`cinemascope off` first, damage the player from an NPC (`human_base_0033.dodamage player 1000`), and
keep level 10 runs under 50 s — or start from a playable save (`-Saves`, the test save library #70).
The game event log (#130) shows all of it with `t=` times; read it before suspecting the feature.

## 2026-10-09 — The game crashes in `ScriptEventName::toString()` for a console command

**Symptom:** a new log line that names the script event of a failing command crashed the game
(`ACCESS_VIOLATION`, stack `Script.cpp:180` ← `ScriptEvent.cpp` ← `Console.cpp`) the moment a
console / channel command failed; the same line was fine for commands failing inside real scripts.
**Cause:** console lines run as the pseudo-event `SM_EXECUTELINE` (and jumps as `SM_DUMMY`), which
have no `"on <name>"` entry; `toString()` asserts on that (a no-op in Release) and takes
`substr(3)` of whatever came back. The engine itself only prints events through `operator<<`,
which special-cases both.
**Do:** never call `ScriptEventName::toString()` without handling `SM_EXECUTELINE` / `SM_DUMMY`
first (see `script.error` in `ScriptEvent.cpp`, #130), or print through `operator<<`.

## 2026-10-09 — Scripting a launch from PowerShell: three tools that quietly do the wrong thing

**Symptoms:** (1) `pwsh -File run-windows.ps1 ... -- --no-vr` fails with "parameter name '' is ambiguous";
(2) a script waiting for `status.json` to say `MENU` times out although the game just started — the
first thing it reads is `"state":"EXITED"` of the **previous** run; (3) a date written to a JSON store
with `ToString('o')` comes back from `ConvertFrom-Json` as a `DateTime` and `[DateTime]::Parse(...)` of it
throws or loses the precision.
**Cause:** (1) `pwsh -File` consumes the `--` itself; (2) `run-windows.ps1` does not clear the test
channel directory (the smoke test does); (3) PowerShell 7 turns ISO date strings into objects.
**Do:** (1) start such a launch with `pwsh -Command "& .\scripts\run-windows.ps1 ... -- --no-vr"`;
(2) delete `status.json` before launching and wait for a state *and* the new pid; (3) store epoch seconds
(`log-digest-lib.ps1` does), never date strings, in JSON read back with `ConvertFrom-Json`.

## 2026-10-09 — `vr.shownFrames` stays at 1 while `vr.frames` grows: the frame loop looks broken

**Symptom:** a VR self-test with nobody in the headset reports `frames=834 shown=1`; the log has
one `First VR frame shown` line and nothing after it, as if the layers stopped being submitted.
**Cause:** with an idle headset the session reaches `synchronized` but never `visible`, and SteamVR
sets `XrFrameState::shouldRender` only for the very first frame; the game then submits frames
without layers (as the spec asks) to keep the timing. The one shown frame proves that the whole
layer path (acquire, clear, copy, release, two layers in `xrEndFrame`) ran without an error.
**Do:** judge the frame loop by `vr.frames` (must grow) and by the absence of `[E]` lines, not by
`vr.shownFrames`; whether the picture is right is the simulated backend's (#112) or the owner's
question. A `shownFrames` that grows with an idle headset would mean the runtime changed its
behaviour, not that the game got better.

## 2026-10-09 — A press page says a closed-source mod is "MIT", or that a feature exists the code removed

**Symptom:** research on a reference mod finds two accounts of the same fact: MoguraVR's summary table
lists CircuitLord's Titanfall 2 VR as "MIT" and two pages say a player can copy their flat-game saves
into the VR profile, while the author's README says "the mods are closed source … all rights reserved"
and the installer's own test asserts that the "copy save" button does not exist.
**Cause:** summaries repeat each other and the tool's earlier versions; the installer's MIT licence is
read as the mod's, and a feature removed in v1.2.4 lives on in pages written before it. A licence
line or a feature list copied from a press page into the reference catalog would put a wrong
"copy this" or "this exists" into a decision.
**Do:** for a licence, read `LICENSE` and the README of the repository that ships the thing, and the
installer's third-party notices; for a feature of a closed mod, prefer the author's text and the
installer's code and tests over articles, and label every catalog row with its evidence (code, docs,
add-on, report). When primary and secondary sources disagree, say so in the row.

## 2026-10-09 — `next-task.ps1` says the MVP queue is empty; `set-status.ps1` fails to move a task

*Resolved for the dev scripts by #186: a board that cannot be read is the cache (with a warning) or an error, never
"empty"; `set-status.ps1` queues the move. The cause below stays true for any hand-typed `gh project ...`.*

**Symptom:** `.\scripts\dev\next-task.ps1` prints `NEXT none — the MVP queue is empty` although
`docs/vr/MVP.md` and the board are full of Todo tasks, and `.\scripts\dev\set-status.ps1` throws
`Failed to move #N to <status>`; both outputs start with `GraphQL: API rate limit exceeded for
user ID …`.
**Cause:** the board scripts read the project through `gh`'s GraphQL API, and that hourly quota is
shared by every session of the owner's account (the coordinator, every worker, `cycle-report.ps1`).
When it is spent the project query returns nothing and the scripts read "nothing" as "no tasks".
`set-status.ps1` may still have landed the move: its `#N -> <status>` line is printed before a
trailing query fails.
**Do:** read the reset time with `gh api rate_limit --jq '.resources.graphql'` and retry after it;
never conclude "queue empty" or "move failed" from an output that carries the rate-limit line; check
the board in the browser when in doubt.

## 2026-10-09 — A new PR gets no CI run: `gh pr checks` says "no checks reported" for minutes

**Symptom:** the PR is open, other PRs got their `Build (Windows)` run within a minute, but this one
has none after 6 minutes (`gh run list --branch <branch>` is empty); closing and reopening the PR
changes nothing, `gh workflow run build-windows.yml --ref <branch>` runs green but is not attached
to the PR as a check.
**Cause:** the PR **conflicts with `main`** (`gh pr view <pr> --json mergeStateStatus,mergeable` →
`DIRTY` / `CONFLICTING`). A `pull_request` workflow runs on the merge ref (`refs/pull/N/merge`), and
GitHub cannot create it for a conflicting PR, so no run is created at all — silently, nothing on
the PR page says why. Here two PRs merged into `main` while the task ran (#153, #154) and both
touched `TESTING.md` / `TRAPS.md` next to this task's lines.
**Do:** when a PR shows no checks, look at `mergeStateStatus` first. `git fetch origin`,
`git merge origin/main`, resolve, push — the run starts on the push. Check `mergeable` again before
asking why CI is quiet; `gh pr merge` would also refuse a `CONFLICTING` PR.

## 2026-10-09 — `set-status.ps1` fails with "API rate limit exceeded" although `gh api rate_limit` shows thousands left

*Resolved for the dev scripts by #186 (cached board, one mutation, queue, "secondary limit" recognised and waited for or
queued). Still true for hand-typed `gh project ...`.*

**Symptom:** `set-status.ps1 -Issue <n> -Status Backlog` prints `Issue #<n> is not on the board yet,
adding it` and then `Failed to move #<n>`; run by hand, `gh project item-list` / `field-list` answer
`GraphQL: API rate limit exceeded for user ID …`, while `gh api rate_limit` reports
`graphql.remaining` in the thousands. Repeats for a quarter of an hour, then works.
**Cause:** the Projects commands are heavy GraphQL queries (`item-list` with the whole board), and
several sessions of the same account poll the board (`cycle-report.ps1`, `next-task.ps1`, workers'
`set-status.ps1`); GitHub's secondary / points limit for the account trips before the hourly
counter does. `item-add` went through each time, so the issue **is** on the board — without a status.
**Do:** retry later (minutes, not seconds); do not add the item again by hand (`item-add` is
idempotent, but every retry is another query). Mention the un-set status in the report if the limit
does not lift before the task ends. A board script should treat this error as "try again later",
not as "not on the board".

## 2026-10-09 — A `spellcast` on the player from the console casts nothing

**Symptom:** `spellcast -fs 5 magic_sight self` through the test channel is acknowledged with
`errors=0`, no `[W]`, yet nothing happens: no spell sound, no effect, `camera.fov` in `status.json`
stays at 75.00 although Magic Sight should widen it; a second `spellcast` on the player does nothing
either. The #71 "26 spells, 0 of 240 frames differ" rows were accepted on exactly this command.
**Cause:** `TryToCastSpell` launches a script-cast spell at once only with `NOANIM` **and** `NODRAW`
(`-s` and `-m`). Otherwise it is queued in `spellcast_data` for the rune-drawing loop
(`RuneDraw.cpp`), which skips the player entity — the spell never starts, and the queued
`castingspell != SPELL_NONE` makes every later `spellcast` on the player return at the first line.
There is no error anywhere: the script command succeeded, the launch simply never comes.
**Do:** cast on the player with `-fsm` (`spellcast -fsm 5 <spell> self`) and prove the spell is active
with a number — `spells.launchedByType` / `spells.activeByType` in `status.json` (#155), or the
spell's own effect (`camera.fov` 80.65 for Magic Sight, life / mana, a `shot`) — never with the ack
alone. `scripts/dev/spell-drawcheck.ps1` re-ran the #71 spells check that way (#155).
**Even `-fsm` can launch nothing:** every spell has its own `CanLaunch`, and a refusal is silent too
(at most a fizzle sound). Found in #155: `fake_summon` refuses the player as caster, and the player's
`summon_creature` / `raise_dead` find no valid spot 3 m ahead anywhere tried on level 10. Cast those
from an NPC (`human_base_0033.spellcast -fsm 5 <spell> player`) — and check `launchedByType`.

## 2026-10-09 — A worktree build keeps the commit of its first build in the log and the window title

**Symptom:** `arx.log` says `Starting Arx Fatalis VR 0.1-dev + a7e4255` (so does `status.json`
`version` and the window title) although the worktree is several commits further; `built-from.txt`
next to `arx.exe` names the right commit.
**Cause:** `Version.cpp` is generated by a custom command whose dependencies are git's `HEAD`,
`logs/HEAD` and `packed-refs` — but in a worktree `.git` is a *file* (`gitdir: <path>`), those files
live under the main checkout's `.git/worktrees/<name>/`, and the rule depended on nothing that changes
with a commit. The owner's copy (a real `.git` directory) never showed it, every worker's copy did.
**Do:** fixed in #115 (`cmake/VersionString.cmake` resolves the gitfile and the common dir, so the
reflog is a dependency again). The banner's `[W] built-from.txt names commit … but the executable
carries version information from …` says when a build id lies: rebuild, and if it persists,
reconfigure (`build-windows.ps1 -Clean`).

## 2026-10-09 — `status.json` says `window.focused: true` for a window that never got the focus

**Symptom:** a self-test started without activation (`SDL_WINDOW_NO_ACTIVATION_WHEN_SHOWN=1`, #147)
reports `window.focused: true` in `status.json`, although the foreground window (user32
`GetForegroundWindow`) is still the terminal and the game never took the keyboard.
**Cause:** `SDL2Window::initialize()` called `onFocus(true)` unconditionally after `SDL_ShowWindow`
— an assumption from the days when a shown window was always the active one. The flag was never
read from SDL; `SDL_WINDOWEVENT_FOCUS_GAINED` fixes it only when the focus really changes later.
**Do:** fixed in #147 (`onFocus(SDL_GetWindowFlags(m_window) & SDL_WINDOW_INPUT_FOCUS)`), so
`window.focused` is now evidence. In general: when `status.json` and an OS-level probe disagree,
suspect the engine's bookkeeping before the probe, and check where the value is *set*, not where it
is reported.

## 2026-10-09 — SteamVR starts, and the headset wakes up, from "just a test launch"

**Symptom:** the owner's Index and base stations turn on, SteamVR's window appears, although nobody
asked for VR — only a worker's test ran.
**Cause:** the OpenXR loader starts the active runtime (SteamVR) inside `xrCreateInstance`. The
engine's guard (#17) refuses when the runtime manifest names SteamVR and `vrserver.exe` is not
running, but anything that bypasses the guard starts SteamVR anyway: `xr_hello --start-runtime`,
another active runtime, a raw `arx.exe` of a build without the guard, and, worst, a worker starting
`vrstartup.exe` by hand to get VR evidence (it happened in #93 — 15 VR runs, someone closed SteamVR
two minutes later).
**Do:** launch only through the scripts: `run-windows.ps1` and `smoke-test.ps1` add `--no-vr` while
`vrserver.exe` is not running and say so; `smoke-test.ps1 -Vr` is skipped (exit 2). Never run
`vrstartup.exe`, never stop `vrmonitor` / `vrserver`; VR evidence without the owner's SteamVR is the
simulator's job (#112). Only the owner passes `-StartSteamVR` (D-014).
## 2026-10-09 — A `shot` taken right after `GAMEPLAY` is all black

**Symptom:** `smoke-test.ps1 -Commands 'shot level10'` with `--loadlevel 10` saves a 640×480 capture
in which every pixel is black (frame 15, 0.15 s after `state GAMEPLAY`), while `status.json` reports
`GAMEPLAY`, 0 errors and a normal frame rate — as if nothing were rendered.
**Cause:** the level starts behind a fade from black. A capture from frame 86 on (0.9 s) shows the
scene (58 % of sampled pixels non-black in the draw-check captures and in a `shot` at frame 517,
#71). The only code path that starts a fade is the `worldfade` script command (`fadeRequestStart`),
so this is the level script's own fade-in, not the renderer; the first `-Commands` line runs the
moment the state flips, i.e. inside that fade.
**Do:** take `shot` captures at least a second after `GAMEPLAY` — put a few `status` lines in front
of the `shot` (each ack takes ≈ 0.25 s) or wait first; judge "is anything rendered" by a non-black
pixel count over several frames, never by one early capture. An early black shot is no evidence
against the renderer.

## 2026-10-09 — The obvious owner fix for #93 changed nothing: the sword was linked to a mesh the NPC no longer used

**Symptom:** the exit crash #93 reads a freed owner through `m_owner`; releasing the NPC weapon
(`_npcdata->weapon`) before the NPC is deleted looked like the fix, yet a detector in `~Entity` still
reported `human_base_0033` deleted while it owned `long_sword_0021` on every exit from level 10 —
an entity that is not the NPC's weapon, not in its inventory and, by `isEntityLinked()`, not linked.
**Cause:** the level 10 script links the sword to the NPC and then applies a full mesh tweak
(`HEAD|TORSO|LEGS`): `EERIE_MESH_TWEAK_Do()` swaps in a new mesh **without** the links and keeps the
old one as `tweaky` (restored by `TWEAK_REMOVE`). The sword keeps `show = LINKED` and its owner, but
the link exists only in `tweaky`, which `destroy()`, `UnlinkAllLinkedObjects()` and `isEntityLinked()`
never look at.
**Do:** do not trust a guess about *which* relation owns an entity — log what the owned entity looks
like (`show`, `scriptload`, linked / in inventory / weapon) and trace `setOwner()` before fixing. The
fix releases every entity whose `owner()` is the dying one, whatever the relation (#93).

## 2026-10-09 — `smoke-test.ps1 -Commands 'loadlevel N'` fails with "null-valued expression" and kills the game

**Symptom:** `=> FAIL You cannot call a method on a null-valued expression.`; the game disappears in
the middle of loading the new level, with no crash report — it looks like the level change crashed.
**Cause:** a test-tool bug, not the game: the channel's `loadlevel` runs synchronously, the level
change writes `status.json` with the new `cmdSeq` before `ack.txt` exists, `Send-ArxCommand` reads a
missing ack, throws, and the smoke test kills the game in its error handler (#142).
**Do:** until #142 is fixed, change levels with `-- --loadlevel N` (one launch per level), not with
the channel command.

## 2026-10-09 — A crash report folder is dated "yesterday" and the VR self-test crashes where the flat one is clean

**Symptom:** a run at 01:52 local time leaves `crashes\2026-10-08-22-52-43-00`; looking for a report
from "today" finds none, and an isolated user directory (`--user-dir`, #123) seems to have a
different crash than the shared one.
**Cause:** crash report folders are named in **UTC** (the `crash time` inside the report too); the
isolated directory simply has its own `crashes\` folder, so reports of a worker's runs are under
`%LOCALAPPDATA%\ArxFatalisVR\userdirs\<key>\crashes\`, not in `Saved Games\Arx Libertatis`. And the
crash that showed up only in VR (`smoke-test.ps1 -Vr`, 2 of 3 exits) is #93 again: the OpenXR session
changes the heap layout enough for the dangling `m_owner` read to fault without any cinematic loaded.
**Do:** match reports by the smoke test's `new crash report: <name>` line, not by the date; look in the
user directory the run used (the smoke test prints it). Treat a `-Vr` exit crash with the
`Entity::cleanReferences` ← `DanaeClearLevel` stack as #93, not as a VR bug — and remember `-Vr` is
not a gate until #93 is fixed.
*Resolved for the exit crash: #93 is fixed (no exit crash in 13 VR exits after the fix); `-Vr` is
still not a reliable gate because of #146 (the game exits with code -1 mid-run in 2 of 15 VR runs).*

## 2026-10-09 — The exit crash #93 needs no playing cinematic — a loaded one is enough

**Symptom:** closing the window after `--loadlevel 10` crashes in `Entity::cleanReferences`
(`DanaeClearLevel`) in 2 of 5 runs although no cinematic ever played and the log shows only
`Loading cinematic "…/introduction.cin"`; quitting through the channel is clean 5 of 5.
**Cause:** the level 10 script only *preloads* the intro (`cine -p introduction`); `cine play` comes
from the real new-game flow, never from `--loadlevel 10`. A loaded `.cin` (its bitmaps) is enough to
change the heap so that the dangling `m_owner` read of #93 faults; the window-close path is more
exposed than the channel quit. The first version of #124 skipped cinematics by loading them and
ending them at once — and brought the crash back.
**Do:** a skipped cinematic must never be loaded (#124 ends it through `cinematicEnd()` without
loading the file). Test exits with `-CloseWindow` as well as with the channel quit: they fail
differently. `--intro` / `--cinematics` remain exposed to #93 until it is fixed.
*Resolved: #93 is fixed — the loaded intro with `-CloseWindow` was 10 of 10 clean after the fix. The
cinematic only changed the heap layout; the dangling owner was there on every exit.*

## 2026-10-09 — An issue body edited from PowerShell lost all its line breaks

**Symptom:** after `gh issue edit --body-file`, the issue body is one long line, and the `-replace`
that should have inserted a section into it did nothing.
**Cause:** PowerShell captures `gh issue view --json body -q .body` as an **array of lines**, not a
string: a multi-line regex never matches, `-eq` between two arrays is not a "did it change" check,
and `Set-Content -NoNewline` with an array joins the lines **without** separators.
**Do:** write issue bodies from a here-string or with `[IO.File]::WriteAllText`; to get a body as one
string use `gh … | Out-String` (or `-join "`n"`); read the issue back and check its line count before
moving on. (Hit on #124, 2026-10-09; the body was restored from the original text.)

## 2026-10-08 — Closing the game 30 s after `--loadlevel 10` crashes 4 times out of 5

**Symptom:** a scripted flat launch (`--no-vr --loadlevel 10`, close the window after 30 s) ends with
"Illegal storage access" (`Entity::cleanReferences` ← `DanaeClearLevel` ← `ArxGame::shutdownGame`)
in 4 of 5 runs, and the crash dialog stays on the screen.
**Cause:** `--loadlevel 10` starts a new game, the level 10 script plays the intro cinematic, and
closing during the cinematic is bug #93 (an entity owner is destroyed before its dependants).
**Do:** development launches now skip the intro cinematic (`--intro` plays it; #92 restores it for
players), so `--loadlevel 10` lands in GAMEPLAY after ~1 s and closing is clean (10/10 with the
channel's `quit`, measured in #99). #93 itself is still open for the `--intro` / player path.
*Resolved: #93 is fixed (an entity linked to a swapped-out tweak mesh kept a dangling owner).*

## 2026-10-08 — `arx.log` is empty or stale while the game runs

**Symptom:** a test that reads `arx.log` while the game is running sees nothing of the current run
(or only the first few KB).
**Cause:** the file logger writes through an `ofstream` that is never flushed until shutdown; a crash
closes it (so a crashed run *does* leave a log, without `Clean shutdown`).
**Do:** start the game with `--test-dir <dir>`: the test channel flushes the log every 250 ms together
with `status.json`, and `[game] state:` / `[mark] <text>` lines give a script fixed anchors. Without
the channel, read the log only after the process exited.
*Resolved: since #129 the file logger flushes a warning or error at once and everything else at
least once a second from its own flush thread (so a stuck main thread does not stop it). A
`Stop-Process` 15 s into a flat run without the channel left a complete log ending with `Done
loading level`, where the old logger left 4137 bytes cut mid-line. The channel's 250 ms flush stays
for tests that want the log fresher than a second.*

## 2026-10-08 — The crash dialog outlives the game and opens Explorer when dismissed

**Symptom:** after a crash a "Fatal Error - Arx Fatalis VR" window stays open although `arx.exe` has
exited with code 1, and closing it opens the crash report folder in Explorer.
**Cause:** the crash processor is a second `arx.exe --crashinfo=…` that shows a message box and then
*always* launches the report directory; without `arxcrashreporter` nothing closes it for a script.
**Do:** automated runs set `ARX_UNATTENDED=1` (the test channel does it for the game, `smoke-test.ps1`
for the process): the report is still written, the dialog is skipped. A leftover dialog is best
stopped with `Stop-Process` (closing it would open Explorer).

## 2026-10-08 — A rebuild fails with `LNK1104: cannot open file 'arx.exe'`

**Symptom:** the build fails at the final link although nothing in the code is wrong.
**Cause:** the game from that build directory is still running (a smoke test, the owner's window, a
forgotten process) and Windows keeps the executable locked. A CMake reconfigure can relink `arx.exe`
even when no source changed.
**Do:** `Get-Process arx` before building; wait for the smoke test to finish (it refuses to start
while another `arx.exe` runs, for the same reason).

## 2026-10-08 — `@(... | Measure-Object).Count` is always 1

**Symptom:** a PowerShell probe reported one `[E]` line in every log, including logs without any.
**Cause:** `Measure-Object` returns a single object; wrapping *that* in `@()` and taking `.Count`
counts the wrapper, not the matches.
**Do:** `@(Select-String …).Count` or `(… | Measure-Object).Count` — and verify a check can fail
(a log with no errors must give 0) before trusting it.

## 2026-10-08 — A board script "succeeded" but changed the wrong issues

**Symptom:** a PowerShell script printed "retitled #36" / "noted #18" for every issue, but titles were
unchanged and comments were empty or belonged to another issue.
**Cause:** `$ordered[36]` on an `[ordered]` dictionary indexes by **position**, not by key, when the
index is an integer (out of range → `$null`, in range → some other entry). Native commands (`gh`)
failing with a non-zero exit code do not stop a script even with `$ErrorActionPreference = 'Stop'`.
**Do:** iterate `[ordered]` dictionaries with `.GetEnumerator()` (`$_.Key` / `$_.Value`) or use a plain
hashtable for lookups; check `$LASTEXITCODE` after every `gh` call; verify the result afterwards
(read the issues back) instead of trusting the script's own output.

## 2026-10-08 — CI downloads hang or time out

**Symptom:** a CI run sits in "vcpkg install" for half an hour or fails with
`error: Download timed out` from `github.com`.
**Cause:** the network to GitHub from the owner's PC is flaky at times, and a vcpkg without a cache
downloads every port source again.
**Do:** CI and local builds share `C:\vcpkg-shared` (vcpkg checkout, downloads, binary cache — see
WORKFLOW.md); a one-off timeout is re-run once (`gh run rerun <id>`), not debugged.

## 2026-10-08 — A freshly built `arx.exe` does not start

**Symptom:** a build that ran minutes ago is suddenly blocked, or the game "crashes" before writing
anything.
**Cause:** Windows **Smart App Control** is enforced on the owner's PC
(`HKLM\SYSTEM\CurrentControlSet\Control\CI\Policy\VerifiedAndReputablePolicyState = 1`) and blocks
unsigned files by reputation ("An Application Control policy has blocked this file").
**Do:** before debugging the engine, check
`Get-WinEvent -LogName Microsoft-Windows-CodeIntegrity/Operational` for event 3077 naming `arx.exe`.
Report it to the owner; never try to bypass it (turning Smart App Control off is irreversible and the
owner's call). Relevant for tester builds too (#88): unsigned builds may be blocked or warned about.

## 2026-10-08 — `arx.log` shows an old run

**Symptom:** after a launch the log looks unchanged, as if the fix did nothing.
**Cause:** `%USERPROFILE%\Saved Games\Arx Libertatis\arx.log` is written only on a **clean
shutdown**; a launch that failed (or was killed) leaves the previous run's log in place.
**Do:** compare the log's `LastWriteTime` with the launch time (or delete it first), and close the game
with `Process.CloseMainWindow()`, never `Stop-Process`.
*Resolved for the log: since #129 the previous run's log is rotated to `arx.1.log` at startup and the
current run's lines reach the disk within a second, so a killed run leaves its own log (the `Run:`
banner line names the pid and the start time; the smoke test no longer deletes `arx.log` before a run,
the history would go with it). `Stop-Process` still skips `Clean shutdown` and the channel's final
status: close the game properly whenever those are the evidence you need.*

## 2026-10-08 — `--data-dir` with the Steam path breaks the command line

**Symptom:** `arx.exe` prints `[CRITICAL] CommandLine:68 Error parsing command-line argument` to
stdout and exits with code 1 without a log.
**Cause:** `Start-Process -ArgumentList` does not quote arguments; `...\common\Arx Fatalis` has a space.
**Do:** quote it (`"--data-dir", "`"$dir`""`) and capture `-RedirectStandardOutput` /
`-RedirectStandardError`; `scripts/run-windows.ps1` already does the right thing.

## 2026-10-08 — A worktree session's branch has none of our work

**Symptom:** a task branch has no `docs/vr/`, no VR code; its PR cannot be merged into `main`.
**Cause:** `EnterWorktree` bases new branches on `origin/master` — the untouched upstream mirror —
because git sees `master` as the default branch, while our development branch is `main`.
**Do:** right after entering the worktree: `git branch -m ARXVR-<n>-<type>-<name>`,
`git reset --hard origin/main`, verify with `git merge-base HEAD origin/main`. The worktree guard
also refuses `git -C <owner copy>` and commands with runtime-computed paths: use literal paths.

## 2026-10-08 — The build pulls in Visual Studio's own vcpkg

**Symptom:** dependencies are rebuilt from source although they were built before.
**Cause:** `VsDevCmd.bat` sets `VCPKG_ROOT` to the vcpkg bundled with Visual Studio.
**Do:** `scripts/build-windows.ps1` reads `VCPKG_ROOT` before loading the MSVC environment — keep it that way.

## 2026-10-08 — CMake refuses a build directory

**Symptom:** `CMake Error: The source "…" does not match the source "…" used to generate cache`.
**Cause:** one build directory was configured from another working copy (the main clone vs. a worktree).
**Do:** build directories are per working copy (`scripts/build-paths.ps1`); do not share them by hand.

## 2026-10-08 — `rc.exe` fails with RC1203

**Symptom:** the build stops at `arx-version.rc` with `RC1203: invalid language ID`.
**Cause:** C/C++-only MSVC flags added with `add_definitions()` reach the resource compiler.
**Do:** add compiler-only flags with `add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:...>)` (fixed in #12).

## 2026-10-09 — `claude --bg` refuses a worker's worktree: "Workspace not trusted"

**Symptom:** `start-worker.ps1` fails with `Workspace not trusted. Run claude in <worktree> once and accept
the trust prompt`, even after the owner trusted the parent folder `C:\Users\arzib\arx-fatalis-vr-wt`.
**Cause:** Claude Code's workspace trust is checked for the launch folder itself; trusting a parent does
not cover a new subfolder. Every worktree is a new folder.
**Do:** launch workers in the owner's copy (trusted) and let them enter the worktree first —
`start-worker.ps1` passes `/next-task <n> worktree=<path>` (D-011). Never write trust flags into
`~/.claude.json` by hand: the owner gives trust.
