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
| 6e | F2P quest: Romeo & Juliet | done | Romeo/Juliet/Lawrence/Apothecary + journal; dbrow quest_romeoandjuliet |
| 6f | F2P quest: VampireSlayer | done | Morgan+Harlow(vis)+coffin+garlic cupboard+stake kill; garlic weaken/regen deferred |
| 6g | F2P quest: Monk's Friend (drunkmonk) | done | Omad+Cedric+journal+ladder timer; party balloons deferred |
| 6h | F2P quest: Goblin Diplomacy | done | bartender+generals+dye mail+journal; %gobdip_main; dragon/clue deferred |
| 7a | skill_woodcutting | done | woodcutting_trees server table; cat 189=tree; cache WC cols empty; axe anim switch |
| 7b | skill_mining | done | mining_table + rock cats; clay..runite; prospect; essence/gem deferred |
| 7c | skill_firemaking | done | cat 22=firemaking_logs; p_opobj wired; FM level/XP switch; light sources deferred |
| 7d | skill_fishing | done | salt/fresh/rare F2P spots; p_opnpc non-2 re-issue; XP/rate switches; movement/macros deferred |
| 7e | skill_cooking | done | cooking_generic table + F2P meat/fish/bread/pies; cat 687=cooking_oven; dough/gnome deferred |
| 7f | skill_crafting (remainder) | done | pottery+gems+F2P leather; spinning already in 3; jewellery/glass/guild deferred |
| 7g | skill_smithing | done | smelting+anvil F2P; cats 215/772/151; p_choice menus; cannonballs/dragon sq/claws deferred |
| 8k | skill_runecraft | done | F2P air..body + members cosmic..death; essence mine + Aubury/Sedridor tele; cats 2156/8200/8201; deferred: soul/blood, Ourania/zeah, tiaras, projanim_pl, int loc_param(rune_type), Aubury shop |
| 8a | F2P quest: The Restless Ghost | done | Aereck/Urhney/ghostx + coffin/skull; dbrow quest_restlessghost; open=openghostcoffin_* |
| 8b | F2P quest: Ernest the Chicken | done | Veronica/Oddenstein + manor/levers/doors via ernestlever/ernestdoors; quest_ernestthechicken |
| 8c | F2P quest: Prince Ali Rescue | done | Hassan/Osman/Leela/Keli/Joe/Ali + Ned rope/wig + Aggie paste; quest_princealirescue |
| 8d | F2P quest: Demon Slayer | done | Aris/Prysin/Rovin/Traiborn/Oracle + drain/Delrith; quest_demonslayer; clue/DS map deferred |
| 8e | F2P quest: Black Knights' Fortress | done | Amik + fortress doors/grill/cabbage; quest_blackknightsfortress; door swing deferred |
| 8f | F2P quest: Shield of Arrav | done | Both gang paths + Reldo/tramp/Roald/curator; quest_shieldofarrav; book UI + chest Close ops deferred |
| 8g | F2P quest: Pirate's Treasure | done | Frank/Luthas/Wydin + crate/chest/dig; quest_piratestreasure; customs search deferred |
| 8h | F2P quest: The Knight's Sword | done | Squire/Thurgo/Vyvin + cupboard/blurite mine; quest_knightssword |
| 8i | F2P quest: Dragon Slayer (core) | done | Guild/Oziach/Klarense/Ned/Wormbrain/ship/map; Melzar maze + Elvarg deferred |
| 8j | F2P quest: Dragon Slayer (Melzar/Elvarg) | done | Maze keys/chest, oracle magic door+chest, Elvarg complete, secret wall; fire-breath + authored locs deferred |
| 8l | Aggie dyes (Draynor) | done | red/yellow/blue dye + fine + opnpcu; skin paste kept |
| 8m | Wyson woad leaves (Falador) | done | 15/20gp woadleaf purchase dialogue |
| 8n | skill_thieving (stalls/pickpocket) | done | pickpocket.dbrow + stealing.dbrow (*thiefstall remap); thieving.rs2 helpers; deferred: chests/doors, viking/misc stalls, guard2, npc_retaliate |
| 8o | skill_thieving (chests/doors) | done | trapped_chest + locked_door dbrows/scripts; loc remap trapchest*/picklock*/toollock*; walk-through (open_and_close deferred); viking/misc stalls still deferred |
| 8p | skill_magic F2P core | done | helpers+staff/spells tables; teleport/alchemy/telegrab/enchant1-4/superheat; magic_spellbook IF remap; deferred: charge orb/charge/bones, enchant5, trollheim, Ancient/Lunar, oc_cost/oc_members runtime |
| 8q | skill_combat F2P magic | done | player_magic F2P strike→wave+debuffs/bind; magic_combat_spells.dbrow; table combat cols; deferred: autocast IF, PvP, god/iban/crumble, freeze varn, npc_statsub, projanim_npc |
| 8r | skill_fletching | done | F2P knife/logs/shafts/arrows/stringing; cat 22/968; p_choice menus; deferred maple+/mithril+/darts/bolts/crossbow |
| 8s | skill_agility (Gnome Stronghold) | done | helpers + gnome course; climbing_branch/obstical_pipe3_*; bas stubs; deferred barb/wild/rooftops/arena/shortcuts |
| 8t | skill_herblore | done | clean/brew/grind dbtables; cat 773/69; members gate; deferred decant, snails, mort/eadgar/ogre, extreme/raids/barb/tar, huasca |
| 8 | Outward areas / remaining quests / minigames | pending | Next: outward areas (shops blocked on inv.ini) |


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
- slice 6e done: Romeo & Juliet (Romeo/Juliet/Lawrence/Apothecary + journal + quest_romeoandjuliet)
- slice 6f done: Vampire Slayer (Morgan/Harlow_vis/coffin/garlic cupboard/stake finish); garlic weaken+regen deferred
- slice 6g done: Monk's Friend (Omad/Cedric + journal + quest_monksfriend); party balloons deferred; blanket ladder timer
- slice 6h done: Goblin Diplomacy (bartender start + Wartface/Bentnoze + dye mail + journal); %gobdip_main on carrier; dragon/clue deferred
- slice 7a done: woodcutting (woodcutting_trees + tree cat/overlays + chop loop); cache woodcutting_* has columns=0 so server table; axe anim switch; p_oploc(1) resume
- slice 7b done: mining (mining_table + normal/fast rock cats + prospect); clay..runite; essence/gem/blurite deferred
- slice 7c done: firemaking (cat 22=firemaking_logs + Light/tinderbox); p_opobj + lineofwalk engine; FM level/XP switches; ashes via world_delay+obj_add; light sources/achey deferred
- slice 7d done: fishing (saltfish/freshfish/rarefish); p_opnpc non-Attack re-issues interaction; fish XP/rate/equip switches; movement/macros/member deferred
- slice 7e done: cooking (cooking_generic + F2P meat/fish/bread/cake/pizza/pies); 687=cooking_oven; cook-o-matic + gauntlets; dough/wine/gnome deferred
- slice 7f done: crafting remainder (pottery softclay/wheel/oven; gem cutting table; F2P leather via p_choice); jewellery/glass/dragonhide/guild deferred
- slice 8a done: Restless Ghost (Aereck/Urhney/ghostx + shutghostcoffin↔openghostcoffin_*; skull; journal quest_restlessghost); tower altar multiloc + npc_retaliate deferred
- slice 8b done: Ernest the Chicken (Veronica/Oddenstein + compost/fountain/closet/levers; %ernestdoors multilocs; journal quest_ernestthechicken); double-door open_and_close deferred; fountain poison server varp
- slice 8c done: Prince Ali Rescue (Hassan/Osman/Leela/Keli/Joe/Ali + Ned rope/wig + Aggie paste; journal quest_princealirescue); Aggie dyes + Ned Dragon Slayer + metal gate helper deferred
- slice 8d done: Demon Slayer (Aris/Prysin/Rovin/Traiborn + drain key + Delrith incantation; journal quest_demonslayer); Oracle clues + Dragon Slayer map piece deferred
- slice 8e done: Black Knights' Fortress (Sir Amik + fortress doors/grill/cabbage sabotage; journal quest_blackknightsfortress); open_and_close_door swing + inacbk Open op deferred
- slice 8f done: Shield of Arrav (Phoenix + Black Arm paths; Reldo/tramppg/Baraek/Straven/Katrine/Curator/Roald; journal quest_shieldofarrav); book UI + open-chest Search/Close ops deferred
- slice 8g done: Pirate's Treasure (Frank/Luthas/Wydin + banana crate/chest/Falador dig; journal quest_piratestreasure); customs officer rum search deferred
- slice 8h done: The Knight's Sword (Squire/Thurgo/Vyvin + portrait cupboard + blurite rocks; journal quest_knightssword); trail clue on squire deferred
- slice 8i done: Dragon Slayer core (Champions' Guild, Oziach, Klarense/ship repair, Ned, Wormbrain, map assemble, Duke shield, Oracle; journal quest_dragonslayer1); Melzar's Maze + Elvarg fight deferred
- slice 8j done: Melzar maze (keyed doors + *_1_key drops + funchest mappart1), oracle door as dragon_slayer_qip_magic_door + mappart3 chest, Elvarg/elvarg_alive → dragon_complete, dragonsecretdoor; deferred: fire-breath AI, Melzar combat spells, crandor_rock/rope/elvarg_gate (authored absent)
- slice 7g done: skill_smithing (smelting table + furnace cat 215; anvil cat 772 + smithing_bar 151; F2P bars/products via p_choice; dorics_anvil gate); deferred cannonballs, dragon sq, claws/darts/wire/studs, jewellery furnace redirects, CS2 smithing.if
- slice 8k done: skill_runecraft (runecraft_table + F2P air..body + members cosmic..death; rc_ruins 8200 / rc_exit_portal 8201; essence mine enter/exit; Aubury+Sedridor tele wired); deferred soul/blood, Ourania/zeah, tiaras, projanim_pl projectile, int loc_param(rune_type), Aubury shop, Brimstail/Disentor/Cromperty
- slice 8l done: Aggie dyes (reddye/yellowdye/bluedye + insult fine + onion/redberries/woadleaf use); skin paste path unchanged
- slice 8m done: Wyson the gardener woad leaf purchase (15gp×1 / 20gp×2)
- slice 8n done: skill_thieving stalls/pickpocket (dbrows + thieving.rs2 helpers + stealing.rs2; loc remap *thiefstall; %thieving_stall_timer server varp); deferred: trapped_chest/locked_door, viking/misc/etc stalls, viking pickpocket, guard2, ~npc_retaliate
- slice 8o done: skill_thieving chests/doors (trapped_chest.rs2 + locked_door.rs2 + dbrows; trapchest1..5/pickchest3/emptypickchest/inacopenchest + picklock*/toollock* remap; door swing → walk-through; damage via hitsplat); deferred: open_and_close_door swing, viking/misc stalls
- slice 8p done: skill_magic F2P core (magic.rs2 helpers + magic_spell/staff tables; teleport Varrock/Lumby/Fally + Camelot/Ardougne/Watchtower; low/high alch; telegrab; enchant 1-4; superheat via smelting table; IF → magic_spellbook:*); deferred: charge orb/charge/bones convert, enchant5, trollheim, Ancient/Lunar, oc_cost+oc_members engine gaps
- slice 8q done: skill_combat F2P magic (player_magic.rs2 apnpct on magic_spellbook; magic_combat_spells.dbrow strike→wave + confuse/weaken/curse/bind; combat columns on magic_spell_table; magic XP in give_combat_experience; ai_queue2 delayed hits); deferred: auto_cast (no staff_spells/combat_staff_2), PvP, god/iban/crumble, freeze (%npc_stunned varn), npc_statsub, projanim_npc, p_opnpct resume
- slice 8r done: skill_fletching F2P (fletch_bow_table + fletching_table; cut_logs/arrows/bows; 968=arrowheads; p_choice; members gate); deferred maple+/mithril+/darts/bolts/crossbow/ogre
- slice 8s done: skill_agility Gnome Stronghold (agility.rs2 helpers + gnome_course.rs2; climbing_branch / obstical_pipe3_1/2; %gnome_course_progress + pipe timers; bas no-ops); deferred barbarian/wilderness/rooftops/arena/pyramid/shortcuts; fail helpers unused by gnome
- slice 8t done: skill_herblore (herblore_clean/brew/grind tables; identify+brew+grind+empty; cat 773=grimy_herb / 69=potion; members gate); deferred decanting, snail grind, mort serum/eadgar/ogre quest mixes, extreme/raids/barbarian/tar, huasca
- next pending: outward areas / remaining quests / minigames (shops blocked on inv.ini)
