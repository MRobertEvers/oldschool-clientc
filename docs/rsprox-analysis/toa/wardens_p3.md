# The Wardens, phase 3

Read from 8 kills: 14 (solo, Entry) and 1852 (7 Expert raids), region 61,80.
Ticks are relative to entry (E) or the event named. Full inventory:
[wardens_p3.out](wardens_p3.out). Phases 1–2: [wardens_p1p2.md](wardens_p1p2.md).

**Ours:** `minigame_toa/scripts/toa_wardens.rs2` (not yet compared).

Tumeken's Warden is the phase-3 warden in all 8 kills (`toa_warden_tumeken_phase3`,
11762, from E+1), carrying spotanim `spotanim_wardens01_idle05` (2219).

## Floor

From E+6 the floor tiles under the arena play map_anim
`spotanim_wardens01_undertile01_explode01..04` (2220–2223) whenever a slam hits
them (one of the four variants per tile).

## Slams

| Sequence | Tiles (z 5157–5165) | Gap |
|---|---|---|
| `npc_wardens_attackleft01/02` (9674 / 9675) | x 3936–3946 (78 tiles) | 18–21 / 12 |
| `npc_wardens_attackright01/02` (9676 / 9677) | x 3926–3936 (78 tiles) | 18–21 / 12 |
| `npc_wardens_attackcenter01/02` (9678 / 9679) | x 3926–3946 (138 tiles) | 18–21 / 12 |

The `01` set is the slower pace (gaps 18–21), the `02` set the faster (12).
The tiles' graphics are sent on the slam tick with delays **60 to 126 in steps
of 6**, spreading across the area.

## Charge, orbs and stagger

C = `npc_wardens_charge01` (9682); Tumeken retypes to
`toa_warden_tumeken_phase3_charging` (11764).

| | Recorded |
|---|---|
| Bombs | pairs of `fx_wardens_bomb03` (2226): up start 0, end 58, angle 20, slope 0, height 20 → 87; then down start 30, end 120, height 87 → 20, to a tile; the tile's shadow `gargboss_debris_shadow_120` (1447) |
| Orbs | `wardens_p3_orb_blue` (11772) on the floor (e.g. (3931,5161), (3933,5164), (3941,5161), (3939,5164), (3935,5164), (3936,5158), (3937,5164)); Tumeken charges them with `fx_wardens_charging01` (2227): start 0, end 90, angle 0, slope 70, height 100 → 5; a charged orb retypes to `wardens_p3_orb_red` (11773) (298 of 298) |
| Stagger | **13–20 ticks after C** (14 in 8 of 28): `npc_wardens_stagger01` (9680), retype back to `toa_warden_tumeken_phase3` |

## Phantoms

| Npc | Id | Tile | Added | Attack |
|---|---|---|---|---|
| Zebak | `toa_wardens_zebak` (11774) | (3943,5153) | E+59..154 | `npc_zebak01_attack_ranged_enraged` (9626) every 8 (later 5); the Zebak split shot: `zebak_mage_projanim_initial_enraged` (2177) / `zebak_range_projanim_initial_enraged` (2179) start 30, end 120, angle 20, height 50 → 125; split npc `spotanim_zebak_ranged01_npc` (11744) at (3938,5156) with `zebak_mage_split` / `zebak_ranged_split` height 535; fragments `zebak_mage_projanim_split` / `zebak_ranged_fragment01..02` start 0, end 90, angle 127, height 125 → 22 |
| Ba-Ba | `toa_wardens_baba` (11775) | (3925,5153) | E+87..225 | `npc_baba_attack_melee` (9743) every 8 (later 5); rock falls map_anim `toa_baba_rock_fall` (2250) |

Both are added with ops 0b00000 (they cannot be attacked).

## Amascut and the enrage

| | Recorded |
|---|---|
| Amascut appears | `toa_amascut_p3` (11767), E+147..391; on its first tick `npc_amascut_giant01_enrage01` (9718) delay 1 and spotanim `spotanim_amascut01_sucking01_enraged01` (2231) delay 1, then retype `toa_amascut_p3_enrage` (11768) carrying `spotanim_amascut01_sucking01` (2229) (and `_flash01`, 2230) |
| The floor goes | `spotanim_wardens_tiles01` (2228) projectiles from the floor to far tiles (distance 31–35): start 0, end 90, angle 20, slope 0, height 0 → 200 |
| Lightning | map_anim `wardens_lightning` (2197) on tiles, every tick in bursts with 4-tick gaps, each after a `gargboss_debris_shadow_90` (1446) on the tile |
| Tumeken | `npc_wardens_enraged01` (9685) every **2 ticks** (188 of 188 gaps) |
| Death | five `toa_warden_p3_death_tumeken` (11765) npcs at (3934,5132), (3934,5137), (3934,5142), (3934,5147), (3934,5152) playing `spotanim_wardens01_death02_offset01..05` (9691–9695) |

The completion message is two lines in one: "Challenge complete: The Wardens.
Duration: <col=ef1020>M:SS.T</col>" and "Tombs of Amascut: <Mode> challenge
completion time: <col=ef1020>MM:SS.T</col>. Personal best: …" (or "(new
personal best)").
