# Migrating `coordinator/` to its own local git repository

Run from "update the core" (step 5 of [UPDATE-CORE.md](UPDATE-CORE.md)) when `coordinator/` is not yet
the coordinator's own git repository hidden from the project's git ([INIT.md](INIT.md)).
**Nothing is moved, untracked or deleted before the owner's yes to the exact plan.** No file is ever
lost: everything that leaves the project's git stays on disk and in the new repository.

## 1. Detect and report

Run read-only, in the project root:
- `git ls-files coordinator/` — files the project's git tracks there (case **tracked**);
- `git check-ignore -q coordinator/ && echo ignored` — ignored by `.gitignore` (case **ignored**);
- otherwise untracked (case **untracked**);
- `git -C coordinator rev-parse --show-toplevel` and `git -C coordinator remote -v` — whether
  `coordinator/` already is a repository; a remote pointing to claude-coordinator means the core was
  copied as the whole folder (case **core clone**);
- `grep -n coordinator/ CLAUDE.md` — project files that point into the folder;
- `test -d coordinator/core` — missing means an older remote core (case **no local core**).

Show the owner: the case, the files involved, the exact commands below, what changes for the project's
other clones and teammates, and the one yes this needs.

## 2. On the owner's yes

1. **Back up** the folder outside the project (e.g. `cp -a coordinator <scratch>/coordinator-backup`).
2. **Core clone only:** record its commit (`git -C coordinator rev-parse HEAD`), move the core's files
   into `coordinator/core/` (keep `PROJECT.md`, `BINDINGS.md`, `LOG.md`, `HANDOFF.md` at the top),
   remove `coordinator/.git`, and record in `PROJECT.md` a local core at that commit.
2a. **No local core:** clone claude-coordinator to a scratch folder, copy its files into
   `coordinator/core/`, record that commit in `PROJECT.md`, and point the worker safety file, the
   status line and `BINDINGS.md` at `coordinator/core/scripts/`.
3. **Create the repository:** `git -C coordinator init`, add the entry `README.md` from
   `state-repo/README.template.md` if missing, `git -C coordinator add -A`, commit.
4. **Tracked only — untrack in the project, keep on disk:** in a separate worktree of the project
   (`git worktree add <scratch>/untrack -b <branch>`, so this working copy is never switched),
   `git rm -r --cached coordinator/`, commit, and integrate through the project's change path (PR,
   review, merge per `PROJECT.md`, §1.1). Pulling that merge deletes the files from this working copy
   too; restore them at once from the new repository: `git -C coordinator restore .`. If the
   folder is in `.gitignore`, leave it; otherwise add `coordinator/` to `.git/info/exclude`.
   Tell the owner before the merge: **other clones of the project lose these files from their working
   copy on their next pull**, and the files remain in the project's past history (rewriting history is
   a separate decision, never part of this migration).
5. **Untracked or ignored:** add `coordinator/` to `.git/info/exclude` unless `.gitignore` covers it.
6. **Pointers:** a project `CLAUDE.md` line pointing into `coordinator/` now works only on this machine;
   propose to the owner whether to keep it (it is a project change either way, §1.1).
7. **Verify:** `git status` in the project shows nothing under `coordinator/`; `git -C coordinator log`
   shows the commit; `diff -r -x .git` against the backup shows the files intact (except the step-2 moves).
   Only then delete the backup.
8. **Journal** the migration with the case, the commits and the project PR if any.
