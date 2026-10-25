# The Knight's Sword: rebuilt from RSProx index 4369 and 4370

Sessions (both rev 241.1, world 631, 2026-10-24, the owner's "Knights Sword" uploads):

- **index 4369**: stage 0 -> 2. The Squire's start ([77..113]) and Reldo's introduction and Imcando pages ([164..179]).
- **index 4370**: stage 2 -> 7. Reldo again plus the Shield of Arrav start ([10..51]), Thurgo ([508..571]), the Squire ([683..702]), Sir Vyvin ([750..788]), the cupboard ([799..807]), the portrait ([1122..1153]), mining ([1301..1308]), the sword ([1393..1407]), the hand-in and completion ([1437..1454]).
- **index 3224** (rsprox.net 3241, rev 233): the cupboard's catch rule and its revert ([1963..2420]).
- **index 3122** (rsprox.net 3139, rev 232): mining sounds.

Our side was a LostCity (2004) port: `quests/quest_squire/scripts/quest_squire.rs2`, `areas/falador/scripts/squire.rs2`, `areas/port_sarim/scripts/thurgo.rs2`, `areas/varrock/scripts/reldo.rs2`, `areas/falador/scripts/sir_vyvin.rs2`. Branches no recording takes use the OSRS wiki transcripts (Transcript:The_Knight's_Sword, Transcript:Thurgo, Transcript:Reldo, Transcript:Squire) and are marked *unverified* in the scripts.

Verification: `test/quests/squire.lua` now walks the recorder's own path (every talk and every menu choice of 4369 and 4370) and records `ticklog.tsv`; run on scriptrun with `TORIRS_SCRIPTRUN_WIRELOG`. The page sequence of both transcripts was diffed against ours line by line (`docs/rsprox-analysis/quests/tools/ks_page_seq.py`): **every recorded page, menu, mesbox, objectbox, stage write and quest message has its identical line in ours** (speaker side, chathead seq, text with every `<br>`, menu title and rows, objectbox obj and zoom). The one remaining difference is the level-up (below).

## Defects found and fixed

| # | where | recorded | ours before | fix |
|---|---|---|---|---|
| 1 | Squire start | the cache's start menu "Start The Knight's Sword quest?" Yes./No. ([110]); stage 1 + "You've started a new quest" on the Yes click ([112]) | the player's line as a 2-option menu, no start line | `~p_choice2_header`, stage and line on Yes |
| 2 | Squire | "...research on the races of Gielinor." ([109]) | "RuneScape" | text |
| 3 | Reldo | first talk: "Hello, stranger." + a 5-page introduction, `reldo_met` (11509) written on its last Reldo page ([164..169]); later "Hello there, <name>." (4370 [10]) | "Hello stranger." always | greeting + varbit |
| 4 | Reldo menu | quest / Imcando / trade / "I'd better get going." (4369 [170]); Imcando only at stage 1 (4370 [13]) | "What do you do?" row, no goodbye | menu rebuilt |
| 5 | Reldo Imcando | 8 pages ([173..178]), stage 2 with the last page | 2004's 7 pages | pages |
| 6 | Reldo Shield of Arrav start | 9 pages; varp 145 = 1, varbit 13716 = 1, "You've started a new quest" on [50] | 2004 pages, no line, no varbit | pages, line, varbit (13716 = 2 at SoA's completion: the cache row's `endstate`) |
| 7 | Thurgo | every talk opens on a menu, no greeting ([508], [1123], [1393]): the stage's question, the pie while carried, the cape | 2004's greeting-led branches; stage 0-1 mesbox "doesn't appear to be interested" | `[label,thurgo_menu]` |
| 8 | Thurgo pie | objectbox held with no continue row, rows filled at +0, +3 (synth `eat` 2393), +9, continue only on the last ([552..561]); then the talk restarts as a new op: close + menu on the same tick ([563]) | one mesbox, the sword talk as a second click | timed boxes + `p_opnpc(1)` |
| 9 | Thurgo pages | "We Imcando dwarves love them - they're GREAT!", the 5 materials pages ([1146..1151]), "Okay. I'll go and find them then." | 2004 text | pages |
| 10 | Thurgo portrait / sword | objectbox (zoom 400) with the item; portrait and stage 6 on the box's click ([1146]); ore/bars out and sword in as the box opens ([1404]) | mesbox; inv changes after the talk | `~objbox_scaled(.., 400)`, inv on the recorded tick |
| 11 | Squire stage 5 | "I didn't get the picture yet." ([700]) | "...yet..." | text |
| 12 | Squire hand-in | the player speaks first, no "So how are you doing" ([1437..1439]) | the opener | skip it with the sword in hand |
| 13 | completion | all on the mesbox click ([1445]): scroll rows "1 Quest Point", "12,725 Smithing XP", the sword's model at 78,128; jingle 153 (9,955 ms) | queued a tick later; "12725 Smithing XP\|Smelt blurite ore..." with a coins model; **no jingle** | inline `~squire_complete`; rows; jingle (14) |
| 14 | `~quest_complete_jingle` (shared) | — | `case 3 :` / `case 1 :` were empty arms (no fall-through): every difficulty-1/3 quest with no recorded row played no jingle | `case 3, 4 :` / `case 1, 2 :`; Knight's Sword added to the recorded `quest_complete_2` row |
| 15 | Sir Vyvin | option "Can I just distract you for a minute?"; the long line `chatneu3` ([754], [774]) | "Can I distract you...", happy | text, mood |
| 16 | cupboard open/shut | no message; anim `human_openchest`/`human_closechest` + synth `cupboard_open` 58 / `cupboard_close` 57 on the op tick (after `p_arrivedelay`), the loc a tick later (4370 [800..802]; 3224 [1971..1973], [2106..2107]); open reverts 299 ticks later (3224 [2107] -> [2406]) | "You open the cupboard." immediately, revert 500 | the recorded shape |
| 17 | Thurgo movement | walks: 14 steps over 9 lives, 2998..3002 x 3142..3145 (4370, 3437, 3114) | `wanderrange=0`, `defaultmode=none` | `npc_movement/t/thurgo.move` authored, `moverestrict=indoors`, `quest_squire.npc` wander 5 |
| 18 | mining (shared) | `mine_ore` 3600 on the success tick, nothing on a swing: 33/33 successes and 0/33 swings (index 3122), 4370 [1301..1308] | `mine_quick` every swing tick, `mine` on success | `mining.rs2`, docs/SKILLING_SOUNDS.md §4.1 |

## The cupboard's catch rule

Sir Vyvin stops a search when he stands within one tile of the player (Chebyshev): 3 catches at distance <= 1 and 13 passes at distance >= 3, across index 3224 and 4370 (`docs/rsprox-analysis/quests/tools/ks_cupboard_rule.py`). Ours already tested exactly that (`npc_find(coord, sir_vyvin, 1, 0)`); kept. The catch applies after the portrait too (3224 [2420]).

## Matched without change

- Every page's chathead mood and row count, the stage writes' ticks relative to their pages (1 [112], 2 [178], 3 [552], 4 [570], 5 [689], 6 [1146], 7 [1445]).
- Npc info: the four npcs only ever send `[facing]` (`npc_block_inventory.py`: 0 kinds missing); each faces the player on the talk's first tick and resets with its first wander step after.
- Quest vars reach the client: varp 122 (transmit), `reldo_met` and SoA's varbit (bits 15 and 6-7 of varp 3403, transmitted).
- The completion's other packets: varp 101, the line, disease -50/0, song 147 with a zero envelope, the scroll title.

## Still open

- **Level-up display (engine-wide).** Recorded after the scroll closes: the line, `levelup_display` (233), jingle 64, `levelup_anim` spotanim, two area sounds (4370 [1449..1452]). Our `[advancestat,_]` only pushes the combat summary (levelup.rs2's own header); no skill's level-up is shown anywhere yet.
- **Thurgo at 99 Smithing**: "Can we talk about skillcapes?" and the cape sale (Transcript:Thurgo) need a skillcape seller this tree does not have; the cape question shows instead.
- **Unrecorded branches** are the wiki transcript's, moods chosen by the line: the Squire's "Is he angry?", "I can make a new sword", "No." to the start, the stage 1/2, 5 (portrait held), 6 (no sword, worn sword) and post-quest talks; Thurgo before Reldo, without a pie, the Giant Dwarf variants, the second pie, the portrait-less "About that sword...", the materials refusals, the post-quest sword; Reldo's goodbye and Lost Tribe menu; the start advisory box.
- Shield of Arrav's varbit 13716 is set to 1 only by Reldo's route; Charlie's Black Arm route never writes it.
