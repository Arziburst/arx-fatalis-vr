# Bindings — Arx Fatalis VR

Layer 3 of the coordinator: the concrete tools behind the generic terms of the core
([core/CORE.md](core/CORE.md)) and the project rules ([PROJECT.md](PROJECT.md)). Kept current by the coordinator.

| Generic term | Here |
|---|---|
| Operating system, shells | Windows 11; PowerShell 7 (scripts), Git Bash for the Bash tool |
| Repository, branches | `Arziburst/arx-fatalis-vr`; `main` (development, default) and `master` (upstream mirror) — never pushed directly |
| Owner's working copy | `E:\dev\arx-fatalis-vr` (outside OneDrive since 2026-10-11; on `main`, pulled with `--ff-only`, never edited) |
| First start on a machine | `scripts\dev\setup-coordinator.ps1` (`-Check` later): status line, cross-session messages, auto-continue; lists the owner-only steps (bypass disclaimer, folder trust, `gh auth login`, D-011). Then `/coordinator` in the owner's copy |
| Build / run / test | `scripts\build-windows.ps1`, `scripts\run-windows.ps1`, `scripts\dev\smoke-test.ps1`, `--vr-sim` `.xrs` sequences, `eye-check.ps1` (#112) |
| Start a worker | from the owner's copy: `scripts\dev\start-worker.ps1 <n> -Type <t> -Model <m> -Effort <e>` (the line `plan-next.ps1` prints); subscribe with `SendMessage(to: <name>, notify_when_idle: true)`; lane limits: VR lane ≤ 2 in progress, ≤ 4 in Testing |
| Start a reviewer | `scripts\dev\start-reviewer.ps1 <pr>` (Sonnet medium, D-021) |
| List / stop / remove workers | `claude agents --json`, ListAgents (bare names — refs change after a respawn), `claude stop <id>`, `claude rm <id>` **as a separate command**, `claude respawn <id>` |
| Worker working copies | `scripts\dev\new-worktree.ps1 -Issue <n> -Type <t> -Name <x>` / `-Remove` (from the owner's copy) |
| Worker context | ledger `~/.claude/statusline/sessions/<id>.json` (over ~60% → help or split) |
| Move a task | `scripts\dev\set-status.ps1 -Issue <n> -Status <s>` (`-Flush` for a queued move, PENDING in `cycle-report.ps1`) |
| Cycle overview | `scripts\dev\cycle-report.ps1`, `scripts\dev\plan-next.ps1`, `scripts\dev\pr-checks.ps1` |
| Status page / journal | issue #193 "ARXVR status (coordinator)", via REST |
| Usage / limits | `~/.claude/statusline/last.json`; forecast `pwsh scripts\dev\usage-plan.ps1` (SUMMARY on #193; its ADVICE does not pace to the reset). 5-hour cap: stop non-Fable workers, wait for `rate_limits.five_hour.resets_at`, respawn. Fable stuck on a usage limit → restart on Opus |
| Periodic tick | session cron `:17` / `:47` (CronCreate; session-only — recreate after a restart) |
| Owner's test station | `scripts\dev\test-pr.ps1 <pr> -Saves <save> -StartSteamVR`; close the game with `CloseMainWindow`; `scripts\dev\stop-steamvr.ps1` after the batch |
| Owner's build | pull the owner's copy, `scripts\build-windows.ps1` (Release) — the desktop icon runs it; the shortcut rebuilds a stale build (#141) |
| Decision numbers | workers write `D-NEW` / `D-NEW-<tag>`; <!-- d-new-literal --> `scripts\dev\number-decisions.ps1 -Pr <pr>` after merging main, right before the merge; the `Decision numbers` check fails a hand-numbered clash |
| Progress line | `gh api repos/Arziburst/arx-fatalis-vr/milestones?state=all` → "M1 MVP" closed / all |
| Claude permission rules and hooks | `.claude/settings.json`: read-only allow rules (#340); PreToolUse safety hook `scripts/dev/hooks/safety-guard.sh` (#185, log `%LOCALAPPDATA%\ArxFatalisVR\safety-hook.log`; denies one command, never stops a task); session-title hook |
| Coordinator's own PRs | process docs / coordination tooling from a worktree (`new-worktree.ps1 -Issue <n> -Type docs -Name <x>`); docs-only PRs have no CI |
| Production backup | not applicable (no server production) |

## Project map
| What | Where |
|---|---|
| Plan, queue, waves | `docs/vr/MVP.md` |
| Decisions, workflow, traps, testing | `docs/vr/DECISIONS.md`, `WORKFLOW.md`, `TRAPS.md`, `TESTING.md` |
| Research catalog, VR checklist | `docs/vr/RESEARCH-reference-mods.md`, `docs/vr/VR-CHECKLIST.md` |
| Test saves | `%LOCALAPPDATA%\ArxFatalisVR\savelib\` (`mvp-new-game`, `mvp-items`, `mvp-goblin-fight`) |
| Builds, user dirs, launches log | `%LOCALAPPDATA%\ArxFatalisVR\build\…`, `…\userdirs\…`, `…\launches.log` |
| Game data | `D:\games\SteamLibrary\steamapps\common\Arx Fatalis` (Steam app 1700) |
| Sensitive data | none in the repository; GitHub token in `gh` auth (never copied) |
| Never touch | `master`; the owner's copy (pull only); Smart App Control settings |

## Access list
| Resource | Needed for | Right | Via | Status |
|---|---|---|---|---|
| GitHub `Arziburst/arx-fatalis-vr` | issues, PRs, board, CI | write | `gh` (REST; GraphQL only for the board) | connected |
| SteamVR / Index | headset tests | — | the owner starts it (`-StartSteamVR`) | owner |

## Project-only rules
- **Launches (D-014, `launch-lib.ps1`):** workers never start or stop SteamVR (flat unless the owner's
  `vrserver` runs); one game at a time; only the worktree's own fresh build; ≤ 10 launches per task;
  own game folders (`-Isolated`). While SteamVR runs, workers are "flat / sim only".
- CI (D-022): the single self-hosted runner; the fast set on every PR, heavy checks by path filter;
  after a merge `gh pr update-branch` the other open PRs.
- Safety specifics: never turn off Smart App Control (TRAPS.md); folder trust and the bypass consent
  only by the owner — never written into `~/.claude.json`; SteamVR only via `stop-steamvr.ps1`; Steam
  only via `steam.exe -shutdown`.

## Traps (more in [TRAPS.md](../docs/vr/TRAPS.md))
- `claude --bg` refuses untrusted folders → workers start in the owner's copy and enter their worktree
  (`/next-task <n> worktree=<path>`).
- Without `crossSessionInbound: accept`, cross-session messages wait for approval.
- In a worktree-isolated job the guard rejects compound commands touching git elsewhere: plain commands,
  `.ps1` scripts, PR bodies from a file (`gh pr create -F`).
- `git commit -F -` with a PowerShell here-string commits nothing: write the message to a file.
- Shell writes into `~/.claude` (memory) prompt the owner — use the Write / Edit tools.
