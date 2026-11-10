# Big Chompy Bird Hunting, and the rest of RSProx index 4503

Source: index 4503 (rev 242.1, w470, 2026-11-09), a full playthrough, tick 3543 to 6246.
Quest tick numbers in `[ ]` are that session's. The first 3,500 ticks are generic (farming,
GE, cooking, Keldagrim) and are covered in "Everything else in 4503". Keldagrim and the Giant
Dwarf are the Giant Dwarf port's (index 4488..4502).

Tests: `test/quests/chompybird.lua` 177/177, `test/quests/_journals.lua` 137/137,
`test/farming/gardener_pay.lua` 29/29.

## Stages (varp 293)

The values already matched. What moved is when each is written.

| value | recorded | write |
|---|---|---|
| 5 | [3636] | on the click of "OK, I'll make you some 'stabbers'.", with the started line |
| 10 | [4854] | with "Rantz takes six ogre arrows off you." |
| 15 | [4940] | on the click of "How do we make the chompys come?"'s answer |
| 20 | [5031] | the rock lifted |
| 25 | [5149] | before the second box ("He nods with approval") |
| 30 | [5212] | a tick after the toad goes down |
| 35 | [5289] | bite + 2 (was: when the bird spawned) |
| 40 | [5290] | bite + 3, with "Rantz keeps missing" and his overhead |
| 45 | [5337] | the bow handed over |
| 50 | [5622] | the tick the bird turns into a carcass (was a queue, a tick late) |
| 55 | [5738] | before "Okay's now you's needs to cook" |
| 60 | [6183] | "Eventually the chompy is cooked" |
| 65 | [6222] | the "It's my pleasure!" click |

## What changed against the 2004 port

| section | recorded | was |
|---|---|---|
| start | one straight talk, then "Start the Big Chompy Bird Hunting quest?" Yes/No | a looping 4-option menu with lines OSRS no longer has |
| requirement box | wiki transcript's line when a skill is low | absent |
| cave | lands 2647,9378 and 2629,2997 (all crossings); both exits say the daylight line | 2647,9379 and 2630,2997; one exit silent |
| chest fail | the two lines are one message | two messages |
| chest open | `human_openchest`, take panel (mode 23, max 10), "You take a pair of bellows." | no menu, 2004 line |
| Suck | all on one tick: line, seq and spotanim at 30, synth at 30 | seq only, two ticks later, no sound |
| swamp ops | `oploc1 swampbubbles` [5517] and `opnpc1 toad` [5522] act with held bellows | unbound |
| inflate | seq stops on the op, catch next tick, toad gone 4 ticks later, synth `ogre_bellows` | same tick, silent |
| toads | `timer`: Ribbit/Croak and `toad_croak` r5, gaps 76..99 | the timer never ran (46 recorded, 0 ours) |
| bait | line + seq + toad on one tick, the stage a tick later | the stage first, the line after the toad |
| burst | `toad_burst` r5 with the map anim | silent |
| bird | walks to ITS bait, eats in 2-tick beats | nearest toad, all on one tick |
| Rantz | says the line at the bite's start, shoots on the bite (`ogre_longbow`, launch 96, arrow projectile, `ogre_bow` r10) | a timer after the spawn, invented message |
| bird hit | defend seq delayed to the landing, once per shot | sent twice, one undelayed |
| bird fight | never swings back | retaliated (16 attack seqs) |
| kill | stays in its slot: seq, transformation, level 0, area sound r7 | removed and a new npc added |
| hint arrow | on the owner's bird from stage 45, off at the kill | none |
| spit | varbit 1770 0→1→2→0, `human_cooking` x2, `spit_roast` | `loc_change` and pottery seq |
| recipe line | "onion, tomato and the cabbage" (Rantz, Fycie, Bugs) | Rantz, Bugs, Fycie |
| ogre arrows | Jagex's make-menus and batches at 2-tick steps | single instant crafts |
| scroll | 1,470 Cooking / 735 Ranged / 262 Fletching / An Ogre Bow / Ability to make Ogre Arrows | other order and wording |

The varp under the spit's varbit (`varp665_ogre_area_extra`) was `transmit=no`; it is `yes` now.

## Journal

`test/quests/_journals.lua` compares every row against 4503 for stages 5, 15, 25, 40, 45, 50, 55
and 60 (chompy held). Stage 60 with the chompy dropped reads as stage 55 ([6191]).

UNVERIFIED, no recording opens them: stages 10, 30, 35, stage 50 without the carcass, and the
completed journal (LostCity's text).

The not-started overview's string is in `quest_overview_start.rs2`.

## Npc blocks (`npc_block_inventory.py` on the final run)

| kind | recorded | ours | verdict |
|---|---|---|---|
| bird `say`, `transformation`, `sequence` | 7, 2, 17 | 6, 1, 9 | the test kills one bird; the recorder killed two. Screech needs the player inside 2 tiles, not exercised |
| Rantz `say`, `spotanim`, `sequence` | 2, 1, 1 | 2, 1, 1 | matched |
| toad `say` | 46 | present | fixed |
| bait `say` | 2 | 2 | matched |
| `facing` on Rantz, Bugs, Fycie | 28, 10, 8 | fewer | the test holds fewer conversations |
| `enabled_ops` on the bird and on toad adds | 2, 2 | 0 | all ops stay on; our engine sends only a change |
| `level_change` | 2 | 0 in the tool | `npc_setlevel(0)` is in the script; the tick log has no such row, so this is not confirmed on the wire |

Not compared: the stage's first bird arrival gives 1 tick between arriving and "Sqwark!" where
the later bird gives 3. Ours uses the later shape.

## Open on the quest

- The clearing's hint arrow (from stage 25) stays 2 ticks longer in ours: Jagex clears it as the
  player reaches the clearing ([5209]); ours clears it when the toad goes down.
- Rantz's aim tile (2 east, 4 south of the bird) is one roll.
- The chest's revert time: no re-add is visible in the recording; 50 ticks is unverified.
- The quest chompy's burn chance: 1 success recorded, the wiki doesn't know.
- Hat talk and the off-quest cook have no recording; the cook uses the quest's frames.
- "Thanks." followed by "No thanks..." on rebuying the bow is the wiki's transcript as written.
- A raw chompy on a fire ("This is too big...", wiki) is not bound.

## Everything else in 4503

| action | verdict |
|---|---|
| Keldagrim door, stairs, Santiri, dwarves, rowdy dwarf | Giant Dwarf port's |
| tablets (Varrock, Falador, Ardougne), lunar Catherby tablet | bound; shape covered by `test/magic/teleport_tablets.lua` |
| ring of dueling, games necklace | bound; `test/magic/jewellery_teleports.lua` |
| banks, booths, chests, tabs, placeholders, notes, deposit | bound, generic bank interface |
| GE clerk, offers, collecting a slot | bound; `test/ge` |
| GE booth Exchange (op1), booth Collect (op3) | **added**; Collect tested, Exchange compiled only |
| tree and fruit patches: plant, check health | bound. Plant line and tick corrected from [242], [243] |
| maple, yew, magic trees | **added**: only oak and willow existed. Maple's plant value is recorded; yew and magic are RuneLite's |
| Gnome Stronghold tree patch | **added**: patch 5 had no state, and every op on it did nothing |
| gardener Pay | **added**: chop, protect, confirm, every refusal, pooled from the archive |
| spirit tree Travel menu and Last-destination | **added**; recorded tiles replace two wrong ones |
| Rogues' Den fire Cook op | **added** |
| Gnormadium Avlafrim before the quest | **added** |
| Rick Turpentine, Elkoy's Follow, the railing, Captain Barnaby, the plank | bound |
| picks, drops, equips, potions, bones, wolves, achey trees | bound or engine default |
| `port_task_master_brimhaven` | no binding; Sailing isn't in this tree, and the recording shows no answer to the clicks |

Payment amounts for yew, apple, pineapple and papaya have no recording or checked source and are
UNVERIFIED; the other eight are recorded. Dragonfruit has no patch row, so no payment.

Exercised by a test: the tree-patch Pay branches at Varrock and the Gnome Stronghold, one spirit
tree trip, Collect, the fire's Cook and Gnormadium. Not run: the fruit-tree Pay branch, a yew or
magic tree, Last-destination, the GE booth's Exchange op.

Only spirit tree destinations 0..4 are offered. The rest of Jagex's list is shown grey and refuses,
which matches a player with none of those unlocked.

Locked-row behaviour and gardeners other than tree and fruit patches (allotments, hops, bushes,
spirit trees) are not built.
