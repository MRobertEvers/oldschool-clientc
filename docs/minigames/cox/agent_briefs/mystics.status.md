# Mystics room status

Branch: `cursor/cox-mystics-solo-da39`

Replacement for stuck agent `bc-d3cf9a9c` (empty transcript, no commits).

## Gate

- `cox_mystics` **green** + coverage **FULL** (6/6 in-scope spec rows)
- Proved: run30/run31 under `flock /tmp/cox_raid_gate.lock` with
  `QUEST_BINARY=/workspace/src/torirs_mystics_qt --no-build --no-publish`

## Strategy (owner 2026-10-07)

**Synq learner solo** (`synq_transcript.md` [0:31:54]): Protect from Magic,
ranged + salve, focus one mystic at a time. Twisted bow (not blowpipe — short
range walks into melee). Brew-primary sustain (potions do not add weapon
delay); emergency angler only under 22 hp. Engage once per focus, pack
world-slot drop counts the kill.

## Implemented

- Test SM: `LAND → ARM_PRAYER → FOCUS → DONE` (per-tick FOCUS)
- Kit: tbow + salve + masori; 5 brew + 5 restore + 14 angler + 2 karambwan
- Prayer-reduction samples magic-style hits only
- Spec table: all six kill-path rows grade C
- Content: existing mystic procs in `cox_minions.rs2` (no shaman edits)
