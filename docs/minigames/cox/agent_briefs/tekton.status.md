# Tekton room status

Branch: `cursor/cox-tekton-runaround-da39`
Agent: `bc-6e8e01ee` (replacement for stuck `bc-d8bc5247`)

## Strategy (owner 2026-10-07)

**Synq run-around cycle** (`synq_transcript.md` [1:16:04] / [1:17:50]):
lure far from anvil, counterclockwise attacks on pre-corner tiles, spark dodge
on anvil, re-engage. Not stand-and-tank.

## Implemented

- Test SM: `LAND → LURE → BAIT → CYCLE ⇄ ANVIL_DODGE → REENGAGE → DONE`
- Spec assertions retained (`test/raids/cox_tekton.lua`)
- Content: `cox_tekton.rs2` (submodule `dda1f15`)
- Setup: shark 10 (unstackable), rune plate for unprotected sample hit
- BAIT state: one unprotected wedge hit → Protect from Melee → cycle

## Gate

- In progress under `flock` + `QUEST_BINARY=src/torirs_tekton --no-build --no-publish`
