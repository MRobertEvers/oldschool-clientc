# Equip BAS port queue

Agent-loop state for equipable-item stats, stance BAS (`*_baseanim`), and
related attack/defend anim overlays (LostCity → OSRS-Content).

Inventory = objs with `wearpos` (RuneLite/cache equipables). Stats = cache
params (do not import RuneLite `ItemEquipmentStats`). BAS = LostCity
`ready_baseanim`…`running_baseanim` + `~update_bas`. Post-254 weapons without
LC overlays keep param defaults.

Each tick ports **one** pending unblocked slice per `docs/PORTING_GUIDE.md` §4.
Status: `pending` | `in_progress` | `done` | `blocked`.

Loop prompt: read this file + PORTING_GUIDE §4; port the next pending unblocked
slice; verify (`mock230_pack --check-only`, `make -C src mock230-scripts`);
update this file; re-arm. Stop only when the user stops the loop.

| # | Slice | Status | Notes |
|---|---|---|---|
| 0 | Queue tracker | done | This file |
| 1 | Engine idle-anim path | done | Player fields + READYANIM…RUNANIM; put_appearance reads them; coverage regenerated |
| 2 | `~update_bas` | done | appearance.rs2; equip/unequip/login; agility stubs removed; hooks.update_bas |
| 3 | BAS overlays — spears | done | 21 objs in skill_combat/configs/bas/spears.obj |
| 4 | BAS overlays — polearms | done | 8 objs in bas/polearms.obj |
| 5 | BAS overlays — staves | done | 17 objs in bas/staves.obj |
| 6 | BAS overlays — sparse | done | dragon_longsword human_ds_ready |
| 7 | Attack/defend anim overlays | done | 210 objs in bas/attack_anims.obj; stab/crushattack_anim params; combat_attack_anim style switch |
| 8 | Stats audit | done | Cache bonuses remain authority (bronze scimitar selftest); no RL dump |
| 9 | Headless verify | done | Selftest: spear wield → human_staffready; unequip → human_ready; pack 0 errors |

## Log

- queue created (equip BAS parallel to CONTENT_PORT_QUEUE)
- slices 1–9 done in one pass: engine READYANIM hosts, ~update_bas, LC BAS+attack overlays, obj param overlay loader (beyond levelrequire), server .param defaults walk, opcode coverage regen, pack 0 errors
