# Sources — where the truth lives and what else to draw on

Part of the coordinator core, read at init and whenever a source is added, changes or contradicts
another. Same authority as `CORE.md`; the hard rules of CORE §1 apply (§1.13 above all).

## 1. Sources of truth — one main system per kind

Asked at init (INIT 3.1, question 4), recorded in `PROJECT.md` (*Sources of truth*). First detect what exists:
the repository, its remote, the connected MCP servers (tracker, design, docs), links in README /
CLAUDE.md / the docs. Then show the table with a proposal per row and ask "confirm, or fix rows":

| Kind | Main system (proposed) | The coordinator may |
|---|---|---|
| Code and review | e.g. GitHub `org/app` | read, write through PRs |
| Tasks and statuses | e.g. Jira `APP`, GitHub Issues, or `TASKS.md` (§3) | read, change status |
| Requirements | e.g. the Google Doc "Spec v3" | read only |
| Decisions and their reasons | e.g. `docs/decisions.md` | write through the change path |
| Acceptance | e.g. the business analyst's Jira comment | read only |
| Design | e.g. Figma file `…` | read only |
| The coordinator's memory | the state repository (`coordinator/`) | write |

- **One main system per kind;** anything else holding the same kind is a mirror. Main and mirror
  disagree → the main one wins, the coordinator tells the owner and never "fixes" the main one from a
  mirror.
- **Rights are explicit per row.** "Read only" (often a client's document) → the coordinator proposes
  the edit to the owner and changes nothing there.
- **No connector to a main system** → say so ("Jira is not connected: I cannot see the tasks"), offer
  to connect it (CORE §4c) or ask the owner for excerpts; never work from memory of it.
- Worker briefs point to this table: where requirements come from, where a status is written.

## 2. The source catalog — everything else that helps

`coordinator/SOURCES.md` lists every source the owner gives, at init ("send everything useful you
have") and at any time later: links (Figma, documents, the owner's and the client's repositories,
reference apps) and files (screenshots of a chat, a photo of a screen, PDFs). Files are kept in
`coordinator/sources/` — the state repository is private and never reaches the client.

1. **Take it in.** A link is recorded; a file is copied into `coordinator/sources/` under its id.
2. **Read it.** Text in an image is transcribed; what cannot be read for sure is marked
   `[unclear: …]`, never guessed. Documents and designs are read through their connector; none → say
   so (§1).
3. **Mask sensitive data in the transcription** — phone numbers, emails, addresses, amounts tied to a
   person, tokens, passwords: `[phone]`, `[email]`, `[token]`. Briefs, PRs, issues and comments get
   the masked summary only, never the raw file (CORE §1.13). A secret found in a source → tell the
   owner that it should be rotated.
4. **Propose one line, the owner confirms:**

   | id | what | where | kind | weight | summary | read on | replaces |
   |---|---|---|---|---|---|---|---|
   | S-7 | screenshot of the chat with the client, 10 Oct | `sources/S-7.png` | client decision | evidence | the client asks to drop sign-up by phone | 2026-10-11 | — |

5. **Weight.** *Main* — only what §1 names. *Evidence* — chats, photos, what the client said, meeting
   notes. *Reference* — examples, other people's code, articles. Evidence or reference that
   contradicts a main source is a **question to the owner**, never resolved by the coordinator.
6. **Superseded.** A newer version (Spec v3 after v2) marks the older `replaced by S-9`; replaced
   sources are not given to workers. A newer chat overrides an older document only on the owner's yes.
7. **Conflicts become decisions.** The owner's answer is written as a decision in the decision log
   (main system from §1) citing both sources, so the question does not come back.
8. **Fresh and cheap.** The summary is the cached reading: a source is re-read only when it changed
   since `read on` (document history, file date, commit) and before a task that relies on it — not in
   every task.

Nothing is collected beyond what the owner gives and what the project already links to: no sweeps of a
whole drive, mailbox or chat history.

## 3. Missing tools — no tracker, no code host, no git

Detected at init (INIT, *Platform* and 3.2), said to the owner in one line, recorded in `PROJECT.md`:

- **No tracker** → the tasks live in `coordinator/TASKS.md`: one row per task — `T-<n>`, title,
  status (Backlog / Todo / In Progress / Testing / Done), branch, PR or commit, sources (`S-<n>`). It is
  the main system for tasks (§1); the coordinator moves rows as it would cards. A tracker later → the
  tasks move there on the owner's yes and the file is retired.
- **Git, no code host** → no PRs and no CI: each worker still has its own branch and worktree; the
  verifier reviews the branch diff and runs the checks locally; merging per the strictness (CORE §3),
  locally. Only the hook guards the protected branches (no branch protection — say so at init). The
  state repository can only be local (warn as INIT says).
- **No git** → propose `git init` in the project (the owner's yes; without it there are no branches,
  worktrees, rollback or parallel workers). Refused → one worker at a time in the folder itself, a copy
  of the folder outside the project before each task; the swarm preset is not available.

## 4. In contracts and reviews

- A contract's `inputs` name the sources by id (`S-3 Figma: payment screen`); each acceptance
  criterion carries `source:` — the id it comes from (CONTRACT.md). A criterion with no source is the
  coordinator's own reading: say so in the brief.
- The worker gets each source's summary and link, not the whole catalog; a fact taken from a
  transcribed image is reported as `claim`.
- The verifier checks each criterion against its cited source, not against its own idea of the task
  (roles/verifier.md).
