# Toolkit — Claude Code mechanics for a coordinator

How a coordinator does its job with Claude Code on **any machine** (Windows, macOS, Linux). This file
tracks Claude Code features and changes with them — re-check the linked docs when something behaves
differently. Items marked *(unconfirmed)* were not stated in the docs; verify them in the dry run (CORE §12a).

## Workers: start, name, manage
- **Background session per worker:** `claude --bg --name "<KEY>-<task> <title>" --model <model> --permission-mode <mode> "<brief>"`
  — prints an id; `claude agents [--json]`, `claude logs <id>`, `claude attach <id>`, `claude stop <id>`,
  `claude respawn <id>`, `claude rm <id>`. `--bg` cannot be combined with `-p`.
  [agent-view](https://code.claude.com/docs/en/agent-view.md), [cli-reference](https://code.claude.com/docs/en/cli-reference.md)
- **Name every session** (CORE §4): `--name` / `-n`; without it the name is auto-generated and the owner
  cannot tell sessions apart.
- **The coordinator's own name** (CORE §2 step 1): `<Project> Coordinator`. Started by the owner from
  the CLI: `claude --name "<Project> Coordinator"`; in an already running session the owner can rename
  it (`/rename <name>` *(unconfirmed)*), or a `UserPromptSubmit` hook can set the title from the project
  *(unconfirmed)*. If the coordinator cannot rename itself, it asks the owner to — once, at init.
- **Model:** `--model`. **Effort** for `--bg` *(unconfirmed)* — put the effort in the brief if no flag works.
- **Directory:** start the session from the worker's working copy (`cd` first); there is no documented
  `--cwd` *(unconfirmed)*.
- **Trust:** the folder must already be trusted, or `--bg` fails with "Workspace not trusted". Trusting a
  folder is the **owner's** step — never written into config by the coordinator.
- **Headless alternative:** `claude -p "<prompt>" --output-format stream-json --max-turns N` for short,
  non-interactive jobs (no idle notices, no messages).
- **Subagents vs sessions:** subagents (the Agent tool) for short focused jobs — research, review, a
  search; up to 20 concurrent, nesting depth 3, they cannot ask the owner questions, their usage counts
  toward yours. Background sessions for real tasks: independent, visible in the session list, own
  working copy. [sub-agents](https://code.claude.com/docs/en/sub-agents.md)
- **Lessons:** a session's message address can change after `respawn` — list sessions again and use the
  bare name; idle sessions may exit — `respawn` them when their turn comes; remove a session with
  `claude rm` as its own command (some guards block compound removals).

## Working copies
- Built-in: `claude --worktree <name>` (`.claude/worktrees/<name>`, branch `worktree-<name>`);
  background sessions get their own worktree by default (`worktree.bgIsolation: "none"` turns that off).
  Manual `git worktree add` works too — pick **one** of the two, or a worker ends up with two copies.
  [worktrees](https://code.claude.com/docs/en/worktrees.md)
- After merge: remove the worktree only if clean and fully pushed (CORE §8) — `scripts/cleanup-worker.sh`.

## Messages between sessions
- `ListAgents` + `SendMessage` (Claude uses them itself); `notify_when_idle: true` gives one notice when
  the worker is next idle (same machine, expires after 12 h).
  [cross-session-messaging](https://code.claude.com/docs/en/cross-session-messaging.md)
- Setting **`crossSessionInbound: "accept"`** lets workers take the coordinator's messages unattended
  (`hold` / `refuse` otherwise). Needs Claude Code v2.1.224+ (macOS / Linux / WSL2), v2.1.234+ (native
  Windows). Native Windows and WSL2 sessions cannot message each other.

## The periodic tick
- `CronCreate` / `/loop` are **session-scoped**: fire only while the session is open and idle, expire
  after 7 days, max 50 per session. `--resume` restores cron tasks (not self-paced loops). Recreate the
  tick after every restart. Durable alternatives: Routines (cloud, ≥ 1 h) or Desktop scheduled tasks.
  [scheduled-tasks](https://code.claude.com/docs/en/scheduled-tasks.md)

## Safety
- **Permission rules:** `deny` → `ask` → `allow`; a deny always wins. Compound commands are split
  (`&&`, `;`, `|`) and every part must match an allow rule. PowerShell rules look like Bash rules:
  `PowerShell(Get-Content *)`. Read-only Bash commands are auto-allowed; PowerShell cmdlets are not.
  [permissions](https://code.claude.com/docs/en/permissions.md)
- **Never allow** patterns that run arbitrary code (interpreters, `npx`, `npm run *`, `gh api *`,
  script blocks).
- **The real enforcement is on the code host:** branch protection — PRs required, checks required, a
  human review required for commercial work. Rules and hooks on the machine are a safety net, not a wall.
- **Hook:** a `PreToolUse` hook (`scripts/hooks/safety-guard.sh`) guards only the irreversible:
  pushing, force-pushing or deleting the protected branches (main / master — incl. quoted,
  `refs/heads/`, `HEAD:`, `+refspec` forms, and a push resolving to the current branch while on main),
  mirror pushes, and removing `/`, home or a worktree by hand. Work-branch operations (rebase, amend,
  reset, force-with-lease on its own branch, deleting work branches) pass. Exit code 2 blocks and
  shows the reason. **A hook that fails to run does not block** — so `settings.example.json` runs
  `safety-guard.sh --self-test` at session start; a failure there means: fix before starting workers.
  Hooks see only shell tools — **MCP tools are covered by deny rules only**. Whether hooks run in
  `bypassPermissions` *(unconfirmed)*. [hooks](https://code.claude.com/docs/en/hooks.md)
- **Deny rules** in `settings.example.json` are the autonomous baseline: the protected branches only.
  Add `gh pr merge*` / `gh pr ready*` where humans merge or mark ready; the cautious level also adds
  `Read(**/.env*)` and the stack candidates (database CLIs, production deploys, MCP write tools) from
  `examples/nextjs-supabase/settings.json`.
- **Protected paths:** writes into `.git` and `.claude` (including `~/.claude`) always ask, except in
  bypass mode — no allow rule pre-approves them. Write memory files with the Write / Edit tools, not
  shell commands.
- **Modes:** coordinator in `default` (or `plan` for the dry run); workers in **`auto`** at the
  autonomous level (the default — no permission prompts, the classifier and the hook as the net) or
  `acceptEdits` at the cautious level. `bypassPermissions` only by the owner's own explicit setup. The `auto` mode's
  classifier reviews actions rules don't settle and honors limits stated in the conversation.
  [permission-modes](https://code.claude.com/docs/en/permission-modes.md)

## Usage and limits
- The status line command receives JSON with `rate_limits.five_hour.used_percentage`,
  `rate_limits.seven_day.used_percentage` and `resets_at` (claude.ai Pro / Max only, after the first
  response). `scripts/statusline.sh` saves it to `~/.claude/coordinator/statusline.json` for the
  coordinator to read. [statusline](https://code.claude.com/docs/en/statusline.md)
- API-key setups have no such data: budget by cost (`--max-budget-usd` on headless jobs) instead.

## Platform notes
- macOS: the background-session host needs the *Files and Folders* and *Local Network* permissions.
- Notifications: `osascript` (macOS), `notify-send` (Linux), PowerShell toast (Windows).
- Windows: Claude Code runs Bash tools through Git Bash; PowerShell 7 is a separate tool.
- "Could not resolve authentication method" on dispatch → `claude daemon stop --any --keep-workers`, then
  check the login.

## New machine — safe start checklist
Run in this order; the first cycle is a **dry run** (CORE §12a).
1. `claude --version` meets the messaging minimum above; `gh auth status` (or the host's CLI) logged in.
2. The owner trusts the project folder (and accepts any one-time consent) — the owner's own step.
3. **Worker safety file** from [settings.example.json](settings.example.json) with absolute paths (passed
   to every worker by `start-worker.sh --safety`). **The coordinator's own settings** (its session or the
   user settings): the status line saving limits (`scripts/statusline.sh`) and
   `crossSessionInbound: "accept"`.
   Secrets net: enable the code host's push protection / secret scanning (or a `gitleaks protect`
   pre-commit) — a leaked token on the host cannot be rolled back, only rotated.
4. Make the scripts executable: `chmod +x scripts/*.sh scripts/hooks/*.sh` (macOS / Linux).
5. Test the hook: `scripts/hooks/safety-guard.sh --self-test` → `safety-guard: OK`.
6. Branch protection on the code host is on (or the owner records why not).
7. Initialize (CORE §2 — the one start sequence): context of running sessions and PRs, open questions,
   the init quiz, `PROJECT.md` / `BINDINGS.md`, the access list and project map, the platform row, the MCP
   tool check.
8. Dry-run cycle → the owner approves → wait for "start a new cycle"; a tick only where autopilot is on.
