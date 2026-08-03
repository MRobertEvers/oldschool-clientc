# Content port queue

Agent-loop state for the LostCity → OSRS-Content forward port.
Each tick ports **one** pending unblocked slice per `docs/PORTING_GUIDE.md` §4.
Status: `pending` | `in_progress` | `done` | `blocked`.

Loop prompt: read this file + PORTING_GUIDE §4; port the next pending unblocked
slice; verify (`mock230_pack --check-only`, scripts build); update this file;
re-arm. Stop only when the user stops the loop.

| # | Slice | Status | Notes |
|---|---|---|---|
| 0 | Queue tracker | done | This file |
| 1 | Lumbridge NPCs: Fred, Duke Horacio, Father Urhney | done | Fred+Duke+Urhney; sheep varp/constants for Fred; 0 pack errors |
| 2 | Near-spawn general_use (water, gates, haybales, pickables, crates/drawers) | done | haybales/pickables/water/crates; gates+drawers deferred (orphan cats) |
| 3 | Sheep Shearer deps: shear_sheep + spinning wheel | done | shear variants + spinningwheel by name |
| 4 | Sheep Shearer quest + journal | done | journal + quest_complete dbrow; Fred dialogue in slice 1 |
| 5a | general_use batch: barrels, bookcases, chests, coffins, cupboards | done | barrels/bookcases/coffins; chests+cupboards deferred (orphan cats) |
| 5b | general_use batch: drawers, fence, findsomethingnice, gangplank, hammer | done | sacks/manholes/hammer/spade; drawers/fence/findsomethingnice/gangplank deferred |
| 5c | general_use batch: hat_stand, locked_doors, locked_gates, manholes, mithril_seeds | done | hatstand/lockeddoor1/metal gates/mithril seeds; manholes already in 5b |
| 5d | general_use batch: newcomer_map, organs, sacks, spade, tables, trapdoors, wardrobes, web, windmills | done | tables/trapdoors/wardrobes/web/windmills/organs; sacks+spade already 5b; newcomer_map deferred (playermap_east + newcomers_pos) |
| 6a | F2P quest: Rune Mysteries | done | Duke+Sedridor+Aubury+journal; essence teleport/shop stubbed |
| 6b | F2P quest: Imp Catcher | done | Mizgog + journal + quest_impcatcher; beads already on imp drop table |
| 6c | F2P quest: Doric's Quest | done | full dialogue + journal + quest_dorics |
| 6d | F2P quest: Witch's Potion (Hetty) | done | Hetty+cauldron+journal; rats_tail drop gate enabled |
| 6e | F2P quest: Romeo & Juliet | pending | |
| 6f | F2P quest: VampireSlayer | pending | |
| 6g | F2P quest: Monk's Friend (drunkmonk) | pending | |
| 6h | F2P quest: Goblin Diplomacy | pending | |
| 7a | skill_woodcutting | pending | Prefer cache `woodcutting_*` dbtables |
| 7b | skill_mining | pending | |
| 7c | skill_firemaking | pending | |
| 7d | skill_fishing | pending | |
| 7e | skill_cooking | pending | |
| 7f | skill_crafting (remainder) | pending | Spinning may land in 3 |
| 8 | Outward areas / remaining quests / minigames | pending | Tail |

## Log

- queue created
- slice 1 done: Fred / Duke / Urhney + sheep varp/constants + sheep_complete queue; scripts compile; mock230_pack 0 errors
- slice 2 done: haybales, pickables, water fill, crates (gates/drawers deferred — orphan loc categories)
- slice 3 done: shear_sheep (all colour variants) + spinningwheel wool/flax
- slice 4 done: sheep_journal wired to quest_sheepshearer
- slice 5a done: barrels, bookcases, coffins (chests/cupboards need loc category allocation)
- slice 5b done: sacks, manholes, hammer, spade (drawers/fence/findsomethingnice/gangplank deferred)
- next pending: 5c / 5d / 6a Rune Mysteries
- loop armed: AGENT_LOOP_WAKE_content_port every ~180s
- slice 6c done: Doric's Quest (ahead of 5c/6a — small self-contained F2P)
- slice 5c done: hatstand, lockeddoor1, lockedmetalgate l/r, mithril_seeds (simplified plant)
- slice 5d done: tables (cat+name expand), trapdoors, wardrobes, web+slash_checker, windmills (%mill_flour + hopper_full), organs mes-stub; newcomer_map deferred
- slice 6a done: Rune Mysteries (Duke/Sedridor/Aubury + journal + quest_runemysteries); essence teleport + Aubury shop stubbed
- slice 6b done: Imp Catcher (Mizgog + journal + quest_impcatcher); ^chat_laugh added
- slice 6d done: Witch's Potion (Hetty + cauldron + journal + quest_witchspotion); rats_tail drop gated
- next pending: 6e Romeo & Juliet
