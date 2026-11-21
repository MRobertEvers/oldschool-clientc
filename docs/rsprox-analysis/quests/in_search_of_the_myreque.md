# In Search of the Myreque -- from RSProx index 4632 + 4633

Recordings (rev 242.1, the owner's, 2026-11-21): **4632** "In Search of the Myreque - Start"
(Vanstrom to the ambush and the first escape from the hound) and **4633** "... - Finish"
(logs in at stage 80: the hound, Veliaf, the way out, the Stranger, the scroll). Transcripts:
`rsprox_index.py get 4632 4633` (default and `--raw`). Ticks below are `[tick]` of 4632 unless
written `4633 [tick]`.

Our content was a LostCity (2004) port (`quest_routequest`): every page, the stage machine's
reads, the bridge's plane, the boats' shape, the ambush and the hound were 2004 text or invented.
OSRS rewrote this quest's dialogue since (none of the recording's 160 pages matched ours). It is
rebuilt here from the recording; a branch no session takes is the OSRS wiki transcript's
(`Transcript:In_Search_of_the_Myreque`, fetched 2026-11-21; itself an older text than the
recording) and is marked `[wiki]` in the scripts, mood unverified.

Files: `OSRS-Content/osrs-content/server/scripts/quests/quest_routequest/`
`scripts/routequest_start_and_route.rs2` (Vanstrom, Cyreg, boats, bridge, Curpile, doors),
`scripts/routequest_hideout.rs2` (cave mouths, members, Veliaf, ambush, hound, way out, Stranger,
completion), `scripts/routequest_shared.rs2` (fades, boxes, the copy, the lock, the hooks),
`scripts/routequest_journal.rs2`, `configs/*`. Test: `test/quests/routequest.lua`.

## Stages and vars (every recorded write)

| value | tick | goes out with | ours |
|---|---|---|---|
| 387 = 5 | [726] | click on "Sounds like a good place to start then." + "You've started a new quest" | same |
| 10 | [1325] | Cyreg "Hmm... Perhaps... But I'm sorry, I still can't help you." | same |
| 15 | [1388] | player "So you'll help me? Will you take me to them?" | same |
| varbit routequest_plank_dialogue (16793) = 1 | [1402] | Cyreg "You should also bring a hammer, 225 steel nails..." | same |
| 20 | [1648] | the objbox "The boatman takes three wooden planks..." (3 planks gone) | same |
| 25 | [1783] | arrival at the Hollows (first time) | same |
| bridgerung1..3 (176..178) | [1817], [1823], [1828] | "You repair a section of the bridge." | same |
| 52 | [1881] | Curpile "Okay, I's got an idea..." | same |
| 388 bits 0..5 (asked) | [1890], [1904], [1920]; [2073], [2081], [2088] | each question's page; all cleared on the first question's page | same (setbit/clearbit, `wholewrite=allow`) |
| routequestionscorrect / answered (173/174) | [1902] .. [2100] | the page after the player's answer | same |
| 55 | [2102] | Curpile "Okay, I believes ya..." | same |
| 60 | [2119] | the surface doors, first time (L+2, with the seq) | same |
| 65 | [2281] | Veliaf "Hmm... Very well. It's true..." | same |
| thsfm_spoken_allmyreque (2496) | Sani [2305] 1, Polmafi [2332] +8, Ivan [2350] +16, Radigad [2378] +4, Harold [2405] +2 | each member's recorded page | same (was a server-only varp 6878 in the 2004 port, never sent) |
| 67 | [2484] | "You give the weapons to Veliaf." (interface 11, longsword + mace) | same |
| 70 + thsfm_vanstrom_hide (396) = 1 | [2489] | the ambush's lock | same |
| 71, 72 ... 80 | [2553] | the scene's unlock, one tick | same |
| 85 | 4633 [54] | the tick after the hound's death, with the fade | same |
| 90 | 4633 [88] | Veliaf "Once you are done, I'd appreciate..." | same |
| 95 | 4633 [171] | the false wall (L+2) | same |
| 97 | 4633 [222] | the basement ladder's move | same |
| 100 | 4633 [266] | Stranger "If you say so, friend..." | same |
| 105, 110; 388 = 0, then bridgerung1..3 = 1 | 4633 [268] | the last page's click: the line, the jingle, the scroll | same (^routequest_complete 105 kept for every `>=` reader, 110 written after it) |

Both words are `scope=perm, transmit=yes` (4633 logs in with all of 388 restored).

## Action inventory (`rsprox_actions.py`), a verdict per row

4632:

| first tick | action | verdict |
|---|---|---|
| 7..132 | welcome screen, popout, settings, loot tracker | client UI, not content |
| 89 | Varrock Teleport (spell) | generic (spellbook), not this quest's |
| 136..299 | Varrock bank booth, deposits, withdrawals, tabs, placeholders, worn ops | generic bank |
| 160..193 | quest list, overview (782), world map | quest journal: see Journal |
| 325, 329 | prayer restore potions | generic drinking |
| 349..534 | Grand Exchange clerk, offers (steel weapons, nails, mushrooms) | generic GE |
| 536..565 | Varrock West banker, bank | generic bank |
| 578 | mort myre fungus on druid pouch | Nature Spirit's pouch (generic) |
| 605, 1422 | Kharyll teleport tablet (op 2) | generic tablet (Ancient tablets); not audited here |
| 613, 4633 [252] | Malak (fourdiamonds_vampire_lord) | Desert Treasure I post-quest line (another quest) |
| 624, 659 | Vanstrom (multi_vanstrom_stranger_entity) | **this content** (Start) |
| 787 | quest journal close | Journal |
| 810 | Hair of the Dog trapdoor at stage 0 | **this content**: "The trapdoor is locked." mesbox |
| 821, 1063, 1506 | stamina potions | generic |
| 849, 859, 861 | Ulizius, the Mort Myre gate, the wilderness-style warning | Nature Spirit / Mort Myre generic: not audited here |
| 943 | swamp snail (pick?) op2 | generic Mort Myre |
| 971..1002 | settings | client UI |
| 1069 .. 2004 | Cyreg Paddlehorn | **this content** |
| 1231 | world map orb | client UI |
| 1265, 1601 | route_rowboat_hollows (Board) | **this content** (the free boat to Mort'ton) |
| 1294, 1999 | route_rowboat_mortton (Board) | **this content** |
| 1434..1497 | Canifis bank booth (planks, hammer, coins, blessed sickle) | generic bank |
| 1517, 1519 | loot tracker | client UI |
| 1540, 1544 | the Mort Myre gate again | generic |
| 1805..1830 | spooky_tree_base_forbridge op2/op1, route_swampbridge_1..3 | **this content** (bridge) |
| 1835 .. 2102 | Curpile Fyod | **this content** |
| 2111..2138 | freedomfighterentrancer / undergroundentrancel | **this content** (doors) |
| 2148 | thrttavernbasementfalsewall at stage 60 | **this content**: "You search the wall but don't find anything of note." |
| 2152, 2154 | crate3 / crate3_old (basement) | generic crates: crate3 was bound, crate3_old was NOT (fixed in general_use/scripts/crates.rs2) |
| 2166, 2168 | a giant rat (attack, special) | generic combat, the recorder's kit (magic shortbow (i)) |
| 2184 | route_cavewalltunnel (in) | **this content** |
| 2202..2223 | the six Myreque | **this content** |
| 2230..2239 | burgh_chest_closed / open (garlic) | **In Aid of the Myreque's** garlic chest: now bound (quest_inaidofthemyreque/scripts/myreque2_hideout.rs2): Open = general_use `~open_chest` (L+2 human_openchest + chest_open, L+3 the open chest), Search = C human_closechest + chest_close, C+2 "You search the chest..." / "...you find a bulb of garlic which you add to your inventory.", a garlic, myq2_garlic_chest + 1 (0 -> 1 -> 2, saved). Unrecorded: Close, the open chest's lifetime (300 ticks), a full backpack, the varbit past 2 (capped at 3) |
| 2555..2560 | the hound (Attack), prayers | **this content** (fight); prayers generic |
| 2566 | route_cavewalltunnel_escape | **this content** (escape) |
| 2578 | logout | client |

4633:

| first tick | action | verdict |
|---|---|---|
| 11 | drop rotten food | generic drop |
| 12, 150 | route_cavewalltunnel | **this content** (into the hound's copy; out of the hideout) |
| 20..54 | the hound, prayers | **this content** |
| 99, 103 | quest journal | Journal (stage 90) |
| 104..145 | Veliaf, Radigad, Polmafi, Ivan | **this content** |
| 163..171 | false wall | **this content** |
| 176, 218, 228 | basement ladder | **this content** |
| 177..213 | crate3, crate3_old, crate_old, crate2_old | generic crates (see above) |
| 225 | the trapdoor from above | **this content** |
| 258 | the Stranger | **this content** (the end) |
| 274 | quest rating | client UI (not ours) |

Teleports: every one is a tablet (generic), a boat, the tree climbs, the doors, the cave mouths,
the copy entry/exit, the knock-out, the ladder or the trapdoor; each is a section below.

## Scenes and recurring frames

| scene / frame | where | shape (from the trigger) | ours |
|---|---|---|---|
| **The ambush** (cutscene_status 1, a copy) | [2487..2553] | T fade + empty chatbox; T+2 lock (fov_clamp **3**, rooftops override, area sounds 100), copy (14002,14895 = template 3506,9839), cast of six, hud lock, `cam_forceangle(160,960)`; T+3 `cam_moveto` (50,53) 555 232/100 + `cam_lookat` (49,40) 85 232/100; T+4 fade in; mist, the reveal, two barrages, the summon, the mist out; [2553] unlock with no fade, stages 71..80, the hound let loose, the escape loc on the cave mouth | `routequest_ambush`, every tick copied |
| The boats | [1265..1281], [1609..1625], [1767..1783], [2005..2021] | C fade (15 cycles) + chat shut; C+1 a boarding box with nothing to click, panels shut, silence + jingle 137 (`the_last_sail_in_search_of_the_myreque`), interface 321 with seq 1491 (out) / 1492 (back), minimap off; C+15 the landing (Hollows facing 1024, Mort'ton 512); C+16 panels back, modal and box shut, fade in, the arrival box | `routequest_boat_trip` |
| The doors | [2117..2125], [2127..2132], [2134..2141] | L+1 face; L+2 human_pickuptable; L+3 iron_door_open + fade; L+5 move; L+6 fade in; L+8 overlay shut | `routequest_door_pass` |
| The cave mouths | [2184..2193], 4633 [150..160] | L+1 face; L+2 human_longcrawl + fade + climbing_loop (3 loops, delay 4); L+4 move; L+5 fade in; L+7 overlay shut | `routequest_tunnel_crawl` |
| The hound's copy | 4633 [12..18] | C fade; C+3 a fresh copy (12850,15471), cast of five incl. the hound; C+4 fade in; C+6 overlay shut | `routequest_hound_enter` |
| The escape | [2569..2577] | "Are you sure you want to leave?" Yes/No; C fade; C+2 out at 3492,9824; C+3 fade in; C+5 overlay shut | `routequest_escape` |
| The hound's death | 4633 [53..60] | D the jingle (`the_fight_continues_fight_cave`), death seq 6558, synth 1188; D+1 stage 85 + fade; D+3 out of the copy beside Veliaf; D+5 fade in; D+7 overlay shut and Veliaf speaks unasked | `[ai_queue3,skeleton_hellhound]` + `[queue,routequest_hound_dead]` |
| The knock-out | [1934..1950] | K+1 box with nothing to click + Curpile's slash + hacksword_slash; K+2 block + stunned_thieving + stunned; K+3 human_death_backwards + fade; K+4 minimap off; K+8 beside Cyreg; K+11 fade in + the same box to click; "Ow... my head..."; Cyreg | `routequest_curpile_knockout` |

## Pages

All 160 recorded pages are in the scripts verbatim with their recorded chathead seq (the scripts
name the variant, e.g. `chatneu3`, so the head is Jagex's even where our wrap might differ).
Menus are copied with their recorded option order and with the recorded exclusion rules:
Vanstrom's, Cyreg's persuasion pages and the members' menus leave out the option just chosen;
Cyreg's main menu has five topics and shows the five besides the last asked (a fresh talk leaves
out "Where am I meant to be going?"), labels its vampyres topic "Tell me about the Drakans."
when the menu leaves out "Tell me about yourself." or "Tell me more about the Myreque." (and the
player still says "Tell me more about the vampyres.", [1733]), and switches its way out from
"I'll be back later." to "I'd best be off." once a question was asked ("Let's talk about
something else." resets it, [1729]). Curpile's menu drops "No reason!" after it and both "No
reason!" and the way-out question after "I'm lost..." ([1851], [1862]). Veliaf's weapons menu
starts with three topics and "More options..." ([2437]).

Unrecorded branches (from the wiki, `[wiki]` in the scripts): Vanstrom's "Hello again" between
start and ambush; Cyreg at stage 10, his cash / "I just want to help" / "What have they been up
against?" / "vigilante" answers and every way out; Curpile's no-weapons line and his after-55
line; the members' second greetings (Radigad's is recorded); Veliaf without weapons; every
"I'll be back later." / "Okay, thanks." the player says leaving.

## The bridge (fixed)

The deck is a set of bridged level-1 tiles walked on level 0, and the map already holds the
three rung multilocs (m54_53: 26245..26247 on 1 46,36..38, LINK_BELOW). The 2004 port added a
second set of rungs on level 1 and teleported the player onto level 1; the recording has every
rung, landing and climb on level 0 ([1806] loc_add_change at 3502,3428..3430,0). Ours now uses
the map's own rungs and the recorded tiles: up from 3502,3425 to 3502,3427 (L+1 face, L+2
human_reachforladder, L+3 the move and "You climb up the tree."), the repair on C
(pest_repair_quick 15168 + hammer_wood x3 at 0/20/40) and the line, the bit, a plank and 75
nails gone and the step onto the rung on C+3; down from 3502,3430 to 3502,3432 (C
human_pickupfloor + "You climb down the tree.", C+1 the move).

## The hound (4633 [15..56], 4632 [2553..2572])

| field | recorded | ours |
|---|---|---|
| levels | cache stat1..4 70/100/110/55 | same (.npc) |
| attack | seq 6579 every 4 ticks, synth 1187 | params attack_anim / attack_sound (were absent: the generated block's defaults) |
| defend | seq 6578 delay 51 on the arrow's launch tick, synth 1185 on the splat | defend_anim / defend_sound |
| death | seq 6558 on the 0-hp tick +1, `[facing] type=reset`, `enabled_ops 0`, synth 1188 | death_anim / death_sound; ops are the engine's |
| drop | none | `death_drop,null` |
| damage on the player | 13 swings, all 0 (4 unprayed, 9 under Protect from Melee) | the wiki's max; **unverified** (no non-zero splat to fit) |
| spawn | in the copy: (49,44) facing north after the summon; (49,42) on re-entry, walks to the player | same |

## Journal

| stage | recorded | ours |
|---|---|---|
| 0 (overview) | [173] `questjournal_overview_setup` with "talking to Vanstrom Klause in the Hair of the Dog Tavern in Canifis." | the cache's/overview path (unchanged) |
| 5 | [783]: one current paragraph, a second ("To do so, I'll need to find them..."), a blank row, the weapons header and five rows struck when held | verbatim |
| 90 | 4633 [99]: five struck paragraphs, then the current one | verbatim |
| 10..85, 95..110 | not opened | built from the recorded paragraphs; the current wording of those steps is UNVERIFIED |

## Npc movement (`npc_movement_audit.py paths`, recording vs our tick log)

| type | recorded | ours before | ours now |
|---|---|---|---|
| route_veliaf_hurtz_parent | 4 lives, 0 steps | wander 5 (17 steps/life) | `defaultmode=none` |
| route_sani_piliu | 77 steps in 3508..3512 x 9834..9838 | none (0) | wander 2 |
| route_cyreg_paddlehorn | 2 steps over 5 lives, 2x2 | wander 5 (35/life) | wander 1 |
| route_curpile_fyod | 1 step over 3 lives, 3x2 | wander 5 (17/life) | wander 1 |
| route_radigad / polmafi / ivan (_parent) | ~20 steps/life in 3x2 / 2x3 / 3x3 | wander 5 | wander 1 |
| route_harold_evans | 0 | 0 | 0 |
| the ambush cast (`*_cutscene`, vanstrom, mist) | script moves only (the mist's 2+2 steps) | wander 5 (generated default) | `defaultmode=none`, matched 2/2 mist walks |

## Wire audit (our tick log, run rq5, against 4632/4633)

| section | recorded | ours | verdict |
|---|---|---|---|
| start | stage 5 on the "Sounds like..." click with the line | same | matched |
| boat | jingle 137 C+1, landing C+15, arrival C+16, stage 25 at arrival | jingle +0/arrival +15 from the jingle | matched |
| bridge | seq 828 L+2 / move L+3; seq 15168 + 3 x hammer_wood on C, bit + move C+3; seq 827 + move C+1 | same | matched |
| quiz | bit per question page, counts with the next page, clears on the first page | same values (388: 7340064, 7340384, ...) | matched |
| knock-out | 424 + stunned_thieving K+2, 837 K+3, move K+8 | 424 / 837 on consecutive ticks | matched |
| ambush | every seq, spot, synth of `rsprox_tells --timeline` [2489..2553] | same ids, delays and spacing (rq3: 662..716) | matched, except: the hound hunted the player from its summon on (rq3); now `npc_sethuntmode(-1)` until the unlock |
| hound death | 0-hp splat H, seq 6558 + 1188 + jingle 76 + ops 0 + face reset on H+1, stage H+2, out H+4 | 805 / 806 / 807 / 809 | matched |

## Open

- The hound's first swing after the ambush: recorded on unlock+1 ([2553] -> [2554], after one
  step); ours unlock+2 (run rq6: 720 -> 722), one step later. The mode is set on the unlock
  tick from a resumed script, so its first step goes out in the next npc phase.
- GUESSED lines (no recording; marked `[guess]` in the scripts, written in the recorded
  voice): Cyreg refusing a passenger without ten gold ("You don't have ten gold on you,
  friend. No money, no boat, I'm afraid."); a rung without a hammer / 75 nails / a plank
  (one game message each); the doors before stage 55 ("The doors are locked.", a box like the
  trapdoor's [812]); the members at stages 67..79 ("Veliaf has your weapons now. You should
  speak with him.", reached only after a session ended inside the ambush). Replace any of them
  when a recording shows the real one.
- Stage 80 (the hound loose): every way into the hideout -- the passage mouth (4633 [12..18])
  and the Agility stalagmite -- leads to the fight's copy, whose Myreque are the `*_cutscene`
  types with no ops in the cache: nobody can be talked to until the hound is dead.
- The hound's max hit (above).
- The Stranger before stage 97 and after completion: only the first two pages (unverified).
- `last_quest_id` / `last_started_quest_*` varbits (16741..16763) Jagex writes around every
  stage: not sent by our quest framework (engine/framework-wide, not this quest's).
