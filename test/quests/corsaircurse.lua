-- The Corsair Curse -- driven step by step against OSRS-Content quests/quest_corsaircurse,
-- rebuilt from RSProx index 4352 (the whole quest recorded 2026-10-23;
-- docs/rsprox-analysis/quests/corsair_curse.md). The route is the recording's:
--
-- Captain Tock at the crossroads north of Port Sarim (3030,3273) -> his ship west of Rimmington
-- (2910,3226) -> the voyage -> the cove's gangplank -> Ithoi's hut (the ramp at 2531,2833) ->
-- Gnocci's hut (the stairs at 2548,2862; the tinderbox under it) -> Colin's and Arsen's hut (the
-- stairs at 2555,2855) -> the spade on the sand (2552,2846) -> Tock on deck: the relic -> the
-- vine hole (2523,2861) and Chief Tess -> the sand by the tree (2504,2839): the doll -> Ithoi's
-- telescope -> Gnocci, Arsen, Colin told -> Tock -> Gnocci -> Arsen -> Ithoi (twice) -> the
-- driftwood under his hut -> Tock -> the fight in his hut -> Tock: the scroll.
--
-- Scenes play in private copies of the cove square, so their cameras are checked by op and
-- height, not by tile. Pages that go up under a scene have nothing to click (as recorded), so a
-- scene is waited out, not clicked through.
--
-- Setup: the quest has no requirements; Ithoi (level 35, five strike spells) is fought with a
-- rune scimitar at 60 Attack/Strength, 40 Defence, 60 Hitpoints.
return {
    id = "corsaircurse",
    fixture = "fresh_lumbridge.ini",
    max_frames = 220000,
    setup = {
        "::clearinv",
        "::setlevel attack 60",
        "::setlevel strength 60",
        "::setlevel defence 40",
        "::setlevel hitpoints 60",
        "::give rune_scimitar 1",
    },

    run = function(t)
        -- the wire audit against RSProx 4352 reads this (build/quest_gate/<run>/ticklog.tsv)
        t.ticklog.start()
        local bind_result, bind_detail = t.quest.bind({
            varp = "varb6071_corscurs_progress",
            constants = {
                not_started = 0,
                started = 5,
                rimmington = 10,
                cove = 15,
                theories = 20,
                dinner = 25,
                ithoi_cooked = 30,
                firing = 35,
                confessed = 40,
                wont_get_up = 45,
                seen = 50,
                kill = 52,
                killed = 55,
                complete = 60,
            },
            row = "quest_corsaircurse",
            display = "The Corsair Curse",
            points = 2,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.ticks(2)
        t.exec("quest.stage.not_started", t.quest.expect_stage, "not_started")
        t.exec("wield-scimitar", t.player.equip, "rune_scimitar")

        -- A scene's pages: the ones with a continue are clicked; the ones it puts up under a
        -- moving cast have nothing to click (as recorded) and are waited out. Done when the
        -- chatbox has been empty for six ticks after the last click.
        local function play_scene(name, max_ticks)
            local clicks, idle, waited = 0, 0, 0
            while waited < max_ticks do
                if t.chat.kind() ~= "none" then
                    idle = 0
                    local r = t.chat.continue_()
                    if r == "ok" then
                        clicks = clicks + 1
                    end
                else
                    idle = idle + 1
                    if clicks > 0 and idle >= 6 then
                        break
                    end
                end
                t.ticks(1)
                waited = waited + 1
            end
            t.step(name, (clicks > 0 and t.chat.kind() == "none") and "PASS" or "FAIL",
                clicks .. " page(s) clicked over " .. waited .. " tick(s)")
        end
        local function page_up(note, ticks)
            t.await({ level = function() return t.chat.kind() ~= "none" end, note = note }, ticks or 12)
        end
        local function at_tile(name, x, z, level, slack)
            local _, tile = t.world.tile()
            slack = slack or 0
            t.check(name, tile ~= nil and math.abs(tile.x - x) <= slack and math.abs(tile.z - z) <= slack
                and tile.level == level,
                "at " .. tostring(tile and tile.x) .. "," .. tostring(tile and tile.z) .. "," .. tostring(tile and tile.level)
                    .. " (want " .. x .. "," .. z .. "," .. level .. ")")
        end

        -- ---- Captain Tock at the crossroads; his walk to Rimmington is a pan of the shore ----
        t.exec("goto-tock", t.player.goto_tile, 3030, 3271, 0)
        t.exec("talkToTock", t.player.talk_to, "corsair_captain_crossroads", 1)
        local pan_mark = t.cutscene.mark()
        t.exec("talkToTock-dialog", t.chat.play, {
            "npc:Ahoy, young",
            "npc:Might ye be able to help me",
            "options",
            "choose:What kind of help do you need?",
            "player:What kind of help do you need?",
            "npc:It be about my crew.",
            "npc:It be a curse!",
            "npc:So I need a young",
            "options",
            "choose:Yes.",
            "player:Sure, I'll try to help with your curse.",
            "npc:Arrr! Thanks,",
        })
        t.exec("quest.stage.started", t.quest.expect_stage, "started")
        t.ticks(24)
        t.exec("talkToTock-pan", t.cutscene.await, "talkToTock-pan", { since = pan_mark, quiet = 60, expect = {
            { op = "moveto", height = 800 },
            { op = "lookat", height = 100 },
            { op = "reset" },
        } })
        t.exec("talkToTock-after", t.chat.play, {
            "npc:Now, I've a ship moored west of Rimmington.",
            "player:West of Rimmington, you say?",
            "mesbox:Captain Tock swaggers off towards Rimmington.",
        })
        t.exec("quest.stage.rimmington", t.quest.expect_stage, "rimmington")
        at_tile("talkToTock.back", 3030, 3272, 0, 1)

        -- ---- Tock on board at Rimmington: the voyage ----
        t.exec("goto-rimmington", t.player.goto_tile, 2912, 3226, 0)
        t.exec("talkToTockShip", t.player.talk_to, "corsair_ferry_rimmington", 1)
        t.exec("talkToTockShip-dialog", t.chat.play, {
            "npc:Arrr, ye've found my ship",
            "npc:My Corsairs be a good crew",
            "npc:There be four of 'em",
            "npc:I've another crewman, Francois",
            "npc:Now be ye ready to voyage",
            "options",
            "choose:Okay, I'm ready go to Corsair Cove.",
            "player:Okay, I'm ready go to Corsair Cove.",
        })
        t.ticks(8)
        t.exec("quest.stage.cove", t.quest.expect_stage, "cove")
        at_tile("sail.deck", 2578, 2837, 1)

        -- ---- off the ship, up into Ithoi's hut ----
        t.exec("plankOff", t.player.click_loc, "ds2_corsair_cove_shipplank", 1)
        t.ticks(3)
        at_tile("plankOff.quay", 2578, 2839, 0)
        t.exec("goto-ithoi-ramp", t.player.goto_tile, 2532, 2833, 0)
        t.exec("ithoiRampUp", t.player.click_loc, "ds2_corsair_cove_stairs_ramp", 1, { at = { 2531, 2833 } })
        t.ticks(4)
        at_tile("ithoiRampUp.hut", 2529, 2835, 1)
        t.exec("talkToIthoi", t.player.talk_to, "corscurs_navigator_sick", 1)
        t.exec("talkToIthoi-dialog", t.chat.play, {
            "npc:Arrr, we be cursed",
            "options",
            "choose:I hear you've been cursed.",
            "player:I hear you've been cursed.",
            "npc:Aye? D'ye think so?",
            "npc:YES - WE'VE BEEN CURSED!",
            "npc:I've sailed the seas all my life.",
            "npc:I even sailed the seas",
            "player:Um...",
            "npc:The point I'm makin'",
            "npc:I've seen dancing fish-women",
            "npc:There be more things in the world",
            "player:Okay, it doesn't sound like",
            "npc:Arrr!",
        })
        t.exec("telescopeEarly", t.player.click_loc, "corscurs_telescope", 1)
        t.exec("telescopeEarly-text", t.chat.play, { "mesbox:You don't feel you need to see the sea" })
        t.exec("ithoiRampDown.walk", t.player.walk_to, 2529, 2835, 10)
        t.ticks(2)
        t.exec("ithoiRampDown", t.player.click_loc, "ds2_corsair_cove_stairs_ramp", 1, { at = { 2529, 2834 } })
        t.ticks(4)
        at_tile("ithoiRampDown.foot", 2532, 2833, 0)

        -- ---- Gnocci: the flashback ----
        t.exec("goto-gnocci-stairs", t.player.goto_tile, 2550, 2862, 0)
        t.exec("gnocciStairsUp", t.player.click_loc, "ds2_corsair_cove_stairs", 1, { at = { 2548, 2862, 0 } })
        t.ticks(4)
        at_tile("gnocciStairsUp.hut", 2547, 2862, 1)
        t.exec("talkToGnocci", t.player.talk_to, "corscurs_cook_sick", 1)
        local gnocci_mark = t.cutscene.mark()
        t.exec("talkToGnocci-dialog", t.chat.play, {
            "npc:My stomach!",
            "options",
            "choose:I hear you've been cursed.",
            "player:I hear you've been cursed.",
            "npc:Yes, I think She did this to us.",
            "player:I've met him.",
            "npc:It's all my fault",
            "player:Who are you talking about?",
            "npc:I'll explain...",
        })
        play_scene("gnocci-flashback", 150)
        t.exec("gnocci-flashback.cutscene", t.cutscene.await, "gnocci-flashback", { since = gnocci_mark, quiet = 200, expect = {
            { op = "moveto", height = 800 },
            { op = "lookat", height = 200 },
            { op = "reset" },
        } })
        at_tile("gnocci.back", 2546, 2862, 1)

        -- ---- the tinderbox under Gnocci's hut ----
        -- The take changes nothing the click's settle can see (no page, no line, the loc
        -- stays: RSProx 4352 [734]..[735] is the reach and the tinderbox in the pack), so the
        -- press is not a row of its own; the tinderbox arriving is the verdict.
        t.player.click_loc("raids_icedemon_tinderbox", 1)
        t.exec("takeTinderbox.await", t.inv.await, "tinderbox", 1, 8)
        t.exec("gnocciStairsDown", t.player.click_loc, "ds2_corsair_cove_stairs_down", 1, { at = { 2548, 2862, 1 } })
        t.ticks(4)

        -- ---- Colin: the flashback ----
        t.exec("goto-colin-stairs", t.player.goto_tile, 2555, 2857, 0)
        t.exec("colinStairsUp", t.player.click_loc, "ds2_corsair_cove_stairs", 1, { at = { 2555, 2855, 0 } })
        t.ticks(5)
        at_tile("colinStairsUp.hut", 2555, 2854, 1)
        t.exec("talkToColin", t.player.talk_to, "corscurs_cabinboy_sick", 1)
        local colin_mark = t.cutscene.mark()
        t.exec("talkToColin-dialog", t.chat.play, {
            "npc:I'm sorry! Tell her I'm sorry!",
            "options",
            "choose:I hear you've been cursed.",
            "player:I hear you've been cursed.",
            "npc:Yes, that's what it must be.",
            "player:I've met him.",
            "npc:This is all my fault.",
            "npc:Ithoi always told us",
        })
        play_scene("colin-flashback", 250)
        t.exec("colin-flashback.cutscene", t.cutscene.await, "colin-flashback", { since = colin_mark, quiet = 200, expect = {
            { op = "moveto", height = 700 },
            { op = "lookat", height = 350 },
            { op = "reset" },
        } })
        at_tile("colin.back", 2558, 2857, 1)

        -- ---- Arsen ----
        t.exec("talkToArsen", t.player.talk_to, "corscurs_thief_sick", 1)
        t.exec("talkToArsen-dialog", t.chat.play, {
            "npc:Urgh!",
            "options",
            "choose:I hear you've been cursed.",
            "player:I hear you've been cursed.",
            "npc:I think so.",
            "player:I've met him.",
            "npc:I'm afraid it may be all my fault.",
            "player:Oh?",
            "npc:Captain Tock had me exploring",
            "npc:Well, I climbed down the vines",
            "npc:I'd hoped I could use",
            "player:So you were trying to explore",
            "npc:Yes. I quickly grabbed some loot",
            "npc:But a couple of days later",
            "npc:Maybe you could get the relic back",
            "player:I see.",
        })
        t.exec("colinStairsDown", t.player.click_loc, "ds2_corsair_cove_stairs_down", 1, { at = { 2555, 2855, 1 } })
        t.ticks(5)

        -- ---- the spade on the sand ----
        t.exec("goto-spade", t.player.goto_tile, 2552, 2845, 0)
        t.exec("takeSpade", t.player.click_loc, "sand_withspade", 1)
        t.exec("takeSpade.await", t.inv.await, "spade", 1, 6)

        -- ---- Tock: the relic ----
        t.exec("goto-plank", t.player.goto_tile, 2578, 2840, 0)
        t.exec("plankOn", t.player.click_loc, "ds2_corsair_cove_shipplank", 1)
        t.ticks(3)
        at_tile("plankOn.deck", 2578, 2838, 1)
        t.exec("talkToTockRelic", t.player.talk_to, "corsair_ferry_covedocks", 1)
        t.exec("talkToTockRelic-dialog", t.chat.play, {
            "npc:Have ye any idea about how to lift the curse?",
            "options",
            "choose:Arsen says he gave you a sacred ogre relic.",
            "player:Arsen says he gave you",
            "npc:Arrr, that he did.",
            "player:He thinks the ogres have cursed you",
            "npc:Arrr, I've no use fer the silly thing.",
            "objbox:Captain Tock gives you the loot",
        })
        t.exec("talkToTockRelic.relic", t.inv.await, "corscurs_relic", 1, 4)
        t.exec("plankOff2", t.player.click_loc, "ds2_corsair_cove_shipplank", 1)
        t.ticks(3)

        -- ---- Chief Tess, down the vine hole ----
        t.exec("goto-vine-hole", t.player.goto_tile, 2523, 2860, 0)
        t.exec("enterVineHole", t.player.click_loc, "ds2_ogre_corsair_vine_ladder_entrance", 1)
        t.ticks(5)
        t.exec("talkToTess", t.player.talk_to, "ogress_tess", 1)
        t.exec("talkToTess-dialog", t.chat.play, {
            "npc:What you want, skinny creature?",
            "options",
            "choose:I've come to return what Arsen stole.",
            "player:I've come to return what Arsen stole.",
            "npc:He skinny man who climb down vine",
            "player:Arsen says he's very sorry",
            "objbox:You present Tess with Arsen's loot.",
            "npc:Yah, dat little man is big idiot.",
            "player:You're saying this is a tooth-pick?",
            "npc:Dis is tooth-pick.",
            "player:Oh. Well, I guess Arsen will be pleased",
        })
        t.exec("talkToTess.relicGone", t.inv.expect_absent, "corscurs_relic")
        t.exec("climbVineLadder", t.player.click_loc, "ds2_ogre_corsair_vine_ladder", 1)
        t.ticks(4)
        at_tile("climbVineLadder.top", 2523, 2860, 0, 1)

        -- ---- the doll, by the tree west of the cove ----
        t.exec("goto-sand", t.player.goto_tile, 2505, 2839, 0)
        t.exec("digDoll", t.player.inv_op, "spade", 1)
        t.exec("digDoll-dialog", t.chat.play, {
            "player:Okay, he said he buried the possessed doll",
            "options",
            "choose:Search for the possessed doll and face the consequences.",
        })
        t.ticks(12)
        t.exec("digDoll-after", t.chat.play, { "player:That's not possessed." })

        -- ---- Ithoi's telescope: the "mermaid" ----
        t.exec("goto-ithoi-ramp2", t.player.goto_tile, 2532, 2833, 0)
        t.exec("ithoiRampUp2", t.player.click_loc, "ds2_corsair_cove_stairs_ramp", 1, { at = { 2531, 2833 } })
        t.ticks(4)
        local scope_mark = t.cutscene.mark()
        t.exec("telescope", t.player.click_loc, "corscurs_telescope", 1)
        t.exec("telescope-dialog", t.chat.play, { "player:Let's see if I can find that mermaid" })
        t.ticks(24)
        t.exec("telescope-after", t.chat.play, { "player:That doesn't look anything like a mermaid." })
        t.exec("telescope.cutscene", t.cutscene.await, "telescope", { since = scope_mark, quiet = 60, expect = {
            { op = "moveto", height = 400 },
            { op = "lookat", height = 300 },
            { op = "reset" },
        } })
        t.exec("ithoiTheories", t.player.talk_to, "corscurs_navigator_sick", 1)
        t.exec("ithoiTheories-dialog", t.chat.play, {
            "npc:Ye be back? Tell me",
            "player:I've been hearing about what might have caused it",
            "player:Colin thought he upset a mermaid",
            "npc:That'll teach him to meddle",
            "player:Gnocci fished a doll",
            "npc:Arrr chuckie chuckie chuckie!",
            "player:Arsen stole what he thought",
            "npc:Ogre women be powerful",
            "player:I'll carry on trying",
            "npc:Arrr! Off ye goes...",
        })
        t.exec("ithoiRampDown2.walk", t.player.walk_to, 2529, 2835, 10)
        t.exec("ithoiRampDown2", t.player.click_loc, "ds2_corsair_cove_stairs_ramp", 1, { at = { 2529, 2834 } })
        t.ticks(4)

        -- ---- the three told ----
        t.exec("goto-gnocci-stairs2", t.player.goto_tile, 2550, 2862, 0)
        t.exec("gnocciStairsUp2", t.player.click_loc, "ds2_corsair_cove_stairs", 1, { at = { 2548, 2862, 0 } })
        t.ticks(4)
        t.exec("tellGnocci", t.player.talk_to, "corscurs_cook_sick", 1)
        t.exec("tellGnocci-dialog", t.chat.play, {
            "npc:Have you tried to destroy Her yet?",
            "player:Well, sort of.",
            "npc:Clockwork?",
            "npc:I hope you can find the real cause soon.",
            "player:Still trying.",
        })
        t.exec("gnocciStairsDown2", t.player.click_loc, "ds2_corsair_cove_stairs_down", 1, { at = { 2548, 2862, 1 } })
        t.ticks(4)
        t.exec("goto-colin-stairs2", t.player.goto_tile, 2555, 2857, 0)
        t.exec("colinStairsUp2", t.player.click_loc, "ds2_corsair_cove_stairs", 1, { at = { 2555, 2855, 0 } })
        t.ticks(5)
        t.exec("tellArsen", t.player.talk_to, "corscurs_thief_sick", 1)
        t.exec("tellArsen-dialog", t.chat.play, {
            "npc:Have you returned the sacred relic",
            "player:Their chief, Tess, says",
            "npc:Are you telling me",
            "player:She didn't care",
            "npc:Oh. That's a relief, I guess.",
            "player:I'm not done investigating yet.",
        })
        t.exec("tellColin", t.player.talk_to, "corscurs_cabinboy_sick", 1)
        t.exec("tellColin-dialog", t.chat.play, {
            "npc:Have you found the mermaid yet?",
            "player:I've looked through that telescope myself.",
            "npc:What?",
            "player:definitely not a mermaid.",
            "npc:Oh. Oh, I see.",
            "player:How are you feeling?",
            "npc:I still feel awful.",
            "player:I'll be on the case.",
        })
        t.exec("quest.stage.theories", t.quest.expect_stage, "theories")
        t.exec("colinStairsDown2", t.player.click_loc, "ds2_corsair_cove_stairs_down", 1, { at = { 2555, 2855, 1 } })
        t.ticks(5)

        -- ---- Tock: the dinner ----
        t.exec("goto-plank2", t.player.goto_tile, 2578, 2840, 0)
        t.exec("plankOn2", t.player.click_loc, "ds2_corsair_cove_shipplank", 1)
        t.ticks(3)
        t.exec("tockTheories", t.player.talk_to, "corsair_ferry_covedocks", 1)
        t.exec("tockTheories-dialog", t.chat.play, {
            "npc:Have ye any idea about how to lift the curse?",
            "options",
            "choose:I've ruled out all the Corsairs' theories...",
            "player:I've ruled out all the Corsairs' theories...",
            "player:Cabin-boy Colin thought",
            "player:Arsen the Thief thought",
            "player:Ithoi the Navigator said",
            "player:And Gnocci the Cook thought",
            "npc:any sufficiently advanced technology",
            "options",
            "choose:So what do I do now?",
            "player:So what do I do now?",
            "npc:Arrr, I don't rightly know.",
            "player:I see. You're saying it began",
            "npc:Arrr, that be right.",
            "player:That meal must have been interesting...",
        })
        t.exec("quest.stage.dinner", t.quest.expect_stage, "dinner")
        t.exec("plankOff3", t.player.click_loc, "ds2_corsair_cove_shipplank", 1)
        t.ticks(3)

        -- ---- Gnocci: Ithoi cooked ----
        t.exec("goto-gnocci-stairs3", t.player.goto_tile, 2550, 2862, 0)
        t.exec("gnocciStairsUp3", t.player.click_loc, "ds2_corsair_cove_stairs", 1, { at = { 2548, 2862, 0 } })
        t.ticks(4)
        t.exec("gnocciDinner", t.player.talk_to, "corscurs_cook_sick", 1)
        t.exec("gnocciDinner-dialog", t.chat.play, {
            "npc:Have you solved the curse yet?",
            "options",
            "choose:I hear it happened straight after dinner.",
            "player:I hear it happened straight after dinner.",
            "npc:Hey, don't go blaming the cook",
            "player:You mean the clockwork doll?",
            "npc:It's an easy mistake to make!",
            "npc:Anyway I didn't dare try to fish",
            "npc:Anyway, Ithoi got sick",
        })
        t.exec("quest.stage.ithoi_cooked", t.quest.expect_stage, "ithoi_cooked")
        t.exec("gnocciStairsDown3", t.player.click_loc, "ds2_corsair_cove_stairs_down", 1, { at = { 2548, 2862, 1 } })
        t.ticks(4)

        -- ---- Arsen: the firing ----
        t.exec("goto-colin-stairs3", t.player.goto_tile, 2555, 2857, 0)
        t.exec("colinStairsUp3", t.player.click_loc, "ds2_corsair_cove_stairs", 1, { at = { 2555, 2855, 0 } })
        t.ticks(5)
        t.exec("arsenFiring", t.player.talk_to, "corscurs_thief_sick", 1)
        t.exec("arsenFiring-dialog", t.chat.play, {
            "npc:Do you know how we all got cursed?",
            "options",
            "choose:I hear Ithoi cooked the meal you ate that night.",
            "player:I hear Ithoi cooked the meal",
            "npc:I suppose Ithoi's trying to be extra helpful",
            "npc:My brother, Francois",
            "options",
            "choose:What is the mission Francois is doing?",
            "player:What is the mission Francois is doing?",
            "npc:That's a secret.",
            "player:Okay, thanks.",
        })
        t.exec("quest.stage.firing", t.quest.expect_stage, "firing")
        t.exec("colinStairsDown3", t.player.click_loc, "ds2_corsair_cove_stairs_down", 1, { at = { 2555, 2855, 1 } })
        t.ticks(5)

        -- ---- Ithoi confesses ----
        t.exec("goto-ithoi-ramp3", t.player.goto_tile, 2532, 2833, 0)
        t.exec("ithoiRampUp3", t.player.click_loc, "ds2_corsair_cove_stairs_ramp", 1, { at = { 2531, 2833 } })
        t.ticks(4)
        t.exec("ithoiAccuse", t.player.talk_to, "corscurs_navigator_sick", 1)
        t.exec("ithoiAccuse-dialog", t.chat.play, {
            "npc:Ye be back? Have ye been investigatin'",
            "options",
            "choose:I hear you cooked the meal they ate before getting sick.",
            "player:I hear you cooked the meal",
            "npc:Arrr? And what if I did?",
            "npc:Besides, if ye be sayin'",
            "options",
            "choose:Maybe because the Captain's thinking of firing you.",
            "player:Maybe because the Captain's thinking of firing you",
            "npc:Arrr! Too clever ye be...",
            "npc:But they all think I'm sick",
            "player:We'll see what they have to say about that!",
        })
        t.exec("quest.stage.confessed", t.quest.expect_stage, "confessed")
        t.exec("ithoiFaked", t.player.talk_to, "corscurs_navigator_sick", 1)
        t.exec("ithoiFaked-dialog", t.chat.play, {
            "npc:What d'ye want now?",
            "options",
            "choose:I know you've faked the curse.",
            "player:I know you've faked the curse.",
            "npc:The Corsairs all think I'm sick too",
            "options",
            "choose:I bet I can prove you're well enough to get up.",
            "player:I bet I can prove",
            "npc:Ye'll never make me get up!",
        })
        t.exec("quest.stage.wont_get_up", t.quest.expect_stage, "wont_get_up")
        t.exec("ithoiRampDown3.walk", t.player.walk_to, 2529, 2835, 10)
        t.exec("ithoiRampDown3", t.player.click_loc, "ds2_corsair_cove_stairs_ramp", 1, { at = { 2529, 2834 } })
        t.ticks(4)

        -- ---- the driftwood under his hut ----
        local burn_mark = t.cutscene.mark()
        t.exec("lightDriftwood", t.player.use_on, "tinderbox", t.player.by_symbol("loc", "corscurs_driftwood_multi"))
        -- photographed through the scene, not only either side of it: the burning driftwood
        -- (stage 49) is on screen only inside it ([1521]..[1542])
        for i = 1, 8 do
            t.ticks(4)
            t.shot(string.format("lightDriftwood.burn-%d", i))
        end
        t.exec("lightDriftwood-after", t.chat.play, { "player:Well, I guess that proves he's well enough to get up!" })
        t.exec("lightDriftwood.cutscene", t.cutscene.await, "lightDriftwood", { since = burn_mark, quiet = 60, expect = {
            { op = "moveto", height = 200 },
            { op = "lookat", height = 170 },
            { op = "reset" },
        } })
        t.exec("quest.stage.seen", t.quest.expect_stage, "seen")

        -- ---- Tock: "Kill him fer me." ----
        t.exec("goto-plank3", t.player.goto_tile, 2578, 2840, 0)
        t.exec("plankOn3", t.player.click_loc, "ds2_corsair_cove_shipplank", 1)
        t.ticks(3)
        t.exec("tockKill", t.player.talk_to, "corsair_ferry_covedocks", 1)
        t.exec("tockKill-dialog", t.chat.play, {
            "npc:So what's happening now?",
            "options",
            "choose:I've seen Ithoi running around. He's not sick at all.",
            "player:I've seen Ithoi running around.",
            "npc:Arrr! So he was never sick at all!",
            "options",
            "choose:I don't believe in capital punishment.",
            "player:I don't believe in capital punishment.",
            "npc:I do.",
            "options",
            "choose:Why don't you kill him yourself?",
            "player:Why don't you kill him yourself?",
            "npc:A captain shouldn't leave his ship",
            "options",
            "choose:I'll be back.",
            "player:I'll be back.",
        })
        t.exec("quest.stage.kill", t.quest.expect_stage, "kill")
        t.exec("plankOff4", t.player.click_loc, "ds2_corsair_cove_shipplank", 1)
        t.ticks(3)

        -- ---- the fight, in a copy of his hut ----
        t.exec("goto-ithoi-ramp4", t.player.goto_tile, 2532, 2833, 0)
        t.exec("ithoiRampFight", t.player.click_loc, "ds2_corsair_cove_stairs_ramp", 1, { at = { 2531, 2833 } })
        t.ticks(4)
        t.exec("attackIthoi", t.player.attack, "corscurs_navigator_combat", 2, 10)
        t.exec("attackIthoi.dead", t.npc.await_dead, "corscurs_navigator_combat", 200)
        t.ticks(4)
        t.exec("quest.stage.killed", t.quest.expect_stage, "killed")
        at_tile("fight.back", 2529, 2838, 1)
        t.exec("ithoiRampDown4.walk", t.player.walk_to, 2529, 2835, 10)
        t.exec("ithoiRampDown4", t.player.click_loc, "ds2_corsair_cove_stairs_ramp", 1, { at = { 2529, 2834 } })
        t.ticks(4)

        -- ---- Tock: the reward ----
        t.exec("goto-plank4", t.player.goto_tile, 2578, 2840, 0)
        t.exec("plankOn4", t.player.click_loc, "ds2_corsair_cove_shipplank", 1)
        t.ticks(3)
        t.exec("tockDone", t.player.talk_to, "corsair_ferry_covedocks", 1)
        t.exec("tockDone-dialog", t.chat.play, {
            "npc:So what's happening now?",
            "options",
            "choose:I've killed Ithoi for poisoning your crew.",
            "player:I've killed Ithoi for poisoning your crew.",
            "npc:Thank ye fer dealing with that serpent.",
            "npc:Most of them deleterious effects",
            "npc:Anyways, ye've been a mighty help",
        })
        t.ticks(3)
        t.quest.expect_complete()
    end,
}
