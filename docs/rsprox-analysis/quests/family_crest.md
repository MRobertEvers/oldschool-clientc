# Family Crest: rebuilt from RSProx

Recordings: **index 4679..4683** (rev 243.1, w496, 2026-11-25). 4679: the quest
not started (the overview opened). 4680: stages 0 -> 4 (Dimintheis, Caleb).
4681: a login at stage 4. 4683: stage 4 in Al Kharid (the traders, busy Avan).
4682: stages 4 -> 11 (Avan, Boot, Witchaven, the perfect gold, Johnathon,
Chronozon, the crest, the completion).

Before this the quest was a LostCity (2004) port with videos for its journal:
65 recorded pages were not in our tree verbatim, Chronozon's crest dropped on
the wrong tick and his blasts were silent, Witchaven's doors were a guess
("The gate swings open, revealing the chamber beyond." is not sent), the
completion gave no XP, and Caleb's and Avan's pieces were swapped.

## Transcribing rev 243 (tooling, 2026-11-25)

The index server fails rev 243 ("no prot table for revision 243"), and the
transcriber had none either: rsprox-c still vendored 3draster's RSProt at
8b172e999, from before osrs243 (9eb0515bc, 7b819da1c). Synced
(`scripts/sync_rsprot.sh`, 3draster 6b06eaf3f), rebuilt with JDK 21; the
transcripts were made with a private classpath of the 1.0.6 jars (the shared
`~/rsprox-index-data/rsprox-c.classpath` still names 1.0.5).

The 243 cache is not on OpenRS2 yet, so RSProx printed bare ids. Each
`~/.rsprox-index-cache/bin/idx468N.bin` was rewritten with index 4677's rev
242 master index in its header (`tools/rsprox/splice_master_index.py`, the originals kept as
`idx468N.rev243mi.bin`), so names resolve through the 242 cache. The quest's
ids are all older than 242; a 243-only id would print unnamed.

## Action inventory (rsprox_actions.py, every session)

| action | verdict |
|---|---|
| Dimintheis talk x14, crest piece on him ("Nothing interesting happens.") | this content |
| Caleb talk x21, poordoor | this content; the door is the generic door |
| silk trader, Zeke, Karim, gem trader, Al Kharid man/man2, Ali Morrisane, Osman, Prince Ali, Hassan, Ellis, Al the camel, kebab, bankers | the Avan option (this content) and generic talk: Zeke, Prince Ali, Hassan, Ellis, Drogo fixed to the recording; Osman and Ali Morrisane below |
| Avan talk x24, jewellery on him (non-perfect: "Nothing interesting happens.") | this content |
| Boot talk x3, fai_dwarf_trapdoor_down, Drogo's shop | this content / generic |
| slug2_ruin_entrance, famcrest doors x43, levers x13, goldrock2 x9, crates, sacks | this content (Witchaven) |
| fai_falador_furnace (smelt, craft, perfect gold bar on it), Dommik's moulds | generic smelting/crafting, perfect gold |
| fai_varrock_stairs_taller / _top, bookcase, Johnathon talk x14, antipoison on him | this content / generic stairs |
| trapdoor, Edgeville dungeon gates, Chronozon x6 (four Blasts, an autocast), johnathon_crest pickup | this content |
| crest on crest x3, crest drops and pickups | this content |
| teleports: Camelot, Varrock, Falador, Lumbridge, Ardougne spells; dueling ring worn ops; glory worn op; Varrock tablet | generic (audited 2026-10-29) |
| GE, banks, castlewars chest, quest list x31, journal, quest rating, stamina/prayer/antipoison potions, prayers, sandwich lady random | generic / the recorder's own |

## Npc types, assets, stances, scenes

* `rsprox_npc_types.py --unscripted`: no quest-named type unscripted. The
  palace Prince Ali (11645) had no script: added (hassan.rs2).
* `content_asset_inventory.py --unused`: `famcrest_doorh2i2` (2428) is placed
  nowhere in the map; varbits 5348..5350 `family_quest_*` and the NMZ
  Chronozons are Family Pest / Nightmare Zone; the Blast Furnace perfect gold
  varbits are the Blast Furnace's. None is this quest.
* `player_bas_spans.py` (--bas): only the recorder's staff stance.
* No cutscene, card or fade in any session.

## Stages (varp 148, perm, sent)

| stage | written with | recorded |
|---|---|---|
| 1 | the Start menu's Yes, with "You've started a new quest" | 4680 [288] |
| 2 | Caleb's fish menu click (either answer) | [602] |
| 3 | the exchange box's continue (the fish out, caleb_crest in) | [867] |
| 4 | Caleb's "He said he was on some kind of search..." | [1095] |
| 5 | the gem trader's last page's continue | 4682 [264] |
| 6 | the player's "Well, I'll see what I can do." | [416] |
| 7 | Boot's "High quality gold eh?..." | [992] |
| 8 | the double box's continue (jewellery out, avan_crest in) | [2565] |
| 9 | Johnathon's "The... poison..." | [2849] |
| 10 | the antipoison's use (the potion gone, `liquid`) | [2998] |
| 11 | the last reward page's continue: 2,000 Crafting/Smithing/Mining XP, steel gauntlets, the scroll (model at 73,128), the rating | [4141] |

Avan is a multinpc on varp 148 ("Man" to 5, "Avan" from 6, "Gauntlets" op
from 11), as are Caleb and Johnathon ("Gauntlets" from 11).

## Pages

Every recorded page is in the tree verbatim with its mood (`page_gaps.py`
over all three page lists: the only rows left are Ali Morrisane's and
Osman's, below). The long pages are one string where Jagex's split falls
mid-sentence (Dimintheis's lore, Avan's lady, the gauntlets); our wrap gives
Jagex's breaks, "in-<br>fighting" included (wire log of a scratch run).
Unrecorded branches take the wiki transcript's words: Caleb's "You will? It
would help me a lot!", the post-quest gauntlet talks, the other traders'
Avan option, the start warning. Boot's answers to his two other options are
LostCity's (neither source has them).

## Journal

Stages 1..10 and stage 7 with the perfect jewellery: Jagex's rows verbatim
(`~quest_journal_rows`). `test/quests/_journals.lua` compares all 11 row for
row: 11/11. Stage 2 with a fish in the pack and stage 11 are unrecorded.

## Witchaven Dungeon

| thing | recorded (4682) | ours |
|---|---|---|
| entrance | L+1 face, L+2 teleport to 2696,9684, no line | same (was "You climb down into the old ruin." to 2696,9683) |
| lever | L+2 "The lever is now up./down.", macro_lever_switch_up/down, loc changed in place, synth `lever` | same, tick log L+2 |
| doors | g2h1 = G up & H down; h2 = H up; h2g1 = H up & G down; i2h1 = I up & H down (19/19 clicks) | same |
| shut | "This door is locked." on the op tick | same |
| open | walk-through: inviswall + leaf on the far tile for 2 ticks, `door_open`, the step on L+1 | same (tick log: L+1, revert +2) |
| gold | goldrock2 in the hellhound room: perfect_gold_ore, "You manage to mine some gold." | same |
| crates, sacks | "You search the crate but find nothing.", "There's nothing interesting in these sacks." | generic, same |

## Chronozon

| thing | recorded (4682 [3389..3453]) | ours |
|---|---|---|
| weakens | "Chronozon weakens..." on each landed Blast's cast tick, repeats too | same |
| Blast frames | `human_caststrike_staff_walkmerge`, `*_cast_and_fire` synth, `*_hit` area r10 at the impact, projectile 43/31 51..56 | the seq and both sounds were missing from all four Blast rows: added |
| death | kill splat D; D+1 death seq, enabled_ops 0, johnathon_crest private for 200; D+7 the body goes with vile ashes | same (`death_delay=0`, the drop after `npc_delay(5)`); was D+3 for both |
| his attacks | demon_attack every 4, black_demon_* sounds | generated anims and sounds already matched |

## Movement

`npc_routes.py`: Avan, Johnathon, Caleb, Boot, Dimintheis and Chronozon all
wander inside 5 tiles of their spawns: the generated default, left as is.

## Tests

* `test/quests/crest.lua`: 161/161 PASS (scriptrun, private packs,
  2026-11-25), with a tick log. Updated for the recorded pages, the swapped
  pieces, the Start menu, Caleb's door at 2815,3449, the entrance tile and
  the walk-through doors.
* `test/quests/_journals.lua`: the Family Crest block, 11/11 (scratch run).
* A scratch run of the branches the gate never reaches: Zeke's dragon
  scimitar and Avan option, the silk trader's, busy "Man", Avan's lost
  piece, two pieces then three combined, tired and "Wish me luck"
  Johnathon, a locked door: 24/24.
* `crest_selftest.rs2` (a `::crestrun` that called procs by hand) is gone.

## Beside the quest (fixed 2026-11-26)

| thing | recorded | ours |
|---|---|---|
| Osman, Prince Ali Rescue done, Contact! not started | 4682 [16..22], [99]: "Hello again. Well done on freeing Prince Ali." (chatshifty1), menu "Is there anything I can help you with?" / "I need to go."; walked away twice | the greeting and menu (was LostCity's "Well done, A great rescue..."); "I need to go." -> "Until next time." (wiki); the first option is his sq'irk errand: the RuneScape wiki's three-page ask, varbit 3328 `osman_apprentice` set (varp 974 made perm), the OSRS wiki's sq'irk menu and the juice hand-in (350 / 1,350 / 2,350 / 3,000 Thieving xp per winter / spring / autumn / summer juice). Everything past the menu UNVERIFIED |
| Ali Morrisane, Rogue Trader with Icthlarin's Little Helper done | 4683 [155..162]: greeting, "Still selling.", then the Siamun pitch, `roguetrader_clothes` 0 -> 1 with the menu "So what's in this for me?" / "Ok I'll get on to it." | the same (the draper's "~Sigh." pages are now only for a player without Icthlarin's Little Helper); "So what's in this for me?" is the other pitches' reply, one label; "Who did you want me to talk to again?" names Siamun while the errand is open (wiki). Siamun's own half is not built |
| `last_quest_*`, `last_started_quest_*`, `last_completed_quest_*` | sent after every stage write (4680 [288], 4682 [264], [4141]) and at login | `[proc,quest_progress_changed]` (quests/scripts/recent_quests.rs2), run by the engine on a script's write to a var `quest_progress.enum` names (generated from clientscript `quest_progress_get` by tools/quest_gate/gen_quest_progress_enum.py); the completed push in `~quest_complete_rewards_full` after the points; varps 6230..6238 perm and sent. Scratch run against the recorder's lists: start 25,165 / 25,114,99,68, completion 165 / 25,114,99,68, packet order as recorded |
| varbit 5934 | +1 on the swing's tick, any npc (Chronozon 4682, trolls 4455) | `~combat_hit_points` in `~give_combat_experience` (chance min(d,10)/10, the crab's fit), moved from the crab's landing |

Not built: the Sorceress's Garden itself (Osman's errand leads there), Siamun's
dyes/pelts exchange, and the garden's minigame teleport.

## Open

* Gauntlets: changing an imbue (25,000 coins, per the wiki's rewards list)
  has no dialogue in any source; only the first imbue is offered.
* Chronozon emptied before all four Blasts: no recording; healed with
  LostCity's "Chronozon regenerates...", and his death animation has
  already played by then. The wiki's "the weakness wears off" has no timer
  that any source states.
* Lever duration: 500 ticks (LostCity); recorded only that one stays at
  least 219.
* Generic: the strike and bolt rows are as silent as the Blasts were.
* The completion jingle: ours played id 153, the recording 152
  (`quest complete 1`); the proc's pick is generic.
