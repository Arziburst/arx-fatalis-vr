# claude-coordinator

> **If you are a Claude session and were given a link to this repository: you are now the
> coordinator.**
> 1. Read [CORE.md](CORE.md) completely
>    (raw: `https://raw.githubusercontent.com/Arziburst/claude-coordinator/main/CORE.md`).
>    Its rules override your default habits; §1 are hard rules.
> 2. Follow **the one start sequence** in CORE §2: identity → one coordinator per project → find the
>    project files → learn the context (sessions, PRs, history) → open questions → the **init quiz**
>    (multiple choice with a recommended option; first "strictness: swarm, standard or strict?") → platform and safety → **dry run** → **wait** for the owner's explicit "start a new
>    cycle". Start no work until every quiz question is answered (CORE §1.12); never start a cycle on
>    your own.
> 3. Templates: [PROJECT.template.md](PROJECT.template.md), [BINDINGS.template.md](BINDINGS.template.md);
>    machine mechanics and the new-machine checklist: [TOOLKIT.md](TOOLKIT.md). The
>    [examples](examples/) are illustrations, never rules.

A reusable **coordinator** for Claude Code: one Claude session that runs a software project — plans the
work, delegates it to worker sessions, controls and reviews the result, integrates it per the project's
rules and reports to the owner. Works for any project and stack, solo or in a team, own or client work.

## Layers
- [CORE.md](CORE.md) — who the coordinator is and how it works; the same everywhere.
- [PROJECT.template.md](PROJECT.template.md) → `coordinator/PROJECT.md` in the project — how this
  project works: the cycle, tracking, the change flow, review, merging, acceptance, environments,
  resources, budget, voice, language, MCP. Filled in a setup interview with the owner.
- [BINDINGS.template.md](BINDINGS.template.md) → `coordinator/BINDINGS.md` — the concrete commands and tools.
- [CONTRACT.md](CONTRACT.md) — the unit of delegated work: the contract a worker gets, the report it
  returns, and the evidence classes (oracle / review / claim).
- [roles/](roles/) — role definitions: [worker.md](roles/worker.md) implements a contract,
  [tester.md](roles/tester.md) writes its failing tests first,
  [verifier.md](roles/verifier.md) reviews it independently, [scout.md](roles/scout.md) condenses long
  logs and threads on the light model, [researcher.md](roles/researcher.md) returns a catalog of what to take.
- [procedures/](procedures/) — parts of the core read only when needed: the init quiz
  ([INIT.md](procedures/INIT.md)), the move to another machine ([HANDOFF.md](procedures/HANDOFF.md)), the answer to
  "what is your task?" ([WHAT-IS-YOUR-TASK.md](procedures/WHAT-IS-YOUR-TASK.md)), "update the core"
  ([UPDATE-CORE.md](procedures/UPDATE-CORE.md), with [MIGRATE-LAYOUT.md](procedures/MIGRATE-LAYOUT.md)), the sources of
  truth, the source catalog and projects without a tracker or git ([SOURCES.md](procedures/SOURCES.md)).

## Toolkit (any machine — Windows, macOS, Linux)
- [TOOLKIT.md](TOOLKIT.md) — Claude Code mechanics for a coordinator: named background workers,
  messages, the tick, safety rules and hooks, limits, platform notes, and the **new machine checklist**.
- [scripts/](scripts/) — reference scripts, one style (TOOLKIT *Scripts*: a `.sh` + `.ps1` pair, nothing to install): `start-worker` (named session
  in its own worktree), `cleanup-worker` (only clean and pushed work), `hooks/safety-guard`
  (guards only the irreversible: pushes / force-pushes / deletes of main, mirror pushes, removing `/`,
  home or a worktree by hand — work branches stay free), `statusline`
  (the status line `Opus 5.5 (high) · ctx 837k/1M (84%) · 5h 14% · week 92%`, offered in the init quiz;
  also saves usage limits for the coordinator). Run with `--dry-run` / `-DryRun` first on a new machine.
- [settings.example.json](settings.example.json) / [settings.example.windows.json](settings.example.windows.json)
  — the worker safety file (passed by `start-worker --safety` / `-Safety`): protected-branch denies, the hook and its self-test, messages accepted — the swarm and
  standard baseline; the strict preset and the cautious override add denies.

## Examples
- [examples/nextjs-supabase/](examples/nextjs-supabase/) — client web app (Next.js + Supabase, Vercel, a
  team with a business analyst, humans merge, "announce and wait" cycle).
- [examples/arx-fatalis-vr/](examples/arx-fatalis-vr/) — own game project (VR port, swarm preset, autopilot
  cycle with ~10 parallel workers).

## Use
- **Any project:** open a Claude Code session in the project and give it the link to this repository —
  it becomes the coordinator and initializes. The coordinator keeps its files in the project's
  `coordinator/` folder, its own local git repository hidden from the project's git (CORE §12b).
- **The core is a local copy** in `coordinator/core/`, like a pinned package: editable for that project;
  at start the coordinator says when upstream is newer, and "update the core" brings the changes in on
  command, with an impact report first.
- **"Start a new cycle"** — the coordinator runs the project's cycle recipe (CORE §4).
- **Moving to another machine** — `coordinator/` is pushed after every cycle to a private remote, the
  project's state repository, if the owner chose one at init; say "prepare a handoff" before a move: the
  coordinator pushes the work in flight and writes `HANDOFF.md` (CORE §12b, `state-repo/README.template.md`,
  `HANDOFF.template.md`). On the new machine give a session only that repository's link.
- **"Coordinator, what is your task?"** — it explains its full flow and the rules in effect (CORE §13).

## Changing the core
Universal lessons go into `CORE.md` with the owner's yes (CORE §10); project lessons stay in the
project's `coordinator/PROJECT.md`. Before a pull request: `scripts/check-core.sh` (Windows:
`scripts\check-core.ps1`). A new or changed script keeps the one style of TOOLKIT *Scripts*.
