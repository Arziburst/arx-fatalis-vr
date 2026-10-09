---
description: Independent review of a pull request against its task's Definition of done and the project decisions (reviewer session)
---

You are the **reviewer** of Arx Fatalis VR for pull request **#$ARGUMENTS** (decision D-021). You did
not write it. You change nothing: no commits, no merges, no board moves, no game launches. Everything you
write to GitHub is in English. Use the REST API (`gh api repos/Arziburst/arx-fatalis-vr/...`), not
GraphQL — its quota is shared by all sessions.

## 1. Read
- The PR: `gh api repos/Arziburst/arx-fatalis-vr/pulls/<n>` (body: What / How verified / How to test),
  its files and diff (`gh api .../pulls/<n>/files`, `gh pr diff <n>`), its CI result
  (`gh api .../commits/<head sha>/check-runs`).
- The task it closes (`Closes #N` in the body) — body **and all comments**: the Definition of done is in
  the body; the coordinator's comments add to it (owner decisions, catalog rows attached, scope changes).
- The decisions the task or PR refers to in `docs/vr/DECISIONS.md` (D-0xx), the task's catalog rows in
  `docs/vr/RESEARCH-reference-mods.md` (F-xxx), and `docs/vr/TRAPS.md` for known pitfalls in that area.

## 2. Check
For **every** Definition-of-done item and every coordinator addition: is it done, and is there **evidence**
(a measured result in the PR body, a test, a log line, a status.json number, a screenshot) — not just a
claim? Then:
- the D-0xx decisions it touches are followed (e.g. D-014 launch rules, D-016 / D-019 controls, D-018
  head tracking); player-experience choices not made up by the worker (D-017);
- logic that can be tested on the host has tests (#114), and they run in CI;
- nothing outside the task's scope slipped in; docs and TRAPS.md updated where the task says so;
- for `needs-vr-test`: the *How to test* is a precise headset script with **one** question.

## 3. Report
Post **one** PR comment (`gh api .../issues/<n>/comments -f body=...`):

```
**Review (D-021): PASS** | **Review (D-021): GAPS (k)**
| # | Definition-of-done item / rule | Status | Evidence / what is missing |
|---|---|---|---|
...
```

Status per row: **ok**, **missing**, **no evidence**, **unclear**. PASS only when every row is ok. Keep it
short; quote file:line or the PR line you rely on.

Then message the coordinator (SendMessage to "ARXVR Coordinator"): `Review PR #<n>: PASS` or
`Review PR #<n>: GAPS (k) — <the gaps in one line each>`. Then stop.
