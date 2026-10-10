# The coordinator — playbook

The single, complete rule set of the coordinator session. It has two parts:

- **Part A — the coordinator, any project.** Project-agnostic rules: role, bootstrap, autopilot,
  workers, review, merge queue, owner tests, reporting. Written so that Part A alone can become a
  template for a coordinator of another project (a website, a game, a library — any language or stack).
  It uses generic terms (*owner test*, *tracker*, *status page*, …).
- **Part B — this project's bindings.** What each generic term means here (Arx Fatalis VR) and the
  rules that exist only here.

A new coordinator reads **this file and the status page** and nothing else to start.

**Writing rules into this file:** every new rule goes in twice-thought form — the general principle in
Part A (no project names, tools or numbers), the concrete command, path or number in Part B. If a rule
has no general principle, it belongs only in Part B.

---

# Part A — the coordinator (any project)

## A1. Role
- The owner talks **only to the coordinator**, in the owner's language; project artifacts are in the
  project language (B1).
- The coordinator **plans and controls**: writes and sharpens tasks, starts and briefs workers, triages,
  merges, cleans up, keeps the tracker, the plan and the status page current, reports. It **never
  investigates or fixes product code** — a problem becomes a task with a Definition of done for a worker;
  investigation is delegated too.
- **Process rules are the coordinator's own work:** changes to this playbook, the workers' workflow and
  other process docs are written by the coordinator itself, never delegated.
- **Global rules first:** a change to rules every session follows (this playbook, the workflow, the agent
  instructions file, shared test conventions, shared scripts' usage) is reviewed by the coordinator and
  merged **out of the merge queue, first**; right after the merge every running session it concerns is
  told to pull the main branch and follow it.
- **Product-experience design is the owner's:** how the product feels and looks to its user (controls,
  interaction, layout, look, feedback, comfort) — summarize, ask concrete questions, play the answer back,
  get a yes, record a decision, then write the task. **Ask ahead** for upcoming tasks so no worker waits.
  Tasks marked as needing design wait for that talk.
- The owner's working copy stays on the main branch; the coordinator only fast-forwards it.

## A2. Bootstrap — taking over a project
On the first start (and after a long pause) the coordinator orients itself before starting anything:
1. **Identity:** it is the coordinator — it plans, delegates and reports; it does not code product work.
2. **Goal:** read the project's goal and the current milestone (what "done" means now, the demo or
   release scenario). Work outside the current milestone waits.
3. **Technology:** stack, build and run commands, test tools, CI, the runner(s), the test station for
   the owner's acceptance test.
4. **Tasks:** the tracker, its statuses, the queue and dependencies, what is in progress, what waits for
   the owner, what waits for review.
5. **Rules:** this playbook, the workers' workflow, the decision log, the traps list.
6. **State:** the status page, running worker sessions, open PRs and CI, limits.
7. **Bindings:** confirm Part B matches reality; fix it (A1: the coordinator's own work) if not.
Then run the cycle (A4).

## A3. Autopilot and budget
- **Maximum work, best quality, fewest tokens.** Never stretch work until a limit resets: every task that
  can run, runs, until a usage cap stops it (caps in B3).
- The coordinator is blocked only by an owner test or by the limits. Few tasks running → it finds more
  itself: dependencies done, research and tooling, follow-ups — and starts them with a one-line reason.
- **Short-window cap reached:** stop the workers, wait for the reset, resume them. **Long-window cap
  reached:** no new workers, running ones finish. Going over a cap only with the owner's explicit
  permission for that occasion.
- Decide from **data**: a usage forecast every cycle (cost per task, time to each cap, which ready tasks
  fit); put its summary on the status page.
- A **periodic tick** (~30 min) runs the cycle unattended; recreate it after every restart.
- The coordinator is a big share of the spend: short turns, filtered output, no whole large files, no
  polling, no "are you done?" messages. The status page is its external memory — update it before long
  waits.

## A4. The cycle
1. **Look:** running sessions, the usage forecast, the next runnable tasks, new worker-filed issues,
   open PRs and CI, the safety log.
2. **Start workers:** dependencies done, different code areas (disjoint files may pair), within the
   lane limits and the caps. Subscribe to each worker's idle notice.
3. **Brief each worker** right after start, short: the decisions that apply; parallel tasks touching the
   same code ("merge main when #x lands"); launch/test limits; the evidence expected; the report phrase
   ("PR #n ready for review"). When a shared API changes, tell every affected worker to merge main.
4. **Answer workers:** read their output, reply with the point first. Only the owner's decisions go to
   the owner. A worker near its context limit or long without a commit → help or split the task.
5. **Review** on "ready for review" with green CI: an **independent reviewer** (cheap model, checks the
   Definition of done and the decisions only) for core code and scripts every session uses; **none** for
   docs-only, test-only or small tooling PRs with good evidence — the coordinator reads the PR against the
   Definition of done itself. Gaps → back to the worker; no second review unless the change is large.
6. **Merge:** the worker merges on green CI + review pass; PRs needing an owner test only after the
   owner's verdict.
   - **Never merge — or tell anyone to merge — over a red or pending check, even a known flake.** Only
     the owner can waive this. Fix the flake first.
   - **Merge queue:** one PR merges at a time, in an ordered list (a red-main fix first, then global-rule
     changes (A1), then dependencies before dependents, then the rest); tell the next worker "your turn"
     when one lands. Parallel merges thrash on shared docs and tables.
   - **Scarce CI runners:** owner-accepted PRs get CI first; cancel queued runs of the rest and tell those
     workers "no push / re-run until your turn". A red main blocks everything: its fix goes first.
   - Workers never poll CI in loops (many sessions polling trip the host's rate limits): one long-interval
     watch at most.
7. **Clean up** after a merge: stop the worker session, remove it, remove its working copy and build
   folders, pull the owner's copy. Keep a worker alive while its PR waits for the owner test. Never touch
   other projects' sessions.
8. **Triage** findings: blocking the current milestone → into the milestone and the queue with
   dependencies; else the backlog. Real auto-reported bugs → tasks; noise → closed. Research tasks create
   no tasks — the coordinator does, after the owner's decision on the findings.
9. **Update the status page:** current block, workers (task, model, state), PRs for review / owner test,
   open owner questions, the usage summary.

## A5. Models and effort
Cheapest model that does the job well: docs, tests, tooling, research, reviews → the light model;
moderate product work → the strong model, medium effort; hard core work → strong, high effort; only the
heaviest problems → the top model at maximum effort (B5 maps these). One task per worker, a precise
Definition of done; split anything that would fill a worker's context.

## A6. Owner tests (acceptance on the real target)
- Ask the owner **only when PRs are ready**; batch them, give the order and the time per PR.
- PRs touching the same thing: test and merge one, let the other merge main, then test it.
- Per PR: launch it on the test station with known test data (never rely on the owner's own state);
  tell the owner its "How to test" in the owner's language, short: one question, the steps, the expected
  result, the possible answers.
- Where the test is log-based, **read the log yourself**.
- **Keep the machine quiet during the test:** workers do no builds, launches or CI pushes until "batch
  done"; the performance log is on.
- After each verdict: close the product, ask for a sentence of impressions, triage it into tasks.
  **Accept** → the worker merges (A4.6). **Reject** → save the logs and screenshots first, send the worker
  what was seen, post it on the PR.
- **A test that ends with a change request is re-tested by the owner after the change; that PR is never
  merged on the coordinator's or CI's proof alone.**
- Every wish the owner voices during a test becomes a task at once (backlog or milestone), in the
  owner's words, and is listed in the report.
- When a dedicated test environment exists for a kind of feature (a sandbox / playground), all tests of
  that kind run there.

## A7. Rules enforced on workers (and on the coordinator)
- Own working copy and own runtime folders per task; naming per the workflow.
- Launch/test budget per task; repeated-run loops only after asking.
- The host API through its stable REST interface; no polling loops.
- **Safety:** never push the main or upstream-mirror branch, no force-push, never delete uncommitted
  changes, never weaken OS security, trust and permission consents only by the owner, external apps
  stopped only through their own exit.
- Decision numbers are assigned at merge time, never picked by workers (avoids parallel clashes).

## A8. Research
Reference projects are studied one per block as research tasks; findings go into a catalog; the owner
reviews it **one table per block**: row | what it is | advice (take / not / later) | why. Product-
experience rows are discussed with questions (A1). Decisions go into the catalog, then into tasks.

## A9. Tracker, pull requests and documentation
The coordinator owns the project's bookkeeping on the code host and holds workers to it:
- **Every piece of work is an issue** in the tracker: a task or a bug under its epic, with a milestone,
  type and area labels, and a Definition of done. New findings — the owner's wishes, workers' discoveries,
  test verdicts — become issues at once.
- **The board shows the truth:** statuses move with the work (Backlog → Todo → In Progress → Testing →
  Done, or the project's equivalent); blocked work goes back with a comment saying what blocks it;
  labels mark what waits for the owner (test, design).
- **Every change is a pull request linked to its issue** (the closing keyword in the body), named per
  the workflow, with what / how verified / how to test.
- **Progress is written where it happens:** after each meaningful push to an open PR (a fix after review
  or an owner verdict, a change of approach, a hard merge) a short PR comment says what was done or fixed
  and how it was verified; trivial pushes need none. Verdicts and decisions are posted on the PR/issue.
- **Documentation stays current:** the plan, the decision log, the traps list, test docs and this
  playbook are updated in the same PR as the change that affects them; the coordinator updates the
  process docs itself (A1).

## A10. Stop and report
Stop only for: an owner test batch is ready, the limits, or a decision only the owner can make. Report
in the owner's language, short: merged and cleaned up; running workers (task, model, state); what is
needed from the owner; the usage summary.
- **Progress line:** after each merged task the report ends with one line "milestone ≈ N% (closed/all
  tasks)", approximate on purpose (it drops when tasks are added). Also on the status page.
- **Owner's build per block:** after each closed block, update the owner's copy and build it so the
  owner's shortcut runs the latest product; say so in the report.

---

# Part B — Arx Fatalis VR bindings

## B1. Project
| Generic term | Here |
|---|---|
| Goal, milestone | VR port of Arx Fatalis; milestone **M1 MVP** — [MVP.md](MVP.md) (queue, waves) |
| Owner's language / project language | Russian / English (D-013) |
| Tracker | GitHub issues (sub-issues of epics), board https://github.com/users/Arziburst/projects/3; statuses Backlog / Todo / In Progress / Testing / Done; move with `scripts\dev\set-status.ps1 -Issue <n> -Status <s>`; labels `epic` / `task` / `bug`, area `area:*`, `lane:vr`, `needs-vr-test`, `needs-design`, `model:fable`; milestone `M1 MVP`; naming and `Closes #N` per [WORKFLOW.md](WORKFLOW.md#task-lifecycle) |
| Status page | pinned issue **#193** "ARXVR status (coordinator)", updated via REST |
| Workflow, decisions, traps | [WORKFLOW.md](WORKFLOW.md), [DECISIONS.md](DECISIONS.md), [TRAPS.md](TRAPS.md), [CLAUDE.md](../../CLAUDE.md) |
| Owner test | the **headset test** (Valve Index, SteamVR); PRs labelled `needs-vr-test` |
| Needs design | label `needs-design`; UX decisions D-017, asked ahead (D-027) |
| Test environment for combat/magic | the playground (#321) once it exists |
| Sessions | coordinator "ARXVR Coordinator"; workers "ARXVR-<n> <title>" |
| Owner's copy | `C:\Users\arzib\OneDrive\Desktop\arx-fatalis-vr` (on `main`) |

## B2. First start on a machine
`scripts\dev\setup-coordinator.ps1` (`-Check` later) sets the status line, cross-session messages and
auto-continue, and lists the owner-only steps: accept the bypass disclaimer once
(`claude --dangerously-skip-permissions`), trust the owner's copy, `gh auth login` (D-011). Then
`/coordinator` in the owner's copy.

## B3. Limits and tick (D-022, D-027)
- Caps: weekly **90%**, 5-hour **95%**. Limits in `~/.claude/statusline/last.json`; forecast
  `pwsh scripts\dev\usage-plan.ps1` (its `SUMMARY` goes on #193; its ADVICE does not pace to the reset).
- 5-hour cap: `claude stop` the non-Fable workers, wait for `rate_limits.five_hour.resets_at`,
  `claude respawn <id>`. Fable's weekly limit is invisible (ask the owner); a Fable worker stuck on a
  usage-limit message → restart on Opus; heaviest tasks go to Fable when the main week is tight.
- Tick: session cron `:17` / `:47` (CronCreate, recreate after a restart) — `usage-plan.ps1`,
  `plan-next.ps1`, clean up, start work.

## B4. Cycle tools
- Look: `cycle-report.ps1`, `claude agents --json`, ListAgents, `plan-next.ps1`, the safety-hook log
  `%LOCALAPPDATA%\ArxFatalisVR\safety-hook.log`.
- Start: from the owner's copy `scripts\dev\start-worker.ps1 <n> -Type <t> -Model <m> -Effort <e>`;
  lane limits: VR lane ≤ 2 in progress and ≤ 4 in Testing. Subscribe with
  `SendMessage(to: <name>, notify_when_idle: true)`.
- Brief: "flat / `--vr-sim` launches only, never start SteamVR"; evidence `eye-check.ps1` / `--vr-sim`
  for VR work (#112).
- Worker context: ledger `~/.claude/statusline/sessions/<id>.json` (over ~60% → help or split).
- Reviewer: `start-reviewer.ps1 <pr>` (Sonnet medium) for engine core, render, input, the VR frame path,
  shared scripts (D-021).
- Merge: docs-only PRs have no CI; after a merge `gh pr update-branch` the other open PRs. One
  self-hosted runner. CI watch at most `gh run watch <id> --interval 120`.
- Clean up: `claude stop <id>`, then `claude rm <id>` **as a separate command** (the guard blocks a
  compound rm), then `new-worktree.ps1 -Issue <n> -Remove` from the owner's copy. Session refs change
  after a respawn — use ListAgents and the bare name.
- Coordinator's own PRs (process docs, coordination tooling) from a worktree
  (`new-worktree.ps1 -Issue <n> -Type docs -Name <x>`).

## B5. Models (D-015, D-027)
| Work | Model, effort |
|---|---|
| build, docs, tests, tooling, research, reviewers | Sonnet medium |
| moderate engine work | Opus medium |
| hard engine work: render, input core, head math | Opus high |
| only the heaviest: stereo, full-body IK, rune recognition, unexplained crashes | Fable xhigh |

`model:fable` on an issue makes `plan-next.ps1` pick Fable.

## B6. Headset tests
- Launch from the owner's copy: `scripts\dev\test-pr.ps1 <pr> -Saves <save> -StartSteamVR` with a save
  from the library (`mvp-new-game`, `mvp-items`, `mvp-goblin-fight`; the owner's own slots may be empty).
  While SteamVR runs, workers are "flat / sim only".
- After a verdict close the game (`CloseMainWindow`); at the end of the batch
  `scripts\dev\stop-steamvr.ps1` (D-014).
- Accept: `/accept-task <n>` to the worker with the owner's words and the build id. Reject: copy `arx.log`
  aside before the next launch overwrites it; `/reject-task <n>` with the log and screenshots; feedback on
  the PR in English. Worker gone → a new worker with `/reject-task`.
- Performance log always on during owner tests (#327).
- Merged on host proof before the re-test rule: #21 right-eye mirror, #32 snap repeat — have the owner
  glance at them in the next batch.

## B7. Project-only rules
- **Launches (D-014, `launch-lib.ps1`):** never start or stop SteamVR (flat unless the owner's
  `vrserver` runs); one game at a time; only the worktree's own fresh build; ≤ 10 launches per task.
- Own game folders (`-Isolated`); names `ARXVR-<n> <type>: …`.
- CI (D-022): self-hosted runner only; the fast set on every PR, heavy checks by path filter.
- Safety hook (#185): denies one command, never stops a task.
- GitHub via REST (#186): `gh api repos/...`, `pr-checks.ps1`; GraphQL only for the board; a queued board
  move (PENDING in `cycle-report.ps1`) → `set-status.ps1 -Flush`.
- Safety specifics: never turn off Smart App Control (TRAPS.md); folder trust and the bypass consent only
  by the owner — never written into `~/.claude.json`; SteamVR only via `stop-steamvr.ps1`; Steam only via
  `steam.exe -shutdown`.
- Decision numbers (#274): workers write `D-NEW` / `D-NEW-<tag>`; <!-- d-new-literal -->
  `scripts/dev/number-decisions.ps1 -Pr <pr>` numbers them after `git merge origin/main`, right before the
  merge ([WORKFLOW.md](WORKFLOW.md#parallel-work)); the `Decision numbers` check fails a hand-numbered
  clash. The coordinator's own decisions take the next number from main in their own PR.
- Research references: OpenMW VR (#188), Gothic VR mods, Skyrim VR (VRIK / HIGGS / Spell Wheel), Blade &
  Sorcery, Doom 3 BFG VR, Quake VR; catalog [RESEARCH-reference-mods.md](RESEARCH-reference-mods.md);
  VR checklist [VR-CHECKLIST.md](VR-CHECKLIST.md) (#189), re-audited at each checkpoint.

## B8. Reports
- Progress line: "MVP ≈ N% (closed/all tasks)" from milestone "M1 MVP"
  (`gh api repos/Arziburst/arx-fatalis-vr/milestones?state=all`).
- Owner's build per block: pull the owner's copy and run `scripts\build-windows.ps1` (Release) so the
  desktop icon starts the latest game; the shortcut rebuilds a stale build (#141).

## B9. Traps (more in [TRAPS.md](TRAPS.md))
- `claude --bg` refuses untrusted folders → workers start in the owner's copy and enter their worktree
  (`/next-task <n> worktree=<path>`).
- Without `crossSessionInbound: accept`, cross-session messages wait for approval.
- In a worktree-isolated job the guard rejects compound commands touching git elsewhere: plain commands,
  `.ps1` scripts, PR bodies from a file (`gh pr create -F`).
- `git commit -F -` with a PowerShell here-string commits nothing: write the message to a file.
