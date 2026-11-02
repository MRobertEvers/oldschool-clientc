# Twilight's Promise — RSProx index 4444

Recorded 2026-10-30, rev 241.1, world 369, one session end to end (2,445 ticks), the
quest started at [259] and completed at [2430]. The session covers:

- a Varrock tablet, the bank, two trees, and Regulus's flight to Fortis [131..248];
- the twins outside the palace and the start [256..269];
- Metzli in the temple, the crypt stairs, the prince and Servius [334..557];
- the twins inside, the crest [614..660];
- the bazaar knights and the stolen amulet [712..815];
- the cothon knight's crates, the shipwright and the sailors [893..1051];
- the pub knights and Azali's fountain scene [1139..1262];
- the Colosseum and Mezan [1337..1667] (section "Mezan (Colosseum)", by the fight agent);
- the Kualti Headquarters' chests and the letter [1803..1894];
- Velam's judgement [1923..1976];
- Regulus, Renu and four quetzal flights [1999..2135];
- the Teomat: Servius, the neltas, Metzli, the battle and its aftermath [2154..2430].

The journal was opened 30 times, at 13 distinct states from stage 4 to 38.

Before this port the quest was a wiki port (Transcript:Twilight's_Promise oldid
15319500 and Quest Helper), with every cutscene "narrated as dialogue" by invented
`mes()` lines, no moods on any page, a one-line-per-stage journal, a random-tile
flight, bare `p_telejump` stairs, and no binding for any of the Varlamore townsfolk
the recorder talked to.

**Ours:** `quests/quest_twilightspromise/` (`twilightspromise.rs2`, `_knights.rs2`,
`_colosseum.rs2`, `_teomat.rs2`, `_battle.rs2`), `skill_hunter/scripts/quetzal_transport.rs2`,
`areas/area_varlamore/scripts/varlamore_people.rs2` (new), `skill_thieving/scripts/coin_pouch.rs2`
(new), `sailing/scripts/boat_selection.rs2`, the shared frames `general/scripts/fade_move.rs2`
and `general/scripts/cutscene_frame.rs2` (new), and the client fix in
`3rd/toridraw/toridraw_model_sprite.c`.

## The quetzal travel interface: a client bug (fixed)

The owner's report: the quetzal map looked wrong. Interface 874 `quetzal_menu` draws its
parchment (`scroll`, model 51193) and the map of Varlamore (`map`, model 51200) as
orthographic type-6 models on 32x32 components centred in `contents`, and every
destination icon and padlock as an orthographic model too (onload 5007, our
`torirs_db64_model_list_hook`, building them from db table 64). The server's packets
match the recording ([2038]: `toplevel_mainmodal_open`, `if_opensub mainmodal 874`, synth
8413, `if_setevents icons 0..13 OP1`).

On the soft3d lane (the lane every quest test runs) the parchment was gone and the map
was cut off in the top-left corner; GL3 drew it correctly. `ToriDraw_RenderModelExtentsAtWidget`
reused `widget_model_project_vertices`, which leaves each vertex relative to the centre of
the model's own extents sprite (`px + dx - sw/2`, right for the sprite baker), and then
added a widget origin that never put the `sw/2` back. Every orthographic widget model sat
half its extents up and left of where the deob draws it (`class144.method4835`: centre from
`class147.method5050` = widget x + w/2, plus x*scale/zoom; no extents term). The 450 px map
put its centre in the corner and the parchment left the screen. Fixed by adding
`(sw >> 1, sh >> 1)` back in the ortho branch; soft3d and GL3 shots now agree pixel for
pixel in layout (probe `test/quests/_quetzal_menu_probe.lua`, shots
`build/quest_gate/qz_probe_7a/shots/003-quetzal-menu.png` and `..._gl3/...`).

Also wrong on our side, all fixed:

| R | ours before | now |
|---|---|---|
| a flight: wings synth 8501 (loops 3, delay 55) and the fade on the click, minimap off C+1, teleport C+4, fade in + minimap on C+5, overlay closed C+7 ([2046..2053], [2076..2083], [2094..2101], [2129..2136]) | `~player_teleport_normal`: the spell-cast seq and spotanim | `~fade_move(dest, null, 2)` |
| the landing is the dbrow's coord exactly (Fortis 1697,3140; Sunset Coast 1548,2995; Hunter Guild 1585,3053; Teomat 1437,3171; 4/4) | `map_findsquare(.., 3)` | the coord |
| [2033]: `quetzal_base_appearance` 1 with the roosts | never set | `~quetzal_prepare` |
| `::complete quest_twilightspromise` | left Renu on her no-op rung (no Travel) | calls `~quetzal_prepare` |

## Action inventory (`rsprox_actions.py`), a verdict per row

| tick | action | verdict |
|---|---|---|
| 2 | welcome screen Play | engine |
| 107.. | quest list op2 (30), journal close (22) | the journal, below |
| 115 | questjournal_overview close | engine |
| 131 | Varrock teleport tablet Break | generic; tablets ported 2026-10-29 (`teleport_tablet.rs2`) |
| 187, 1307 | bank booth / table Bank, bank tab and withdraw clicks | generic bank |
| 211..214 | bankside herb deposits | generic bank |
| 233, 236 | tree / oak Chop down | generic woodcutting (`trees.loc`) |
| 241 | Regulus (Varrock) op3 Travel | **fixed**: the flight frame, 1700,3141 |
| 256, 289 | Furia / Ennius outside | this content (pages) |
| 279 | `varlamore_citizen_rich_f_2` Talk-to | **was unbound**; `varlamore_people.rs2` |
| 306, 608, 1073, 1675, 1899 | stamina potion Drink | generic potions |
| 334 | Metzli (Fortis) | this content |
| 409.. 553 | temple stairs top/bottom (9) | **fixed**: the fade frame and the recorded landings |
| 457 | temple gate Open | this content |
| 478 | `prisonbarsgate_locked` Open | this content |
| 507 | Prince Itzla (crypt) | this content |
| 614 | Ennius (palace) x6 | this content |
| 621 | `varlamore_squire_f_1` Talk-to | **was unbound**; `varlamore_people.rs2` |
| 712, 744 | bazaar knights | this content |
| 716 | `varlamore_guard_f_2` Attack | generic combat (no recorded fight follows) |
| 756 | `vmq2_citizen` Pickpocket x3 | **fixed**: below |
| 773 | `vmq2_amulet` Drop | generic drop; picking it again finds a new one ([789]) |
| 805 | coin pouch Open-all | **was unbound**; `coin_pouch.rs2` |
| 858, 866, 1280 | world map open/close | client |
| 893 | cothon knight | this content |
| 908, 914 | cothon crates / crate Search (19) | this content (pages matched) |
| 927 | `varlamore_sailor_f_1` Talk-to | **was unbound**; `varlamore_people.rs2` |
| 965 | `shipwright_workbench` Craft | **was unbound**; the missing-tools mesbox, see Open |
| 969 | Shipwright Sennia Talk-to | **fixed** greeting "Nilsal! What can I do for you?" |
| 994 | dock ledger table | generic sailing |
| 1139, 1199 | pub knights, Azali following | this content |
| 1160 | run toggle | client |
| 1161 | beer glass take | generic ground item |
| 1337, 1661 | Colosseum entrance / exit | Colosseum minigame (fight agent's section) |
| 1351, 1408 | Mezan talk / Attack | fight agent's section |
| 1358, 1363 | gladiators Talk-to | **was unbound**; `varlamore_people.rs2` |
| 1410 | prayer 15 (Protect from Melee) x48 | the recorder's kit |
| 1427, 1472 | staff of earth / rune scimitar Wield | the recorder's kit |
| 1621, 2367 | lobster Eat | generic food |
| 1803.. 1888 | Kualti HQ spiral stairs (7) | generic `ladders.loc` |
| 1828..1862 | the knights' chests | this content |
| 1868 | letter Read | this content |
| 1999 | Regulus (Fortis) | **fixed**: three-option menu, the mesbox |
| 2019 | Renu Feed | **fixed**: below |
| 2046.. 2129 | quetzal menu icons x4 | **fixed**: the flight |
| 2053, 2085 | Sunset Coast / Hunter Guild quetzals Travel | generic quetzal network |
| 2154 | Servius (Teomat) | this content |
| 2187, 2222 | Nelta of Ralos / of Ranul Talk-to | **were unbound**; `varlamore_people.rs2` |
| 2229 | Metzli (Teomat) | this content |
| 2263..2387 | cultists Attack | the battle, below |
| 2436 | quest scroll close | engine |
| 2445 | logout | engine |

## Teleports, a verdict per row

| tick | from -> to | cause | verdict |
|---|---|---|---|
| 135 | 2808,3086 -> 3213,3423 | Varrock tablet | generic |
| 245 | Varrock -> 1700,3141 | Regulus Travel | fixed (flight frame) |
| 413, 447 | temple top -> 1694,9492 / 1694,9481 | stairs down | fixed: landings and faces (1699,z) |
| 440, 557 | crypt -> 1692,3086 / 1692,3087 | stairs up | fixed: landings (were 1694,3084/3088) and faces |
| 1236, 1253 | the fountain copy in/out | the fountain scene | built |
| 1342, 1403, 1637, 1667 | Colosseum | fight agent |
| 1806..1894 | Kualti HQ floors | spiral stairs | generic |
| 1930, 1962 | the palace copy in/out | Velam's judgement | built |
| 2050..2133 | four flights | quetzal menu | fixed (exact landings) |
| 2256 | -> the battle copy 33,43 | Itzla's shout | built |
| 2405, 2420 | the aftermath copy in/out -> 1453,3173 | the eighth death | built |

## Stages

Every `vmq2` (9649) write, 0..50 in steps of 2, keyed as our constants already were.
Moved to their recorded pages: 30 before Ennius's "This should be fun." ([1927]); 34 before
his "You should not waste time." ([1975]); 20 before Azali's last page ([1262]); 44 with the
player's "I guess I'd better..." ([2252]); 46 three ticks after the eighth cultist ([2403]);
48 on the guard's last aftermath page ([2418]).

## The journal

Rebuilt in the current format from the 13 recorded states (stages 4, 8, 12, 14 twice,
16, 18, 20 twice, 22, 24, 28, 34, 38), the knights' groups keyed on their four varbits.
`test/quests/_journals.lua` checks all 14 openings row for row: **14/14 match** (95/95 rows
of that test pass). UNVERIFIED: stages 2, 6, 10, 26, 30, 32, 36, 40..48, the cothon's "To do
so" line, the last three struck paragraphs and the completed journal (no recording opens
them; the wiki has no journal transcript).

## Pages

`rsprox_pages.py` + a verbatim check against our scripts: 298 of 356 distinct texts matched
before; 24 were punctuation drifts from the wiki ("Well this" for "Well, this") and are
fixed; the rest were missing content (the townsfolk, Renu's "Hello, Renu. I have something for
you here.", the ending's three pages, "any Old Ones around") and are written. Moods and heads:
no page had a mood; 295 of the 324 recorded lines now go out with Jagex's mood and
chathead npc (Jagex uses a head per place: `vmq2_ennius_inner_palace`, `vmq2_itzla_teomat`,
not the `_vis` multinpc). Invented `mes()` lines the recording never sends, deleted: "Renu perks
up..." (a mesbox in R), "The summoned knights are lined up...", "Ennius kills Velam...",
"Azali dunks her head into the fountain.", "Cultists storm the Teomat!", "You leave the
battle.", "You and Prince Itzla return...", "The guard leaves with Servius' body.".

## The bazaar: the amulet and the pouch

| R | ours before | now |
|---|---|---|
| an amulet pick ([759], [789]): the spam line, the same words in an objbox, the amulet only: no coins, no xp | 3 coins + 8 xp + the amulet, objbox only | as recorded |
| a plain pick ([801]): 8 xp, `pickpocket_coin_pouch_citizen`, clientscript 7192 (loot tracker, kind 350) | 3 coins | as recorded |
| Open-all ([805]): synth `coins`, `tracking_coins_gained`, "You open all of the pouches and receive a total of 3 coins." | no binding for any pouch | `coin_pouch.rs2`, all 21 pouches (category 1249, values from the wiki's table) |

## Renu

[2019..2038] from the op: the player's line, on its click `human_quetzal_tame` +
`npc_quetzal_tame_01` under an objbox with no continue; nine ticks on, the stage, every roost,
the feed gone and the same box with its continue; Regulus; the map the tick after his click
with the chat closed. Ours had no player line, no anims, the stage before the first box.

## The fountain ([1155..1262])

Azali follows at a walk; "Hey! You're going too fast!" and she stops when the player is ten
ahead ([1186]; ours: twelve); a break line now and then ([1217]); checked every tick (ours
every 8). The scene starts the tick after her step to within 2 of the fountain ([1233] ->
[1234]). The scene is copied packet for packet: the black and an empty messagebox; T+2 the
lock (fov_clamp 3, rooftops 2), the copy of m27_47, her stand-in and the fountain's, the
fountain's blocker and sound loc, the hud; T+3 the camera; T+5 her walk and the black lifting;
T+7 her glide; T+9 the dunk (`civitas_fountain01_knight01_dunk_walk_01`); T+17 the varbits and
the black; T+19 out; T+21 the black lifts; T+23 her first page.

## Velam's judgement ([1928..1966])

A copy of the palace square: the twins' and six knights' stand-ins in their recorded tiles, the
camera, Ennius's walk and turn, the trial pages (moods as recorded), the slash (`human_sword_slash`
delay 5, `hacksword_slash` delay 5) on the last page's click, Velam's `human_death` (delay 20),
`blade4`, `human_death` (delay 20) the tick after, Velam gone three ticks later, the dismissal and
the five knights gliding out a tile a tick, the stage and the black on the fourth tick, out beside
the twins.

## The Teomat battle ([2252..2400]) and the aftermath ([2403..2430])

The battle is a copy of m22_49 with a fade (not a cutscene lock): eight cultists, four guards and
the prince in their recorded tiles and pairs; cultist_m_1 goes for the player; the noop stairs,
ladders and bowl blocking the way out. Recorded per pair: a 4-tick loop (the cultist's
`human_sword_stab` + the guard's `human_blunt_block`, a 0 or a hit on the guard the tick after;
two ticks on the guard's `human_blunt_pound` + the cultist's `human_sword_def`); the prince's
`human_sword_slash` / `human_sword_def`. Sounds at the actor's own tile: cultists
`stabsword_stab` / `leather_hit_3`, guards `warhammer_crush` / `blade4`, the prince
`hacksword_slash` / `blade4`. Guards can die (guard_1 [2309]); an npc whose opponent falls takes
another ([2336], [2351], [2386]); a cultist the player hits turns on the player ([2318]). Every
cultist death is "Enemies Defeated: N/8" ([2302] was the prince's kill). The rates are fitted to
this one recording (cultist on guard 24/95 land, guard on cultist 22/67, prince 7/11, 1..4 each):
UNVERIFIED as rules. Damage on the player: melee, every 4 ticks, max 4, 53/53 zero under Protect
from Melee (`rsprox_player_hits.py`); the cultists' stat block matches the cache.

The aftermath: a fresh copy, the wounded Servius, a guard and the prince's stand-ins, the
shrine's stand-in and four blockers, the camera, the prince's walk and the player walked three
tiles, six pages, the stage, out to 1453,3173 beside the Teomat's prince, whose talk starts two
ticks after the black lifts with no click; the quest completes on its last page ([2430]).

## Townsfolk (`varlamore_people.rs2`)

Citizens (the wiki's ten random answers, [287]), squires (five, [626]), sailors (the two
recorded, [931], [1000]), gladiators (the recorded exchanges, [1358..1366]), the neltas of Ralos
(the menu without its last option, "What is this place?" only at the Teomat; the heal [2193]:
`human_castentangle`, `heal_casting` slot 2 height 120, `heal` area sound, hitpoints full) and of
Ranul. Ethically Acquired Antiquities' two citizens fall through to the same talk outside its
stage (was "Busy day.", unrecorded).

## Wire audit (ours: `build/quest_gate/tp_7a/ticklog.tsv`, the full quest test, 152/152)

Aligned on each scene's cast spawn (T+2):

| scene | R | ours | verdict |
|---|---|---|---|
| fountain | cast T+2, her steps T+5 T+6, glide T+7, dunk T+9, out + sober Azali at 1756,3069 T+19 | the same ticks | matched (after the fix below) |
| execution | cast T+2, Ennius's steps T+5..T+7; on the "step forward" click Ennius and Velam step the same tick; slash on the last click, death + both sounds the tick after, Velam gone 3 later; knights glide from the dismissal click | the same ticks | matched |
| battle | 13 npcs in the recorded tiles, 9 deaths, 142 ticks | 13 in the same tiles, 8 deaths (no guard fell), 128 ticks | see below |

Fixed by this audit: a walk set from a script resumed by `p_delay` stepped a tick late (the
resume runs after the npc phase), so Azali's stand-in glided three tiles instead of two and
Ennius trailed by a tick; those walks are now set a tick earlier. A walk set on a dialogue click
was already on time.

The battle, per kind over the whole fight: R `human_sword_stab` 137, `human_sword_def` 145,
`human_blunt_pound` 99, `human_blunt_block` 95, `human_sword_slash` 25, `stabsword_stab` 106,
`leather_hit_3` 122, `warhammer_crush` 99, `blade4` 106; ours 69, 82, 62, 42, 14, 67, 123, 61,
54. Every kind is there and at the right tiles, but the sparring runs at about half Jagex's
rate: OPEN (each pair should trade on every 4-tick cycle; ours misses some).

## Second pass: the open items

**The battle's pace (fixed, engine and content).** Three causes, each found on the tick log:
- An npc's swing at another npc (`[ai_opnpc2]` from `npc_vs_npc_tick`) ran under whatever
  player the server had served last; the `npc_queue` it armed recorded that player as the hit's
  hero, and the hit then took the retaliation latch onto the player. Fixed in
  `torirs_server_combat.c`: the swing runs bound to nobody, as the npc queue already runs an
  entry an npc armed.
- The cast was owned by the player (`npc_setowner`, for the death line), and an owned npc's AI
  triggers run as its owner, so the same latch fired. Dropped; the death line finds the player
  as the copy's owner (`map_instance_owner`).
- Both sides of each pair were told to fight at spawn and walked into each other; in the
  recording only the cultists step in ([2258]). Now the cultists engage at spawn and the guards
  and the prince answer their own two ticks later ([2260]).

Per kind over the fight, R / ours (probe `test/quests/_tp_teomat_probe.lua`): `human_sword_stab`
137 / 142, `human_sword_def` 145 / 157, `human_blunt_pound` 99 / 105, `human_blunt_block` 95 /
84, deaths 9 / 9, `human_sword_slash` 25 / 16. The defend sounds run higher than recorded
(`leather_hit_3` 122 / 184): area sounds reach the recorder only within 10 tiles of a pair.

**The follower varps (fixed, engine and content).** `npc_setfollower` now writes
`follower_npc` (447) = (type << 16) | the npc's index in that player's view, taking the
per-player name the npc info then reuses; Azali's script sets `follower_obj` (448) =
`vmq2_knight_5_follower_obj` and clears both at the scene's lock; both varps now `transmit=yes`.
Ours: 877723697 (13393 << 16 | 49) and 28977 on the follow's tick, both -1 at the lock, as
[1155] / [1236]. Other followers (pets) get 447 from the engine; their 448 is theirs to set.

**The workbench (built, `sailing/scripts/shipwright_workbench.rs2`).** Index 3443 (rev 235,
Catherby) crafts at it 44 times: Craft opens "What would you like to craft?" "Hull parts."/
"Repair kits." at L+2, the make-X menu the tick after; each craft is a 4-tick cycle
(`human_poh_build` + `poh_build_wood`, the product and the line on the 4th tick): "You craft some
teak hull parts." (12) and "You craft a set of two camphor repair kits." (32: adamant nails -10,
swamp paste -5). Levels, materials and xp are the wiki's per product; the other products' lines
are UNVERIFIED. Craft-hull-parts and Craft-repair-kits go straight to their menu (unrecorded).
Probe: one teak hull part from 5 planks, two camphor kits, each with the recorded line.

**The ending has two branches (found in index 1679, rev 226, a second full playthrough).** A
player who can start The Heart of Darkness (Mining 55, Thieving 48, Slayer 48, Agility 46 --
4444's recorder has Mining 20) hears "Word has been sent to mother..." and "However, let's not
worry about that just now..." (the completion on that page, [2411]); when the scroll is shut
([2416]) the talk carries on: "So what's this plan you mentioned?", "You want to hear it
already?...", "Start The Heart of Darkness quest?" Yes./No. Both branches are built and probed
(`_tp_teomat_probe.lua`, with and without the levels; the scroll shut by its button and by
Escape). 1679 also gives the scroll's three rows and model (the crest), now copied, and confirms
the knights' and the cutscenes' pages; its punctuation is the wiki's (Jagex since added the
commas 4444 has).

**The start (fixed).** "Yes."/"No." (ours said "No..."); stage 2 and "You've started a new
quest" on "Yes." ([261], ours sent neither); stage 4 before "Okay. Guess I should get going
then." ([268]).

**The journal past stage 38 stays UNVERIFIED.** Neither 4444 nor 1679 opens it there, no other
indexed session has the quest, and the wiki has no journal transcript.

## Open

- The journal's UNVERIFIED stages (above): no recording or transcript exists.
- The Heart of Darkness past its start is that quest's (a stub port in this tree); the start sets
  its own stage ladder's ^hod_pub, where Jagex writes 2.
- The prince's slashes run below the recording (16 / 25) and the battle's hit rates are fits.
- Music inside the copies is left to the engine (the recording re-sends 783 at each lock).
- From the Mezan section: his first swing a tick late, the boss bar's timing, the multiway icon,
  the shield's block sound and the varn allocator's slots 62/63 (engine/shared).

## Mezan (Colosseum)

Two sessions hold the fight (`rsprox_index.py search npc 12916`: no others): index 4444 above
([1337..1670]; the recorder at combat 46, Defence 31, amulet of glory, rune scimitar / staff of
earth, elemental shield, Protect from Melee up for 41 of his 45 swings), and index 1679
(rsprox.net 1695, rev 226, a normal world, [1630..1705]; a maxed recorder with Osmumten's fang
and a Bow of faerdhinen). Both agree on every frame below. Ticks are 4444's unless they name
1679; C is the "Kuani!" page's click, H the tick of the splat that empties him.

**Ours:** `twilightspromise_colosseum.rs2` (rewritten), the knight's block in
`twilightspromise.npc`, seven lines out of his generated block, the driver
`test/quests/_tp_mezan.lua` (the lobby door, the first talk with the crest, the bout, the way
out, the street door) and the quest test's colosseum leg. Tick logs: scriptrun
`build/quest_gate/tp_mezan_s4/` (17/17, with `wirelog.tsv`) and the live lane
`build/quest_gate/tp_mezan_a2/` (the bout long, 119 of his swings) and `tp_mezan_a3/` (the final pack, 17/17); the whole
quest test on scriptrun `build/quest_gate/tp_quest_q1/` (every colosseum row passes; its four
FAILs are the Teomat cultists, `quest.journal` and the reward teleport, none in this leg).

### Action and teleport rows (the inventory's "fight agent" rows)

| tick | action / move | verdict |
|---|---|---|
| 1337 | `colosseum_entrance_outside` Enter | **fixed**: the lobby door (below); was a telejump into the training room |
| 1342 | -> 1799,9506 | the lobby door's landing (fixed) |
| 1351, 1369, 1396 | Mezan Talk-to (1354's talk cut off by a gladiator click) | this content: the talk |
| 1403 | -> the copy of m28_48, local (32,30) | **fixed**: the way in (below) |
| 1408..1617 | Mezan Attack (24) | the bout |
| 1410..1634 | Protect from Melee (48) | the recorder's kit (flicking) |
| 1427..1616 | staff of earth / rune scimitar Wield (8) | the recorder's kit (two styles) |
| 1562, 1594, 1597 | side stones | client |
| 1570, 1604 | Magic / Strength level up | engine |
| 1621 | lobster Eat | generic food |
| 1637 | -> 1803,9521 | **fixed**: the way out (below) |
| 1648..1655 | quest list, journal | the journal section |
| 1661 | `colosseum_exit_lobby` Exit | **fixed**: the street door (below); was a bare telejump |
| 1667 | -> 1795,3106 | the street door's landing (matched) |

### His per-life record (`rsprox_npcinfo.py`, `npc_block_inventory.py`)

| block | R (4444 / 1679) | ours (scriptrun s4 / live a2) | verdict |
|---|---|---|---|
| add | C+2 ([1403] / [1651]): template 1824,3108 = copy (32,36), `spawnangle=south`, `[facing] coord=` the player's tile (2/2) | C+2, (32,36), south, a coord face on the player's tile | **fixed** (was (32,33), no face) |
| set on the player | C+3: `[facing] player=` and his first step (2/2) | C+3 (`npc_setmode(opplayer2)` latches a tick late) | matched |
| approach | south a tile a tick, 5 steps / 4 (1679's player stepped in at [1655]); then idle 104 / 40 ticks, no wander | 5 steps C+3..C+7, then still | matched (`defaultmode=none`, `wanderrange=0`) |
| first swing | add+5 (2/2): 4444 steps in and swings on [1408]; 1679 beside the player after their step [1655], swing [1656] | add+6 (s4 [75], a2 [41]): beside at add+5, swing a tick later | OPEN, engine-generic: our npc checks reach before its step, never swings on a step tick (Fight Arena's "step and swing", 8 of 9 there) |
| swing | `human_sword_slash` (390) delay 0, 5 ticks apart: 53/53 (gaps 44 + 7, all 5) | 390 every 5: 48/48, 119/119 | **fixed** (was `human_unarmedpunch`) |
| parry | `human_sword_def` (388) on the player's attack tick when he is not swinging: delay 0 for melee (17 / 5), the projectile's `endtime` for a spell or an arrow (56 x2 / 51 x4); never on his own swing tick | 388 on 40/40 of the player's attacks off his swing (0 x19, 56 x21), none on his swing ticks | **fixed** (was `human_unarmedblock`) |
| headicon | `[headicon_customisation]` slot 0, group 440, frame 0 melee / 2 magic (1 missiles never shown: 1679's ranged switch came as he yielded); on every swing (53/53, empty while he prays nothing), on each switch (9), cleared at the yield (2/2) | every swing with the prayer then current (48/48, 119/119), each switch, cleared at H+1 | **fixed** (we sent no overhead at all) |
| headbar | id 2 `healthbar_2`, fills of 40 (38/38, 15/15) | `healthbar_2` (41/41) | **fixed** (was `healthbar_standard_30`) |
| hits on him | the dealer's 16 / 12 / 43; `blade5` (1972) to the player on 53/53 landings | blade5 on every landing | **fixed** (`defend_sound` was null) |
| say | 9 lines, 3 of each, each under either style; echoed as `message_game type=npc_say` 9/9 | one of three at random, the echo (116) 7/7 | **fixed** (no echo before; see the rule) |
| the yield | H+1 (2/2): `level_change` 81, `[facing] type=reset`, empty headicon, `enabled_ops 0b00000`, `transformation` vmq2_knight_6_combat_done (12917); no death seq, no splat, no swing after; gone with the copy at H+4 | H+1: retype 12917 (level 81, ops off), face reset, headicon cleared; no swing (the done form's `[ai_queue1]` / `[ai_opplayer2]` do nothing) | **fixed** (was `~npc_default_death`: `human_death` and a despawn) |
| heals | none: 100 damage takes him 100 -> 0 in both (225 ticks in 4444) | none | **fixed**: `param=regenrate,10000` (run a1 regenerated 1 at [136]) |
| the training-room Mezan | a multinpc (13396 -> 12914 at stages 14..22); `multinpc_stance_audit.py`: shell and rung stances the same | the static spawn, wandering in the room | matched |

`npc_block_inventory.py` flags `enabled_ops` and `level_change` as MISSING: ours sends both at H+1
but logs them under 12917, the type after the transformation; `hit` is ours' `hit_npc` rows. The
facing re-send when the player moves round him (3/3 in 4444: [1494], [1538], [1558]) is not
exercised by our driver (it stands still); engine-generic.

### His prayer

| # | R | the run that made it (landing ticks: value) | line, frame, synth |
|---|---|---|---|
| 1 | [1422] | melee [1409]:4 [1413]:8 [1417]:0 [1421]:5 | "Shield me, Ralos!", 0, `protect_from_melee` |
| 2 | [1451] | magic [1430]:2 [1435]:2 (splash [1438]) [1445]:4 [1450]:4 | "Ralos' light protects me!", 2, `protect_from_magic` |
| 3 | [1487] | melee [1474]:7 [1478]:5 [1482]:0 [1486]:5 | "Ralos will defend me!", 0, melee |
| 4 | [1526] | magic (splash [1498]) [1505]:1 (splash [1508]) [1515]:1 [1520]:1 [1525]:1 | "Ralos will defend me!", 2, magic |
| 5 | [1552] | melee [1539]:6 [1543]:0 [1547]:5 [1551]:6 | "Ralos' light protects me!", 0, melee |
| 6 | [1591] | magic (splashes [1558] [1563] [1568]) [1575]:1 [1580]:2 [1585]:1 [1590]:6 | "Ralos' light protects me!", 2, magic |
| 7 | [1612] | melee [1599]:4 [1603]:5 [1607]:1 [1611]:4 | "Ralos will defend me!", 0, melee |
| 8 | 1679 [1672] | melee [1656]:17 [1661]:5 [1666]:29 (kit 1+1 [1667]) [1671]:21 | "Shield me, Ralos!", 0, melee |
| 9 | 1679 [1695] | ranged [1682]:4 [1686]:1 (kit 1+1 [1687]) [1690]:13 (kit [1692]) [1694]:4, nothing left | "Shield me, Ralos!", no overhead (the yield clears it), `protect_from_missiles` |

The rule: four of the player's hits LANDING running in one style, a miss's 0 included, and he
prays against it a tick after the fourth splat (9/9). A spell that splashes lands nothing: it
neither counts nor breaks the run (6 splashes inside runs 2, 4 and 6). Damage no swing of the
player's dealt counts for nothing (1679's 1+1 kit splats). A hit in the style he prays against
lands a 0 (4444: melee 5/5; a spell still rolls, 3 successes land 0 and 4 splash; 1679: the fang
1/1). Under his melee prayer the player's spells succeeded 14 of 21, under his magic prayer 3 of
7: no evidence that his prayer moves accuracy.

| | R | ours before | ours now (s4 / a2) | verdict |
|---|---|---|---|---|
| when | the fourth landing + 1 (9/9) | on the fourth swing (melee 2 ticks early, a spell 3) | the fourth landing + 1 (7/7, 8/8) | **fixed**: counted in `[ai_queue2]` from a FIFO of in-flight styles the funnel files, the switch on `[ai_queue4]` a tick later |
| splashes | skipped | skipped | skipped (no landing) | matched |
| the prayed style | lands 0 | 0 | melee 8/8, 28/28; spell successes 2/2, 22/22 land 0, splashes still roll | matched |
| the state | - | npc var slot 62 = `^npc_poison_var_slot` (varn.alloc gave it to `tp_mezan_prayer`): every switch "poisoned" him, each of his swings' `~npc_poison_tick` hit him for 1 and took the prayer back to 0; run a1: 17 poison splats, 14 lines for 9 runs | slot 62 cleared and unused; the prayer rides in `%tp_mezan_streak` | **fixed** |
| the line, echo, overhead, sound | all four (9/9) | the line only | all four | **fixed** |

### Damage on the player (`rsprox_player_hits.py`, §13; `rsprox_hit_causes.py`, §13b)

| | R | ours | verdict |
|---|---|---|---|
| unprayed, 4444's kit | 4: 4, 6, 8, 6 (no zero; predicted 0.34 zeros a swing from his (70+9) x 64 = 5056 against (31+8) x (4+64) = 2652: p = 0.19 for none in 4) | live 71: 20 zeros (28%), max 8, mean of the hits 4.69 (uniform 1..8: 4.5); scriptrun 15: 2 zeros, max 8 | matched |
| unprayed, 1679 (Defence 99) | 8: 0, 0, 7, 0, 0, 0, 8, 1 | not run | consistent |
| max | 8 in both = floor(0.5 + (70+8) x 64 / 640) | 8 | matched |
| Protect from Melee | 41/41 zero | 48/48, 33/33 | matched |
| his levels | cache stat1..6 70/40/70/100/-/20 | the block's; `check_npc_cache_levels.py` 0 | matched |
| every splat | 45/45 on the 4444 recorder are his swing (44 `adj`, 1 `near`: the step-in swing at [1408]); no `none`, no heals | 167 `adj` (s4 + a2) | the `near` row is the step-and-swing (OPEN above) |

### Sounds

| | R | ours | verdict |
|---|---|---|---|
| his swing | `hacksword_slash` (2500), 53/53 | the same | **fixed** (null) |
| his parry | `blade5` (1972) on each landing, 53/53 | the same | **fixed** (null) |
| his prayer | `protect_from_melee` / `_magic` / `_missiles` on the switch tick, 9/9 | the same | **fixed** (none) |
| the player hit | `human_hit_2`/`_3` delay 20, 4/4 | `human_hit_1..4` delay 20 | matched (engine-generic) |
| the player blocks | `blade4_1..5` delay 20 on 41/41 (the elemental shield) | `human_block_1` | OPEN, shared: `~combat_player_block_sound` (skill_combat/combat.rs2) has no blade4 family for the elemental shield; the recorder's kit |

### The way in (C = the "Kuani!" click)

| tick | R (2/2: [1401..1407], 1679 [1649..1655]) | ours (scriptrun wire) | verdict |
|---|---|---|---|
| C | the fade out (`fade_overlay` 948 0,255,0,0,50, the hp bar hidden + `cc_deleteall`), the page closed | the same | **fixed** (no fade) |
| C+1 | `minimap_state` 2, `minimap_toggle` 2 | the same | **fixed** |
| C+2 | the copy (--raw 10944,15678 = template 1824,3102: translated, unturned), the player on (32,30) facing (32,40), Mezan added, the gate (31,19) rotation 1 shape 10, `midi_song` 775 0,60,60,0 + `music_current_track` 3449 + 3932 | the same | **fixed** (player (20,34), knight (32,33), gate (16,34) rotation 2, no song) |
| C+3 | his face and first step | the same | matched |
| C+4 | `multiway_indicator` 1, `minimap_state` 0, busy 0, the fade in, `minimap_toggle` 0 | the same, but the multiway icon at C+2 (the engine's zone latch: a copy is multi-way) | **fixed**; the icon's tick OPEN (engine-generic) |
| C+6 | `hpbar_hud_npc` 12916, 100/100, the boss bar 1, the fade overlay closed | the same (`hpbar_hud_npc(npc_uid)`; his cache param 509 is 2) | **fixed** |

Ours pins the overlay, so the engine sends the boss panel's colours + 2376 once, not on every
tick as Jagex (and our unpinned bosses) do: OPEN, engine-generic (`torirs_server_hpbar.c`).

### The end (H = the splat that empties him)

| tick | R (2/2: [1633..1641], 1679 [1694..1702]) | ours (scriptrun) | verdict |
|---|---|---|---|
| H+1 | the yield (above); the player's face reset | the same, `p_stopaction` | **fixed** |
| H+2 | his var 1 -> 2, `hpbar_hud_npc` -1 + base 0 + the boss bar 0, busy 1, the fade out | his var, the boss bar 0, busy 1, the fade | **fixed**; `hpbar_hud_npc` -1 goes at H+8 (the engine lingers six ticks after `hpbar_hud_npc(-1)`) and our overlay wrote 12917 at H+1 (it follows the type): OPEN, engine-generic |
| H+3 | `minimap_state` 2, `minimap_toggle` 2 | the same | **fixed** |
| H+4 | 1803,9521 (2/2), facing the training-room Mezan's tile; the copy gone | the same | **fixed** (was 1806,9522 four ticks on, no frame) |
| H+6 | `multiway_indicator` 0, `music_current_track` 3743, `midi_song` 811 0,60,60,0 + 3932, `minimap_state` 0, the fade in | the same, the icon at H+4 (engine) | **fixed** |
| H+8 | the overlay closed, 811 three times more, busy 0, and his page by itself: "You fought well in there..." (4444: he stepped beside the player; 1679: the player stepped beside him) | the same (`p_opnpc(1)` on him) | **fixed** (was "You leave the battle.", invented, and a talk the player had to start) |

### The talk

Every page matches both sessions verbatim with its mood (4444 [1369..1400], [1641..1645]; 1679
[1630..1648], [1702..]; rev 226 has "the colosseum" lower-case). Fixed: his var 0 -> 1 goes out
with the "Oh, and bring two attack styles" page ([1380], 1679 [1644]; was a page later); "So
you'll return to the palace now?" is `chathap1` (was neutral); his var 2 -> 3 and the stage 20 ->
22 go out with "Perfect! Thanks." ([1645]; were after it). Unrecorded, the wiki's: "I need to go
and prepare." and its answer, the crestless answer, the talk once he is done.

### The Colosseum's doors

| | R | ours | verdict |
|---|---|---|---|
| entrance (knights' stage) | [1337..1345]: L+1 the op's face, L+2 busy and a face through the doorway, L+3 the fade, L+4 the minimap off, L+5 the lobby at 1799,9506, L+6 the fade in and the lobby's track four times (811), L+8 the overlay closed. 1679 [1601..1622] and the minigame's index 1211 have the fade in at landing + 2, not + 1 | `p_arrivedelay` + `~colo_enter_lobby` (the minigame's index-1211 frame): landing + 2 | **fixed** (was a telejump to the training room and "You enter the Colosseum."); 4444's landing + 1 is 1 of 3 |
| exit | [1661..1670]: L+1 the op's face, L+2 busy, a face 10 west and the fade, L+3 the minimap off, L+4 1795,3106, L+5 the fade in, 811 four times, the menu's second op cleared, L+7 the overlay closed | the same, from `p_arrivedelay` (from a standing start the frame starts on the op) | **fixed** (was a bare telejump) |
| the gate (`vmq2_knight_6_combat_leave`, Exit / Quick-exit) | never used | the bout's way out without the talk | UNVERIFIED |

### Open

- Engine-generic: the npc's step-and-swing order (his first swing add+6, recorded add+5).
- Engine-generic: the pinned boss overlay (no per-tick colour re-sends; -1 six ticks late; the
  type re-written at the transformation); the multiway icon two ticks early on both moves.
- Shared: the elemental shield's block sound (`blade4`, 41/41) in the player block table.
- Tooling: `pack/varn.alloc` hands out npc var slots 62 and 63, which `^npc_poison_var_slot`
  (62) and the sailing cannon effects (`npc_var_get(62)`, `(63)`) use raw; any npc var given 62
  is ticked down by every swing. `tfa_dn_jelly_target` holds 63. The allocator should keep both.
- Stale, not mine to edit: `^tp_arena_land_x/z`, `^tp_arena_knight_x/z` and
  `^tp_lobby_after_mezan` (twilightspromise.constant) name the old wrong tiles and are now unused;
  `::tp_mezan_hp` (twilightspromise_debug.rs2) prints `%tp_mezan_prayer`, now always 0.
- UNVERIFIED: the ranged prayer's overhead (frame 1 never drawn), a fifth hit in a style before
  his switch tick, a run broken by a hit in the prayed style, the gate.
