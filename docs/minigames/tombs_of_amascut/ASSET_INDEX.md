# Tombs of Amascut — cache asset index

Every id the raid needs, and where it came from. All of it is already in
`cache.osrs239` — **no asset authoring is required for this raid**, only server
content. That single fact is what makes the plan's shape possible, so §0 gives
the commands to reproduce every table here from the cache rather than trusting
the tables.

Mechanics and provenance live in [`ENCOUNTERS.md`](ENCOUNTERS.md); the plan is
[`TOMBS_OF_AMASCUT_PLAN.md`](TOMBS_OF_AMASCUT_PLAN.md).

## 0. Regenerating this

```
python3 tools/toa_cache_dump.py OSRS-Content/osrs239-content \
    docs/minigames/tombs_of_amascut/sources
```

writes every `sources/cache_*_toa.*` file below. It selects by symbol prefix —
`toa_`, `akkha`, `npc_mandrill`, `npc_kephri`, `npc_wardens`, `crondis_`,
`wardenpet_` and the rest — because the ids are **not contiguous**: the raid's
own block is `11694–11851` for npcs but it also reuses `729` (`scarab_swarm`) and
`4189`, and the reward items are scattered from `25969` to `30893`.

Spot-check any single record without the tool:

```
awk -v s='[toa_baba]' '$0==s{f=1;next} /^\[/{f=0} f' \
    OSRS-Content/osrs239-content/configs/all.npc
grep -n '=toa_baba$' OSRS-Content/osrs239-content/configs/all.npc.compack
```

## 1. Map squares — twelve, all present

Region id → `m<x>_<y>`, where `x = region >> 8` and `y = region & 0xFF`. All
twelve are in `pack/5_maps.pack`; verified by name.

| Region | Square | Room |
|---:|---|---|
| 14160 | `m55_80` | Nexus / main hall |
| 14162 | `m55_82` | Path of Scabaras |
| 14164 | `m55_84` | Kephri |
| 14672 | `m57_80` | Tomb / vault / reward room |
| 14674 | `m57_82` | Path of Het |
| 14676 | `m57_84` | Akkha (plane 1) |
| 15184 | `m59_80` | Wardens P1 (plane 1) |
| 15186 | `m59_82` | Path of Apmeken |
| 15188 | `m59_84` | Ba-Ba |
| 15696 | `m61_80` | Wardens P2/P3 (plane 1) |
| 15698 | `m61_82` | Path of Crondis |
| 15700 | `m61_84` | Zebak |

Lobby, outside the tombs proper: the reference exits players to **(3358, 9113)**.

## 2. NPCs — 183 records

Full records in [`sources/cache_npc_toa.txt`](sources/cache_npc_toa.txt); the
combat columns in
[`sources/cache_npc_stats_toa.tsv`](sources/cache_npc_stats_toa.tsv).

In the cache's npc block, `stat1..stat6` are attack, strength, defence,
**hitpoints**, ranged, magic — `stat4` is hitpoints, which is the trap this tree
has already been caught by once. `param_26` is the attack style (1 melee-crush,
2 melee-slash, 4 ranged, 5 magic), `attackrate` the speed in ticks, `param_65`
the max-hit field.

### 2.1 Bosses and their forms

| Id | Symbol | Name | Lvl | Size | HP | Rate |
|---:|---|---|---:|---:|---:|---:|
| 11789 | `akkha_spawn` | Akkha | 337 | 3 | 400 | 6 |
| 11790 / 11791 / 11792 | `akkha_melee` / `_range` / `_mage` | Akkha | 337 | 3 | 400 | 6 |
| 11793–11796 | `akkha_enrage_spawn` / `_initial` / `akkha_enrage` / `_dummy` | Akkha | 337 | 3 | 400 | 6 |
| 11797 | `akkha_shadow` | Akkha's Shadow | 108 | 3 | 70 | 6 |
| 11798 / 11799 | `akkha_shadow_enrage` / `_dummy` | Akkha's Shadow | 108 | 3 | 70 | 6 |
| 11800–11803 | `akkha_trail_orb_lightning` / `_darkness` / `_burn` / `_freeze` | orb | — | 1 | — | — |
| 11804 / 11805 | `akkha_enrage_orb` / `akkha_headbar_npc` | — | — | 1 | — | — |
| 11719–11722 | `toa_kephri_boss_shielded` / `_weak` / `_enrage` / `_dead` | Kephri | 341 | 5 | 150 / 150 / 80 | 6 |
| 11723 | `toa_kephri_shield_scarab` | Scarab Swarm | 8 | 1 | — | — |
| 11724 / 11725 / 11726 | `toa_kephri_guardian_melee` / `_ranged` / `_mage` | Soldier / Spitting / Arcane Scarab | 89 | 3 | 40 | 6 / 3 / 6 |
| 11727 | `toa_kephri_scarab_rangekite` | Agile Scarab | 53 | 1 | 30 | 4 |
| 11728 / 11729 | `kephri_egg_explode` / `kephri_egg_hatch` | Egg | — | 1 | — | — |
| 11730 / 11732 / 11733 | `toa_zebak` / `_enraged` / `_dead` | Zebak | 371 | 9 | 580 | 7 |
| 11731 / 11734 | `toa_zebak_tail` / `_dead` | Zebak | 371 | — | — | — |
| 11735–11745 | `toa_zebak_jug`, `_jug_rolling`, `_safespot`, `_wave`, `_wave_bloody`, `_landcroc`, `_watercroc`, `_blood_cloud`, `_blood_cloud_small`, `spotanim_zebak_ranged01_npc`, `spotanim_zebak_jugbreak_npc` | — | — | — | — | — |
| 11778 | `toa_baba` | Ba-Ba | 359 | 5 | 380 | 6 |
| 11779 / 11780 | `toa_baba_coffin` / `_digging` | Ba-Ba | 359 | 5 | 380 | 6 |
| 11781 | `toa_baba_baboon` | Baboon | 77 | 1 | 10 | 2 |
| 11782 / 11783 | `toa_baba_boulder` / `_weak` | Boulder | — | 3 | 25 | — |
| 11784–11788 | `toa_baba_rubble` ×4, `toa_baba_sarc_npc` | — | — | — | — | — |
| 11746–11749 | Warden P1 inactive / active, both gods | Warden | 489 | 5 | 800 | 14 |
| 11750 / 11751 / 11752 | obelisk P1 inactive / active, P2 | Obelisk | — | 3 | 260 | 4 |
| 11753–11755 | `toa_warden_elidinis_phase2_mage` / `_range` / `_exposed` | Elidinis' Warden | 489 | 5 | 140 | 7 |
| 11756–11758 | `toa_warden_tumeken_phase2_*` | Tumeken's Warden | 489 | 5 | 140 | 8 |
| 11759–11764 | P3 inactive / active / charging, both gods | Warden | 544 | 5 | 880 | 5 / 1 * |
| 11765 / 11766 | `toa_warden_p3_death_tumeken` / `_elidinis` | — | — | — | — | — |
| 11767 / 11768 | `toa_amascut_p3` / `_enrage` | Amascut | — | — | — | — |
| 11769 | `toa_wardens_energy` | Energy Siphon | — | — | — * | — |
| 11770 / 11771 | `toa_warden_tumeken_core` / `_elidinis_core` | Core | — | 1 | — | — |
| 11772 / 11773 | `wardens_p3_orb_blue` / `_red` | — | — | 1 | — | — |
| 11774–11777 | `toa_wardens_zebak` / `_baba` / `_kephri` / `_akkha` | phantoms | — | — | — | — |

`*` the siphon carries no combat stats at all in the cache; its 1–4 hitpoints are
set by the fight, from the number of players still alive.

`*` the cache's P3 attackrate values (5 for Elidinis', 1 for Tumeken's) do not
match the wiki's stated attack speed of 7 for the slam, because the slam is a
scripted mechanic rather than an auto-attack. Use **7** and treat the cache field
as unused. `[M7]`

### 2.2 Challenge-room npcs

| Id | Symbol | Name | Lvl | HP | Rate | Style |
|---:|---|---|---:|---:|---:|---|
| 11697 | `toa_scabaras_scarab` | Scarab | 56 | 12 | — | ranged |
| 11698 / 11699 | `toa_scabaras_guesser_obelisk` / `_hit` | Obelisk | — | — | — | — |
| 11700–11704 | `toa_crondis_tree_1..5` | Palm of Resourcefulness | — | — | — | — |
| 11705 | `toa_crondis_crocodile` | Crocodile | 71 | 30 | 7 | melee |
| 11706 / 11707 | `toa_het_goal` / `_vulnerable` | Het's Seal (protected / weakened) | — | 119 | — | — |
| 11708 | `toa_het_orb` | Orb of Darkness | — | — | — | — |
| 11709 / 11712 | `toa_path_apmeken_baboon_melee_1` / `_2` | Baboon Brawler | 56 / 68 | 25 / 30 | 4 | melee |
| 11710 / 11713 | `..._ranged_1` / `_2` | Baboon Thrower | 56 / 68 | 30 / 35 | 4 | ranged |
| 11711 / 11714 | `..._magic_1` / `_2` | Baboon Mage | 56 / 68 | 20 / 25 | 4 | magic |
| 11715 | `..._shaman` | Baboon Shaman | 74 | 40 | 8 | magic |
| 11716 | `..._zombie` | Volatile Baboon | 72 | 8 | 4 | melee |
| 11717 | `..._cursed` | Cursed Baboon | 70 | 20 | 4 | melee |
| 11718 | `..._thrall` | Baboon Thrall | 52 | 2 * | 4 | melee |
| 729 | `scarab_swarm` | Scarabs | 92 | 25 | — | — |

`*` the wiki says 9. `[M4]`

### 2.3 Lobby, vault and cosmetic

`toa_bank_camel` 11806, `toa_maisa_vis` 11807, the four `toa_lobby_*` gods
11836–11839, `toa_midraidloot_trader` 11694 (Helpful Spirit),
`toa_player_ghost` 11695, `toa_amascut_vis` 11696, the five `toa_osmumten_*`
forms 11829–11834, `toa_amascut` 11835, the twelve god `*_captured` / `_freed` /
`_nexus` / `_vis` npcs 4189 and 11654–11828, `toa_zebak_transmog` 10680, and the
eight `osb10_toa_*_cutscene` doubles 12125–12132.

Pets: `warden_pet_tumeken` 11812, `warden_pet_elidinis` 11813, the four boss
morphs `warden_pet_akkha` / `_baba` / `_kephri` / `_zebak` 11846–11849, the two
destroyed forms 11850/11851, and their POH twins 11652/11653 and 11840–11845.

## 3. Locs — 1,357 ids

Full list in [`sources/cache_loc_toa.txt`](sources/cache_loc_toa.txt); the id
range is **15483–58525**. Most of it is scenery. The ones content must actually
wire:

**Het:** mirror 45455, fixed 45456, dirty 45457, destroyed 45467; barriers
45458–45466; barrier mound 45487; statue parent 45468; pickaxe statues by tier
45469 (`empty`) then bronze / iron / steel / black / mithril / adamant / rune /
gilded / dragon ×3 / 3rd age / infernal / crystal / trailblazer 45470–45486.

**Scabaras:** memory-game buttons 1–9 at 45356–45364 and tiles 1–9 at
45365–45373; `toa_scabaras_lightsout_tile_off` 45344; the `toa_scabaras_fx`,
`_totaltiles` and `_memorygame` families.

**Crondis:** waterfall floors 45400–45402; poison tiles 45418–45425 (active,
two recolours, inactive, edge variants).

**Apmeken:** venom pool 45493, pillar 45494, shaking 45495, no-repair 45496,
hammer crate 45497, potion crate 45498, vent 45499, continue 45500, transitions
45501/45502.

**Kephri:** dung 45149 / 45150 / 45151.

**Zebak:** rocks 45545–45548 and the `toa_zebak_debris` family.

**Progression:** `toa_teleport_crystal_continue` 45137,
`toa_teleport_crystal_continue_wardens` 45138.

**Reward chest:** ids 41696 and 44786 closed, 44787–44792 opened (per the wiki's
scenery infobox).

## 4. Items — 86 records

Full records in [`sources/cache_obj_toa.txt`](sources/cache_obj_toa.txt).

**Uniques:** Osmumten's fang **26219** (ornament 27246, kit 27248), Lightbearer
**25975**, Elidinis' ward **25985** (broken 25983, fortified 27251, ornament
27253, kit 27255), Masori mask **27226** / body **27229** / chaps **27232**
(fortified 27235 / 27238 / 27241; the 25969–25975/26217 block is the earlier
`masori_*` naming), Tumeken's shadow **27275** (uncharged 27277).

**Keris:** partisan 25979, breaching 25981, corruption 27287, sun 27291,
amascut 30891.

**Jewels:** thread of elidinis 27279, breach of the scarab 27283, eye of the
corruptor 27285, jewel of the sun 27289, jewel of amascut 30893.

**Remnants:** akkha 27377, ba-ba 27378, kephri 27379, zebak 27380, **Ancient
remnant 27381**. Pets: `wardenpet_tumeken` / `_elidinis` and their destroyed
forms.

**Raid-only tools:** water container 27295, mirror 27296, neutralising potion
27297, supplies bag 27314, rune cache 27293, honey locust 27351, ancient key
27369 (`toa_camel_key`), dawn scarab egg 27368, mask of rebirth 27370.

**Supplies:** heal 4/3/2/1 27315–27321, heal-over-time 27323/27325, prayer
4/3/2/1 27327–27333, prayer-over-time 27335/27337, energy 27339/27341, stats
27343/27345, panic-heal 27347/27349.

**Books:** lobby 27300, akkha 27302, baba 27304, kephri 27306, zebak 27308,
wardens 27310, icthlarin 27312.

**Point tokens** (each is an MVP item worth `300 × teamSize`): `toa_zebak_fang`
27219, `toa_baba_banana` 27221, `toa_akkha_ashes` 27223, `toa_kephri_poo` 27214,
`toa_loot_poo` 27216, `toa_grain` 27225.

## 5. Sequences and spotanims

244 seqs in [`sources/cache_seq_toa.txt`](sources/cache_seq_toa.txt), 95
spotanims in [`sources/cache_spotanim_toa.txt`](sources/cache_spotanim_toa.txt).

Named families: `npc_akkha_*` (idle/walk, spear), `npc_mandrill_*` (Ba-Ba —
idle, walk ×2, spawn, despawn, attack ranged/magic/special), `npc_kephri_*`
(idle, walk, stunned idle, dead), `npc_scarab_*` and `npc_scarab_flying_*`,
`npc_wardens_*` (dormant, wake ×2, idle ×6, walk ×4, attackleft/right/centre ×2
each, charging), `npc_amascut_giant01_*`, `npc_apmeken/crondis/het/scabaras_*`
(idle, walk, trapped), `crondis_poison_tile_activate_*` ×10,
`crondis_spear_trap_spear*`, and the `toa_*` prop seqs.

Load-bearing individual ids: `toa_flip_tile` 9490, `toa_tile_glow_idle` 9496,
`toa_teleporter_idle` 9501, `toa_osmumten_chest_closed/reveal/open`
9504/9505/9506, `toa_obelisk01_glow` 9515, `toa_wall_raising` 9516,
`toa_boulder_rolling` 9518, `toa_dung_drop` 9519, `toa_life_leach` 9520,
`toa_wardens01_pyramid_attack` 9524, `_appear` 9525, `_leave` 9526, `_load`
9527, `_invisible` 9528, `toa_het_orb_walk01` 9542,
`toa_keris_partisan_special01` 9544 (+ fx 9545).

Attack animations the wiki never names, from the reference `[nr]`: Akkha melee
9770, ranged swing 9772, magic swing 9774, ground detonation 9776, memory 9777,
trail 9778, final stand 9779, become invisible 9784 / visible 9785; the Warden
P2 "down" animation 9670.

Projectiles `[nr]`: Akkha ranged 2255, magic 2253; Zebak magic 2176 splitting to
2181, ranged 2178 splitting to 2187; Zebak floor roar spotanim 2184; Kephri dung
spotanim 2145. Het beam graphics objects 2114 horizontal, 2064 vertical, 2120
crash `[toa-plugin]`.

## 6. Sounds — 946 ids

[`sources/cache_sound_toa.txt`](sources/cache_sound_toa.txt). Census by family is
in [`ENCOUNTERS.md`](ENCOUNTERS.md) §13. The handful with known attack semantics:

| Id | Role | Source |
|---:|---|---|
| 5585 | Akkha style change | `[nr]` |
| 5640 | Akkha ranged impact | `[nr]` |
| 5667 | Akkha quadrant symbol active | `[nr]` |
| 5774 | Akkha magic impact | `[nr]` |
| 5591 / 3887 / 173 / 156 | Akkha quadrant hit, NW / NE / SE / SW | `[nr]` |
| 5878 | Zebak magic split | `[nr]` |
| 5884 | Zebak impact | `[nr]` |
| 5896 | Zebak ranged split | `[nr]` |
| 6480 | Kephri dung | `[nr]` |

## 7. Music — twelve tracks

Table in [`ENCOUNTERS.md`](ENCOUNTERS.md) §12, sourced from
`docs/audio/music_tracks_osrs239.tsv` (DBTable 44). Archive ids **730–741**, contiguous;
unlock varps 22 bits 28–31 and 23 bits 0–7.

## 8. Interfaces, invs and vars

**Interfaces:** 481 `toa_hud`, 482 `toa_raid_summary`, 771 `toa_chests`, 772
`toa_partylist`, 773 `toa_lobby`, 774 `toa_partydetails`, 775 `toa_scoreboard`,
776 `toa_invocations`, 777 `toa_midraid_loot`, 778 `toa_midraidloot_bag`.

**Invs:** 807/808/809 `toa_midraidloot_bundle1..3` (size 7), 810
`toa_midraidloot_bag` (size 28), 811 `toa_chests` (size 9).

**Client vars:** 1081 `toa_client_nextrefresh`, 1082–1084 scroll positions,
1085 `toa_raid_summary_tooltip`, 1086 rewards-hidden, 1099–1106
`toa_client_name0..7`.

**Varbits** — 94, in
[`sources/cache_varbit_toa.txt`](sources/cache_varbit_toa.txt). The load-bearing
groups:

| Range | Purpose |
|---|---|
| 14346–14353 | `toa_client_p0..p7` — party slot state |
| 14354 / 14355 | `toa_client_partyslot`, `toa_client_hideplayers` |
| 14362–14369 | `toa_client_primary0..7` — Apmeken's Sight holder |
| 14376–14379 | `toa_client_crondis/scabaras/het/apmeken_level` — the four path levels |
| 14380 / 14381 | `toa_client_raid_level`, `toa_client_current_path` |
| 14495 | `toa_client_raid_level_stop_auto_updating` |
| 14356–14360, 14370–14372 | `toa_vault_chest_0..7` |
| 14373 | `toa_vault_sarcophagus` |
| 14323 / 14325 / 14374 / 14375 | damage taken / done, total and current room |
| 14318–14322 | party-list filter, has-loot, scoreboard tab, mid-raid loot claimed / rolled |
| 14324, 14326–14343 | selected title and the eighteen title-achieved flags |
| 14344, 14361 | mid-raid loot stats timer, energy active |
| 14439 / 14440 | `toa_pickaxe_charges`, `toa_pickaxe_stored` |
| 14441–14447 | the camel quest — state, four per-boss solved flags, name learned, secret chest |
| 14449–14454 | the six books |
| 14455 / 14456 | `toa_wardens_buff_type`, `toa_wardens_buff_value` |
| 14498–14503 | unlocked pet morphs |
| 14541 / 14542 | selected invocation preset, secret passage found |
| 14671 | `toa_ca_qualified` |
| 13837 / 10149 / 3211 | entrance open, been-teleported, kicked-from-raid |

The reference also names three varbits this cache dump does not label: **3586**
points, **3680** the invocation-preset base, and the invocation state itself,
which it writes through the party interface rather than a named var. `[M8]`

## 9. Structs — the invocation table

46 rows, in
[`sources/cache_invocations_toa.tsv`](sources/cache_invocations_toa.tsv). Struct
ids are **not** contiguous: 417–442, 444, 445, 447–449, 542, 543, 603, 752, 949,
1275, 1276, 1278, 1688, 2874, 2933, 2934, 2971, and the two Leagues entries 5892
and 5893. The dump finds them by "has both param 1159 and param 1160" rather than
by an id list, so a struct added by a later cache is picked up automatically.

## 10. Combat Achievements

51 tasks, ids **421–471**, in
[`sources/wiki_combat_achievements_toa.tsv`](sources/wiki_combat_achievements_toa.tsv).
Not cache data — Combat Achievement definitions are server-side, so this table is
the wiki's.
