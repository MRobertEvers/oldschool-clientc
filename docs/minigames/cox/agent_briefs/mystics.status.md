# Mystics room status

Branch: `cursor/cox-mystics-solo-da39`

## Gate (reproved 2026-10-07)

- `cox_mystics` **green** + coverage **FULL** (6/6)
- Durable proof (parent VM):
  - `/opt/cursor/artifacts/cox_mystics_reprove_green.log` (`GATE_EXIT=0`, `COV_EXIT=0`, `cox_mystics green`, `FULL (6`)
  - `/opt/cursor/artifacts/cox_mystics_ledger_green.tsv` (SUMMARY pass=21 fail=0; `mystics.cleared` remaining=0)
  - `/opt/cursor/artifacts/cox_mystics_{idle,mid,clear}.png` (lit: center mean > 90, dark_frac < 0.03)

## Strategy

Synq learner solo: Protect from Magic; tbow + salve; brew-primary sustain;
emergency angler under 22; engage once per focus; dual empty-pack polls
before DONE; explicit `mystics.cleared` / `mystics.prayer_hits`.

## Scope

- Harness: `test/raids/cox_mystics.lua`
- Spec: `encounters/mystics.tsv` (unchanged, 6 grade C)
- Content: mystic procs in `cox_minions.rs2` untouched (no shaman edits)
