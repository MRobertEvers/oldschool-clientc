# Script name collisions — the remaining queue

A `[trigger,subject]` declared twice does not compose and has no precedence
rule. Both declarations take a script id, but `finish_script` resolves every
body back to the **first** matching name, so whichever file the compiler
reaches **last** silently replaces the other's body — and the loser's id is
left empty. Nothing is reported at run time: the npc or loc simply runs the
wrong script.

That is how Sheep Shearer (quest_coldwar beat Fred), Rune Mysteries
(quest_templeoftheeye beat Sedridor), A Tail of Two Cats (its own file beat
itself twice) and the Giant Mole's mud (a `return;` stub beat the real proc)
all became dead content.

`sscompile` now prints `warning: duplicate script name '...'` for each one.
Once this queue is empty the warning should become a hard error, next to the
`debugproc` / `[login,_]` singletons that already are.

## The fix, every time

Keep **one** trigger, in the file that owns the npc/loc (normally `areas/`,
or the base file for a quest's own npc). Every other file that needs it moves
its body into a `[label,...]` of its own and the canonical trigger branches to
it on that quest's varp — the idiom `areas/lumbridge/scripts/fred_the_farmer.rs2`
and `areas/varrock/scripts/apothecary.rs2` now use.

## Remaining

[ai_queue3,grandtree_blackdemon]  x2
    quests/quest_grandtree/scripts/grandtree_black_demon.rs2:23
    drop_tables/scripts/black_demon.rs2:11
[label,brother_kojo_before_placing_cogs]  x2
    quests/quest_cog/scripts/brother_kojo.rs2:54
    areas/area_ardougne_east/scripts/brother_kojo.rs2:47
[label,brother_kojo_placed_all_cogs]  x2
    quests/quest_cog/scripts/brother_kojo.rs2:70
    areas/area_ardougne_east/scripts/brother_kojo.rs2:63
[label,brother_kojo_placed_one_cog]  x2
    quests/quest_cog/scripts/brother_kojo.rs2:59
    areas/area_ardougne_east/scripts/brother_kojo.rs2:52
[label,brother_kojo_placed_three_cogs]  x2
    quests/quest_cog/scripts/brother_kojo.rs2:67
    areas/area_ardougne_east/scripts/brother_kojo.rs2:60
[label,brother_kojo_placed_two_cogs]  x2
    quests/quest_cog/scripts/brother_kojo.rs2:63
    areas/area_ardougne_east/scripts/brother_kojo.rs2:56
[label,brother_kojo_post_clock_tower]  x2
    quests/quest_cog/scripts/brother_kojo.rs2:76
    areas/area_ardougne_east/scripts/brother_kojo.rs2:69
[label,citizen_dialogue_eastardy]  x2
    areas/area_ardougne_east/scripts/man_east_ardougne.rs2:11
    areas/area_ardougne_east/scripts/ardougne_east_thin.rs2:101
[label,cog_start_options]  x2
    quests/quest_cog/scripts/brother_kojo.rs2:36
    areas/area_ardougne_east/scripts/brother_kojo.rs2:29
[label,cog_start_quest]  x2
    quests/quest_cog/scripts/brother_kojo.rs2:28
    areas/area_ardougne_east/scripts/brother_kojo.rs2:21
[label,tramp_phoenixmember]  x2
    areas/varrock/scripts/tramppg.rs2:54
    areas/varrock/scripts/tramp.rs2:58
[opheldu,bucket_milk]  x2
    skill_cooking/scripts/cakes.rs2:24
    skill_cooking/scripts/gnome_cooking/gnome_cooking.rs2:124
[opheldu,chocolate_bar]  x3
    skill_herblore/scripts/grind_ingredient.rs2:58
    skill_cooking/scripts/gnome_cooking/gnome_seasoning.rs2:85
    skill_cooking/scripts/gnome_cooking/gnome_cooking.rs2:45
[opheldu,chocolate_dust]  x2
    skill_herblore/scripts/brew_potion.rs2:199
    skill_cooking/scripts/gnome_cooking/gnome_seasoning.rs2:84
[opheldu,cooked_meat]  x2
    quests/quest_belowicemountain/scripts/belowicemountain.rs2:284
    skill_cooking/scripts/cooking_inv/scripts/cooked_meat.rs2:9
[opheldu,knife]  x3
    quests/quest_thegreatbrainrobbery/scripts/brain_castle.rs2:125
    quests/quest_spiritsoftheelid/scripts/elid_genie.rs2:95
    skill_fletching/scripts/cut_logs.rs2:14
[opheldu,leather]  x2
    quests/quest_coldwar/scripts/coldwar_outpost.rs2:169
    skill_crafting/scripts/leather/leather.rs2:25
[opheldu,lemon]  x2
    skill_cooking/scripts/cutting_fruit.rs2:10
    skill_cooking/scripts/gnome_cooking/gnome_cooking.rs2:120
[opheldu,lime]  x2
    skill_cooking/scripts/cutting_fruit.rs2:22
    skill_cooking/scripts/gnome_cooking/gnome_cooking.rs2:122
[opheldu,needle]  x2
    quests/quest_spiritsoftheelid/scripts/elid_house.rs2:68
    skill_crafting/scripts/leather/leather.rs2:10
[opheldu,orange]  x2
    skill_cooking/scripts/cutting_fruit.rs2:16
    skill_cooking/scripts/gnome_cooking/gnome_cooking.rs2:121
[opheldu,pineapple]  x2
    skill_cooking/scripts/cutting_fruit.rs2:28
    skill_cooking/scripts/gnome_cooking/gnome_cooking.rs2:123
[opheldu,redberries]  x2
    quests/quest_handinthesand/scripts/handsand_betty.rs2:99
    skill_cooking/scripts/cooking_inv/scripts/pies.rs2:37
[opheldu,toads_legs]  x2
    skill_herblore/scripts/brew_potion.rs2:196
    skill_cooking/scripts/gnome_cooking/gnome_seasoning.rs2:34
[opheldu,white_berries]  x2
    quests/quest_handinthesand/scripts/handsand_betty.rs2:122
    skill_herblore/scripts/brew_potion.rs2:163
[oploc1,bookcase]  x2
    quests/quest_thegreatbrainrobbery/scripts/brain_prayerbook.rs2:11
    general_use/scripts/bookcases.rs2:6
[oploc1,brimstone_dungeon_exit]  x2
    quests/quest_ascentofarceuus/scripts/ascentofarceuus_locs.rs2:32
    minigames/minigame_karuulm/scripts/karuulm.rs2:11
[oploc1,gertrudeempty_crate]  x2
    quests/quest_fluffs/scripts/quest_fluffs.rs2:244
    general_use/scripts/crates.rs2:14
[oploc1,grotto_door_druidicspirit]  x2
    quests/quest_nightatthetheatre/scripts/nightatthetheatre.rs2:220
    quests/quest_druidspirit/scripts/quest_druidspirit.rs2:105
[oploc1,mm2_cavern_entrance]  x2
    quests/quest_monkeymadnessii/scripts/monkeymadnessii.rs2:358
    minigames/minigame_gorilla/scripts/gorilla.rs2:4
[oploc1,mm_climbing_rope_bottom_temple]  x2
    quests/quest_mm/scripts/mm_temple.rs2:26
    minigames/minigame_zenyte/scripts/zenyte.rs2:23
[oploc1,mm_temple_trapdoor]  x2
    quests/quest_mm/scripts/mm_temple.rs2:9
    minigames/minigame_zenyte/scripts/zenyte.rs2:4
[oploc1,mm_temple_trapdoor_open]  x3
    quests/quest_mm/scripts/mm_temple.rs2:15
    ladders_stairs/scripts/climb_shared.rs2:42
    minigames/minigame_zenyte/scripts/zenyte.rs2:11
[oploc1,myq4_hideout_trapdoor_open]  x2
    quests/quest_tasteofhope/scripts/tasteofhope.rs2:193
    ladders_stairs/scripts/climb_shared.rs2:45
[oploc1,pipeastsidetrapdoor_open]  x2
    quests/quest_sinsofthefather/scripts/sinsofthefather.rs2:353
    ladders_stairs/scripts/climb_shared.rs2:54
[oploc1,sailing_gangplank_disembark]  x2
    quests/quest_pandemonium/scripts/pandemonium.rs2:260
    quests/quest_redreef/scripts/redreef.rs2:377
[oploc1,trapdoor_open]  x2
    ladders_stairs/scripts/climb_shared.rs2:60
    general_use/scripts/trapdoors.rs2:10
[oploc2,mm_temple_trapdoor_open]  x2
    quests/quest_mm/scripts/mm_temple.rs2:23
    minigames/minigame_zenyte/scripts/zenyte.rs2:18
[opnpc1,ardougnian_female1]  x2
    areas/area_ardougne_east/scripts/man_east_ardougne.rs2:9
    areas/area_ardougne_east/scripts/ardougne_east_thin.rs2:98
[opnpc1,ardougnian_male1]  x2
    areas/area_ardougne_east/scripts/man_east_ardougne.rs2:8
    areas/area_ardougne_east/scripts/ardougne_east_thin.rs2:95
[opnpc1,arhein]  x2
    quests/quest_currentaffairs/scripts/currentaffairs.rs2:62
    areas/area_catherby/scripts/arhein.rs2:10
[opnpc1,brother_kojo]  x2
    quests/quest_cog/scripts/brother_kojo.rs2:10
    areas/area_ardougne_east/scripts/brother_kojo.rs2:10
[opnpc1,death_woman_indoors1]  x2
    quests/quest_atailoftwocats/scripts/twocats.rs2:49
    areas/area_burthorpe/scripts/burthorpe_thin_npcs.rs2:33
[opnpc1,elias_white_vis]  x2
    quests/quest_curseofarrav/scripts/curseofarrav.rs2:32
    quests/quest_defenderofvarrock/scripts/dov_elias.rs2:167
[opnpc1,filliman_tarlock_ns]  x2
    quests/quest_nightatthetheatre/scripts/nightatthetheatre.rs2:212
    quests/quest_druidspirit/scripts/filliman.rs2:632
[opnpc1,goblin_cook]  x2
    quests/quest_recipefordisaster/scripts/recipefordisaster_goblins.rs2:57
    areas/lumbridge/scripts/tutors.rs2:78
[opnpc1,harry]  x2
    quests/quest_currentaffairs/scripts/currentaffairs.rs2:308
    areas/area_catherby/scripts/harry.rs2:8
[opnpc1,ics_little_sphinx]  x2
    quests/quest_dragonslayer2/scripts/dragonslayer2.rs2:1294
    quests/quest_atailoftwocats/scripts/twocats.rs2:144
[opnpc1,misc_smithy]  x2
    quests/quest_misc/scripts/misc_smithy.rs2:8
    areas/area_miscellania/scripts/derrik.rs2:7
[opnpc1,monk_ardougne]  x2
    areas/area_ardougne_east/scripts/ardounge_monk.rs2:8
    areas/area_ardougne_east/scripts/ardougne_east_thin.rs2:78
[opnpc1,peng_larry_rell]  x2
    quests/quest_makingfriendswithmyarm/scripts/makingfriendswithmyarm.rs2:121
    quests/quest_coldwar/scripts/coldwar_larry.rs2:194
[opnpc1,risingsun_barmaid]  x2
    quests/quest_belowicemountain/scripts/belowicemountain.rs2:363
    areas/falador/scripts/barmaid.rs2:10
[opnpc1,tailorp]  x2
    quests/quest_eaglepeak/scripts/asyff.rs2:5
    areas/varrock/scripts/fancy_dress_shop_owner.rs2:8
[opnpc1,tea_seller]  x2
    quests/quest_templeoftheeye/scripts/templeoftheeye.rs2:126
    areas/varrock/scripts/tea_seller.rs2:9
[opnpc1,traiborn]  x3
    quests/quest_templeoftheeye/scripts/templeoftheeye.rs2:209
    quests/quest_recipefordisaster/scripts/recipefordisaster_lumbridgeguide.rs2:50
    areas/draynor/scripts/traiborn.rs2:2
[opnpc1,tramppg]  x2
    areas/varrock/scripts/tramppg.rs2:5
    areas/varrock/scripts/tramp.rs2:9
[opnpc1,tt_raley_conch]  x2
    quests/quest_troubledtortugans/scripts/troubledtortugans.rs2:145
    quests/quest_redreef/scripts/redreef.rs2:55
[opnpc1,vmq2_quetzal_keeper_fortis]  x2
    quests/quest_ethicallyacquiredantiquities/scripts/ethicallyacquiredantiquities.rs2:30
    quests/quest_twilightspromise/scripts/twilightspromise.rs2:361
[opnpc1,zoo_keeper]  x2
    areas/area_ardougne_east/scripts/zoo_keeper.rs2:9
    areas/area_ardougne_east/scripts/ardougne_east_thin.rs2:31
[opnpc3,tailorp]  x2
    quests/quest_eaglepeak/scripts/asyff.rs2:37
    areas/varrock/scripts/fancy_dress_shop_owner.rs2:24
