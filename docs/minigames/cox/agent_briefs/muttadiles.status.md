# Muttadiles — room orchestrator status

**Agent:** Muttadiles only  
**Branch:** `cursor/cox-raid-rooms-da39`  
**Commit:** landing on `cursor/cox-muttadiles-green-da39` (content `12a8d1ec4b`)

## Owned files

| Path | Change |
|---|---|
| `OSRS-Content/.../scripts/cox_muttadiles.rs2` | Small→large unleash; submerged 4-tick LoS 1/3 magic + checkvis; style lock 3 autos; junior melee/ranged; meat tree HP/chop; meals at 50% (≤3×2 bites); `~cox_mutta_prayer_scaled` |
| `OSRS-Content/.../configs/cox.constant` | flock `^cox_mutta_*` (`attack_speed`, `submerged_checkvis`, `prayer_remaining_pct`, tree constants, npcvars 14–17, traces 86–87) |
| `OSRS-Content/.../configs/cox.varp` | `varp6798_cox_mutta_style` + `varp6799_cox_mutta_style_lock` **transmit=yes** (client/`t.var.server` readable for overhead swap + lock sample) |
| `docs/minigames/cox/encounters/muttadiles.tsv` | 8 rows; max-hit + tree_feeds tol `range` (ceilings under Protect Missiles / observed meal visits) |
| `test/raids/cox_muttadiles.lua` | `t.raid.enter("cox","muttadiles",{seed=1})`; masori+blowpipe+`br_anguish_necklace`; Protect Missiles; lean shark bag + restock; **kill large from landing before tree chop** |

Owned backups: `/workspace/.mutta_owned/`, `/opt/cursor/artifacts/cox_muttadiles.rs2.owned`, `cox_muttadiles.lua.owned`.

## Mechanics (COX_MECHANICS.md §8)

| Spec | Implementation |
|---|---|
| Small then large | Junior death → unleash / `npc_changetype` to `raids_dogodile` |
| Submerged magic while small lives | 4-tick gate, 1/3, checkvis 1; run28 submerged hit_player count **565** |
| Style lock 3 autos | `^cox_mutta_style_lock_attacks=3`; measured lock_seen=**3** (run28) |
| Meat tree 50%, ≤3×2 bites | `feed_hp_pct=50`, meals/bites constants + npcvars on junior |
| Tree HP / chop | WC×5 floor 100; chopped after room clear in harness |

## Heal amount 40% vs 50% — still open

**Feed threshold** settled at **50%** (29 Nov 2023 changelog, up from 40%).

**Heal amount** — both quotes kept; code uses 50% per visit without inventing a tiebreak:

1. **wiki_Muttadile.wikitext** Mechanics: `"eating up to 50% of their health (100% total) if triggered"`
2. **COX_MECHANICS.md:555**: `"up to 50 %" on one page, "up to 40 %" on another ⚠️ conflict"`

## Gate

| Check | Result |
|---|---|
| `spec_check.py …/muttadiles.tsv` | **ok, 8 rows** |
| `run.py cox_muttadiles` (run28) | **PASS** ledger SUMMARY pass=19 fail=0; `muttadiles.cleared` remaining 0; junior max hit **15** under Protect Missiles; large style lock **3**; 25× hit_npc on large (7563) |
| `gate.py cox_muttadiles` | **green** |
| `raid_coverage.py cox_muttadiles` | **FULL** (8 in-scope spec rows measured within tolerance) |

Evidence: `/opt/cursor/artifacts/muttadiles_ledger_run28.tsv`, `muttadiles_gate_run28b.log`, `muttadiles_coverage_FULL.log`, `muttadiles_shots_run28/`.

```sh
flock -x /tmp/raid_gate.lock \
  env QUEST_BINARY=/workspace/src/torirs_mutta_qt \
      TORIDRAW_PROBE_CFLAGS=-DGL_GLEXT_PROTOTYPES \
  python3 tools/raid_gate/run.py cox_muttadiles --no-build --no-publish
python3 tools/raid_gate/gate.py cox_muttadiles
python3 tools/raid_gate/raid_coverage.py cox_muttadiles
```

## Harness notes (why earlier runs died / stalled)

- **run26:** brew/restore stacks crowded out sharks → `player.died` tick 135 after 50% mark under stacked junior+submerged.
- **run27:** tree chop before large parked player at 6503,73 with **zero** `opnpc2` on large for 700 loops; style varps `transmit=no` always read 0.
- **run28 fix:** lean shark restock; kill large from landing (6512,80) with tbow **before** tree chop; style varps `transmit=yes`.

## Sibling notes

- Parallel agents flip the working tree to `cursor/cox-vasa-gate-8456`; restore from `.mutta_owned` before runs. Do not `git checkout -f` / park sibling trees.
- No commit.
