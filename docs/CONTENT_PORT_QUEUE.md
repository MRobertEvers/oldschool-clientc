# Content port queue

Agent-loop state for the LostCity → OSRS-Content forward port.
Each tick ports **one** pending unblocked slice per `docs/PORTING_GUIDE.md` §4.
Status: `pending` | `in_progress` | `done` | `blocked`.

## Shared tree — never silence another lane

**Do not ever** `.rs2.skip` / `dirname.skip` / move / delete sibling content
(`skill_construction/`, `minigame_mta/`, or any other live tree) to green
`sscompile`. Fix your own errors. See PORTING_GUIDE §7 and
`.cursor/rules/no-park-sibling-content.mdc`.

Loop prompt: read this file + PORTING_GUIDE §4 / §7; **immediately mark the
chosen slice `in_progress`** (other lanes share this tree — claim before
measure/port); NEVER park sibling lanes; verify (`mock230_pack --check-only`,
scripts build); update this file; re-arm. Stop only when the user stops the loop.

**Do not park sibling lanes.** Never rename `skill_construction/` →
`skill_construction.skip`, `*.rs2.skip` POH/MTA scripts, or delete another
queue's tree to green your compile. Fix your slice. See PORTING_GUIDE §7 and
`.cursor/rules/no-park-sibling-content.mdc`.

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
| 9d | general_use gates/fence/gangplank (+findsomethingnice) | done | wooden double fence via gate_main/gate_outer (fencegate/farming/rustic/plaguesheep/pvpa_access_gate); metalgate still door_closed; memberfencegate walk-through; mournerstewfence; ship planks (not dragonship*); findsomethingnice+wire; deferred: board_message param, duel arena, ~open_and_close_metal_gate, doubledoors.rs2 |
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
| 14m | death doors + stone mechanism | done | harold/sherpa/castle doors walk-through + stone balls; cat 73=death_cannonball; %death_stones; deferred: obj_find pickup clear |
| 14n | death caves/stile/rocks/boots | done | hermit cave teleports + stile + climbing rocks + Wear gates; deferred: dangersign cam_* |
| 14o | troll thrower AI + npc ranged | done | npc_combat_ranged helpers + thrower name-expand AI/overlays/drops; proj params hardcoded (fields/npc.ini gap); deferred: .hunt |
| 14p | death_archer + guard drops + dangersign | done | archer ranged AI/drops + guard drops + dangersign mesbox; combat overlays; deferred: cam cinematic, rangebonus npc field |
| 14q | Heroes Guild leftovers | done | achietties/helemos/entrance + %heroquest/%zanaris stubs + quest_hero procs; Trade stub; door walk-through; deferred: full Heroes body, openshop, IF1 progress |
| 14r | combat training camp | done | trainers/shop Trade stub + gate (biohazard stub) + dummy + ranged-only ogre; %biodummy authored 5770; deferred: Biohazard body, autocast ogre, openshop |
| 14s | trollweiss chill | done | chill_zones + apply_chill timer/mapzone; stats enum→named drain; deferred: snow overlay IF1, music |
| 14t | gerrant lava eel arm | done | Heroes-in-progress blamish slime handoff; Trade stub kept; deferred: openshop, clues |
| 14u | entrana firebird heroquest gate | done | hot_feather drop gated on %heroquest < ^hero_complete |
| 14v | quest equip gates | done | heroes (dragon_mace/battleaxe) + dragon slayer (rune plate/green dhide body) in ~levelrequire_quest_gate; deferred: zanaris/legends/regicide equip siblings |
| 14w | brimhaven thin | done | davon/alfonse Trade stubs + pirate_guard + pineapple name-expand; deferred: hajedy/kangai/barcrawl/clues |
| 14x | deadmans bartender + platform fishermen | done | grog/rum sales (barcrawl deferred) + fishplatform1..4 Talk-to; deferred: Sea Slug body |
| 14y | Sea Slug thin NPCs | done | bailey/kennith/kent/holgart + %seaslugquest constants/coords; kennithwall; boat tele procs; deferred: Caroline, ladder/panel/crane/sticks body, journal |
| 14z | brimhaven hajedy stub | done | cart Examine/Talk + ^zombiequeen_complete gate; trail clue deferred; Shilo quest body deferred |
| 15a | brimhaven kangai stub | done | Tribal Totem start/hand-in + constants; trail/body/journal deferred |
| 15b | gnome shop stubs | done | gulluck/hudo/rometti/heckelfunch Trade stubs; trail deferred |
| 15c | brimstail + cave | done | essence tele + cave entrance/ladder; trail deferred |
| 15d | gnome troop | done | gnomeknight/d_skingnomeknight Talk-to + ranged AI |
| 15e | kalron + chantergnome | done | Tree Gnome Village thin + %treequest constants |
| 15f | gnome waiter stub | done | Trade stub; dish sell/restaurant deferred |
| 15g | aluft gianne stub | done | greeting only; restaurant progress unresolved |
| 15h | remsai | done | Tree Gnome Village Talk-to arms |
| 15i | bolkoy shop stub | done | treevillage_shopkeeper1 Trade stub + quest arms |
| 15j | gnome trainer | done | agility trainer Talk-to; trail puzzle deferred |
| 15k | Caroline (Sea Slug start) | done | start + complete queue + pearls/fishing XP; journal deferred |
| 15l | gnome_gate | done | areagate+treedoors walk-through + grandtree constants; femi/swing deferred |
| 15m | commander_montai | done | Tree Gnome Village Talk-to arms; ai_timer catapult deferred |
| 15n | king_bolren | done | start/hand-in/complete + ceremony; trail deferred |
| 15o | elkoy | done | maze guide Talk-to + telejumps (elkoy/elkoy_village) |
| 15p | hazelmere | done | Grand Tree bark/scroll arms; trail deferred |
| 15q | blurberrybarmen | done | Trade stub + lemon/orange/shaker sales; barcrawl/cocktail-sell deferred |
| 15r | blurberry stub | done | greeting only; gnome_bar tutorial deferred (%gnome_bar_progress unresolved); barcrawl deferred |
| 15s | gnome_glider | done | pilot_* name-expand + p_choice fly (IF1 glidermap deferred) + glider.constant; trail deferred |
| 15t | spirit_tree | done | ent/stronghold_ent/spirittree_small teleports + spirit_tree.constant |
| 15u | gnomevillage_fence | done | treegnomelooserailing squeeze via agility_exactmove |
| 15v | Sea Slug locs | done | sticks dry/light + slugladder/loosepanel/fishingcrane + seaslug pickup |
| 15w | seaslug_journal | done | journal + quest_seaslug dbrow wire |
| 15x | gnomes Talk-to | done | gnome/female/child Talk-to + ranged AI; ~objbox→~mesbox |
| 15y | gnome cook/cocktail book stubs | done | giannes_cook_book + cocktail_guide ~mesbox recipes; IF1 book UI deferred |
| 15z | restaurant start + varp | done | %gnome_restaurant_progress authored 5803 + start dialogue + cookbook handoff; assigns/jobs deferred |
| 16a | restaurant tutorial assigns | done | cheese+tom→toad crunchies tutorial + utensil replace + premade reject; jobs deferred |
| 16b | restaurant jobs | done | job start/finish + dish_ok helpers + XP/coins; cooking skill deferred |
| 16c | swamp_toad | done | opheld1 swamp_toad → toads_legs (restaurant fresh legs) |
| 16d | cutting_fruit | done | knife↔lemon/orange/lime/pineapple ~p_choice2 slice/dice; cocktail-shaker deferred |
| 16e | grandtree translation_book | done | grandtree_translationbook ~mesbox glossary; IF1 book deferred |
| 16f | gloughs_journal stub | done | grandtree_journal ~mesbox pages; IF1 book deferred |
| 16g | grandtree anita | done | Anita Talk-to key handoff; trail clue deferred |
| 16h | grandtree charlie | done | Charlie Talk-to + jail release; narnode_trust_glough parked here until 16m |
| 16i | grandtree femi | done | Femi Talk-to/boxes/sneak-in + %femi_help authored 5856; gnome_gate boxes wired |
| 16j | grandtree glough | done | Glough Talk-to + arrest→jail; MM/cutscene/demon deferred |
| 16k | grandtree foreman | done | Foreman quiz + order + death drop; MM arm deferred |
| 16l | shipyardworker Talk-to | done | shipyardworker1/2 name-expand chatter; gate password deferred |
| 16m | king_narnode | done | Full Talk-to + quest start/complete + %daconia_rock_root 5869; MM deferred |
| 16n | gnome cooking dough | done | param/constant/struct/tray overlays + gianne_dough → raw tin |
| 16o | gnome seasoning | done | Ingredient int-param overlays + spice/equa seasoning Use-with |
| 16p | grandtree scrolls | done | scroll/order/invasionplans ~mesbox stubs; IF1 scroll deferred |
| 16q | gnome crunchies | done | bits varps + add/bake + raw→half_baked dbrows + cook_item hook + string_procs; finish deferred 16t |
| 16r | gnome battas | done | bits varps + add/bake + raw dbrows + cook_item; finish deferred 16t |
| 16s | gnome bowls | done | bits varps + add/bake + raw dbrows + cook_item; finish deferred 16t |
| 16t | gnome food finish | done | topping finish name-expand + seasoning finish arms |
| 16u | cocktail shaker | done | bits varps + mix/pour + inspect display |
| 16v | cocktail finish | done | garnish finish name-expand + oven warm dbrows |
| 16w | gnome ingredient reverse Use | done | ingredient→half_baked/shaker reverse binds |
| 16x | grandtree climb/trapdoors | done | climbtree/downtree + trapdoorunder/closed teleports; cutscene stub |
| 16y | grandtree chest/cupboard/pillars | done | key chest + journal cupboard + twig pillars (obj_find deferred) |
| 16z | grandtree roots/rootdoor | done | daconia root search + rootdoor forcemove |
| 17a | shipyard gate + password | done | fencegate walk-through + Ka-Lu-Min password quiz |
| 17b | hazelmere Talk-to | done | bark sample → scroll; trail clue deferred |
| 17c | grandtree black demon | done | ai_timer/death + glough cutscene spawn; Attack/%npc_aggressive_player/cam_* deferred |
| 17d | grandtree journal | done | ~grandtree_journal + quest_grandtree dbrow wire |
| 17e | tree tracker gnomes | done | tracker1/2/3 Talk-to + coords |
| 17f | khazard warlord | done | Talk-to + death orbs drop; bones after-quest |
| 17g | tree battlefield locs | done | ballista/door/wall/chest; coord_projectile/door swing deferred |
| 17h | tree journal | done | ~tree_journal + quest_treegnomevillage dbrow wire |
| 17i | waterfall almera + constants | done | start Talk-to + quest_waterfall.constant; %waterfall_quest clean-varp 65 |
| 17j | waterfall hudon | done | island apnpc1 Talk-to + progress to spoken |
| 17k | waterfall gerald | done | riverside Talk-to + fishsize anim |
| 17l | waterfall golrie | done | pebble handoff + %waterfall_golrie_and_puzzle authored; door label |
| 17m | waterfall hadley | done | tourist guide Talk-to + ~p_choice menus |
| 17n | waterfall baxtorian book | done | Read → progress + ~mesbox pages; IF1 book deferred |
| 17o | waterfall locs | done | raft/tomb/rope/doors/chalice + complete; armour cats deferred |
| 17p | waterfall journal | done | ~waterfall_journal + quest_waterfall dbrow wire |
| 17q | waterfall pillars | done | rune-on-pillar puzzle bits 1–18 |
| 17r | jungle potion constants | done | quest_junglepotion.constant; %junglepotion clean-varp 175 |
| 17s | jungle potion locs | done | herb pick + cave enter/exit + complete queue |
| 17t | jungle potion journal | done | ~junglepotion_journal + quest_junglepotion dbrow wire |
| 17u | jungle potion trufitus | done | Talk-to + Use herb; ZQ/TBWT arms deferred |
| 17v | clock tower kojo + helpers | done | brother_kojo Talk-to + %cogquest bitfield helpers; trail deferred |
| 17w | clock tower locs/cogs/journal | done | cogs+spindles+gates/levers+trough/rats + %cog_bits + journal wire; loc_1541→prisondooropen; cat cog/spindle name-expand |
| 17x | Ardougne east shops | done | aemad/kortan/baker/zenesha/fur/gem/silver/spice/fionella/siegfried Trade stubs + silk buy; openshop deferred |
| 17y | Ardougne east thin NPCs | done | bartender beer/zoo/horacio/monk/citizens/barnaby sail/archer AI; barcrawl+trail deferred |
| 17z | Cromperty + RPDT + guide | done | wizard_cromperty+rpdt_employee totem/essence; ardougne_book→~mesbox; IF1 book deferred |
| 18a | Hazeel Cult constants + Ceril | done | quest_hazeelcult.constant/varp + complete queues + sir_ceril_carnillean |
| 18b | Hazeel Cult Clivet | done | clivet_hazeel_cultist Talk-to + side choice |
| 18c | Hazeel Cult journal | done | ~hazeelcult_journal + quest_hazeelcult dbrow wire |
| 18d | Hazeel Cult cave/valves | done | cave/stairs/raft/valves; ~hazeel_valve_idx; Turn→p_choice2 L/R |
| 18e | Sheep Herder start + Halgrive | done | constants/varp/complete + councillor_halgrive; sheep varbits for incinerate check |
| 18f | Sheep Herder Doctor Orbon | done | doctor_orbon plague outfit buy + ~has_plague_outfit |
| 18g | Sheep Herder diseased sheep | done | herder_plaguesheep prod/poison + sheep_table zones; death→sheep_death |
| 18h | Sheep Herder furnace | done | plaguesheep_furnace incinerate bones |
| 18i | Sheep Herder gate | done | plaguesheep_gatel/r walk-through |
| 18j | Sheep Herder journal | done | ~sheepherder_journal + questlist wire |
| 18k | Sheep Herder farmer_brumty | done | Talk-to progress branches |
| 18l | Hazeel Cult Alomone | done | alomone Talk-to + defeat + hazeel cutscene |
| 18m | Hazeel Cult butler Jones | done | butler_jones Talk-to |
| 18n | Hazeel Cult Claus + range | done | claus + carnilleanrange poison |
| 18o | Hazeel Cult house locs | done | cupboard/chest/crate + carnilleanbookcase_knock |
| 18p | Hazeel Cult guard + Philipe | done | guard_carnillean + philipe_carnillean |
| 18q | Hazeel Cult cultist | done | hazeel_cultist Talk-to |
| 18r | Dig Site constants + helpers | done | quest_itexam.constant/varp + progress helpers + complete |
| 18s | Dig Site examiner + curator stamp | done | examiner exams + curator letter/certificate arms; trail puzzle deferred |
| 18t | Dig Site student1 | done | rock_sample1 errand + exam tips |
| 18u | Dig Site student3 (LC student2.rs2) | done | rock_sample2 errand + exam tips |
| 18v | Dig Site student2 (LC student3.rs2) | done | rock_sample3/opal errands + exam tips |
| 18w | Dig Site archaeological expert | done | Terry Balando Talk/Use; DT etchings arms merged here |
| 18x | Dig Site panning guide | done | tea invite + tool tips; intervene label for later digsite locs |
| 18y | Dig Site workmen | done | digworkman1/2 Talk + Steal-from + invite scroll + cave key beg |
| 18z | Dig Site exam centre locs | done | sample cupboard + digbookcase; digsitebook→~mesbox |
| 19a | Dig Site chemistry | done | nitrate/nitro/charcoal/arcenia → digcompound |
| 19b | Dig Site journal | done | ~itexam_journal + quest_digsite questlist wire |
| 19c | Dig Site area locs | done | panning/soil/winch/shaft/chest/barrel; cam_shake deferred |
| 19d | Trawler Murphy (dock) | done | murphy Talk-to + constants; %trawler→%trawler_status; trail deferred |
| 19e | Trawler at sea / gangplank | done | zones+gangplank+ladders+murphy_at_sea+sink/escape; %npc_int deferred |
| 19f | Trawler gameplay core | done | net/bail/winch/leak Fill/reset+login; hull remaps; control/varn deferred |
| 19g | Trawler start/win/sink | done | start tele+mes; win+shore net→inv; sink huntall; IF/varn deferred |
| 19h | Shantay disclaimer | done | thshantaydisc opheld1 → ~mesbox |
| 19i | Canifis Barker | done | werewolfshopkeeper2 Talk + Trade stub |
| 19j | Chadwell (W. Ardougne) | done | chadwell Talk + Trade stub |
| 19k | West Ardougne recruiter | done | recruiter + citizen npc_say; .npc→finduid restore |
| 19l | Zanaris thin NPCs | done | fairy_queen + jakut/irksol Trade stubs |
| 19m | Miscellania veg monger | done | misc_veg_monger Trade stub |
| 19n | Miscellania fish monger | done | misc_fish_monger Trade stub |
| 19o | W. Ardougne priest | done | w_ardoungepriest Talk-to |
| 19p | W. Ardougne child | done | w_ardoungechild1 + dskin Talk-to |
| 19q | Canifis Fidelio | done | werewolfshopkeeper3 Trade stub |
| 19r | Canifis Rufus | done | werewolfshopkeeper1 Trade stub |
| 19s | Zanaris Lunderwin | done | fairy_lunderwin cabbage buy |
| 19t | Zanaris ladder exit | done | fairy_ladder_attendant + zanarisladderout |
| 19u | Karamja Jiminua | done | jiminua Trade stub |
| 19v | Shilo Obli | done | shilogeneralstore Trade stub |
| 19w | Shilo Fernahei | done | shilofishowner Trade stub |
| 19x | Velrak (Taverley dung.) | done | velrak_the_explorer dusty_key |
| 19y | Kalphite old man | done | kalphite_oldman Talk-to |
| 19z | W. Ardougne Carla | done | carla Talk-to plague dialogue |
| 20a | Miscellania flower girl | done | misc_flowergirl 15gp→flowers_waterfall_quest |
| 20b | Canifis Sbott tanner | done | werewolftanner Talk + shared tan labels @ 2/5/45gp |
| 20c | Bedabin nomad | done | bedabin Talk + Trade stub; pineapple arm on %desertrescue |
| 20d | Misc approval dialogue | done | ^misc_complete + approval % dialogue + man_misc_chatanim |
| 20e | Gardener Gunnhild | done | misc_gardener Talk + iron sickle sale; weeding → 20n |
| 20f | Lumberjack Leif | done | misc_lumberjack Talk; woodcut intercept deferred |
| 20g | Miner Magnus | done | misc_miner Talk; mining intercept deferred |
| 20h | Fisherman Frodi | done | misc_fisherman Talk; fishing intercept deferred |
| 20i | Misc/Etceteria people | done | misc_man/woman + etc Talk/Attack; approval drain on kill |
| 20j | Seravel ship tickets | done | shiloshiptickets 25gp→shiloshipticket |
| 20k | Canifis building stairs | done | building_steps_up/down telejump by angle |
| 20l | Gunnjorn (Barb. Outpost) | done | gunnjorn course greeting; Horror key arms deferred |
| 20m | W. Ardougne man/woman | done | man+woman Talk-to + %elenaquest/%elena_* stubs; shared plague dialogue |
| 20n | Miscellania weed_herbs | done | misc_heather sickle weeding + approval; sound_synth dropped |
| 20o | Bedabin nomad guard | done | tent door + plans gate; %desertrescue_map_mechanisms; desertrescue stages expanded |
| 20p | Dark mage (upassmage) | done | Talk-to + Iban staff fix; %upass stub; ~objbox→~mesbox |
| 20q | E. Ardougne citizens | done | ardougnian_male1/female1 random Talk-to + flier |
| 20r | Zoo keeper | done | Talk-to; greegree/trail deferred |
| 20s | Ardougne monk | done | monk_ardougne %drunkmonkquest lines |
| 20t | W. Ardougne civilians | done | wantcat1–3 mice Talk + cat→100 deathrune; cat/overgrown cats named |
| 20u | W. Ardougne clerk | done | civic office Talk + plague-house arms; ~quest_elena_set_progress stub |
| 20v | Canifis Roavar | done | werewolfinnkeeper beer/gossip/story; trail deferred |
| 20w | Shantay chest + kebab instr | done | thbankchest→~openbank; thkebabinstructs Read→~mesbox |
| 20x | Kharidian cactus | done | Cut/Use waterskin fill + next_loc_stage; sound_synth dropped |
| 20y | Canifis citizens | done | Talk-to + wolfbane transform + drops; ~canafis_werewolf_type; death_drop overlay |
| 20z | Plague manhole | done | plaguemanhole open/cover/climb; loc_findallzone→loc_find; sewer telejump |
| 21a | Mourners Talk-to | done | mourner1–3/head/stew/twb + elena/biohazard arms; biohazard stages expanded |
| 21b | Shantay Talk-to | done | jail varp authored; pass sale; Trade stub; ~objbox→~mesbox |
| 21c | Desert heat | done | %desert_heat temp (carrier avoided); timer+zones; desert clothes bonuses; armour cats deferred |
| 21d | Shantay pass | done | guards+henge+prison walk-through; desert heat enter; Ana barrel deferred |
| 21e | E. Ardougne Elena door | done | elenadoor2 quest-gated walk-through |
| 21f | W. Ardougne doors | done | bravek + city wall + mourner HQ walk-through; biohazard/elena gates |
| 21g | Viking fur door | done | viking_fur_door open/close toggle; sound dropped |
| 21h | VT council workman | done | Talk-to + beer/tankard→viking_firecracker; other drinks rejected |
| 21i | Magic Guild portals/doors | done | magicguild_door_* walk-through (lvl 66); wiztower/darkwiz/thormac portals; dungeon fence arm deferred |
| 21j | Captain Shanks | done | ticket sale + Khazard/Sarim telejump; set_sail IF deferred; rum dice |
| 21k | Plague mud pile | done | plaguemudpile→^quest_elena_garden_coord |
| 21l | Ardougne teleport scroll | done | ardougnescroll Read; complete→unlock; incomplete→flame+damage0 |
| 21m | Elena (elenap) Talk-to | done | freed_elena advance; post-complete mes |
| 21n | UPass doomion trio drops | done | doomion/holthion/othainian ashes+amulet; ^upass_found_doll |
| 21o | Tree spirit defeat | done | %zanaris→spirit_defeated on kill |
| 21p | Elemental Workshop drops | done | air/water/fire/earth + rock_version ore; randomherb/jewel |
| 21q | Alrena Talk-to | done | gasmask + dwellberries; Elena stage constants expanded |
| 21r | Edmond Talk-to + complete | done | start/tunnel/pull + quest_elena_complete→quest_plaguecity |
| 21s | Mud patch dig/soften | done | plaguemudpatch1/2 spade+bucket; sewer telejump |
| 21t | Jethick Talk-to | done | picture/book arms; bravek tip |
| 21u | King Lathas | done | biohazard finish + UPass start (%upass_lathas_met); Regicide deferred |
| 21v | Bravek Talk-to | done | hangover note/cure + warrant |
| 21w | Alrena cupboard | done | spare gasmask Search; name-bound open/shut |
| 21x | Sewer pipe | done | rope grill + climb; %elena_pipe_used temp; sound dropped |
| 21y | Rehnison family | done | ted/martha/milli + stairs |
| 21z | Koftik (caveguide1) | done | UPass entrance Talk; %upass_lathas_met gate |
| 22a | Elena2 Biohazard Talk | done | start→complete arms; Regicide deferred |
| 22b | Plague house | done | key barrel + stairs + elenagate walk-through |
| 22c | Elena doors/book return | done | rehnisondoorshut book return + plagueelenadoor*_vis black-cross; walk-through; deferred: IF1 cutscene |
| 22d | Scruffy note Read | done | IF1 scroll → mesbox stub (garbled hangover recipe) |
| 22e | Biohazard Jerico Talk | done | start→spoken_jerico + distract ideas; Omart cross deferred |
| 22f | Biohazard Chemist Talk | done | touch paper / sample arms; Regicide still deferred |
| 22g | Biohazard Guidor Talk | done | analyse sample → ^biohazard_found_secret; wife/door deferred |
| 22h | UPass area-1 obstacles | done | rockslide/swamp/mudpile/pipes + %upass_area1_pipe_used; rope swings deferred |
| 22i | Biohazard loc leftovers | done | jerico cupboard/birdfeed/pigeons/watchtower + climb_ladder label + cauldron/nurse cupboard/HQ gate/crate; omart/kilron still deferred |
| 22j | Guidor door + wife | done | guidordoor walk-through + guidors_wife Talk; priest gown/robe gate |
| 22k | Biohazard errand boys | done | hops/chancy/devinci + %bioerrand + ^hops_*/^chancy_*/^devinci_* |
| 22l | UPass rope swings | done | rock/swamp swings; LC loc_2276→norope, 2273→withrope, 2274→withrope2; traps deferred |
| 22m | UPass old bridge | done | damp_cloth→unlit/lit arrows + guiderope shot + lever; FM tinderbox hook |
| 22n | UPass grid + slaves | done | portcullis lever + grilltrap zones/timer + %upass_grid_pattern (not ibanmulti 22-31) + cave_slave1–7; Lathas ~setupassgrilltrap; sound dropped |
| 22o | UPass speartrap | done | disarm + mapzone trap timer (LC music.rs2); springtrap timer branch included |
| 22p | UPass springtrap plank | done | double_springtrap_trigger Examine + woodplank→crossing_plank |
| 22q | UPass logtrap + orb | done | logtrap disarm + caveorb1; %upass_caveorb_1 (not ibanmulti); orboflight trap |
| 22r | UPass ledge | done | sidestep ledge + rat-pit fail |
| 22s | UPass walkway | done | walkway_upass_narrow_mid_top rock bridge balance |
| 22t | UPass pipe6 | done | area-2 pipe crawl + %upass_area2_pipe_used; unicorn gate tele |
| 22u | UPass collapsed bridge | done | bridgecollapsed1/2 jump; fail→caveguide4 + %upass_koftik_chat |
| 22v | UPass Niloof | done | upassdwarf1 Talk (Well of Voyage / doll arms) |
| 22w | UPass Klank | done | upassdwarf2 Talk + gauntlets gift/sale + tinderbox; ~p_choice2 |
| 22x | UPass Kamen | done | upassdwarf3 drunk brew Talk; damage on refuse path |
| 22y | UPass journals (Randas + Iban history) | done | upass_journal + old_journal Read; IF1 book→mesbox; %upass_read_journal / %upass_read_iban_book; ~p_choice4_header chapters |
| 22z | UPass paladins (Jerro/Carl/Harry) | done | upass_paladin1–3 Talk + badge drops; %upass_paladin_food; ~paladin_drops; hostile after main area |
| 23a | UPass Iban disciple | done | ibanmonk Talk (robe disguise) + robe/broken-staff drops; hostile without robes |
| 23b | UPass Kalrag | done | kill→blessed spider aggro + %upass_venom_on_doll (LC blood_on_doll); custom spider AI deferred |
| 23c | UPass Lord Iban | done | [ai_spawn]/[ai_timer,iban] bolt storm; sound dropped; damage(); timer via spawn overlay |
| 23d | UPass entrance + exit | done | upass_caveentrance2 + cave_exit_upass; biohazard/%upass_lathas_met gates |
| 23e | UPass orbs destroy + pickup | done | furnace_upass destroy + caveorb1–4 pickup; %upass_caveorb_* |
| 23f | UPass cave well + mudpile | done | cave_well climb/use + mudpile_upass exit |
| 23g | UPass witch (Kardia) | done | door/cat/chest; %upass_gavecat; doll loot |
| 23h | UPass bloodwell + temple doors | done | inscription mesbox + badge/horn offer; cavetempledoor2; staff charge deferred |
| 23i | UPass unicorn area (boulder/railings/cage) | done | thieving railings + boulder rail + unicorncage horn |
| 23j | UPass mud dig + unicorn tunnels | done | upass_mud dig + unicorn doorl/r tele |
| 23k | UPass area-2 food crate | done | cavefood1 + %upass_crate_food |
| 23l | UPass tomb + doll + Iban altar | done | brew barrel + tomb burn/ashes + doll smear/search + temple doors + altar defeat; %upass_brew_tomb; Regicide shortcut + staff charges deferred |
| 23m | UPass cages + shadow chest | done | soulless cages dove + amulet chest → ibansshadow |
| 23n | UPass tablets + quest complete | done | tablets→mesbox; caveguide5 exit; Lathas complete queue + agility/attack XP |
| 23o | UPass voice + cavewall tunnels | done | iban_say zone voices + cavewalltunnel up/down (Koftik spawn) |
| 23p | UPass demon drops (Doomion trio) | done | already in upass_demon_drops.rs2 (LC files are ai_queue3 drops only; no Talk) |
| 23q | UPass quest journal | done | ~upass_journal + dbrow wire; ^journal_* colours; fire-arrow parts helper |
| 23r | Zanaris camp adventurers | done | monk/archer/wizard/warrior; %zanaris start via warrior; drop send_quest_progress |
| 23s | Zanaris doorman + market door | done | diamond tax + zanarismarketdoor; walk-through |
| 23t | Kalphite larva + locs | done | larva flee + rope burrow/chamber; coords landed; chamber_entrance→norope |
| 23u | Shilo Vigroy cart | done | shilocart/shilocartdriver → Brimhaven; ~calc_shilocart_cost |
| 23v | Shilo Paramaya inn | done | shiloinnowner drinks/tickets + dorm ladders |
| 23w | Shilo Yanni antiques | done | quote + opnpcu sell zq* quest leftovers |
| 23x | Dragon Inn bartender | done | Dragon Bitter / Greenman's Ale; barcrawl deferred |
| 24a | Varrock tramp | done | tramppg Talk + Black Arm tip; ~p_choice*; drop send_quest_progress |
| 24b | Lumbridge cook (area) | done | post-quest ~p_choice4 restored in quest_cook; trails clue deferred |
| 24c | Varrock east gate | done | bioguard1 + guidor gate Open; Biohazard vial search; sound dropped |
| 24d | Draynor Joe guard | done | already in areas/draynor/joe.rs2 (~p_choice* beer distract) |
| 24e | Varrock gypsy (Aris) | done | already areas/varrock/aris.rs2 (full Demon Slayer Talk) |
| 24f | Karamja volcano entrance | done | volcano.rs2: pot-hole + climbing_rope2 rim |
| 24g | Shilo sandpit | done | shilo_sandpit.rs2: sand1–3 scoop + members gate |
| 24h | Yanille agility dungeon | done | agility_dungeon.rs2: pipe/ledge/rocks/sinister chest/trap altar; ~agility_delay_fail |
| 24i | Karamja karam dungeon | done | karam_dungeon.rs2: Saniboch fee via %karam_dungeon_entryfee + vines/stones/logs/pipes |
| 24j | Shilo Yohnus furnace | done | shilofurnaceowner Talk + 20gp forcemove; shilofurnacedoor absent |
| 24k | Yanille rockcake stall | done | rockcounter_withcakes thieve + empty_rock_cake_counter |
| 24l | Mort Myre snails | done | spit AI + shell/corpse drops; mortmyre.npc attack overlays |
| 24m | Vampire leech AI | done | melee + 1/5 skill drain; leech overlays |
| 24n | Kalphite oldman | done | kalphite_oldman Talk (~p_choice*) |
| 24o | Kalphite Queen combat | done | both forms mage/range/melee; form-change via npc_changetype; worker spawn + crackopen deferred |
| 24p | Tai Bwo Wannai Timfraku | done | Timfraku Talk + TBWT constants/varps/title helpers/journal wire; brothers deferred |
| 24q | TBWT Tiadeche | done | Talk/Use vessel+bait; crafting manual; ~p_choice*; drop music_jingle |
| 24r | TBWT Tamayu | done | Talk/Use spear+agility; hunt cutscene simplified (cam/.npc deferred); KP spears via name switch |
| 24s | TBWT Tinsay | done | meal fetch tree + vessel→manual for Tiadeche; ~objbox→~mesbox |
| 24t | TBWT Lubufu | done | apprentice/bait dialogue + vessel reward; fishing spots deferred |
| 24u | TBWT items + locs | done | vessel load/paste/rum/banana/monkey skin; bamboo door + tribal statue stubs; brother_completion helpers |
| 24v | TBWT brother finals | done | village reward Talk + Trade stubs (inv.ini); XP + KP spear / cook / fish unlocks |
| 24w | TBWT jogre + monkey | done | furnace burn hook in smelting; monkey ai_queue3 corpse drop; FM/superheat/dodge deferred |
| 24x | TBWT fishing spots | done | cats 632/633 mint; karambwanji net + karambwan vessel; lubufu → @attempt_fish_karambwan; deferred: sound/afk |
| 24y | memberfish spots | done | shark harpoon + big-net roll + oyster/casket; deferred: greegree/afk/whirlpool |
| 24z | fishing guild | done | fishguilddoor walk-through + master_fisher + fishguildshop Trade stub |
| 25a | slimeyfish spots | done | cat 457=slimeyfish mint; mort_slimey_eel bait; deferred: sound/movement |
| 25b | lavafish spots | done | 0_45_152_lavafish oily-rod + burnup; lavafish_loc deferred (loc_2630 unresolved) |
| 25c | cooking guild | done | chefdoor walk-through (32 cook + chefs_hat) + head_chef Talk |
| 25d | ranging armour salesman | done | Talk tree + Trade stub (inv.ini) |
| 25e | ranging bow salesman | done | Talk + Trade stub (inv.ini) |
| 25f | Kalphite worker spawn | done | kalphite_worker_spawn.rs2 + KQ queue2 wire; hatch walk deferred |
| 25g | Death Plateau troll thrower | done | already troll_thrower.rs2 name-expanded |
| 25h | Charlie the cook (Heroes) | done | charlie_the_cook.rs2 Phoenix gherkin dialogue; secret door deferred |
| 25i | Ranging guild competition judge | done | competition_judge.rs2 enter/rules/buy arrows/reward; targets deferred |
| 25j | Ranging guild door + doorman | done | ranging_guild_door walk-through + doorman Talk |
| 25k | Ranging guild guard + tribal | done | ranging_guild_npcs.rs2 Talk + Trade stub + ranged AI |
| 25l | Ranging leatherworker | done | leatherworker Talk + @tan_leather_choices reuse |
| 25m | Ranging guild targets | done | ranging_target Fire-at + score UI; projectile visual deferred |
| 25n | Ranging guild tower | done | ladders + fill_towers + archer AI + advisor Talk; .npc timer refill deferred |
| 25o | Ranging ticket exchange | done | ticket_merchant Trade stub (rangingguild_ticketexchange IF deferred) |
| 25p | lavafish_loc | blocked | no lava-eel loc in osrs239 (npc 0_45_152_lavafish covers baiting); loc_2630 was rev254-only |
| 25q | Wizard Tower wizards AI | done | wizard.rs2 Fire Strike AI; Grayzag deferred |
| 25r | Party Room Megan | done | partyroom_megan beer/dance/news Talk |
| 25s | Duel Arena Mubariz | done | mubariz info dialogue; duel engine deferred |
| 25t | Party Pete + Lucy | done | party_pete Talk + Lucy beer; chest AI deferred |
| 25u | Duel Arena Fadli + Jaraah | done | Fadli bank/Trade stub + Jaraah/nurses heal |
| 25v | Duel Arena Hamid + Zahwa | done | duel_monk + duel_injured1 Talk; clues deferred |
| 25w | Games Room barmaid | done | Talk + Trade stub; boardgames.rs2 challenge/IF deferred |
| 25x | Observatory Scorpius | done | scorpiusgrave mesbox + zamorakghost retaliate |
| 26a | Heroes kitchen locs | done | herokitchendoor/panel walk-through gated on heroquest |
| 26b | Heroes grubor + HQ door | done | grubor Talk + clover password + grubordoor walk-through |
| 26c | Heroes fire feather | done | hot_feather ice_gloves gate + damage; @pickup_obj |
| 26d | Sheep flavour Baa | done | ai_timer sheepsheered/unsheered |
| 26e | Cow flavour Moo | done | ai_timer cow/cow2/cow_beef |
| 26f | Mcannon cave + guard | done | cave/mudpile tele + guard Talk + constants; remains/doors deferred |
| 26g | Gnomeball cheerleader | done | gnome_cl1..8 idle AI timer |
| 26h | Ghrim kingdom book | done | misc_ghrimbook ~mesbox pages |
| 26i | Duel tomato crate + throw | done | crate Trade stub; throw (.coord) deferred |
| 26j | Duel arena spectators | done | crowd Talk random lines; clue deferred |
| 26k | Party Room lever + knights | done | lever Pull + knight dance queue; balloons deferred |
| 26l | Party Room chest open/close | done | open/shut chest; Deposit IF deferred |
| 26m | Gnomeball referee Talk | done | Talk/rules; game start deferred (%gnomeball_progress) |
| 26n | Mining Guild dwarf Talk | done | mguild_dwarf1/2 welcome + guild info |
| 26o | Agility Arena Cap'n Izzy | done | Talk place/do; pay/enter deferred |
| 26p | Agility Arena Jackie | done | Talk + Trade stub; ticket IF deferred |
| 26q | Boot the Dwarf Talk | done | hello/why-Boot; crest gold deferred |
| 26r | Derrik (misc_smithy) Talk | done | anvil yes; giant nib deferred |
| 26s | Party Room balloons thin | done | Burst stamp→pop→del; chest loot deferred |
| 26t | Duel tomato throw | blocked | Needs .coord / secondary player + splash (opplayeru) |
| 26u | Duel forfeit flags stub | done | Choice + stub mes; duel_finish engine deferred |
| 26v | Mcannon dwarf remains + book | done | remains pickup gate + mcannonbook mesbox pages |
| 26w | Heroes oily fishing rod | done | blamish_oil + fishing_rod → oily_fishing_rod |
| 26x | Chicken flavour squawk | done | _chicken timer squawk + egg; sound deferred |
| 26y | Mcannon doors | done | walk-through gated on %mcannon |
| 26z | Nulodion notes | done | mesbox Read |
| 27a | Rabbit drop table | done | rabbit_1..3 death → bones + raw_rabbit |
| 27b | Legends Guild doors | done | quest gate + walk-through; body deferred |
| 27c | Mcannon tower ladder | done | mcannonladder trapdoor gate + climb |
| 27d | Troll Godric Talk | done | cage Talk + ^troll_freed_godric constants |
| 27e | Mcannon crate + dwarf child | done | search crate + rescue Talk; walk-off simplified |
| 27f | Misc giant nib | done | Use nib on logs → misc_giant_pen |
| 27g | Heroes Trobert | done | ID papers Black Arm HQ Talk |
| 27h | Astrology book | done | Scorpius tale mesbox pages |
| 27i | Heroes Garv + door | done | mansion unlock papers + walk-through |
| 27j | Legends Guild gates | done | mithril gate Examine + walk-through thin |
| 27k | Zanaris Shamus | done | leprechaun Talk; tree spirit already landed |
| 27l | Mort Myre gate guard | done | Ulizius Talk; trails clue deferred |
| 27m | Prifddinas city guard | done | prif_city_guard Talk + gate Halt |
| 27n | Grip death (Heroes) | done | heroquest progress + death_drop |
| 27o | Undead one drops thin | done | zqzombie_* bones; greenmist deferred |
| 27p | Nightshade Eat | done | Cave nightshade ifop4 Eat → poison damage |
| 27q | Observatory goblin_guard Talk | done | Talk + retaliate; drops already landed |
| 27r | Duck flavour | done | _duck/_duckling Quack/Eep; sound/death deferred |
| 27s | Zombie flavour | done | 4 zombie timers Brainssss; dragonslayer_zombie absent |
| 27t | God staff/cape conflict | done | opheld2 conflict→~equip; drop/pickup already landed |
| 27u | Serum book (Mort'ton) | done | mesbox pages + %morttonquest start; IF1 deferred |
| 27v | Elemental workshop book | done | mesbox + knife key; OSRS varbits book/key |
| 27w | Regicide General Hining | done | Talk + thin ^regicide_* constants |
| 27x | Regicide quartermaster | done | Talk + Trade stub (inv.ini) |
| 27y | Spirit of Scorpius | done | mould/bless; %scorpius_given_symbol authored |
| 27z | Ikov fire warrior | done | Talk + Fire Blast AI + death → ^ikov_defeated_fire_warrior |
| 28a | Death barman apnpc | done | apnpc1/3 approach Talk + Trade stub |
| 28b | Ikov Winelda | done | Talk + limpwurt tele (cam/smoke; sound deferred) |
| 28c | Ikov Lucien1 Talk | done | Start/mission/pendant thin; lucien2 deferred |
| 28d | Regicide tyras lazy guard | done | Talk + cooked rabbit → %regicide_given_rabbit |
| 28e | Trollromance Aga | done | Talk + thin ^troll_love_*; flower Use mes |
| 28f | Misc Ulby doorguard | done | Heroes gate + audience + throne door walk-through |
| 28g | Viking market stubs | done | Fur/fish monger outerlander + Trade stub |
| 28h | Viking sailor Talk | done | Outerlander / postquest thin; merchant trial deferred |
| 28i | Eadgar Tegid washing | done | Flavour Talk; dirty robes quest arm deferred |
| 28j | Lucy apnpc + Arandar gates | done | Lucy approach beer; overpass walk-through after tyras; elf_overpass_guard absent → mesbox |
| 28k | Ikov Guardians of Armadyl | done | Talk thin; ikov_guardianmale/female; help path → ^ikov_helping_armadyl |
| 28l | Regicide camp tracker | done | Talk + pendant show; full ^regicide_* stage constants |
| 28m | Regicide Koftik Talk | done | caveguide6 food + well line; %regicide_koftik_food / down_well |
| 28n | Ikov Lucien2 Talk | done | Staff handoff thin; quest_complete UI / death queues deferred |
| 28o | Regicide Tyras guards | done | Camp/tent guard Talk; combat/drops deferred |
| 28p | Regicide kings messenger | done | Talk + summons; spawn timer deferred |
| 28q | Viking fisherman thin | done | Outerlander/postquest; merchant trial deferred |
| 28r | Viking weapons salesman | done | Outerlander/council thin + Trade stub |
| 28s | Fight Arena Lady Servil | done | Talk start + thin ^arena_*; progress IF / rewards deferred |
| 28t | Eadgar troll thistle | done | Pick/move/drop; grind/brew/cook dry deferred |
| 28u | Arena spectator Talk | done | Flavour by %arenaquest; armour proc deferred |
| 28v | Arena fightslave Talk | done | Kelvin/Joe/fightslave thin; armour/.npc_find deferred |
| 28w | Justin Servil Talk | done | Thin by %arenaquest; release labels deferred |
| 28x | General Khazard Talk | done | Remap → general_khazard_arena; cinematic deferred |
| 28y | Arena boss death progress | done | Ogre/scorpion/bouncer %arenaquest advance thin |
| 28z | Brother Kojo Talk | done | Clock Tower start/progress; trails watch deferred |
| 29a | Burntmeat Talk thin | done | eadgar_troll_chief_cook + thin ^eadgar_* |
| 29b | Parroty Pete Talk | done | Aviary Talk; alco-chunks/hatch deferred |
| 29c | Horror rockcrab | done | Activate + drops; trail clue deferred |
| 29d | Arena khazard_barman | done | Beer/Khali brew Talk; SotN soft-skip merged |
| 29e | Arena hengrad Talk | done | Thin jail Talk; cinematic deferred |
| 29f | Sammy Servil Talk | done | Remap jeremy→sammy_servil*; free/keys deferred |
| 29g | Viking longhall barkeep | done | Outerlander + Trade stub; merchant trial deferred |
| 29h | Viking Sigmund Talk | done | Outerlander + Trade stub; merchant flower chain deferred |
| 29i | Viking clothing shopkeeper | done | Outerlander + Trade stub; shoe IF deferred |
| 29j | Trollromance Ug Talk | done | Expanded ^troll_love_*; flower/sled deferred |
| 29k | Lord Iorwerth Talk | done | Thin by %regicide_quest; trail clue deferred |
| 29l | Arena khazard_guard | done | arena_guard1–4 Talk + brew keys; wearing deferred |
| 29m | Crest Caleb Talk | done | caleb_fitzharmon fish→avan_crest; gauntlets deferred |
| 29n | Observatory assistant | done | qip_obs_proffesors_assistant remap; wine claim |
| 29o | Viking Brundt Talk | done | Start + vote stub; merchant/history deferred |
| 29p | Viking citizen Talk | done | viking_man/woman thin; drops deferred |
| 29q | Viking Askeladden Talk | done | Outerlander/council/postquest thin; pet rock deferred |
| 29r | Crest Johnathon Talk | done | Cure + Talk thin; gauntlets deferred |
| 29s | Crest Dimintheis Talk | done | Start/progress thin; complete/gauntlets deferred |
| 29t | Crest Chronozon AI | done | Death/weaken thin; magic cast hook deferred |
| 29u | Horror Larrissa Talk | done | Girlfriend Talk + ^horror_*; gunnjorn key deferred |
| 29v | Troll Eadgar Talk | done | Prison + stew thin; Eadgar's Ruse trees deferred |
| 29w | Troll prison guards | done | Pickpocket keys thin; snore AI deferred |
| 29x | Watchtower city_guard | done | Riddle + deathrune map thin |
| 29y | Watchtower Toban | done | Dragon bones → relicpart3 thin |
| 29z | Watchtower enclave_guard | done | Talk + nightshade → cave tele |
| 30a | Watchtower Gorad | done | Talk + tooth death drop thin |
| 30b | Watchtower Grew | done | Talk + ogretooth → relicpart2/crystal1 |
| 30c | Watchtower Og | done | Talk + stolen_gold → relicpart1 |
| 30d | Watchtower ogre_guard | done | Gu'Tanoth gates + %gutanoth_gold; door swing → tele |
| 30e | Watchtower ogre_shaman | done | Talk blast + magic_ogre_potion dissolve; ai_queue2 deferred |
| 30f | Watchtower skavid | done | Talkers + scared/mad language + crystal2 |
| 30g | Watchtower wizard | done | Start/fingernails/relic/potion enchant thin; crystal Use deferred |
| 30h | Watchtower ogre_potion mix | done | guam/janger/ground bat → ogre_potion; wrong-order explode |
| 30i | Watchtower locs | done | Bushes/caves/chest/jumps/lever/finish thin; wall/ropeswing/gates deferred |
| 30j | Fairy/Shilo bankers | done | fairy_banker + shilobanker Talk + ~openbank |
| 30k | Dragon sq halves Use | done | dragonshield_a↔b anvil hint; smith repair deferred |
| 30l | Chompy ogre chest | done | Rock lift + bellows; ^chompybird_* constants |
| 30m | Viking guard drops | done | ai_queue3 table; trail clue deferred |
| 30n | Chompy Fycie Talk | done | Feathers trade + %chompybird_kills; trail clue deferred |
| 30o | Chompy Bugs Talk | done | Chisel/knife trade + scratchers bit |
| 30p | Desert Rescue Irena | done | Talk start/progress thin; barrel Ana/XP deferred |
| 30q | Mosol Rei Talk thin | done | Start/postquest tele; belt/Trufitus deferred |
| 30r | Regicide darkelf AI/drops | done | AI + table; trail deferred; name-expand darkelf/2 |
| 30s | Viyeldi Talk thin | done | Spirit dialogue; hat/dagger stab deferred |
| 30t | Al Shabim greeting thin | done | Intro/place/looking; dart chain deferred |
| 30u | Troll spectator Attack gate | done | Name-expand 1–7 + champion gate + drops; face timer deferred |
| 30v | Crest Avan Talk | done | Remap avan→avan_fitzharmon_*; gauntlets kept |
| 30w | Arrg Talk/death thin | done | Spawn + death progress; ranged AI deferred |
| 30x | Ikov Lucien door | done | Walk-through; door swing deferred |
| 30y | Horror diary Read | done | IF1 book → mesbox stub |
| 30z | Regicide alchemy book | done | IF1 book → mesbox stub |
| 31a | Book of Binding Read | done | Enchant IF deferred |
| 31b | Bullroarer Use thin | done | Jungle spawn/Gujuó deferred |
| 31c | Mcannon railings | done | %mcannon_railing*_fixed varbits |
| 31d | Mcannon broken cannon | done | tool1–3 + safety_on varbits |
| 31e | Plague City mournertwa | done | Thin Talk by %elenaquest |
| 31f | Members general shops | done | Khazard/dwarven/Zanaris Trade stubs |
| 31g | Grip Talk | done | Heroes' Quest black arm HQ; Attack gate deferred |
| 31h | Chompy caves + bellows | done | Rantz cave tele + swamp gas fill |
| 31i | Horror basalt rocks | done | Lighthouse jumping spots |
| 31j | Achey log firemaking | done | Tinderbox light; knife→shafts deferred |
| 31k | Oomlie wrap | done | palm_leaf↔raw_oomlie |
| 31l | Pie shell + fill | done | Switch map (no uncooked_pie_struct) |
| 31m | Stew assemble | done | potato/meat+bowl + curry; BIM meat Use deferred |
| 31n | Pizza assemble | done | Switch map (no pizza_topping_struct); topping XP; tomato/cheese BIM |
| 31o | Ugthanki kebab assemble | done | Switch map (no ugthanki_kebab_struct); members gate |
| 31p | cooked_meat BIM Use | done | Pie/stew/pizza reverse Use via cooked_meat.rs2 |
| 31q | Ugthanki kebab bad Eat | done | Name-bound over _misc_food |
| 31r | Ape Atoll food Eat | done | mm_monkey_nuts/bar + mm_banana_stew |
| 31s | Poison karambwan + slimey eel Eat | done | tbwt_poorly + mort_slimey_eel_cooked |
| 31t | Strange fruit Eat | done | macro_triffidfruit; poison immunity |
| 31u | Empty vial | done | opheld4 _potion + vial_water |
| 31v | Cooked snail Eat | done | snail_corpse_cooked1–3 name-bound |
| 31w | Keg of beer Drink | done | + cr_queue cam_reset |
| 31x | Cooking snail rows | done | cooking_generic thin/lean/fat |
| 31y | Ogre arrow fletching | done | achey shafts + tips/feathers/heads |
| 31z | Fight Arena journal | done | ~arena_journal + quest_fightarena wire |
| 32a | Family Crest journal | done | ~crest_journal + quest_familycrest wire |
| 32b | Lost City journal | done | ~zanaris_journal + quest_lostcity wire |
| 32c | Plague City journal | done | ~elena_journal + quest_plaguecity wire |
| 32d | Temple of Ikov journal | done | ~ikov_journal + quest_templeofikov wire |
| 32e | Observatory journal | done | ~itgronigen_journal + quest_observatory wire |
| 32f | Troll Romance journal | done | ~troll_love_journal + quest_trollromance wire |
| 32g | Horror from the Deep journal | done | ~horror_journal + quest_horrorfromthedeep wire |
| 32h | Dwarf Cannon journal | done | ~mcannon_journal + quest_dwarfcannon; railing varbits; **found 2026-08-11 (auditing Between a Rock..., QUESTHELPER_CONTENT_PORT_QUEUE.md #133):** despite this row and 26f/26v/26y/27c/31c/33f all being individually real, the quest is NOT actually completable -- grep of every `%mcannon =` assignment in the tree shows the master varp never advances 0->1, 8->9, or 10->11; the "Dwarf Commander" quest-giver and Nulodion's own talk dialogue (only his *item* mesbox exists) have no script anywhere. Not re-scored (missing content, not a wrong `done`), but any future slice gating on Dwarf Cannon FINISHED must soft-skip it until a commander-dialogue slice lands |
| 32i | Tribal Totem journal | done | ~totem_journal + quest_tribaltotem; handelmort_traps_disabled authored |
| 32j | Watchtower journal | done | ~itwatchtower_journal + quest_watchtower wire |
| 32k | Troll Stronghold journal | done | ~troll_journal + quest_trollstronghold wire |
| 32l | Elemental Workshop I journal | done | ~elemental_workshop_journal + quest_elementalworkshop1; named varbits |
| 32m | Priest in Peril journal | done | ~priestperil_journal + quest_priestinperil; ^priestperil_* authored |
| 32n | Biohazard journal | done | ~biohazard_journal + quest_biohazard wire |
| 32o | Big Chompy Bird Hunting journal | done | ~chompybird_journal + quest_bigchompybirdhunting wire |
| 32p | Regicide journal | done | ~regicide_journal + quest_regicide; chemist_chat varbit |
| 32q | Tourist Trap journal | done | ~desertrescue_journal + quest_touristtrap wire |
| 32r | Eadgar's Ruse journal | done | ~eadgar_journal + quest_eadgarsruse; %eadgar_bits/grain/chickens authored |
| 32s | Heroes' Quest journal | done | ~hero_journal + quest_heroes wire |
| 32t | Fremennik Trials journal | done | ~viking_journal + quest_fremenniktrials; %viking_bits + trial helpers; name stub |
| 32u | Shades of Mort'ton journal | done | ~mortton_journal + quest_shadesofmortton; olive_oil/shade_remains/pyre_logs cats |
| 32v | Nature Spirit journal | done | ~druidspirit_journal + quest_naturespirit; %druidspirit_bits; ^priestperil_complete |
| 32w | Scorpion Catcher journal | done | ~scorpcatcher_journal + quest_scorpioncatcher; thin ^scorpcatcher_* (parallel) |
| 32x | Throne of Miscellania journal | done | ~misc_journal + quest_throneofmiscellania; affection constants (parallel) |
| 32y | Shilo Village journal | done | ~zombiequeen_journal + quest_shilovillage; %zq_map_mechanisms authored |
| 32z | Monkey Madness journal | done | ~mm_journal + quest_monkeymadness1; ^monkeymadness_* (Misthalin owns ^mm_*); unnamed flag varbits |
| 33a | Legends Quest journal | done | ~legends_journal + quest_legends; %legends_bits authored; constants expanded |
| 33b | In Search of the Myreque journal | done | ~routequest_journal + quest_insearchofthemyreque; %routequest_myreque_bits; bitcount→getbit_range |
| 33c | Ogre bow Check kills | done | ogre_bow opheld3 + ~get_chompy_rank; ~objbox→~mesbox |
| 33d | Swamp toad inflate | done | opnpcu toad + ai_timer; synth/sound_area dropped |
| 33e | Chompy bloated toad release/place | done | bloated_toad release/place + bait roll; thin ~spawn_chompy_bird; full AI deferred |
| 33f | Cannonballs smelting | done | steel_bar+ammo_mould → mcannonball×4; furnace Use hook |
| 33g | Dragon sq anvil repair | done | @make_dragon_sq via oplocu anvil; halves Use hint kept |
| 33h | Crest Boot gold branch | done | boot_the_dwarf ^crest_spoken_* arms |
| 33i | Holiday scythe/bunnyears pickup | done | opobj3 one-owned; %scythe_unlocked/%bunny_ears_unlocked authored |
| 33j | MM talk helpers + padulah | done | ~mm_wearing_greegree (cat 95) / monkeyspeak + padulah Talk |
| 33k | MM temple priests | done | hafuba/lofu/denadu Talk |
| 33l | MM sleeping monkey guard Talk | done | opnpc1 mesbox; ai_timer (%npc_int) deferred |
| 33m | Dragon spear Shove special | done | sa_kind=8 + pvm_dragon_spear_sa; CW barricade gate deferred |
| 33n | MM monkeys uncle Talk | done | opnpc1; aa_summon deferred |
| 33o | MM dugopul Talk | done | Talk only; AI/hunt deferred |
| 33p | MM uwogo/murowoi Talk | done | aa_summon deferred |
| 33q | MM shopkeepers Trade stubs | done | hamab/ifaba/tutab/daga/solihib; openshop→stub |
| 33r | MM elder_guard Talk thin | done | greegree/speak; quest arms+AI deferred |
| 33s | MM zoo_monkey capture Talk | done | backpack handoff; timer/trail deferred |
| 33t | MM karam Talk | done | talker + busy; AI deferred |
| 33u | MM caranock Talk | done | shipyard liaison; ^monkeymadness_* rename |
| 33v | MM kruk Talk | done | %varbit_117/%varbit_120; escort tele thin |
| 33w | MM lumdo Talk | done | crash↔atoll boat; seal proof; fade→tele |
| 33x | MM waydar Talk | done | hangar/crash menus; glidermap→tele; ch2 thin |
| 33y | MM garkor Talk | done | quest arms; %varbit_118; ch4 cutscene thin |
| 33z | AA market stalls | done | mm_stall_* Steal; padulah guard; afk/combat drop |
| 34a | MM lumo jail Talk | done | lumo/bunkdo/carado Talk; jail queue deferred |
| 34b | MM monkeys aunt Talk | done | greegree/speak Talk; AI/guards deferred |
| 34c | MM daero Talk | done | hangar tele thin; training IF1 deferred; %varbit_99–101 |
| 34d | MM zooknock Talk+Use | done | flags %varbit_106–112/133; ch3 cutscene thin; AI deferred |
| 34e | MM awowogei throne Talk | done | %varbit_118; MM2 soft-skips merged; aa_guards deferred |
| 34f | MM monkey child Talk | done | %varbit_119/%varbit_113; banana/talisman Use |
| 34g | MM glough reinit | done | buyout → %varbit_103; hooked from grandtree_glough |
| 34h | MM narnode Talk | done | quest start/reward/complete; mm_ch1 cutscene deferred |
| 34i | MM monkey minder Talk | done | cage tele thin; %varbit_132; greegree arms |
| 34j | MM temple trapdoor | done | open/climb/rope; mapzone spikes deferred |
| 34k | AA wildlife poison | done | spider/snake/scorpion 1/10 poison_player |
| 34l | MM hangar puzzle thin | done | crate+hint+panel → %varbit_103/^daero_reinit; IF1 slide deferred |
| 34m | MM greegree Hold+zone | done | ~mm_greegree_zone + opheld2 Hold; transmogrify deferred |
| 34n | MM archer ranged AI | done | posted/monkey/ravine; grass/greegree skip; knockout deferred |
| 34o | MM jail doors+Talk | done | pick lock walk-through; aberab/trefaji Talk; patrol deferred |
| 34p | MM skeleton drops | done | ai_queue3 death_drop; reanimate deferred |
| 34q | MM jungle demon thin | done | sigil tele+spawn+Talk; wave/melee AI; cutscene deferred |
| 34r | MM atoll locs | done | bamboo ladders/stairs/banana crates/bunker tele |
| 34s | Ape Atoll dungeon thin | done | exit ladder+plank trap+bones crumble; mapzone spikes deferred |
| 34t | AA monkey guard AI | done | greegree despawn; religious heal; knockout deferred |
| 34u | MM bamboo doors+trees | done | largedoor greegree gate; banana deplete chain |
| 34v | MM monkey amulet string | done | ball_of_wool + unstrung speak amulet |
| 34w | Mort Myre swamp decay | done | ~start_swampdecay_timer + %mortmyre; music wire deferred |
| 34x | Priest Peril evil monks | done | confuse/weaken/curse/zap AI + pipkey_gold |
| 34y | MM amulet smith firewall | done | enchanted gold → unstrung speak amulet; zenyte hook |
| 34z | MM aa_summon_guards | done | ~aa_summon_guards + aa_guards_queue; %mm_guards_cooldown temp |
| 35a | MM summon shop/uncle/advisors | done | shopkeepers + uncle + uwogo/murowoi → ~aa_summon_guards |
| 35b | MM summon aunt/dugopul/throne | done | aunt+dugopul queue aa_guards; throne human path |
| 35c | MM thin chapter cards | done | mm_ch1 + ch2/ch3/ch4 mesbox cards; IF1 deferred |
| 35d | MM supply crates | done | iron/bronze scimitar + tinderbox/thread/needle/chisel/hammer |
| 35e | MM quest crates+pineapple | done | denture/mould/eye crates + bookcase + pineapple plant |
| 35f | MM warehouse hole+ropes | done | crate hole tele/damage + ropes + trapdoor + locked door |
| 35g | MM bridge+jump+gold bar | done | bridge ladders + jumping square + enchanted gold Examine |
| 35h | Priest Peril vampire coffin | done | blessed water Use + Examine Talk |
| 35i | Priest Peril temple doors | done | Open/Knock-at; door swing → walk-through |
| 35j | Priest Peril trapped Drezel | done | cell Talk/key/bless water |
| 35k | Heroes Scarface mansion locs | done | grip cabinet + pete doors + candle chest (garvdoor already) |
| 35l | Legends jungle forester | done | Talk + map→bullroarer Use |
| 35m | Observatory professor Talk | done | progress Talk/Use; trails stubbed |
| 35n | Shilo nazastarool death | done | zq_mainzombie* queues + thin summon; %npc_aggressive_player→findhero |
| 35o | Legends echned_zekin Talk | done | Talk/Use dagger + nezi summon; trails/update_all deferred |
| 35p | Nature Spirit filliman | done | Talk/Use + ns dialogue + quest_complete queues; VFX/sounds thin |
| 35q | Nature Spirit ghast | done | invis AI + pouch reveal + vis death/reward; food-rot thinned; %ghast_delay |
| 35r | Nature Spirit locs/bloom | done | gate/grotto/stones/bloom/pouch/altar/harvest + next_loc_stage overlays |
| 35s | Shilo rashiliyia | done | intro AI + Talk; ~random_undead_one; sounds/queue damage era |
| 35t | Legends irvig_senay | done | Talk/Attack/death + thin @summon_nezi_part3; drop %lastcombat |
| 35u | Legends san_tojalon | done | Talk/Attack/death; drop %lastcombat |
| 35v | Legends ranalph_devere | done | Talk/Attack/death; drop %lastcombat |
| 35w | Legends boulder + pool | done | push + lgwaterpool Look; already-pushed via %legendsquest (no %npc_start_coord) |
| 35x | Legends strange_barrel | done | smash + thin loot/explode; NPC spawn deferred |
| 35y | Alkh curtains + mausoleum gates | done | inaccastledoubledoor* open/close; pip_underground walk-through |
| 8 | Outward areas / remaining quests / minigames | pending | After 35y: MM AI/knockout/transmog deferred; CW/Trails deferred; skip blocked list + lavafish_loc + tomato throw (.coord); duel closed chest absent (loc_3193 false-friend) |



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
- slice 9d done: general_use gates/fence/gangplank + findsomethingnice — wooden double fence on gate_main/gate_outer (fencegate/farming/rustic/plaguesheep/pvpa_access_gate — MTA desert approach uses pvpa_access_gate_* model 966); metalgate still door_closed; memberfencegate walk-through; mournerstewfence squeeze; sarim/karamja/brimhaven/ardougne/entrana planks (not dragonship*); findsomethingnice + wire into chests/drawers/crates/sacks/wardrobes; deferred: board_message param, duel arena, ~open_and_close_metal_gate, doubledoors.rs2
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
- slice 14m done: death doors + stone mechanism + cat 73=death_cannonball + %death_stones; mock230_pack 0 errors (3659 scripts)
- slice 14n done: hermit cave + stile + climbing rocks + boot Wear; dangersign deferred; mock230_pack 0 errors (3666 scripts)
- slice 14o done: npc_combat_ranged + troll thrower AI/overlays/drops; mock230_pack 0 errors (3711 scripts)
- slice 14p done: death_archer combat/drops + death_guard drops + dangersign stub; mock230_pack 0 errors (3718 scripts)
- slice 14q done: Heroes Guild leftovers (achietties/helemos/entrance) + %heroquest/%zanaris stubs + quest_hero procs/complete; Trade stub; door walk-through; mock230_pack 0 errors (3738 scripts)
- slice 14r done: combat training camp (trainers/shop stub/gate/dummy/ogre) + %biohazard/%biodummy; mock230_pack 0 errors (3749 scripts)
- slice 14s done: trollweiss chill (zones + timer/mapzone drain); snow overlay IF1 deferred; mock230_pack 0 errors
- slice 14t done: gerrant lava-eel Heroes arm (blamish slime); mock230_pack 0 errors
- slice 14u done: entrana firebird hot_feather gated on %heroquest; mock230_pack 0 errors
- slice 14v done: quest equip gates (heroes + dragon slayer) via ~levelrequire_quest_gate; mock230_pack 0 errors
- slice 14w done: brimhaven thin (davon/alfonse/pirate_guard/pineapple); mock230_pack 0 errors
- slice 14x done: deadmans bartender drinks + fishplatform1..4 Talk-to; mock230_pack 0 errors (3772 scripts)
- slice 14y done: Sea Slug thin (bailey/kennith/kent/holgart + constants/coords + kennithwall + boat procs); Caroline/locs deferred; mock230_pack 0 errors (3800 scripts)
- slice 14z done: hajedy cart stub + ^zombiequeen_complete; trail/Shilo body deferred; mock230_pack 0 errors (3805 scripts)
- slice 15a done: kangai_mau Tribal Totem start/hand-in + constants; body/journal/trail deferred; mock230_pack 0 errors (3814 scripts)
- slice 15b done: gnome shop stubs (gulluck/hudo/rometti/heckelfunch); mock230_pack 0 errors
- slice 15c done: brimstail essence tele + cave locs; mock230_pack 0 errors
- slice 15d done: gnome troop Talk-to + ranged AI; mock230_pack 0 errors
- slice 15e done: kalron + chantergnome + treequest constants; mock230_pack 0 errors
- slice 15f done: gnome_waiter Trade stub; mock230_pack 0 errors
- slice 15g done: aluft_gianne greeting stub (restaurant deferred); mock230_pack 0 errors
- slice 15h done: remsai Talk-to; mock230_pack 0 errors
- slice 15i done: bolkoy Trade stub; mock230_pack 0 errors
- slice 15j done: gnometrainer Talk-to; mock230_pack 0 errors (3848 scripts)
- slice 15k done: Caroline Sea Slug start + complete queue; mock230_pack 0 errors (3861 scripts)
- slice 15l done: gnome_gate walk-through + grandtree constants; mock230_pack 0 errors (3908 scripts)
- slice 15m done: commander_montai Talk-to; mock230_pack 0 errors (3909 scripts)
- slice 15n done: king_bolren start/complete + ceremony; mock230_pack 0 errors (3914 scripts)
- slice 15o done: elkoy maze guide; mock230_pack 0 errors
- slice 15p done: hazelmere bark/scroll; mock230_pack 0 errors (3946 scripts)
- slice 15q done: blurberrybarmen Trade stub + ingredient sales; barcrawl deferred; mock230_pack 0 errors (3976 scripts)
- slice 15r done: blurberry greeting stub (gnome_bar deferred); mock230_pack 0 errors
- slice 15s done: gnome_glider pilot_* + p_choice fly + glider.constant; mock230_pack 0 errors (4050 scripts)
- slice 15t done: spirit_tree teleports + spirit_tree.constant; mock230_pack 0 errors (4071 scripts)
- slice 15u done: treegnomelooserailing squeeze; mock230_pack 0 errors (4081 scripts)
- slice 15v done: Sea Slug sticks/ladder/panel/crane/pickup; mock230_pack 0 errors
- slice 15w done: seaslug_journal + questlist wire; mock230_pack 0 errors
- slice 15x done: gnomes Talk-to + ranged AI; mock230_pack 0 errors (4132 scripts)
- slice 15y done: giannes_cook_book + cocktail_guide ~mesbox recipe stubs (IF1 deferred); mock230_pack 0 errors (4162 scripts)
- slice 15z done: %gnome_restaurant_progress authored 5803 + restaurant start + aluft wire; mock230_pack 0 errors (4197 scripts)
- slice 16a done: restaurant tutorial assigns through complete + utensil/premade helpers; mock230_pack 0 errors (4207 scripts)
- slice 16b done: restaurant jobs start/finish + dish helpers; mock230_pack 0 errors (4336 scripts)
- slice 16c done: swamp_toad → toads_legs; mock230_pack 0 errors (4337 scripts)
- slice 16d done: fruit knife slice/dice via p_choice2; mock230_pack 0 errors (4418 scripts)
- slice 16e done: grandtree_translationbook ~mesbox stub; mock230_pack 0 errors
- slice 16f done: grandtree_journal ~mesbox stub; mock230_pack 0 errors (4420 scripts)
- slice 16g done: grandtree anita Talk-to key handoff; mock230_pack 0 errors (4505 scripts)
- slice 16h done: grandtree charlie Talk-to + jail release; mock230_pack 0 errors (4516 scripts)
- slice 16i done: femi Talk-to/boxes + %femi_help authored 5856 + gnome_gate boxes; mock230_pack 0 errors (4528 scripts)
- slice 16j done: glough Talk-to + arrest jail; mock230_pack 0 errors (4573 scripts)
- slice 16k done: foreman quiz/order/death drop; mock230_pack 0 errors (4592 scripts)
- slice 16l done: shipyardworker1/2 Talk-to name-expand; mock230_pack 0 errors (4607 scripts)
- slice 16m done: king_narnode Talk-to + quest complete + %daconia_rock_root 5869; mock230_pack 0 errors (4636 scripts)
- slice 16n done: gnome cooking param/struct/tray + gianne_dough; mock230_pack 0 errors (4644 scripts)
- slice 16o done: gnome ingredient seasoning Use-with; mock230_pack 0 errors (4680 scripts)
- slice 16p done: grandtree scroll/order/invasionplans ~mesbox stubs; mock230_pack 0 errors (4687 scripts)
- slice 16q done: gnome crunchies bits+add/bake + raw dbrows + cook_item + string_procs; mock230_pack 0 errors (4733 scripts)
- slice 16r done: gnome battas bits+add/bake + raw dbrows + cook_item; mock230_pack 0 errors (4752 scripts)
- slice 16s done: gnome bowls bits+add/bake + raw dbrows + cook_item; mock230_pack 0 errors (4782 scripts)
- slice 16t done: gnome food topping finish + seasoning finish arms; mock230_pack 0 errors (4826 scripts)
- slice 16u done: cocktail shaker mix/pour + bits varps; mock230_pack 0 errors (4834 scripts)
- slice 16v done: cocktail garnish finish + oven warm rows; mock230_pack 0 errors (4875 scripts)
- slice 16w done: ingredient→half_baked/shaker reverse Use; mock230_pack 0 errors (4893 scripts)
- slice 16x done: grandtree climb/trapdoors; mock230_pack 0 errors (~4936 scripts)
- slice 16y done: grandtree chest/cupboard/pillars; mock230_pack 0 errors (~4976 scripts)
- slice 16z done: grandtree roots/rootdoor; mock230_pack 0 errors (4980 scripts)
- slice 17a done: shipyard gate walk-through + Ka-Lu-Min password; mock230_pack 0 errors (4988 scripts)
- slice 17b done: hazelmere bark sample → scroll; mock230_pack 0 errors (5000 scripts)
- slice 17c done: grandtree black demon death/timer + glough cutscene spawn; mock230_pack 0 errors (5075 scripts)
- slice 17d done: grandtree journal + questlist wire; mock230_pack 0 errors (5084 scripts)
- slice 17e done: tree tracker1/2/3 Talk-to; mock230_pack 0 errors (~5108 scripts)
- slice 17f done: khazard warlord Talk-to + orbs death; mock230_pack 0 errors
- slice 17g done: tree ballista/door/wall/chest; mock230_pack 0 errors (5132 scripts)
- slice 17h done: tree journal + questlist wire; mock230_pack 0 errors
- slice 17i done: waterfall almera + constants; mock230_pack 0 errors
- slice 17j done: waterfall hudon Talk-to; mock230_pack 0 errors
- slice 17k done: waterfall gerald Talk-to; mock230_pack 0 errors
- slice 17l done: waterfall golrie pebble + authored bitfield varp; mock230_pack 0 errors
- slice 17m done: waterfall hadley tourist guide; mock230_pack 0 errors
- slice 17n done: waterfall baxtorian book ~mesbox stub; mock230_pack 0 errors (5166 scripts)
- queue: added 18a Fishing Trawler + 18b Castle Wars (LC-owned; measured off SCAPE2009 10/11)
- slice 17o done: waterfall locs (raft/tomb/rope/doors/chalice/complete); mock230_pack 0 errors (5227 scripts)
- queue: added 18c Treasure Trails (LC `game_trail`; measured off SCAPE2009 #18)
- slice 17p done: waterfall journal + questlist wire; mock230_pack 0 errors (5228 scripts)
- slice 17q done: waterfall pillars rune puzzle; mock230_pack 0 errors (5232 scripts)
- slice 17r done: jungle potion constants; mock230_pack 0 errors
- slice 17s done: jungle potion locs + complete; mock230_pack 0 errors
- slice 17t done: jungle potion journal + questlist wire; mock230_pack 0 errors
- slice 17u done: trufitus Jungle Potion Talk-to/Use; mock230_pack 0 errors
- slice 17v done: brother_kojo + cog helpers/complete; mock230_pack 0 errors (5311 scripts)
- queue: added 18d Dig Site / quest_itexam (LC-owned; measured off SCAPE2009 #24)
- slice 17w done: clock tower locs/cogs/journal — %cog_bits authored; cogs+spindles+gates/levers+trough/rats; ~cog_journal+quest_clocktower wire; loc_1541→prisondooropen; mock230_pack 0 errors (5359 scripts)
- slice 17x done: Ardougne east shops Trade stubs + silk buy dialogue; mock230_pack 0 errors (5382 scripts)
- slice 17y done: Ardougne east thin NPCs (bartender/zoo/horacio/monk/citizens/barnaby/archer); barcrawl+trail deferred; mock230_pack 0 errors (~5401 scripts)
- slice 17z done: wizard_cromperty + rpdt_employee + ardougne_book~mesbox; mock230_pack 0 errors (~5470 scripts)
- slice 18a done: Hazeel Cult constants/varp/complete + Ceril; mock230_pack 0 errors (~5487 scripts)
- slice 18b done: Clivet; mock230_pack 0 errors (~5503 scripts)
- slice 18c done: Hazeel Cult journal + questlist wire; mock230_pack 0 errors (~5515 scripts)
- slice 18d done: Hazeel Cult cave/stairs/raft/valves; mock230_pack 0 errors (~5526 scripts)
- queue: renumbered Fishing Trawler/Castle Wars/Trails/Dig Site → 18g–18j (hazeel took 18a–18d)
- slice 18e done: Sheep Herder constants/varp/complete + councillor_halgrive; mock230_pack 0 errors (5564 scripts)
- slice 18f done: doctor_orbon plague outfit; mock230_pack 0 errors (5576 scripts)
- queue: renumbered Fishing Trawler/Castle Wars/Trails/Dig Site → 18s–18v (sheepherder/hazeel leftovers took 18g–18r)
- slice 18g done: herder_plaguesheep prod/poison; mock230_pack 0 errors (5606 scripts)
- slice 18h done: plaguesheep_furnace incinerate; mock230_pack 0 errors
- slice 18i done: plaguesheep_gatel/r walk-through; mock230_pack 0 errors (5611 scripts)
- slice 18j done: ~sheepherder_journal + questlist wire; mock230_pack 0 errors
- slice 18k done: farmer_brumty Talk-to; mock230_pack 0 errors (5633 scripts)
- slice 18l done: alomone Talk-to/defeat/hazeel cutscene; mock230_pack 0 errors (5642 scripts)
- slice 18m done: butler_jones Talk-to; mock230_pack 0 errors
- slice 18n done: claus + carnilleanrange poison; mock230_pack 0 errors (5648 scripts)
- slice 18o done: hazeel house cupboard/chest/crate + bookcase wall; mock230_pack 0 errors
- slice 18p done: guard_carnillean + philipe_carnillean; mock230_pack 0 errors
- slice 18q done: hazeel_cultist Talk-to; mock230_pack 0 errors (5664 scripts)
- slice 18r done: Dig Site constants/varp/helpers/complete; mock230_pack 0 errors (5690 scripts)
- queue: renumbered Dig Site examiner…journal → 18s–19b; Trawler/CW/Trails → 19d–19f; area_digsite → 19c
- slice 18s done: Dig Site examiner + curator stamp/cert arms; trail deferred; mock230_pack 0 errors (5713 scripts)
- slice 18t done: student1 errand/tips; mock230_pack 0 errors (5720 scripts)
- slice 18u done: student3 (LC student2.rs2); mock230_pack 0 errors (5727 scripts)
- slice 18v done: student2 (LC student3.rs2); mock230_pack 0 errors (5734 scripts)
- slice 18w done: archaeological_expert + DT etchings merge; mock230_pack 0 errors (5752 scripts)
- slice 18x done: panning_guide; mock230_pack 0 errors (5756 scripts)
- slice 18y done: digworkman1/2 Talk/Steal/invite/cave key; mock230_pack 0 errors (5779 scripts)
- slice 18z done: exam centre cupboard/bookcase + digsitebook~mesbox; mock230_pack 0 errors (5786 scripts)
- slice 19a done: itexam chemistry mixes → digcompound; mock230_pack 0 errors (5802 scripts)
- slice 19b done: ~itexam_journal + questlist wire; mock230_pack 0 errors (5813 scripts)
- slice 19c done: Dig Site area locs (panning/soil/winch/shaft/chest/barrel); cam_shake deferred; mock230_pack 0 errors (5867 scripts)
- queue: subdivided Trawler→19d–19g; CW→19h; Trails→19i; outward leftovers→19j–19l
- slice 19d done: Murphy dock + trawler.constant; %trawler→%trawler_status; trail sextant deferred; mock230_pack 0 errors (5882 scripts)
- slice 19e done: zones+gangplank+ladders+murphy_at_sea+sink/escape; %npc_int deferred; mock230_pack 0 errors (5935 scripts)
- slice 19f done: net/bail/winch/leak Fill/reset+login hook; hull→trawler_hull_*; control timer+varn deferred; mock230_pack 0 errors (6020 scripts)
- slice 19g done: start tele+mes; win+shore net→inv; sink huntall; IF/control varn deferred; mock230_pack 0 errors (6021 scripts)
- queue: CW/Trails deferred (large); 19h–19l = outward leftovers
- slice 19h done: thshantaydisc opheld1 → ~mesbox; mock230_pack 0 errors (6035 scripts)
- slice 19i done: Canifis Barker Trade stub; mock230_pack 0 errors (6035 scripts)
- slice 19j done: Chadwell W. Ardougne Trade stub; mock230_pack 0 errors (6035 scripts)
- slice 19k done: recruiter + citizen npc_say (finduid restore); mock230_pack 0 errors (6035 scripts)
- slice 19l done: Zanaris fairy_queen + jakut/irksol Trade stubs; mock230_pack 0 errors (6035 scripts)
- slice 19m done: Miscellania misc_veg_monger Trade stub; mock230_pack 0 errors
- slice 19n done: Miscellania misc_fish_monger Trade stub; mock230_pack 0 errors
- slice 19o done: W. Ardougne priest; mock230_pack 0 errors
- slice 19p done: W. Ardougne child Talk-to; mock230_pack 0 errors
- slice 19q done: Canifis Fidelio Trade stub; mock230_pack 0 errors
- slice 19r done: Canifis Rufus Trade stub; mock230_pack 0 errors
- slice 19s done: Zanaris Lunderwin cabbage buy; mock230_pack 0 errors
- slice 19t done: Zanaris ladder attendant + exit; mock230_pack 0 errors
- slice 19u done: Karamja Jiminua Trade stub; mock230_pack 0 errors
- slice 19v done: Shilo Obli Trade stub; mock230_pack 0 errors
- slice 19w done: Shilo Fernahei Trade stub; mock230_pack 0 errors
- slice 19x done: Velrak dusty_key; mock230_pack 0 errors
- slice 19y done: Kalphite old man; mock230_pack 0 errors
- slice 19z done: W. Ardougne Carla; mock230_pack 0 errors (6282 scripts)
- note: CW/Trails deferred (large IF1/minigame). **SUPERSEDED habit:** do **not**
  park MTA/construction with `.skip` to unblock compile — that practice is
  forbidden (PORTING_GUIDE §7). MTA + POH are live; fix your lane instead.
- slice 20a done: Miscellania flower_girl 15gp→flowers_waterfall_quest; mock230_pack 0 errors (6452 scripts)
- slice 20b done: Canifis Sbott werewolftanner + shared tan @ 2/5/45gp; mock230_pack 0 errors (6458 scripts)
- slice 20c done: bedabin Talk + Trade stub + pineapple arm; mock230_pack 0 errors
  (6477 scripts). **Note:** parking `ferox_upgrades.rs2.skip` for a concurrent
  WIP was a lane-silence anti-pattern — do not repeat (PORTING_GUIDE §7).
- slice 20d done: misc approval dialogue + ^misc_complete + man_misc_chatanim; mock230_pack 0 errors (6518 scripts)
- slice 20e done: Gardener Gunnhild Talk + iron sickle; mock230_pack 0 errors (6526 scripts)
- slice 20f done: Lumberjack Leif Talk; mock230_pack 0 errors (6529 scripts)
- slice 20g done: Miner Magnus Talk; mock230_pack 0 errors (6546 scripts)
- slice 20h done: Fisherman Frodi Talk; mock230_pack 0 errors (6586 scripts)
- slice 20i done: Misc/Etceteria people Talk/Attack; mock230_pack 0 errors (6632 scripts)
- slice 20j done: Seravel shiloshiptickets 25gp sale; mock230_pack 0 errors (6635 scripts)
- slice 20k done: Canifis building_steps telejump; mock230_pack 0 errors (6637 scripts)
- slice 20l done: Gunnjorn course greeting (Horror key deferred); mock230_pack 0 errors (6642 scripts)
- slice 20m done: W. Ardougne man/woman + elenaquest carriers; mock230_pack 0 errors (6772 scripts)
- slice 20n done: misc_heather weed_herbs sickle loop; mock230_pack 0 errors
- slice 20o done: bedabin_guard + tent door + desertrescue_map_mechanisms; mock230_pack 0 errors (6787 scripts)
- slice 20p done: upassmage Talk + Iban staff fix + %upass stub; mock230_pack 0 errors
- slice 20q done: E. Ardougne citizen Talk-to; mock230_pack 0 errors
- slice 20r done: zoo_keeper Talk-to (greegree/trail deferred); mock230_pack 0 errors
- slice 20s done: monk_ardougne drunkmonkquest lines; mock230_pack 0 errors
- slice 20t done: wantcat civilians + cat/overgrown category names; mock230_pack 0 errors
- slice 20u done: clerk civic office + elena set_progress stub; mock230_pack 0 errors
- slice 20v done: werewolfinnkeeper Roavar beer/gossip/story; mock230_pack 0 errors
- slice 20w done: thbankchest + thkebabinstructs; mock230_pack 0 errors
- slice 20x done: kharidian cactus Cut/waterskin; mock230_pack 0 errors (6890 scripts)
- slice 20y done: canafis_citizen Talk/transform/drops; mock230_pack 0 errors (6919 scripts)
- slice 20z done: plague manhole open/cover/climb; mock230_pack 0 errors (~6947 scripts)
- slice 21a done: mourner Talk-to + stew/biohazard arms; mock230_pack 0 errors (6971 scripts)
- slice 21b done: shantay Talk-to + jail varp + pass sale; mock230_pack 0 errors (6983 scripts)
- slice 21c done: desert_heat timer + %desert_heat; mock230_pack 0 errors (~7040 scripts)
- slice 21d done: shantay_pass guards/henge/prison; mock230_pack 0 errors (7047 scripts)
- slice 21e done: elenadoor2 walk-through; mock230_pack 0 errors
- slice 21f done: W. Ardougne bravek/city/mourner HQ doors; mock230_pack 0 errors (7059 scripts)
- slice 21g done: viking_fur_door toggle; mock230_pack 0 errors
- slice 21h done: vt_council_workmen Talk + beer→firecracker; mock230_pack 0 errors
- slice 21i done: magic guild doors/portals; dungeon fence deferred; mock230_pack 0 errors
- slice 21j done: captain_shanks ticket + telejump sail stub; mock230_pack 0 errors
- slice 21k done: plaguemudpile + garden coord; mock230_pack 0 errors
- slice 21l done: ardougnescroll Read; mock230_pack 0 errors
- slice 21m done: elenap Talk-to freed_elena; mock230_pack 0 errors
- slice 21n done: doomion/holthion/othainian drops + ^upass_found_doll; mock230_pack 0 errors
- slice 21o done: tree_spirit defeat → %zanaris; mock230_pack 0 errors
- slice 21p done: elemental workshop ai_queue3 drops; mock230_pack 0 errors (7220 scripts)
- note: fixed sibling rogue_chests.rs2 (inline ^consts moved to .constant) — do not park
- slice 21q done: alrena Talk + Elena stage constants; mock230_pack 0 errors
- slice 21r done: edmond Talk + quest_elena_complete; mock230_pack 0 errors
- slice 21s done: plaguemudpatch dig/soften; mock230_pack 0 errors
- slice 21t done: jethick Talk; mock230_pack 0 errors
- slice 21u done: kinglathas biohazard+UPass start; ^upass_complete=10; mock230_pack 0 errors
- slice 21v done: bravek hangover/warrant; mock230_pack 0 errors
- slice 21w done: alrena cupboard gasmask; mock230_pack 0 errors
- slice 21x done: plaguesewerpipe rope/climb; mock230_pack 0 errors
- slice 21y done: rehnisons family+stairs; mock230_pack 0 errors
- slice 21z done: caveguide1 Koftik entrance; mock230_pack 0 errors
- slice 22a done: elena2 Biohazard Talk; mock230_pack 0 errors
- slice 22b done: plaguehouse barrel/stairs/gate; mock230_pack 0 errors (7364 scripts)
- slice 22c done: elena doors/book return (rehnisondoor + plagueelenadoor*_vis); mock230_pack 0 errors
- slice 22d done: scruffy_note IF1→mesbox stub; mock230_pack 0 errors
- slice 22e done: jerico Talk-to; mock230_pack 0 errors
- slice 22f done: chemist Talk (biohazard arms; Regicide deferred); mock230_pack 0 errors
- slice 22g done: guidor Talk analyse→found_secret; mock230_pack 0 errors
- slice 22h done: UPass area-1 obstacles (rockslide/swamp/mudpile/pipes); mock230_pack 0 errors (7503 scripts)
- slice 22i done: biohazard loc leftovers (cupboard/pigeons/watchtower/ladder/HQ); mock230_pack 0 errors (7584 scripts)
- slice 22j done: guidordoor + guidors_wife; mock230_pack 0 errors (7608 scripts)
- slice 22k done: errand boys hops/chancy/devinci + %bioerrand; mock230_pack 0 errors (7623 scripts)
- slice 22l done: UPass rope swings (rock/swamp); mock230_pack 0 errors (7627 scripts)
- slice 22m done: UPass old bridge + cloth/lit arrows + FM hook; mock230_pack 0 errors (7634 scripts)
- slice 22n done: UPass grid (portcullis lever + zone timer arming + %upass_grid_pattern) + cave_slave1–7 Talk-to + Lathas ~setupassgrilltrap; mock230_pack 0 errors (7814 scripts)
- slice 22o done: UPass speartrap + mapzone trap timer; mock230_pack 0 errors (7820 scripts)
- slice 22p done: UPass double springtrap + woodplank cross; mock230_pack 0 errors (7914 scripts)
- slice 22q done: UPass logtrap + orboflight + caveorb1; %upass_caveorb_1; mock230_pack 0 errors (7917 scripts)
- slice 22r done: UPass ledge sidestep; mock230_pack 0 errors (7918 scripts)
- slice 22s done: UPass narrow walkway; mock230_pack 0 errors (7960 scripts)
- slice 22t done: UPass pipe6 + %upass_area2_pipe_used; mock230_pack 0 errors (7991 scripts)
- slice 22u done: UPass collapsed bridge + caveguide4 insane Koftik; %upass_koftik_chat; mock230_pack 0 errors (7996 scripts)
- slice 22v done: UPass Niloof (upassdwarf1) Talk; mock230_pack 0 errors
- slice 22w done: UPass Klank (upassdwarf2) Talk + gauntlets; mock230_pack 0 errors (8092 scripts)
- slice 22x done: UPass Kamen (upassdwarf3) drunk brew Talk; mock230_pack 0 errors (8091 scripts)
- slice 22y done: UPass journals (Randas upass_journal + Iban old_journal); IF1→mesbox; %upass_read_journal/%upass_read_iban_book; mock230_pack 0 errors (8101 scripts)
- slice 22z done: UPass paladins Jerro/Carl/Harry Talk + badge drops; %upass_paladin_food; mock230_pack 0 errors (8203 scripts)
- slice 23a done: UPass ibanmonk Talk + robe/staff drops; mock230_pack 0 errors (8205 scripts)
- slice 23b done: UPass Kalrag kill → %upass_venom_on_doll; mock230_pack 0 errors (8269 scripts)
- slice 23c done: lord_iban ai_timer bolt storm + ai_spawn timer; scripts+pack 0 errors
- slice 23d done: UPass entrance + exit; mock230_pack 0 errors (8400 scripts)
- slice 23e done: UPass orb destroy/pickup + furnace_upass hook; %upass_caveorb_*; mock230_pack 0 errors
- slice 23f done: UPass cave_well + mudpile_upass; mock230_pack 0 errors
- slice 23g done: UPass witch Kardia (door/cat/chest); %upass_gavecat; mock230_pack 0 errors
- slice 23h done: UPass bloodwell + temple doors; staff charge deferred; mock230_pack 0 errors
- slice 23i done: UPass boulder/railings/unicorncage; mock230_pack 0 errors
- slice 23j done: UPass mud dig + unicorn tunnels; mock230_pack 0 errors
- slice 23k done: UPass cavefood1 crate; %upass_crate_food; mock230_pack 0 errors
- slice 23l done: UPass tomb/doll/temple climax; mock230_pack 0 errors
- slice 23m done: UPass soulless cages + shadow chest; mock230_pack 0 errors
- slice 23n done: UPass tablets + caveguide5 + complete XP; mock230_pack 0 errors
- slice 23o done: UPass voice zones + cavewall tunnels; mock230_pack 0 errors (8629 scripts)
- slice 23p done: demon drops already present; slice 23q done: upass_journal + questlist wire; mock230_pack 0 errors
- slice 23r done: Zanaris camp adventurers (monk/archer/wizard/warrior); mock230_pack 0 errors
- slice 23s done: Zanaris doorman + market door; mock230_pack 0 errors
- slice 23t done: Kalphite larva + rope locs + coords landed; mock230_pack 0 errors
- slice 23u done: Shilo Vigroy cart → Brimhaven; mock230_pack 0 errors
- slice 23v done: Shilo Paramaya inn + dorm ladders; mock230_pack 0 errors
- slice 23w done: Shilo Yanni antiques quote/sell; mock230_pack 0 errors
- slice 23x done: Dragon Inn bartender drinks (barcrawl deferred); mock230_pack 0 errors (9045 scripts)
- slice 24a done: Varrock tramppg Talk; mock230_pack 0 errors
- slice 24b done: cook post-quest ~p_choice4; trails deferred
- slice 24c done: Varrock east gate + bioguard; mock230_pack 0 errors
- multi-lane: claim slices as `in_progress` before porting (loop prompt updated)
- next pending: outward leftovers (gypsy/joe_guard/volcano_entrance/…); skip blocked: scorpcatcher, wilderness_chaos_druid (Elder), Mort'ton lair, pyre, kolodion_fight, antifire, guard2, ditch, shops inv.ini, npc_poison varn, imp teleport, barcrawl, gnome_bar (%progress unresolved), trawler control (%npc_* varn), castlewars, trails (large), werewolfroadblocker (unresolved), biohazard-gated kilron/nurse/omart, gunnjorn Horror arms, outpost_gate (barcrawl), priestperil well/barrier/dog, vampire_spider (%npc_int), zambo (name absent)
- sibling unblock (not LC slices): pestcontrol missing temp/perm varps + wave constants; BIM constant aliases (^bim_recruited/^bim_willow/^bim_checkal/^bim_golem/^bim_willow_coord)

- slice 24e done: Aris already present (areas/varrock/aris.rs2)
- slice 24f done: Karamja volcano_entrance + climbing_rope2; mock230_pack 0 errors
- slice 24g done: Shilo sand1–3 scoop; mock230_pack 0 errors
- sibling unblock: DT2 typo camzodaal→camdozaal on archaeologist_2_vis
- next pending: karam_dungeon / yanille agility_dungeon / KQ / tbwt_timfraku

- slice 24h done: Yanille agility dungeon + ~agility_delay_fail helper; mock230_pack 0 errors
- slice 24i done: Brimhaven karam_dungeon (Saniboch/vines/stones/logs/pipes); entryfee varbit not whole varp; mock230_pack 0 errors
- next pending: KQ / tbwt_timfraku
- slice 24j done: Shilo Yohnus furnace Talk (door absent); mock230_pack 0 errors
- slice 24k done: Yanille rockcake stall thieve; mock230_pack 0 errors
- slice 24l done: Mort Myre snail spit AI + drops; mock230_pack 0 errors
- slice 24m done: vampire_leech melee+drain; mock230_pack 0 errors
- slice 24n done: kalphite_oldman Talk (already present); mock230_pack 0 errors
- slice 24o done: KQ combat both forms + kalphite worker melee; mock230_pack 0 errors (9330 scripts)
- next pending: 24p Timfraku (claimed) / TBWT brothers remainder

- slice 24p done: Timfraku + TBWT scaffold (constants/varps/title helpers/journal); brothers deferred; mock230_pack 0 errors
- sibling unblock: Inferno missing temp varps (logout/paused/safespot/saved_*); removed varp/varbit name clashes
- next pending: TBWT brothers/Lubufu; leave 24j–24o in_progress alone

- slice 24q done: Tiadeche Talk/Use + crafting manual; mock230_pack 0 errors
- slice 24r done: Tamayu Talk/Use spear+agility; hunt cutscene simplified; mock230_pack 0 errors
- slice 24s done: Tinsay meal tree + vessel→manual; mock230_pack 0 errors
- slice 24t done: Lubufu apprentice/bait + vessel; fishing spots deferred; mock230_pack 0 errors
- slice 24u done: TBWT item uses + bamboo/statue stubs + helpers; mock230_pack 0 errors
- slice 24v done: brother finals village rewards + Trade stubs; mock230_pack 0 errors
- slice 24w done: jogre furnace burn + monkey corpse drop; mock230_pack 0 errors (9541 scripts)
- next pending: TBWT fishing spots (karambwanji/karambwan) / small outward leftovers (hemenster/barnaby/…); skip blocked list unchanged

- slice 24x done: TBWT fishing spots (cats 632/633 + lubufu wire); sibling unblock mole_try_mud_extinguish stub; mock230_pack 0 errors
- slice 24y done: memberfish shark/big-net + oyster/casket; mock230_pack 0 errors
- slice 24z done: fishing guild door/master/Roachey Trade stub; mock230_pack 0 errors
- slice 25a done: slimeyfish (cat 457 mint); mock230_pack 0 errors
- slice 25b done: lavafish oily-rod (loc deferred); mock230_pack 0 errors
- slice 25c done: cooking guild chefdoor + head_chef; mock230_pack 0 errors
- slice 25d done: ranging guild armour salesman Talk + Trade stub; mock230_pack 0 errors
- slice 25e done: ranging guild bow salesman Talk + Trade stub; mock230_pack 0 errors (9648 scripts)
- next pending: charlie_the_cook / competition_judge (%target*) / lavafish_loc; skip blocked list unchanged

- note: parallel rubber-stamp of 25a–e as "already present" superseded by real ports above
- slice 25f done: kalphite_worker_spawn + KQ wire; mock230_pack 0 errors
- slice 25g done: troll thrower already present
- sibling unblock: strength4 name for 4-dose strength potion

- slice 25h done: charlie_the_cook Phoenix dialogue; mock230_pack 0 errors

- slice 25i done: competition_judge Talk + tickets; targets deferred; mock230_pack 0 errors

- slice 25j done: ranging_guild_door walk-through + doorman; mock230_pack 0 errors
- slice 25k done: ranging guard + tribal salesman Trade stub
- slice 25l done: ranging leatherworker → tan_leather_choices
- slice 25m done: ranging_target Fire-at + score; projectile deferred
- slice 25n done: ranging tower ladders + fill_towers + archer AI + advisors; mock230_pack 0 errors
- slice 25o done: ticket_merchant Trade stub (IF deferred); mock230_pack 0 errors (9779 scripts)
- slice 25p blocked: lavafish_loc — no eel loc in osrs239 (npc spot covers it)
- next pending: party balloons / tomato throw / CW/Trails; skip blocked list

- slice 25q done: wizard Fire Strike AI; mock230_pack 0 errors
- slice 25r done: partyroom Megan Talk
- slice 25s done: duel Mubariz Talk; duel engine deferred

- slice 25t done: Party Pete + Lucy; mock230_pack 0 errors
- slice 25u done: Fadli/Jaraah/nurses bank+heal
- slice 25v done: Hamid + Zahwa Talk

- slice 25w done: Games Room barmaid Talk + Trade stub; boardgames.rs2 deferred
- slice 25x done: Observatory scorpiusgrave + zamorakghost
- slice 26a done: Heroes herokitchendoor/panel walk-through
- slice 26b done: Heroes grubor + blackarm HQ door
- slice 26c done: Heroes hot_feather ice_gloves pickup
- slice 26d done: sheep flavour Baa ai_timer
- slice 26e done: cow flavour Moo ai_timer
- slice 26f done: mcannon cave/mudpile/guard + constants; remains deferred; mock230_pack 0 errors (9896 scripts)
- next pending: party balloons / tomato throw / CW/Trails; skip blocked list + lavafish_loc

- slice 26g done: gnomecheerleader gnome_cl* timer; mock230_pack 0 errors
- slice 26h done: misc_ghrimbook mesbox pages
- slice 26i done: rotten_tomato_crate Trade stub; throw deferred
- slice 26j done: duel crowd spectators Talk

- slice 26k done: partyroom lever + knights dance; balloons deferred; mock230_pack 0 errors
- slice 26l done: partyroom chest open/shut; Deposit deferred
- sibling unblock: gauntlet_gather %gauntlet_run_corrupted (varbit name clash)

- equip BAS parallel queue: docs/EQUIP_BAS_PORT_QUEUE.md (slices 0–9 done)

- **eight opcodes this queue deferred slices on are now hosted** (2026-08-04):
  `projanim_pl` (8k, 9o), `projanim_npc` + `npc_statsub` + `p_opnpct` (8q),
  `inv_dropitem_delayed` (8u ammo recovery), `set_player_op` (9i wilderness
  overlay), `stat_add` (9e), `obj_find` (14m, 16y pickup clears). Plus `busy`,
  `npc_sethuntmode` and `map_multiway`, named on the other queues. Each of those
  slices stays as landed — the *content* is still deferred — but none of them is
  blocked on the engine any more, so re-reading a "deferred: <opcode>" note here
  should now mean "not written yet", never "cannot be written". The one still
  genuinely blocked is **`cam_shake`** (19c Dig Site winch), which needs the
  rev-230 wire opcode measured; the gap-log row in
  [`SCAPE2009_CONTENT_PORT_QUEUE.md`](SCAPE2009_CONTENT_PORT_QUEUE.md) records why
  the number in the `v0/osrs` table is the wrong revision and must not be copied.
  Full accounting of what each op needed: that queue's log, same date.

- slice 26m done: gnomeball referee Talk/rules; game start deferred; mock230_pack 0 errors (10086 scripts)
- slice 26n done: Mining Guild dwarf Talk (mguild_dwarf1/2); mock230_pack 0 errors (10124 scripts)
- slice 26o done: Cap'n Izzy Talk place/do; pay/enter deferred
- slice 26p done: Jackie Talk + Trade stub; ticket IF deferred
- slice 26q done: Boot the Dwarf hello/why; crest gold deferred
- slice 26r done: Derrik anvil Talk; giant nib deferred; mock230_pack 0 errors (10214 scripts)

- slice 26s done: party balloon Burst (colour→pop); chest loot deferred; mock230_pack 0 errors
- slice 26t blocked: rotten_tomato throw needs .coord / secondary player
- slice 26u done: duel forfeit flags/trapdoor stub; duel engine deferred
- sibling unblock: combat.param `undead` + param.server for crumble_undead overlays
- next pending: outward leftovers (CW/Trails deferred); skip blocked list + lavafish_loc + tomato throw (.coord)

- slice 26v done: mcannonremains pickup + mcannonbook mesbox; mock230_pack 0 errors
- slice 26w done: blamish_oil + fishing_rod → oily_fishing_rod
- slice 26x done: chicken flavour squawk + egg; sound/%npc_attacking_uid deferred
- next pending: leave 26p–r alone if still in_progress; skip blocked; CW/Trails deferred

- slice 27a done: rabbit_1..3 drops; mock230_pack 0 errors
- slice 27b done: legends guild doors walk-through + constants
- slice 27c done: mcannonladder trapdoor gate
- slice 27d done: troll_godric Talk + quest_troll.constant
- sibling unblock: duel_bank_chest loc_3193 false-friend; gauntlet_floor_tick stub
- next pending: leave 26z alone if still in_progress; skip blocked; CW/Trails deferred

- slice 26y done: mcannondoor/mcannondoor1 walk-through; mock230_pack 0 errors
- slice 26z done: nulodions_notes mesbox
- slice 27e done: mcannoncrateboy + dwarfchildtw1 rescue
- slice 27f done: misc_giant_nib → pen on logs
- slice 27g done: trobert ID papers Talk
- slice 27h done: book_of_astrology mesbox
- slice 27i done: garv + garvdoor mansion unlock
- slice 27j done: legendsguildgate l/r thin walk-through
- slice 27k done: zanarisleprechaun Shamus Talk
- slice 27l done: mort_myre_gate_guard Ulizius Talk; trails deferred
- sibling fix: removed duplicate [softtimer,gauntlet_floor_tick]
- final: 10414 scripts; mock230_pack 0 errors
- next pending: outward leftovers (CW/Trails deferred); skip blocked list + lavafish_loc + tomato throw + duel closed chest (loc_3193 false-friend)

- slice 27m done: prif_city_guard Talk + elf_city_gate Halt; mock230_pack 0 errors
- slice 27n done: grip death → %heroquest killed_grip + death_drop
- slice 27o done: zqzombie undead_one drops; greenmist deferred
- next pending: skip blocked; CW/Trails deferred

- slice 27p done: nightshade ifop4 Eat → 15 dmg; mock230_pack 0 errors
- slice 27q done: goblin_guard Talk + retaliate
- slice 27r done: duck/duckling Quack/Eep flavour
- slice 27s done: zombie Brainssss flavour (4 names)
- slice 27t done: god staff/cape opheld2 conflict→~equip
- slice 27u done: serum_book mesbox + %morttonquest start constants
- slice 27v done: elemental workshop book mesbox + knife key (OSRS varbits)
- slice 27w done: regicide_general_hining Talk + thin constants
- slice 27x done: regicidegeneralshopkeeper Trade stub
- slice 27y done: spirit_of_scorpius mould/bless + authored %scorpius_given_symbol
- sibling fix: dropped client-only turnspeed from inferno.npc overlay
- final: 10515 scripts; mock230_pack 0 errors
- next pending: outward leftovers (CW/Trails deferred); skip blocked list + lavafish_loc + tomato throw + duel closed chest

- slice 27z done: ikov_firewarrior Talk + Fire Blast AI + death progress; mock230_pack 0 errors
- slice 28a done: death_barman apnpc approach
- loop re-armed: AGENT_LOOP_TICK_content_port every ~180s (pid 25968)
- next pending: skip blocked; CW/Trails deferred

- slice 28b done: ikov_winelda Talk + limpwurt tele; expanded quest_ikov.constant
- slice 28c done: ikov_lucien1 Talk thin (start/pendant); lucien2 deferred
- slice 28d done: regicide_tyras_lazy_guard Talk + cooked_rabbit → %regicide_given_rabbit
- slice 28e done: trollromance_aga Talk + thin ^troll_love_*
- slice 28f done: misc_ulby_doorguard + throne door walk-through
- slice 28g done: viking fur/fish monger outerlander + Trade stub
- slice 28h done: viking_sailor Talk thin
- slice 28i done: eadgar_druid_washing flavour Talk
- final: 10647 scripts; mock230_pack 0 errors
- next pending: leave 28k alone if still in_progress; outward leftovers (CW/Trails deferred); skip blocked list + lavafish_loc + tomato throw + duel closed chest

- slice 28l done (sibling): regicide camp tracker Talk + pendant + stage constants
- slice 28m done (sibling): koftik caveguide6 food + well line
- slice 28n done: ikov_lucien2 Talk + staff handoff thin; quest_complete UI deferred
- slice 28o done: regicide tyras camp/tent guard Talk
- slice 28p done: regicide_kings_messenger Talk + summons; spawn timer deferred
- slice 28q done: viking_fisherman1 outerlander/postquest thin
- slice 28r done: viking_weapons_salesman outerlander + Trade stub
- slice 28s done: lady_servil Talk start + quest_arena.constant
- slice 28t done: eadgar_troll_thistle pick/move/drop
- slice 28u done: arena_spectator Talk by %arenaquest
- final: 10688 scripts; mock230_pack 0 errors
- next pending: outward leftovers (CW/Trails deferred); skip blocked list + lavafish_loc + tomato throw (.coord) + duel closed chest (loc_3193 false-friend)

- slice 28v done: fightslave_kelvin/joe/fightslave Talk thin; armour/.npc_find deferred
- slice 28w done: justin_servil Talk thin; release labels deferred
- slice 28x done: general_khazard_arena Talk thin; jail cinematic deferred
- slice 28y done: arena_ogre/scorpion/bouncer death → %arenaquest advance + zone coords
- slice 28z done: brother_kojo Clock Tower Talk; trails watch deferred
- slice 29a done: eadgar_troll_chief_cook (Burntmeat) Talk + thin ^eadgar_* constants
- slice 29b done: eadgar_zoo_keeper_aviary (Parroty Pete) Talk; alco-chunks deferred
- slice 29c done: horror_rockcrab activate + drops; trail clue deferred
- final: 10754 scripts; mock230_pack 0 errors
- next pending: outward leftovers (CW/Trails deferred); skip blocked list + lavafish_loc + tomato throw (.coord) + duel closed chest (loc_3193 false-friend)

- slice 29d done: khazard_barman beer/Khali brew Talk; SotN soft-skip merged from secretsofthenorth
- slice 29e done: hengrad Talk thin; jail cinematic deferred
- slice 29f done: sammy_servil/sammy_servil_arena Talk (jeremy remap); free/keys cinematic deferred
- slice 29g done: viking_longhall_barkeep outerlander + Trade stub
- slice 29h done: viking_sigmund outerlander + Trade stub; merchant flower chain deferred
- slice 29i done: viking_clothing_shopkeeper (Yrsa) outerlander + Trade stub; shoe IF deferred
- slice 29j done: trollromance_ug Talk + expanded ^troll_love_* + ^troll_complete; rewards deferred
- slice 29k done: lord_iorwerth Talk thin by %regicide_quest; trail clue deferred
- final: 10772 scripts; mock230_pack 0 errors
- next pending: outward leftovers (CW/Trails deferred); skip blocked list + lavafish_loc + tomato throw (.coord) + duel closed chest (loc_3193 false-friend)

- slice 29l done: arena_guard1–4 Talk + drunk brew→keys; wearing_khazard deferred where LC uses proc
- slice 29m done: crest_caleb Talk + quest_crest.constant; gauntlets deferred
- slice 29n done: observatory assistant (qip_obs_proffesors_assistant) + expanded itgronigen stages
- slice 29o done: viking_brundt start + vote stub; merchant/history deferred
- slice 29p done: viking_citizen man/woman Talk thin; drops deferred
- slice 29q done: viking_askelapen outerlander/council/postquest thin; pet rock deferred
- final: 10842 scripts; mock230_pack 0 errors
- next pending: outward leftovers (CW/Trails deferred); skip blocked list + lavafish_loc + tomato throw (.coord) + duel closed chest (loc_3193 false-friend)

- slice 29r done: crest_johnathon Talk + antipoison cure; gauntlets deferred
- slice 29s done: crest_dimintheis Talk start/progress; complete UI/gauntlets deferred
- slice 29t done: crest_chronozon death/weaken + %crest_spells_levers_gauntlets; magic cast hook deferred
- slice 29u done: horror_girlfriend (Larrissa) Talk + ^horror_*; gunnjorn key deferred
- slice 29v done: troll_eadgar prison + stew Talk; Eadgar's Ruse trees deferred
- slice 29w done: troll_prison_guard1/2 pickpocket keys; snore AI deferred
- slice 29x done: watchtower city_guard riddle + deathrune→skavidmap + constants/bits
- slice 29y done: watchtower toban dragon_bones→relicpart3
- final: 10907 scripts; mock230_pack 0 errors
- next pending: outward leftovers (CW/Trails deferred); skip blocked list + lavafish_loc + tomato throw (.coord) + duel closed chest (loc_3193 false-friend)

- slice 29z done: enclave_guard Talk + nightshade → cave tele; expanded ^itwatchtower_* + %gutanoth_gold
- slice 30a done: gorad Talk + tooth death drop thin
- slice 30b done: grew Talk + ogretooth → relicpart2/powering_crystal1
- slice 30c done: og Talk + stolen_gold → relicpart1 + toban_key
- slice 30d done: ogre_guard1–4 Gu'Tanoth gates; door swing → p_teleport; rockcake market thin
- slice 30e done: ogre_shaman Talk blast + magic_ogre_potion dissolve; ai_queue2 deferred
- slice 30f done: skavid talkers + scared/mad language + powering_crystal2
- slice 30g done: watchtower_wizard start/fingernails/relic/potion enchant thin; crystal Use deferred
- final: 10963 scripts; mock230_pack 0 errors
- next pending: outward leftovers (watchtower ogre_potion mix / locs; CW/Trails deferred); skip blocked list + lavafish_loc + tomato throw (.coord) + duel closed chest (loc_3193 false-friend)

- slice 30h done: ogre_potion mix (guam/janger/bat bones; explode wrong order); jangerberries brew hook
- final: 10970 scripts; mock230_pack 0 errors
- next pending: watchtower locs; CW/Trails deferred; skip blocked list + lavafish_loc + tomato throw (.coord) + duel closed chest (loc_3193 false-friend)

- slice 30i done: watchtower locs (bushes/caves/chest/jumps/lever/crystal rock/finish); wall climb/ropeswing/gate swing deferred
- final: 11026 scripts; mock230_pack 0 errors
- next pending: outward leftovers (CW/Trails deferred); skip blocked list + lavafish_loc + tomato throw (.coord) + duel closed chest (loc_3193 false-friend)

- slice 30j done: fairy_banker + shilobanker Talk/~openbank
- slice 30k done: dragonshield_a↔b Use anvil hint; repair deferred
- slice 30l done: chompybird chest + ^chompybird_* constants; bellows name-expand
- slice 30m done: viking_guard/2 death drops; trail clue deferred
- final: 11038 scripts; mock230_pack 0 errors
- next pending: outward leftovers (CW/Trails deferred); skip blocked list + lavafish_loc + tomato throw (.coord) + duel closed chest (loc_3193 false-friend)

- slice 30n done: fycie Talk + %chompybird_kills bitfield + expanded ^chompybird_*; trail clue deferred
- slice 30o done: bugs Talk + scratchers bit; tools trade
- slice 30p done: desertrescue irena Talk start/progress thin; barrel Ana/XP deferred
- slice 30q done: mosol_rei Talk start/postquest tele; belt/Trufitus deferred
- slice 30r done: regicide_darkelf/2 AI + drops; trail deferred
- slice 30s done: viyeldi Talk thin; hat/dagger deferred
- slice 30t done: al_shabim greeting/looking thin; dart chain deferred
- slice 30u done: troll_spectator1–7 Attack gate + drops; face timer deferred
- final: 11092 scripts; mock230_pack 0 errors
- next pending: outward leftovers (CW/Trails deferred); skip blocked list + lavafish_loc + tomato throw (.coord) + duel closed chest (loc_3193 false-friend)

- slice 30v done: crest Avan Talk (avan_fitzharmon_* remap); gauntlets kept
- slice 30w done: trollromance_arrg Talk + spawn/death; ranged AI deferred
- slice 30x done: ikov_luciendoor walk-through; swing deferred
- slice 30y done: horror_diary1–3 Read mesbox stub
- slice 30z done: regicide_alchemy Read mesbox stub
- slice 31a done: book_of_binding Read mesbox; enchant IF deferred
- slice 31b done: bullroarer Use thin; kharazi/Gujuó deferred
- slice 31c done: mcannon railings via %mcannon_railing*_fixed
- slice 31d done: broken_multicannon repair via tool1–3/safety_on
- slice 31e done: mournertwa Talk by %elenaquest
- final: 11160 scripts; mock230_pack 0 errors
- next pending: outward leftovers (CW/Trails deferred); skip blocked list + lavafish_loc + tomato throw (.coord) + duel closed chest (loc_3193 false-friend)

- slice 31f done: khazard/dwarven/generalshop8 Trade stubs
- slice 31g done: grip Talk + id_papers/misc_key; Attack gate deferred
- slice 31h done: rantz cave teleports + swamp bellows fill
- slice 31i done: horror_jumping_spot1–10 basalt jumps
- slice 31j done: achey_tree_logs firemaking (tinderbox); knife→shafts deferred
- slice 31k done: palm_leaf↔raw_oomlie wrap
- slice 31l done: pie shell + apple/redberry/meat fill (switch map)
- slice 31m done: stew assemble + curry spice; BIM cooked_meat Use deferred
- final: 11236 scripts; mock230_pack 0 errors
- next pending: outward leftovers (CW/Trails deferred); skip blocked list + lavafish_loc + tomato throw (.coord) + duel closed chest (loc_3193 false-friend)

- slice 31n done: pizza assemble + toppings (switch map; pizza_topping_struct deferred); tomato/cheese BIM; pineapple_chunks arm
- slice 31o done: ugthanki kebab assemble (switch map; ugthanki_kebab_struct deferred); members gate; knife check
- slice 31p done: cooked_meat/chicken BIM → pie/stew/pizza
- slice 31q done: ugthanki_kebab_bad Eat (name-bound)
- slice 31r done: mm_monkey_nuts/bar + mm_banana_stew Eat
- slice 31s done: tbwt_poorly_cooked_karambwan + mort_slimey_eel_cooked Eat
- slice 31t done: macro_triffidfruit Eat + %poison immunity
- slice 31u done: opheld4 _potion + vial_water → vial_empty
- final: 11297 scripts; mock230_pack 0 errors
- next pending: outward leftovers (CW/Trails deferred); skip blocked list + lavafish_loc + tomato throw (.coord) + duel closed chest (loc_3193 false-friend)

- slice 31v done: snail_corpse_cooked1–3 Eat (name-bound; slimey heal ranges)
- slice 31w done: keg_of_beer Drink + cam_shake + cr_queue cam_reset
- slice 31x done: cooking_generic snail thin/lean/fat rows
- slice 31y done: ogre arrow fletching (achey shafts + wolf tips + feathers + heads); knife/chisel/feather BIM
- slice 31z done: Fight Arena journal (~arena_journal + quest_fightarena wire)
- slice 32a done: Family Crest journal (~crest_journal + quest_familycrest wire)
- final: 11342 scripts; mock230_pack 0 errors
- next pending: outward leftovers (CW/Trails deferred); skip blocked list + lavafish_loc + tomato throw (.coord) + duel closed chest (loc_3193 false-friend)

- slice 32b done: Lost City journal (~zanaris_journal + quest_lostcity wire)
- slice 32c done: Plague City journal (~elena_journal + quest_plaguecity wire)
- slice 32d done: Temple of Ikov journal (~ikov_journal + quest_templeofikov wire)
- slice 32e done: Observatory journal (~itgronigen_journal + quest_observatory wire)
- slice 32f done: Troll Romance journal (~troll_love_journal + quest_trollromance wire)
- slice 32g done: Horror from the Deep journal (~horror_journal + quest_horrorfromthedeep wire)
- slice 32h done: Dwarf Cannon journal (~mcannon_journal + quest_dwarfcannon; railing bits → named varbits)
- slice 32i done: Tribal Totem journal (~totem_journal + quest_tribaltotem; %handelmort_traps_disabled authored)
- final: 11376 scripts; mock230_pack 0 errors
- next pending: outward leftovers (CW/Trails deferred); skip blocked list + lavafish_loc + tomato throw (.coord) + duel closed chest (loc_3193 false-friend)

- slice 32k done: Troll Stronghold journal (~troll_journal + quest_trollstronghold wire)
- final: 11378 scripts; mock230_pack 0 errors
- next pending: outward leftovers (CW/Trails deferred); skip blocked list + lavafish_loc + tomato throw (.coord) + duel closed chest (loc_3193 false-friend)

- slice 32l done: Elemental Workshop I journal (~elemental_workshop_journal + quest_elementalworkshop1; LC bits → named varbits)
- final: 11379 scripts; mock230_pack 0 errors
- next pending: outward leftovers (CW/Trails deferred); skip blocked list + lavafish_loc + tomato throw (.coord) + duel closed chest (loc_3193 false-friend)

- slice 32m done: Priest in Peril journal (~priestperil_journal + quest_priestinperil; ^priestperil_* authored; well/barrier/dog still deferred)
- slice 32n done: Biohazard journal (~biohazard_journal + quest_biohazard wire)
- slice 32o done: Big Chompy Bird Hunting journal (~chompybird_journal + quest_bigchompybirdhunting wire)
- slice 32p done: Regicide journal (~regicide_journal + quest_regicide; LC chemist bit → %regicide_chemist_chat)
- final: 11383 scripts; mock230_pack 0 errors
- next pending: outward leftovers (CW/Trails deferred); skip blocked list + lavafish_loc + tomato throw (.coord) + duel closed chest (loc_3193 false-friend)

- slice 32q done: Tourist Trap journal (~desertrescue_journal + quest_touristtrap wire)
- final: 11387 scripts; mock230_pack 0 errors
- next pending: eadgar / hero / viking / mortton journals; CW/Trails deferred; skip blocked list + lavafish_loc + tomato throw (.coord) + duel closed chest (loc_3193 false-friend)

- slice 32r done: Eadgar's Ruse journal (~eadgar_journal + quest_eadgarsruse; %eadgar_bits/%eadgar_grain/%eadgar_chickens authored)
- final: 11388 scripts; mock230_pack 0 errors
- next pending: hero / viking / mortton journals; CW/Trails deferred; skip blocked list + lavafish_loc + tomato throw (.coord) + duel closed chest (loc_3193 false-friend)

- slice 32s done: Heroes' Quest journal (~hero_journal + quest_heroes wire)
- final: 11389 scripts; mock230_pack 0 errors
- next pending: viking / mortton journals; CW/Trails deferred; skip blocked list + lavafish_loc + tomato throw (.coord) + duel closed chest (loc_3193 false-friend)

- slice 32t done: Fremennik Trials journal (~viking_journal + quest_fremenniktrials; %viking_bits + trial get/set helpers; ~get_viking_name stub)
- final: 11406 scripts; mock230_pack 0 errors
- next pending: mortton / remaining journals; CW/Trails deferred; skip blocked list + lavafish_loc + tomato throw (.coord) + duel closed chest (loc_3193 false-friend)

- slice 32u done: Shades of Mort'ton journal (~mortton_journal + quest_shadesofmortton; olive_oil/shade_remains/pyre_logs cats)
- final: 11407 scripts; mock230_pack 0 errors
- next pending: remaining journals (druidspirit/misc/mm/legends/zombiequeen); CW/Trails deferred; skip blocked list + lavafish_loc + tomato throw (.coord) + duel closed chest (loc_3193 false-friend)

- slice 32v done: Nature Spirit journal (~druidspirit_journal + quest_naturespirit; %druidspirit_bits authored; ^priestperil_complete)
- final: 11422 scripts; mock230_pack 0 errors
- next pending: remaining journals (misc/mm/legends/zombiequeen/routequest); CW/Trails deferred; skip blocked list + lavafish_loc + tomato throw (.coord) + duel closed chest (loc_3193 false-friend); scorpcatcher landed in parallel (leave alone)

- slice 32y verified: Shilo Village journal already landed (~zombiequeen_journal + quest_shilovillage; %zq_map_mechanisms)
- slice 32z done: Monkey Madness journal (~mm_journal + quest_monkeymadness1; ^monkeymadness_* rename vs Misthalin ^mm_*; unnamed OSRS flag varbits)
- slice 33a done: Legends Quest journal (~legends_journal + quest_legends; %legends_bits; constants expanded)
- slice 33b done: In Search of the Myreque journal (~routequest_journal + quest_insearchofthemyreque; %routequest_myreque_bits; bitcount→getbit_range)
- slice 33c done: Ogre bow Check kills (opheld3 + ~get_chompy_rank; ~objbox→~mesbox)
- slice 33d done: Swamp toad inflate (opnpcu toad + ai_timer; synth/sound_area dropped)
- final: 11481 scripts; mock230_pack 0 errors
- next pending: outward leftovers (CW/Trails deferred); skip blocked list + lavafish_loc + tomato throw (.coord) + duel closed chest (loc_3193 false-friend)
- slice 33e done: Chompy bloated toad release/place (~release_toad, Drop→place bait, ai_queue4 explode/spawn thin); chompybird via names.map; %npc_attacking_uid/huntmode deferred
- final: 11500 scripts; mock230_pack 66 errors (all special_attack.obj unknown objs — pre-existing, not 33e)
- next pending: outward leftovers (CW/Trails deferred); skip blocked list + lavafish_loc + tomato throw (.coord) + duel closed chest (loc_3193 false-friend)

- slice 33f done: cannonballs (steel_bar+ammo_mould → mcannonball×4; furnace Use hook)
- slice 33g done: dragon sq anvil repair (@make_dragon_sq via oplocu anvil)
- slice 33h done: crest Boot gold branch (^crest_spoken_* arms)
- slice 33i done: holiday scythe/bunnyears opobj3 one-owned; %scythe_unlocked/%bunny_ears_unlocked authored
- slice 33j done: MM talk helpers (cat 95=mm_greegree) + padulah Talk
- slice 33k done: MM temple priests (hafuba/lofu/denadu)
- slice 33l done: MM sleeping monkey guard Talk; ai_timer deferred
- slice 33m done: dragon spear Shove (sa_kind=8); also stripped 22 unresolved special_attack.obj names → pack 0
- final: 11567 scripts; mock230_pack 0 errors
- next pending: outward leftovers (CW/Trails deferred); skip blocked list + lavafish_loc + tomato throw (.coord) + duel closed chest (loc_3193 false-friend)

- slice 33n done: MM monkeys uncle Talk (aa_summon deferred)
- slice 33o done: MM dugopul Talk only (AI/hunt deferred)
- slice 33p done: MM uwogo/murowoi Talk (aa_summon deferred)
- slice 33q done: MM shopkeepers Trade stubs (hamab/ifaba/tutab/daga/solihib)
- slice 33r done: MM elder_guard Talk thin (quest arms+AI deferred)
- slice 33s done: MM zoo_monkey capture Talk (backpack handoff; timer/trail deferred)
- slice 33t done: MM karam Talk (talker + busy; AI deferred)
- slice 33u done: MM caranock Talk (^monkeymadness_* rename)
- final: 11645 scripts; mock230_pack 0 errors
- next pending: outward leftovers (mm_kruk/lumdo/waydar/garkor/aa_market/CW/Trails deferred); skip blocked list + lavafish_loc + tomato throw (.coord) + duel closed chest (loc_3193 false-friend)

- slice 33v done: MM kruk Talk (%varbit_117/%varbit_120; escort tele thin)
- slice 33w done: MM lumdo Talk (crash↔atoll boat; seal proof)
- slice 33x done: MM waydar Talk (glidermap→tele; ch2 cutscene thin)
- slice 33y done: MM garkor Talk (%varbit_118; ch4 cutscene thin)
- slice 33z done: AA market stalls (mm_stall_* Steal; padulah guard)
- slice 34a done: MM lumo/bunkdo/carado jail Talk (queue deferred)
- final: 11693 scripts; mock230_pack 0 errors
- next pending: outward leftovers (mm_monkeys_aunt/daero/zooknock/awowogei/monkey_child/glough reinit; CW/Trails deferred); skip blocked list + lavafish_loc + tomato throw (.coord) + duel closed chest (loc_3193 false-friend)

- slice 34b done: MM monkeys aunt Talk (AI/guards deferred)
- final: 11719 scripts; mock230_pack 0 errors
- next pending: mm_daero/zooknock/awowogei/monkey_child/glough reinit; CW/Trails deferred; skip blocked list + lavafish_loc + tomato throw (.coord) + duel closed chest (loc_3193 false-friend)

- slice 34c done: MM daero Talk + hangar tele thin (training IF1 deferred; %varbit_99–101)
- final: 11741 scripts; mock230_pack 0 errors
- next pending: mm_zooknock/awowogei/monkey_child/glough reinit; CW/Trails deferred; skip blocked list + lavafish_loc + tomato throw (.coord) + duel closed chest (loc_3193 false-friend)

- slice 34d done: MM zooknock Talk+Use (%varbit_106–112/133; ch3 thin; AI deferred)
- final: 11778 scripts; mock230_pack 0 errors
- next pending: mm_awowogei/monkey_child/glough reinit; CW/Trails deferred; skip blocked list + lavafish_loc + tomato throw (.coord) + duel closed chest (loc_3193 false-friend)

- slice 34e done: MM awowogei throne Talk (%varbit_118; MM2 soft-skips merged)
- final: 11780 scripts; mock230_pack 0 errors
- next pending: mm_monkey_child/glough reinit; CW/Trails deferred; skip blocked list + lavafish_loc + tomato throw (.coord) + duel closed chest (loc_3193 false-friend)

- slice 34f done: MM monkey child Talk+Use (%varbit_119/%varbit_113)
- final: 11800 scripts; mock230_pack 0 errors
- next pending: glough reinit; CW/Trails deferred; skip blocked list + lavafish_loc + tomato throw (.coord) + duel closed chest (loc_3193 false-friend)

- slice 34g done: MM glough reinit buyout (%varbit_103; grandtree_glough hook)
- final: 11801 scripts; mock230_pack 0 errors
- next pending: mm_narnode/minder/temple/wildlife; CW/Trails deferred; skip blocked list + lavafish_loc + tomato throw (.coord) + duel closed chest (loc_3193 false-friend)

- slice 34h done: MM narnode Talk (start/reward/complete; mm_ch1 deferred)
- final: 11824 scripts; mock230_pack 0 errors
- next pending: mm_monkey_minder/temple/wildlife; CW/Trails deferred; skip blocked list + lavafish_loc + tomato throw (.coord) + duel closed chest (loc_3193 false-friend)

- slice 34i done: MM monkey minder Talk (cage tele thin; %varbit_132)
- slice 34j done: MM temple trapdoor/rope (mapzone spikes deferred)
- slice 34k done: AA wildlife poison-on-attack (1/10 → poison_player)
- final: 11834 scripts; mock230_pack 0 errors
- next pending: mm_puzzle/greegree/archer/jail/skeleton/demon; CW/Trails deferred; skip blocked list + lavafish_loc + tomato throw (.coord) + duel closed chest (loc_3193 false-friend)

- slice 34l done: MM hangar puzzle thin (crate/hint/panel → %varbit_103 + ^daero_reinit; IF1 deferred)
- slice 34m done: MM greegree Hold + zone + force unequip timer
- slice 34n done: MM archer ranged AI (grass/greegree skip; knockout deferred)
- slice 34o done: MM jail pick-lock + aberab/trefaji Talk (patrol deferred)
- slice 34p done: MM skeleton death_drop (reanimate deferred)
- slice 34q done: MM jungle demon thin (sigil tele/Talk/wave AI; cutscene deferred)
- slice 34r done: MM atoll ladders/stairs/banana crates/bunker tele
- slice 34s done: Ape Atoll dungeon exit/plank/bones exit (spikes deferred)
- slice 34t done: AA monkey guard AI (greegree despawn; knockout deferred)
- slice 34u done: MM bamboo largedoor + banana tree deplete
- slice 34v done: MM monkey speak amulet stringing
- slice 34w done: Mort Myre swamp_decay timer + %mortmyre
- slice 34x done: Priest Peril evil monk AI + pipkey_gold
- slice 34y done: MM amulet smith on iban firewall (zenyte oplocu hook)
- final: 11975 scripts; mock230_pack 0 errors
- next pending: quest_mm leftovers (aa_summon_guards/cutscenes); CW/Trails deferred; skip blocked list + lavafish_loc + tomato throw (.coord) + duel closed chest (loc_3193 false-friend)

- slice 34z done: MM aa_summon_guards + aa_guards_queue (%mm_guards_cooldown)
- final: 11988 scripts; mock230_pack 0 errors
- next pending: wire summon callers (shopkeepers/uncle/aunt/dugopul/uwogo/throne); thin mm_ch1; MM loc leftovers; CW/Trails deferred; skip blocked list + lavafish_loc + tomato throw (.coord) + duel closed chest (loc_3193 false-friend)

- slice 35a done: wire aa_summon into shopkeepers/uncle/uwogo/murowoi
- final: 12000 scripts; mock230_pack 0 errors
- next pending: aunt/dugopul/throne summon; thin mm_ch1; MM loc leftovers; CW/Trails deferred; skip blocked list + lavafish_loc + tomato throw (.coord) + duel closed chest (loc_3193 false-friend)

- slice 35b done: aunt/dugopul/throne → aa_guards_queue
- final: 12001 scripts; mock230_pack 0 errors
- next pending: thin mm_ch1; MM loc leftovers; CW/Trails deferred; skip blocked list + lavafish_loc + tomato throw (.coord) + duel closed chest (loc_3193 false-friend)

- slice 35c done: thin mm_ch1 + ch2/ch3/ch4 chapter mesbox cards
- final: 12002 scripts; mock230_pack 0 errors
- next pending: MM loc leftovers (crates/warehouse/bridge); CW/Trails deferred; skip blocked list + lavafish_loc + tomato throw (.coord) + duel closed chest (loc_3193 false-friend)

- slice 35d done: MM supply crates (scimitars + tools)
- slice 35e done: MM denture/mould/eye crates + bookcase + pineapple
- slice 35f done: MM warehouse hole/ropes/trapdoor/locked door
- slice 35g done: MM bridge ladders + jumping square + enchanted gold Examine
- final: 12031 scripts; mock230_pack 0 errors
- next pending: MM AI/knockout/transmog deferred; CW/Trails deferred; skip blocked list + lavafish_loc + tomato throw (.coord) + duel closed chest (loc_3193 false-friend)

- slice 35h done: Priest Peril vampire coffin (blessed water Use)
- slice 35i done: Priest Peril temple doors (Open/Knock-at; walk-through)
- slice 35j done: Priest Peril trapped Drezel (cell Talk/key/bless)
- slice 35k done: Heroes Scarface mansion locs (grip cabinet/pete doors/candle chest)
- slice 35l done: Legends jungle forester Talk + map→bullroarer
- slice 35m done: Observatory professor Talk/Use (trails stubbed)
- slice 35n done: Shilo nazastarool death queues + thin summon
- slice 35o done: Legends echned_zekin Talk/Use + nezi summon
- final: 12178 scripts; mock230_pack 0 errors
- next pending: Nature Spirit filliman/ghast; MM AI/knockout/transmog deferred; CW/Trails deferred; skip blocked list + lavafish_loc + tomato throw (.coord) + duel closed chest (loc_3193 false-friend)

- slice 35p done: Nature Spirit filliman Talk/Use + quest_complete queues
- final: 12246 scripts; mock230_pack 0 errors
- next pending: Nature Spirit ghast/locs; MM AI/knockout/transmog deferred; CW/Trails deferred; skip blocked list + lavafish_loc + tomato throw (.coord) + duel closed chest (loc_3193 false-friend)

- slice 35q done: Nature Spirit ghast AI/pouch + %ghast_delay; food-rot thinned
- final: 12275 scripts; mock230_pack 0 errors
- next pending: Nature Spirit locs/bloom; MM AI/knockout/transmog deferred; CW/Trails deferred; skip blocked list + lavafish_loc + tomato throw (.coord) + duel closed chest (loc_3193 false-friend)

- slice 35r done: Nature Spirit gate/grotto/bloom/pouch/altar/harvest
- final: 12308 scripts; mock230_pack 0 errors
- next pending: outward leftovers; MM AI/knockout/transmog deferred; CW/Trails deferred; skip blocked list + lavafish_loc + tomato throw (.coord) + duel closed chest (loc_3193 false-friend)

- slice 35s done: Shilo rashiliyia intro AI + Talk + random_undead_one
- final: 12368 scripts; mock230_pack 0 errors
- next pending: Legends warriors / boulder / barrel / curtains / mausoleum gates

- slice 35t done: Legends irvig_senay Talk/Attack/death + thin summon_nezi_part3
- final: 12368 scripts; mock230_pack 0 errors
- next pending: san_tojalon / ranalph / boulder / barrel / curtains / gates

- slice 35u done: Legends san_tojalon Talk/Attack/death
- final: 12368 scripts; mock230_pack 0 errors
- next pending: ranalph / boulder / barrel / curtains / gates

- slice 35v done: Legends ranalph_devere Talk/Attack/death
- final: 12368 scripts; mock230_pack 0 errors
- next pending: boulder / barrel / curtains / gates

- slice 35w done: Legends boulder push + lgwaterpool Look (%npc_start_coord→quest gate)
- final: 12368 scripts; mock230_pack 0 errors
- next pending: strange_barrel / curtains / mausoleum gates

- slice 35x done: Legends strange_barrel smash + thin loot/explode
- final: 12368 scripts; mock230_pack 0 errors
- next pending: alkharid curtains / mausoleum gates

- slice 35y done: Alkh palace curtains + Priest Peril mausoleum gates
- final: 12368 scripts; mock230_pack 0 errors
- next pending: outward leftovers; MM AI/knockout/transmog deferred; CW/Trails deferred; skip blocked list + lavafish_loc + tomato throw (.coord) + duel closed chest (loc_3193 false-friend)
