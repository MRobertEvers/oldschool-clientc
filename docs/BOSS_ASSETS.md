# Boss encounter assets — Tormented Demons and TzKal-Zuk

Everything the two ported encounters are built out of: which cache each asset
came from, what its id is there, what it is called after the port, and what it
is actually for. Both were exported with
`3rd/rscache/tools/port_lostcity` into `LostCity_Server/content`.

The two are documented together because they were found the same way and they
break the same way. An encounter is never one npc record — it is a body model,
a framemap, eight or nine sequences hanging off that framemap, a handful of
spotanims, and a second set of sequences that rig the spotanim models rather
than the boss. Miss any one of them and the fight still runs; it just does
something invisible, which is much harder to notice than a crash.

## How to find this sort of thing yourself

The npc record names its idle and its walk and nothing else. Attack, death,
defend and spawn animations are not referenced by anything the npc points at,
so they cannot be reached by walking ids outward from the npc.

Two tools cover it, both in `3rd/rscache/tools`:

```sh
make -C 3rd/rscache/tools

# Everything sharing the npc's framemap — this is what finds the attack and
# death animations, because a rigged sequence must share the rig.
./find_anims/find_anims --rev osrs230 cache.osrs230 --npc 13599

# Everything whose *name* matches — this is what finds the projectiles, which
# share no framemap with the demon and are referenced by no config at all.
./find_named/find_named --rev osrs230 cache.osrs230 --name undead_demon
```

`find_named` was written for this job. Sequences and spotanims carry the
content team's own debug names in these revisions (`luc2_undead_demon_melee`,
`zuk_attack`), and that name is the only handle on an asset nothing points at.
It also dumps single records:

```sh
./find_named/find_named --rev osrs230 cache.osrs230 --npc 13599
./find_named/find_named --rev osrs230 cache.osrs230 --seq 11392
./find_named/find_named --rev osrs230 cache.osrs230 --spotanim 2853
./find_named/find_named --rev osrs230 cache.osrs230 --scan-spotanim-model 50027
```

Sequence dumps report duration in both client cycles and server ticks, which is
the number the scripts actually need — **30 client cycles is one server tick**.

---

# Tormented Demons

Source cache `cache.osrs230`. Authored under the prefix `luc2_undead_demon_`,
which is the Desert Treasure II content pack's internal name for them.

## NPCs

Eight records, all level 450, size 3, and all sharing the same two animations
and the same stat block.

| id | model | ported as | note |
|---|---|---|---|
| 13599 | 53287 | `td_demon` | shielded body |
| 13600 | 53285 | `td_demon_unshielded` | unshielded body |
| 13601 | 53287 | — | duplicate of 13599 |
| 13602 | 53285 | — | duplicate of 13600 |
| 13603–13606 | 6318 | — | the 2008 RS2 demon, kept in cache, not ported |

**The fire shield is geometry, not an overlay.** That is the single most useful
thing in this table: 53287 carries the shield and 53285 does not, which is why
dropping the shield in the script is an `npc_changetype_keepall` between two npc
types rather than a spotanim. Getting that wrong means either a shield that
never visibly comes off, or — if you reach for plain `npc_changetype` — a demon
that heals to full every time the player hits it.

The stat block from `stats(74-79)` on every one of them:

```
attack 255   defence 150   strength 255   hitpoints 600   ranged 255   magic 255
```

which is exactly what the wiki lists, so the cache is a usable source of truth
for the numbers and not just the art.

## Body animations — framemap 2278

All nine hang off one framemap, which is what makes `find_anims --npc 13599`
find the whole set from the two the npc record names.

| id | cache name | frames | duration | fp | ported as | role |
|---|---|---|---|---|---|---|
| 11391 | `luc2_undead_demon_ready` | 32 | 120cy / 4t | 4 | `td_ready` | idle |
| 11390 | `luc2_undead_demon_walk` | 32 | 90cy / 3t | 5 | `td_walk` | walk |
| 11392 | `luc2_undead_demon_melee` | 16 | 60cy / 2t | 6 | `td_melee` | melee swing |
| 11389 | `luc2_undead_demon_spare_ribs` | 21 | 90cy / 3t | 6 | `td_spare_ribs` | ranged attack |
| 11388 | `luc2_undead_demon_firey_balls` | 14 | 60cy / 2t | 6 | `td_firey_balls` | magic attack |
| 11387 | `luc2_undead_demon_explosion_fire` | 40 | 150cy / 5t | 7 | `td_explosion_fire` | fire bomb cast |
| 11393 | `luc2_undead_demon_defend` | 22 | 90cy / 3t | 6 | `td_defend` | block |
| 11394 | `luc2_undead_demon_death` | 33 | 150cy / 5t | 10 | `td_death` | death |
| 11395 | `luc2_undead_demon_summon` | 53 | 240cy / 8t | 9 | `td_summon` | spawn |

`spare_ribs` is the ranged attack and the name is literal — the demon opens its
rib cage and throws. `firey_balls` is the magic one. There is no third attack
animation: melee, magic and ranged are three animations, and which of the two
projectile styles is being used is carried by the *projectile*, not the body.

Several carry frame sounds (`11393` alone has eight), so a sound port has
somewhere to start.

## Effect animations

These rig the spotanim models rather than the demon, so they sit on their own
framemaps and `find_anims` on the npc will never surface them. They arrive as
dependencies of the spotanims below.

| id | cache name | frames | ported as |
|---|---|---|---|
| 11398 | `luc2_undead_demon_swipe_spot` | 12 | `td_swipe_spot` |
| 11399 | `luc2_undead_demon_shield_spot` | 15 | `td_shield_spot` |
| 11400 | `luc2_undead_demon_explosion_fire_spot` | 40 | `td_explosion_fire_spot` |
| 11401 | `luc2_undead_demon_shield_restore_spot` | 30 | `td_shield_restore_spot` |
| 11402 | `luc2_undead_demon_summon_spotanim` | 60 | `td_summon_spot` |
| 11403 | `luc2_undead_demon_death_spot` | 40 | `td_death_spot` |
| 11405 | `luc2_undead_demon_fireball_large_proj` | 46 | `td_fireball_large_proj_seq` |
| 11404 | `luc2_undead_demon_summon_spotanim_prototype` | 60 | — |

11404 is dead content: nothing references it, and it is the only sequence in the
set no spotanim points at. Not ported.

## Spotanims — projectiles and effects

| id | cache name | model | seq | ported as | role |
|---|---|---|---|---|---|
| 2853 | `luc2_undead_demon_fireball_proj` | 50027 | 10640 | `td_fireball_proj` | ranged projectile |
| 2854 | `luc2_undead_demon_fireball_impact` | 50027 | 10641 | `td_fireball_impact` | its impact |
| 2855 | `luc2_undead_demon_fireball_large_proj` | 54031 | 11405 | `td_fireball_large_proj` | magic projectile, 160% scale |
| 2856 | `luc2_undead_demon_fireball_large_impact` | 50027 | 10641 | `td_fireball_large_impact` | its impact, 160% scale |
| 2846 | `luc2_undead_demon_fireball_impact_spot` | 3082 | 660 | `td_fireball_impact_spot` | recoloured legacy impact |
| 2851 | `luc2_undead_demon_melee_spot` | 53280 | 11398 | `td_melee_gfx` | claw swipe |
| 2849 | `luc2_undead_demon_shield_spot` | 53283 | 11399 | `td_shield_gfx` | shield coming off |
| 2850 | `luc2_undead_demon_shield_restore_spot` | 53283 | 11401 | `td_shield_restore_gfx` | shield going back on |
| 2852 | `luc2_undead_demon_explosion_fire_spot` | 53283 | 11400 | `td_explosion_fire_gfx` | the bomb on the ground |
| 2847 | `wgs_undead_demon_summon_spotanim` | 53283 | 11402 | `td_summon_gfx` | spawn |
| 2848 | `luc2_undead_demon_death_spot` | 53282 | 11403 | `td_death_gfx` | death |

Three of these are older assets the modern fight reuses rather than replaces:
2846 is built on model 3082 / seq 660, both 2008-era, with a recolour
(`926->960`, `947->6080`) applied on top; 2853/2854 use model 50027 and seqs
10640/10641, which predate the `luc2` pack. 2847 is prefixed `wgs_`, not `luc2_`
— it came from the While Guthix Sleeps content and was adopted here.

## Re-running the export

```sh
./3rd/rscache/tools/port_lostcity/port_lostcity \
  --manifest 3rd/rscache/tools/port_lostcity/tormented_demon.ini --apply
```

The manifest lists every id above with its ported name. It writes
`content/scripts/areas/area_tormented_demons/configs/td.{npc,seq,spotanim}`,
the models and animsets under `content/models/td/`, and the id lines in
`content/pack/*.pack`.

**It overwrites `td.npc`, and `td.npc` is where the combat stats live.** The
exporter only knows about the visual fields; hitpoints, the defence bonuses, the
params and the hunt mode are hand-authored and have to be put back. Same trap as
`inferno.npc` — see that area's README.

The 15 `framemap transform_actor/masks/tail dropped for dat1` warnings are
expected: dat1 framemaps have no skeletal tail, and the fight does not use one.

---

# TzKal-Zuk and the Inferno

Source cache `cache.osrs239` for the port. `cache.osrs230` also carries these
records **and carries sequence debug names**, which osrs239 does not — so dump
from 230 when you want to know what a sequence is, even though the port reads
239.

## NPCs

| id | name | model | size | level | ported as |
|---|---|---|---|---|---|
| 7706 | TzKal-Zuk | 33011 | 7 | 1400 | `inferno_zuk` |
| 7707 | Ancestral Glyph | 33036 | 3 | 0 | `inferno_zuk_shield` |
| 7708 | Jal-MejJak | 33099 | 1 | 250 | `inferno_mejjak` |
| 7700 | JalTok-Jad | 33012 | 5 | 900 | `inferno_jad` |
| 7701 | Yt-HurKot | 9326, 9328, 9327 | 1 | 141 | `inferno_hurkot` |
| 7702 | Jal-Xil | 33014 | 3 | 370 | `inferno_xil` |
| 7703 | Jal-Zek | 33000 | 4 | 490 | `inferno_zek` |

Zuk is **size 7** in the source and LostCity caps npc size at 5. The port
clamps him, and the encounter constants shift his spawn coord one tile north and
east to put the smaller footprint back under the same centre —
`2269*128 + 5*64` is exactly `2268*128 + 7*64`. Without that he draws visibly
off-centre in his alcove.

Yt-HurKot is level 141 here, not the Fight Caves' level 122 — the cache settles
an argument the wiki pages are ambiguous about.

## Sequences

Zuk, framemap 1691:

| id | cache name | frames | duration | fp | ported as |
|---|---|---|---|---|---|
| 7564 | `zuk_ready` | 28 | 90cy / 3t | 0 | (readyanim) |
| 7566 | `zuk_attack` | 31 | 90cy / 3t | 10 | `inferno_zuk_attack` |
| 7565 | — | 30 | 90cy / 3t | 6 | `inferno_zuk_defend` |
| 7562 | `zuk_death` | 50 | 150cy / 5t | 10 | `inferno_zuk_death` |
| 7563 | — | 70 | 210cy / 7t | 10 | `inferno_zuk_spawn` |
| 13717 | — | 70 | 210cy / 7t | 0 | — (not ported) |

Ancestral Glyph, framemap 1697: 7567 (idle/walk, 20f), 7568 → `inferno_shield_defend`
(20f, 2t), 7569 → `inferno_shield_death` (42f, 4t).

JalTok-Jad, framemap 1692: 7588 walk, 7589 idle, 7590 → `inferno_jad_melee`,
7591 → `inferno_jad_defend`, 7592 → `inferno_jad_magic` (50f, 5.33t),
7593 → `inferno_jad_range`, 7594 → `inferno_jad_death` (51f, 5.13t). Also 8857
and 8858 on the same framemap, unported.

Jal-Xil, framemap 1693: 7602 idle, 7603 walk, 7604 → `inferno_xil_melee`,
7605 → `inferno_xil_range`, 7606 → `inferno_xil_death`, 7607 → `inferno_xil_defend`.

Jal-Zek, framemap 1686: 7608 walk, 7609 idle, 7610 → `inferno_zek_magic`,
7612 → `inferno_zek_melee`, 7613 → `inferno_zek_death`, 7611 (72f, 6t) unported.

Jal-MejJak, framemap 19: 2863 → `inferno_mejjak_defend`, 2864 → `inferno_mejjak_spawn`,
2865 → `inferno_mejjak_death`, 2867 idle, 2868 → `inferno_mejjak_attack`,
2866 and 2869 unported.

Yt-HurKot, framemap 163: 2634 walk, 2636 idle, 2639 → `inferno_hurkot_heal`,
2635/2637 unported. **2638 is worth knowing about**: its frames total 20090
client cycles — 670 server ticks — because one frame carries an enormous length.
That is the invalid-loop-range quirk, not a decode bug.

Scenery: 7561 `safe_spot_distructible_pillar_collapse` (22f, 60cy / 2t) →
`inferno_seal_collapse`. Its two-tick length is exactly why the seal's rocks are
cleared two ticks after the animation starts.

## Spotanims

| id | ported as | role |
|---|---|---|
| 1375 | `inferno_zuk_proj` | Zuk's shot |
| 1376 | `inferno_zek_proj` | Jal-Zek magic |
| 1377 | `inferno_xil_proj` | Jal-Xil ranged |
| 660 | `inferno_heal_proj` | healer beam |
| 659 | `inferno_lava_splash` | Jal-MejJak bombardment impact |
| 447 | `inferno_jad_magic_gfx` | Jad magic cast |
| 448, 449, 450 | `inferno_jad_proj1/2/3` | Jad projectiles |
| 451 | `inferno_jad_range_gfx` | Jad ranged cast |
| 157 | `inferno_jad_hit` | Jad impact |
| 444 | `inferno_hurkot_heal_gfx` | Yt-HurKot heal |

Note 660 is the same spotanim the Tormented Demons' `td_fireball_impact_spot`
(2846) is built on, recoloured. The two encounters share more than they look
like they do.

## Locs

30343, 30344 (broken rock variants), 30339–30342, 30331, 30345, 30346 (the
rocks flanking Zuk), 30356 (the multiloc marker Kronos removes). Map square
`35_83`.

The flanking rocks 30345/30346 are **not in the map square's loc data** — Kronos
spawns them when it builds the arena, on level 1. The port raises them from
`[queue,inferno_seal_clear]`, the tick the falling seal rocks are removed and
their tiles come free: a simulated multiloc transform, and the only ordering
that works. Added earlier they replace the standing rocks (rotated 2x5, south
ends on the rocks' own tiles) and the collapse never plays; placed statically
they stand from tick zero and hide the topple inside their own geometry.
Level 1 is not scriptable at all — a zone update carries no plane, so a level-1
`loc_add` lands on the player's own level. See the inferno README.

## Re-running the export

The full command is in
`LostCity_Content2/scripts/areas/area_inferno/README.md` (not under
`LostCity_Server` — that tree has no Inferno). This engine ports Zuk with
**size 7** (cache) and `map_instance_from_square` isolation, so the shared-world
seal reseal path is replaced by instance teardown; seal **break** still uses
`loc_change` / `loc_anim` / angle-aware del+add. Soft gaps landed in
`OSRS-Content/.../minigame_inferno/`: Jal-Zek corpse revive (type-code ring +
half-HP `npc_add`), logout-pause between waves (perm `%inferno_paused` /
saved wave+pillars; exit arms request mid-wave), Zuk-kill Jal-nib-rek pet
roll (`infernopet`, 1/100) plus Ket-Keh cape exchange. Run **all** of the exporter
command if re-exporting — a partial run rewrites `inferno.loc` and `inferno.seq`
with only the ids you passed, which is how 69 locs once became 2.

---

# Traps common to both

- **`bun run build` writes `engine/data/pack`, not `LostCity_Server/data/pack`.**
  The latter exists, is all zero-byte files, and is a decoy.
- **Stop the server before building, and before copying the cache out.** The
  running server rewrites `data/pack`; a copy taken while it is up is torn. A
  torn cache reads as `bad read, dat length 0` in the client, and a short
  `main_file_cache.dat` (7.72MB against 7.96MB) is the tell.
- The client cache and `manifest_rs254.ini`'s `jag_crc` have to be refreshed
  together after any content build, or login fails silently — the client
  connects and then never completes.
- **The exporter overwrites the `.npc` config**, and that is where every
  hand-authored combat stat lives.
- A sequence's duration is in client cycles. Divide by 30 for server ticks
  before putting it in a script; `find_named --seq` prints both.
