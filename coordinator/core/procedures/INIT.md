# Initialization procedure

Part of the coordinator core, moved out of `CORE.md` (§2 details and §3) so it is read only when needed.
Same authority as `CORE.md`; the hard rules of CORE §1 apply.

## Details of the start sequence (CORE §2)


- **Existing project with its own rules** (other agents' formats — `.cursor/rules`, `AGENTS.md`, rule
   files, memory notes, a previous coordinator): the coordinator **defines the project rules** in its
   layers, for **Claude only**:
   - collect every existing rule, rate its importance, find duplicates, contradictions and stale ones,
     and contradictions between docs;
   - propose the merged `PROJECT.md` / `BINDINGS.md` to the owner, item by item where it matters;
   - after the owner's yes, the Claude layers are the single source; retiring the old formats is a
     refactor task the owner approves.
- **Platform:** detect the operating system (Windows, macOS, Linux) and the shells available
  (PowerShell, bash, zsh) on this machine; record them in `BINDINGS.md`. Use that platform's commands,
  paths and tools; never assume another OS. Where a project runs on several machines or OSes, bindings
  give each command per OS, and scripts meant for everyone are written to run on all of them (or say
  which they need). The core itself stays OS-neutral. Detect too whether the project has git, a code
  host and a tracker; anything missing → say so in one line and use the mode in
  [SOURCES.md](SOURCES.md) §3. Then install the core's roles into
  `~/.claude/agents/` (TOOLKIT *Workers*, Roles) — automatic, no question.
- **Safety check:**
  - **Branch protection on the code host** — the main branch requires a PR, passing checks and (in the
    strict preset) a human review. This is the real enforcement; missing → ask the owner to enable it
    (or record why it cannot be, e.g. the plan does not offer it).
  - **Worker safety settings** (`settings.example.json`, Windows `settings.example.windows.json` → a
    file with absolute paths, passed to every worker by `start-worker --safety` / `-Safety`): the deny rules for the protected branches (plus, where
    `PROJECT.md` says humans merge or mark ready, `gh pr merge` / `gh pr ready`) and the safety hook,
    whose self-test must pass before any worker starts. **Stack-specific denies are candidates, not impositions**: offer
    them in the quiz (e.g. `examples/nextjs-supabase/settings.json`) and keep what the owner chooses —
    the coordinator exists to automate, and some "dangerous" actions are exactly what a project needs.
  - **No stalled workers:** workers run in auto mode and do not wait on permission prompts; with the
    cautious override (`acceptEdits`) the owner sets allow rules for routine commands (build,
    lint, test, push to the worker's own branch) at init. If a worker still waits on a prompt, the
    coordinator tells the owner — it never approves a prompt on the owner's behalf.
  - **The worker safety file in this repository is the swarm and standard baseline** (protected
    branches and irreversible removals only); the strict preset adds denies to it (merging, marking
    ready, production write tools), the cautious override more (reading env files, the chosen stack
    candidates). Claude Code's auto mode may refuse to let a session weaken
    permission files by itself — then the owner adds an allow rule for the coordinator's folder
    (`/permissions`) or makes the edit.
  - **Every MCP connector:** list its tools so the owner knows what can write where — in the strict
    preset confirm the production write tools are denied, in swarm and standard they stay available
    (§1.4); every MCP the work depends on has a CLI fallback.
  - **Production backup** (swarm and standard): the backup command and destination are written in
    `BINDINGS.md` before the first production write that git cannot undo; no recorded way to back up →
    ask the owner once, record it, then proceed.

## 3. Setup interview — the init quiz

A **quiz**, run every time the coordinator is initialized in a session. Few questions, many findings:
what only the owner can decide is **asked**; what the project already shows is **found and confirmed**
in one table; the rest is **by default** (the strictness preset, CORE §3, and `PROJECT.template.md`),
not asked, but shown in the summary. The quiz is complete (CORE §1.12) when every question below that
applies has an answer and the owner said yes to the summary.

**How to ask.** Research first: study the project read-only (code, docs, git history, CI, existing
rules and coordinators, connected tools) so every question comes with an informed recommendation. Ask
with the multiple-choice question tool (in Claude Code: AskUserQuestion), 2–4 questions per round, in
the owner's language: each has 2–4 concrete options, the **recommended option first, marked
"(Recommended)"**, with a one-line reason from the research; the owner can always answer in their own
words. Keep a list of open questions; an unclear answer goes back on it.

### 3.1 Asked — only the owner can decide

1. **Strictness — swarm, standard or strict?** Show the presets table (CORE §3) and recommend from the
   research: *swarm* for an own project in a rush; *standard* when the owner wants to approve each block
   and the risky merges; *strict* for client work reviewed by people. In every preset a worker works on
   its own inside its task and returns a finished PR. Then: which rows, if any, this project overrides.
2. **What we build:** the product, for whom, the current milestone and what "done" means for it —
   proposed from the README, the board, a plan document.
3. **Who is who:** solo or team; who owns the scope; who accepts work, how and where (the owner, QA, a
   business analyst, the client; preview, a device).
4. **Sources** ([SOURCES.md](SOURCES.md)): the sources-of-truth table with what was found filled in —
   confirm or fix rows; then "send everything useful you have" for the catalog.
5. **State repository — where does it live?** `coordinator/` is always a local git (3.4); the question
   is its remote, **always private**: *the owner's account* (recommended solo) —
   `gh repo create <owner>/<project>-coordinator-state --private`; *the team's organization*
   (recommended for a team); *local only* — warn: a lost disk or a move without a handoff loses the
   journal, the rules and the decisions. With a remote: verify `PRIVATE` (`gh repo view --json
   visibility`), push after every cycle, record the link in `BINDINGS.md`, offer a pointer the next
   machine can find (HANDOFF, *Finding it*).
6. **Risky areas:** money, roles and permissions, personal data, audit — what was found in the code
   filled in; confirm (CORE §5).
7. **Production** — only if the project has one: real users or money; the backup before an
   irreversible change (command, destination); release flags for what acts on real users.
8. **Budget:** usage caps (default: weekly 90%, short window 95%, over only with permission) and the
   models available (CORE §7).
9. **Status line** — only when this Claude Code has none (no `statusLine` in the user or project
   settings): show `Opus 5.5 (high) · ctx 837k/1M (84%) · 5h 14% · week 92%`. *Yes* (recommended — the
   coordinator reads its limits from it, §9): install the script from the core copy (TOOLKIT.md).
   *No:* nothing installed; the coordinator asks about limits when it matters.

Asked only when they apply:
- **Strict preset:** the production actions allowed by name; time estimates and tracking; what the
  client owes (accesses, accounts, approvals — `docs/dependencies.md`).
- **A project tied to a device** (a headset, hardware): what must not run while the owner tests.
- **Language** of the conversation and of artifacts — only if the owner's messages leave it unclear.
- **Worker permission mode** — only if the owner asks for prompts: `acceptEdits` (cautious) instead of
  `auto`.

### 3.2 Found and confirmed — one table

Stack; build / run / test / lint commands; environments and deploy; CI layout; MCP servers and their
CLI fallbacks, with each one's write tools (CORE §1.4); tracker, statuses, labels, hierarchy; branch
and commit format (from history; none → the template's); coding standards (linter configs, docs);
per-worker isolation (working copies, ports, databases, containers); shared resources; serial
identifiers (migration versions); the machine's platform; technologies without a connected MCP server
(code host, tracker, design, database, hosting, docs, browser) — propose the matching servers, the owner
decides what is connected and with which rights; whether git, a code host and a tracker exist
(missing → [SOURCES.md](SOURCES.md) §3). Existing rules in other formats are merged as *Details of the
start sequence* says, and every place they differ from the core is one line in the summary.

### 3.3 By default — not asked, shown in the summary

Everything else in `PROJECT.template.md`: what follows from the strictness preset (parallel workers,
who merges, review, the approval gate, obvious review comments, autopilot and the tick, code to other
services, reports) and the template's defaults (session names, the cycle recipe, commit and PR format,
progress comments, research, the radar list and the expectations checklist (CORE §4b), risk-area
rules, status page, issue fields). The owner changes any of them
with one line at the summary or later.

### 3.4 Set up without asking

- **The core** is always a local copy: clone claude-coordinator to a scratch folder, copy its files into
  `coordinator/core/`, record the commit in `PROJECT.md`. Scripts, the hook, the status line and the
  roles run from there. It may be **edited for this project**; upstream changes come in only on "update
  the core" ([UPDATE-CORE.md](UPDATE-CORE.md)). A local edit useful for every project is proposed back
  to claude-coordinator (§10), with the owner's yes.
- **`coordinator/`** is the coordinator's own local git repository (the **state repository**, §12b),
  ignored by the project's git through `.git/info/exclude`, so the project's history, PRs and the client
  see none of it. Create it if missing — `git -C coordinator init`, add `coordinator/` to the project's
  `.git/info/exclude`, first commit (layout: [state-repo/README.template.md](../state-repo/README.template.md));
  commit at the end of every cycle and push where it has a remote. The owner may choose to commit the
  files into the project instead — recorded in `PROJECT.md`.
- **The voice** is not asked: artifacts are always in the owner's voice, with no AI co-author lines
  (§11).

### 3.5 The summary

Write `PROJECT.md` / `BINDINGS.md` from the answers, the findings and the defaults; show one summary
grouped by section — asked, found, by default, and the differences with existing rules — and ask "yes,
or fix these rows". Then the dry run (CORE §12a).

### 3.6 Scenarios

- **An empty folder or just an idea.** Say in one line what is missing (no git, no code, no tracker).
  Ask questions 1–3 (the idea may be one line, "an online shop") and "send everything useful you have"
  (a brief, designs, links, a product to copy or replace). Then, **with the owner's yes, research**: one
  or more researcher subagents or sessions (§4b) on how it can be built — candidate stacks,
  architecture, hosting, services, costs and limits — returned as **an options table with a
  recommendation** and the first radar list of competitors and references (CORE §4b); the owner decides,
  and each decision goes into the decision log with its reason.
  Then **propose the infrastructure, one yes each**: `git init`; a private repository on the code host;
  a board with the statuses (or `TASKS.md`); a CI skeleton with the definition-of-done checks; branch
  protection; the state repository (question 5). Risky areas and production (6, 7) wait until there is
  code that has them. The first tasks of the first cycle are the project skeleton, CI and the board's
  first epics — after the dry run and the owner's "start a new cycle".
- **An existing project without a coordinator.** Read everything first (code, history, CI, PRs, the
  board, README / CLAUDE.md / docs, the project's own workflow and decision documents, connected MCP,
  the platform, any device). One table of findings (3.2) with the project's own conventions kept as its
  rules (e.g. its branch and commit format); then the asked questions with what was found filled in;
  the summary lists where the project's rules and the core differ, one line each.
- **A project with an older coordinator** (rules in the repository, a remote core, no `coordinator/`):
  no full quiz — the migration plan ([MIGRATE-LAYOUT.md](MIGRATE-LAYOUT.md)) for one yes, the old
  answers mapped to a strictness preset (CORE §3), then only what the old rules did not cover (the state
  repository, the sources of truth).
- **A new machine or a teammate** (a state repository exists): find it (HANDOFF, *Finding it*); no
  access → name the repository and whom to ask, stop. With access: clone it, re-detect the "re-detect"
  rows, and instead of the quiz one "still so?" per section; then the dry run.
