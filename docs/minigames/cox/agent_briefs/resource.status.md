# Resource room — room status

Branch: `cursor/cox-resource-green-da39` (base `cursor/cox-raid-rooms-da39`)  
Owner: CoX ROOM ORCHESTRATOR (resource only)

## Gate — GREEN / FULL

```
cox_resource             green
GATE_RC=0
cox_resource: FULL (6 in-scope spec rows measured within tolerance)
```

Log: `/opt/cursor/artifacts/cox_resource_gate11.log`  
`t.raid.enter("cox","resource",{seed=1})`

| Spec | Measured |
|---|---|
| plots | 2 |
| herb_grow | 50 ticks |
| overload_boost_99 | +17 |
| overload_hp_cost | 50 |
| overload_duration | 500 |
| overload_reapply | 25 |

Also PASS: gourd/geyser, plant→harvest, elder brew, bat catch (seed=1 → bats).

## Mechanics

- Farming: 2 plots; grow `12+12+11+11` → wall 50 ticks; `[oplocu]` + `::coxresourceplant`
- Overload: article `floor(level×13/100)+5`; −50 HP; 25-tick reapply; 500 duration. Main page 4+10% conflict kept in `COX_MECHANICS.md` §15
- Supply: 50/50 bats/fishing; pack npc 27406; varps 7340/7341

## Own files

`cox_resource.rs2`, `cox_herblore.rs2`, `cox_bats.rs2`, configs flock, `resource.tsv`, `test/raids/cox_resource.lua`, additive `cox_potion.rs2` overload constants
