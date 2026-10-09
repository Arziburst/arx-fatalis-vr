---
description: The owner tested a task in VR and accepts it — merge its pull request and close the task
---

The project owner tested a `needs-vr-test` pull request in the headset and accepts it.
Read `docs/vr/WORKFLOW.md` first.

1. Find the pull request: the one named in `$ARGUMENTS` (PR or issue number), otherwise the task in
   **Testing** (`.\scripts\dev\next-task.ps1`) and its open PR (`gh api "search/issues?q=repo:Arziburst/arx-fatalis-vr+is:pr+is:open+<issue number>"`).
   If more than one candidate exists, ask which one.
2. Check CI: `.scriptsdevpr-checks.ps1 <pr>` (exit 0 = green). If it is not green, do not merge — tell the owner why.
3. Comment on the PR: "Tested in VR and accepted by the project owner." plus any notes from `$ARGUMENTS`.
4. Merge: `gh pr merge <pr> --merge --delete-branch --subject "<PR title> (#<pr>)"`, then return the
   working copy to `main`:
   `git switch main; git pull --ff-only`.
5. `.\scripts\dev\set-status.ps1 -Issue <n> -Status Done` (the issue closes through `Closes #<n>`; close it
   with `gh issue close` if it did not).
6. If the task was the last open task of its epic and the epic's definition of done is met, say so and
   suggest closing the epic.
7. Report in Russian: what was merged. The owner goes back to the coordinator session for the next task.
