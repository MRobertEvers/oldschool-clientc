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
spawns them when it builds the arena, on level 1, after teleporting the player
onto that plane (zone LOC_* packets apply to the player's current level only).
The port mirrors that: `p_teleport` to plane 1 → `~inferno_spawn_flanks` →
`p_delay(1)` → back to plane 0 → then the L0 seal collapse. Plane changes must
**not** REBUILD the scene (that would wipe the L1 flanks); see
`mock230_world_player_level_changed`. Do not bake flanks into the client cache.

## Four engine defects the Zuk fight uncovered (2026-08-04)

All four presented as "the Inferno is broken" and none of them were Inferno
content. Each is stated with the command that settles it, because each is a
general defect that the encounter happened to be the first content to exercise.

1. **`npc_walk` was not implemented, and a targetless npc never walked.** Two
   halves, and fixing either alone changes nothing. `SS_OP_NPC_WALK` printed
   `ssvm: NPC_WALK is not implemented` and returned defaults; and the npc phase
   only stepped waypoints for `WANDER` / a targeted mode, so even a queued
   destination sat still. The reference's `noMode()` *is* `updateMovement()` —
   mode `none` steps whatever `waypointIndex` holds (`Npc.ts`). The Ancestral
   Glyph therefore never left its spawn tile, so `%inferno_zuk_ready` never
   latched and the fight sat there after the cutscene, which is exactly the
   symptom `inferno.npc`'s `moverestrict=blocked` comment predicts for a
   different reason. `grep 'NPC_WALK is not implemented' <run log>`.

2. **A `loc_change`d loc was drawn twice, at two different depths.**
   `WorldBuilder_ApplyLocChange` released the painter's exclusive *wall* slot
   for the loc it removed and nothing for a centrepiece — so the baked static
   scenery element stayed in its tile chains. Scene element ids are recycled,
   the replacement loc was handed the id the dead one held, and the painter then
   emitted that model twice: once where the abandoned element sat in the
   back-to-front order and once at the new loc's own depth. Everything drawn
   between the two was overpainted by the second, which is what made the seal's
   rubble and the flank rocks swap in front of and behind the ground rocks
   around them. `painter_release_scenery` is the counterpart of
   `painter_release_wall`. `painter_reset_to_static` cannot help: the stale
   element is below `static_element_count`, the range it exists to preserve.
   Measured with a per-frame emit-order dump — 30340 at order 482 *and* 1341 in
   the same frame — not from pixels; the two painters (`bucket`, `world3d`)
   agree here and swapping them proves nothing.

3. **`npc_basestat(hitpoints)` compiled to param 2100 and answered 0.** The
   compiler's stat-name hint (`ssc_compile.c`, the thing that stops bare
   `hitpoints` resolving to the param that shares the word) tested membership by
   name prefix — `STAT_`, `STAT`, `NPC_STAT` — and `NPC_BASESTAT` matches none
   of them. So `%inferno_zuk_base_hp` was 0 and the overlay read `1200/1`. This
   is the failure that comment says the hint exists to prevent, missed on a
   spelling. Pinned by `test_stat_argument_hint` in `ssc_test.c`, which goes red
   on exactly one of its four legs if the clause is removed.

4. **`turnspeed` never reached the client on a dat2 cache.** The dat2→ToriRS npc
   mapping pinned `turn_speed = 32` under a comment saying dat2 records carry no
   such field. They do: opcode 103, decoded as `rotation_speed`, defaulted to 32
   by the decoder itself. `0` means "never turns", and it is how a loc-like npc
   holds a fixed facing — the Ancestral Glyph states 0 so it slides along its row
   still facing the arena, and with 32 forced it swung east and west as it
   walked. Cache-wide, not Inferno-only.

## The Zuk fight's own defects, and `::zuktest` (2026-08-04)

Five, all in content or in a content-facing seam, all found after the four
engine ones above stopped masking them. `::zuktest` is the command that now
holds them down — it drives the fight and reports each milestone with a
deadline, so a stage that stalls says which stage and at which tick instead of
looking like "the fight is just sitting there".

1. **No projectile ever left Zuk.** `[proc,player_projectile]` and
   `[proc,npc_projectile]` computed an honest flight time and had the
   `projanim_pl` / `projanim_npc` call **commented out**, under a header saying
   the opcodes were "declared but not hosted yet". They are hosted
   (`mock230_scripts.c` `SS_OP_PROJANIM_PL`/`_NPC` → `mock230_zone_projanim`).
   Blocker decay again, and this one was tree-wide: *every* ranged and magic
   attack in the content tree drew nothing. `MOCK230_PROJ_DEBUG=1` counts the
   sends — 0 before, 14 in a 4,500-frame Zuk run after.

2. **The blocked shot fired nothing at all.** When the player is behind the
   glyph, Kronos still sends the projectile — at the glyph — and flinches it on
   impact (`TzKalZuk.attack()`: `projectileTarget.animate(getDefendAnimation(),
   delay - 25)`). This port played the block animation instantly and sent no
   projectile, so a working safespot looked like a broken one. Note the
   argument order: `~npc_projectile` measures flight with `npc_range($coord)`
   **from the active npc**, so the glyph has to be active and Zuk's tile the
   argument; the reverse computes zero.

3. **One varp was doing two clocks' work, and the add waves paid for it.**
   Content2 keeps `%inferno_cooldown` (the emerge delay) and
   `%npc_action_delay` (the gap between shots) apart, and calls
   `~inferno_wave_tick` *between* them. Sharing one varp put the
   between-shots gap on the **top** of `[ai_timer]`, so the wave tick ran on one
   tick in ten: `^inferno_add_wave_first` 60 behaved like 600 and
   `^inferno_add_wave_interval` 350 like 3,500. The adds read as "never
   spawning". Split into `%inferno_zuk_action_delay`.

4. **Six anim params were missing, and the seq names are not the authority.**
   None of the Zuk-phase npcs declared `attack_anim` / `defend_anim` /
   `death_anim`, so all of them died and blocked on `npc_param`'s default. The
   ids come from Kronos (`data/npcs/combat/<Name>.json`, and `Inferno.java:600`
   for the glyph) — **not** from this cache's seq names, which get three of six
   wrong because the Inferno reuses older assets and keeps their names:
   Jal-MejJak's defend is 2863 (`..._creature_walk` here) and its death is 2865
   (`..._creature_go_down`), while the seq actually *named* `..._creature_death`
   is 2866 and is not it. Jal-Xil's and Jal-Zek's `attack_animation` is the
   **melee** seq; their ranged/magic seqs are named in the monster classes.

5. **The glyph re-queued its walk every tick.** Content2 re-issues on a 2-tick
   cooldown and says why in its own comment (anything that clears the queued
   movement strands it mid-row). Every tick is the opposite failure: it
   re-queues a destination the client is already walking to.

`::zuktest` reports each milestone with the tick it landed on and the deadline
it had. Read it headlessly with `MOCK230_ECHO_MES=1`, which mirrors every `mes`
to stderr — a content self-test that reports through the chat box is otherwise
invisible to a headless run. A healthy fight:

```
zuktest: spawn ok — tick 21.
zuktest: emerge ok — tick 21.
zuktest: ready (glyph reached an end) ok — tick 35.
zuktest: first shot ok — tick 35.
zuktest: first add wave ok — tick 95.
zuktest: done at tick 161 — 13 shot(s) fired.
```

The wave number is the one to read: **95 = ready (35) + `^inferno_add_wave_first`
(60)**, to the tick. That equality is the whole point of the test — it is what
distinguishes a wave clock advancing once per tick from one advancing once per
attack cooldown, and the two are indistinguishable from any single screenshot.
`MOCK230_PROJ_DEBUG=1` alongside it counted 16 sends against the test's 13
shots; the extra three are the add wave's own attacks, which is the cheapest
confirmation that the spawned mager and ranger are live rather than merely
present.

Two things the test got wrong first, both worth copying rather than repeating:

- **The clock has to be `map_clock`.** Counting queue passes reads perfectly
  plausibly and is wrong: a re-queued `[queue]` does not land once per tick
  here, so every milestone came out at roughly a quarter of its real tick and
  the deadlines were measuring a unit nothing else in the fight uses. It anchors
  `%inferno_test_start = map_clock` and subtracts.
- **The runner has to survive.** Zuk hits for up to `^inferno_zuk_maxhit`, and
  the milestones that matter most are the late ones. A test that dies at tick 20
  prints four passes and then simply stops — which is indistinguishable from a
  pass. It tops the runner's hitpoints up every tick.

## The damage that never landed (2026-08-04, third pass)

Four defects, and three of them were engine-wide rather than Inferno's. They are
grouped here because they had one symptom — "the mobs do no damage" — and four
independent causes, each of which alone was enough to produce it.

1. **`last_int` was a property of the player, not of the script state.** The
   reference pushes `state.lastInt` and the npc queue seeds it per request
   (`Npc.ts`: `state.lastInt = request.lastInt`). Ours read `player->last_int`,
   which is correct for every player-context reader and returns 0 for the one
   context with no player value to read: an `[ai_queue<n>]` on an npc. That is
   where `npc_queue(2, $damage, $delay)` delivers its damage, so **every
   npc-to-npc hit in the tree landed for zero**. `state->last_int` had been
   declared for this and read by nothing.

2. **`npc_setmode(none)` did not clear the target.** `NPC_SETMODE` in the
   reference is `clearInteraction()` for the targetless modes, and
   `clearInteraction` sets `target = null`. Ours set the mode field only, and
   the npc phase skips any npc with a combat target ("combat and death own the
   npc's movement") — so a script-driven npc that anything hit once stopped
   walking permanently. The Ancestral Glyph binds `[ai_queue1] npc_setmode(none)`
   precisely so being attacked does not stop its sweep, and it froze on the
   first hit anyway.

3. **The compiler never checked command arity.** `queue` states three arguments
   (`[command,queue](queue $queue, int $delay, int $arg)`); ten sites passed
   four. Four pushes into a three-value pop does not fail — it *shifts*, so the
   script id came out of the delay slot and each of those queued a garbage id.
   `QUEUEVARARG` — the reference's `queue*(script, delay)(args…)`, which is what
   those sites actually wanted — was unhosted.

4. **The projectile left from the wrong tile.** Kronos `Projectile.send` offsets
   the source by `size / 2`; `npc_coord` is the south-west tile, so TzKal-Zuk
   (size 7) threw from three tiles off his own middle.
   `[proc,npc_projectile_source]` is that offset, over `nc_size` — which was
   implemented and documented in content as "absent".

### What the arity check found

Adding it turned up ~160 sites, none of them theoretical:

| command | sites | what was wrong |
|---|---:|---|
| `obj_add` | 120 | the DURATION was in the count slot, so every drop spawned 200 items |
| `npc_del` | 19 | called with an argument; the command declares none |
| `queue` | 10 | four arguments to a three-argument command (defect 3) |
| `npc_changetype` | 4 | no duration |
| `stat` | 3 | `mes("::boost <stat> …")` — see below |
| others | 4 | `damage`, `map_findsquare`, `movecoord`, `map_playercount` |

Two of those are worth reading past the count. `mes("::boost <stat> <constant>
<percent>")` is a usage line, and `<stat>` compiled to a `stat` **call**: the
interpolation test asked whether the name was a command and not whether the
command took arguments, so `<displayname>` (which takes none) and `<stat>`
(which takes one) were indistinguishable. And the first fix for `obj_add` was
itself wrong at 107 of the 120 sites, because they pass `~randomherb` — a proc
returning `(namedobj, int)` — which already supplies the count. **A check that
only warned would not have caught that**; the count went back up and said so.

The check is fatal now. It has two verdicts and only two, because only two are
sound: more arguments than slots is always wrong (every expression pushes at
least one value), and fewer is wrong only when nothing in the list could have
pushed extra — a multi-return proc or a `db_getfield` on a `coord,coord` column
legitimately fills several slots from one expression.

### Stats, audited

Every inferno npc against `kronos-server/data/npcs/combat/*.json`
(attack/strength/defence/hitpoints/ranged/magic + `max_damage`). Three records
and one constant disagreed:

| record | field | was | Kronos |
|---|---|---:|---:|
| `inferno_creature_ranger` / `inferno_ranger_finalwave` | hitpoints | 125 | 130 |
| `inferno_zuk_healer` | hitpoints | 75 | 80 |
| `^inferno_zuk_maxhit` | — | 148 | 251 |

Yt-HurKot reads as a mismatch and is not: Kronos carries two stat blocks for it
(hp 60 and 90) and both of this tree's records use the 90 one. The max-hit
constant is the one to note — the other five (46, 70, 113, 10, 18) match Kronos
exactly, which is what made the odd one out worth checking at all.

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
