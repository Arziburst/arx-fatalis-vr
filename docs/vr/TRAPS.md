# Arx Fatalis VR — Traps

Things that already cost us time: misleading symptoms, stale state, instruments that lied. Read this
before debugging something that "makes no sense". **Add an entry in the same PR as the work that hit
the trap**; never delete entries (mark them *resolved* if the cause is gone). Newest first.

Format: **symptom** — what is really going on — what to do.

---

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
