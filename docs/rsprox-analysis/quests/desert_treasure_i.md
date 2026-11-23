# Desert Treasure I, rebuilt from RSProx

Rebuilt 2026-11-21 from the owner's playthrough, RSProx index 4621 to 4629 (rev 242, a normal
world, one recorder). Before this the quest was a 2009scape and wiki port that kept each diamond's
progress in private varps of its own. Every section now uses Jagex's variables with the recorded
values, on the recorded ticks.

One recording is one roll: every rate in the section documents is fitted to a handful of events.
Nothing here is from another recorder; no other session in the index holds any of the four bosses.

## Where each part is

| part | sessions | script (`quest_deserttreasure/scripts/`) | document | test (`test/quests/`) |
|---|---|---|---|---|
| start: the Archaeologist, Terry Balando, the bandit camp, Eblis, the mirrors | 4621, 4622 | `dt_start.rs2` | `desert_treasure_i_start.md` | `deserttreasure.lua` |
| smoke: the well, the torches, the chest, the gate | 4623, 4627 | `dt_smoke.rs2` | `desert_treasure_i_smoke.md` | `deserttreasure.lua` |
| Fareed | 4623 [1956..2120] | `dt_fareed.rs2` | `desert_treasure_i_fareed.md` | `_dt_fareed.lua` |
| shadow: Rasolo, the bandit chest, the ring, the dungeon | 4624 to 4627 | `dt_shadow.rs2` | `desert_treasure_i_shadow.md` | `deserttreasure.lua` |
| Damis, both forms | 4626 [746..1154] | `dt_damis.rs2` | `desert_treasure_i_damis.md` | `_dt_damis.lua` |
| ice: the troll child, the gate, the cold, the trolls, the path, the parents | 4628 | `dt_ice.rs2` | `desert_treasure_i_ice.md` | `deserttreasure.lua` |
| Kamil | 4628 [1507..1700] | `dt_kamil.rs2` | `desert_treasure_i_kamil.md` | `_dt_kamil.lua` |
| blood: Malak, Ruantun, the pot, Entrana, the tomb | 4628, 4629 | `dt_blood.rs2` | `desert_treasure_i_blood.md` | `deserttreasure.lua` |
| the Stranger (the assassin a diamond calls; added 2026-11-23, missed by the first rebuild) | 4628 [1680], [2962..3170]; 413; six ownerless sessions | `dt_stranger.rs2` | `desert_treasure_i_stranger.md` | `_dt_stranger.lua` |
| Malak's tithe scene | 4628 [3273..3283] | `dt_malak_scene.rs2` | below | `_dt_malak.lua` |
| Dessous, the unkillable one and the real one | 4629 [1482..1682], [3404] | `dt_dessous.rs2` | `desert_treasure_i_dessous.md` | `_dt_dessous.lua` |
| pillars, pyramid, Azzanadra, completion, Eblis afterwards | 4629 [4356..5922] | `dt_pyramid.rs2`, `dt_start_eblis_complete.rs2` | `desert_treasure_i_pyramid.md` | `_dt_pyramid.lua` |
| journal | 39 openings in all nine | `dt_journal.rs2` | below | `_dt_journal.lua` |
| saves from before the rebuild | none | `dt_migrate.rs2` | below | `_dt_migrate.lua` |

## The variables

`deserttreasure` (358): 1 [4621 661], 2 [965], 3 [986], 4 [1035], 5 [1339], 6 [1421], 7 [2654],
8 [4622 38], 9 [221], 10 [1174], 11 [1229] the hunts, 12 [4629 3490] all four diamonds held,
13 [4444] the pillars full, 14 [5419], 15 [5464] complete.

Each diamond's part is its own varbits (`fd_firechest`, `fd_killed_firewarrior`,
`fd_shadowwarrior_quest` 0..5, `fd_icewarrior_subquest` 0..5, `fdvw_subquest` 0..4 and the rest);
the section documents have the tick of every write.

## Scenes

| scene | recorded | ours |
|---|---|---|
| a look into each mirror | 4622 [1406..1470] | `dt_start.rs2` |
| Malak collects Roavar's tithe | 4628 [3275..3283] | `dt_malak_scene.rs2` |
| the troll parents freed (fade, snow) | 4628 [2733] | `dt_ice.rs2` |
| a pit trap in the pyramid (fade, tumble) | 4629 [4647], [4799] | `dt_pyramid.rs2` |

**Malak's scene.** A player at the hunts who steps into the Hair of the Dog is taken two ticks
later (in at [3273], the scene at [3275]): `fdvw_cutscene` 0 -> 1, the camera slides to 3495,3473
(height 500, rates 20 and 10) and looks at Roavar; the player turns to him and his page opens. On
each page the camera's look-at and the player's facing go to whoever speaks. A werewolf's hit ended
the recording's scene on its second page, so the pages from Roavar's second on are the wiki's
(Transcript:Desert Treasure I, "Entering the Hair of the Dog tavern"), written with neutral moods,
UNVERIFIED. After the last page the camera is reset and the server walks the player to Malak, whose
talk opens with nothing pressed. Checked on a headless run: one `cam_moveto`, fourteen
`cam_lookat`, one `cam_reset`, then "A human, eh?". The variant for a player who has finished The
Blood Moon Rises is not written (that quest is not in this tree).

## The unblessed pot

The first Dessous the recorder fought ([1482..1682]) is `blooddiamond_vampirewarrior_unkillable`
(3460): the pot poured on the tomb had not been blessed. Ours: an unblessed pot of blood, garlic
and spice raises that form and the pot comes back as it was. At 0 hitpoints he does not die: he
says "You will never defeat me", heals to full and fights on, every time; no diamond, no stage.
The line and the heal are the wiki's: the recorder left by teleport with him at 4 of 30 on the
bar, so no recording reaches 0. A blessed pot raises the real one. `_dt_dessous.lua` takes the
unkillable one to 0 twice and then kills the real one.

## The journal

Not in the cache; sent only when opened. The recorder opened it 39 times, at 24 distinct states.
`_dt_journal.lua` sets each state's varbits, opens the journal and compares every row as plain
text (the wraps): 24 of 24 match. No recording opens it after stage 13: stage 14 reads as 13 and
the completed journal is UNVERIFIED.

## Saves from before the rebuild

`dt_migrate.rs2` runs on login. If any of the old private varps (6607 blood, 6614 smoke, 6613 the
gate, 6618 ice, 6622 shadow) holds a value, the progress is carried to the varbits (only ever
raising them), the old varps are emptied, and an account at the old stage 10 moves to 11. What it
cannot do: the old numbering of the main stage below 10 shares its varbit with the new one and no
save says which it is, so such an account reads as the recorded stage of the same number, at most
two conversations back.

## Other content the recording exercised

- **Magic carpet, Travel (op 3) at the Shantay Pass**: was the Talk-to pages; now the recorded
  menu "Carpet rides cost 100 coins." (Uzer / Bedabin camp / Pollnivneach / Cancel) and the ride
  on the pick. The other stations' op 3 is not recorded and still runs their Talk-to.
- **The Ancient altar before the quest**: "This altar seems to do nothing...".
- **The completion scroll's close**: `quests/scripts/questscroll.rs2` calls the quest's own step.
- **Dialogue line breaks**: Jagex's own breaks inside a page ("A human, eh?|Give me one good
  reason...") had been dropped by the rewrite; 160 strings have them back and nine strings that
  ran over two recorded pages are two pages again.
- **The shadow dungeon is multi-combat** (`maps/multiway.csv`, the zones the recorder stood in).
- **The troll parents' ice blocks** no longer punch back (no sequence of any kind recorded).

## Tests (headless, the private server build)

`deserttreasure.lua` (the whole quest), on the committed tree, four rolls: 464 of 464, 447 of 447
twice, and 472 of 474. The two failing rows on that roll are survival margins (lowest hitpoints 16
at Dessous with no food left, 17 on the climb through the troll stronghold); the quest completed.
No pit falls on any roll: the driver walks to 2784,4954 before it presses the third floor's ladder.
Pressed from the landing, the press found no route and sent nothing, the character stood where its
walk ended, and a pit took it 27 times running on one roll. Damis's first form is attacked again
every 24 ticks: on one roll a single press left the character 300 ticks beside him without a kill.
`_dt_pyramid.lua` 67, `_dt_malak.lua` 6, `_dt_migrate.lua` 3, `_dt_journal.lua` 24,
`_dt_fareed.lua` 12, `_dt_damis.lua` 8, `_dt_dessous.lua` 21, `_dt_kamil.lua` 12: all passing.
`_dt_stranger.lua` 11 on seven rolls, `_dt_mummies.lua` 9 on six (2026-11-23), and `deserttreasure.lua` 447/447 again with
the Stranger's attempts wired in (one roll).
`_dt_kamil.lua` and `_dt_pyramid.lua` were each run on three more rolls after two test faults were
fixed: a lost press on Protect from Melee (Kamil then hit through the wrong prayer and the
character died) and the third floor's ladder pressed from too far, as in the whole-quest test.

Not run: the live client. Every check above is the headless server.

## Open

Per section in each document. Across the quest:

- **The pyramid's pit traps** open only under a standing player (owner's call, 2026-11-21): 1 in
  45 on each tick stood still on the second and third floors down. Two recorded pits and the wiki's
  "don't stop" are the whole basis; the 1 in 45 is two pits in 91 standing ticks.
- **Rates fitted to one recording**: the pyramid's mummies and scarabs, Fareed's one-in-five magic,
  Kamil's one-in-three cast in reach, Damis's taunts.
- **Wiki only, UNVERIFIED**: the unkillable Dessous's line and heal; Fareed's 37/40 maximum and the
  ice gloves stopping his disarm; Kamil's and Damis's unprayed maximums; the Magic requirement's
  two lines at the pillars; most of Malak's scene; Eblis after the staff is bought.
- **Engine, every boss**: an npc set on a player by a script sends its face a tick late and one
  face block too many; ours swings the tick after arriving where Jagex moves and swings together.
- **Not written anywhere**: `last_quest_id` (16741); the loot tracker's merged line; the
  `last_npc_elemental_weakness` varp on a fire or water spell.
- **Leaving or dying mid-fight** is unrecorded for Fareed, Kamil and Damis.
- **The mummies' and scarabs' own combat** was not audited (the recorder prayed melee; every hit
  read 0).
- `ancient_sceptre.rs2` (Eblis and the ancient icon) is invented text.
- The start section's npcs' facing and wander were not diffed block by block.
- **The pyramid's mummies**: their adds were fixed 2026-11-23 ("Rawr!", the face, the emerge seq on
  a rotation-1 lid; `_dt_mummies.lua`), but the T+1 face reset on the glide does not reach the
  wire (engine: `npc_face_settle`), and the rest of their npc info and their combat were never
  diffed. The world-spawned `deserttreasure_mummy_2..5` (211 adds in 4623 and 4629) run on no
  script of ours.
- **The Stranger's** chance per attempt, boss-area radii and poison: `desert_treasure_i_stranger.md`.

### Missed-content audit (2026-11-23, after the Stranger)

Run over 4621..4629 with the rsprox-content skill's step 1b plus two sweeps: every quest var the
sessions write (37 names: all written by ours) and every game message whose text is in none of
our files (42; most are generic or built at run time).

- **Fixed:**
  - The pyramid's back door before completion: "This passage doesn't seem to lead anywhere..."
    (4621 [2399]; the old comment said nothing was recorded).
  - The Ancient altar's two lines, which ours had invented: "You feel a strange wisdom fill your
    mind..." (3000 [2168], 3256 [1070]) and "You feel a strange drain upon your memory..."
    (4629 [5677] and three more).
  - The freeze refusal (generic magic): "Your target is currently immune to that spell." (4626
    [770], Snare on Damis; 235 [3379]) replaced the unrecorded "That target is already frozen.".
    The 5-tick immunity after a bind ends: tracked since 2026-11-23 (the Engine row below).

  Test: `_dt_lines.lua` (5 rows, two rolls).
- **The Frozen Door is invented end to end.** DT1's completion starts it, and ours drops the
  letter in at login with a made-up line, a made-up letter and a private varp
  (`varp7278_gwd_frozen_door_state`).
  - Jagex: a `nex_messenger` walks up 465 ticks after completion, once the player is idle (4629
    [5929]..[5941]; 3256), says "I have a letter for you. I'm told it's quite important. Please
    read it when you get a chance.", gives `nex_message`, then objbox "The messenger has given you
    a letter.", and walks off.
  - `frozen_door` (varbit 13175) goes 0 -> 1 with the delivery and 1 -> 2 on the read, which
    opens `longscroll` (625) with the real text (3256 [146]).
  - The rest of the miniquest is recorded too: 3262 (2 -> 4) and 800 (4 -> 10).
- **The mummies' fight: built 2026-11-23** (`dt_mummies.rs2`, `_dt_mummy_fight.lua`;
  `desert_treasure_i_pyramid.md` "The mummies' fight"): swing and block anims, the four sounds,
  fire and water, the turn to ash, the drop table, the sarcophagus mummy's rise. Damage, wander
  and hunt ranges are still the generated ones (nothing to fit them from).
- **Leftovers: deleted 2026-11-23**, the 56 constants nothing read (the old port's stages and
  stand tiles, a duplicate cave tile, the torch roll) and the comments that described them.
- **Engine: fixed 2026-11-23.** `npc_facereset` over a standing coord face now reaches the wire (the
  mummy glide's reset, 5/5); binding spells refuse a cast in the 5-tick immunity after a freeze
  (new opcode `npc_freeze_immune`; `test/magic/bind_immunity.lua`).

- Two other quest tests press the Shantay carpet's Travel and were updated to the new menu.
  `icthlarin.lua`: its carpet rows pass (its one failing row, `finish-contact-no`, is elsewhere).
  `ratcatchers.lua` fails 29 rows from its second on (a scroll's box kind, the cat, Shantay's
  pages), never gets through the pass, and so never reaches the rug merchant: unjudged.
