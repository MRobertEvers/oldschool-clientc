-- Desert Treasure I: the pillars, the pyramid, Azzanadra, the completion and Eblis afterwards, in
-- the order RSProx index 4629 recorded it ([4356]..[5922]). dt_pyramid.rs2 and
-- dt_start_eblis_complete.rs2 have the recorded ticks;
-- docs/rsprox-analysis/quests/desert_treasure_i_pyramid.md the tables.
--
--     python3 tools/quest_gate/run.py --script test/quests/_dt_pyramid.lua --name dt_pyramid --fixture fresh_lumbridge.ini
return {
    id = "dt_pyramid",
    fixture = "fresh_lumbridge.ini",
    max_frames = 120000,
    setup = {
        -- The cheat puts every prerequisite and sub-stage in place; the rows after it take the
        -- diamonds back out of the pillars and stand the player by the north-west one at stage 12.
        "::deserttreasure",
        "::setvar varb358_deserttreasure 12",
        "::setvar varb390_fd_column_blood 0",
        "::setvar varb387_fd_column_fire 0",
        "::setvar varb389_fd_column_ice 0",
        "::setvar varb388_fd_column_shadow 0",
        "::setlevel hitpoints 99",
        "::setlevel defence 70",
        "::setlevel prayer 70",
        "::setlevel magic 50",
        "::clearinv",
        "::give fd_blood_diamond",
        "::give fd_diamond_fire",
        "::give fd_icediamond",
        "::give fd_dark_diamond",
        "::give coins 80000",
        "::give shark 12",
        "::give water_skin4 2",
        "::tele 0_50_45_21_27",
    },

    run = function(t)
        t.ticklog.start()
        local br, bd = t.quest.bind({
            varp = "varb358_deserttreasure",
            constants = { not_started = 0, started = 1, four_diamonds = 12, columns = 13, azzanadra = 14, complete = 15 },
            row = "quest_deserttreasure",
            display = "Desert Treasure I",
            points = 3,
        })
        t.step("quest.bind", br == "ok" and "PASS" or "FAIL", bd)
        local function reading()
            local r, tt = t.world.tile()
            if r == "ok" and type(tt) == "table" then return tt.x .. "," .. tt.z .. "," .. tostring(tt.level) end
            return tostring(r)
        end
        local function count(sym) local r, n = t.inv.count(sym) return r == "ok" and n or 0 end
        local function var(name) return select(2, t.var.server(name)) end

        -- The pillars: the wrong diamond first ([4357]), then each in its own ([4374], [4407], [4426], [4444]).
        local function place(step, stand_x, stand_z, sym, varname, diamond)
            t.exec("goto-" .. step, t.player.goto_tile, stand_x, stand_z, 0)
            local pillar = t.player.by_symbol("loc", sym)
            local r, d = t.player.use_on(diamond, pillar)
            t.ticks(3)
            t.check(step, var(varname) == 1 and count(diamond) == 0, "use_on " .. diamond .. " -> " .. tostring(r) .. " " .. tostring(d) .. "; "
                .. varname .. " = " .. tostring(var(varname)) .. "; messages: " .. tostring(t.msg.last(2)))
        end
        -- The floors' mummies and scarabs are held passive (test affordance, docs/quest_authoring/gaps-combat.md):
        -- this test reads the section's frames, and a walk of 900 ticks through them unfed is a death.
        local passive_ok = true
        for _, sym in ipairs({ "deserttreasure_mummy_1", "deserttreasure_mummy_2", "deserttreasure_mummy_3", "deserttreasure_mummy_4", "deserttreasure_mummy_5", "scarab_swarm" }) do
            if t.cheat("::passive " .. sym) ~= "ok" then passive_ok = false end
        end
        t.check("passive", passive_ok, "::passive on the pyramid's mummies and scarabs")
        t.player.use_on("fd_dark_diamond", t.player.by_symbol("loc", "desert_treasure_oblix_a"))
        t.ticks(4)
        t.exec("wrongDiamond", t.msg.expect, "That doesn't appear to be the correct diamond...")
        t.check("wrongDiamond-kept", count("fd_dark_diamond") == 1 and var("varb390_fd_column_blood") == 0, "shadow diamond held " .. count("fd_dark_diamond"))
        place("placeBlood", 3221, 2907, "desert_treasure_oblix_a", "varb390_fd_column_blood", "fd_blood_diamond")
        t.exec("placeBlood-line", t.msg.expect, "The diamond is absorbed into the pillar...")
        place("placeSmoke", 3245, 2907, "desert_treasure_oblix_b", "varb387_fd_column_fire", "fd_diamond_fire")
        place("placeIce", 3245, 2884, "desert_treasure_oblix_c", "varb389_fd_column_ice", "fd_icediamond")
        t.check("threeIn-stage", var("varb358_deserttreasure") == 12, "deserttreasure = " .. tostring(var("varb358_deserttreasure")) .. " with three diamonds in (12)")
        place("placeShadow", 3223, 2885, "desert_treasure_oblix_d", "varb388_fd_column_shadow", "fd_dark_diamond")
        t.exec("placeShadow-line", t.msg.expect, "The force preventing access to the Pyramid has now vanished.")
        t.check("placeShadow-stage", var("varb358_deserttreasure") == 13, "deserttreasure = " .. tostring(var("varb358_deserttreasure")) .. " once the fourth diamond is absorbed (13)")

        -- The door and the ladder ([4476]..[4490]).
        t.exec("goto-enterPyramid", t.player.goto_tile, 3233, 2909, 0)
        t.exec("walk-enterPyramid.northStairs", t.player.walk_route, { { 3233, 2905 }, { 3233, 2900 } })
        t.exec("enterPyramid.entrance", t.player.cross_gate, { loc = "four_diamonds_door_1", at = { 3233, 2899, 0 }, near = { 3233, 2900 },
            far_ok = function(tile) return tile.z == 2898 end, far_desc = "through the entrance to 3233,2898 ([4481])" })
        t.exec("enterPyramid", t.player.climb, { loc = "desert_laddertop", at = { 3233, 2897, 0 }, src = { 3233, 2898 }, dest = { 2913, 4954, 3 }, slack = 0 })
        t.check("enterPyramid-clock", var("varb394_fd_pyramid_timer") ~= nil, "fd_pyramid_timer = " .. tostring(var("varb394_fd_pyramid_timer")))

        -- A search on the top floor ([4504]..[4511]): two lines four ticks apart. A mummy or the pit may follow.
        -- The recorder searched from 2901,4947, the tile the lid opens onto ([4506]).
        t.exec("goto-search", t.player.goto_tile, 2901, 4947, 3)
        t.player.click_loc("deserttreasure_sarcophigi_door_4", 1)
        t.ticks(8)
        t.exec("search-first", t.msg.expect, "You search the sarcophagus...")
        t.exec("search-second", t.msg.expect, "You don't find anything interesting.")

        -- The floors. A pit throws the player out to 3233,2887: then the way in is walked again.
        local function inside()
            local r, tt = t.world.tile()
            return r == "ok" and not (tt.level == 0 and tt.z < 3000)
        end
        local function reenter()
            t.ticks(14)
            t.exec("reenter.goto", t.player.goto_tile, 3233, 2900, 0)
            t.exec("reenter.entrance", t.player.cross_gate, { loc = "four_diamonds_door_1", at = { 3233, 2899, 0 }, near = { 3233, 2900 },
                far_ok = function(tile) return tile.z == 2898 end, far_desc = "through the entrance" })
            t.exec("reenter.ladder", t.player.climb, { loc = "desert_laddertop", at = { 3233, 2897, 0 }, src = { 3233, 2898 }, dest = { 2913, 4954, 3 }, slack = 0 })
        end
        local ladders = {
            { "goDownFromFirstFloor", "desert_laddertop3_2", { 2909, 4964, 3 }, { 2846, 4964, 2 } },
            { "goDownFromSecondFloor", "desert_laddertop2_1", { 2846, 4973, 2 }, { 2782, 4972, 1 } },
            -- The third floor's ladder is pressed from 2784,4954: from the landing the press finds no route and
            -- sends nothing, and a character left standing where its walk ends is taken by a pit.
            { "goDownFromThirdFloor", "desert_laddertop1_0", { 2784, 4941, 1 }, { 3233, 9293, 0 }, { 2784, 4954 } },
        }
        local falls = 0
        local i = 1
        local guard = 0
        while i <= #ladders and guard < 30 do
            guard = guard + 1
            if not inside() then
                falls = falls + 1
                reenter()
                i = 1
            end
            local row = ladders[i]
            if row[5] then
                t.player.walk_to(row[5][1], row[5][2], 120)
            end
            local r, d = t.player.climb({ loc = row[2], at = row[3], dest = row[4], slack = 0, ticks = 200 })
            local lr, lt = t.world.tile()
            if r == "ok" and lr == "ok" and lt.x == row[4][1] and lt.z == row[4][2] and lt.level == row[4][3] then
                t.check(row[1], true, "landed " .. reading() .. " (recorded landing)")
                i = i + 1
            elseif inside() then
                t.ticks(2)
            end
        end
        t.check("floors", i > #ladders, "ladders done " .. (i - 1) .. " of 3, pit falls " .. falls .. ", now " .. reading())

        -- The temple ([5266]..[5270]) and what is in it before the quest is done.
        t.exec("goto-temple", t.player.goto_tile, 3234, 9325, 0)
        t.exec("temple.door", t.player.cross_gate, { loc = "dt_ancient_temple_door_open", at = { 3234, 9324, 0 }, near = { 3234, 9325 },
            far_ok = function(tile) return tile.x == 3234 and tile.z == 9323 end, far_desc = "3234,9323 ([5269])" })
        t.player.click_loc("deserttreasure_sarcophigi_door_4", 1)
        t.ticks(3)
        t.exec("temple.sarcophagus", t.msg.expect, "It seems like someone has already looted this Sarcophagus...")
        t.player.click_loc("dt_zaros_altar", 1)
        t.ticks(3)
        t.exec("temple.altar-before", t.msg.expect, "This altar seems to do nothing...")
        t.exec("temple.lectern", t.player.click_loc, "dt_ancient_lectern", 1)
        t.exec("temple.lectern-dialog", t.chat.play, { "mesbox:You must be using the Ancient spellbook to use this lectern.", "choose:No" })

        -- Azzanadra ([5362]..[5464]).
        local snapshot_result, snapshot = t.skill.snapshot()
        t.exec("talkToAzz", t.player.talk_to, "azzanadra_real", 1)
        t.exec("talkToAzz-dialog", t.chat.play, {
            "npc:I knew they could not trap me here for long! Well done, friend. Tell me, how goes the war?",
            "player:War?",
            "npc:You do not know of the war for our survival?",
            "npc:Tell me, what news of great Paddewwa?",
            "player:Er... sorry, I've never heard of them...",
            "npc:No! My lord... what has become of you?",
            "npc:My thanks to you friend for your help in freeing me",
        })
        t.check("talkToAzz-stage14", var("varb358_deserttreasure") == 14, "deserttreasure = " .. tostring(var("varb358_deserttreasure")) .. " with the eighth page up (14, [5419])")
        t.exec("talkToAzz-dialog2", t.chat.play, {
            "npc:How long have I been trapped here?",
            "npc:My friend, for your efforts in freeing me, I offer you the gift of knowledge.",
            "npc:They will replace the knowledge you previously had",
            "npc:I trust that we shall meet again, but now I must leave you.",
        })
        t.ticks(3)
        t.quest.expect_complete()
        t.check("complete.stage", var("varb358_deserttreasure") == 15, "deserttreasure = " .. tostring(var("varb358_deserttreasure")))
        t.check("complete.spellbook", var("varb4070_spellbook") == 1, "spellbook = " .. tostring(var("varb4070_spellbook")) .. " after the scroll's close (1 = Ancient, [5474])")
        t.exec("complete.wisdom", t.msg.expect, "A strange wisdom has filled your mind...")
        local _, magic_after = t.skill.read("magic")
        local magic_before = snapshot and snapshot.magic and snapshot.magic.experience
        local magic_gain = (type(magic_after) == "table" and magic_after.experience or 0) - (magic_before or 0)
        t.check("reward.magic_xp", magic_before ~= nil and (magic_gain == 20007 or magic_gain == 20006),
            "magic: before=" .. tostring(magic_before) .. " delta=" .. magic_gain .. " (the recorded stat packet's gain reads 20,007)")

        -- The lectern on the Ancient spellbook ([5496]), the portal out ([5497]..[5501]) and the tunnel back ([5664]..[5670]).
        t.exec("after.lectern", t.player.click_loc, "dt_ancient_lectern", 1)
        t.exec("after.lectern-dialog", t.chat.play, { "objbox:You need some soft clay to use this lectern." })
        t.player.click_loc("dia_down", 1)
        t.await({ level = function() local r, tt = t.world.tile() return r == "ok" and tt.z < 3000 end, note = "portal" }, 12)
        t.check("after.portal", reading() == "3233,2887,0", "out of the portal at " .. reading() .. " (3233,2887,0 [5501])")
        t.player.click_loc("desert_trasure_back_door_skew", 1)
        t.await({ level = function() local r, tt = t.world.tile() return r == "ok" and tt.z > 9000 end, note = "tunnel" }, 12)
        t.check("after.tunnel", reading() == "3233,9312,0", "through the tunnel to " .. reading() .. " (3233,9312,0 [5670])")
        t.player.click_loc("dia_down", 1)
        t.await({ level = function() local r, tt = t.world.tile() return r == "ok" and tt.z < 3000 end, note = "portal2" }, 12)

        -- Eblis by the mirrors ([5549]..[5616], [5912]..[5922]): the signet, "No thanks.", then the staff.
        local eblis_pages = {
            "npc:So have you spoken to my Lord Azzanadra yet?",
            "player:Yes I have.",
            "npc:And what words did he have for his followers?",
            "player:Er.... He didn't really mention you at all",
            "npc:It is understandable perhaps...",
            "npc:Anyway, if he has taught you our ancient magics",
            "npc:magic of our god.",
            "npc:Normally I could not bear to part with such a priceless relic",
            "npc:Are you interested?",
        }
        local function eblis(step, tail)
            local rows = {}
            for _, l in ipairs(eblis_pages) do rows[#rows + 1] = l end
            for _, l in ipairs(tail) do rows[#rows + 1] = l end
            t.exec(step, t.player.talk_to, "fd_elder_by_mirrors", 1)
            t.exec(step .. "-dialog", t.chat.play, rows)
        end
        t.exec("goto-eblis", t.player.goto_tile, 3211, 2955, 0)
        eblis("eblis.signet", { "choose:Have you got any symbols of your god?", "player:Have you got any symbols of your god?",
            "objbox:Eblis gives you a little signet." })
        t.check("eblis.signet-held", count("poh_ancient_signet") == 1, "signets held " .. count("poh_ancient_signet"))
        eblis("eblis.no", { "choose:No thanks.", "player:No thanks.", "npc:As you wish.", "choose:I'll leave you to it.", "player:I'll leave you to it." })
        local coins0 = count("coins")
        eblis("eblis.staff", { "choose:Yes please.", "player:Yes please.", "npc:Take care of it, it is the only heirloom from those times I possess" })
        t.check("eblis.staff-held", count("staff_of_zaros") == 1 and count("coins") == coins0 - 80000 and var("varb391_fd_got_zaros_staff") == 1,
            "staff held " .. count("staff_of_zaros") .. ", coins " .. coins0 .. " -> " .. count("coins") .. ", fd_got_zaros_staff = " .. tostring(var("varb391_fd_got_zaros_staff")))
        t.finish(0)
    end,
}
