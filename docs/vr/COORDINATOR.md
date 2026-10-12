# The coordinator — moved to `coordinator/`

Since 2026-10-11 the Arx Fatalis VR coordinator runs on a **local copy of the reusable coordinator
core** ([Arziburst/claude-coordinator](https://github.com/Arziburst/claude-coordinator)):

- [coordinator/core/CORE.md](../../coordinator/core/CORE.md) — the core rules (layer 1; version in `core/VERSION`);
- [coordinator/PROJECT.md](../../coordinator/PROJECT.md) — the Arx rules (layer 2);
- [coordinator/BINDINGS.md](../../coordinator/BINDINGS.md) — the Arx tools and commands (layer 3);
- [coordinator/core/TOOLKIT.md](../../coordinator/core/TOOLKIT.md) — Claude Code mechanics.

Start a coordinator with `/coordinator` in the owner's copy.

## Where the old sections went (links in DECISIONS.md and elsewhere)
| Old section | Now |
|---|---|
| A1 Role, global rules, strict mode, UX is the owner's | CORE §1, §10; PROJECT.md *The approved plan* |
| A2 Bootstrap | CORE §2 (the one start sequence), §3 (init quiz) |
| A3 Autopilot and budget | CORE §9; PROJECT.md *Project*, *Budget and models* |
| A4 The cycle (conflict foresight, review, merge queue, cleanup, triage) | CORE §4, §4a, §6, §8; PROJECT.md *The cycle*, *The change flow* |
| A5 Models and effort | CORE §7; PROJECT.md *Budget and models* |
| A6 Owner tests | CORE §4 step 6; PROJECT.md *Acceptance — the owner's headset test* |
| A7 Rules enforced on workers | CORE §1.9, §1.13, §8; BINDINGS.md *Project-only rules* |
| A8 Research | CORE §4b; PROJECT.md *Research* |
| A9 Tracker, PRs, docs | CORE §11; PROJECT.md *Tracking*, *The change flow* |
| A10 Stop and report | CORE §12; PROJECT.md *The cycle* step 6 |
| B1–B9 bindings | BINDINGS.md; models table (B5) in PROJECT.md *Budget and models*; headset procedure (B6) in PROJECT.md *Acceptance* |
