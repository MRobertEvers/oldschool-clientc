# Fishing Contest vs RSProx index 4404

Recording: index 4404 (rev 241.1, world 611, 2026-10-29, "Fishing Contest"),
the whole quest from Vestri's first page [241] to the scroll [1346] and the
tunnel [1360], then logout [1368]. One playthrough, so the routes and the
stranger's walks are one roll each. Ours: `test/quests/fishingcompo.lua`
(92 rows; 91 pass, the one FAIL is the travel staging's Camelot Teleport
cast-window row, below) and `test/quests/_journals.lua` (stages 2 and 4 row
for row).

Content: `areas/area_white_wolf_mountain/scripts/mountain_dwarf.rs2` (the
dwarves, the start and the finish), `areas/area_seers/scripts/hemenster/`
(`bonzo.rs2`, `fishermen.rs2` Morris/Dave/Joshua, `grandpa_jack.rs2`,
`sinister_stranger.rs2`), `areas/area_seers/scripts/mcgrubors_wood.rs2`,
`quests/quest_fishingcompo/` (`quest_fishingcompo_gate.rs2` the gate, the
garlic pipe, the tunnel stairs; `hemenster_fishing.rs2` the spots;
`fishingcompo_journal.rs2`). First ported from LostCity (2004), so per the
rsprox-content skill §16 every page and shape was rebuilt from the recording.
Branches the recording never took keep the wiki's text
(`Transcript:Fishing_Contest`, `Transcript:Vestri`, `Transcript:Grandpa_Jack`,
marked `[wiki]`, moods unverified) or LostCity's (`[LostCity]`, in neither
source). The LostCity port's invented pages (Morris's "invitation only"
choice, Big Dave's "Oi, I'm fishin' here.", Joshua's, Jack's "Can you teach me
to fish?", the dwarf's "So where is the trophy?" and "You give the trophy to
the dwarf.") are gone.

## Action inventory (`rsprox_actions.py`)

| first | n | action | verdict |
|---|---|---|---|
| 5 | 1 | welcome_screen:play | engine, login |
| 11, 461 | 5 | bankbooth / kr_bankbooth Bank, bank ops [23..101], [483..489] | generic (bank) |
| 110, 200 | 3 | poh_tablet_varrock/camelotteleport Break | generic, bound (`teleport_tablet.rs2`) |
| 125, 127 | 2 | amulet_of_glory Wear | generic (equip) |
| 151, 160..166 | 6 | banker Bank, stamina/scimitar withdraw, rune_scimitar Wield | generic |
| 172..192 | 14 | GE clerk, offers, collect | generic (Grand Exchange) |
| 210, 460, 880, 1084 | 4 | stamina potion Drink | generic |
| 240 | 5 | opnpc1 tunnel_dwarf1 (Vestri) | this content (rebuilt) |
| 245 | 5 | oploc1 tunnelstairstop | this content: the halt before the quest is done (rebuilt); after it, the maplink [1359..1360], ours `maplink.dbrow` 0_44_54_4_30 -> 0_44_154_4_26, matched |
| 422 | 1 | oploc1 kr_bookcase | generic (category 205 bookcase search) |
| 426 | 1 | oploc1 kr_poordoor | generic door |
| 428 | 1 | opobj3 garlic (Seers' kitchen) | generic take of the `m42_54.spawn` garlic |
| 544 | 1 | opnpc1 morris | this content (rebuilt) |
| 553 | 4 | oploc1 fishinggateclosedr / l | this content (rebuilt: walk-through gate) |
| 556, 602, 608, 1220 | 4 | opnpc3 bonzo (Pay) | this content (rebuilt) |
| 561 | 3 | opnpc1 bonzo | this content (rebuilt) |
| 596 | 7 | opnpc1 sinister_stranger | this content (rebuilt) |
| 665 | 4 | oploc1 garlicpipe | this content (rebuilt) |
| 677, 682 | 5 | questlist journal, close | this content: the journal at stages 2 and 4 (below) |
| 689 | 2 | opnpc1 0_41_53_compofishspot | this content: "You need a fishing rod to catch these fish." |
| 721 | 3 | oploc1 poshdoor (Jack's door) | generic door: opens, but silent (open: door sounds, below) |
| 725 | 4 | opnpc1 grandpa_jack | this content (rebuilt) |
| 850, 861, 1062 | 6 | opnpc1 mcgruborforester | this content: the gate's page, and "He doesn't seem interested in talking to you." |
| 851, 1053..1060 | 4 | oploc1 mcgruborgatel / r | this content (rebuilt: the forester from outside, "The gate is locked." + `locked` from inside) |
| 890, 895 | 2 | pillory crate Buy, rotten tomato buy | generic (Pelters' shop, bound) |
| 899 | 1 | oploc1 pillory_railing (Unlock) | generic, **was unbound**: now `pelters_veg_stall.rs2` |
| 928 | 5 | oploc1 ikov_shinykeydoor | generic (Temple of Ikov, bound: "The door is locked." + `locked`) |
| 933, 940 | 2 | oploc1 ladder_cellar | generic; [942] "I can't reach that!" (engine) |
| 949, 1085 | 3 | oploc1 mcgruborlooserailing | this content (rebuilt) |
| 968 | 1 | opobj3 dwellberries | generic take; a guard dog attacks the recorder [970] (generic, `::passive guarddog` in the test) |
| 985 | 1 | oploc1 crate2_old | generic: "You search the crate but find nothing." (OPEN: unbound in ours) |
| 999, 1008 | 3 | fairyring_minorhub Zanaris, Configure | generic, **was unbound**: now `general/scripts/misc/fairy_rings.rs2` (no Fairytale II in this tree, so the recorded refusal for every player) |
| 1000 | 1 | oploc1 tree2 | generic woodcutting, superseded by the next click |
| 1013 | 5 | oploc1 red_worm_* | this content (rebuilt) |
| 1021, 1029 | 2 | spade Drop, take | generic (drop, take) |
| 1171 | 1 | garlic on sinister_stranger | this content (new) |
| 1186 | 1 | garlic on garlicpipe | this content (rebuilt) |
| 1190 | 2 | opnpc1 0_41_53_sinisterfishspot | this content (rebuilt) |
| 1328, 1339 | 2 | hemenster_fishing_trophy Drop, take | generic (drop, take); the dwarf's no-trophy branch between them |
| 1351 | 1 | questscroll close | this content (the dwarf goes on) |
| 1362 | 1 | oploc1 tunnelstairs | this content: up, the maplink [1363] |
| 1368 | 1 | logout | engine |

## Teleports

| tick | from -> to | cause | verdict |
|---|---|---|---|
| 114 | -> 3213,3424 | Varrock teleport tablet | generic, bound |
| 205, 1282 | -> 2758,3478/9 | Camelot teleport tablet | generic, bound |
| 952 | 2661,3500 -> 2662,3500 | the railing, in | rebuilt, matched (below) |
| 1092 | 2662,3500 -> 2661,3500 | the railing, out | rebuilt, matched |
| 1360 | 2821,3486 -> 2820,9882 | tunnel stairs down | maplink, matched |
| 1363 | 2820,9882 -> 2820,3486 | tunnel stairs up | maplink, matched |

## Stages and vars

| tick | var | value | goes out with | ours |
|---|---|---|---|---|
| 362 | varp 11 | 0 -> 1 | the page after the pass's box, with "You've started a new quest" and the pass | same (`tunnel_dwarf_friends`) |
| 550 | varbit 2053 `_passed` | 1 | Morris's box "You show Morris your pass." | same |
| 553 | varbit 2053 | 0 | the gate's op, through | same |
| 578 | varbit 2052 `_paid` | 1 | the box "You pay Bonzo 5 coins.", the coins | same |
| 601 | varp 11 | 1 -> 2 | the willow box, 9 ticks after the stranger's first step | same (t85 step, t94 stage) |
| 715 | varp 11 | 2 -> 1 | the click of "Ok, I'll see you again." | same |
| 1188 | varbits 2051 `_garlicpipe`, 2054 `_stranger` | 1, 1 | the click of "You stash the garlic in the pipe.", the garlic | same |
| 1204 | varp 11 | 1 -> 3 | Bonzo's "Hmm..." 9 ticks after the stranger's first step | same (t287 step, t296 stage) |
| 1243 | varbit 2052, varp 11 | 0, 3 -> 4 | the click of "You hand over your catch.", carp -> trophy | same |
| 1346 | varp 11, varbit 2051 | 5, 0 | the click of "Oh! It's even more shiny...", trophy gone, scroll | same; 2054 stays 1 on both sides |

**The varbits were never sent.** All four sit on `varp12_garlicpipe`, which the
generated block made `scope=temp, transmit=no` (the Tree Gnome Village trap): the
client never saw the stranger's multinpc rung (2054 picks `sinister_stranger0/1`)
and the fee and the garlic were lost at logout. Now `scope=perm, transmit=yes` in
`quest_fishingcompo.varp`, the generated lines deleted;
`varp_transmit_audit.py` lists no quest var.

## Sections

### Vestri's start [241..363] and the stairs [245..256]

| R | ours | verdict |
|---|---|---|
| "Hmmph. What do you want?" only on Talk-to; every branch but "grumpy" and the start goes back to the first menu with no greeting ([257], [262], [266], [280], [297], [313]) | `@tunnel_dwarf_menu` | rebuilt (LostCity ended each branch) |
| menu "I was wondering what was down those stairs?" / player says "I was just wondering..." | same | rebuilt |
| "Do you have a brother?" / "What if I do! It's no business of yours." | added | rebuilt (missing) |
| "Good. Because you can't." ... "Well then." (8 pages) | same | rebuilt (LostCity: "Good.") |
| "I'm bigger than you. Let me by!", "...stumpy.", "Go away! You're not going to bully your way in HERE!" (angry, neutral, angry) | same | rebuilt |
| stairs before completion: "Hoi there! Halt!", "You can't come in here!", menu "Why not?" / "Oh sorry, I hadn't realised it was private." / "I'm bigger than you. Let me by!", "Well, it is." then the first menu | `@tunnel_dwarf_halt` | rebuilt (LostCity: a game message) |
| start: "Well, let's be friends!" -> "I don't make friends easily..." -> `Start the Fishing Contest quest?` Yes/No; Yes = "And how am I meant to do that?" | same | matched |
| the box "You got the Fishing Contest Pass!" (zoom 400), then the stage, the line and the pass with "Go to Hemenster and do us proud!" [362] | same | matched |
| No / under 10 Fishing | `[wiki]` grumpy; "I'd rather not, thank you." + the requirements box | unverified |

Every recorded page's text and mood was checked mechanically against the scripts
(pages from `rsprox_pages.py`; 0 missing, 0 mood mismatches).

### Morris and the gate [544..555], [1150..1162]

| R | ours | verdict |
|---|---|---|
| Talk-to: the PLAYER asks "What are you sitting around here for?" (confused), Morris "I'm making sure only those with a competition pass enter the fishing contest.", "I have one here.", box "You show Morris your pass." with `_passed` = 1, "Move on through. Talk to Bonzo to enter the competition." | same | rebuilt (LostCity: Morris spoke first, a "Show your pass at the gate" line) |
| gate with `_passed`: through on the op [553], `_passed` 0 | same | rebuilt |
| gate with the pass: "Competition pass please.", the box, "Move on through...", through on that page's click [1160] | same | rebuilt |
| every crossing: `inviswall` rot 2 in both leaves' slots, `inacfishinggateclosedl` (2643,3442) / `inacfishinggateclosedr` (2644,3442) rot 1 for 2 ticks, `gate_open` (67), the step | `~fishingcompo_gate_cross` | matched (t72, t116, t271, t340: four locs, 67, back 2 ticks later) |
| leaving in the competition: Bonzo stops the player a tile short [706..707] (ap range 1), "So you're calling it quits here for now?"; Yes: the stage back to 1 on the last click with a step onto the gate, through a tick later [715..716] | `[aploc1]` + `p_aprange(1)`; anyone else `p_aprange(-1)` -> the op [1263..1265] | matched (t115 stage, t116 crossing) |
| Morris turns to the player on his pages; Bonzo, speaking across the field, does not | `~fishingcompo_chat_from_afar` for Bonzo | matched |
| no pass / post-quest | "Competition pass please." and nothing more; Land of the Goblins' whitefish pass kept | unverified |

### Bonzo [556..612], [1221..1249]

| R | ours | verdict |
|---|---|---|
| Roll up (3 pages), menu, "No thanks, I'll just watch the fun." ends | same | matched |
| "I'll enter...", "Marvelous!", the fee box, the next page a tick after its click [579..580], "Ok, we've got all the fishermen! It's time to roll!", "Ok, nearly everyone is in their place already..." | same (`p_delay(0)`) | matched |
| the places, no garlic: the chat closes, the stranger walks willow -> pipes (11 steps, a face reset with the first), faces 2634,3444 a tick after arriving, the willow box and stage 9 ticks after his first step | same; ours starts from his spawn (5 steps) | matched (first step [592] read as the click's jitter: the garlic walk steps on its click) |
| Pay when paid: "You've already paid, you don't need to pay me again!", the places page, the willow box at once (stranger already there) [602..610] | same | matched |
| hand-in: "So how are you doing so far?", menu, "I have this big fish." / "Is it enough to win?", a tick's pause, "Well, we'll just wait till time is up.", a tick, mesbox "You wait", 8 ticks, `human_pickuptable` (832) + "You hand over your catch.", on its click the fee varbit, stage 4, carp -> trophy with "We have a new winner!...", box "You are given the Hemenster fishing trophy!" | same | matched (t331 anim, t332 stage) |
| other catches, "bigger fish", "Hello champ!", the spare trophy | `[wiki]` | unverified |

### The Sinister Stranger and the pipe [596..688], [1146..1210]

| R | ours | verdict |
|---|---|---|
| Talk-to tree ("...", "Who are you?", "So... you like fishing?") with the OSRS spelling ("ze smell ov garlic und", "ein velaxing hobby", "I tsink I may do zat soon...") | same | rebuilt (LostCity: "the smell of garlic and", "vampire") |
| garlic on him: "Uurrggh! Get zat horrible tsing avay from me!..." (angry), "Um... sorry" | added | rebuilt (missing) |
| pipe search: player shifty "Ewww - it's a smelly sewage pipe." | same | rebuilt (LostCity: a message) |
| garlic in the pipe: box "You stash the garlic in the pipe." on the op, the garlic and both varbits on its click; no stage | same | rebuilt (LostCity moved him on the stash) |
| the pipes' spot, paid, stage 1, garlic stashed [1190]: Bonzo's places page, the stranger "Arrgh!..." (no turn to the player), on its click the chat closes and he walks to the willow (11 steps S,S,S,S,SW x5,W,W), stage 3 with Bonzo's "Hmm..." 9 ticks after his first step, the pipes box; he faces 2630,3435 a tick after arriving | same | matched step for step (t287..t297, stage t296, face t298) |
| one npc for the world, a multinpc whose rung is `_stranger` | `npc_walk` of the shared npc, `defaultmode=none` | matched; his first spot is the generated spawn (2637,3440), recorded at the willow [546] |
| fishing his spot | `[wiki]` "I think you will find that is my spot." | unverified |

### Fishing [689..695], [1212..1215]

| R | ours | verdict |
|---|---|---|
| no rod: "You need a fishing rod to catch these fish." | same | matched |
| the pipes' spot, rod + red vine worm: C the op, C+1 `human_fishing_casting` (622), C+3 "You catch a giant carp.", carp in, a worm out; one cast, no xp | same | matched (t303) |
| other bait/spots' catches | LostCity's (sardine, shrimps) | unverified |
| Dave's and Joshua's spots; their Talk-to | `[wiki]` | unverified (never clicked) |

### Grandpa Jack [725..836]

| R | ours | verdict |
|---|---|---|
| greeting "Hello young man!<br> Come to visit..." (a hard break) | same, gendered | rebuilt |
| the 5-option menu in Jagex's order, during the quest | same; the wiki's 4 outside it | rebuilt (LostCity's order and option text) |
| the story (5 pages), "I've forgotten how to fish..." (7 pages, a 4-row page cut "...and then be" / "patient... ") | same | rebuilt |
| the hints, incl. "Try to get a spot round there..." (missing from the wiki) and the player's "There's this weird creepy guy..." | same | rebuilt (LostCity invented the garlic hint) |
| the rod: "Of course you can young man...", "Very fair, I'll buy that rod!", "Excellent choice!", the money and the rod with the double box (rod, coins, zoom 400), "Thanks!", "You're welcome young 'un." | same (`~fishingcompo_objbox_double`) | rebuilt (LostCity: 5 coins, "Yes please.") |

### McGrubor's Wood [850..1092]

| R | ours | verdict |
|---|---|---|
| gate from outside (z < 3470): the forester "Hey! You can't come through here! This is private land!", a tick after its click the mesbox "You will need to find another way in." | same | matched |
| gate from inside (z >= 3470): "The gate is locked." + `locked` (2402) | same | matched (t229); LostCity tested `>` and gave a player on the gate row the forester |
| railing in: L+1 the face, O+1 the line (spam), `railing_squeeze` (3844), exactmove 15..90, angle 512 | same | matched (t168) |
| railing out: O+1 a step onto the railing's tile, O+2 a face west, O+3 the slide, angle 512 | same | matched (t262) |
| red vine: O `human_dig` (830) + `digspade` (1470), O+1 "You dig in amongst the vines." "You find a red vine worm." + the worm; one a click; no spade: "You need a spade to dig up this vine." | same | matched (t208); LostCity's "You use your spade with the vine." deleted |
| the forester walks round his post (2649..2652 x 3464..3469) | wanders (range 5), `mcgruborforester.move` authored | fixed (was stationary) |

### Vestri's hand-in [1318..1357]

| R | ours | verdict |
|---|---|---|
| no trophy: "Have you won yet?", "Yes I have!", "Well done! That's brilliant! Do you have the trophy with you?", "Uh... I don't have it with me.", "You'd better go get it then hadn't you?" | same | rebuilt |
| with it: "Yep, I have it right here!", "Oh! It's even more shiny and gold than I thought possible...", and on its click the stage, the garlic varbit, the trophy gone, 1 QP, the line, disease -50/0, song 147 + jingle 152, the scroll (trophy at 78,128): "2,437 Fishing XP / Access to the White Wolf / Mountain Tunnel" | same | rebuilt (LostCity's rows: "underground White Wolf Mountain passage", an Angler's outfit line) |
| on the scroll's close: "You've done us proud...", "In where?", "Why the tunnel of course!...", "Excellent. That will come in most handy." | `[queue,fishingcompo_after_scroll]` | matched |
| the Fishing level-up after the dialogue [1358] | engine | generic |
| stages 1-3, stage 5, an extra trophy | `[wiki]` | unverified |

## The journal

| stage | R | ours |
|---|---|---|
| 2 | [677]: 4 rows (struck "The Dwarves will let me use the tunnel through White Wolf" / "<str><str>Mountain if I can win the Hemenster Fishing Competition" with no full stop, the willow, "I need to catch the biggest fish to win!") | row for row (`_journals.lua`), tags on a `TORIRSSERVER_TRACE_OUT=2` run |
| 4 | [1252..1261]: 5 rows, the full stop back, "<str>I easily won the contest by catching some Giant Carp.", the trophy step in two rows | row for row |
| 0, 1, 3, 5 | not opened by any recording; no wiki journal transcript | UNVERIFIED (3 = stage 2 with the pipes) |

`split_init` (`torirs_server_split.c`) used to put the carried colour in front of a
row that opens its own (`<col=800000><col=000080>I need to catch...`). It now
drops the carried tag there (it drew nothing); `make -C src test-torirsserver-split`
has the case. The title is the generic `<col=800000>Fishing Contest`; Jagex sends
`<col=7f0000>Fishing Contest</col>` (every quest: generic, open).

## Npc movement (`npc_movement_audit.py paths`)

Bonzo, Morris and the fishing spots stand on both sides; Grandpa Jack, Big Dave,
Vestri and the guard dogs wander on both; the stranger walks only on script on both;
the forester was the one diff (fixed above).

## Open

- **Door sounds (generic).** Jack's `poshdoor` opens with `nicedoor_open` (81) in the
  recording [723]; every door in our tree is silent (`doors.rs2` header: no
  `open_sound` param can be declared yet).
- **Music (generic).** The recorded songs (119 by Vestri, 184 in Hemenster and the
  wood, 140 Seers) differ from our region rows (`music_regions.tsv` is per square,
  open since 2026-10-25).
- **`crate2_old` Search** ("You search the crate but find nothing.", [985..989]) is
  unbound.
- **The zone's ambience.** `fish_swim` (2601) and the ducks' `quack`/`duck_hit`
  area sounds along the river [1171..1210] are not this quest's and unaudited.
- **The stranger's first spot.** He starts on the generated spawn (2637,3440); the
  recording first shows him at the willow (a shared npc, left wherever the last
  player's contest put him).
- **The test's Camelot Teleport cast row** fails on its cast window while the landing
  row passes (driver timing for spells, not this content).
- Unverified branches are marked `[wiki]` / `[LostCity]` in the scripts.
