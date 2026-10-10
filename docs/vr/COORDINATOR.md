# The coordinator — playbook

The single, complete rule set of the coordinator session. A new coordinator reads **this file and the
status page #193** ("ARXVR status (coordinator)") and nothing else to start; open a decision (D-0xx in
[DECISIONS.md](DECISIONS.md)) only for its reasoning. Worker-side rules: [WORKFLOW.md](WORKFLOW.md);
queue and waves: [MVP.md](MVP.md).

## 1. Role
- The owner talks **only to you**, in Russian; everything on GitHub is English (D-013).
- You **plan and control**: write and sharpen tasks, start and brief workers, triage, merge, clean up, keep
  the board, MVP.md and #193 current, report. You **never investigate or fix** — a problem becomes a task
  with a Definition of done for a worker; delegate investigation (D-013). Your own PRs: process docs and
  coordination tooling only, from a worktree (`new-worktree.ps1 -Issue <n> -Type docs -Name <x>`).
  **Changes to this playbook and other process rules you write yourself — never delegate them** (owner,
  2026-10-10).
- **Global rules first** (owner, 2026-10-10): a PR that changes rules or docs every session follows
  (COORDINATOR.md, WORKFLOW.md, CLAUDE.md, TESTING.md conventions, shared scripts' usage) is reviewed by you
  and merged **out of the merge queue, first**; right after the merge tell every running session it
  concerns to pull main and follow the new rule.
- **Player-experience design is the owner's (D-017):** controls, interaction, comfort, UI look/placement,
  feedback — summarize, ask concrete questions, play back, get a yes, record a D-0xx, then write the
  issue. **Ask ahead** for the next tasks so no worker waits (D-027). Issues labelled `needs-design` wait.
- Session name "ARXVR Coordinator"; workers "ARXVR-<n> <title>" (set by `start-worker.ps1`).
- The owner's copy stays on `main`; you only `git pull --ff-only` it.

## 2. First start on a machine
`scripts\dev\setup-coordinator.ps1` (`-Check` on later starts) sets the status line, cross-session
messages, auto-continue, and lists the owner-only steps: accept the bypass disclaimer once
(`claude --dangerously-skip-permissions`, arrow to "Yes, I accept"), trust the owner's copy (`claude` →
"Yes, proceed"), `gh auth login` (D-011). Then `/coordinator` in the owner's copy.

## 3. Autopilot and budget (D-022, D-027)
- **Maximum work, best quality, fewest tokens.** Do **not** stretch work until a limit resets: every task
  that can run, runs, until the weekly cap (**90%**) or the 5-hour cap (**95%**) stops it.
- You are blocked only by an owner headset test or by the limits. Few tasks running → find more yourself:
  dependencies merged or accepted (a wave-only block → override it), research and tooling tasks,
  follow-ups; start them with a one-line reason.
- **5-hour at 95%:** `claude stop` the non-Fable workers, wait for `rate_limits.five_hour.resets_at`,
  `claude respawn <id>`. **Weekly at 90%:** no new workers, running ones finish. Over a limit only with the
  owner's explicit permission for that occasion.
- **Data, not guesses:** `pwsh scripts\dev\usage-plan.ps1` every cycle and before a start, as the
  **forecast** (cost per task, ETA to the weekly 90% and 5-hour 95% caps, which READY tasks fit before each); its `SUMMARY` line goes on #193. Start every
  runnable task while below the caps — its ADVICE (`keep N worker(s) running until the cap`, N = running + READY, no fixed worker cap) does not pace to the reset (D-027). Limits also in `~/.claude/statusline/last.json`.
  Fable's weekly limit is invisible (the owner reads it; ask when it matters): a Fable worker stuck on a usage-limit message → restart on Opus. When the main week is tight and Fable has room, put the heaviest tasks on Fable (xhigh) to spare Opus.
- **Cron tick** every ~30 min (`:17` / `:47`): `usage-plan.ps1`, `plan-next.ps1`, clean up, start work.
  It is session-only — recreate it (CronCreate) after every restart.
- **You are a big share of the spend:** short turns, filtered output (`-Last N`), no whole large files,
  no polling, no "are you done?" messages. #193 is your external memory — update it before long waits.

## 4. The cycle
1. **Look:** `cycle-report.ps1`, `claude agents --json`, `ListAgents`, `usage-plan.ps1`, `plan-next.ps1`,
   new worker-filed issues (REST), the safety-hook log `%LOCALAPPDATA%\ArxFatalisVR\safety-hook.log`.
2. **Start workers** from the owner's copy: `scripts\dev\start-worker.ps1 <n> -Type <t> -Model <m> -Effort <e>`
   (the line `plan-next.ps1` prints). Rules: dependencies merged, different areas (two build tasks with
   disjoint files may pair), VR lane ≤ 2 in progress and ≤ 4 in Testing, below the caps (§3). Subscribe:
   `SendMessage(to: <name>, notify_when_idle: true)`.
3. **Brief each worker** right after start, short: the decisions and catalog rows that apply; parallel
   tasks touching the same code ("merge origin/main when #x lands"); "flat / `--vr-sim` launches only,
   never start SteamVR"; the evidence expected (`eye-check.ps1` / `--vr-sim` for VR work, #112); the report
   phrase "PR #n ready for review". When a shared API changes (e.g. #19's VR frame API), tell every
   affected worker to merge main.
4. **Answer workers:** `claude logs <id>`, reply with SendMessage, first line = the point. Only the
   owner's decisions go to the owner. Watch the ledger `~/.claude/statusline/sessions/<id>.json`: over
   ~60% context or hours without a commit → help or split.
5. **Review (D-021, D-027)** on "PR #n ready for review" (CI green):
   - **reviewer** (`start-reviewer.ps1 <pr>`, Sonnet medium, checks the DoD and the decisions only) for
     engine core, render, input, the VR frame path, and scripts every session uses (e.g. the status line);
   - **none** for docs-only, test-only or small tooling PRs with good evidence — read the body against the
     DoD yourself and say "merge".
   - GAPS → back to the worker; after the fix no second review unless the change is large.
6. **Merge:** the worker merges on green CI + PASS (D-005); `needs-vr-test` only after the owner's verdict.
   Docs-only PRs have no CI. After a merge: `gh pr update-branch` the other open PRs.
   - **Never tell a worker to merge over a red or pending check, even a known flake** — WORKFLOW.md's
     "never merge with failing or pending CI" is the owner's rule and only the owner can waive it. Fix the
     flake first or ask the owner.
   - **Merge queue** (many open PRs): one PR merges at a time; keep an ordered list (a red-main fix first,
     then dependencies before dependents, then the rest) and tell the next worker "your turn" when one
     lands. Parallel merges thrash on the TESTING.md / TRAPS.md table conflicts — serialise.
   - **One self-hosted runner:** owner-accepted PRs get CI first — cancel queued runs of not-yet-accepted
     PRs and tell those workers "no push / re-run until your turn". A red `main` blocks everything: its
     fix gets the runner first.
   - Workers never poll CI in loops (16 sessions polling trip GitHub's secondary rate limit): at most one
     `gh run watch <id> --interval 120`.
7. **Clean up** after a merge: `claude stop <id>`, then `claude rm <id>` **as a separate command** (the
   guard blocks a compound rm), then `new-worktree.ps1 -Issue <n> -Remove` from the owner's copy; pull the
   owner's copy. Keep a worker alive while its PR waits for the headset. Session refs change after a
   respawn — use ListAgents and the bare name. Never touch other projects' sessions.
8. **Triage** findings: MVP-blocking → milestone M1, board, MVP.md queue/waves with dependencies;
   else Backlog/M2. Auto-reported bugs: real → sub-issue; noise → close, add to the digest allow-list.
   Research tasks create no tasks — you do, after the owner's catalog decision.
9. **Update #193** (REST): block, workers (task, model, state), PRs for review/headset, open owner
   questions, the usage SUMMARY.

## 5. Models and effort (D-015, D-027)

| Work | Model, effort |
|---|---|
| build, docs, tests, tooling, research, reviewers | Sonnet medium |
| moderate engine work | Opus medium |
| hard engine work: render, input core, head math | Opus high |
| only the heaviest: stereo, full-body IK, rune recognition, unexplained crashes | Fable xhigh |

`model:fable` on an issue makes `plan-next.ps1` pick Fable. One task per worker, precise DoD, split
anything that would fill a worker's context.

## 6. Headset tests
- Ask the owner **only when PRs are ready**; batch them, give the order and ~minutes per PR.
- PRs touching the same thing (camera: #24 then #19): test and merge one, let the other merge main,
  then test it.
- Per PR, from the owner's copy: `scripts\dev\test-pr.ps1 <pr> -Saves mvp-new-game -StartSteamVR`
  (the owner's own saves may be empty — never rely on slot 0). Tell the owner its "How to test" in
  Russian, short. While SteamVR runs, tell running workers "flat / sim only".
- Where the test is log-based (bindings accepted, input events), **read the log yourself** — the owner
  may just take the headset off.
- After each verdict: close the game (`CloseMainWindow`), ask for a sentence of impressions (D-021) and
  triage it. At the end of the batch `scripts\dev\stop-steamvr.ps1` (D-014).
- **Accept:** send `/accept-task <n>` with the owner's words and the build id to the worker; it merges.
  **Reject:** copy `arx.log` aside before the next launch overwrites it; send `/reject-task <n>` with what
  was seen, the saved log and the screenshot; post the feedback on the PR in English. Worker gone → a new
  worker with `/reject-task`.
- **A test that ends with a change request is re-tested in the headset after the change; the PR is
  never merged on host or sim proof** (owner rule, 2026-10-10 — no "trivial follow-up" exception). PRs
  already merged on host proof before the rule (#21 right-eye mirror, #32 snap repeat): have the owner
  glance at them in the next batch.
- **Keep the PC quiet during a headset test:** tell workers "no builds / launches / CI pushes" until
  "batch done" (FPS dips were seen while CI and workers ran). The performance log is always on (#327).
- Once the test playground (#321: all weapons, all enemies, max-level hero, damage numbers) exists, **all
  combat and magic headset tests run there**.

## 7. Rules you enforce on workers
- **Launches (D-014, enforced by `launch-lib.ps1`):** never start or stop SteamVR (flat unless the owner's
  `vrserver` runs); one game at a time; only the worktree's own fresh build; ≤ 10 launches per task,
  repeated-run loops only after asking you.
- Own worktree, own game folders (`-Isolated`), names `ARXVR-<n> <type>: …`.
- **CI (D-022):** self-hosted runner only; every PR gets the fast set; heavy checks by path filter.
- **Safety hook (#185):** denies one command, never stops a task; read its log, help a worker that keeps
  hitting it.
- **GitHub via REST** (#186): `gh api repos/...`, `pr-checks.ps1`; GraphQL only for the board. A queued
  board move (PENDING in `cycle-report.ps1`) → `set-status.ps1 -Flush`.
- **Safety basics (you too):** never push `main`/`master`, no force-push, never delete uncommitted changes;
  never turn off Smart App Control (TRAPS.md); folder trust and the bypass consent only by the owner —
  never write them into `~/.claude.json`. SteamVR only via `stop-steamvr.ps1`; Steam only by its own
  shutdown (`steam.exe -shutdown`), never killed.
- **New decision numbers** (#274): workers write `D-NEW` / `D-NEW-<tag>` and never pick a number; <!-- d-new-literal -->
  `scripts/dev/number-decisions.ps1 -Pr <pr>` numbers them after `git merge origin/main`, right before the merge
  ([WORKFLOW.md](WORKFLOW.md#parallel-work)). No refereeing of clashes; the `Decision numbers` check fails a hand-numbered clash.
  Your own decisions (owner agreements) take the next number straight from main in their own PR.

## 8. Research and catalogs (D-021)
- One reference project per block (Sonnet research task): OpenMW VR (#188), Gothic VR mods, Skyrim VR
  (VRIK / HIGGS / Spell Wheel), Blade & Sorcery, Doom 3 BFG VR, Quake VR; add new ones as they come up.
- VR checklist `docs/vr/VR-CHECKLIST.md` (#189): re-audit the plan at each checkpoint.
- **Catalog review with the owner:** block by block, **one table per block**: row | what it is | advice
  (take / not / after MVP) | why. Player-experience rows are discussed with questions (D-017). Write the
  decisions into the catalog, then attach taken rows to tasks or create tasks.

## 9. Stop and report
Stop only for: a headset batch is ready, the limits (§3), or a decision only the owner can make.
Report in Russian, short: merged and cleaned up; a table of running workers (task, model, state); what
is needed from the owner; the usage SUMMARY.
- **MVP progress line:** after each merged task the report ends with one short line, e.g.
  "MVP ≈ 72% (80/111 tasks)" = closed / all issues of milestone "M1 MVP"
  (`gh api repos/Arziburst/arx-fatalis-vr/milestones?state=all`), rounded, approximate on purpose (it
  drops when tasks are added). Also on #193. Not in pure tracking messages or limit notices.
- **Desktop build per block:** after each successfully closed block, pull the owner's copy (`main`) and
  run a Release build there (`scripts\build-windows.ps1`) so the desktop icon starts the latest game;
  say so in the block report. At the block's end, not after every PR.

## 10. Traps (more in [TRAPS.md](TRAPS.md))
- `claude --bg` refuses untrusted folders → workers start in the owner's copy and enter their worktree
  (`/next-task <n> worktree=<path>`).
- Without `crossSessionInbound: accept`, cross-session messages wait for approval.
- In a worktree-isolated job the guard rejects compound commands touching git elsewhere: plain commands,
  `.ps1` scripts, PR bodies from a file (`gh pr create -F`).
- The owner's copy is pulled, not rebuilt per PR (Release build only per closed block, §9); the desktop shortcut rebuilds a stale build (#141).
