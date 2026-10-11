# "Update the core"

Run only on the owner's command ("update the core", "обнови ядро", any wording). The project's own rules
(`PROJECT.md`, `BINDINGS.md`) are never rewritten by an update; they keep winning over the core's defaults.

1. **Which core.** Read the claude-coordinator commit recorded in `PROJECT.md` for `coordinator/core/`.
   No `coordinator/core/` (an older remote core) → the plan is to create it at the latest commit
   (MIGRATE-LAYOUT), shown in the report.
2. **What changed upstream:** clone claude-coordinator to a scratch folder; the commits and files since
   the recorded commit. The core is every file a coordinator reads: `CORE.md`, `TOOLKIT.md`,
   `CONTRACT.md`, `roles/`, `procedures/`, the scripts.
3. **Impact report to the owner, before changing anything:**
   - per change: what it does and what it changes in this project's work (briefs, review, models, init);
   - every line of `PROJECT.md` / `BINDINGS.md` that now overlaps or disagrees with the new core — the
     project rule still wins, the owner decides whether to keep, adapt or drop it;
   - each upstream change that conflicts with a local edit, and the proposed resolution;
4. **On the owner's yes:** merge the upstream files into the local copy, keeping the agreed local edits,
   record the new commit in `PROJECT.md`, commit in `coordinator/`, and re-read the core. If the
   scripts changed, re-run the hook's self-test. Then
   install the roles into `~/.claude/agents/` (TOOLKIT *Workers*, Roles) — automatic, no separate question. Changes to `PROJECT.md` the owner approved are committed in `coordinator/`.
5. **Layout:** if `coordinator/` is not yet its own git repository hidden from the project's git
   ([INIT.md](INIT.md)), include the migration plan in the report and run it on the owner's yes:
   [MIGRATE-LAYOUT.md](MIGRATE-LAYOUT.md) (files tracked by the project's git leave it through the
   project's change path and stay on disk).
6. **Journal** the update: the old and new commits and what the owner decided per item. Tell running
   sessions to re-read their brief's rules only where the update changes them.
