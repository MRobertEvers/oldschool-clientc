# Skilling sound — the table, and the plan to make the skills audible

Every skill in this tree is silent. You swing a pickaxe at a rock and hear
nothing; you light a fire, spin a bowstring, pick a lock, rake a patch, set a
box trap — nothing. The engine can play all of it and has been able to since the
Inferno work; the content simply does not ask, because **239 ported scripts
carry a comment saying `sound_synth` was dropped, and not one skilling script
calls it**.

This document is (1) the list of skilling sounds per skill, sourced from
LostCity and the OldSchool Wiki, and (2) the plan to put them in.

The tree is not uniformly silent: **combat** has sound (weapon FX, the special
attacks, `combat.rs2`) and the **Inferno** has sound. Those two are the proof
the pipeline works. Every non-combat skill is the gap.

Companion to [`INFERNO_SOUNDS.md`](INFERNO_SOUNDS.md) (the same problem for one
encounter, and where the technique in §3 comes from),
[`NPC_SOUNDS_ANIMS.md`](NPC_SOUNDS_ANIMS.md) (npc combat sound),
[`AUDIO_ACCURACY.md`](AUDIO_ACCURACY.md) (which sound plays where) and
[`../AUDIO_SYSTEM_OPUS.md`](../AUDIO_SYSTEM_OPUS.md) (the engine underneath).

---

## 0. The finding, in one paragraph

**Nothing is blocked.** The server command exists and is wired to the wire
(`SS_OP_SOUND_SYNTH` → `mock230_send_synth_sound`), the client plays it, the
compiler resolves `synth` names, and — the part that was actually missing when
the skills were ported — **every sound name the reference uses now resolves in
this cache**. All 49 distinct synth names LostCity's skilling scripts call
resolve in `pack/4_soundeffects.pack`, and so do 199 of the 200 further names
this document proposes from the wiki's named-id table (248/249 total; the one
miss was an invented name, not a real one). The ambience layer — furnaces
humming, ranges crackling, waterwheels turning — is **already done and already
audible**, because it is loc data the client reads by itself. What is missing is
one layer: the sound a *player action* makes, which is server content, and which
was dropped skill-by-skill for a reason that is no longer true.

---

## 1. Why it was dropped, and why that reason is dead

The rationale is written down, in the first script that hit it —
[`skill_prayer/scripts/bury_bone.rs2:26-29`](../OSRS-Content/osrs239-content/server/scripts/skill_prayer/scripts/bury_bone.rs2#L26-L29):

> `sound_synth` is dropped, because the synth table in this cache is unnamed —
> every id in `synth.pack` reads `synth_<n>` — so there is no honest symbol to
> write.

That was correct when written and is now false. `pack/4_soundeffects.pack` has
**12,010 entries, 10,113 of them named** with Jagex's own config names (the
February 2025 leak, imported via `gen_sound_names.py` — same provenance as
`docs/audio/osrs_wiki_sound_ids.wikitext`). Only 1,897 still read `synth_<n>`,
and no skilling sound is among them.

So the 242 `// sound_synth dropped` comment lines across 239 scripts are a
**stale deferral**, not a standing decision. Every one of them is now a one-line
edit whose symbol compiles.

## 2. What the engine already does

| piece | state | where |
|---|---|---|
| `sound_synth(synth, loops, delay)` | **works** — pops 3, drops negative ids, sends the packet | [`mock230_scripts.c:8143`](../src/net/mock/mock230_scripts.c#L8143) |
| `SYNTH_SOUND` wire encode | **works** | `mock230_send_synth_sound`, `mock230_encode.c` |
| client playback (self) | **works** | `RS_Audio_Synth`, [`app.c:13710`](../src/app.c#L13710) |
| client playback (positional) | **works** | `RS_Audio_SynthAt`, [`app.c:13723`](../src/app.c#L13723) |
| `synth` symbol type in the compiler | **works** | [`ssc_symbols.c:504`](../src/serverscript/ssc_symbols.c#L504) |
| loc ambient sound (`soundid=`) | **works, and already playing** | decode [`torirs_location_from_rscache.c:128`](../src/engine/torirs_location_from_rscache.c#L128) → emitter [`world_scenery.u.c:1993`](../src/engine/world_builder/world_scenery.u.c#L1993) → `rs_audio.c` ambient loops |
| `huntall` / `huntnext` | **works** — so `~sound_area` is portable as 4 lines of content | [`mock230_scripts.c:4740`](../src/net/mock/mock230_scripts.c#L4740) |
| `~sound_area` / `~sound_within_distance` procs | **already ported — and called by nothing** (0 call sites) | [`general/scripts/misc/sound.rs2`](../OSRS-Content/osrs239-content/server/scripts/general/scripts/misc/sound.rs2) |
| `MIDI_JINGLE` (level-up jingles) | **opcode declared, server-side unimplemented**, and `11_musicjingles.pack` is entirely unnamed (`jingle_0…`) | [`ss_opcode.h:138`](../src/serverscript/ss_opcode.h#L138) |

The only *blocked* row is the last one (§6). The `~sound_area` row is the
surprise: it was ported, it is correct, and **nothing in the tree calls it** —
so the area-audible layer is sitting there finished and unused.

**1,506 locs in `configs/all.loc` populate `soundid=`** and the client plays
them today: `furnace`→`furnace_loop_3`, `fai_varrock_range`→`cooking_loop_2`,
`tzhaar_forge`→`furnace_loop_1`, `waterwheel_centre`→`waterwheel_loop`,
`archeus_altar_blood`→`rune_altar_ambience`. Do not re-add any of these as
scripted sounds — the ambience layer is done, and doubling it is the most likely
way to make this work sound *worse*.

## 3. How to read the tables — three source layers

Same vocabulary as `INFERNO_SOUNDS.md` §1. A row's layer is not a quality score;
it is **what kind of thing would have to be wrong** for the row to be wrong.

| layer | meaning | what would have to be wrong |
|---|---|---|
| **L** | **LostCity calls it at a named trigger.** A running server, cited file:line. Both the *sound* and the *moment* are stated. | LostCity's authors mis-observed the 2004 game |
| **W** | **The wiki's named-id table states the name**, and the name plus its position in the id block says what it is for. The *sound* is stated; the *moment* is mine. | I attached a real sound to the wrong action |
| **d** | **Derived** — no source names it; picked by analogy with an L or W row. | the analogy |

Every name in every table below was checked against
`OSRS-Content/osrs239-content/pack/4_soundeffects.pack`. Ids are that pack's,
which is this cache's. **Write the name, never the id** — `sound_synth(mine, 1,
0)`, not `sound_synth(2656, 1, 0)` — for the reason `PORTING_GUIDE` §4.1 rule 4
gives: ids never cross trees, names do.

### The block structure, which is itself evidence

The 239 cache's sound table is **grouped by skill**, in order, in ids 2427–2739:

```
2427–2449  farming        2600–2603  fishing        2692–2708  ranged
2450–2496  agility        2604–2606  fletching      2709–2720  slayer
2497–2573  melee weapons  2607–2615  herblore       2721–2727  smithing
2574–2583  cooking        2616–2654  hunter         2728–2736  woodcutting/canoe
2584–2593  crafting       2655–2661  mining         2737–2739  prayer (bones)
2594–2599  firemaking     2662–2691  prayer
```

This is why layer **W** is worth having at all: `farming_dibbing` sitting at 2432
between `farming_fillplantpot` and `farming_fill` is not a name that could mean
something else.

---

## 4. The tables

### 4.1 Mining — `skill_mining/scripts/mining.rs2`

| sound | id | when | layer | source |
|---|---|---|---|---|
| `mine_quick` | 2659 | **every swing** of the pickaxe | L | `mining.rs2:95` |
| `mine` | 2656 | on **success** — the ore is granted, rock depletes | L | `mining.rs2:134,178` |
| `found_gem` | 2655 | success on a **gem rock** | L | `mining.rs2:224` |
| `prospect` | 2661 | Prospect op, both the start and the standalone script | L | `mining.rs2:23`, `prospect.rs2:10` |
| `mine_3` / `mine_5` / `mining_3` | 2657/2658/2660 | swing variants — pick one at random per swing for texture, or leave out | W | block position |
| `smash_gem` | 2589 | cracking open a geode / crushed gem | d | — |

LostCity's comments cite a video for the swing/success split
(`https://youtu.be/ix4_VVi9Xm4`). Our anchors are `mining.rs2:81` and `:87`
(`anim(~mining_pickaxe_anim($pickaxe), 0)`) for the swing, and the `get_ore_*`
labels at `:95`/`:120` for success.

### 4.2 Woodcutting — `skill_woodcutting/scripts/woodcut.rs2`

| sound | id | when | layer | source |
|---|---|---|---|---|
| `woodchop` | 2735 | **on the first chop**, alongside the axe anim | L | `woodcut.rs2:72` |
| `woodchop_quick` / `woodchop_4` | 2730/2736 | subsequent chop variants | W | block position |
| `tree_fall` | 2734 | the tree depletes | W | name |
| `remove_axe` | 2733 | axe stuck / retrieved | W | name |
| `woodcutting_birdsnest` | 1516 | a bird's nest drops | W | name |
| `build_canoe` / `canoe_paddle_loop` / `canoe_roll` / `canoe_sink` | 2729/2728/2731/2732 | canoe content (not ported) | W | name |

LostCity plays `woodchop` **once**, at the start, and its comment cites a video
(`youtu.be/T3IRz4hZcjc`). Do the same; do not loop it per tick. Our anchor is
`woodcut.rs2:50`/`:56`.

### 4.3 Fishing — `skill_fishing/`

| sound | id | when | layer | source |
|---|---|---|---|---|
| `fishing_cast` | 2600 | **rod/harpoon catch attempt** (fires every attempt cycle, not on the cast anim) | L | `freshfish.rs2:49`, `rarefish.rs2:45`, `saltfish.rs2:113`, `tbwt.rs2:122`, `waterfall.rs2:26,84`, `slimeyfish.rs2:39`, `memberfish.rs2:38` |
| `net` | 2603 | **net catch attempt** | L | `saltfish.rs2:45`, `memberfish.rs2:110`, `tbwt.rs2:49` |
| `fire_lit` | 2596 | lava eel / infernal-style cooking on catch | L | `lavafish.rs2:42` |
| `lever` | 2400 | Fishing Guild / karambwan-vessel lever | L | `memberfish.rs2:235` |
| `found_gem` | 2655 | Fishing-Trawler style rare, cited to a video | L | `memberfish.rs2:265` |
| `lavacast` | 2602 | lava fishing cast | W | name + block position |
| `fish_splash` / `fish_swim` | 2148/2601 | spot ambience | W | name |

Note the exact placement, which is the interesting part: `fishing_cast` is **not**
on the `human_fishing_casting` anim one tick earlier — LostCity puts the anim and
the "You cast out your line..." message on `%action_delay = map_clock + 3` and
the *sound* on `+2`, with "You attempt to catch a fish."
(`freshfish.rs2:42-50`). Copy the offset, not the intuition.

### 4.4 Cooking — `skill_cooking/`

| sound | id | when | layer | source |
|---|---|---|---|---|
| `fry` | 2577 | **every cook attempt**, right after the cook anim | L | `cooking.rs2:163` |
| `oven` | 2580 | oven/stove cooking (vs. range) | W | block position |
| `spit_roasting` | 2578 | spit roast | W | name |
| `churn` | 2574 | dairy churn | W | name |
| `millstones` / `fill_grain` / `hopperlever` | 2579/2576/2575 | windmill flour: grind, fill hopper, pull lever | W | block position |
| `pour_tea` | 2583 | tea | W | name |
| `uncooking` | 2322 | burning the food | d | name |
| `cooking_loop_1..3` | 2198–2200 | **already loc ambience** — do not script | — | `all.loc` |

Anchor: `cooking.rs2` `[label,cook_item]`, immediately after `anim($anim, 0)`.

### 4.5 Firemaking — `skill_firemaking/`

| sound | id | when | layer | source |
|---|---|---|---|---|
| `tinderbox_strike` | 2599 | **every light attempt**, including each failed re-attempt | L | `firemaking.rs2:57,72,101,113`; `achey.rs2:38,50` |
| `fire_lit` | 2596 | **success**, inside `~firemaking_success`, before `facesquare` | L | `firemaking.rs2:122` |
| `strike_and_light` | 2594 | alternate light | W | block position |
| `flint` / `fuse_light` / `bunsen_burner` | 2597/2598/2595 | quest/other ignition | W | name |

The re-attempt case matters: LostCity plays `tinderbox_strike` **again** on each
failed roll (`firemaking.rs2:113`), not once at the start.

### 4.6 Crafting — `skill_crafting/`

| sound | id | when | layer | source |
|---|---|---|---|---|
| `chisel` | 2586 | cutting a gem; carving a snelm | L | `uncut_gem.rs2:40`, `snelm.rs2:16` |
| `spinning` | 2590 | **each spin** on the wheel | L | `spinning.rs2:24,86,97,129` |
| `pottery` | 2588 | shaping on the potter's wheel | L | `pottery.rs2:81` |
| `furnace` | 2725 | firing pottery; smelting glass; casting jewellery | L | `pottery.rs2:130,162`, `glass.rs2:49`, `jewellery.rs2:219,270,322` |
| `glassblowing` | 2724 | blowing glass | W | block position |
| `sand_bucket` | 2584 | filling a bucket with sand | W | name |
| `stiching` / `stitching` | 2591/2592 | needle-and-thread leather | W | block position |
| `stringing` | 2593 | stringing an amulet | W | block position (LC uses no sound at `jewellery/stringing.rs2`) |
| `loom_weave` | 2587 | loom | W | name |
| `attach_orb` | 2585 | attaching a charged orb to a staff | W | name |
| `smash_gem` | 2589 | failed gem cut | d | block position |

Note `jewellery.rs2:360` in the reference is a **commented-out** `sound_synth` —
do not port a line its own authors disabled.

### 4.7 Fletching — `skill_fletching/`

| sound | id | when | layer | source |
|---|---|---|---|---|
| `fletch` | 2605 | **each cut** of a log into bows/stocks | L | `cut_logs.rs2:82` |
| `fletch_once` | 2604 | the single-item variant | W | block position |
| `string_bow` | 2606 | stringing a bow | W | block position |
| *(bolt-tipping)* | — | LC passes a `$sound` **variable** at `bolts.rs2:44` | L | resolve per bolt type when bolt-tipping is ported |

### 4.8 Herblore — `skill_herblore/`

| sound | id | when | layer | source |
|---|---|---|---|---|
| `grind` | 2608 | pestle-and-mortar grinding | L | `herblore.rs2:23,36` |
| `grind_snail` | 1183 | grinding a **snail shell** — deliberately different, LC comments it | L | `grind_ingredient.rs2:85` |
| `liquid` | 2401 | decanting a potion (LC marks this one "osrs") | L | `decant_potion.rs2:51` |
| `herblore_clean_herb_1..4` | 3920–3923 | cleaning a grimy herb — pick one at random | W | name |
| `vial_empty` / `vial_pour` / `vialpour` | 2610/2613/2614 | emptying / pouring a vial | W | block position |
| `vial_mix` / `vial_mix_smokepuff` | 2611/2612 | mixing the potion | W | block position |
| `tap_fill` / `well_fill` | 2609/2615 | filling vials at a sink / well | W | block position |
| `dragon_potion_finished` | 2607 | finishing an antifire | W | name |

### 4.9 Smithing — `skill_smithing/`

| sound | id | when | layer | source |
|---|---|---|---|---|
| `anvil_4` | 2721 | **each smithing hammer strike** | L | `smithing.rs2:258,299`, `dragon_sq.rs2:40` |
| `furnace` | 2725 | **each smelt**, and cannonball casting | L | `smelting.rs2:160,186`, `cannonballs.rs2:28` |
| `anvil` / `anvil2` / `new_anvil` | 2722/2723/2726 | strike variants | W | block position |
| `hammering1..3` | 1143–1145 | generic hammering (non-anvil) | W | name |

⚠ **`anvil` is the one colliding name in this whole document** — it names both a
sound and an obj category, and `sound_synth`'s argument is not type-hinted
(§5.0 rule 3). Use `anvil_4` / `anvil2` / `new_anvil`; do not write bare `anvil`.

### 4.10 Thieving — `skill_thieving/`

| sound | id | when | layer | source |
|---|---|---|---|---|
| `pick` | 2581 | **each pickpocket attempt** | L | `thieving.rs2:23,66,188` |
| `npc_param(attack_sound)` | — | the npc's own hit sound **when the pickpocket fails** and it hits you | L | `thieving.rs2:40` |
| `stunned` | 2727 | you are stunned by the failure | L | `thieving.rs2:42` |
| `locked` | 2402 | a locked door/chest refuses | L | `thieving.rs2:126`, `trapped_chest.rs2:75,91`, `locked_door.rs2:104` |
| `chest_open` | 52 | a chest opens | L | `thieving.rs2:129`, `trapped_chest.rs2:100` |
| `lever` | 2400 | lever-operated thieving loc | L | `thieving.rs2:113,141` |
| `pick2` | 2582 | pickpocket variant | W | block position |
| `pick_lock` / `unlock` | 2407/2419 | lockpicking a door, and it giving | W | name |
| `disarm_trap` / `disarm_trap_failure` | 2387/2386 | chest trap | W | name |
| `safe_crack` / `rogue_safe_open` | 1243/1238 | safes | W | name |

`thieving.rs2:40` is worth calling out: it is the one place the reference reads
a **npc param** for the sound. That param is `attack_sound`, which
`NPC_SOUNDS_ANIMS.md` says no npc in this tree states yet — so it will play
nothing until that separate work lands, and the `< 0` guard in
`mock230_scripts.c` means that is *silent*, not broken.

### 4.11 Prayer — `skill_prayer/`

Two halves. The **engine** half:

| sound | id | when | layer | source |
|---|---|---|---|---|
| `prayer_off` | 2673 | any prayer deactivated (13 call sites, one per prayer script) | L | `prayers/*.rs2:26` |
| `cancel_prayer` | 2663 | all prayers cancelled at once | L | `prayer.rs2:40` |
| `prayer_drain` | 2672 | prayer points hit zero and drain out | L | `prayer.rs2:176` |
| `prayer_boost` | 2671 | praying at an altar that boosts | L | `altar.rs2:13` |
| `prayer_recharge` | 2674 | praying at an altar that recharges | L | `altar.rs2:16` |
| `bones_down` | 2738 | **burying bones** | L | `bury_bone.rs2:6` |
| `put_down` | 2739 | offering bones (ectofuntus / chaos altar) | W | block position |

And the **per-prayer** half — LostCity stores it as a `synth` column on the
prayers dbtable (`prayers.dbtable:7 column=sound,synth`) and plays
`db_getfield($data, prayers:sound, 0)` on activation (`prayer.rs2:22`). All
24 names resolve here:

| prayer | sound | id | | prayer | sound | id |
|---|---|---|---|---|---|---|
| Thick Skin | `thick_skin` | 2690 | | Protect from Magic | `protect_from_magic` | 2675 |
| Burst of Strength | `strength_burst` | 2688 | | Protect from Missiles | `protect_from_missiles` | 2677 |
| Clarity of Thought | `clarity` | 2664 | | Protect from Melee | `protect_from_melee` | 2676 |
| Rock Skin | `rock_skin` | 2684 | | Sharp Eye | `sharp_eye` | 2685 |
| Superhuman Strength | `superhuman_strength` | 2689 | | Hawk Eye | `hawk_eye` | 2666 |
| Improved Reflexes | `improved_reflexes` | 2662 | | Eagle Eye | `eagle_eye` | 2665 |
| Rapid Restore | `rapid_restore` | 2679 | | Mystic Will | `mystic_will` | 2670 |
| Rapid Heal | `rapid_heal` | 2678 | | Mystic Lore | `mystic_lore` | 2668 |
| Protect Item | `protect_items` | 1982 | | Mystic Might | `mystic_might` | 2669 |
| Steel Skin | `steel_skin` | 2687 | | Retribution | `retribution` | 2682 |
| Ultimate Strength | `ultimate_strength` | 2691 | | Redemption | `redemption` | 2680 |
| Incredible Reflexes | `incredible_reflexes` | 2667 | | Smite | `smite` | 2686 |

Layer **L** for the 15 LostCity states in `prayers.dbrow`; layer **W** for the
9 it does not have (the Eye/Mystic/Retribution/Redemption/Smite set, all sitting
inside the same 2662–2691 block). Note `protect_items` is at **1982**, outside
the block — the only one that is, so do not pattern-match its id.

### 4.12 Agility — `skill_agility/`

Per obstacle *kind*, not per course. All layer **L** (LostCity's gnome,
barbarian, wilderness courses and shortcuts) unless marked.

| sound | id | obstacle | source |
|---|---|---|---|
| `log_balance` | 2470 | log balance — **loops 1–4** by log length | `gnome_course.rs2:5`, `shortcuts.rs2:40,42`, `wilderness_course.rs2:134,142,145` |
| `balancing_ledge` | 2451 | narrow ledge (loops 1–2, one with delay 10) | `barbarian_course.rs2:82,97,101` |
| `climb_wall` | 2453 | wall climb (one site delays 40) | `shortcuts.rs2:23`, `barbarian_course.rs2:118` |
| `climbing_loop` | 2454 | sustained climb, 7 loops | `wilderness_course.rs2:154` |
| `squeeze_in` | 2489 | squeeze through a gap | `gnome_course.rs2:88,94`, `wilderness_course.rs2:69,75`, `barbarian_course.rs2:145` |
| `swing_across` | 2494 | rope swing | `barbarian_course.rs2:29`, `shortcuts.rs2:149`, `wilderness_course.rs2:101` |
| `tightrope` | 2495 | tightrope, 5 loops | `gnome_course.rs2:33` |
| `jump_no_land` | 2467 | gap jump with no landing thud | `wilderness_course.rs2:108–123`, `shortcuts.rs2:242` |
| `stumble_loop` | 2493 | **failing** an obstacle, 10 loops | `agility.rs2:105`, `barbarian_course.rs2:44,86`, `wilderness_course.rs2:163` |
| `human_hit2` / `female_hit2` | — | the damage grunt on a fail, delay 20, gender-split | `agility.rs2:111,113` |
| `monkeybars_on/loop/off` | 2474/2466/2473 | monkey bars: mount, 3–4 loops, dismount | `shortcuts.rs2:170,172,190,195` |
| `pool_plop` | 1658 | dropping into water | `shortcuts.rs2:72,252` |
| `watersplash` | 2496 | falling in water | `barbarian_course.rs2:48` |
| `sizzle` | 2412 | the wilderness course's lava | `wilderness_course.rs2:116` |
| `door_open` | 62 | the wilderness gate | `wilderness_course.rs2:45` |
| `squeeze_out` / `squeeze_thru_crack` | 2490/2491 | exit / crack variants | W |
| `handholds_grab` / `_loop` / `_off` | 2456/2459/2460 | handhold traverse (rooftop) | W |
| `ropeclimb` / `ropeclimb_loop` | 2482/2483 | rope climb | W |
| `plankwalk` / `plank_break` | 2480/2479 | plank crossing, and it breaking | W |
| `jump` / `jump2` / `jump_up` / `jump_further` / `jump_land_bridge` | 2461/2462/2468/2465/2458 | rooftop jump variants | W |
| `land_flat` / `fall_land` | 2469/2455 | landing, and falling | W |
| `sidestep` / `runonspot` / `pressups` / `situps` / `star_jump` | 2485/2484/2481/2486/2492 | Barbarian Outpost / agility-arena emotes | W |
| `climb_under` | 2452 | duck-under | W |

Our tree's agility is the **rooftop** courses, which LostCity (Sept 2004) does
not have — so the rooftop obstacles take the layer-**W** rows above, matched by
obstacle kind. 113 `anim(` call sites across
[`skill_agility/`](../OSRS-Content/osrs239-content/server/scripts/skill_agility/)
is the size of that job.

### 4.13 Farming — `skill_farming/` *(all layer W — LostCity has no farming)*

The 2427–2449 block is farming end-to-end, and the names are unusually literal:

| sound | id | action | | sound | id | action |
|---|---|---|---|---|---|---|
| `farming_raking` | 2442 | rake weeds | | `farming_prune` | 2440 | prune a bush |
| `farming_dibbing` | 2432 | plant a seed | | `farming_plantcure` | 2438 | plant cure |
| `farming_putin` | 2441 | put in a patch | | `farming_scoop` | 2443 | scoop compost |
| `farming_fillplantpot` | 2433 | fill a plant pot | | `farming_sprinkle` | 2444 | sprinkle |
| `farming_fillpot` | 2436 | fill a pot | | `farming_pourpint` | 2439 | pour |
| `farming_fill` | 2434 | fill (bucket/can) | | `farming_emptybarrel` | 2431 | empty a barrel |
| `farming_watering` | 2446 | water a patch | | `farming_plant_scarecrow` | 2435 | place a scarecrow |
| `farming_pick` | 2437 | harvest | | `farming_amulet` | 2430 | amulet of nature |
| `farming_compost` | 2427 | apply compost | | `farming_valve` | 2445 | valve |
| `compost_open` / `compost_close` | 2429/2428 | compost bin | | `fill_vat` | 2447 | vat |
| `pick_cactus` | 2448 | cactus | | `slice_watermelon` | 2449 | watermelon |

69 `anim(` sites in
[`skill_farming/`](../OSRS-Content/osrs239-content/server/scripts/skill_farming/);
the highest-value four are rake / dib / water / pick, which is the whole loop.

### 4.14 Hunter — `skill_hunter/` *(all layer W)*

The 2616–2654 block is hunter end-to-end, 32 usable names — the richest
untouched block in the cache, against 83 `anim(` sites in
[`skill_hunter/`](../OSRS-Content/osrs239-content/server/scripts/skill_hunter/):

| trap | set | trigger / catch | dismantle |
|---|---|---|---|
| box trap | `hunting_layboxtrap` 2636 | `hunting_boxtrap` 2627 | `hunting_dismantle` 2632 |
| bird snare | `hunting_setsnare` 2647 | `hunting_birdtrap` 2626, `hunting_birdcaught` 2625, `hunting_birdcall` 2624 | " |
| net trap | `hunting_set_twitchnet` 2644 | `hunting_twitchnet` 2652, `hunting_twitchnet_smoke` 2653, `hunting_smokepuff_escape` 2648 | " |
| deadfall | `hunting_setdeadfall` 2645 | `hunting_deadfall` 2631 | " |
| pitfall | `hunting_placebranches` 2639 / `hunting_placeleaves` 2640 / `_2` 2641 | `hunting_pitfall_collapse` 2638, `hunting_collapsebranches` 2630 | `hunting_takebranches` 2649 / `hunting_takeleaves` 2650 |
| noose wand | `hunting_setnoose` 2646 | `hunting_noose` 2637, `noose_wand` 2628, `lasoo` 2629 | — |
| butterfly net | — | `hunting_butterflynet` 2623, `hunting_release_butterfly` 2642 | — |
| falconry | — | `hunting_falcon_fly` 2633, `hunting_falcon_swoop` 2634 | — |
| big-game / tracking | `hunting_search` 2643 (track search) | `hunting_bigcat_jump` 2619, `hunting_tease_feline` 2651, `hunting_jump` 2635 | — |

### 4.15 Construction — `skill_construction/` *(all layer W)*

| sound | id | when |
|---|---|---|
| `poh_build_wood` / `poh_build_stone` / `poh_build_metal` | 938/937/930 | build, by material |
| `poh_select` | 970 | selecting a build option |
| `poh_wrong` | 990 | invalid build |
| `sawing_1` / `sawing_2` | 2105/2106 | sawing planks |
| `hammering` | 1770 | hammering |
| `poh_teleport` | 984 | house teleport |
| `poh_portal_shrink` / `_unshrink` | 967/968 | portal |
| `poh_tablet_break` / `_teleport` | 979/965 | breaking a house tab |
| `poh_clay_hit` / `poh_hit_limestone` / `poh_hit_marble` / `poh_stone_shatter` | 940/949/950/977 | stone-working |
| `poh_offer_bones` | 958 | altar bone offering |

Our construction is 5 scripts and 1 `anim(` — the smallest skill in the tree,
so this is a short slice.

### 4.16 Runecraft — `skill_runecraft/`

| sound | id | when | layer | source |
|---|---|---|---|---|
| `bind_runes` | 2710 | **crafting runes at an altar** | L | `runecraft.rs2:78` |
| `teleport_all` | 200 | talisman/tiara teleport into a ruin | L | `runecraft.rs2:7,40`, `essence_mine.rs2:33` |
| `curse_all` | 125 | the essence-mine teleport out | L | `essence_mine.rs2:15` |
| `abyssal_squeezethrough` | 2709 | Abyss obstacles | W | name |
| `rune_altar_ambience` / `rune_temple_ambience` | 3140/3141 | **already loc ambience** — do not script | — | `all.loc` |

### 4.17 Slayer — `skill_slayer/` *(all layer W)*

| sound | id | when |
|---|---|---|
| `slayer_throwbucket` | 2716 | bucket of water on a gargoyle-style task |
| `slayer_statuemove` | 2712 | statue task |
| `slayerdoors` / `slayerdoors_close` | 2717/2718 | slayer dungeon doors |
| `slayer_arrow` | 2703 | broad arrow / slayer-only ammo |

Slayer has 0 `anim(` sites in our tree — it is assignment/tracking, so its
sounds are for the *content* the tasks point at, not for `skill_slayer/` itself.
Lowest priority.

### 4.18 Summoning — `ported_scape2009_summoning/`

One row, and it is thin: `lore_craft_pouch` (4671) for infusing a pouch at an
obelisk. Layer **W** by name. Familiar *combat* audio is a different problem
already documented — see the familiar-combat audio work, which found only 6 of
78 familiars have any sound at all.

### 4.19 Not a skill, but adjacent

| sound | id | when | note |
|---|---|---|---|
| `digspade` | 1470 | digging with a spade | clue scrolls, quests |
| `digsite_dig_pick` / `_trowel` | 2373/2376 | Digsite tools | |
| `bank_drawer` / `bank_bell` | 2021/2022 | opening the bank | UI, not a skill |
| `firework` | 2396 | LostCity has this **commented out** at `levelup/…:34` with "Was an osrs addition apparently" | do not port |

---

## 5. The plan

The whole thing is content. No C changes are required for §4 except the two
optional engine items in §5.5.

### 5.0 The rule that makes this safe

Three rules, in priority order, and the first one is the one that will actually
be violated:

1. **Do not script anything the loc ambience already plays.** Check
   `configs/all.loc` for the loc's `soundid=` before adding a scripted sound to
   an action on that loc. Furnace, range, altar, waterwheel, windmill are
   already audible.
2. **Names, never ids.** `sound_synth(mine, 1, 0)`.
3. **Know that `sound_synth`'s first argument is not type-hinted.** The
   compiler resolves a bare name there with
   `SSC_SymbolsFind(…, SSC_SYM_UNKNOWN)`
   ([`ssc_compile.c:1457`](../src/serverscript/ssc_compile.c#L1457)) — an
   *all-namespaces* lookup. A synth name that also names an obj, loc or
   category can resolve to the wrong id, and unlike the script-vs-symbol case
   just above it in that file, **the compiler prints no note when it happens**.
   This is the `settimer(poison, …)` failure mode: it compiles, it runs, and it
   does something unrelated.

   I checked all 123 sounds in §4 whose names are plausible collision bait
   against every `pack/*` namespace. **Exactly one collides: `anvil`**, which is
   also a `category` name. LostCity uses `anvil_4` and so should we — but re-run
   the check when adding a name this document does not list, because names like
   `net`, `mine`, `pick`, `lever`, `oven` and `jump` are one content slice away
   from becoming ambiguous.
4. **One slice per skill, and each slice deletes its own `// sound_synth
   dropped` comments.** A stale deferral comment left next to a working sound
   call is the thing the next person reads and believes.

### 5.1 Phase 0 — the shared prerequisite (half a day)

**`~sound_area` is already there — start using it.**
[`general/scripts/misc/sound.rs2`](../OSRS-Content/osrs239-content/server/scripts/general/scripts/misc/sound.rs2)
already carries `[proc,sound_area]`, `[proc,.sound_area]` and
`[proc,sound_within_distance]`, all built on `huntall`/`huntnext` (implemented
at [`mock230_scripts.c:4740`](../src/net/mock/mock230_scripts.c#L4740)), and
**zero scripts call any of them**. So Phase 0 is not writing the proc, it is
deciding which §4 rows go through it: a sound that only the actor should hear
stays a plain `sound_synth`, and one a bystander should hear —
anvil, furnace, tree-fall, an agility fail — becomes
`~sound_area(<name>, 0, coord, <tiles>)`.

One thing to know before using it: the ported procs pass **`0` loops**, not 1
(`sound_synth($sound, 0, $delay)`), which differs from every direct call in §4.
Confirm that is what the client wants before fanning a skill out through it —
if it is wrong, it is wrong for every caller at once.

**Add a regression fixture.** `embed_test.c:1027` already exercises
`SS_OP_SOUND_SYNTH` end-to-end; extend it with one skilling case (mine a rock,
assert two `SYNTH_SOUND` packets with `mine_quick` then `mine`) so the whole
sweep has a test that can fail. Per the "verify blocker" rule: mutate the script
to drop one call and confirm the assertion goes red before trusting it.

### 5.2 Phase 1 — the four loops everyone hears — **done, 2026-08-12**

Ordered by how much of a session they cover, not by table size. Each is a
handful of one-line edits at an existing `anim(` call site.

| # | skill | edits | anchors |
|---|---|---|---|
| 1 | **Mining** | 5 | `mining.rs2:23,81,87,95,120`, `prospect` |
| 2 | **Woodcutting** | 2–3 | `woodcut.rs2:50,56` + `tree_fall` on depletion |
| 3 | **Fishing** | ~9 | one per spot script, at the `+2` attempt tick (§4.3) |
| 4 | **Firemaking** | 6 | `firemaking.rs2` attempt + re-attempt + success; `achey.rs2` |

These four are layer **L** throughout, so there is nothing to guess: the sound
and the tick are both stated. Stop here and the game already sounds alive.

**Landed** (done in a worktree off `v3`, branch `worktree-skilling-sounds`):
mining (`mine_quick`/`mine`/`prospect`), woodcutting (`woodchop`/`tree_fall`),
all 7 ported fishing spots — `freshfish`, `rarefish`, `saltfish`, `tbwt`,
`slimeyfish`, `lavafish`, `memberfish` (the last also picked up the casket's
`lever`/`found_gem`, which LC plays but this port had dropped alongside the
skilling sounds) — and firemaking (`tinderbox_strike`/`fire_lit`, plus
`achey.rs2` which reuses `firemaking.rs2`'s labels for free). `waterfall.rs2`
from the doc's original source list does not exist in this content tree — 7
spot files were ported, not 9; §4.3 undercounted.

Verification: `make mock230-scripts` compiles the same **14,240 scripts** with
**zero new errors or notes** (all 8 new synth names — `mine`, `mine_quick`,
`prospect`, `woodchop`, `tree_fall`, `tinderbox_strike`, `fire_lit`, plus the
already-used `fishing_cast`/`net`/`fire_lit`/`lever`/`found_gem` — resolve
clean). `make test-sound` (the audio unit suite) is fully green. `make
test-mock230` could not be exercised end-to-end: its `mock230-servpack`
prerequisite fails on an unrelated, pre-existing gap in the checked-out
OSRS-Content commit — several fishing npcs (`freshfish`, `saltfish`, …) state
a server-band field `pack/npc.server` doesn't claim. `git status` in the
worktree's OSRS-Content confirms this touches none of the 11 files edited
here; it is a content-pack registration gap orthogonal to this work, not a
sound regression.

### 5.3 Phase 2 — the production skills (2–3 days)

| # | skill | edits | note |
|---|---|---|---|
| 5 | **Smithing** | 6 | anvil + furnace, all L |
| 6 | **Cooking** | 1 + variants | one `[label,cook_item]` site covers everything |
| 7 | **Crafting** | ~12 | L for chisel/spin/pottery/furnace; W for the rest |
| 8 | **Herblore** | 4 + clean-herb | `herblore_clean_herb_*` random-of-4 is the nice touch |
| 9 | **Fletching** | 2–3 | resolve the `bolts.rs2` `$sound` variable when bolt-tipping lands |
| 10 | **Thieving** | ~13 | L throughout; the `npc_param(attack_sound)` row will be silent until npc sound data exists |
| 11 | **Prayer** | 7 + 24 | see §5.4 — this one is a config change, not edits |
| 12 | **Runecraft** | 4 | L throughout |

### 5.4 Phase 3 — the data-driven and post-2004 skills (3–5 days)

**Prayer is different and should be done as data.** Add a `sound,synth` column
to the prayers dbtable and 24 `data=sound,<name>` rows, then one
`sound_synth(db_getfield($data, prayers:sound, 0), 0, 0)` in the activation
proc — exactly LostCity's shape (`prayer.rs2:22`). One call site, 24 sounds.
Note the reference passes **`0` loops**, not 1, at that site; keep it.

The rest have no LostCity reference at all and are pure layer **W**, so they are
slower per row and want a pass in the running game to confirm each sound is not
absurd:

| # | skill | edits | why it is a bigger slice |
|---|---|---|---|
| 13 | **Agility** | ~40 of 113 anim sites | rooftop courses have no reference; map by obstacle kind |
| 14 | **Farming** | ~20 of 69 | rake/dib/water/pick first, the rest by tool |
| 15 | **Hunter** | ~25 of 83 | the richest block; set/trigger/dismantle per trap type |
| 16 | **Construction** | ~6 | smallest skill in the tree |
| 17 | **Slayer / Summoning** | ~5 | lowest value; do last |

### 5.5 Optional engine work, if the polish is wanted

Neither blocks §4.

- **Positional skilling sound.** `RS_Audio_SynthAt` exists
  ([`app.c:13723`](../src/app.c#L13723)) but no ServerScript opcode reaches it.
  A `sound_area`-equivalent that sends a *coord* instead of fanning out over
  `huntall` would be both cheaper and more accurate than §5.1's proc. Do it only
  if the proc proves too loud in a crowded bank.
- **Swing-sound variety.** `mine_3`/`mine_5`/`mining_3`, `woodchop_quick`/`_4`,
  `herblore_clean_herb_1..4`, `anvil`/`anvil2`/`new_anvil` are variant sets. A
  `~random_synth` content proc picking one per swing is 5 lines and no engine
  change.

### 5.6 Verification, per slice

1. **`make -C src mock230-scripts`** is the name check. It runs `sscompile`,
   which resolves `synth` symbols out of `pack/4_soundeffects.pack`, so a name
   that is not in this cache is a **compile error** — you cannot ship a wrong
   name silently. This is why §5.0 rule 2 (names, never ids) is not style
   advice: an id typo compiles and plays the wrong noise.
2. `make -C src test-sound` (the audio suite) and the mock230 selftest green.
   Beware the roam-RNG fragility already documented for the selftest — do not
   read a wander-related failure as a sound regression.
3. In-game, with `MOCK230_VERBOSE=1`: the server prints
   `mock230: sound_synth(<id>, <loops>, <delay>)` at
   [`mock230_scripts.c:8160`](../src/net/mock/mock230_scripts.c#L8160). Confirm
   the *id* and the *tick* against the table, not just "I heard something".
4. Confirm nothing doubled with loc ambience (§5.0 rule 1) by standing at a
   furnace and smelting: you should hear the hum **and** `furnace`, and they
   should not be the same sound.

---

## 6. Open and blocked

**Level-up jingles are blocked, and are the one genuinely blocked item.** Two
independent reasons, either of which alone would stop it:

- `MIDI_JINGLE` (opcode 2064) is declared in
  [`ss_opcode.h:138`](../src/serverscript/ss_opcode.h#L138) and has **no case in
  `mock230_scripts.c`** — the command compiles and does nothing.
- `pack/11_musicjingles.pack` is **entirely unnamed**: 315 entries, every one
  `jingle_<n>`. This is precisely the situation `4_soundeffects.pack` was in
  when the skilling sounds were dropped (§1), and it has no equivalent rescue —
  the February 2025 leak named sound effects, not jingles.

LostCity states the *intent* cleanly (`levelup.dbtable` has
`levelup_jingle,midi` and `unlocks_jingle,midi` columns; `levelup.dbrow` has
`advance attack` / `advance strength` / … per skill), so the content shape is
known. What is missing is a name→id mapping for this cache's jingle table. Until
someone produces one — by matching audio, or by finding a source that states it
— writing `midi_jingle(jingle_137)` would be a guess dressed as data.

**Also open:**

- **`npc_param(attack_sound)` for the thieving-fail row** (§4.10) — depends on
  the npc-sound data problem in `NPC_SOUNDS_ANIMS.md`, which is unsolved for
  anything past 2004.
- **`bolts.rs2`'s `$sound` variable** (§4.7) — a per-bolt-type sound the
  reference passes as an argument; needs bolt-tipping ported before it can be
  resolved.
- **Whether `~sound_area` or a positional opcode is right** (§5.5) — decide
  from how it actually sounds in a busy area, not in advance.
