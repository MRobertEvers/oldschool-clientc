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
-- STOPS at talkToBleemadge: pilot_white_wolf's world spawn row
-- (areas/world/configs/m44_54.spawn) carries the MULTINPC BASE symbol
-- (all.npc's own [pilot_white_wolf] multinpc5=pilot_white_wolf_base), so
-- per QUEST_AUTHORING.md trap 19 the live npc's op lookup resolves to
-- areas/area_gnome/scripts/gnome_glider.rs2:16
-- ([opnpc1,pilot_white_wolf] @gnome_pilot_talk;), never to
-- onesmallfavour_relay.rs2:165's [opnpc1,pilot_white_wolf_base] -- which
-- is exactly this trap's dead-code shape. gnome_pilot_talk has no
-- onesmallfavour branch at all, so the Guthix-rest-tea exchange Sanfew
-- sends the player to (osf_brewing_tea) can never be reached by a real
-- click, and everything the guide drives after Sanfew (Arhein, Phantuwti,
-- the goblin-cave sculpture/Slagilith, the weathervane, the dwarf-gang
-- fight, the pot lid, and every return leg back down to Yanni) is
-- unreachable behind it. content_bug, not a driver seam.

return {
    id = "onesmallfavour",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so a requirement fits
        "::give steel_bar 4",
        "::give bronze_bar 1",
        "::give iron_bar 1",
        "::give chisel 1",
        "::give guam_leaf 2",
        "::give marentill 1",
        "::give harralander 1",
        "::give cup_empty 1",
        "::give pot_empty 1",
        "::give bowl_hot_water 1",
        "::setlevel agility 36",
        "::setlevel crafting 25",
        "::setlevel herblore 18",
        "::setlevel smithing 30",
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
                johanhus_told = 25,
                fred_told = 45,
                seth_told = 50,
                horvik_told = 55,
                apoth_told = 60,
                tassie_told = 65,
                hammerspike_told = 70,
                sanfew_told = 75,
                brewing_tea = 80,
                complete = 285,
            },
            row = "quest_onesmallfavour",
            display = "One Small Favour",
            points = 2,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

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
        -- areas/draynor/scripts/aggie.rs2:10-14 delegates to
        -- onesmallfavour_relay.rs2's [proc,osf_aggie_talk] FIRST. That proc's
        -- own "character witness" branch (relay.rs2:20-29) is gated on the
        -- SAME %onesmallfavour value (^osf_axe_to_brian, 10) that Brian's own
        -- branch above already consumed and advanced past to
        -- ^osf_aggie_agreed (20) -- a genuine stage-value collision between
        -- two owner scripts (both check ==10, both set ->20). Brian is
        -- visited first here (matching his own "I'll go and see Aggie" line
        -- and the narrative -- Aggie's dialogue explains a plot beat the
        -- player could only ask about after hearing Brian decline), so by
        -- the time Aggie is reached the stage has already moved on and her
        -- real page never lights up; she falls through to the generic
        -- "Hello again, dearie." aggie.rs2 fallback (chat progress here does
        -- not gate anything downstream -- osf_johanhus_told is set by
        -- Johanhus himself below, not by this visit). Read what the client
        -- actually shows rather than assert exact text neither owner script
        -- promises at this stage.
        t.exec("goto-talkToAggie", t.player.goto_tile, 3086, 3259, 0)
        t.exec("talkToAggie", t.player.talk_to, "aggie", 1)
        t.exec("talkToAggie-dialog", t.chat.drain, { max_pages = 6, shots = true })
        t.check("quest.stage.after_aggie", select(1, t.quest.stage()) == "ok",
            "stage after Aggie: " .. tostring(select(2, t.quest.stage())) ..
            " (relay.rs2:20 and brian.rs2:11 both gate on osf_axe_to_brian and both advance to" ..
            " osf_aggie_agreed -- Brian's own visit above already consumed it, so Aggie's own" ..
            " 'character witness' page never lit up this run; see aggie.rs2:39 fallback)")

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
        -- areas/varrock/scripts/horvik.rs2:13-18.
        t.exec("goto-talkToHorvik", t.player.goto_tile, 3229, 3438, 0)
        t.exec("talkToHorvik", t.player.talk_to, "horvik_the_armourer", 1)
        t.exec("talkToHorvik-dialog", t.chat.play, {
            "player:Hi, I need to talk to you about chicken cages!",
            "npc:Cages, eh?",
            "player:Ok, I guess one good turn deserves another.",
        })
        t.expect("quest.stage.horvik_told", t.quest.expect_stage("horvik_told"))

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
            "player:Yep, it's a deal.",
        })
        t.expect("quest.stage.sanfew_told", t.quest.expect_stage("sanfew_told"))

        -- === Captain Bleemadge (White Wolf Mountain gnome glider base) =====
        -- The guide's next step: brew a cup of Guthix rest (Sanfew's own
        -- recipe -- guam x2, marrentill, harralander, all four already
        -- given in setup) and bring it to Captain Bleemadge
        -- (onesmallfavour_relay.rs2:180-217, osf_sanfew_told branch) to
        -- unlock osf_brewing_tea. The live npc at White Wolf Mountain is
        -- spawned as `pilot_white_wolf` (areas/world/configs/m44_54.spawn),
        -- which is a MULTINPC BASE (configs/all.npc [pilot_white_wolf]
        -- multinpc5=pilot_white_wolf_base) -- trap 19's exact shape. The
        -- op lookup is keyed on the spawned symbol only, so talking to it
        -- reaches areas/area_gnome/scripts/gnome_glider.rs2:16
        -- ([opnpc1,pilot_white_wolf] @gnome_pilot_talk;), which has no
        -- onesmallfavour branch anywhere in gnome_pilot_talk -- never
        -- onesmallfavour_relay.rs2:165's [opnpc1,pilot_white_wolf_base],
        -- which is unreachable dead code from a live spawn. Prove it live
        -- before blocking: talk to him, read what actually opens, confirm
        -- the stage never moves.
        t.exec("goto-talkToBleemadge", t.player.goto_tile, 2847, 3499, 0)
        t.exec("talkToBleemadge", t.player.talk_to, "pilot_white_wolf", 1)
        local chat_kind = t.chat.kind()
        local text_result, chat_text = t.chat.text()
        t.check("talkToBleemadge-observed", chat_kind ~= "none",
            "chat kind=" .. tostring(chat_kind) .. " text=" ..
            tostring(text_result == "ok" and chat_text or text_result) ..
            " -- not the onesmallfavour Guthix-rest-tea exchange" ..
            " (relay.rs2:181 'That's me! You after a lift, gnome-friend?' never appears)")
        t.chat.close()
        t.exec("quest.stage.still_sanfew_told", t.var.await_server, "onesmallfavour", 75, 10)

        -- Every remaining Quest Helper step lives behind osf_brewing_tea
        -- (80), which the block above proves is unreachable through a real
        -- click -- same root cause for all of them, cited once per step so
        -- helper_coverage.py grades each a declared CONTENT_GAP rather than
        -- UNMATCHED (trap 32).
        -- GUIDE-GAP: talkToArhein unreachable behind onesmallfavour_relay.rs2:165's dead pilot_white_wolf_base trigger
        -- GUIDE-GAP: talkToPhantuwti unreachable behind onesmallfavour_relay.rs2:165's dead pilot_white_wolf_base trigger
        -- GUIDE-GAP: enterGoblinCave unreachable behind onesmallfavour_relay.rs2:165's dead pilot_white_wolf_base trigger
        -- GUIDE-GAP: searchWall unreachable behind onesmallfavour_relay.rs2:165's dead pilot_white_wolf_base trigger
        -- GUIDE-GAP: talkToCromperty unreachable behind onesmallfavour_relay.rs2:165's dead pilot_white_wolf_base trigger
        -- GUIDE-GAP: talkToTindel unreachable behind onesmallfavour_relay.rs2:165's dead pilot_white_wolf_base trigger
        -- GUIDE-GAP: talkToRantz unreachable behind onesmallfavour_relay.rs2:165's dead pilot_white_wolf_base trigger
        -- GUIDE-GAP: returnToRantz unreachable behind onesmallfavour_relay.rs2:165's dead pilot_white_wolf_base trigger
        -- GUIDE-GAP: returnToTindel unreachable behind onesmallfavour_relay.rs2:165's dead pilot_white_wolf_base trigger
        -- GUIDE-GAP: getPigeonCages unreachable behind onesmallfavour_relay.rs2:165's dead pilot_white_wolf_base trigger
        -- GUIDE-GAP: enterGoblinCaveAgain unreachable behind onesmallfavour_relay.rs2:165's dead pilot_white_wolf_base trigger
        -- GUIDE-GAP: standNextToSculpture unreachable behind onesmallfavour_relay.rs2:165's dead pilot_white_wolf_base trigger
        -- GUIDE-GAP: killSlagilith unreachable behind onesmallfavour_relay.rs2:165's dead pilot_white_wolf_base trigger
        -- GUIDE-GAP: readScrollAgain unreachable behind onesmallfavour_relay.rs2:165's dead pilot_white_wolf_base trigger
        -- GUIDE-GAP: talkToPetra unreachable behind onesmallfavour_relay.rs2:165's dead pilot_white_wolf_base trigger
        -- GUIDE-GAP: returnToPhantuwti unreachable behind onesmallfavour_relay.rs2:165's dead pilot_white_wolf_base trigger
        -- GUIDE-GAP: finishWithPhantuwti unreachable behind onesmallfavour_relay.rs2:165's dead pilot_white_wolf_base trigger
        -- GUIDE-GAP: returnToArhein unreachable behind onesmallfavour_relay.rs2:165's dead pilot_white_wolf_base trigger
        -- GUIDE-GAP: killGangMembers unreachable behind onesmallfavour_relay.rs2:165's dead pilot_white_wolf_base trigger
        -- GUIDE-GAP: spinPotLid unreachable behind onesmallfavour_relay.rs2:165's dead pilot_white_wolf_base trigger
        -- GUIDE-GAP: firePotLid unreachable behind onesmallfavour_relay.rs2:165's dead pilot_white_wolf_base trigger
        -- GUIDE-GAP: usePotLidOnPot unreachable behind onesmallfavour_relay.rs2:165's dead pilot_white_wolf_base trigger
        -- GUIDE-GAP: pickUpPot unreachable behind onesmallfavour_relay.rs2:165's dead pilot_white_wolf_base trigger
        t.blocked("content_bug: onesmallfavour_relay.rs2:165 [opnpc1,pilot_white_wolf_base] " ..
            "gates Captain Bleemadge's Guthix-rest-tea exchange on the CHILD symbol of a " ..
            "multinpc (configs/all.npc [pilot_white_wolf] multinpc5=pilot_white_wolf_base), but " ..
            "the live world spawn (areas/world/configs/m44_54.spawn: 'pilot_white_wolf 2847 3499 0') " ..
            "carries the BASE symbol -- QUEST_AUTHORING.md trap 19's exact dead-code shape (the op " ..
            "lookup is keyed on the spawned symbol only). The base symbol's own owner trigger, " ..
            "areas/area_gnome/scripts/gnome_glider.rs2:16 ([opnpc1,pilot_white_wolf] " ..
            "@gnome_pilot_talk;), has no onesmallfavour check anywhere in gnome_pilot_talk, so " ..
            "%onesmallfavour can never advance past osf_sanfew_told (75) through a real click. Same " ..
            "fix idiom as trap 19's reldo/osman/holgart precedent: gnome_pilot_talk must check " ..
            "%onesmallfavour first and hand off to onesmallfavour_relay.rs2's own osf_bleemadge_talk " ..
            "label before falling into its own dialogue. Everything the guide drives after Sanfew " ..
            "(Arhein's T.R.A.S.H., Phantuwti's weathervane and the goblin-cave sculpture/Slagilith " ..
            "fight, Hammerspike's dwarf-gang fight, Tassie's pot lid, and every return leg back down " ..
            "to Yanni) sits behind this same unreachable gate.")
        return
    end,
}
