# Moving to another machine

Part of the coordinator core, moved out of `CORE.md` (§12b) so it is read only when needed.
Same authority as `CORE.md`; the hard rules of CORE §1 apply.

## 12b. Moving to another machine — the state repository

A coordinator moves to another machine through the project's **state repository**: an **always
private** repository (`<project>-coordinator-state`, in the owner's account or the team's organization)
holding the coordinator's files —
`PROJECT.md`, `BINDINGS.md`, `LOG.md`, `HANDOFF.md` — and an entry `README.md` from
`state-repo/README.template.md`.

- **Day to day:** the project's `coordinator/` folder *is* the state repository — a local git
  repository, excluded from the project's git (`.git/info/exclude`), committed at the end of every cycle
  (one line in the report). With a remote (`PROJECT.md`, chosen at init) it is pushed after every
  commit: `git pull --ff-only` first; the remote ahead and not fast-forwardable (another machine's or a
  teammate's coordinator wrote it) → stop and ask the owner, never force-push it. Before every push
  check it is still `PRIVATE`; public → stop and tell the owner. Local only → a lost disk loses it.
- **Finding it** (a new machine, a teammate's fresh clone, no `coordinator/`): in this order — the
  pointer the project carries if the owner chose one at init (a line `Coordinator state: <url>` in the
  project's `CLAUDE.md`, added through the project's change path; never in a client's repository);
  `gh repo view <owner>/<project>-coordinator-state` and the same name in the project repository's
  organization; else ask the owner for the link. Found but not readable (404 / 403 with this login) →
  tell the owner the repository and that its owner must grant access; work stops until then. Nothing
  found and the owner confirms there is none → the init quiz as for a new project.
- **The handoff — only on the owner's command** ("prepare a handoff", "update the state repo"); moves
  are rare, and the command is the owner's yes for every step below, done without further questions.
  1. Tell every running worker to commit and push its work; list anything that cannot be pushed and
     stop until the owner decides.
  2. Write `HANDOFF.md` (format: `HANDOFF.template.md`): the moment, every task in flight with its
     branch, PR and next step, the merge queue, open questions, accesses and logins the new machine
     needs, machine-specific settings to recreate.
  3. Mark machine-specific rows of `BINDINGS.md` "re-detect"; commit `coordinator/` and push it to its
     remote — created if missing (local only until now) where the owner chooses, the owner's account or
     the team's organization
     (`gh repo create <owner-or-org>/<project>-coordinator-state --private`); verify it is private
     (`gh repo view --json visibility` → `PRIVATE`); record in `HANDOFF.md` the claude-coordinator commit
     the coordinator ran on; commit and push.
  4. Report the link. Running sessions do not move — they are restarted on the new machine from their
     branches.
- **On the new machine** the owner gives the state repository's link, or the session finds it
  (above). The session reads its
  `README.md`, becomes the coordinator, loads this core, and runs the start sequence (§2) with the
  context already known: clone the project, clone the state repository into its `coordinator/` and add
  `coordinator/` to the project's `.git/info/exclude`, re-detect the platform and the "re-detect" rows, check the
  logins and the safety setup, confirm the quiz briefly, dry run (§12a) — including which interrupted
  tasks it would restart — then wait for the owner.
- After the move only the new machine's coordinator writes the state repository; the old one stops (a
  push it still tries fails the fast-forward check above). The state repository holds internal project
  information: **always private**, never shared with the client.
