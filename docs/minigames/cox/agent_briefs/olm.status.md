# Olm room status

Branch: `cursor/cox-olm-solo-4t41-a9fc` (base `cursor/cox-raid-rooms-da39`)

## Gate

- `cox_olm_solo_4t41`: **green** — ledger pass=15 fail=0; `sm.done` head_dead=true;
  `spec.olm.phases_solo`=4; coverage FULL (8 in-scope rows)
- Artefacts: `/opt/cursor/artifacts/cox_olm_solo_4t41_{run,gate,coverage}.log`

## Strategy (owner 2026-10-07)

1. **Solo Melee 4-tick 4:1** first (`synq_transcript.md` [2:48:05]) — done green
2. Then **duo**, then **trio** — each its own harness
3. Explicit state machine (not scythe spam)
4. Prayer flick on `%varp6766_cox_olm_style` (magic/ranged); no godmode

## Implemented

- `test/raids/cox_olm_solo_4t41.lua` — SM:
  `ENTER → WAIT_SPAWN → KILL_MAGE → SETUP_41 → CYCLE_{TANK,FREE,RUN,TURN} → WAIT_PHASE → HEAD → DONE`
- Spec table: `encounters/olm_solo_4t41.tsv` (+ `olm.tsv` phases_solo=4)
- Content: `cox_olm.rs2` basic hit respects protect prayer; sphere delay + pending varp;
  `^cox_olm_prayer_mult_*`
- Entry: `^cox_olm_entry_lz = 25`
- Loc: `cox.loc` wall_top floors + head Large-hole `blockwalk/blockrange=0 active=0`;
  carved `raids_olmic_head*` `blockrange=0`
- NPC: `olm_head` size=5 + Large-hole mesh/resize for Attack pick
- Head engage: aisle centre-south of 5x5 (`player.attack` quick+slot; `drive.op` fallback)

## Next

- Duo / trio harnesses
