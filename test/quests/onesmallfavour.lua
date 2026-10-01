-- One Small Favour (tier 2). Driven against the real relay chain in
-- OSRS-Content/osrs239-content/server/scripts/quests/quest_onesmallfavour/
-- (onesmallfavour_relay.rs2, onesmallfavour_puzzles.rs2, onesmallfavour.constant)
-- plus the shared-npc owner files each leg merges into (yanni_salika.rs2,
-- jungle_forester.rs2, brian.rs2, aggie.rs2, dttd_haminfiltrate.rs2,
-- fred_the_farmer.rs2, idesofmilk.rs2, horvik.rs2, apothecary.rs2,
-- sanfew.rs2, gnome_glider.rs2).
--
-- Route: fixture stands beside Hans in Lumbridge. Every trapdoor/stair/
-- upstairs leg below is driven with a plain t.player.goto_tile onto the
-- destination tile at its own level (QUEST_AUTHORING.md section 2's
-- "Floors and ladders" rule) -- no click_loc on ham_multi_trapdoor,
-- fai_dwarf_trapdoor_down or spiralstairs, all three are generic
-- category=climb_* records this content pack already climbs for free.
--
-- Captain Bleemadge: gnome_glider.rs2:27-29 hands the One Small Favour
-- window (stages 75..86 and 190) to onesmallfavour_relay.rs2's
-- [label,osf_bleemadge_talk]; the whole chain is driven to Yanni's reward.

return {
    id = "onesmallfavour",
    fixture = "fresh_lumbridge.ini",
    max_frames = 240000,
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so a requirement fits
        "::give steel_bar 4",
        "::give bronze_bar 1",
        "::give iron_bar 1",
        "::give chisel 1",
        "::give guam_leaf 2",
        "::give marentill 1",
        "::give harralander 1",
        "::give hammer 1",
        "::give cup_empty 1",
        "::give bowl_hot_water 1",
        -- Cut gems for the landing lights: Quest Helper getItemRecommended
        -- opal2/jade2/redTopaz2 (wiki: "Two of each of the following cut
        -- gems, or a chisel to cut the uncut gems received during the
        -- quest"); the lamps' own uncut sapphires are cut in the run
        -- (sapphires cannot be crushed). Cutting the uncut opal/jade/red
        -- topaz would crush at random (gem.dbrow success_rate), so the
        -- recommended cut ones are brought instead.
        "::give jade 2",
        "::give opal 2",
        "::give red_topaz 2",
        -- Slagilith (level 92) and three level-44 gang dwarves: a pickaxe
        -- (the guide's recommended weapon for Slagilith), armour and food.
        "::give rune_pickaxe 1",
        "::give rune_full_helm 1",
        "::give rune_chainbody 1",
        "::give rune_platelegs 1",
        "::give rune_kiteshield 1",
        "::give shark 4",
        "::setlevel agility 36",
        "::setlevel crafting 25",
        "::setlevel herblore 18",
        "::setlevel smithing 30",
        "::setlevel attack 80",
        "::setlevel strength 80",
        "::setlevel defence 80",
        "::setlevel hitpoints 90",
        "::complete quest_runemysteries",
        "::complete quest_druidicritual",
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "onesmallfavour",
            constants = {
                not_started = 0,
                forester_axe = 5,
                axe_to_brian = 10,
                aggie_agreed = 20,
                aggie_told = 22,
                johanhus_told = 25,
                fred_told = 45,
                seth_told = 50,
                horvik_told = 55,
                apoth_told = 60,
                tassie_told = 65,
                hammerspike_told = 70,
                sanfew_told = 75,
                brewing_tea = 80,
                bleemadge_wants_trash = 86,
                arhein_told = 88,
                phantuwti_told = 90,
                wall_found = 95,
                cromperty_told = 100,
                tindel_told = 105,
                rantz_told = 110,
                gnormadium_told = 115,
                lights_repairing = 120,
                lights_fixed = 122,
                gnormadium_done = 125,
                rantz_done = 130,
                tindel_done = 135,
                cromperty_done = 140,
                slagilith_fight = 145,
                slagilith_defeated = 150,
                petra_freed = 152,
                phantuwti_weather = 160,
                vane_search = 175,
                vane_searched = 176,
                vane_loosened = 177,
                vane_parts_taken = 178,
                vane_repaired = 180,
                phantuwti_vane_done = 185,
                arhein_done = 190,
                bleemadge_done = 195,
                sanfew_done = 200,
                hammerspike_gang = 205,
                hammerspike_done = 225,
                tassie_done = 230,
                pot_made = 235,
                horvik_medicine = 238,
                horvik_done = 240,
                seth_done = 250,
                johanhus_done = 255,
                aggie_done = 260,
                brian_done = 265,
                forester_done = 270,
                complete = 285,
            },
            row = "quest_onesmallfavour",
            display = "One Small Favour",
            points = 2,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))
        t.exec("gear.pickaxe", t.player.equip, "rune_pickaxe")
        t.exec("gear.helm", t.player.equip, "rune_full_helm")
        t.exec("gear.body", t.player.equip, "rune_chainbody")
        t.exec("gear.legs", t.player.equip, "rune_platelegs")
        t.exec("gear.shield", t.player.equip, "rune_kiteshield")

        -- === Yanni Salika (Shilo Village) -- start the favour chain ======
        -- areas/area_shilo/scripts/yanni_salika.rs2:9-27 (osf_not_started
        -- branch, additive ahead of the antiques-trading fallback).
        t.exec("goto-talkToYanni", t.player.goto_tile, 2835, 2985, 0)
        t.exec("talkToYanni", t.player.talk_to, "shiloantiques", 1)
        t.exec("talkToYanni-dialog", t.chat.play, {
            "player:Is there anything else interesting to do around here?",
            "npc:Interesting, you say?",
            "player:Yes.",
            "npc:Splendid! Go and see them",
        })
        t.expect("quest.stage.forester_axe", t.quest.expect_stage("forester_axe"))

        -- === Jungle Forester (south of Shilo Village) =====================
        -- quests/quest_legends/scripts/jungle_forester.rs2:14-22
        -- ([opnpc1,jungleforester_m/f] @osf_jungleforester_check;).
        -- Real spawn row (areas/world/configs): jungleforester_f 2759,2944,0.
        t.exec("goto-talkToJungleForester", t.player.goto_tile, 2759, 2944, 0)
        t.exec("talkToJungleForester", t.player.talk_to, "jungleforester_f", 1)
        t.exec("talkToJungleForester-dialog", t.chat.play, {
            "player:I need to talk to you about red mahogany.",
            "npc:Red mahogany! Rare stuff around here.",
            "player:Okay, I'll take your axe to get it sharpened.",
            "npc:Bless you. Brian at the Port Sarim axe shop",
        })
        t.expect("quest.stage.axe_to_brian", t.quest.expect_stage("axe_to_brian"))
        t.exec("blunt_axe.received", t.inv.await, "favour_jungleforesteraxe_blunt", 1, 10)

        -- === Brian (Port Sarim axe shop) ===================================
        -- areas/port_sarim/scripts/brian.rs2:8-21 (osf_axe_to_brian branch,
        -- requires the blunt axe already in the backpack).
        t.exec("goto-talkToBrian", t.player.goto_tile, 3028, 3250, 0)
        t.exec("talkToBrian", t.player.talk_to, "brian", 1)
        t.exec("talkToBrian-dialog", t.chat.play, {
            "player:Do you sharpen axes?",
            "npc:That old thing?",
            "player:Look, can you sharpen this cursed axe or what?",
            "npc:Ok, ok, I'll do it! I'll go and see Aggie.",
        })
        t.expect("quest.stage.aggie_agreed", t.quest.expect_stage("aggie_agreed"))

        -- === Aggie (Draynor Village) ========================================
        -- areas/draynor/scripts/aggie.rs2 delegates to onesmallfavour_relay.rs2's
        -- [proc,osf_aggie_talk]: Brian's osf_aggie_agreed (20) is her stage, and
        -- "Oh, Ok, I'll see if I can find Jimmy." hands Johanhus osf_aggie_told
        -- (wiki Transcript "Asking Aggie to be a character witness"; Quest
        -- Helper talkToAggie's three dialogue steps).
        t.exec("goto-talkToAggie", t.player.goto_tile, 3086, 3259, 0)
        t.exec("talkToAggie", t.player.talk_to, "aggie", 1)
        t.exec("talkToAggie-dialog", t.chat.play, {
            "npc:What can I help you with?",
            "choose:Could I ask you about being a character witness?",
            "player:Could I ask you about being a character witness?",
            "npc:Not at the minute I'm afraid",
            "choose:Let me guess, you're going to ask me to do you a favour?",
            "player:Let me guess, you're going to ask me to do you a favour?",
            "npc:Would you my dear",
            "player:Hmm, I seem to have heard that one before.",
            "npc:Could you go on and check out that abandoned building",
            "choose:Oh, Ok, I'll see if I can find Jimmy.",
            "player:Oh, Ok, I'll see if I can find Jimmy.",
            "npc:Oh, thanks ever so much",
        })
        t.expect("quest.stage.aggie_told", t.quest.expect_stage("aggie_told"))

        -- === Johanhus Ulsbrecht (H.A.M. hideout, south chamber) ============
        -- quests/quest_deathtothedorgeshuun/scripts/dttd_haminfiltrate.rs2:74-83.
        -- Instanced band (z+6400): goto_tile the destination tile+level
        -- directly, no click_loc on ham_multi_trapdoor first (section 2).
        t.exec("goto-talkToJohanhus", t.player.goto_tile, 3171, 9620, 0)
        t.exec("talkToJohanhus", t.player.talk_to, "favour_johanhus_ulsbrecht", 1)
        t.exec("talkToJohanhus-dialog", t.chat.play, {
            "player:I'm looking for Jimmy the Chisel.",
            "npc:Jimmy? He owes us a debt he hasn't paid.",
            "player:And I suppose you need me to do you a favour?",
            "npc:As it happens",
            "player:Ok, Jimmy has to be worth more than a few scrawny chickens!",
            "npc:Take it or leave it.",
        })
        t.expect("quest.stage.johanhus_told", t.quest.expect_stage("johanhus_told"))

        -- === Fred the Farmer (north of Lumbridge) ==========================
        -- areas/lumbridge/scripts/fred_the_farmer.rs2:27-32.
        t.exec("goto-talkToFred", t.player.goto_tile, 3189, 3273, 0)
        t.exec("talkToFred", t.player.talk_to, "fred_the_farmer", 1)
        t.exec("talkToFred-dialog", t.chat.play, {
            "player:I need to talk to you about Jimmy.",
            "npc:Jimmy the dwarf? Not my business",
        })
        t.expect("quest.stage.fred_told", t.quest.expect_stage("fred_told"))

        -- === Seth Groats (farm north east of Lumbridge) ====================
        -- quests/quest_idesofmilk/scripts/idesofmilk.rs2:142-151.
        t.exec("goto-talkToSeth", t.player.goto_tile, 3223, 3293, 0)
        t.exec("talkToSeth", t.player.talk_to, "favour_seth_groats", 1)
        t.exec("talkToSeth-dialog", t.chat.play, {
            "player:Fred said you might be able to help with some chickens.",
            "npc:Chickens? My coops are falling apart",
            "player:Oh, ok! I guess it's not that much further to Varrock!",
        })
        t.expect("quest.stage.seth_told", t.quest.expect_stage("seth_told"))

        -- === Horvik the armourer (Varrock) ==================================
        -- areas/varrock/scripts/horvik.rs2: Seth's three steel bars pay his
        -- debt (Quest Helper talkToHorvik requires steelBars3), then the
        -- medicine favour and the five pigeon cages (wiki Transcript "Asking
        -- Horvik about chicken cages").
        t.exec("goto-talkToHorvik", t.player.goto_tile, 3229, 3438, 0)
        t.exec("talkToHorvik", t.player.talk_to, "horvik_the_armourer", 1)
        t.exec("talkToHorvik-dialog", t.chat.play, {
            "player:Hi, I need to talk to you about chicken cages!",
            "npc:Hmm, Seth eh!",
            "player:Ok, well I have three steel bars here",
            "npc:Ok then! Great",
            "player:Oh dear, you don't sound too well?",
            "npc:No, I'm not actually",
            "player:Well that's a shame",
            "npc:I'm sorry, but the most I can manage",
            "player:Oh I see, you need me to do you a favour?",
            "npc:Well, just one small favour",
            "choose:Ok, I guess one good turn deserves another.",
            "player:Ok, I guess one good turn deserves another.",
            "npc:Well that's jolly decent of you",
            "player:Well, hopefully, I'll be right back",
            "npc:it'll be a lot easier for me to simply adjust some existing pigeon cages",
            "npc:But first things first, bring me the medicine!",
        })
        t.expect("quest.stage.horvik_told", t.quest.expect_stage("horvik_told"))
        t.exec("talkToHorvik.bars", t.inv.await, "steel_bar", 1, 5)

        -- === Apothecary (west Varrock) ======================================
        -- areas/varrock/scripts/apothecary.rs2:18-24. (Not the Cadava-potion
        -- branch further down that file -- that is gated on %rjquest, a
        -- different quest's own stage, and never fires here.)
        t.exec("goto-talkToApoth", t.player.goto_tile, 3195, 3404, 0)
        t.exec("talkToApoth", t.player.talk_to, "apothecary", 1)
        t.exec("talkToApoth-dialog", t.chat.play, {
            "player:Talk about One Small Favour.",
            "npc:One Small Favour, is it?",
            "player:Oh, ok, I guess it's not that far to the Barbarian Village.",
            "player:I guess I can go to the Barbarian Village.",
        })
        t.expect("quest.stage.apoth_told", t.quest.expect_stage("apoth_told"))

        -- === Tassie Slipcast (Barbarian Village pottery) ====================
        -- onesmallfavour_relay.rs2:43-51.
        t.exec("goto-talkToTassie", t.player.goto_tile, 3084, 3408, 0)
        t.exec("talkToTassie", t.player.talk_to, "favour_tassie_slipcast", 1)
        t.exec("talkToTassie-dialog", t.chat.play, {
            "player:The Apothecary sent me. He says you're the one to speak to about the Dwarven Mine.",
            "npc:Ugh, don't remind me.",
            "player:Ok, I'll deal with Hammerspike!",
            "npc:Would you? He's in the west cavern",
        })
        t.expect("quest.stage.tassie_told", t.quest.expect_stage("tassie_told"))

        -- === Hammerspike Stoutbeard (Dwarven Mine, west cavern) =============
        -- onesmallfavour_relay.rs2:68-77. Instanced band (z+6400):
        -- goto_tile straight to the cavern tile, no click_loc on
        -- fai_dwarf_trapdoor_down first (section 2).
        t.exec("goto-talkToHammerspike", t.player.goto_tile, 2965, 9811, 0)
        t.exec("talkToHammerspike", t.player.talk_to, "favour_hammerspike_stoutbeard", 1)
        t.exec("talkToHammerspike-dialog", t.chat.play, {
            "player:Have you always been a gangster?",
            "npc:Ha! I run this cavern.",
            "player:She'd like you and your gang to leave the potters alone.",
            "npc:That's a lot to ask for nothing.",
            "player:Ok, another favour",
            "npc:Good. Sanfew up in Taverley owes me",
        })
        t.expect("quest.stage.hammerspike_told", t.quest.expect_stage("hammerspike_told"))

        -- === Sanfew (Taverley herblore store, upstairs) =====================
        -- areas/area_taverly/scripts/sanfew.rs2:12-19. Plane change: goto_tile
        -- straight onto the upstairs tile, no click_loc on spiralstairs
        -- first (section 2).
        t.exec("goto-talkToSanfew", t.player.goto_tile, 2897, 3426, 1)
        t.exec("talkToSanfew", t.player.talk_to, "sanfew", 1)
        t.exec("talkToSanfew-dialog", t.chat.play, {
            "player:Are you taking any new initiates?",
            "npc:Perhaps, if the applicant showed promise.",
            "player:Do you accept dwarves?",
            "player:A dwarf I know wants to become an initiate.",
            "npc:Hmm. Tell you what",
            "npc:- brew him a cup of Guthix rest",
            "player:Yep, it's a deal.",
        })
        t.expect("quest.stage.sanfew_told", t.quest.expect_stage("sanfew_told"))

        -- === Guthix rest tea + Captain Bleemadge (White Wolf Mountain) =====
        -- gnome_glider.rs2:27-29 hands %onesmallfavour 75..86 and 190 to
        -- onesmallfavour_relay.rs2's [label,osf_bleemadge_talk]. At 75 the
        -- first talk only sets osf_brewing_tea (relay.rs2:172-178); the tea
        -- is handed over on a SECOND talk (relay.rs2:180-191).
        t.exec("goto-talkToBleemadge", t.player.goto_tile, 2846, 3497, 0)
        t.ticks(3)
        t.exec("meetBleemadge", t.player.talk_to, "pilot_white_wolf", 1)
        t.exec("meetBleemadge-dialog", t.chat.play, {
            "player:Right-o, Captain Bleemadge?",
            "npc:That's me! You after a lift, gnome-friend?",
            "player:Sanfew sent me.",
            "npc:Sanfew, eh?",
        })
        t.expect("quest.stage.brewing_tea", t.quest.expect_stage("brewing_tea"))

        -- onesmallfavour_puzzles.rs2:163-172 (bowl of hot water on the cup)
        -- then brew_potion.rs2:90 -> ~osf_brew_tea (puzzles.rs2:196-211).
        t.exec("useBowlOnCup", t.player.use_item_on_item, "bowl_hot_water", "cup_empty")
        t.exec("useBowlOnCup.cup", t.inv.await, "cup_hot_water", 1, 10)
        t.exec("useHerbsOnCup", t.player.use_item_on_item, "guam_leaf", "cup_hot_water")
        t.exec("makeGuthixRest", t.inv.await, "cup_guthix_rest_3", 1, 10)

        t.exec("talkToBleemadge", t.player.talk_to, "pilot_white_wolf", 1)
        t.exec("talkToBleemadge-dialog", t.chat.play, {
            "player:I have a special tea here for you from Sanfew!",
            "npc:Ah, lovely, just what the herblorist ordered.",
            "npc:Much better already!",
        })
        t.expect("quest.stage.bleemadge_wants_trash", t.quest.expect_stage("bleemadge_wants_trash"))

        -- === Arhein (Catherby) -- arhein.rs2:21-26 ==========================
        t.exec("goto-talkToArhein", t.player.goto_tile, 2804, 3431, 0)
        t.exec("talkToArhein", t.player.talk_to, "arhein", 1)
        t.exec("talkToArhein-dialog", t.chat.play, {
            "player:I need to talk T.R.A.S.H. to you.",
            "npc:T.R.A.S.H.? Captain Bleemadge sent you",
            "player:Yes, Ok, I'll do it!",
        })
        t.expect("quest.stage.arhein_told", t.quest.expect_stage("arhein_told"))

        -- === Phantuwti Farsight (Seers' Village) -- relay.rs2:211-220 =======
        t.exec("goto-talkToPhantuwti", t.player.goto_tile, 2704, 3474, 0)
        t.exec("talkToPhantuwti", t.player.talk_to, "favour_phantuwti_farsight", 1)
        t.exec("talkToPhantuwti-dialog", t.chat.play, {
            "player:Hi, can you give me a weather forecast?",
            "npc:I would, if my seeing-tools",
            "player:What can I do to help?",
            "npc:Find Petra",
            "player:Yes, Ok, I'll do it.",
        })
        t.expect("quest.stage.phantuwti_told", t.quest.expect_stage("phantuwti_told"))

        -- === Goblin cave sculpture -- mcannon_cave_guard.rs2:8-14 (enter),
        -- onesmallfavour_puzzles.rs2:11-16 (search) ==========================
        t.exec("goto-enterGoblinCave", t.player.goto_tile, 2624, 3392, 0)
        t.exec("enterGoblinCave", t.player.click_loc, "mcannoncave", 1)
        t.ticks(3)
        local cave_r, cave_tile = t.world.tile()
        t.check("enterGoblinCave.landed", cave_r == "ok" and cave_tile.z > 9000,
            "after Enter: " .. tostring(cave_tile and (cave_tile.x .. "," .. cave_tile.z .. "," .. cave_tile.level)))
        t.exec("goto-searchWall", t.player.goto_tile, 2620, 9834, 0)
        t.exec("searchWall", t.player.click_loc, "favour_lady_in_wall", 1)
        t.exec("searchWall-dialog", t.chat.play, { "mesbox:crude sculpture of a woman" })
        t.expect("quest.stage.wall_found", t.quest.expect_stage("wall_found"))

        -- === Wizard Cromperty (East Ardougne) -- wizard_cromperty.rs2:14-20
        t.exec("goto-talkToCromperty", t.player.goto_tile, 2683, 3325, 0)
        t.exec("talkToCromperty", t.player.talk_to, "ardounge_wizard", 1)
        t.exec("talkToCromperty-dialog", t.chat.play, {
            "player:Chat.",
            "player:I need to talk to you about a girl stuck in some rock!",
            "npc:Stuck in rock?",
            "player:Oh! One more 'small favour'",
        })
        t.expect("quest.stage.cromperty_told", t.quest.expect_stage("cromperty_told"))

        -- === Tindel Marchant (Port Khazard) -- relay.rs2:253-262 ============
        t.exec("goto-talkToTindel", t.player.goto_tile, 2678, 3152, 0)
        t.exec("talkToTindel", t.player.talk_to, "tindel_marchant", 1)
        t.exec("talkToTindel-dialog", t.chat.play, {
            "player:Wizard Cromperty sent me to get some iron oxide.",
            "npc:Iron oxide!",
            "player:Ask about iron oxide.",
            "npc:Bring me back a proper comfy mattress",
            "player:Okay, I'll do it!",
        })
        t.expect("quest.stage.tindel_told", t.quest.expect_stage("tindel_told"))

        -- === Rantz (Feldip Hills) -- relay.rs2:286-293 ======================
        t.exec("goto-talkToRantz", t.player.goto_tile, 2630, 2980, 0)
        t.exec("talkToRantz", t.player.talk_to, "rantz", 1)
        t.exec("talkToRantz-dialog", t.chat.play, {
            "player:I need to talk to you about a mattress.",
            "npc:A mattress!",
            "player:Ok, I'll see what I can do.",
        })
        t.expect("quest.stage.rantz_told", t.quest.expect_stage("rantz_told"))

        -- === Gnormadium Avlafrim (glider strip) -- onesmallfavour_relay.rs2 ==
        -- Wiki Transcript "Helping Gnormadium with the gnome glider": his
        -- "Yes, I'll take a look at them." is 115 -> 120.
        t.exec("goto-talkToGnormadium", t.player.goto_tile, 2544, 2972, 0)
        t.exec("talkToGnormadium", t.player.talk_to, "gnormadium_avlafrim", 1)
        t.exec("talkToGnormadium-dialog", t.chat.play, {
            "player:Rantz said I should help you finish this project.",
            "npc:Rantz? *gulp*",
            "npc:I expect that it's far too complex for you",
            "choose:Yes, I'll take a look at them.",
            "player:Yes, I'll take a look at them.",
            "npc:Ok then, just pop over",
            "player:We'll see!",
        })
        t.expect("quest.stage.lights_repairing", t.quest.expect_stage("lights_repairing"))

        -- fixAllLamps: Quest Helper's take1..take8 / cutSaph / put1..put8 on
        -- the eight osf_multi_landinglight_* copies (maps/m39_46.jl2; row 1 at
        -- z 2974, row 2 at z 2969). Search takes the uncut gem (the cache's
        -- checklandinglights bit), the cut gem used on the light places it
        -- (fixedlandinglights). The two uncut sapphires are cut here with the
        -- chisel; the jade/opal/red topaz are the recommended cut ones.
        local lights = {
            { "osf_multi_landinglight_jade_1", 2554, 2974, "uncut jade", "jade" },
            { "osf_multi_landinglight_redtopaz_1", 2551, 2974, "uncut red topaz", "red_topaz" },
            { "osf_multi_landinglight_opal_1", 2548, 2974, "uncut opal", "opal" },
            { "osf_multi_landinglight_sapphire_1", 2545, 2974, "uncut sapphire", "sapphire" },
            { "osf_multi_landinglight_jade_1", 2554, 2969, "uncut jade", "jade" },
            { "osf_multi_landinglight_redtopaz_1", 2551, 2969, "uncut red topaz", "red_topaz" },
            { "osf_multi_landinglight_opal_1", 2548, 2969, "uncut opal", "opal" },
            { "osf_multi_landinglight_sapphire_1", 2545, 2969, "uncut sapphire", "sapphire" },
        }
        for i = 1, #lights do
            local light = lights[i]
            local step = "take" .. i
            t.exec(step, t.player.click_loc, light[1], 1, { at = { light[2], light[3] } })
            t.exec(step .. "-gem", t.chat.expect_text, "You find an " .. light[4] .. " in the landing light.")
            t.exec(step .. "-dialog", t.chat.play, { "*" })
            if light[5] == "sapphire" then
                t.exec("cutSaph-" .. i, t.player.use_item_on_item, "chisel", "uncut_sapphire")
                t.exec("cutSaph-" .. i .. ".cut", t.inv.await, "sapphire", 1, 10)
            end
            step = "put" .. i
            t.exec(step, t.player.use_on, light[5], t.player.by_symbol("loc", light[1]),
                { at = { light[2], light[3] } })
            t.exec(step .. "-placed", t.chat.expect_text, "It seems to look right.")
            local tally = i == #lights and "mesbox:You've fixed all the landing lights!"
                or (i == 1 and "mesbox:You've fixed one landing light so far..."
                    or ("mesbox:You've fixed " .. i .. " landing lights so far..."))
            t.exec(step .. "-dialog", t.chat.play, { "*", tally })
        end
        t.expect("quest.stage.lights_fixed", t.quest.expect_stage("lights_fixed"))
        t.exec("fixAllLamps", t.var.await_server, "fixedlandinglights", 255, 5)
        -- The leftover uncut opal/jade/red topaz stay in the pack: six slots,
        -- and the fullest point later (the vane parts on top of the pigeon
        -- cages) still leaves room (seam27_osf_full2 shot 320: 16 used).

        -- "I've fixed all the lights!": Gnormadium flicks all_lights_fixed.
        t.exec("talkToGnormadiumAgain", t.player.talk_to, "gnormadium_avlafrim", 1)
        t.exec("talkToGnormadiumAgain-dialog", t.chat.play, {
            "npc:Hello! Don't get in the way around here",
            "player:I've fixed all the lights!",
            "npc:Hmm. That seems a tad unlikely",
            "npc:I don't believe it - you fixed it!",
            "player:I know one ogre who'll be very pleased",
        })
        t.expect("quest.stage.gnormadium_done", t.quest.expect_stage("gnormadium_done"))
        t.exec("talkToGnormadiumAgain.lit", t.var.await_server, "all_lights_fixed", 1, 5)

        -- === Rantz, Tindel, Cromperty again =================================
        t.exec("goto-returnToRantz", t.player.goto_tile, 2630, 2980, 0)
        t.exec("returnToRantz", t.player.talk_to, "rantz", 1)
        t.exec("returnToRantz-dialog", t.chat.play, {
            "player:Ok, I've helped that Gnome",
            "npc:Splendid! Let's see to that mattress, then.",
        })
        t.expect("quest.stage.rantz_done", t.quest.expect_stage("rantz_done"))
        t.exec("returnToRantz.mattress", t.inv.await, "favour_matress_comfy", 1, 10)

        t.exec("goto-returnToTindel", t.player.goto_tile, 2678, 3152, 0)
        t.exec("returnToTindel", t.player.talk_to, "tindel_marchant", 1)
        t.exec("returnToTindel-dialog", t.chat.play, {
            "player:I have the mattress.",
            "npc:Ahh, lovely and comfy.",
        })
        t.expect("quest.stage.tindel_done", t.quest.expect_stage("tindel_done"))
        t.exec("returnToTindel.oxide", t.inv.await, "favour_iron_oxide", 1, 10)

        t.exec("goto-returnToCromperty", t.player.goto_tile, 2683, 3325, 0)
        t.exec("returnToCromperty", t.player.talk_to, "ardounge_wizard", 1)
        t.exec("returnToCromperty-dialog", t.chat.play, {
            "player:I have that iron oxide you asked for!",
            "npc:Marvellous! Here's your animate rock scroll",
        })
        t.expect("quest.stage.cromperty_done", t.quest.expect_stage("cromperty_done"))
        t.exec("returnToCromperty.scroll", t.inv.await, "favour_animate_rock", 1, 10)

        -- === Pigeon cages behind Jerico's house =============================
        -- Three `pigeons` ground spawns (areas/world/configs/m40_51.spawn:116-118);
        -- the guide wants five, so the loop waits for the respawn. Horvik
        -- converts them into the chicken cages (horvik.rs2, talkToHorvikFinal).
        t.exec("goto-getPigeonCages", t.player.goto_tile, 2619, 3324, 0)
        for cage = 1, 5 do
            local seen = t.await({
                level = function()
                    return t.world.obj_near("pigeons", 4) == "ok"
                end,
                note = "pigeon cage on the ground",
            }, 300)
            if seen == "ok" then
                t.player.click_obj("pigeons", 3)
                t.inv.await("pigeons", cage, 10)
            end
        end
        local cages_r, cages = t.inv.count("pigeons")
        t.check("getPigeonCages", cages_r == "ok" and cages >= 5,
            "pigeon cages carried after the pickup loop: " .. tostring(cages) .. " (need 5)")

        -- === Slagilith and Petra -- onesmallfavour_puzzles.rs2:27-64,
        -- onesmallfavour_relay.rs2:366-425 ====================================
        t.exec("goto-enterGoblinCaveAgain", t.player.goto_tile, 2624, 3392, 0)
        t.exec("enterGoblinCaveAgain", t.player.click_loc, "mcannoncave", 1)
        t.ticks(3)
        t.exec("goto-standNextToSculpture", t.player.goto_tile, 2617, 9835, 0)
        t.exec("standNextToSculpture", t.player.use_on, "favour_animate_rock",
            t.player.by_symbol("loc", "favour_lady_in_wall"))
        t.exec("standNextToSculpture-dialog", t.chat.play, { "mesbox:You read the animate rock scroll aloud." })
        t.expect("quest.stage.slagilith_fight", t.quest.expect_stage("slagilith_fight"))
        t.exec("killSlagilith", t.player.attack, "slagilith", 2, 15)
        t.exec("killSlagilith.dead", t.npc.await_dead_engaged, 200, 8)
        t.exec("killSlagilith.stage", t.var.await_server, "onesmallfavour", 150, 15)
        t.exec("readScrollAgain", t.player.use_on, "favour_animate_rock",
            t.player.by_symbol("loc", "favour_lady_in_wall"))
        t.exec("readScrollAgain-dialog", t.chat.play, { "mesbox:This time the spell strikes the sculpture." })
        t.expect("quest.stage.petra_freed", t.quest.expect_stage("petra_freed"))
        t.ticks(2)
        t.exec("talkToPetra", t.player.talk_to, "favour_petra", 1)
        t.exec("talkToPetra-dialog", t.chat.play, {
            "player:Are you alright? Phantuwti sent me to find you.",
            "npc:Oh, thank the gods!",
            "player:It's dealt with now.",
            "npc:I will, right away.",
        })
        t.expect("quest.stage.phantuwti_weather", t.quest.expect_stage("phantuwti_weather"))

        -- === Phantuwti, then the weathervane ================================
        t.exec("goto-returnToPhantuwti", t.player.goto_tile, 2704, 3474, 0)
        t.exec("returnToPhantuwti", t.player.talk_to, "favour_phantuwti_farsight", 1)
        t.exec("returnToPhantuwti-dialog", t.chat.play, {
            "player:I've released Petra, she should have returned.",
            "npc:She has! Thank you.",
        })
        t.expect("quest.stage.vane_search", t.quest.expect_stage("vane_search"))

        -- The weathervane (onesmallfavour_puzzles.rs2): search it (op 5,
        -- "Search"), use the hammer on it, search it again for the three
        -- broken parts -- Quest Helper searchVane / useHammerOnVane /
        -- searchVaneAgain, stages 175 / 176 / 177; texts from the wiki
        -- Transcript "Weather vane".
        t.exec("goto-searchVane", t.player.goto_tile, 2702, 3475, 3)
        t.ticks(2)
        t.exec("searchVane", t.player.click_loc, "osf_weathervane", 5)
        t.exec("searchVane-dialog", t.chat.play, { "mesbox:You search the weather vane..." })
        t.expect("quest.stage.vane_searched", t.quest.expect_stage("vane_searched"))
        t.exec("useHammerOnVane", t.player.use_on, "hammer", t.player.by_symbol("loc", "osf_weathervane"))
        t.exec("useHammerOnVane-dialog", t.chat.play, { "mesbox:You give the structure a good solid whack..." })
        t.expect("quest.stage.vane_loosened", t.quest.expect_stage("vane_loosened"))
        t.exec("searchVaneAgain", t.player.click_loc, "osf_weathervane", 5)
        t.exec("searchVaneAgain-ornament", t.chat.expect_text, "You find a broken ornament...")
        t.exec("searchVaneAgain-p1", t.chat.play, { "*" })
        t.exec("searchVaneAgain-directionals", t.chat.expect_text, "broken directionals...")
        t.exec("searchVaneAgain-p2", t.chat.play, { "*" })
        t.exec("searchVaneAgain-pillar", t.chat.expect_text, "and a broken rotating pillar.")
        t.exec("searchVaneAgain-p3", t.chat.play, { "*" })
        t.expect("quest.stage.vane_parts_taken", t.quest.expect_stage("vane_parts_taken"))
        t.exec("searchVaneAgain.parts", t.inv.await_all,
            { favour_ornament_broken = 1, favour_directionals_broken = 1, favour_pillar_broken = 1 }, 10)

        t.exec("goto-useVane123OnAnvil", t.player.goto_tile, 2712, 3494, 0)
        local anvil = t.player.by_symbol("loc", "anvil")
        t.exec("useVane123OnAnvil", t.player.use_on, "favour_directionals_broken", anvil)
        t.exec("useVane123OnAnvil.directionals", t.inv.await, "favour_directionals_fixed", 1, 10)
        t.exec("useVane123OnAnvil-ornament", t.player.use_on, "favour_ornament_broken", anvil)
        t.exec("useVane123OnAnvil.ornament", t.inv.await, "favour_ornament_fixed", 1, 10)
        t.exec("useVane123OnAnvil-pillar", t.player.use_on, "favour_pillar_broken", anvil)
        t.exec("useVane123OnAnvil.pillar", t.inv.await, "favour_pillar_fixed", 1, 10)

        t.exec("goBackUpLadder", t.player.goto_tile, 2702, 3475, 3)
        t.ticks(2)
        local vane = t.player.by_symbol("loc", "osf_weathervane")
        t.exec("useVane1", t.player.use_on, "favour_ornament_fixed", vane)
        t.exec("useVane1-dialog", t.chat.play, { "mesbox:You slot the ornament back into the housing." })
        t.exec("useVane2", t.player.use_on, "favour_directionals_fixed", vane)
        t.exec("useVane2-dialog", t.chat.play, { "mesbox:You slot the directionals back into the housing." })
        t.exec("useVane3", t.player.use_on, "favour_pillar_fixed", vane)
        t.exec("useVane3-dialog", t.chat.play, {
            "mesbox:You slot the rotating pillar back into the housing.",
            "mesbox:With the last part in place",
        })
        t.expect("quest.stage.vane_repaired", t.quest.expect_stage("vane_repaired"))

        t.exec("goto-finishWithPhantuwti", t.player.goto_tile, 2704, 3474, 0)
        t.exec("finishWithPhantuwti", t.player.talk_to, "favour_phantuwti_farsight", 1)
        t.exec("finishWithPhantuwti-dialog", t.chat.play, {
            "player:I've fixed the weather vane!",
            "npc:Marvellous! Here's your weather report",
        })
        t.expect("quest.stage.phantuwti_vane_done", t.quest.expect_stage("phantuwti_vane_done"))
        t.exec("finishWithPhantuwti.report", t.inv.await, "favour_weather_report", 1, 10)

        -- === The return legs: Arhein, Bleemadge, Sanfew =====================
        t.exec("goto-returnToArhein", t.player.goto_tile, 2804, 3431, 0)
        t.exec("returnToArhein", t.player.talk_to, "arhein", 1)
        t.exec("returnToArhein-dialog", t.chat.play, {
            "player:What did you want me to do again?",
            "player:I have the weather report for you.",
            "npc:Splendid!",
        })
        t.expect("quest.stage.arhein_done", t.quest.expect_stage("arhein_done"))

        t.exec("goto-returnToBleemadge", t.player.goto_tile, 2846, 3497, 0)
        t.ticks(3)
        t.exec("returnToBleemadge", t.player.talk_to, "pilot_white_wolf", 1)
        t.exec("returnToBleemadge-dialog", t.chat.play, {
            "player:Hey there, did you get your T.R.A.S.H?",
            "npc:That I did!",
        })
        t.expect("quest.stage.bleemadge_done", t.quest.expect_stage("bleemadge_done"))

        t.exec("goto-returnToSanfew", t.player.goto_tile, 2897, 3426, 1)
        t.exec("returnToSanfew", t.player.talk_to, "sanfew", 1)
        t.exec("returnToSanfew-dialog", t.chat.play, {
            "player:Hi there, the Gnome Pilot has agreed to take you to see the ogres!",
            "npc:Excellent! A deal's a deal",
        })
        t.expect("quest.stage.sanfew_done", t.quest.expect_stage("sanfew_done"))

        -- === Hammerspike and his gang -- relay.rs2:79-162 ===================
        t.exec("goto-returnToHammerspike", t.player.goto_tile, 2965, 9810, 0)
        t.exec("returnToHammerspike", t.player.talk_to, "favour_hammerspike_stoutbeard", 1)
        t.exec("returnToHammerspike-dialog", t.chat.play, { "npc:Sanfew took you seriously, did he?" })
        t.expect("quest.stage.hammerspike_gang", t.quest.expect_stage("hammerspike_gang"))
        t.exec("killGangMembers", t.player.attack, "favour_gangster_dwarf", 2, 15)
        t.exec("killGangMembers.dead1", t.npc.await_dead_engaged, 150, 8)
        t.exec("killGangMembers-2", t.player.attack, "favour_gangster_dwarf_2", 2, 15)
        t.exec("killGangMembers.dead2", t.npc.await_dead_engaged, 150, 8)
        t.exec("killGangMembers-3", t.player.attack, "favour_gangster_dwarf_3", 2, 15)
        t.exec("killGangMembers.dead3", t.npc.await_dead_engaged, 150, 8)
        t.exec("quest.stage.hammerspike_done", t.var.await_server, "onesmallfavour", 225, 15)
        t.exec("talkToHammerspikeFinal", t.player.talk_to, "favour_hammerspike_stoutbeard", 1)
        t.exec("talkToHammerspikeFinal-dialog", t.chat.play, { "npc:Alright, alright! You've made your point." })

        -- === Tassie, the pot lid, the Apothecary ============================
        -- Tassie gives the soft clay and teaches pot lids (wiki Transcript
        -- "Tassie"; walkthrough "Tassie, who will give you some soft clay");
        -- the pot is the one in the Barbarian Village helmet shop (Quest
        -- Helper pickUpPot; areas/world/configs/m48_53.spawn pot_empty
        -- 3074,3431).
        t.exec("goto-returnToTassie", t.player.goto_tile, 3085, 3408, 0)
        t.exec("returnToTassie", t.player.talk_to, "favour_tassie_slipcast", 1)
        t.exec("returnToTassie-dialog", t.chat.play, {
            "player:Hey there, Hammerspike won't be bothering you anymore!",
            "npc:Really! Fantastic!",
            "player:Well you could make me an airtight pot!",
            "npc:I'll do better than that!",
            "npc:Now, while pots are quite easy to make",
        })
        t.exec("returnToTassie-clay", t.chat.expect_text, "Tassie gives you some clay!")
        t.exec("returnToTassie-dialog2", t.chat.play, { "*", "npc:Ok then, just use it on the wheel over there!" })
        t.exec("returnToTassie-lids", t.chat.expect_text, "Tassie shows you how to make pot lids.")
        t.exec("returnToTassie-dialog3", t.chat.play, { "*" })
        t.expect("quest.stage.tassie_done", t.quest.expect_stage("tassie_done"))
        t.exec("returnToTassie.clay", t.inv.await, "softclay", 1, 10)

        t.exec("spinPotLid", t.player.use_on, "softclay", t.player.by_symbol("loc", "potterywheel"))
        local menu_r = t.ui.await_open("skillmulti", 10)
        local cell_r, cell = t.ui.widget("skillmulti:f")
        local press_r = t.ui.invoke(cell, 1)
        t.note("skillmulti open=" .. tostring(menu_r) .. " cell f=" .. tostring(cell_r) .. "/" .. tostring(cell)
            .. " invoke=" .. tostring(press_r))
        t.exec("spinPotLid.made", t.inv.await, "potlid_unfired", 1, 15)
        t.exec("firePotLid", t.player.use_on, "potlid_unfired",
            t.player.by_symbol("loc", "fai_barbarian_pottery_oven"))
        t.exec("firePotLid.fired", t.inv.await, "potlid", 1, 15)
        t.exec("goto-pickUpPot", t.player.goto_tile, 3074, 3430, 0)
        t.exec("pickUpPot", t.player.click_obj, "pot_empty", 3)
        t.exec("pickUpPot.pot", t.inv.await, "pot_empty", 1, 10)
        t.exec("usePotLidOnPot", t.player.use_item_on_item, "potlid", "pot_empty")
        t.exec("usePotLidOnPot.pot", t.inv.await, "favour_airtight_pot", 1, 10)
        t.expect("quest.stage.pot_made", t.quest.expect_stage("pot_made"))

        t.exec("goto-returnToApothecary", t.player.goto_tile, 3195, 3404, 0)
        t.exec("returnToApothecary", t.player.talk_to, "apothecary", 1)
        t.exec("returnToApothecary-dialog", t.chat.play, {
            "player:Talk about One Small Favour.",
            "npc:An airtight pot, wonderful!",
        })
        t.exec("returnToApothecary.items", t.inv.await_all,
            { favour_breathing_salts = 1, favour_herbal_tincture = 1 }, 10)

        -- === Horvik, Seth, Johanhus, Aggie, Brian, the forester, Yanni ======
        -- Horvik: the medicine first (Quest Helper returnToHorvik), then the
        -- five pigeon cages become chicken cages (talkToHorvikFinal) -- wiki
        -- Transcript "Giving the items to Horkiv" / "Getting the chicken cages".
        t.exec("goto-returnToHorvik", t.player.goto_tile, 3229, 3437, 0)
        t.exec("returnToHorvik", t.player.talk_to, "horvik_the_armourer", 1)
        t.exec("returnToHorvik-dialog", t.chat.play, {
            "player:I have the tincture and the breathing salts.",
            "npc:Wonderful! That's just great! I just need the pigeon cages now.",
        })
        t.expect("quest.stage.horvik_medicine", t.quest.expect_stage("horvik_medicine"))
        t.exec("talkToHorvikFinal", t.player.talk_to, "horvik_the_armourer", 1)
        t.exec("talkToHorvikFinal-dialog", t.chat.play, {
            "player:I have the five pigeon cages you asked for!",
            "npc:Great stuff",
        })
        t.exec("talkToHorvikFinal-handover", t.chat.expect_text, "You hand over the pigeon cages.")
        t.exec("talkToHorvikFinal-dialog2", t.chat.play, {
            "*",
            "mesbox:Horvik works for sometime on the pigeon cages",
            "npc:There you go then! There's your chicken cages!",
        })
        t.expect("quest.stage.horvik_done", t.quest.expect_stage("horvik_done"))
        t.exec("talkToHorvikFinal.cages", t.inv.await, "favour_chicken_cage", 5, 10)

        t.exec("goto-returnToSeth", t.player.goto_tile, 3223, 3293, 0)
        t.exec("returnToSeth", t.player.talk_to, "favour_seth_groats", 1)
        t.exec("returnToSeth-dialog", t.chat.play, {
            "player:I have the chicken cages Horvik made up for you.",
            "npc:Perfect fit!",
        })
        t.expect("quest.stage.seth_done", t.quest.expect_stage("seth_done"))

        t.exec("goto-returnToJohnahus", t.player.goto_tile, 3171, 9620, 0)
        t.exec("returnToJohnahus", t.player.talk_to, "favour_johanhus_ulsbrecht", 1)
        t.exec("returnToJohnahus-dialog", t.chat.play, {
            "player:I have the chickens Seth Groats promised you.",
            "npc:You're in luck",
        })
        t.expect("quest.stage.johanhus_done", t.quest.expect_stage("johanhus_done"))

        t.exec("goto-returnToAggie", t.player.goto_tile, 3086, 3259, 0)
        t.exec("returnToAggie", t.player.talk_to, "aggie", 1)
        t.exec("returnToAggie-dialog", t.chat.play, {
            "player:Good news! Jimmy has been released!",
            "npc:Wonderful! I'll go have a word with Brian myself.",
        })
        t.expect("quest.stage.aggie_done", t.quest.expect_stage("aggie_done"))

        t.exec("goto-returnToBrian", t.player.goto_tile, 3028, 3250, 0)
        t.exec("returnToBrian", t.player.talk_to, "brian", 1)
        t.exec("returnToBrian-dialog", t.chat.play, {
            "player:I've returned with good news.",
            "npc:Aggie spoke up for me!",
        })
        t.expect("quest.stage.brian_done", t.quest.expect_stage("brian_done"))

        t.exec("goto-returnToForester", t.player.goto_tile, 2759, 2944, 0)
        t.exec("returnToForester", t.player.talk_to, "jungleforester_f", 1)
        t.exec("returnToForester-dialog", t.chat.play, {
            "player:Good news, I have your sharpened axe!",
            "npc:Wonderful, thank you!",
        })
        t.expect("quest.stage.forester_done", t.quest.expect_stage("forester_done"))
        t.exec("returnToForester.log", t.inv.await, "favour_mahogany_log", 1, 10)

        local snap_r, snap = t.skill.snapshot()
        t.check("reward.snapshot", snap_r == "ok",
            "stats before the hand-in, for the reward rows: agility level " ..
            tostring(snap and snap.agility and snap.agility.level))
        t.exec("goto-returnToYanni", t.player.goto_tile, 2835, 2985, 0)
        t.exec("returnToYanni", t.player.talk_to, "shiloantiques", 1)
        t.exec("returnToYanni-dialog", t.chat.play, {
            "player:Here's the red mahogany you asked for.",
            "npc:Ah, perfect!",
        })
        t.exec("quest.stage.complete", t.var.await_server, "onesmallfavour", 285, 15)
        t.quest.expect_complete()
        -- Rewards (yanni_salika.rs2:33-37): two reward lamps and the steel key ring.
        t.exec("reward.thosf_reward_lamp", t.inv.expect_has, "thosf_reward_lamp", 2)
        t.exec("reward.favour_key_ring", t.inv.expect_has, "favour_key_ring", 1)
        t.finish(0)
        return
    end,
}
