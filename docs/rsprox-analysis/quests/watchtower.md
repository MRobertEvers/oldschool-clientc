# Watchtower from RSProx 4527 (rev 242, 2026-11-11)

Source: RSProx index 4527, world 370, recorded 2026-11-11: a full playthrough from
the Watchtower Wizard's start confirm to the scroll and the spell read (9,166 ticks),
with a Pillory random event near the end. `[n]` is the tick in the session.
Transcripts: `rsprox_index.py get 4527` (also `--raw` for the enclave scene's copy).
The wiki transcript compared against it is in
[watchtower_wiki_drafts.md](watchtower_wiki_drafts.md).

The port this replaces was LostCity's (2004) with OSRS seams: every page was 2004
text with forced `|` breaks, 204 of the recording's 488 distinct pages and menu rows
were missing, the stage-1 to stage-2 conversation ended on lines Jagex no longer
sends, the ogres called the player "little rat" and "small thing", the Gu'Tanoth
guards teleported the player in and out, the skavid caves had a 2004 "dark cave"
detour, the enclave had no scene, and the journal was 2004 text. It was rebuilt
page by page from the recording; where the recording never goes, the wiki
transcript (rev 15397834) is used and marked UNVERIFIED in the scripts.

## Files

| file | what |
|---|---|
| `quest_itwatchtower/scripts/watchtower_wizard.rs2` | the Watchtower Wizard at every stage, the relic hand-in and assembly, the crystals shown, the potion infused; the three wizards on the top floor |
| `quest_itwatchtower.rs2` | the bushes, the tower ladder and trellis, the skavid caves (map, dark, insects), Toban's island, the relic parts, the rope swing, the battlement, the rock cake, the Gu'Tanoth chests and bridge, the enclave door, the Rock of Dalgroth, the robe, the pillars, the lever and completion, the scroll |
| `ogre_guard.rs2` | the four Gu'Tanoth guards, the push down the hill, the gates opened for the player |
| `og.rs2`, `grew.rs2`, `toban.rs2`, `gorad.rs2` | the three chiefs and Gorad (talk, Attack refusal, the tooth) |
| `city_guard.rs2` | the riddle, the map, the map lost |
| `skavid.rs2` | the four teaching skavids, the scared skavid and the ogre who silences him, the mad skavid |
| `enclave_guard.rs2` | the guards, the nightshade, `cws_warning_12`, the run past, the enclave scene |
| `ogre_shaman.rs2` | the six shamans: talk blast, Attack refusal, the potion kills |
| `ogre_potion.rs2` | the mixes |
| `itwatchtower_journal.rs2` | the journal, row for row |
| `configs/quest_itwatchtower.constant` | `^watchtower_enclave_entry`, `^watchtower_cws_count_max` |
| `areas/area_yanille/scripts/yanille_thin_npcs.rs2` | Colonel Radick's first page and menu ([496..501]) |
| `areas/area_yanille/scripts/yanille_shop_stubs.rs2` | the ogre trader's page (no forced break) |
| `test/quests/itwatchtower.lua` | the gate test, on the recorded pages and flows |

Deleted from the 2004 port: the dark-cave detour (`darkcaverock`, `darkcave_escape`,
the rope timer), the bare `ogre_shaman` handlers, "Gorad has gone.", the guards'
teleports in and out, the lever's crystal deletion, the stage-1 page "Well, the
guards need to be dealt with".

## Stages (varp 212 `itwatchtower`)

Our stage numbers already equalled Jagex's; what moved is where each is written.

| value | set on | tick |
|---|---|---|
| 1 | the click after "I would be happy to.", with "You've started a new quest" | [789] |
| 2 | the click after "You'll need some way to make the guards let you in.", with the fingernails gone | [1220] |
| 3 | three ticks after the assembly's click (the wizard's seq and spotanim, an empty box, the synth), with the relic and its box | [4110] |
| 4 | the north-west guard's "It's got the statue of Dalgroth...", with the relic gone | [4276] |
| 5 | the city guard's riddle, its first page, with `watchtower_puzzle_chat` | [4986] |
| 6 | "The guard takes your death rune and gives you a map." | [5333] |
| 7 | the mad skavid's second page, with the crystal | [6330] |
| 8 | the nightshade on an enclave guard, his page | [7395] |
| 9 | the wizard's "Collect a guam leaf..." | [7879] |
| 10 | four ticks after "The wizard mutters strange words over the liquid...", with the potion swapped | [8352] |
| 11 | "Put the crystals on the pillars there..." | [8976] |
| 13 | the lever, a tick after its seq, with the pillars lit, the jingle and the move into the finished tower | [9120] |
| 14 | the scroll read | [9148] |

The quest's own varbits (all on varp 936, transmitted): `watchtower_guard` (the
tower guard's first let-through, [3403]), `gate_chat_1..3` ([1562], [1727], [4751]),
`puzzle_chat`, `tooth_got` ([2856]), `nightshade_chat` ("Alright!", [6560]),
`nightshade_used` (the run past, [7682]), `enclave_scene` ([7716]), `shaman_hit`
([7751]), `rock_mined` ([8789]), `shaman_1..6` ([8579..8707]), `pillar_1..4` (1 placed,
2 lit). The relic parts the wizard holds, the gold paid and the cake given go out in
no var: they stay in our server-side `varp6865`/`varp6866`.
`watchtower_crystal_variable` (3126) flips with journal opens ([8403], [8811]); not
written by us (OPEN).

## Pages

All 488 distinct recorded pages and menu rows are checked mechanically against the
scripts (recorded `<br>` read as a space): 476 are present verbatim. Of the 12 left,
3 are an artefact of the check (a `-<br>` the check joins; the scripts have the
wiki's "- it's", "- you", "- try") and 9 are not this quest: the watchman's
pickpocket line (a template, matches), the Zogre coffin guard and Uglug Nar
(Zogre Flesh Eaters), Keef (Monkey Madness II), the Pillory random event. Forced
breaks Jagex sends inside a short page are kept as `<br>` (14 pages:
"No gold, no passage.<br>Get out of this city!", the robe's boxes, ...).

## Interaction shapes (each checked on the recording; ours by construction)

| interaction | recorded | ticks |
|---|---|---|
| bush search | L+1 face; L+2 `hunting_searching_bushes`, the find and its page | [853..859] |
| trellis | L+1 both lines and `human_reachforladder`; L+3 the move to 2548,3117,1; 31 Agility xp | [636..642] |
| skavid cave, no map | L+1 face and fade out; F+3 the move out of another cave; F+5 fade in; F+8 the line, overlay closed | [1539..1548] |
| skavid cave, map | the line and the move on L+1; `darkness_medium` (light) or `darkness_dark` + minimap 2 (none) a tick later | [5748..5750], [5614] |
| dark, no light | "You hear tiny insects skittering..." + `insect_swarm` 16 ticks in; "Tiny biting insects swarm all over you!" 15 later; no damage in 30 ticks | [5641], [5656] |
| Toban's cave / ladder | the line and the move on the op (no climb anim), the cave's page | [2710], [2716..2718] |
| Toban's chest | L+1 face; L+2 `human_pickuptable` delay 5 + `unlock` delay 10; L+3 key for gold, `chest_open`, `chest_close` delay 60, `tobanchestopen` for one tick, the box | [2790..2797] |
| the push down the hill | P the line, the guard's `ogre_attack`, exactmove 7 tiles over 75 cycles with `human_blown_start`, fade out; P+4 move, `qip_watchtower_getup`, `stunned_thieving`; P+6 3 damage, fade in; P+8 seq cleared, "Urrrrrgh!", overlay closed | [1778..1786] (6 pushes) |
| a guard lets you through | his page, then the gate opened and walked through (synth `bigdoor_open` 5-7 ticks after) | [2359..2364], [4276..4283] |
| battlement | L+1 face; L+2 exactmove 2 tiles (30..90 cycles), `human_walk_style` delay 30, `climb_wall` | [5256..5262] |
| after the cake | the guard's page, then a run to the battlement and over it | [4779..4784] |
| bridge jump | the line, exactmove 4 tiles (0..45), `qip_watchtower_double_square_jump`, `jump`, 5 Agility xp, the page, one tick; back: no xp | [4868], [4872] |
| Gu'Tanoth chests | L+2 `human_openchest` + `chest_open`; L+3 `opentanothchest1/2` for 300 ticks with the content | [4812..4819], [4889..4893], [5192] |
| relic assembly | the click: the wizard's `qip_watchtower_wizard_relic` + `qip_watchtower_relic_combine` h50, a blank box, `watchtower_make_relic`; +3 the stage, the relic, the box | [4107..4110] |
| potion infusion | the click: the line and the chat closed; +4 the stage, the potion, the page | [8348..8352] |
| shaman kill | L+1 face; L+2 `qip_watchtower_pour_vial`; L+10 both lines, the varbit, the page (sixth: the scream, `human_pickupfloor` delay 1, the crystal, `watchtower_shaman_cry`, the box) | [8568..8579], [8697..8707] |
| shaman talk | the varbit, the blast line and his page on the op, 20 damage a tick later | [7751..7752] |
| Rock of Dalgroth | prospect: the line, its answer four ticks on; mine: the line and the pick, +4 the crystal, the varbit, the box | [7761..7765], [8784..8789] |
| pillar | wrong: the line two ticks after; right: `human_pickuptable`, then the crystal, the varbit, `poh_sphere_powerup` | [9043..9100] |
| lever | the seq, then the synth with the box or the completion | [9071..9078], [9117..9120] |
| completion | +4 coins, scroll, xp, the wizard's two pages; the scroll and `quest complete 3` on the second's click | [9124..9138] |
| scroll read | the stage, `qip_watchtower_read_scroll`, two boxes, the line | [9148..9152] |

The magic xp goes out with the completion click in ours, not with the first page:
our scroll mounts from a queue a tick after the click, and a level-up queued four
ticks earlier took its place (gate run 3). See Open.

## Scenes

| scene | trigger | in a copy | ticks |
|---|---|---|---|
| "Meanwhile, in the Shaman's enclave..." | the first run past the enclave guard (`watchtower_enclave_scene` 0) | 0_40_147 (the enclave), the player at (25,17) | W+3 lock and move to the entry, W+6 into the copy, W+8 the shaman (32,26) and the dragon (34,24), camera from (29,19) h500 at (37,30) h200, W+14 the box; from its click C: +2 breath, +5 30 on the dragon, +6 "Hur, hut, hur. Stoopid dragon!", +10 "Take dis!" and the spell (970, 0..50, angle 30, progress 10, heights 92), +11 "Hur, hur, hur, hur!" and the impact, +12 75, +13 the death, +14 fade, +17 the varbit and the move out, +19 the unlock |
| the scared skavid silenced | his "Here, I'll tell you what you need to know..." | no | C chat closed, `ogre_chieftan_caves` added beside him with a smokepuff; C+2 the swing and both overheads; C+6 the ogre gone and the skavid's last page |

Both scenes match the recording block for block on our wire (the npc block
inventory below).

## Journal (`itwatchtower_journal.rs2`)

Opened 39 times, 22 distinct row sets at stages 1, 2, 4, 5, 6, 7, 8, 9, 10 and 11;
the rows are copied from them with Jagex's colour tags and wraps (the older format:
a blank first row, blank rows between paragraphs). The title is "Watchtower" (ours
was "Watch Tower"). UNVERIFIED: stages 3, 13 and 14, and the rows for a relic,
dragon bones or no cake held.

## Npc info (`npc_block_inventory.py` 4527 against gate run 3's tick log)

| npc | kind | recorded | ours | verdict |
|---|---|---|---|---|
| watchtower_cutscene_blue_dragon | seq, spotanim, hit, headbar, enabled_ops, facing | 2/2/2/2/1/1 | 2/2/2/2/1/1 | matched |
| watchtower_cutscene_ogre_shaman | say (3 lines), seq, facing | 3/1/2 | 3/1/2 | matched |
| scared_skavid, ogre_chieftan_caves | say, seq, facing | 1/1/4, 1/1/1 | 1/1/2, 1/1/1 | matched (facing: page turns) |
| watchtower_wizard | seq + spotanim (assembly) | 1/1 | 1/1 | matched |
| ogre_guard1..3 | sequence | 2/4/7 | 0 | fixed after run 3: the push's `ogre_attack` (6/6); the rest are the recorder's fights |
| city_guard, enclave_guard, toban | sequence, hit, headbar, enabled_ops | fights | 0 | the recorder's fights and "Die, creature!"; the test takes none |
| gorad | seq, hit, headbar | 18/14/14 | 49/24/24 | a longer fight on our side (the test's bot); not compared per swing |
| qip_watchtower_ogre_shaman_01..06 | facing (7 each), say (Attack), enabled_ops/seq (5, 6) | | 0 | OPEN: the shamans' facing; the Attack overhead is built but the test does not attack |
| tower_guard, qip_watchtower_wizard_03/04 | facing | 10, 2/22 | 0 | the recorder talked to them; the test does not (page turns are the engine's) |

## Sounds

Every quest synth the recording sends that the test reaches is in our tick log
(`swing_across` 4/4, `poh_sphere_powerup` 4/4, `watchtower_*` 5/5, `unlock`,
`chest_open`/`chest_close`, `lever`, `dragon_death`). Built but not reached by the
test: `climb_under` (the Yanille hole, generic), `jump` back, `locked`,
`insect_swarm`, `exploding_vial`, `fuse`, `prospect` (rocks3, generic).

## Action inventory (`rsprox_actions.py`), a verdict per row

The rows marked "generic content in the quest's area" each get a verdict here:

| action | verdict |
|---|---|
| `spiralstairsbottom_3`, `xbowsdoubledoor*`, `watchshortcut`, `yanille_castlehole_sc`/`castlewall_sc`, `varrock_sc_tunnel_west`, `tree_ropeswing3` | generic: bound by category (stairs, double doors, agility shortcuts, rope swings); not re-audited here |
| `tree` (1276), `tree2` (1278), `rocks1` | generic: woodcutting / mining categories |
| `rocks3` (11392) "There is currently no ore available in this rock." | OPEN generic: no binding |
| `crate3_old`, `xbows_yanille_castlewall`, `empty_rock_cake_counter` | the default ("Nothing interesting happens." recorded for the counter, [4396]) / unbound; no recorded answer for the crate or wall |
| `tree_forropeswing1` + rope | default "Nothing interesting happens." ([3526]) |
| Colonel Radick | fixed: his first page and menu ([496..501]) |
| yanille_watchman pickpocket / attack | generic thieving (matches) and combat |
| `ogre2`, `ogre`, `spider`, `greater_demon` attacks | the recorder's fights |
| ogre merchants and traders, `rockcounter_withcakes`, `ogre_chieftan` | generic Gu'Tanoth market (yanille_shop_stubs.rs2, yanille_thin_npcs.rs2): the trader's page fixed |
| `ii_impling_type_3`, `knife`, `adamant_dart`, `rottenapples` drop, `mm2_keef_multi` | generic / other content (Keef's page is Monkey Madness II's: OPEN there) |
| Zogre coffin guard, Uglug Nar | other content (Zogre Flesh Eaters): OPEN there |
| Pillory guard and pillory | random event: OPEN there |

The full table:

| first | count | action | verdict |
|---|---|---|---|
| [33] | x1 | `[if_buttonx] com=welcome_screen:play (378:72), op=1` | engine/generic UI |
| [64] | x43 | `[if_buttonx] com=questlist:list (399:7), op=2` | engine/generic UI |
| [68] | x2 | `[if_buttonx] com=questjournal_overview:close (782:9), op=1` | engine/generic UI |
| [82] | x1 | `[if_buttonx] com=inventory:items (149:0), obj=poh_tablet_lumbridgeteleport (8008), op=2` | generic: teleport jewellery/tablets (the recorder's travel; not re-audited here) |
| [93] | x1 | `[oploc2_v3] id=spiralstairsbottom_3 (56230)` | generic content in the quest's area: see the verdict list in the doc |
| [118] | x2 | `[oploc2_v3] id=aide_bankbooth (18491)` | generic: bank / GE (the recorder's errands) |
| [124] | x1 | `[if_buttonx] com=bankmain:depositinv (12:47), op=1` | generic: bank / GE (the recorder's errands) |
| [125] | x1 | `[if_buttonx] com=bankmain:depositworn (12:49), op=1` | generic: bank / GE (the recorder's errands) |
| [128] | x1 | `[oploc3_v3] id=aide_bankbooth (18491)` | generic: bank / GE (the recorder's errands) |
| [145] | x4 | `[if_buttonx] com=bankmain:tabs (12:10), obj=poh_tablet_kourendteleport (28790), op=1` | generic: bank / GE (the recorder's errands) |
| [158] | x1 | `[if_buttonx] com=bankmain:items (12:12), obj=placeholder_osrs_bond (15430), op=8` | generic: bank / GE (the recorder's errands) |
| [169] | x3 | `[if_buttonx] com=bankmain:items (12:12), obj=poh_tablet_watchtowerteleport (8012), op=1` | this content (see its section) |
| [171] | x1 | `[if_buttonx] com=bankmain:items (12:12), obj=poh_tablet_watchtowerteleport (8012), op=3` | this content (see its section) |
| [177] | x2 | `[if_buttonx] com=bankmain:tabs (12:10), obj=adamant_knife (867), op=1` | generic: bank / GE (the recorder's errands) |
| [187] | x2 | `[if_buttonx] com=bankmain:items (12:12), obj=red_dragonhide_chaps (2495), op=1` | generic: bank / GE (the recorder's errands) |
| [188] | x2 | `[if_buttonx] com=bankmain:items (12:12), obj=red_dragonhide_body (2501), op=1` | generic: bank / GE (the recorder's errands) |
| [192] | x1 | `[if_buttonx] com=bankside:items (15:3), obj=red_dragonhide_chaps (2495), op=2` | generic: bank / GE (the recorder's errands) |
| [192] | x1 | `[if_buttonx] com=bankside:items (15:3), obj=red_dragonhide_body (2501), op=2` | generic: bank / GE (the recorder's errands) |
| [195] | x1 | `[if_buttonx] com=bankmain:items (12:12), obj=poh_tablet_varrockteleport (8007), op=1` | generic: bank / GE (the recorder's errands) |
| [201] | x1 | `[if_buttonx] com=side_journal:task_list (629:18), op=1` | engine/generic UI |
| [208] | x1 | `[if_buttonx] com=inventory:items (149:0), obj=poh_tablet_varrockteleport (8007), op=2` | generic: teleport jewellery/tablets (the recorder's travel; not re-audited here) |
| [285] | x3 | `[opnpc3_v2] npc=(id=ge_clerk_1 (2148), )` | generic: bank / GE (the recorder's errands) |
| [288] | x6 | `[opnpc3_v2] npc=(id=banker1_east (1634), )` | generic: bank / GE (the recorder's errands) |
| [296] | x10 | `[if_buttonx] com=ge_offers:index_5 (465:12), op=1` | generic: bank / GE (the recorder's errands) |
| [305] | x1 | `[resume_p_objdialog] id=4dose3antidragon (21978)` | generic: bank / GE (the recorder's errands) |
| [309] | x41 | `[if_buttonx] com=ge_offers:setup (465:26), op=1` | generic: bank / GE (the recorder's errands) |
| [323] | x1 | `[resume_p_objdialog] id=4dose1antidragon (2452)` | generic: bank / GE (the recorder's errands) |
| [328] | x7 | `[if_buttonx] com=ge_offers:setup_confirm (465:30), op=1` | generic: bank / GE (the recorder's errands) |
| [336] | x2 | `[if_buttonx] com=ge_offers:collectall (465:6), op=1` | generic: bank / GE (the recorder's errands) |
| [340] | x11 | `[if_buttonx] com=bankmain:tabs (12:10), obj=4doseprayerrestore (2434), op=1` | generic: bank / GE (the recorder's errands) |
| [342] | x1 | `[if_buttonx] com=bankside:items (15:3), obj=cert_4dose1antidragon (2453), op=6` | generic: bank / GE (the recorder's errands) |
| [343] | x2 | `[if_buttonx] com=bankmain:items (12:12), obj=4dose1antidragon (2452), op=1` | generic: bank / GE (the recorder's errands) |
| [358] | x5 | `[if_buttonx] com=bankmain:tabs (12:10), op=1` | generic: bank / GE (the recorder's errands) |
| [375] | x1 | `[resume_p_objdialog] id=4dosestamina (12625)` | generic: bank / GE (the recorder's errands) |
| [383] | x1 | `[if_buttonx] com=ge_offers:details_collect (465:24), obj=4dosestamina (12625), op=1` | generic: bank / GE (the recorder's errands) |
| [383] | x4 | `[if_buttonx] com=ge_offers:details_collect (465:24), obj=coins (995), op=2` | generic: bank / GE (the recorder's errands) |
| [389] | x1 | `[if_buttonx] com=bankside:items (15:3), obj=cert_4dosestamina (12626), op=8` | generic: bank / GE (the recorder's errands) |
| [391] | x1 | `[if_buttonx] com=bankside:items (15:3), obj=coins (995), op=8` | generic: bank / GE (the recorder's errands) |
| [393] | x2 | `[if_buttonx] com=bankmain:items (12:12), obj=4dosestamina (12625), op=3` | generic: bank / GE (the recorder's errands) |
| [398] | x1 | `[if_buttonx] com=bankmain:tabs (12:10), obj=adamant_scimitar (1331), op=1` | generic: bank / GE (the recorder's errands) |
| [403] | x1 | `[if_buttonx] com=bankmain:items (12:12), obj=ring_of_dueling_1 (2566), op=1` | generic: bank / GE (the recorder's errands) |
| [409] | x1 | `[if_buttonx] com=bankmain:items (12:12), obj=amulet_of_glory_3 (1710), op=1` | generic: bank / GE (the recorder's errands) |
| [411] | x1 | `[if_buttonx] com=inventory:items (149:0), obj=ring_of_dueling_1 (2566), op=3` | generic: teleport jewellery/tablets (the recorder's travel; not re-audited here) |
| [411] | x1 | `[if_buttonx] com=inventory:items (149:0), obj=amulet_of_glory_3 (1710), op=3` | generic: teleport jewellery/tablets (the recorder's travel; not re-audited here) |
| [413] | x1 | `[if_buttonx] com=inventory:items (149:0), obj=poh_tablet_watchtowerteleport (8012), op=2` | this content (see its section) |
| [421] | x1 | `[if_buttonx] com=bankside:items (15:3), obj=poh_tablet_watchtowerteleport (8012), op=8` | this content (see its section) |
| [423] | x1 | `[if_buttonx] com=bankmain:items (12:12), obj=ring_of_dueling_8 (2552), op=1` | generic: bank / GE (the recorder's errands) |
| [429] | x7 | `[if_buttonx] com=wornitems:slot12 (387:24), op=3` | generic: teleport jewellery/tablets (the recorder's travel; not re-audited here) |
| [433] | x1 | `[if_buttonx] com=inventory:items (149:0), obj=ring_of_dueling_8 (2552), op=3` | generic: teleport jewellery/tablets (the recorder's travel; not re-audited here) |
| [490] | x8 | `[if_buttonx] com=inventory:items (149:0), obj=4dosestamina (12625), op=2` | the recorder's own kit (food, potions, prayers, gear) |
| [491] | x1 | `[oploc1_v3] id=crate3_old (358)` | generic content in the quest's area: see the verdict list in the doc |
| [491] | x14 | `[oploc1_v3] id=xbowsdoubledoorr (17093)` | generic content in the quest's area: see the verdict list in the doc |
| [494] | x8 | `[oploc1_v3] id=xbowsdoubledoorl (17091)` | generic content in the quest's area: see the verdict list in the doc |
| [496] | x1 | `[opnpc1_v2] npc=(id=colonel_radick (4406), )` | generic content in the quest's area: see the verdict list in the doc |
| [509] | x16 | `[if_buttonx] com=prayerbook:prayer15 (541:23), op=1` | the recorder's own kit (food, potions, prayers, gear) |
| [522] | x10 | `[oploc1_v3] id=watchshortcut (23543)` | generic content in the quest's area: see the verdict list in the doc |
| [539] | x5 | `[opnpc1_v2] npc=(id=tower_guard (4405), )` | this content (see its section) |
| [544] | x1 | `[opobj3_v2] id=lit_candle (33)` | this content (see its section) |
| [560] | x10 | `[oploc1_v3] id=towerladder (2833)` | this content (see its section) |
| [569] | x54 | `[oploc1_v3] id=watchtowerbush (2798)` | this content (see its section) |
| [610] | x8 | `[oploc1_v3] id=watchtowerbushnail (2799)` | this content (see its section) |
| [636] | x7 | `[oploc1_v3] id=qip_watchtower_trellis_base (20056)` | this content (see its section) |
| [646] | x1 | `[opnpc3_v2] npc=(id=yanille_watchman (5420), )` | generic content in the quest's area: see the verdict list in the doc |
| [651] | x17 | `[oploc1_v3] id=qip_watchtower_ladder_top (17122)` | this content (see its section) |
| [669] | x13 | `[oploc1_v3] id=watchladderup (2796)` | this content (see its section) |
| [683] | x15 | `[opnpc1_v2] npc=(id=qip_watchtower_wizard_04 (4400), )` | this content (see its section) |
| [688] | x1 | `[opnpc1_v2] npc=(id=qip_watchtower_wizard_03 (4399), )` | this content (see its section) |
| [695] | x35 | `[opnpc1_v2] npc=(id=watchtower_wizard (4397), )` | this content (see its section) |
| [820] | x9 | `[oploc1_v3] id=watchladderdown (2797)` | this content (see its section) |
| [853] | x3 | `[oploc1_v3] id=watchtowerbushdagger (2800)` | this content (see its section) |
| [860] | x1 | `[if_buttonx] com=side_journal:quest_list (629:10), op=1` | engine/generic UI |
| [881] | x39 | `[if_buttonx] com=questjournal:close (119:8), op=1` | engine/generic UI |
| [895] | x2 | `[oploc1_v3] id=watchtowerbusheyepatch (2801)` | this content (see its section) |
| [973] | x2 | `[oploc1_v3] id=watchtowerbushrobe (2802)` | this content (see its section) |
| [1044] | x1 | `[if_buttonx] com=inventory:items (149:0), obj=watchtowerrobe (2385), op=10` | this content (see its section) |
| [1064] | x3 | `[opnpc2_v2] npc=(id=yanille_watchman (5420), )` | generic content in the quest's area: see the verdict list in the doc |
| [1071] | x1 | `[if_buttonx] com=inventory:items (149:0), obj=fingernails (2384), op=7` | this content (see its section) |
| [1085] | x1 | `[opnpct] npc=(id=watchtower_wizard (4397), ), com=inventory:items (149:0), obj=watch_eye_patch (2388)` | this content (see its section) |
| [1092] | x1 | `[opnpct] npc=(id=watchtower_wizard (4397), ), com=inventory:items (149:0), obj=watchtowerdagger (2387)` | this content (see its section) |
| [1109] | x1 | `[if_buttonx] com=inventory:items (149:0), obj=watchtowerrobe (2385), op=7` | this content (see its section) |
| [1110] | x1 | `[if_buttonx] com=inventory:items (149:0), obj=watch_eye_patch (2388), op=7` | this content (see its section) |
| [1112] | x1 | `[if_buttonx] com=inventory:items (149:0), obj=watchtowerdagger (2387), op=7` | this content (see its section) |
| [1125] | x1 | `[if_buttonx] com=inventory:items (149:0), obj=fingernails (2384), op=10` | this content (see its section) |
| [1251] | x8 | `[oploc1_v3] id=yanille_castlehole_sc (16520)` | generic content in the quest's area: see the verdict list in the doc |
| [1253] | x6 | `[if_buttonx] com=inventory:items (149:0), obj=3dosestamina (12627), op=2` | the recorder's own kit (food, potions, prayers, gear) |
| [1290] | x3 | `[oploc2_v3] id=bankbooth (10355)` | generic: bank / GE (the recorder's errands) |
| [1301] | x1 | `[if_buttonx] com=bankmain:tabs (12:10), obj=ham_hood (4302), op=1` | generic: bank / GE (the recorder's errands) |
| [1304] | x5 | `[if_buttonx] com=bankmain:items (12:12), obj=4doseprayerrestore (2434), op=1` | generic: bank / GE (the recorder's errands) |
| [1311] | x1 | `[if_buttonx] com=bankmain:items (12:12), obj=robinhoodhat (2581), op=1` | generic: bank / GE (the recorder's errands) |
| [1311] | x1 | `[if_buttonx] com=bankmain:items (12:12), obj=zamorakbook_complete (3842), op=1` | generic: bank / GE (the recorder's errands) |
| [1313] | x1 | `[if_buttonx] com=bankmain:items (12:12), obj=adamant_dart (810), op=7` | generic: bank / GE (the recorder's errands) |
| [1314] | x1 | `[if_buttonx] com=inventory:items (149:0), obj=red_dragonhide_chaps (2495), op=3` | the recorder's own kit (wielding gear) |
| [1314] | x1 | `[if_buttonx] com=inventory:items (149:0), obj=red_dragonhide_body (2501), op=3` | the recorder's own kit (wielding gear) |
| [1315] | x1 | `[if_buttonx] com=inventory:items (149:0), obj=adamant_dart (810), op=3` | the recorder's own kit (wielding gear) |
| [1315] | x1 | `[if_buttonx] com=inventory:items (149:0), obj=robinhoodhat (2581), op=3` | the recorder's own kit (wielding gear) |
| [1316] | x1 | `[if_buttonx] com=inventory:items (149:0), obj=zamorakbook_complete (3842), op=3` | the recorder's own kit (wielding gear) |
| [1319] | x1 | `[if_buttonx] com=bankmain:items (12:12), obj=anma_30_reward (10498), op=1` | generic: bank / GE (the recorder's errands) |
| [1323] | x1 | `[if_buttonx] com=bankmain:items (12:12), obj=4doserangerspotion (2444), op=1` | generic: bank / GE (the recorder's errands) |
| [1325] | x1 | `[if_buttonx] com=inventory:items (149:0), obj=anma_30_reward (10498), op=3` | the recorder's own kit (wielding gear) |
| [1331] | x1 | `[if_buttonx] com=bankmain:tabs (12:10), obj=lobster (379), op=1` | generic: bank / GE (the recorder's errands) |
| [1333] | x1 | `[if_buttonx] com=bankmain:items (12:12), obj=trout (333), op=4` | generic: bank / GE (the recorder's errands) |
| [1334] | x1 | `[if_buttonx] com=bankmain:items (12:12), obj=lobster (379), op=4` | generic: bank / GE (the recorder's errands) |
| [1336] | x13 | `[if_buttonx] com=inventory:items (149:0), obj=trout (333), op=2` | the recorder's own kit (food, potions, prayers, gear) |
| [1401] | x2 | `[oploc1_v3] id=tree (1276)` | generic content in the quest's area: see the verdict list in the doc |
| [1519] | x6 | `[if_buttonx] com=inventory:items (149:0), obj=2dosestamina (12629), op=2` | the recorder's own kit (food, potions, prayers, gear) |
| [1522] | x2 | `[if_buttonx] com=inventory:items (149:0), obj=4doseprayerrestore (2434), op=2` | the recorder's own kit (food, potions, prayers, gear) |
| [1535] | x15 | `[oploc1_v3] id=skavid_cave3 (2807)` | this content (see its section) |
| [1547] | x20 | `[oploc1_v3] id=skavid_cave4 (2808)` | this content (see its section) |
| [1558] | x8 | `[opnpc1_v2] npc=(id=ogre_guard1 (4369), )` | this content (see its section) |
| [1568] | x3 | `[oploc1_v3] id=ogreguardgate1right (2787)` | this content (see its section) |
| [1574] | x4 | `[oploc1_v3] id=ogreguardgate1 (2786)` | this content (see its section) |
| [1657] | x2 | `[oploc1_v3] id=tobanblock (2834)` | this content (see its section) |
| [1667] | x5 | `[oploc1_v3] id=skavid_cave1 (2805)` | this content (see its section) |
| [1724] | x7 | `[oploc1_v3] id=ogreguardgate2 (2788)` | this content (see its section) |
| [1753] | x6 | `[if_buttonx] com=inventory:items (149:0), obj=1dosestamina (12631), op=2` | the recorder's own kit (food, potions, prayers, gear) |
| [1772] | x6 | `[opnpc1_v2] npc=(id=ogre_guard2 (4370), )` | this content (see its section) |
| [1824] | x11 | `[opnpc1_v2] npc=(id=og (4364), )` | this content (see its section) |
| [1836] | x4 | `[opnpc2_v2] npc=(id=ogre2 (2096), )` | generic content in the quest's area: see the verdict list in the doc |
| [1839] | x4 | `[if_buttonx] com=prayerbook:prayer21 (541:29), op=1` | the recorder's own kit (food, potions, prayers, gear) |
| [1840] | x1 | `[if_buttonx] com=combat_interface:1 (593:10), op=1` | engine/generic UI |
| [1904] | x1 | `[if_buttonx] com=inventory:items (149:0), obj=toban_key (2378), op=7` | this content (see its section) |
| [1968] | x2 | `[oploc6_v2] id=tree_forropeswing1 (23639)` | generic content in the quest's area: see the verdict list in the doc |
| [1971] | x4 | `[oploc1_v3] id=tree_ropeswing3 (23570)` | this content (see its section) |
| [1984] | x1 | `[oploc6_v2] id=tree_ropeswing4_norope (23638)` | this content (see its section) |
| [2001] | x2 | `[if_buttonx] com=wornitems:slot2 (387:17), op=2` | generic: teleport jewellery/tablets (the recorder's travel; not re-audited here) |
| [2088] | x1 | `[oploc1_v3] id=tree2 (1278)` | generic content in the quest's area: see the verdict list in the doc |
| [2096] | x2 | `[oploc1_v3] id=varrock_sc_tunnel_west (16529)` | generic content in the quest's area: see the verdict list in the doc |
| [2113] | x8 | `[opnpc3_v2] npc=(id=deadman_banker_grey_west (6528)), )` | generic: bank / GE (the recorder's errands) |
| [2122] | x3 | `[if_buttonx] com=bankmain:items (12:12), obj=rope (954), op=1` | generic: bank / GE (the recorder's errands) |
| [2123] | x1 | `[if_buttonx] com=bankmain:tabs (12:10), obj=iron_ore (440), op=1` | generic: bank / GE (the recorder's errands) |
| [2124] | x1 | `[if_buttonx] com=bankmain:items (12:12), obj=gold_bar (2357), op=1` | this content (see its section) |
| [2133] | x3 | `[opnpc3_v2] npc=(id=ge_clerk_4 (2151), )` | generic: bank / GE (the recorder's errands) |
| [2139] | x1 | `[resume_p_objdialog] id=shark (385)` | generic: bank / GE (the recorder's errands) |
| [2148] | x1 | `[if_buttonx] com=ge_offers:details_collect (465:24), obj=shark (385), op=1` | generic: bank / GE (the recorder's errands) |
| [2152] | x3 | `[if_buttonx] com=bankmain:tabs (12:10), obj=placeholder_lobster (15467), op=1` | generic: bank / GE (the recorder's errands) |
| [2154] | x1 | `[if_buttonx] com=bankside:items (15:3), obj=cert_shark (386), op=6` | generic: bank / GE (the recorder's errands) |
| [2157] | x1 | `[if_buttonx] com=bankmain:items (12:12), obj=shark (385), op=4` | generic: bank / GE (the recorder's errands) |
| [2159] | x1 | `[if_buttonx] com=bankside:items (15:3), obj=trout (333), op=4` | generic: bank / GE (the recorder's errands) |
| [2206] | x2 | `[oploct_v2] id=tree_ropeswing4_norope (23638), , com=inventory:items (149:0), obj=rope (954)` | this content (see its section) |
| [2248] | x8 | `[opnpc1_v2] npc=(id=grew (4365), )` | this content (see its section) |
| [2378] | x2 | `[oploc1_v3] id=skavid_cave6 (2810)` | this content (see its section) |
| [2468] | x5 | `[if_buttonx] com=orbs:worldmap (160:55), op=2` | engine/generic UI |
| [2477] | x1 | `[if_buttonx] com=worldmap:esckey (595:4), op=1` | engine/generic UI |
| [2479] | x2 | `[oploc1_v3] id=skavid_cave5 (2809)` | this content (see its section) |
| [2488] | x2 | `[opnpc2_v2] npc=(id=ogre (2095), )` | generic content in the quest's area: see the verdict list in the doc |
| [2542] | x1 | `[opnpc1_v2] npc=(id=ii_impling_type_3 (1637), )` | generic content in the quest's area: see the verdict list in the doc |
| [2603] | x9 | `[oploc1_v3] id=tobancave (2811)` | this content (see its section) |
| [2637] | x1 | `[opnpc1_v2] npc=(id=zogre_ogre_coffin_guard (865), )` | generic content in the quest's area: see the verdict list in the doc |
| [2647] | x1 | `[opnpc1_v2] npc=(id=zogre_dying1 (862), )` | generic content in the quest's area: see the verdict list in the doc |
| [2651] | x1 | `[opnpc1_v2] npc=(id=zogre_ogre_shaman (860), )` | this content (see its section) |
| [2654] | x1 | `[opnpc3_v2] npc=(id=zogre_uglug_nar (861), )` | generic content in the quest's area: see the verdict list in the doc |
| [2716] | x4 | `[oploc1_v3] id=tobanladderdown (2812)` | this content (see its section) |
| [2724] | x5 | `[opnpc1_v2] npc=(id=toban (4366), )` | this content (see its section) |
| [2773] | x4 | `[opnpc1_v2] npc=(id=gorad (4367), )` | this content (see its section) |
| [2790] | x3 | `[oploc1_v3] id=tobanchest (2790)` | this content (see its section) |
| [2815] | x1 | `[opnpct] npc=(id=toban (4366), ), com=inventory:items (149:0), obj=stolen_gold (2393)` | this content (see its section) |
| [2823] | x6 | `[opnpc2_v2] npc=(id=gorad (4367), )` | this content (see its section) |
| [2832] | x1 | `[if_buttonx] com=inventory:items (149:0), obj=4doserangerspotion (2444), op=2` | the recorder's own kit (food, potions, prayers, gear) |
| [2860] | x1 | `[opobj3_v2] id=adamant_dart (810)` | generic content in the quest's area: see the verdict list in the doc |
| [2863] | x1 | `[if_buttonx] com=inventory:items (149:0), obj=ogretooth (2377), op=7` | this content (see its section) |
| [2907] | x1 | `[if_buttonx] com=inventory:items (149:0), obj=stolen_gold (2393), op=7` | this content (see its section) |
| [2957] | x4 | `[if_buttonx] com=inventory:items (149:0), obj=lobster (379), op=2` | the recorder's own kit (food, potions, prayers, gear) |
| [3056] | x6 | `[if_buttonx] com=orbs:runbutton (160:28), op=1` | engine/generic UI |
| [3113] | x1 | `[if_buttonx] com=inventory:items (149:0), obj=relicpart1 (2373), op=10` | this content (see its section) |
| [3117] | x1 | `[if_buttonx] com=inventory:items (149:0), obj=relicpart1 (2373), op=7` | this content (see its section) |
| [3245] | x1 | `[opnpc2_v2] npc=(id=colonel_radick (4406), )` | generic content in the quest's area: see the verdict list in the doc |
| [3302] | x2 | `[oploc2_v3] id=bankbooth_multi (10356)` | generic: bank / GE (the recorder's errands) |
| [3352] | x1 | `[if_buttonx] com=bankmain:items (12:12), obj=bronze_pickaxe (1265), op=1` | generic: bank / GE (the recorder's errands) |
| [3525] | x1 | `[oploct_v2] id=tree_forropeswing1 (23639), , com=inventory:items (149:0), obj=rope (954)` | generic content in the quest's area: see the verdict list in the doc |
| [3581] | x1 | `[if_buttonx] com=inventory:items (149:0), obj=powering_crystal1 (2380), op=7` | this content (see its section) |
| [3584] | x1 | `[if_buttonx] com=inventory:items (149:0), obj=relicpart2 (2374), op=7` | this content (see its section) |
| [3683] | x1 | `[opnpct] npc=(id=ogre_guard2 (4370), ), com=inventory:items (149:0), obj=relicpart2 (2374)` | this content (see its section) |
| [3733] | x3 | `[opnpct] npc=(id=ogre_guard2 (4370), ), com=inventory:items (149:0), obj=powering_crystal1 (2380)` | this content (see its section) |
| [3790] | x2 | `[if_buttonx] com=inventory:items (149:0), obj=3doseprayerrestore (139), op=2` | the recorder's own kit (food, potions, prayers, gear) |
| [3791] | x5 | `[oploc1_v3] id=castlewars_bankchest (4483)` | generic: bank / GE (the recorder's errands) |
| [3799] | x1 | `[if_buttonx] com=bankmain:items (12:12), obj=dragon_bones (536), op=1` | this content (see its section) |
| [3900] | x1 | `[if_buttont] selectcom=inventory:items (149:0), selectedselectedobj=relicpart3 (2375), targetcom=inventory:items (149:0), targettargetobj=relicpart2 (2374)` | this content (see its section) |
| [3937] | x1 | `[if_buttont] selectcom=inventory:items (149:0), selectedselectedobj=powering_crystal1 (2380), targetcom=inventory:items (149:0), targettargetobj=relicpart2 (237` | this content (see its section) |
| [3941] | x1 | `[if_buttonx] com=inventory:items (149:0), obj=powering_crystal1 (2380), op=10` | this content (see its section) |
| [4034] | x1 | `[oploc1_v3] id=yanille_castlewall_sc (16519)` | generic content in the quest's area: see the verdict list in the doc |
| [4073] | x2 | `[opnpct] npc=(id=watchtower_wizard (4397), ), com=inventory:items (149:0), obj=powering_crystal1 (2380)` | this content (see its section) |
| [4116] | x2 | `[if_buttonx] com=inventory:items (149:0), obj=ogrerelic (2372), op=7` | this content (see its section) |
| [4286] | x2 | `[oploc1_v3] id=ogreguardgate2right (2789)` | this content (see its section) |
| [4309] | x6 | `[opnpc1_v2] npc=(id=ogre_chieftan (4362), )` | generic content in the quest's area: see the verdict list in the doc |
| [4325] | x1 | `[opnpc3_v2] npc=(id=ogre_merchant (4402), )` | generic content in the quest's area: see the verdict list in the doc |
| [4327] | x1 | `[opnpc1_v2] npc=(id=ogre_merchant (4402), )` | generic content in the quest's area: see the verdict list in the doc |
| [4338] | x1 | `[opnpc1_v2] npc=(id=falseogretrader (4404), )` | generic content in the quest's area: see the verdict list in the doc |
| [4344] | x2 | `[opnpc3_v2] npc=(id=falseogretrader (4404), )` | generic content in the quest's area: see the verdict list in the doc |
| [4358] | x1 | `[opnpc3_v2] npc=(id=ogre_trader1 (4401), )` | generic content in the quest's area: see the verdict list in the doc |
| [4359] | x1 | `[opnpc1_v2] npc=(id=ogre_trader1 (4401), )` | generic content in the quest's area: see the verdict list in the doc |
| [4373] | x1 | `[opnpc1_v2] npc=(id=ogre_trader2 (4403), )` | generic content in the quest's area: see the verdict list in the doc |
| [4380] | x1 | `[opnpc3_v2] npc=(id=ogre_trader2 (4403), )` | generic content in the quest's area: see the verdict list in the doc |
| [4385] | x7 | `[oploc2_v3] id=rockcounter_withcakes (2793)` | generic content in the quest's area: see the verdict list in the doc |
| [4390] | x1 | `[if_buttonx] com=inventory:items (149:0), obj=rockcake (2379), op=2` | this content (see its section) |
| [4396] | x1 | `[oploc2_v3] id=empty_rock_cake_counter (2792)` | generic content in the quest's area: see the verdict list in the doc |
| [4402] | x1 | `[if_buttonx] com=inventory:items (149:0), obj=rockcake (2379), op=4` | this content (see its section) |
| [4416] | x2 | `[if_buttonx] com=inventory:items (149:0), obj=rockcake (2379), op=7` | this content (see its section) |
| [4422] | x1 | `[opnpct] npc=(id=ogre_trader2 (4403), ), com=inventory:items (149:0), obj=rockcake (2379)` | this content (see its section) |
| [4426] | x3 | `[opnpc2_v2] npc=(id=ogre_chieftan (4362), )` | generic content in the quest's area: see the verdict list in the doc |
| [4434] | x5 | `[opnpc1_v2] npc=(id=enclave_guard (4381), )` | this content (see its section) |
| [4448] | x14 | `[oploc1_v3] id=shamanentrance (2804)` | this content (see its section) |
| [4489] | x8 | `[opnpc1_v2] npc=(id=ogre_guard3 (4371), )` | this content (see its section) |
| [4495] | x5 | `[oploc1_v3] id=ganothbattlement (2832)` | this content (see its section) |
| [4528] | x8 | `[if_buttonx] com=inventory:items (149:0), obj=shark (385), op=2` | the recorder's own kit (food, potions, prayers, gear) |
| [4621] | x2 | `[if_buttonx] com=worldmap:close (595:38), op=1` | engine/generic UI |
| [4795] | x2 | `[opnpc1_v2] npc=(id=ogre_guard4 (4372), )` | this content (see its section) |
| [4812] | x1 | `[oploc1_v3] id=shuttanothchest2 (2827)` | this content (see its section) |
| [4843] | x5 | `[oploc1_v3] id=tanothjump1 (2830)` | this content (see its section) |
| [4871] | x4 | `[oploc1_v3] id=tanothjump2 (2831)` | this content (see its section) |
| [4889] | x1 | `[oploc1_v3] id=shuttanothchest1 (2826)` | this content (see its section) |
| [4895] | x2 | `[opnpc2_v2] npc=(id=spider (3019), )` | generic content in the quest's area: see the verdict list in the doc |
| [4915] | x11 | `[opnpc1_v2] npc=(id=city_guard (4373), )` | this content (see its section) |
| [4945] | x1 | `[opobj3_v2] id=knife (946)` | generic content in the quest's area: see the verdict list in the doc |
| [4965] | x2 | `[opnpc1_v2] npc=(id=mm2_keef_multi (1451)), )` | generic content in the quest's area: see the verdict list in the doc |
| [5026] | x1 | `[opnpc2_v2] npc=(id=city_guard (4373), )` | this content (see its section) |
| [5080] | x4 | `[if_buttonx] com=bankmain:items (12:12), obj=deathrune (560), op=1` | this content (see its section) |
| [5294] | x1 | `[if_buttont] selectcom=inventory:items (149:0), selectedselectedobj=deathrune (560), targetcom=inventory:items (149:0), targettargetobj=deathrune (560)` | this content (see its section) |
| [5294] | x1 | `[if_buttonx] com=inventory:items (149:0), obj=deathrune (560), op=7` | this content (see its section) |
| [5298] | x1 | `[opnpct] npc=(id=city_guard (4373), ), com=inventory:items (149:0), obj=shark (385)` | this content (see its section) |
| [5302] | x1 | `[opnpct] npc=(id=city_guard (4373), ), com=inventory:items (149:0), obj=rope (954)` | this content (see its section) |
| [5306] | x1 | `[opnpct] npc=(id=city_guard (4373), ), com=inventory:items (149:0), obj=bronze_pickaxe (1265)` | this content (see its section) |
| [5308] | x1 | `[opobj3_v2] id=deathrune (560)` | this content (see its section) |
| [5311] | x1 | `[opnpct] npc=(id=city_guard (4373), ), com=inventory:items (149:0), obj=deathrune (560)` | this content (see its section) |
| [5384] | x1 | `[if_buttonx] com=inventory:items (149:0), obj=skavidmap (2376), op=10` | this content (see its section) |
| [5387] | x3 | `[if_buttonx] com=inventory:items (149:0), obj=skavidmap (2376), op=7` | this content (see its section) |
| [5402] | x1 | `[if_buttonx] com=inventory:items (149:0), obj=rottenapples (1984), op=7` | generic content in the quest's area: see the verdict list in the doc |
| [5453] | x2 | `[if_buttonx] com=inventory:items (149:0), obj=skavidmap (2376), op=2` | this content (see its section) |
| [5593] | x1 | `[if_buttonx] com=inventory:items (149:0), obj=lit_candle (33), op=2` | this content (see its section) |
| [5618] | x4 | `[oploc1_v3] id=cave4exit (2820)` | this content (see its section) |
| [5642] | x5 | `[opnpc1_v2] npc=(id=skavidtalker3 (4378), )` | this content (see its section) |
| [5653] | x2 | `[oploc1_v3] id=rocks3 (11392)` | generic content in the quest's area: see the verdict list in the doc |
| [5684] | x2 | `[if_buttonx] com=bankside:items (15:3), obj=4dosestamina (12625), op=2` | generic: bank / GE (the recorder's errands) |
| [5687] | x1 | `[if_buttonx] com=bankmain:items (12:12), obj=tinderbox (590), op=1` | this content (see its section) |
| [5695] | x1 | `[if_buttont] selectcom=inventory:items (149:0), selectedselectedobj=tinderbox (590), targetcom=inventory:items (149:0), targettargetobj=unlit_candle (36)` | this content (see its section) |
| [5755] | x3 | `[opnpc1_v2] npc=(id=skavidtalker2 (4377), )` | this content (see its section) |
| [5765] | x1 | `[opnpct] npc=(id=skavidtalker2 (4377), ), com=inventory:items (149:0), obj=skavidmap (2376)` | this content (see its section) |
| [5774] | x6 | `[oploc1_v3] id=cave3exit (2819)` | this content (see its section) |
| [5791] | x6 | `[oploc1_v3] id=skavid_cave2 (2806)` | this content (see its section) |
| [5797] | x8 | `[opobj3_v2] id=nightshade (2398)` | this content (see its section) |
| [5805] | x2 | `[opnpc1_v2] npc=(id=skavidtalker1 (4376), )` | this content (see its section) |
| [5809] | x2 | `[oploc1_v3] id=rocks1 (11390)` | generic content in the quest's area: see the verdict list in the doc |
| [5811] | x5 | `[oploc1_v3] id=cave2exit (2818)` | this content (see its section) |
| [5853] | x4 | `[opnpc1_v2] npc=(id=scared_skavid (4374), )` | this content (see its section) |
| [5964] | x1 | `[oploc1_v3] id=cave5exit (2821)` | this content (see its section) |
| [6017] | x3 | `[opnpc1_v2] npc=(id=skavidtalker4 (4379), )` | this content (see its section) |
| [6052] | x2 | `[oploc1_v3] id=cave1exit (2817)` | this content (see its section) |
| [6270] | x10 | `[opnpc1_v2] npc=(id=mad_skavid (4375), )` | this content (see its section) |
| [6354] | x1 | `[if_buttonx] com=inventory:items (149:0), obj=powering_crystal2 (2381), op=7` | this content (see its section) |
| [6367] | x2 | `[oploc1_v3] id=cave6exit (2822)` | this content (see its section) |
| [6422] | x2 | `[oploc1_v3] id=xbows_yanille_castlewall (17047)` | generic content in the quest's area: see the verdict list in the doc |
| [6504] | x1 | `[opnpc1_v2] npc=(id=qip_watchtower_wizard_02 (4398), )` | this content (see its section) |
| [6704] | x2 | `[if_buttonx] com=inventory:items (149:0), obj=nightshade (2398), op=7` | this content (see its section) |
| [6736] | x4 | `[opnpc2_v2] npc=(id=enclave_guard (4381), )` | this content (see its section) |
| [7395] | x4 | `[opnpct] npc=(id=enclave_guard (4381), ), com=inventory:items (149:0), obj=nightshade (2398)` | this content (see its section) |
| [7664] | x1 | `[oploct_v2] id=shamanentrance (2804), , com=inventory:items (149:0), obj=nightshade (2398)` | this content (see its section) |
| [7682] | x2 | `[if_buttonx] com=cws_warning_12:warn1 (573:17), op=1` | this content (see its section) |
| [7730] | x2 | `[if_buttonx] com=inventory:items (149:0), obj=4dose1antidragon (2452), op=2` | the recorder's own kit (food, potions, prayers, gear) |
| [7732] | x5 | `[if_buttonx] com=prayerbook:prayer13 (541:21), op=1` | the recorder's own kit (food, potions, prayers, gear) |
| [7749] | x1 | `[opnpc1_v2] npc=(id=qip_watchtower_ogre_shaman_02 (6209)), )` | this content (see its section) |
| [7760] | x3 | `[oploc1_v3] id=rock_of_dalgroth (2816)` | this content (see its section) |
| [7769] | x2 | `[oploc2_v3] id=rock_of_dalgroth (2816)` | this content (see its section) |
| [7790] | x3 | `[oploc1_v3] id=enclavecave (2813)` | this content (see its section) |
| [7862] | x1 | `[if_buttonx] com=inventory:items (149:0), obj=2doseprayerrestore (141), op=2` | the recorder's own kit (food, potions, prayers, gear) |
| [7972] | x2 | `[if_buttonx] com=bankside:items (15:3), obj=4doseprayerrestore (2434), op=2` | generic: bank / GE (the recorder's errands) |
| [7976] | x1 | `[if_buttonx] com=bankside:items (15:3), obj=1doseprayerrestore (143), op=2` | generic: bank / GE (the recorder's errands) |
| [7981] | x9 | `[if_buttonx] com=bankmain:items (12:12), obj=shark (385), op=1` | generic: bank / GE (the recorder's errands) |
| [7986] | x2 | `[if_buttonx] com=bankside:items (15:3), obj=4dose1antidragon (2452), op=2` | generic: bank / GE (the recorder's errands) |
| [8001] | x1 | `[resume_p_objdialog] id=guamvial (91)` | this content (see its section) |
| [8008] | x1 | `[if_buttonx] com=ge_offers:details_collect (465:24), obj=guamvial (91), op=1` | this content (see its section) |
| [8014] | x1 | `[resume_p_objdialog] id=jangerberries (247)` | this content (see its section) |
| [8020] | x1 | `[if_buttonx] com=ge_offers:index_4 (465:11), op=1` | generic: bank / GE (the recorder's errands) |
| [8025] | x1 | `[resume_p_objdialog] id=bat_bones (530)` | this content (see its section) |
| [8045] | x1 | `[resume_p_objdialog] id=vial_water (227)` | generic: bank / GE (the recorder's errands) |
| [8052] | x1 | `[if_buttonx] com=ge_offers:details_collect (465:24), obj=vial_water (227), op=1` | generic: bank / GE (the recorder's errands) |
| [8060] | x1 | `[if_buttonx] com=bankmain:quantityall (12:37), op=1` | generic: bank / GE (the recorder's errands) |
| [8061] | x1 | `[if_buttonx] com=bankside:items (15:3), obj=deathrune (560), op=2` | this content (see its section) |
| [8079] | x1 | `[if_buttonx] com=bankside:items (15:3), obj=cert_vial_water (228), op=5` | generic: bank / GE (the recorder's errands) |
| [8080] | x1 | `[if_buttonx] com=bankside:items (15:3), obj=cert_vial_water (228), op=6` | generic: bank / GE (the recorder's errands) |
| [8082] | x2 | `[if_buttonx] com=bankmain:items (12:12), obj=vial_water (227), op=1` | generic: bank / GE (the recorder's errands) |
| [8084] | x1 | `[if_buttonx] com=bankside:items (15:3), obj=cert_jangerberries (248), op=5` | this content (see its section) |
| [8087] | x1 | `[if_buttonx] com=bankside:items (15:3), obj=cert_bat_bones (531), op=6` | this content (see its section) |
| [8089] | x1 | `[if_buttonx] com=bankside:items (15:3), obj=cert_guamvial (92), op=5` | this content (see its section) |
| [8091] | x2 | `[if_buttonx] com=bankmain:search (12:42), op=1` | generic: bank / GE (the recorder's errands) |
| [8094] | x1 | `[if_buttonx] com=bankmain:items (12:12), obj=pestle_and_mortar (233), op=1` | this content (see its section) |
| [8103] | x1 | `[if_buttonx] com=bankmain:items (12:12), obj=jangerberries (247), op=1` | this content (see its section) |
| [8107] | x1 | `[if_buttonx] com=bankside:items (15:3), obj=shark (385), op=2` | generic: bank / GE (the recorder's errands) |
| [8108] | x1 | `[if_buttonx] com=bankside:items (15:3), obj=vial_water (227), op=2` | generic: bank / GE (the recorder's errands) |
| [8110] | x1 | `[if_buttonx] com=bankmain:quantity1 (12:29), op=1` | generic: bank / GE (the recorder's errands) |
| [8111] | x2 | `[if_buttonx] com=bankmain:items (12:12), obj=guamvial (91), op=1` | this content (see its section) |
| [8121] | x5 | `[if_buttonx] com=bankmain:items (12:12), obj=bat_bones (530), op=1` | this content (see its section) |
| [8129] | x2 | `[if_buttont] selectcom=inventory:items (149:0), selectedselectedobj=pestle_and_mortar (233), targetcom=inventory:items (149:0), targettargetobj=bat_bones (530)` | this content (see its section) |
| [8140] | x1 | `[if_buttont] selectcom=inventory:items (149:0), selectedselectedobj=guamvial (91), targetcom=inventory:items (149:0), targettargetobj=jangerberries (247)` | this content (see its section) |
| [8152] | x1 | `[if_buttont] selectcom=inventory:items (149:0), selectedselectedobj=jangerberries (247), targetcom=inventory:items (149:0), targettargetobj=guamjangervial (2390` | this content (see its section) |
| [8157] | x1 | `[if_buttont] selectcom=inventory:items (149:0), selectedselectedobj=ground_bat_bones (2391), targetcom=inventory:items (149:0), targettargetobj=guamvial (91)` | this content (see its section) |
| [8183] | x1 | `[if_buttont] selectcom=inventory:items (149:0), selectedselectedobj=ground_bat_bones (2391), targetcom=inventory:items (149:0), targettargetobj=vial_water (227)` | this content (see its section) |
| [8194] | x1 | `[if_buttonx] com=inventory:items (149:0), obj=guamjangervial (2390), op=10` | this content (see its section) |
| [8205] | x1 | `[if_buttont] selectcom=inventory:items (149:0), selectedselectedobj=ground_bat_bones (2391), targetcom=inventory:items (149:0), targettargetobj=guamjangervial (` | this content (see its section) |
| [8208] | x1 | `[if_buttonx] com=inventory:items (149:0), obj=ogre_potion (2394), op=10` | this content (see its section) |
| [8231] | x1 | `[if_buttonx] com=bankside:items (15:3), obj=ground_bat_bones (2391), op=2` | this content (see its section) |
| [8232] | x1 | `[if_buttonx] com=bankside:items (15:3), obj=jangerberries (247), op=2` | this content (see its section) |
| [8232] | x2 | `[if_buttonx] com=bankside:items (15:3), obj=vial_empty (229), op=2` | generic: bank / GE (the recorder's errands) |
| [8239] | x1 | `[if_buttonx] com=bankside:items (15:3), obj=bat_bones (530), op=2` | this content (see its section) |
| [8372] | x1 | `[if_buttonx] com=inventory:items (149:0), obj=magic_ogre_potion (2395), op=7` | this content (see its section) |
| [8549] | x2 | `[opnpc2_v2] npc=(id=qip_watchtower_ogre_shaman_01 (6208)), )` | this content (see its section) |
| [8568] | x1 | `[opnpct] npc=(id=qip_watchtower_ogre_shaman_01 (6208)), ), com=inventory:items (149:0), obj=magic_ogre_potion (2395)` | this content (see its section) |
| [8584] | x1 | `[opnpct] npc=(id=qip_watchtower_ogre_shaman_02 (6209)), ), com=inventory:items (149:0), obj=magic_ogre_potion (2395)` | this content (see its section) |
| [8615] | x1 | `[opnpct] npc=(id=qip_watchtower_ogre_shaman_03 (6210)), ), com=inventory:items (149:0), obj=magic_ogre_potion (2395)` | this content (see its section) |
| [8639] | x1 | `[opnpct] npc=(id=qip_watchtower_ogre_shaman_04 (6211)), ), com=inventory:items (149:0), obj=magic_ogre_potion (2395)` | this content (see its section) |
| [8655] | x1 | `[opnpct] npc=(id=qip_watchtower_ogre_shaman_05 (6212)), ), com=inventory:items (149:0), obj=magic_ogre_potion (2395)` | this content (see its section) |
| [8674] | x1 | `[opobj3_v2] id=shaman_robe (2397)` | this content (see its section) |
| [8682] | x6 | `[if_buttonx] com=inventory:items (149:0), obj=shaman_robe (2397), op=2` | this content (see its section) |
| [8697] | x1 | `[opnpct] npc=(id=qip_watchtower_ogre_shaman_06 (6213)), ), com=inventory:items (149:0), obj=magic_ogre_potion (2395)` | this content (see its section) |
| [8727] | x3 | `[if_buttonx] com=inventory:items (149:0), obj=powering_crystal3 (2382), op=7` | this content (see its section) |
| [8738] | x1 | `[opnpc2_v2] npc=(id=greater_demon (2025), )` | generic content in the quest's area: see the verdict list in the doc |
| [8843] | x2 | `[if_buttonx] com=orbs:prayerbutton (160:20), op=1` | engine/generic UI |
| [8955] | x1 | `[opnpct] npc=(id=watchtower_wizard (4397), ), com=inventory:items (149:0), obj=powering_crystal4 (2383)` | this content (see its section) |
| [8961] | x1 | `[opnpct] npc=(id=watchtower_wizard (4397), ), com=inventory:items (149:0), obj=powering_crystal2 (2381)` | this content (see its section) |
| [9006] | x1 | `[opnpc1_v2] npc=(id=macro_pillory_guard (380), )` | generic content in the quest's area: see the verdict list in the doc |
| [9018] | x2 | `[if_buttonx] com=pillory:button2 (27:8), op=1` | generic content in the quest's area: see the verdict list in the doc |
| [9030] | x1 | `[if_buttonx] com=pillory:button3 (27:9), op=1` | generic content in the quest's area: see the verdict list in the doc |
| [9043] | x1 | `[oploct_v2] id=qip_watchtower_pillar_nocrystal_multi_white (20037), , com=inventory:items (149:0), obj=powering_crystal1 (2380)` | this content (see its section) |
| [9051] | x1 | `[oploct_v2] id=qip_watchtower_pillar_nocrystal_multi_white (20037), , com=inventory:items (149:0), obj=powering_crystal4 (2383)` | this content (see its section) |
| [9055] | x1 | `[oploct_v2] id=qip_watchtower_pillar_nocrystal_multi_white (20037), , com=inventory:items (149:0), obj=powering_crystal3 (2382)` | this content (see its section) |
| [9060] | x1 | `[oploct_v2] id=qip_watchtower_pillar_nocrystal_multi_yellow (20029), , com=inventory:items (149:0), obj=powering_crystal3 (2382)` | this content (see its section) |
| [9063] | x1 | `[oploct_v2] id=qip_watchtower_pillar_nocrystal_multi_yellow (20029), , com=inventory:items (149:0), obj=powering_crystal1 (2380)` | this content (see its section) |
| [9071] | x2 | `[oploc1_v3] id=watchleverup (2794)` | this content (see its section) |
| [9089] | x1 | `[oploct_v2] id=qip_watchtower_pillar_nocrystal_multi_magenta (20033), , com=inventory:items (149:0), obj=powering_crystal2 (2381)` | this content (see its section) |
| [9097] | x1 | `[oploct_v2] id=qip_watchtower_pillar_nocrystal_multi_cyan (20025), , com=inventory:items (149:0), obj=powering_crystal3 (2382)` | this content (see its section) |
| [9144] | x1 | `[if_buttonx] com=questscroll:close_button (153:16), op=1` | engine/generic UI |
| [9146] | x1 | `[if_buttonx] com=quest_rating:5 (983:10), op=1` | engine/generic UI |
| [9148] | x1 | `[if_buttonx] com=inventory:items (149:0), obj=watchtowerspell (2396), op=2` | this content (see its section) |
| [9166] | x1 | `[if_buttonx] com=logout:logout (182:8), op=1` | engine/generic UI |

## Teleports

| cause | rows | recorded landing | ours |
|---|---|---|---|
| trellis | [642], [667], [1063], [1129], [1415] | 2548,3117,1 | 1_39_48_52_45 (same) |
| `watchladderup`/`down`, `qip_watchtower_ladder_top`, `towerladder` | 30 rows | ±1 plane on 2549,3112 / 2544..2545,3111..3112 | `~climb_ladder_to`, category climbs |
| skavid caves without the map | [1543], [1553], [1672], [2386], [2483] | 2553,3055; 2552,3034; 2523,3070; 2523,3070; 2563,3024 | the same five |
| skavid caves with the map | [5614..6267] | 2498,9451; 2518,9454; 2530,9467; 2504,9440; 2498,9418; 2522,9411 | fixed: three were off by a tile (cave2, cave3, cave5) |
| cave exits | [5619..6379] | 2553,3054; 2540,3054; 2523,3070; 2552,3034; 2563,3024; 2529,3012 | fixed: three were off by a tile (cave1exit, cave2exit, cave6exit) |
| the push | [1573..4559] | exactmove 7 tiles, then 2546,3065 / 2523,2998 | `~watchtower_push` |
| the gate guards / gates | [1736], [1740], [1778], [3213], [3691], [3733] | (pushes) | as above |
| Toban's cave / ladder | [2710], [2716], [2718], [2966], [3880], [3917] | 2576,3029 / 2500,2988 | same |
| bridge jumps, battlement, rope swing | [2213], [4868..5541] | exactmoves | `p_exactmove` with the recorded cycles |
| the enclave | [7685], [7688], [7716], [8533] | 2588,9410; the copy | `^watchtower_enclave_entry`, the scene's copy |
| `enclavecave` | [7791], [8832] | 2540,3054 | same |
| the lever | [9120] | 2928,4715,2 (the finished tower) | `movecoord(coord, 385, 0, 1601)` |
| amulet of glory, ring of dueling, tablets | [86], [212], [432], [2004], [2167], [3786], [3924], [5066], [5674], [7918], [8223] | | the recorder's travel; generic, not re-audited here |
| Varrock tunnel, Yanille hole | [2101..2103], [1258..], [4036..] | | generic agility shortcuts |
| Pillory | [9012], [9032] | | random event |

## Gate test

`test/quests/itwatchtower.lua` drives the whole quest on the recorded pages: run 5
281 rows PASS, 1 FAIL (the Camelot Teleport cast, below in Open). Changed with the content: the start's
four new pages and "Start the Watchtower quest?", the relic handed in by talking,
the gates opened by their guards, the battlement crossed for the player, the
recorded cave landings, a tinderbox in the kit (the recorder fetched one), the
`cws_warning_12` press, the enclave scene's box and camera (`t.cutscene.await`:
moveto h500, lookat h200).

## Open

- Gate run: `goUpTrellis.camelotTeleport.cast` fails in the shared teleport-spell
  code (runes and xp taken, no move); not this quest.
- The test eats earlier in the enclave since run 5 (runs 3 and 4 dipped to 12/60 HP
  waiting out the recorded ten-tick pours).
- The scroll/level-up order: our scroll mounts from a queue a tick after the
  completion, so a level-up queued before it wins (questscroll.rs2). Watchtower
  sends its xp after the scroll's booking to keep Jagex's visible order; the shared
  fix belongs in questscroll.rs2/levelup.rs2.
- The shamans' facing (7 per shaman recorded, ours 0), the shaman npc removed from
  view at L+5 of a pour ([8574]) before its varbit at L+10, `enabled_ops`/seq on
  shamans 5 and 6.
- The push is built from six recordings but the gate test takes no push.
- `watchtower_crystal_variable` (3126) flips with journal opens; not sent by us.
- `tol_homonculus_crying` (24 plays round the tower and Gu'Tanoth): ambience, not built.
- The dark cave's insects deal no damage in the 30 recorded ticks; what follows is unknown.
- `cws_warning_12`'s count cap (we stop at 6, Shantay's).
- The gate leaves' tiles are the earlier port's walk-through, not diffed against the
  recording's zone re-sends.
- Generic: `rocks3` unbound; Keef, the Zogre npcs and the Pillory guard's pages are
  their own content's.
- `tools/quest_gate/lint_quest.py` currently crashes on a duplicate varp name
  (`ge_newoffer_price`, varp 5753 and 9507) from other content; not run.
