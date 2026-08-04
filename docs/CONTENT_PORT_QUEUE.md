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
| 10a | Mort'ton shade AI | done | `_shade` cat 345; shadow→shade rise + melee + str drain; overlays L1–5; deferred: quest/flamtaer/%morttonmulti/timer-reset/.hunt; next_npc_type via type-switch (apply_param no type=npc) |
| 10b | player poison system | done | poison.rs2 (%poison varp 102, hitsplat_poison); melee/magic/KBD toxic wire; login+death; deferred: npc_poison varn, weapon_poison, antipoison consume |
| 10c | babydragon combat AI | done | babybluedragon(+2,+3) OP melee + bdrag overlays + babydragon_bones; LC babydragon absent; deferred: AP/fire, .hunt cowardly, sound_synth |
| 10d | antipoison consume | done | Drink labels + dose switch ladder (obj next_obj_stage overlays blocked); deferred: full _potion consume, consume_messages.dbrow, sound_synth |
| 10e | weapon_poison | done | Use→poison for daggers/spears/arrows/bolts/darts/knives/javelins via switches; deferred: cleaning cloth, karambwan paste, combat poison_severity |
| 10f | highwayman combat AI | done | Stand and deliver! AP + overlays + highwayman2 drop bind; deferred: .hunt ranged, attack_sound |
| 10g | chaosmonk combat AI | done | chaosmonk1/2/3 1/4 zap + melee; npc_zap_attack from Melzar; overlays; deferred: .hunt cowardly, attack_sound, drop table (none in LC) |
| 10h | witch combat AI (Draynor) | done | witch1/2 name-expand (_witch orphan); weaken/earth_strike; overlays; deferred: category mint, attack_sound |
| 10i | wine_of_zamorak altar | done | Chaos Temple Take drain+aggro chaosmonks; sound_synth dropped; deferred: telegrab-only modern path |
| 10j | shadow_spider combat AI | done | prayer-halve drain on queue1 + melee; spider_update_* anims; deferred: op drain via ~npc_retal_ready varn, sound_synth, .hunt cowardly |
| 10k | skeletonmage combat AI | done | ^skeleton_mage_attack=105 + magic_spell_skeleton_mage dbrow (skeleton cast anims; sound dropped) + AI + overlay; deferred: undead param, attack_sound |
| 10l | cow milking | done | opnpcu cow/cow2/cow_beef + bucket_empty → bucket_milk; combat overlay already lumbridge.npc |
| 10m | barbarian YEARGH combat AI | done | 1/4 YEARGH + melee; fai_barbarian_* name-expand + anim overlays; deferred: barbarian_woman (absent), %npc_action_delay, gunthor YEARGH (none in LC) |
| 10n | pirate Talk-to dialogue | done | _pirate orphan → pirate1/2/2_aggressive/3_aggressive/lady_pirate; deferred: category mint, pickpocketable variants |
| 10o | chaos_druid combat AI | done | AP/OP bind+confuse + melee; overlay; deferred: %frozen freeze walktrigger, .hunt ranged, wilderness_chaos_druid / warrior AI |
| 10p | al_kharid_warrior pack AI | done | ai_queue1 retaliate + huntall pack say; overlay; deferred: %npc_aggressive_player exact target wiring |
| 10q | freeze (%frozen + walktrigger) | done | frozen.varp; [walktrigger,frozen] + npc_freeze_*; chaos_druid + KBD icy wired; engine walktrigger/p_walk; deferred: %npc_stunned PvM freeze, .walktrigger PvP |
| 10r | cleaning cloth (weapon poison wipe) | done | tbwt_cleaning_cloth Use + reverse switch map + wipe messages; deferred: karambwan paste, poison_severity on hit, obj next_obj_stage overlays |
| 10s | citizen Talk-to expand | done | man2/woman3/al_kharid_man binds on citizens.rs2 (drop-table member list); deferred: _citizen category mint |
| 10t | monk / entrana_monk combat AI | done | 1/4 self-heal (npc_statheal HP) + melee; monk.npc overlays; deferred: sound_synth, %heroquest firebird |
| 10u | chaos_druid_warrior AI expand | done | Name-expand bind+confuse+melee + yanille.npc overlay; deferred: wilderness_chaos_druid (Elder modern false-friend), .hunt, attackbonus |
| 10v | cooking dough | done | pot_flour+water → bread/pastry/pizza/pitta; p_choice4_header; water empty switch; deferred: cake_tin, swamp_tar, murder_proofobj |
| 10w | cooking wine | done | grapes+jug_water → ferment timer (bank+inv); login hook; modulo() for %; deferred: bad-wine polish, reverse Use |
| 10x | dye cape mixing | done | primary dye mixes + cape all cape colours (switch maps); dye→goblin/wig reverse; deferred: crafting_capes_struct overlays |
| 10y | studded leather | done | studs↔leather_armour/chaps → studded_body/chaps; switch maps (struct blocked); members gate |
| 10z | glassblowing | done | sandpit fill + furnace soda_ash/sand→molten_glass + pipe p_choice beer/vial/orb; deferred: lens_mould/telescope, lantern glass, weakqueue batch |
| 11a | necromancer AI | done | invrigar/necromancer summon+confuse/weaken/curse+melee; summonedzombie overlay; LC ^defence→^curse; deferred: .hunt, sound_synth, %npc_action_delay |
| 11b | Taverley jail/prison doors | done | dungeonjail/deepdungeondoor key walk-through; cauldrondoor armour spawn+suit_of_armour; deferred: door swing, ctratgatea clock-tower, grate/iron_door synth |
| 11c | snelm | done | chisel→shell name-expand; snelm_product switch; ~snelm_reduction proc; deferred: Mort Myre snail combat wiring |
| 11d | battlestaves | done | orb+battlestaff Use; switch level/xp/product (struct blocked); members gate |
| 11e | jewellery | done | furnace gold/silver p_choice + stringing switches; perfect gold ruby; deferred: IF1 jewelry UI, weakqueue batches, mm/regicide string |
| 11f | crafting guild | done | craftingguilddoor level40+apron walk-through + master_crafter Talk-to; deferred: door swing |
| 11g | dragonhide leather | done | coif+green/blue/red/black dhide body/vamb/chaps; leather count column; p_choice menus; deferred: weakqueue batch, leather_crafting IF |
| 11h | enchanted jewellery | done | glory/dueling/games necklace Rub tele; forging charges+smelt; ring of life on hit; deferred: recoil (%aggressive_npc), modern multi-dest, pre_tele zones |
| 11i | fountain + enchant5 | done | fountain_of_heroes recharge glory→(4); enchant_5 dragonstone→glory/wealth; deferred: ring-of-wealth Rub, objbox polish |
| 11j | bones to bananas | done | magic_spellbook:bones_bananas + convert_bones; deferred: bones_to_peaches (absent LC) |
| 11k | charge orb | done | oploct/aploct charge_*_orb + loc_type col + air/water/earth/fire rows; deferred: legends gate, charge_orb_name string param, afk/synth |
| 11l | Thormac mystic staff | done | thankyou+makestaff via p_choice5 (air/water/earth/fire/lava); scorpcatcher quest gate deferred; modern IF extras deferred |
| 11m | ring of recoil | done | %ring_of_recoil+%aggressive_npc authored; recoil on melee+magic damage; deferred: %npc_attacking_uid varn, PvP recoil |
| 11n | Mage Arena charge | done | magic_spellbook:charge + timer; %magearena/%magearena_charge clean-varps; deferred: sound_synth, god-spell Charge consumers |
| 11o | stankers + poison chalice | done | Talk-to + chalice gift; Drink random heal/drain/damage; deferred: none |
| 11p | foresters_bartender | done | beer/stew/meat pie sales; deferred: barcrawl (%barcrawl / ^forestersarms_index) |
| 11q | trollheim teleport | done | magic_spellbook:trollheim_teleport + dbrow + ^trollheim_teleport; deferred: Eadgar quest gate, sled unequip |
| 11r | coal trucks | done | oploc1/u coal_truck + %coal_truck authored + ^coal_truck_max; deferred: sound_synth |
| 11s | seer default | done | default Talk-to (Many greetings / knowledge+power); deferred: scorpcatcher quest arms |
| 11t | bees / beehive wax | done | merlin_beehive + insect_repellent/bucket; %beehive_free authored 5759; deferred: none |
| 11u | mcgrubors wood | done | gates name-expand + railing squeeze + _red_vine dig; deferred: sound_synth, mcgrubor_gate cat mint |
| 11v | brother_galahad | done | Talk-to + tea/napkin; %grail clean-varp + quest_grail constants; deferred: full Holy Grail quest |
| 11w | king_arthur + %arthur | done | Talk-to + Merlin Crystal start/grail arms; %arthur clean-varp 14 + constants; arthur/grail complete queues; deferred: full Merlin Crystal / Holy Grail bodies |
| 11x | merlin / merlin2 | done | crystal rush-off npc_del + Holy Grail workshop tips (%grail_spoken_merlin); deferred: Merlin Crystal body |
| 11y | sir_gawain + sir_lancelot | done | Talk-to + %arthur spoken_gawain/lancelot advances; deferred: Lefaye stronghold / crate quest |
| 11z | sir_kay + sir_bedivere | done | Talk-to %arthur/%grail arms; deferred: trail clue on Kay |
| 12a | lucan/palomedes/pelleas/tristram | done | remaining Round Table Talk-to stubs; deferred: Merlin Crystal body |
| 12b | hemenster thin fishermen | done | morris/bigdave/joshua + %fishingcompo clean-varp 11 + constants; deferred: full Fishing Contest |
| 12c | grandpa_jack + sinister_stranger | done | story/hints + Vlad dialogue; deferred: bonzo/comp spots / my_spot arm |
| 12d | poison_salesman | done | Murder Mystery arms; %murderquest clean-varp 192 + poisonproof authored 5760; deferred: full Murder Mystery, Fremennik beer keg |
| 12e | arhein | done | Talk-to + Merlin fort arm; Trade stub; deferred: trail clue, openshop |
| 12f | candle_maker | done | shop stub + black candle Merlin arms; %excalibur_components_progress authored 5761 + bit indices; deferred: openshop |
| 12g | harry | done | fishing shop Talk-to + Trade stub; deferred: openshop |
| 12h | hickton | done | archery shop Talk-to + Trade stub; deferred: openshop |
| 12i | bonzo + hemenster_comp | done | competition entry/handover/champ; %hemenster_comp_stage 5762 + %hemenster_pipe_stashed 5763; deferred: spots |
| 12j | tunnel_dwarf + fishingcompo complete | done | quest start/finish + pass; ~quest_complete(quest_fishingcontest); deferred: tunnel stairs |
| 12k | garlicpipe + hemenster gate | done | stash + move_hemenster_pipe + pass gate walk-through + bonzo_quits; deferred: gate swing |
| 12l | sir_mordred + thrantaxaltar | done | spare Merlin dialogue + chaos altar words; deferred: crate ship, Excalibur lake, bat-bones summon, crystal smash |
| 12m | merlin crate ship | done | merlincrate + %arhein_crate_coord 5764 + Keep Le Faye door walk-through/knock; deferred: door swing/inviswall |
| 12n | Excalibur lake | done | ladyofthelake + lake_beggar + jewellersdoor/ladder walk-through; deferred: door swing |
| 12o | thrantax bat-bones | done | opheld5 summon + binding words → ^arthur_excalibur_bound; deferred: sound_synth |
| 12p | merlins_crystal smash | done | oplocu Excalibur shatter + free Merlin; deferred: none |
| 12q | hemenster competition spots | done | hemenster_fishing + my_spot + bigdave/joshua labels; deferred: gate swing |
| 12r | murder_guard start | done | start/%murdersus + accuse/proof arms + %murder_evidence 5765 + complete queue; deferred: family NPCs, fingerprints flour, journal |
| 12s | gossipy_man | done | full Murder Mystery gossip tree; deferred: none |
| 12t | murder poisonproof locs | done | compost/hive/drain/web/fountain/crest Investigate; deferred: barrels, window thread, flour prints |
| 12u | anna + bob | done | Sinclair family Talk-to trees; deferred: none |
| 12v | carol + david | done | Sinclair family Talk-to trees; deferred: none |
| 12w | elizabeth + frank | done | Sinclair family Talk-to trees; deferred: none |
| 12x | murder servants | done | donovan/hobbes/louisa/mary/pierre/stanford; deferred: trail clues |
| 12y | murder evidence barrels | done | barrela..f Search + murderweapon/murderpot2 Take; deferred: flour prints |
| 12z | murder window/flour/sacks | done | kr_mansion_window_multi thread + flourbarrel + sacks flypaper + dog gates; deferred: Break op |
| 13a | flour prints + murder_journal | done | name-expand flour/print switches + quest_murdermystery journal wire; deferred: category overlays |
| 13b | arthur_journal | done | Merlin's Crystal journal + quest_merlinscrystal wire; deferred: none |
| 13c | fishingcompo_journal | done | Fishing Contest journal + quest_fishingcontest wire; deferred: none |
| 13d | sinclair_guard_dog | done | murder_mystery_guarddog ai_timer BARK; deferred: none |
| 13e | cake_tin dough arm | done | pot_flour↔cake_tin + egg/milk → uncooked_cake; deferred: gnome/cocktail |
| 13f | chocolate cake | done | cake+chocolate_bar/dust → chocolate_cake (lvl 50); herblore grind/brew arms extended |
| 13g | chocolate milk + hangover | done | dust+milk → chocolaty_milk; snape_grass → hangover_cure; ~objbox→~mesbox |
| 13h | swamp paste | done | swamp_tar+pot_flour → rawswamppaste (Sea Slug label); deferred: seaslug body |
| 13i | white_wolf rockslide | done | herorockslide mine+forcemove; mining_anim switch; deferred: sound_synth |
| 13j | grail realm thin NPCs | done | fisherman/maiden/king_percival/peasants Talk-to; deferred: none |
| 13k | fisher_king | done | Talk-to tree + %grail→finding_percival; deferred: none |
| 13l | grail_crone + Entrana priest | done | crone dialogue + high_priest grail arm; deferred: heroquest firebird |
| 13m | sir_percival | done | sack rescue + whistle handoff; deferred: none |
| 13n | grail_journal | done | Holy Grail journal + quest_holygrail wire; deferred: none |
| 13o | black_knight_titan | done | Talk-to + Excalibur death gate + drops; deferred: none |
| 13p | kaqemeex + quest_druid | done | Druidic Ritual start/complete + cauldron dip; %druidquest clean-varp 80; deferred: journal |
| 13q | sanfew | done | Ritual arms only; deferred: Eadgar/goutweed |
| 13r | fletching bolts/darts | done | cat 530/969 + tip cut + tip+bolt/feather; deferred: ogre/crossbow gem tips beyond opal/pearl/barb |
| 13s | druid_journal | done | Druidic Ritual journal + quest_druidicritual wire; deferred: none |
| 13t | crystal_chest/key | done | keyhalf join + chest unlock/loot table; deferred: sound_synth |
| 13u | gaius | done | two-handed sword shop Talk-to + Trade stub; deferred: openshop, trail clue |
| 13v | jatix | done | herblore shop Talk-to + Trade stub; deferred: openshop |
| 13w | taverly druid AI | done | entangle/snare dbrows + AP/OP entangle+melee; deferred: none |
| 13x | boy (witch's house) | done | ballboy Talk-to + %ballquest + complete queue; deferred: house locs/journal |
| 13y | yanille thin NPCs | done | sigbert/radick/tower_guard/frumscone/ogre_chieftan/guild_wizard; deferred: shops, barcrawl |
| 13z | burthorpe thin NPCs | done | citizens name-expand + cook/servant/WK/shop/barman/guards; ^death_complete; deferred: Death Plateau body, openshop |
| 14a | yanille shop stubs | done | frenita/magic_store/ogre_merchant/ogre_trader1 Trade stubs + retaliate traders; deferred: openshop, rockcake stall |
| 14b | witch house locs | done | pot/doors/cupboard/mouse/fountain/shed + experiment chain; coords landed; deferred: ball_irongate |
| 14c | death plateau thin NPCs | done | IG soldiers/sergeants + archers Talk-to; deferred: ai_timer drill/eat, archer combat drops |
| 14d | saba + eohric | done | hermit + headservant + %death_map authored + progress constants; deferred: Denulth/Tenzing/Dunstan/Harold, death_bits |
| 14e | Denulth | done | death_ig_commander + %death_bits 5768 + complete; deferred: troll_quest arms |
| 14f | Tenzing | done | death_sherpa Death Plateau arms + boots shop post-quest; deferred: troll_love/Trollweiss |
| 14g | Dunstan | done | death_smithy Death Plateau arms + spike/anvil/son; deferred: troll_quest/sled/law tali/repair |
| 14h | Harold | done | death_guard_equiproom drink/duty/combo + inline gamble; deferred: death_dice IF1 |
| 14i | witches_diary | done | opheld mesbox pages + mouse-door varp advance; deferred: IF1 book UI |
| 14j | nora AI | done | ai_spawn+ai_timer hunt/LOS curse + patrol overlay; deferred: sound_synth |
| 14k | death IOU + scout | done | death_iou→combination + combination mesbox + secret-path zone; deferred: doors/mechanism/rocks |
| 14l | death + ball journals | done | death_journal + ball_journal wired to quest_deathplateau/quest_witchshouse |
| 8 | Outward areas / remaining quests / minigames | pending | Next: Death Plateau doors/stone mechanism or burthorpe leftovers; skip: scorpcatcher, wilderness_chaos_druid (Elder), Mort'ton lair, pyre, kolodion_fight, antifire, guard2, ditch, shops inv.ini, npc_poison varn, imp teleport, barcrawl |



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
- slice 8a done: Restless Ghost (Aereck/Urhney/ghostx + shutghostcoffin↔openghostcoffin_*; skull; journal quest_restlessghost; tower altar multiloc via %restless_ghost_altar_var + child Search); npc_retaliate deferred
- slice 8b done: Ernest the Chicken (Veronica/Oddenstein + compost/fountain/closet/levers; %ernestdoors multilocs + live remorph/OpLoc resolve; journal quest_ernestthechicken); double-door open_and_close deferred; fountain poison server varp
- slice 8c done: Prince Ali Rescue (Hassan/Osman/Leela/Keli/Joe/Ali + Ned rope/wig + Aggie paste; journal quest_princealirescue); Aggie dyes + Ned Dragon Slayer + metal gate helper deferred
- slice 8d done: Demon Slayer (Aris/Prysin/Rovin/Traiborn + drain key + Delrith incantation; journal quest_demonslayer); Oracle clues + Dragon Slayer map piece deferred
- slice 8e done: Black Knights' Fortress (Sir Amik + fortress doors/grill/cabbage sabotage; journal quest_blackknightsfortress); open_and_close_door swing + inacbk Open op deferred
- slice 8f done: Shield of Arrav (Phoenix + Black Arm paths; Reldo/tramppg/Baraek/Straven/Katrine/Curator/Roald; journal quest_shieldofarrav); book UI + open-chest Search/Close ops deferred
- slice 8g done: Pirate's Treasure (Frank/Luthas/Wydin + banana crate/chest/Falador dig; journal quest_piratestreasure); customs officer rum search deferred
- slice 8h done: The Knight's Sword (Squire/Thurgo/Vyvin + portrait cupboard + blurite rocks; journal quest_knightssword); trail clue on squire deferred
- slice 8i done: Dragon Slayer core (Champions' Guild, Oziach, Klarense/ship repair, Ned, Wormbrain, map assemble, Duke shield, Oracle; journal quest_dragonslayer1); Melzar's Maze + Elvarg fight deferred
- slice 8j done: Melzar maze (keyed doors + *_1_key drops + funchest mappart1), oracle door as dragon_slayer_qip_magic_door + mappart3 chest, Elvarg/elvarg_alive → dragon_complete, dragonsecretdoor; deferred: fire-breath AI, Melzar combat spells, crandor_rock/rope/elvarg_gate (authored absent)
- slice 7g done: skill_smithing (smelting table + furnace cat 215; anvil cat 772 + smithing_bar 151; F2P bars/products via p_choice; dorics_anvil gate); deferred cannonballs, dragon sq, claws/darts/wire/studs, jewellery furnace redirects, CS2 smithing.if
- slice 8k done: skill_runecraft (runecraft_table + F2P air..body + members cosmic..death; rc_ruins 8200 / rc_exit_portal 8201; essence mine enter/exit; Aubury+Sedridor tele wired; ruin multilocs need live remorph + OpLoc resolve — landed); deferred soul/blood, Ourania/zeah, tiaras, projanim_pl projectile, int loc_param(rune_type), Aubury shop, Brimstail/Disentor/Cromperty
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
- slice 10a done: Mort'ton shade AI — category shade=345; mortton_shades.rs2 (ai_queue1/2/13 + opplayer2 rise + melee + 1/20 str drain + shade_attack spotanim) + mortton_shades.npc L1–5 overlays; transform via ~shade_risen_type (apply_param lacks type=npc for next_npc_type); deferred: quest kills, flamtaer, %morttonmulti, timer reset-to-shadow, .hunt shades, undead param, sounds
- slice 10b done: player poison — poison.rs2 (%poison clean-varp 102; hitsplat_poison; timer/damage/clear/login); ~npc_poison_player on melee hit + magic success; KBD toxic queue(poison_player,36); death+login hooks; deferred: %npc_poison varn, weapon_poison ops, antipoison consume labels, sound_synth
- slice 10c done: babydragon AI — babybluedragon(+2,+3) OP melee (~babydragon_melee + bdrag_attack) + combat overlays (death_drop babydragon_bones); LC babydragon absent (names.map); deferred: commented LC AP/fire path, .hunt cowardly, sound_synth
- slice 10d done: antipoison consume — anti_poison.rs2 Drink for 1–4dose antipoison + super; %poison=min(-5/-20); dose ladder via switch (obj next_obj_stage overlays blocked); deferred: full _potion dispatch, consume_messages.dbrow, sound_synth
- slice 10e done: weapon_poison — Use weapon_poison on daggers/spears/arrows/bolts/darts/knives/javelins (switch maps; cat weapon_stab→weapon_stab_sword); deferred: tbwt_cleaning_cloth/karambwan, combat poison_severity on hit
- slice 10f done: highwayman AI — "Stand and deliver!" applayer2 + overlays + highwayman2 drop bind; deferred: .hunt ranged, attack_sound
- slice 10g done: chaosmonk AI — chaosmonk1/2/3 1/4 ~npc_zap_attack + melee + combat overlays; deferred: .hunt cowardly, attack_sound, drops (none in LC)
- slice 10h done: witch AI — witch1/2 name-expand (orphan `_witch`) weaken/earth_strike + overlays; deferred: category mint, attack_sound
- slice 10i done: wine_of_zamorak — Chaos Temple opobj3 drain+stat_drain+chaosmonk aggro; sound_synth dropped; deferred: telegrab-only modern
- slice 10j done: shadow_spider AI — prayer-halve on queue1 + melee; spider_update_* anims; deferred: op ~npc_retal_ready drain, sound_synth, .hunt cowardly
- slice 10k done: skeletonmage AI — ^skeleton_mage_attack=105 + magic_spell_skeleton_mage (skeleton_update_mage_casting / skeleton_weaken_casting; sound dropped) + ai_opplayer2 rot/melee + combat overlay; deferred: undead param, attack_sound
- slice 10l done: cow milking — opnpcu cow/cow2/cow_beef bucket_empty→bucket_milk; deferred: Milk right-click op (LC Use-with only)
- slice 10m done: barbarian YEARGH AI — 1/4 say + melee on barbarian + fai_barbarian_1..8/10..14 + females; anim overlays; deferred: barbarian_woman, %npc_action_delay, gunthor YEARGH
- slice 10n done: pirate Talk-to — name-expand pirate1/2/2_aggressive/3_aggressive/lady_pirate (29-line table); deferred: _pirate category mint, pickpocketable
- slice 10o done: chaos_druid AI — AP/OP bind+confuse + melee + overlay; deferred: %frozen freeze, .hunt ranged, wilderness/warrior variants
- slice 10p done: al_kharid_warrior pack AI — ai_queue1 setmode + huntall "Brother…" + overlay; deferred: %npc_aggressive_player exact
- slice 10q done: freeze — %frozen authored (frozen.varp); [walktrigger,frozen] + queue(npc_freeze_player)/~npc_freeze_*; chaos_druid + KBD icy wired; engine walktrigger/getwalktrigger/p_walk + processWalktrigger; deferred: %npc_stunned PvM freeze, PvP .walktrigger
- slice 10r done: cleaning cloth — tbwt_cleaning_cloth Use + ~weapon_unpoisoned_obj reverse map + wipe messages; deferred: karambwan paste, combat poison_severity
- slice 10s done: citizen Talk-to expand — man2/woman3/al_kharid_man (+ existing) on citizens.rs2; deferred: _citizen category mint
- slice 10t done: monk/entrana_monk AI — 1/4 self-heal (npc_statheal HP-only) + melee + overlays; deferred: sound_synth, %heroquest
- slice 10u done: chaos_druid_warrior AI — name-expand bind+confuse+melee + yanille.npc combat overlay; deferred: wilderness_chaos_druid (Elder false-friend), .hunt, attackbonus
- slice 10v done: cooking dough — pot_flour + bucket/jug/bowl/vial_water → bread/pastry/pizza/pitta via p_choice4_header; empty switch; deferred: cake_tin, swamp_tar, murder_proofobj
- slice 10w done: cooking wine — grapes+jug_water → jug_unfermented_wine + fermenting_wine timer (bank batches + inv); ~ferment_wines_login; deferred: bad-wine polish
- slice 10x done: dye cape — mix primary dyes + dye capes (switch maps, no struct overlays); dye→goblin_armour / plainwig reverse; deferred: crafting_capes_struct .obj overlays
- slice 10y done: studded leather — studs Use on leather_armour/chaps (switch maps; crafting_studded_struct blocked); members gate; mock230_pack 0 errors
- slice 10z done: glassblowing — sandpit+bucket, furnace sand/soda_ash→molten_glass, pipe p_choice beer/vial/orb; furnace hook in smelting; deferred: telescope lens, lantern, weakqueue
- slice 11a done: necromancer tower AI — invrigar/necromancer summon zombie + confuse/weaken/curse + melee + overlays; LC ^defence→^curse; deferred: .hunt, sound_synth
- slice 11b done: Taverley jail/prison doors — dungeonjail+jail_key / deepdungeondoor+dusty_key walk-through; cauldrondoor suitofarmour→suit_of_armour; deferred: swing, clock-tower ctratgatea
- slice 11c done: snelm — chisel↔shell name-expand + product switch; ~snelm_reduction; deferred: Mort Myre snail wiring, sound_synth
- slice 11d done: battlestaves — air/fire/water/earth orb + battlestaff; switch data (crafting_staff_struct blocked); members gate; mock230_pack 0 errors
- slice 11e done: jewellery — furnace gold/silver p_choice + wool stringing switches; perfect gold ruby; deferred: crafting_jewelry.if, weakqueue, mm/regicide
- slice 11f done: crafting guild — craftingguilddoor (LC crafting_guild_door) lvl40+brown_apron walk-through + master_crafter Talk-to; deferred: door swing
- slice 11g done: dragonhide leather — coif + green/blue/red/black body/vamb/chaps; leather namedobj,int count; members gate; p_choice (no leather_crafting IF); mock230_pack 0 errors (3028 scripts)
- slice 11h done: enchanted jewellery — glory/dueling/games necklace Rub tele (charge switches); ring_of_forging varp+smelt; ring_of_life on melee/magic hit; deferred: recoil (%aggressive_npc), modern multi-dest, pre_tele zone tables; mock230_pack 0 errors (3060 scripts)
- slice 11i done: fountain_of_heroes glory recharge + enchant_5 (dragonstone amulet→glory, ring→wealth); mock230_pack 0 errors (3062 scripts)
- slice 11j done: bones to bananas (magic_spellbook:bones_bananas); peaches deferred (absent LC); mock230_pack 0 errors (3064 scripts)
- slice 11k done: charge orb (oploct/aploct magic_spellbook:charge_*_orb + loc_type + 4 rows); deferred: legends gate, charge_orb_name string param, afk/synth; mock230_pack 0 errors (3073 scripts)
- slice 11l done: Thormac mystic staff (p_choice5 air/water/earth/fire/lava); scorpcatcher quest gate deferred; mock230_pack 0 errors (3075 scripts)
- slice 11m done: ring of recoil (%ring_of_recoil + %aggressive_npc authored; melee+magic damage wire); deferred: npc_attacking_uid varn, PvP; mock230_pack 0 errors (3077 scripts)
- slice 11n done: Mage Arena charge (magic_spellbook:charge + timer; %magearena gate); deferred: sound_synth, god-spell consumers; mock230_pack 0 errors (3079 scripts)
- slice 11o done: stankers Talk-to + poison_chalice Drink (random heal/drain/damage → cocktail_glass_empty); mock230_pack 0 errors (3085 scripts)
- slice 11p done: foresters_bartender beer/stew/meat_pie sales; barcrawl deferred; mock230_pack 0 errors (3103 scripts)
- slice 11q done: trollheim teleport (IF + dbrow + constant); Eadgar gate + sled deferred; mock230_pack 0 errors (3104 scripts)
- slice 11r done: coal trucks (%coal_truck authored varp 5758 + ^coal_truck_max); mock230_pack 0 errors (3106 scripts)
- slice 11s done: seer default Talk-to (Many greetings / knowledge+power); scorpcatcher deferred; mock230_pack 0 errors (3109 scripts)
- slice 11t done: bees / merlin_beehive wax (%beehive_free authored 5759); mock230_pack 0 errors (3113 scripts)
- slice 11u done: mcgrubors wood (gates name-expand, railing, _red_vine dig); sound_synth + mcgrubor_gate cat deferred; mock230_pack 0 errors (3121 scripts)
- slice 11v done: brother_galahad + %grail clean-varp + quest_grail constants; full Holy Grail quest deferred; mock230_pack 0 errors (3130 scripts)
- slice 11w done: king_arthur + %arthur clean-varp 14 + constants + arthur/grail complete queues; Merlin Crystal / Holy Grail bodies deferred; mock230_pack 0 errors (3136 scripts)
- slice 11x done: merlin + merlin2 Talk-to; mock230_pack 0 errors (3138 scripts)
- slice 11y done: sir_gawain + sir_lancelot; mock230_pack 0 errors (3141 scripts)
- slice 11z done: sir_kay + sir_bedivere; trail clue deferred; mock230_pack 0 errors (3143 scripts)
- slice 12a done: sir_lucan/palomedes/pelleas/tristram; mock230_pack 0 errors (3147 scripts)
- slice 12b done: hemenster morris/bigdave/joshua + %fishingcompo; mock230_pack 0 errors (3150 scripts)
- slice 12c done: grandpa_jack + sinister_stranger; mock230_pack 0 errors (3157 scripts)
- slice 12d done: poison_salesman + %murderquest + murder_poisonproof_progress authored 5760; Fremennik/full Murder Mystery deferred; mock230_pack 0 errors (3158 scripts)
- slice 12e done: arhein Talk-to + Merlin fort arm; Trade stub; mock230_pack 0 errors (3163 scripts)
- slice 12f done: candle_maker + %excalibur_components_progress authored 5761 + bit indices; mock230_pack 0 errors (3165 scripts)
- slice 12g done: harry fishing shop stub; mock230_pack 0 errors
- slice 12h done: hickton archery shop stub; mock230_pack 0 errors (3169 scripts)
- slice 12i done: bonzo + %hemenster_comp_stage 5762 + %hemenster_pipe_stashed 5763; mock230_pack 0 errors (3175 scripts)
- slice 12j done: tunnel_dwarf + fishingcompo_quest_complete; mock230_pack 0 errors (3187 scripts)
- slice 12k done: garlicpipe + hemenster gate walk-through + move_hemenster_pipe; mock230_pack 0 errors (3196 scripts)
- slice 12l done: sir_mordred spare + thrantaxaltar; mock230_pack 0 errors (3204 scripts)
- slice 12m done: merlin crate ship + %arhein_crate_coord 5764 + Keep Le Faye doors walk-through; mock230_pack 0 errors (3218 scripts)
- slice 12n done: ladyofthelake + beggar + jewellersdoor/ladder; mock230_pack 0 errors (3226 scripts)
- slice 12o done: thrantax bat-bones summon; mock230_pack 0 errors (3227 scripts)
- slice 12p done: merlins_crystal smash; mock230_pack 0 errors (3228 scripts)
- slice 12q done: hemenster competition spots; mock230_pack 0 errors (3239 scripts)
- slice 12r done: murder_guard + %murder_evidence 5765 + murderer constants + complete; mock230_pack 0 errors (3256 scripts)
- slice 12s done: gossipy_man; mock230_pack 0 errors (3257 scripts)
- slice 12t done: murder poisonproof Investigate locs; mock230_pack 0 errors (3263 scripts)
- slice 12u done: anna + bob Sinclair Talk-to; mock230_pack 0 errors
- slice 12v done: carol + david Sinclair Talk-to; mock230_pack 0 errors
- slice 12w done: elizabeth + frank Sinclair Talk-to; mock230_pack 0 errors
- slice 12x done: murder servants (donovan/hobbes/louisa/mary/pierre/stanford); trail clues deferred; mock230_pack 0 errors
- slice 12y done: murder evidence barrels + weapon/pot Take; mock230_pack 0 errors
- slice 12z done: murder window/flour/sacks/dog gates (loc_2666→kr_mansion_window_multi_*); mock230_pack 0 errors
- slice 13a done: flour prints (switch maps) + murder_journal; mock230_pack 0 errors
- slice 13b done: arthur_journal + quest_merlinscrystal wire; mock230_pack 0 errors (3357 scripts)
- slice 13c done: fishingcompo_journal + quest_fishingcontest wire; mock230_pack 0 errors
- slice 13d done: sinclair_guard_dog ai_timer; mock230_pack 0 errors
- slice 13e done: cake_tin → uncooked_cake (+ dough arm); mock230_pack 0 errors
- slice 13f done: chocolate cake; mock230_pack 0 errors
- slice 13g done: chocolaty_milk + hangover_cure; mock230_pack 0 errors
- slice 13h done: swamp_tar+flour → rawswamppaste; mock230_pack 0 errors
- slice 13i done: herorockslide mine+forcemove; mock230_pack 0 errors
- slice 13j done: grail realm thin NPCs (fisherman/maiden/percival/peasants); mock230_pack 0 errors
- slice 13k done: fisher_king Talk-to; mock230_pack 0 errors
- slice 13l done: grail_crone + high_priest grail arm; mock230_pack 0 errors
- slice 13m done: sir_percival; mock230_pack 0 errors
- slice 13n done: grail_journal + quest_holygrail wire; mock230_pack 0 errors (3400 scripts)
- slice 13o done: black_knight_titan Talk-to + Excalibur death gate + drops; mock230_pack 0 errors (3404 scripts)
- slice 13p done: kaqemeex + %druidquest + quest_druid complete/cauldron; mock230_pack 0 errors (3419 scripts)
- slice 13q done: sanfew Druidic Ritual arms (Eadgar deferred); mock230_pack 0 errors (3427 scripts)
- slice 13r done: fletching bolts/darts (cat 530=bolttips, 969=dart_tips); mock230_pack 0 errors (3433 scripts)
- slice 13s done: druid_journal + quest_druidicritual wire; mock230_pack 0 errors (3434 scripts)
- slice 13t done: crystal_chest + crystal_key (join halves + loot); mock230_pack 0 errors (3441 scripts)
- slice 13u done: gaius Talk-to + Trade stub; trail clue deferred; mock230_pack 0 errors (3443 scripts)
- slice 13v done: jatix Talk-to + Trade stub; mock230_pack 0 errors (3445 scripts)
- worn-tab value checker + items kept on death (2026-08-03): pricechecker.rs2 (high-alch), deathkeep preview, on-death→gravestone, coffer/office; oc_cost/tradeable/members + inv_dropall/obj_addall; RCS arity 28; mock230_pack 0 errors
- slice 13w done: taverly druid AI + snare/entangle dbrows; mock230_pack 0 errors
- slice 13x done: ballboy + %ballquest constants/varp + complete; house locs deferred; mock230_pack 0 errors
- slice 13y done: yanille thin NPCs (sigbert/radick/tower_guard/frumscone/ogre_chieftan/guild_wizard); mock230_pack 0 errors
- slice 13z done: burthorpe thin NPCs + ^death_complete/%death_equiproom; mock230_pack 0 errors (3543 scripts)
- slice 14a done: yanille shop stubs (frenita/magic_store/ogre traders + retaliate); mock230_pack 0 errors (3555 scripts)
- slice 14b done: witch house locs + experiment chain + witchrat/experiment coords; mock230_pack 0 errors (3578 scripts)
- slice 14c done: death plateau thin soldiers/archers Talk-to; mock230_pack 0 errors (3595 scripts)
- slice 14d done: saba + eohric + %death_map authored 5767 + progress constants; mock230_pack 0 errors (3603 scripts)
- slice 14e done: Denulth + %death_bits authored 5768 + death_quest_complete; troll_quest deferred; mock230_pack 0 errors (3618 scripts)
- slice 14f done: Tenzing Death Plateau arms + post-quest boots; troll_love deferred; mock230_pack 0 errors
- slice 14g done: Dunstan Death Plateau arms; troll/sled/law tali deferred; mock230_pack 0 errors (3632 scripts)
- slice 14h done: Harold drink/duty/combo + inline gamble (no death_dice IF1); mock230_pack 0 errors
- slice 14i done: witches_diary mesbox pages + mouse-door varp; IF1 book deferred; mock230_pack 0 errors (3642 scripts)
- slice 14j done: nora_t_hagg ai_spawn/ai_timer + patrol overlay; mock230_pack 0 errors (3644 scripts)
- slice 14k done: death_iou→combination + scout zone; doors/mechanism deferred; mock230_pack 0 errors
- slice 14l done: death_journal + ball_journal questlist wires; mock230_pack 0 errors (3649 scripts)
- next pending: Death Plateau doors/stone mechanism or burthorpe leftovers; skip blocked: scorpcatcher quest, wilderness_chaos_druid (Elder), Mort'ton lair, pyre, kolodion_fight, antifire, guard2, ditch, shops inv.ini, npc_poison varn, imp teleport, barcrawl

- equip BAS parallel queue: docs/EQUIP_BAS_PORT_QUEUE.md (slices 0–9 done)
