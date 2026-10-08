# Arx Fatalis VR — Traps

Things that already cost us time: misleading symptoms, stale state, instruments that lied. Read this
before debugging something that "makes no sense". **Add an entry in the same PR as the work that hit
the trap**; never delete entries (mark them *resolved* if the cause is gone). Newest first.

Format: **symptom** — what is really going on — what to do.

---

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

## 2026-10-08 — `arx.log` is empty or stale while the game runs

**Symptom:** a test that reads `arx.log` while the game is running sees nothing of the current run
(or only the first few KB).
**Cause:** the file logger writes through an `ofstream` that is never flushed until shutdown; a crash
closes it (so a crashed run *does* leave a log, without `Clean shutdown`).
**Do:** start the game with `--test-dir <dir>`: the test channel flushes the log every 250 ms together
with `status.json`, and `[game] state:` / `[mark] <text>` lines give a script fixed anchors. Without
the channel, read the log only after the process exited.

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
