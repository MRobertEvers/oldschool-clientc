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
| 5d | general_use batch: newcomer_map, organs, sacks, spade, tables, trapdoors, wardrobes, web, windmills | done | tables/trapdoors/wardrobes/web/windmills/organs; sacks+spade already 5b; newcomer_map → 9m |
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
| 8u | skill_combat F2P ranged | done | player_ranged via player_melee_swing; ranged_ammo_table; cats 62/63; ranged XP/maxhit; deferred PvP/crossbow/thrown/specs/poison/ammo drop |
| 8v | Varrock NPCs (shops + kids + bartenders) | done | Lowe/Thessalia/Zaff/Horvik/swordshop/tea/Scavvo/Valaine/tailorp + kids stub + Gertrude stub + bartenders + sworddummy; shops Trade-stubbed; deferred: shop open, Gertrude's Cat, Thessalia makeover IF1, east_gate, barcrawl/trails |
| 8w | general_use chests/cupboards/drawers | done | Name-bound F2P shut↔open pairs + empty search; deferred: orphan cats, trails, findsomethingnice, locked chests, members/quest loot |
| 8x | Falador NPCs (shops + mining guild + hair/makeover) | done | barmaid/cassie/drogo/flynn/herquin/wayne/nurmof + mining_guild + hairdresser/makeover stubs + goblin_armed; Trade-stubbed; deferred: shops, barcrawl, IF1 kits, pickaxe repair, guard2 |
| 8y | Port Sarim NPCs (shops + sailors + monks) | done | betty/brian/grum/gerrant + sailors (*_1op/*_2op) + shipmonk + dock ladder + rommik; Trade-stubbed; Karamja telejump; deferred: openshop, set_sail IF, Entrana search cats, clues, Heroes eel |
| 8z | Al Kharid + remaining Draynor NPCs | done | zeke/dommik/louie/ranael/gem/silk/kebab + tanner (p_choice) + toll gate (kharidmetalgate*) + curtains; diango/jailguard/manor_vines; Trade-stubbed shops; deferred: shantay*, warrior/witch AI, openshop, crest/trails, duel arena, IF1 tanner |
| 9a | Rimmington done + Edgeville dungeon + Karamja F2P ferry/trees + general shops | done | Rimmington LC already complete (rommik/hetty); edgeville_dungeon brasskey+oddwall walk-through; customs_officer rum search+telejump; banana trees+plantation.loc; F2P generalshop 2..7 Trade stubs; deferred: openshop, set_sail IF, door swing, zambo (absent), jiminua/shanks/members |
| 9b | Barbarian Village (F2P) | done | peksa/gunthor/barbarian(+fai_*) + beer barrel beatdown; Trade-stubbed; spinning already in 3; drop table fai expand; deferred: openshop, scorpcatcher arm, barbarian_woman, outpost/agility/fishing/herblore, fai_barbarian_barrel ops, Hunding |
| 9c | F2P quest: Gertrude's Cat (quest_fluffs) | done | Gertrude+kids+Fluffs+fence+journal quest_gertrudescat; %fluffs=180; fluffs_crate+fence timer server varps; deferred: pet.rs2 growth/follower, trail clues, lostkitten cinematic (.npc_*), chompy doogle use |
| 9d | general_use gates/fence/gangplank (+findsomethingnice) | done | fencegate/metalgate via doors; farming/rustic pairs; memberfencegate walk-through; mournerstewfence; ship planks (not dragonship*); findsomethingnice+wire; deferred: orphan gate_main_*, board_message param, duel arena, ~open_and_close_metal_gate |
| 9e | Monastery (+ Varrock palace plaque) | done | Varrock leftovers thin (east_gate biohazard); Abbot/Jered/monk Talk-to + ladder guild gate + monks_altar +2; %prayer_guild authored; plaque_zamorak_monk; altar.rs2 amount arg; deferred: east_gate, trail clue, monk AI heal, sound_synth, ~objbox |
| 9f | Wilderness F2P stubs | done | Fat Tony/Noterazzo Trade stubs + bandit leaders Talk-to expand + muddy chest; deferred: openshop, wild warning/levels/overlays, wild levers, lava ladders (orphan locs), ditch (no LC script), bandit drops/AI, Mage Arena |
| 9g | Wilderness levers (Ardougne) | done | wildinlever/wildoutlever + hauntedleverdown + warning varp + p_choice3_header; coords from LC; deferred: Entrana cave_monk/high_priest, guard2 (unresolved), wild warning/levels/overlays, lava ladders, ditch |
| 9h | Entrana F2P leftovers | done | cave_monk + entranaladdertop/zanarismagicdoor + high_priest greeting + entrana_monk heal + frincos Trade stub + shipmonk2 leave stub; fixed Port Sarim shipmonk2 bind; deferred: grail/heroquest arms, set_sail, openshop, ai_opplayer2 |
| 9i | Wilderness warning/levels/overlays | done | ~wilderness_level + zones dbrow + warning zone→mesbox + %wilderness; tele >20 gate; enter/exit stubs; deferred: IF1 overlays, set_player_op, music/move mapzones, ditch (no LC), lava ladders |
| 9j | Lava ladders / Mage Arena stubs | done | wildymirrorladder* name-remap teleports + Mage Arena NPC/loc stubs (lundail/gundai/kolodion/guardian + ladders/pool/barrier/statues/god cape drop); constants landed; deferred: kolodion fight/AI, battle_mage, openshop, armour cats, pool exactmove, god equip |
| 9k | KBD lever stubs + entrance ladder | done | dragonkingin/outlever teleports + wildymirrorladdertop1 switch (Monk's Friend + KBD + lava 62,16); constants landed; deferred: king_dragon AI/breaths/drops/hunt, bandit_camp_guards AI |
| 9l | armourmaking_wizard (Wizards' Tower) | done | Talk-to + splitbark craft (bark/cloth/coins); skill_multi→p_choice5; deferred: wizard/wizard_grayzag AI (summonedimp absent), hollow-tree bark gather, Mort'ton fine_cloth |
| 9m | newcomer_map (general_use, deferred from 5d) | done | Read mes-stub; full IF blocked (playermap_east + newcomers_pos + era IF1 model absent); ~mapsquare not needed for stub |
| 9n | bandit_camp_guards AI (+ combat overlays) | done | ai_applayer2 say+opplayer2; ai_queue1→applayer2 (retaliate_ap simplified); combat overlays guard/brawling+leaders; huntmode=aggressive approx; deferred: .hunt ranged/cowardly, ~npc_default_retaliate_ap, attackbonus/sounds |
| 9o | wizard / wizard_grayzag AI | done | npc_combat_magic (fire strike maxhit 4) + wizard AI; grayzag imp spawn remapped to `imp` + melee; combat overlays; deferred: summonedimp record, varn delay/notcombat, projanim_pl, poison/freeze, dark_wizard |
| 9p | king_dragon (KBD) combat AI | done | breaths/melee/hunt AI + drop table + combat overlay; deferred: poison/freeze, antifire potion, .hunt/sethuntmode, acid/ice spotanims, trail clues, sounds |
| 9q | dark_wizard combat AI | done | bearded/young earth|water cast AI + combat overlays; drops already present; deferred: weaken/confuse stat_sub, .hunt ranged, fai_dark_wizard_*, hollow-tree bark |
| 9r | hollow-tree bark gather | done | hollow_tree/_big → tree cat + stump stages; hollow_tree_table (lvl 45, hollow_bark); woodcut.rs2 already covers; deferred: Mort'ton fine_cloth (catacombs) |
| 9s | Mort'ton fine_cloth (shade chests) | done | chest open+drop tables (steel/black/silver roll fine_cloth); names resolve; deferred: lair doors/entrance, shade AI, pyre→keys, flamtaer, quest, trail clues |
| 9t | Melzar AI (Dragon Slayer maze) | done | melzar_the_mad AI (ap/op casts+cabbage+zap+say) + combat overlay; maze critters default melee (drops already 8j); deferred: weaken/curse stat_sub, .hunt aggressive_melee, sound_synth, Elvarg fire-breath |
| 9u | Elvarg fire-breath combat AI | done | ai_ap/op fire+melee + maxhit (shield/Protect Magic) + elvarg/elvarg_alive overlays; deferred: antifire potion, .hunt elvarg_hunt, sound_synth, %npc_aggressive_player last-hit, gosub(npc_death) |
| 9v | npc_stat_change_effect (weaken/confuse/curse) | done | NPC→player drain + debuff_allowed gate in npc_combat_magic; unlocks dark_wizard/Melzar casts; deferred: god-spell exclusion keys, freeze walktrigger, poison |
| 9w | metal-dragon drop tables | done | bronze/iron/steel_dragon ai_queue3 drops + rare platelegs; names resolve; deferred: metal_dragon combat AI, trail clues, Mort'ton lair, Mage Arena god-spell keys |
| 9x | metal-dragon combat AI | done | bronze/iron/steel ai_ap/op + close/far breath + maxhit (shield; no Protect Magic) + combat overlays; deferred: antifire potion, .hunt cowardly, sound_synth, trail clues |
| 9y | Mage Arena god-spell NPC keys + battle_mage | done | ^*_npc keys + magic_spell_*_npc rows; battle_mage Attack gate + AI casts; combat overlays; deferred: cape+staff hail (%npc_aggressive_player), .hunt, player god keys (defer-table), kolodion_fight |
| 9z | chromatic dragon combat AI | done | green/blue/red/black OP AI (1/4 breath + melee) + combat overlays; drops already present; deferred: antifire, .hunt cowardly, sound_synth, babydragon AP |
| 8 | Outward areas / remaining quests / minigames | pending | Next: Mort'ton shade AI (names resolve) / poison; skip: Mort'ton lair (%morttonmulti bits + key cats + metal_gate), kolodion_fight (player god keys defer-table), antifire (%dragonresist), freeze (%frozen), guard2, ditch, shops inv.ini |


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
- slice 8u done: skill_combat F2P ranged (player_ranged.rs2 via player_melee_swing; ranged_ammo_table bronze..rune; cat 62=arrows/63=bolts; ranged XP + maxhit; weapon_attackrange approach); deferred PvP, crossbow combat, thrown, crystal/dark bow specs, chinchompas, poison, inv_dropitem_delayed ammo recover, rapid/longrange clock tweaks
- slice 8v done: Varrock NPCs — Lowe, Thessalia (dialogue+makeover stub), Zaff, Horvik, swordshop1/2, tea_seller, Scavvo, Valaine, tailorp, kanel/philop/shilop/wilough, Gertrude stub, jollyboar/bluemoon/donkey bartenders, sworddummy; Trade → mes stub; deferred: ~openshop, Gertrude's Cat, Thessalia IF1 makeover, east_gate (orphan cat+biohazard), barcrawl, trails; tramppg/aris already present
- slice 8w done: general_use chests/cupboards/drawers (name-bound F2P pairs; ~open_chest/~close_chest shared; empty search); deferred: orphan cats, trails, findsomethingnice, locked chests, arena_hospital drawers, members/quest cupboards already elsewhere
- slice 8x done: Falador NPCs — risingsun_barmaid ales, cassie/drogo/flynn/herquin/wayne/nurmof shop stubs, mguild_ladder/door (mining 60), hairdresser+makeover_mage IF stubs, goblin_red/greenarmour; Trade → mes stub; deferred: ~openshop, barcrawl, kit IFs, to_be_fixed_by_nurmof, guard2 (name unresolved), Kaylee/Tina barmaid binds
- slice 8y done: Port Sarim NPCs — betty/brian/grum/gerrant (+sarim_* dual binds), sailors (*_1op/*_2op Karamja telejump), shipmonk/shipmonk2 members stub, dock ladder, rommik (Rimmington); wydin Trade stub; deferred: ~openshop, ~set_sail IF, Entrana armour cats, clues, Heroes eel
- slice 8z done: Al Kharid + remaining Draynor — zeke/dommik/louie_legs/ranael/gem_trader shop stubs, silk_trader+kebab_seller coin sales, tanner p_choice tan (IF1 deferred), border_gate→kharidmetalgate* walk-through, desertdoor curtains; diango (aprilfoolshorsesalesman)+horsey ops, jailguard, manor_vines; deferred: shantay*/thkebab/thshantay (members), al_kharid_warrior pack AI, witch AI (orphan cat+npc_cast_spell), ~openshop, crest/trails, duel arena, gate swing
- slice 9a done: Rimmington LC complete (rommik/hetty already present); Edgeville dungeon brasskeydoor+oddwall walk-through; Karamja customs_officer (rum search, Port Sarim/Ardougne telejump) + banana trees (name-expanded + plantation.loc); F2P generalshopkeeper/assistant 2..7 Trade stubs (Rimmington/Edgeville/Karamja included); deferred: ~openshop, ~set_sail IF, door swing/inviswall, zambo (absent), jiminua/captain_shanks/members
- slice 9b done: Barbarian Village — peksa (Trade stub), gunthor_the_brave, barbarian Talk-to expanded to fai_barbarian_1..8/10..14 + females, beer_barrel beatdown at re-measured 0_48_53_26_41, drop table fai expand; spinning already slice 3; deferred: ~openshop, scorpcatcher hint, barbarian_woman (absent), outpost/agility/fishing/herblore, fai_barbarian_barrel (no Use op), Hunding (fai_barbarian_9)
- slice 9c done: Gertrude's Cat (quest_fluffs) — Gertrude full dialogue, shilop/wilough → fluffs_boy_dialogue, Fluffs milk/sardine/kitten, gertrudefence + empty barrel/crate, doogle+sardine, journal quest_gertrudescat; %fluffs varp 180 + fluffs_crate/lumberyard_fence_used server; deferred: pet.rs2 growth/follower (reward is kittenobject), trail clues, lostkitten .npc_* cinematic, chompy doogle
- slice 9d done: general_use gates/fence/gangplank + findsomethingnice — fencegate/metalgate already door_closed; farming/rustic pairs added to doors.loc; memberfencegate walk-through; mournerstewfence squeeze; sarim/karamja/brimhaven/ardougne/entrana planks (not dragonship*); findsomethingnice + wire into chests/drawers/crates/sacks/wardrobes; deferred: orphan gate_main_*, board_message param, duel arena, ~open_and_close_metal_gate
- slice 9e done: Varrock leftovers thin → monastery F2P (Abbot Langley heal/join, Brother Jered bless stringstar→blessedstar, monk Talk-to heal, monasteryladder %prayer_guild gate via ~climb, monks_altar +2 via pray_at_altar amount) + varrock_palace plaque_zamorak_monk; altar.rs2 restored amount arg + stat_add; deferred: east_gate (biohazard), trail clue on Abbot, monk ai_opplayer2 self-heal, sound_synth, ~objbox
- slice 9f done: Wilderness F2P — fat_tony/noterazzo Talk-to+Trade stubs, bandit_camp_leaders (black_heather/donny_the_lad/speedy_keith), muddy_chestclosed key loot; deferred: ~openshop, wilderness warning/levels/overlays, wildin/out lever, lava maze ladders, ditch, bandit drops/AI, Mage Arena
- slice 9g done: Wilderness levers — wildinlever/wildoutlever Pull + warning mesbox/p_choice3_header + %warning_wilderness_teleport_lever server varp + ~player_teleport_normal; p_choice3_header added to chat.rs2; drop sound_synth; deferred: Entrana cave_monk/entranaladdertop/zanarismagicdoor/high_priest, guard2 (unresolved), wilderness warning/levels/overlays, lava ladders, ditch
- slice 9h done: Entrana F2P leftovers — cave_monk chat+climb (prayer drain), entranaladdertop→cave_monk via npc_findexact, zanarismagicdoor wild telejump, high_priest greeting (grail/hero deferred), entrana_monk heal via monastery labels, frincos Trade stub, shipmonk2 leave members stub; Port Sarim shipmonk2 unbound (Entrana-only); deferred: %grail/%heroquest, ~set_sail, ~openshop, ai_opplayer2
- slice 9i done: Wilderness warning/levels — wilderness_levels.constant + %wilderness + ~wilderness_level (zones dbrow) + warning strip zones→~mesbox (IF1→mesbox); enter/exit stubs (%wilderness only); magic tele >20 wildy gate; deferred: wilderness_overlay IF1 / set_player_op opcode / music/move mapzone callers, ditch (no LC), lava ladders
- slice 9j done: Lava Maze wildymirrorladder1/2 + top1/2 teleports (LC loc_1766/1768 remapped by name) + Mage Arena stubs — lundail Trade, gundai ~openbank, kolodion Talk-to (duel mes-stub), chamber guardian + statues + god cape drop/pickup, cellar ladders + pool/barrier stubs; mage_arena.constant landed; deferred: kolodion_fight/battle_mage AI, openshop, armour inv_totalcat gate, pool exactmove, god opheld2 equip
- slice 9k done: KBD stubs — dragonkinginlever/dragonkingoutlever (~player_teleport_normal to lair/exit) + king_black_dragon.constant; fixed wildymirrorladdertop1 to loc_coord switch (Monk's Friend 0_40_50_1_22, KBD 0_47_60_9_9, lava 62,16); deferred: king_dragon combat AI/breaths/drops/hunt/freeze, bandit_camp_guards AI, trail clues
- slice 9l done: armourmaking_wizard — Talk-to dialogue + splitbark helm/body/legs/gauntlets/greaves craft (hollow_bark+fine_cloth+coins); skill_multi5→~p_choice5 one-at-a-time; deferred: wizard/wizard_grayzag combat AI (summonedimp absent), hollow-tree bark, Mort'ton fine_cloth drops
- slice 9m done: newcomer_map — Read → ~mesbox stub; playermap_east IF / newcomers_pos varp / if_playermap_east model still absent (era IF1); full mapsquare→marker UI deferred
- slice 9n done: bandit_camp_guards — AI (say+mode) + bandit_camp.npc combat overlays (guard/brawling hunt + leaders HP/stats); drops already 9f/drop_tables; deferred: .hunt ranged/cowardly profiles, ~npc_default_retaliate_ap (varns), attackbonus/attack_sound
- slice 9o done: wizard/wizard_grayzag AI — npc_combat_magic.rs2 (cast helpers + combat_damage_player) + wizard fire-strike AI + grayzag imp spawn (`imp` remap) + melee; wizard_tower.npc combat overlays; deferred: summonedimp authored record, %npc_action_delay/notcombat varns, projanim_pl, npc_poison/freeze, dark_wizard, hollow-tree bark
- slice 9p done: king_dragon combat AI — ai_ap/op breaths+melee (fiery far/close, toxic/icy/shocking specials, shield/Protect Magic maxhit), king_dragon.npc overlay (aggressive hunt approx), drop_tables/king_dragon.rs2; deferred: poison/freeze walktrigger, antifire potion (%dragonresist false-friend), npc_sethuntmode/.hunt, acid/ice/triple spotanims, trail clues, sound_synth
- slice 9q done: dark_wizard combat AI — bearded earth (weaken/earth_strike) + young water (confuse/water_strike) via npc_combat_magic; Delrith busy gate (^demon_silverlight); dark_wizard.npc overlays (aggressive hunt approx); drops already in drop_tables/; deferred: weaken/confuse npc_stat_change_effect, .hunt ranged, fai_dark_wizard_* binds, hollow-tree bark
- slice 9r done: hollow-tree bark — hollow_tree/hollow_tree_big loc overlays (category=tree, next_loc_stage stump) + hollow_tree_table dbrow (product hollow_bark); names resolve; woodcut.rs2 unchanged; deferred: Mort'ton fine_cloth (mortton_catacombs not tiny)
- slice 9s done: Mort'ton fine_cloth — shade chest oploc1/u + open_shade_chest + bronze/steel/black/silver drop tables (fine_cloth on steel/black/silver); constants; deferred: lair entrance/doors, shade combat, pyre keys, flamtaer, quest, trail clues
- slice 9t done: Melzar the Mad combat AI (ai_queue1/ap/op + cabbage/zap procs + say lines) + quest_dragon.npc overlay (aggressive hunt approx); maze critters use default melee (key drops already 8j); deferred: weaken/curse npc_stat_change_effect, .hunt aggressive_melee, sound_synth, Elvarg fire-breath
- slice 9u done: Elvarg fire-breath combat AI — ai_queue1/ap/op (AP↔OP mode rolls + close/far breath + melee) + ~elvarg_max_hit (shield/Protect Magic) + quest_dragon.npc overlays (elvarg + elvarg_alive, aggressive hunt approx); deferred: antifire potion (%dragonresist false-friend), .hunt elvarg_hunt, sound_synth, %npc_aggressive_player last-hit, gosub(npc_death)
- slice 9v done: npc_stat_change_effect — NPC→player confuse/weaken/curse via magic_spell_table:stat_change + ~npc_debuff_allowed (already-below-base gate); dark_wizard/Melzar comments updated; deferred: god-spell exclusion (^flames/^claws/^strike still defer-table), freeze/%frozen, poison
- slice 9w done: metal-dragon drops — bronze_dragon/iron_dragon/steel_dragon ai_queue3 (bars + rare platelegs + 128-table); shared edits (no gosub death, no trail_hardcluedrop); deferred: metal_dragon.rs2 combat AI, Mort'ton lair (%morttonmulti + key cats + open_and_close_metal_gate), Mage Arena god-spell _npc keys (player keys still defer-table)
- slice 9x done: metal-dragon combat AI — bronze/iron/steel ai_queue1/ap/op (melee-in-AP + 1/4 close breath + far projectile) + ~metal_dragon_breath_maxhit (shield; Protect Magic ignored per LC) + metal_dragon.npc overlays (aggressive hunt approx for cowardly); deferred: antifire potion (%dragonresist), .hunt cowardly, sound_synth, trail clues, strongholdcave variants
- slice 9y done: Mage Arena god-spell NPC keys (^flames_of_zamorak_npc/claws_of_guthix_npc/saradomin_strike_npc + dbrows) + battle_mage AI (Attack gate via inzone+p_opnpc(2); casts; say lines) + mage_arena.npc overlays; deferred: cape+staff hail immunity (%npc_aggressive_player), .hunt battle_mage_hunt, player god-spell keys (defer-table), kolodion_fight
- slice 9z done: chromatic dragon AI — green/blue/red/black OP (1/4 ~dragon_fire + ~dragon_melee) + dragon.npc overlays (aggressive approx for cowardly); drops already in drop_tables/; deferred: antifire (%dragonresist), .hunt cowardly, sound_synth, babydragon
- next pending: Mort'ton shade AI (shade_level*/shadeshadow_* resolve) / poison (%poison clean-varp); skip blocked: Mort'ton lair (%morttonmulti bits + key cats + ~open_and_close_metal_gate), kolodion_fight (player god keys defer-table), antifire (%dragonresist false-friend), freeze (%frozen unresolved), guard2, ditch, shops inv.ini
