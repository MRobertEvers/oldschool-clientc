# Sheep Herder from RSProx (index 4685, rev 243)

Index 4685 is one session, recorded 2026-11-26, that starts the quest and completes it. Index 4684, from the same day, is an unrelated bank session.

The session's revision was 243, so it needed the two fixes in [[rsprox-new-revision-transcribe]]:
- the rsprox-c 1.0.6 classpath;
- the 4677 master index spliced in, with the original kept as `idx4685.rev243mi.bin`.

The content before this was a LostCity port (2004 text, one-tile pushes, a sheep made by `npc_add` in the pen). It is rebuilt from the recording.

## Action inventory (`rsprox_actions.py`)

| first tick | count | action | verdict |
|---|---|---|---|
| 17, 102 | 1, 4 | Varrock / Ardougne teleport | recorder's travel, generic |
| 42, 51 | 13, 2 | quest list, overview close | journal: section below |
| 127 | 12 | Talk-to Councillor Halgrive | this content |
| 324 | 1 | Talk-to Priest (`orbon_priest`) | **was unbound**: now "Hello there." / "Peace be with you, friend." |
| 331 | 1 | altar Pray | generic, existing |
| 336 | 8 | Talk-to Doctor Orbon | this content |
| 367, 387 | 2, 4 | drop/take coins (to test the poor branch) | generic |
| 403.. | many | equip/unequip suit, staff, cattleprod | generic equip; the suit and weapon checks are this content |
| 453.. | 29/70/107/258 | Prod plaguesheep_1..4 | this content: herding |
| 524, 530 | 7, 7 | gate leaves | this content: gate |
| 548 | 1 | take cattleprod | generic ground item (spawned in the barn) |
| 584 | 6 | Talk-to Farmer Brumty | this content |
| 628.. | | bank, stamina potions | generic |
| 997, 1828.. | 6 | feed used on a pen sheep | this content |
| 1010, 1859.. | | take sheep bones | generic pickup of the private drop |
| 1045 | 1 | bones on dibber | generic "Nothing interesting happens." |
| 1058 | 1 | bones on fire | generic cooking "You can't cook that." |
| 1070.. | 5 | bones on the incinerator | this content |
| 1263 | 1 | Examine Yellow Sheep | client-side examine |
| 1794.. | 5 | Prod a pen sheep | this content |
| 1810, 1818 | 2 | Telekinetic Grab on a pen sheep | **was unbound**: now "You can't use Telekinetic Grab on them." (`telegrab.rs2`, any npc) |
| 1813 | 1 | Teleport boat to me | **unbound**: Sailing is in our tree (`sailing/`), but this spell's button has no script (Open) |
| 1937, 1971 | 1, 1 | bones on Halgrive / Orbon | generic "Nothing interesting happens." |
| 2135 | 1 | quest rating | shared scroll code |
| 2175 | 1 | Hunter skill guide | generic |

Teleports: only the recorder's spells. No ladders, no cutscenes (no `cutscene_status`, camera or fade lines in the session).

The player's base animation set (`player_bas_spans.py`) changes only with the weapon wielded: staff or cattleprod. There is no stance tied to the quest.

The npc types the server adds (`rsprox_npc_types.py --unscripted`) include no quest-named type without a script. `content_asset_inventory.py --unused` finds nothing unused.

## The quest varp and varbits

| tick | write | goes out with | ours |
|---|---|---|---|
| [292] | `sheepherderquest` 0 -> 1, the recent-quest lists, "You've started a new quest", the feed, its objbox | the click on "Please also take this poisoned sheep feed" | same order |
| [395], [725] | 1 -> 2 on the first sale only; coins, suit, `objectbox_double` | the click on "Okay, I'll take it." | same |
| [970] etc. | `sheepherder_sheep_N` 0 -> 1 | the jump's C+4 | same |
| [1005] etc. | 1 -> 2 | three ticks after the feed box's click | same |
| [1076] etc. | 2 -> 6 | three ticks after "You put the remains into the furnace." | same |
| [2133] | 2 -> 3; a..d back to 0; `tracking_coins_gained` +3100; quest points, the completed list, the line; the feed taken; scroll "4 Quest Points / 1,000 Hunter XP / 3,100 Coins" with the feed at 78,128; quest complete 3 | the click on the last Halgrive page | same |

Before this change:
- the completion was queued;
- the scroll read "3100 coins / Hunt goats on Wyrmscraig" with no Hunter XP;
- the feed and the sheep varbits were left in place.

## Pages

Every recorded page is now in the content verbatim, with its recorded chat head. The old text was LostCity's, and the recording differs from it in many places:
- Halgrive opens with "Hello. How are you?".
- He now says "some sheep", not "four sheep".
- The player asks "Are you sure it's the plague?".
- The start is the "Start the Sheep Herder quest?" Yes/No menu.
- He says "Doctor Orbon should have something suitable."
- The No answer reads "If you know of anyone suitable".
- Orbon's options read "Okay, I'll take it." and "No, I don't need any more.".
- Brumty's stage-2 lines changed.
- The prod refusals have new wording: "I don't want to attack the sheep. <br>I need to make it move!" plus "a more appropriate tool" for a weapon, and "an appropriate tool" for bare hands.

Where a branch isn't recorded, the text comes from the wiki transcript (Transcript:Sheep Herder):
- Halgrive's stage 1 and the lost-feed branch;
- Brumty's stage 1;
- Orbon's full-pack answer;
- feed outside the pen, the vial of poison, bones while that sheep is in the pen, bones held.

Stage 0 and stage 3 for Orbon, Brumty and Halgrive keep LostCity's text, marked UNVERIFIED in the scripts.

## Herding (`diseased_sheep.rs2`)

Three wandering copies per colour (`plaguesheep_N`, multinpc `herder_plaguesheep_N`) plus one pen npc per colour (`herder_plaguesheep_N_enclosure`). The pen npc is visible only while that colour's varbit is 1. All of these were already in `m40_52.spawn`.

- **A prod.** Recorded 239 times. Each sends:
  - the player's `cattleprod` seq;
  - "BAAAAA!" over the sheep;
  - `sound_area cattleprod` at the sheep, range 5;
  - a walk of 4 tiles straight away from the player, one per tick (23 prods walked 4 tiles and stopped; [804] was cut short by a wall).

  LostCity walked the sheep 1 tile. Every recorded prod was made from a side tile.
- **The jump.** Recorded at [966], [1212], [1449] and [1783], each prodded in the gate pocket at x 2592-2594, z 3360-3364. The zone was widened from LostCity's z 3363 because [1449] jumped from z 3364.
  - C: the sheep is teleported onto the pen's first row: 2595,3361 when pushed east, 2595,3362 when pushed south. "BAAAAA!" and the cattleprod synth play.
  - C+2: it is teleported to its pen tile (the enclosure npc's tile).
  - C+4: the wandering sheep is removed, the varbit goes to 1 and the mesbox "The sheep obligingly jumps over the gate and into the enclosure!" opens.
- **Refusals, in order** (all recorded):
  - not wearing the whole suit;
  - bare hands, then a weapon that isn't the cattleprod;
  - "You have already disposed of this sheep; there's no need to prod it again.";
  - "There's already a sheep like this in the pen. You don't need to fetch another one now.";
  - "Someone else is prodding this sheep. Please find another one." ([1324]).
- **Pen sheep.** Prodding one gives "The sheep is already in the enclosure. You don't need to prod it.", suit or not.
  - The feed needs the suit ([1828]).
  - The feed is never used up: one bag killed all four.
  - The poisoning sequence is: the box; its death seq `sheep_update_death`; three ticks later the private bones (despawn 200), varbit 2, the untradeable-drop line and the second box.
- **The bleat** fires every 20-39 ticks (117 recorded gaps; LostCity used 10-29) and now plays `sound_area sheepcall1`, range 5. The pen sheep never bleat.

## The gate (`sheepherder_gate.rs2`)

All seven recorded crossings use one frame:
- both leaves become an inviswall for 2 ticks;
- `plaguesheep_gatel_open` stands at 2595,3362 and `plaguesheep_gater_open` at 2596,3362, both rotation 1;
- `gate_open` plays;
- the player steps one tile across the clicked leaf's row.

Going in needs the whole suit ([524], [530]); coming out never does ([582]). LostCity teleported the player with no frame.

## The incinerator (`sheepherder_furnace.rs2`)

On arrival: "You put the remains into the furnace.", `human_fai_furnace`, the bones removed, the `furnace` synth. Three ticks later: varbit 6 and "The remains burn to dust." LostCity used `human_furnace` and a 4-tick delay.

## Journal

Every recorded open matches row for row in `test/quests/_journals.lua`:
- [310] stage 1;
- [414] and [1016]: killed but the bones dropped, which reads as not yet herded;
- [1033] killed with the bones held;
- [1483];
- [1885];
- [2059] all four burned.

The overview start text for Sheep Herder, Heroes' Quest and Regicide (4685 [42], [76], [90]) is now in `quest_overview_start.rs2`. The completed journal is not recorded (UNVERIFIED).

## Checks

- `test/quests/sheepherder.lua` follows the recorded pages. Its herd planner was changed for 4-tile pushes:
  - a prod can't change x or z mod 4;
  - the gate and the pen's west wall stop an east push at x 2594;
  - a sheep on a row that can't reach them is left to wander.

  Result: PASS, 2,038 ticks. Prods per colour: 41, 10, 64, 118.
- `test/quests/_journals.lua`: PASS, including the seven Sheep Herder states.
- The `::sheepherderrun` debug walk used by the C selftest: OK.
- `test/quests/crest.lua`: PASS (a regression check on the scriptrun fix below).

**Scriptrun fix.** `pick_npc` and the draw-distance checks read an npc's `grid_position`. The headless client never advances that, so after a long herd presses aimed at the tile where the npc was first placed. They now read the route head, the same field `d_npcs` uses (`torirs_server_scriptrun.c`, `npc_world_tile`). The rendered client is not affected.

## Open

- How long another player's prod claims a sheep is not on the wire. It is set to 8 ticks, the combat claim's length (UNVERIFIED).
- The one-tile npc steps at the jump's C+1 and C+3 in two of four jumps are not modelled.
- In the recording the "Untradeable drop" line goes out after the mesbox's echo; ours goes out before it, in the same tick.
- `runclientscript 7192` after each bones drop (npc, a running id, obj, 1) is not sent. It looks like the loot tracker.
- The completed journal and every stage-0 and stage-3 line, where the recording and the wiki have none.
- Teleport boat to me: the spellbook button (218:82) has no binding, so it does nothing. 4685 [1813] got "You need to be near a dock or mooring point to use this spell." The index has 34 casts across 9 sessions (941, 945, 946, 957, 989, 380, 3437, 3828, 4264): the two refusals, plus successful casts that write `sailing_boat_selection_teleport_dock` (5006). Port it from those.
- Engine gap, already known (memory `hidden-multinpc-deleted-from-npc-info`): Jagex deletes a pen npc from npc info when its varbit hides it; ours keeps sending it, and the client hides it.
