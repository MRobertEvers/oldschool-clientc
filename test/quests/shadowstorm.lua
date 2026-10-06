-- Shadow of the Storm (shadowstorm). Tier 2, 340c190aae parity2c "partial".
-- Prereqs (The Golem, Demon Slayer) via ::complete -- rule (e), never ::setvar
-- the quest's own varp. Setup gives only what the wiki's Requirements list
-- brings along (a silver bar, three pieces of black clothing, combat stats
-- and food for a level 100 demon). Every Quest Helper step is driven for
-- real, from Father Reen to the completion scroll: Evil Dave's clothing
-- check and escort, the first ritual in Denath's order, the exit portal to
-- the ruin (seam26), Tanya's and Eric's sigils, Badden, Reen, the golem's
-- strange implement, the second ritual in the tome's order at QH's
-- secondCircleSpot (seam26), and Agrith-Naar fought with the dyed
-- Silverlight worn.
--
-- Door rule (b66 re-drive): every crossing is clicked, nothing is goto'd
-- past. Each leg checked with the sample tools' reach.py (doors closed,
-- --root the worktree):
--   * The run starts at the fixture's tile in Lumbridge (3206,3233); Father
--     Reen (3271,3159) is REACH closed-doors len=401 from it (the walk round
--     by the north, so the Al Kharid toll gate is not an only-way gate).
--   * The desert is behind the Shantay Pass doorway shantay_pass_henge_doorway
--     3302,3116 (its only way in on foot). South: a pass bought from Shantay
--     (5 gp, shantay.rs2) and the doorway's pages; north: the doorway pushes
--     the player 3 tiles (shantay_pass.rs2). Both by t.player.cross_gate, on
--     every crossing. Uzer, the mushrooms and the kilns are all REACH
--     closed-doors from 3304,3113 on the desert side.
--   * The Uzer ruin under the city: golem_insidestairs_top pressed from the
--     arch tile 3491,3090 (the only open tile beside it; the press aims on
--     the drawn model since seam b66), maplink_0_54_48_35_18_down -> 2721,4886,0;
--     left by golem_insidestairs_base (maplink_0_42_76_33_22_up -> 3491,3090,0).
--     Same level, another map frame: t.player.climb with same_level.
--   * The throne room is plane 2 of the ruin, entered by golem_portal's SotS
--     arm (golem_portal.rs2 [oploc1,golem_demon_door_always_open] ->
--     ^sots_throne 2720,4912,2) or Evil Dave's escort, and left by
--     golem_demon_portal (2719,4883,2 -> ^sots_ruin_dave 2721,4911,0); the
--     landing walks to the exit portal (reach.py level 2: REACH len=29).
--   * "Travel to any furnace": the only furnace within 200 tiles of the
--     desert is Al Kharid's fai_falador_furnace 3272,3185 (its room is open
--     to the street: 3280,3186 -> 3275,3186 REACH len=7). So the sigil trip
--     leaves the ruin by the portal and the stairs, goes north through the
--     Shantay doorway, smelts, buys a second pass and comes back.
-- No dialogue on this route branches on the combat level (no
-- ~player_combat_level in quest_shadowstorm, quest_golem or the shantay
-- scripts), so the staged combat stats change no page.
return {
    id = "shadowstorm",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots
        "::complete quest_golem",
        "::complete quest_demonslayer",
        -- seam24 copy: the wiki's own requirements (Crafting 30, a silver bar).
        "::setlevel crafting 30",
        "::give silver_bar",
        -- seam26: the wiki's other requirements. "Any black outfit (minimum of
        -- 3 pieces)" -- the priest gown/robe from Thessalia and a black cape
        -- (wiki oldid 15354765 "Starting Out"; Quest Helper darkItems) -- and
        -- "the ability to defeat a level 100 demon" (combat stats + food).
        "::give priest_gown",
        "::give priest_robe",
        "::give black_cape",
        "::setlevel attack 80",
        "::setlevel strength 80",
        "::setlevel defence 80",
        "::setlevel hitpoints 80",
        "::give shark 10",
        "::give coins 10", -- two Shantay passes, 5 gp each (shantay.rs2)
        "::give water_skin4 2", -- desert heat (desert_heat.rs2: a drink per 150 ticks in desert_zones)
    },

    run = function(t)
        local function count(sym)
            local r, n = t.inv.count(sym)
            if r ~= "ok" or type(n) ~= "number" then
                return -1
            end
            return n
        end
        local function reading()
            local r, tile = t.world.tile()
            if r ~= "ok" or type(tile) ~= "table" then
                return "tile " .. tostring(r)
            end
            return string.format("%d,%d,%d", tile.x, tile.z, tile.level)
        end

        -- Buy a Shantay pass and go south through the doorway (shantay.rs2: 5 gp;
        -- shantay_pass.rs2 [oploc1,shantay_pass_henge_doorway]). The first trip
        -- reads the poster and gets the disclaimer; with the disclaimer held the
        -- second trip goes straight to the pass check.
        local function into_desert(prefix, first)
            t.exec("goto-" .. prefix .. ".shantay", t.player.goto_tile, 3304, 3123, 0)
            local coins0, pass0 = count("coins"), count("shantay_pass")
            t.exec(prefix .. ".buyPass", t.player.talk_to, "shantay", 1)
            local lines = first and { "npc:Hello effendi, I am Shantay.", "npc:I see you're new." }
                or { "npc:Hello again friend." }
            lines[#lines + 1] = "choose:I want to buy a shantay pass for 5 gold coins."
            lines[#lines + 1] = "player:I want to buy a shantay pass for"
            lines[#lines + 1] = "mesbox:You purchase a Shantay Pass."
            t.exec(prefix .. ".buyPass-dialog", t.chat.play, lines)
            local pass_await = t.inv.await("shantay_pass", 1, 5)
            t.check(prefix .. ".buyPass-paid", pass_await == "ok" and count("shantay_pass") == pass0 + 1 and count("coins") == coins0 - 5,
                "shantay_pass " .. pass0 .. " -> " .. count("shantay_pass") .. ", coins " .. coins0 .. " -> " .. count("coins") .. " (want -5, shantay.rs2)")
            local door_chat = {}
            if first then
                door_chat = { "mesbox:There is a large poster on the wall", "mesbox:The Desert is a VERY Dangerous place",
                    "mesbox:That seems pretty scary!", "choose:Yeah, that poster doesn't scare me!" }
            end
            door_chat[#door_chat + 1] = "npc:Can I see your Shantay Desert Pass"
            door_chat[#door_chat + 1] = "mesbox:You hand over a Shantay Pass."
            door_chat[#door_chat + 1] = "player:Sure, here you go!"
            if first then
                door_chat[#door_chat + 1] = "npc:Here, have a disclaimer"
            end
            t.exec(prefix .. ".shantayDoorway", t.player.cross_gate, { loc = "shantay_pass_henge_doorway", at = { 3302, 3116, 0 },
                near = { 3304, 3118 }, far_ok = function(tile) return tile.z <= 3115 end,
                far_desc = "south of the Shantay Pass doorway, z <= 3115", chat = door_chat })
            t.check(prefix .. ".shantayDoorway.passHandedOver", count("shantay_pass") == pass0,
                "shantay_pass " .. count("shantay_pass") .. " after the doorway (handed over), disclaimer " .. count("thshantaydisc"))
        end
        -- North through the doorway: free from the south (shantay_pass.rs2: a
        -- player at or south of the loc is pushed 3 tiles north).
        local function out_of_desert(prefix)
            t.exec("goto-" .. prefix .. ".shantayNorth", t.player.goto_tile, 3304, 3113, 0)
            t.exec(prefix .. ".shantayDoorwayNorth", t.player.cross_gate, { loc = "shantay_pass_henge_doorway", at = { 3302, 3116, 0 },
                near = { 3304, 3114 }, far_ok = function(tile) return tile.z > 3116 end, far_desc = "north of the Shantay doorway, z > 3116" })
        end
        -- Down into the Uzer ruin from the arch tile 3491,3090 (the only open
        -- tile beside the stairs; reach.py: the stair house's other tiles are
        -- solid). The press follows maplink_0_54_48_35_18_down to the stairs'
        -- base 2721,4886,0: same level, another map frame.
        local function ruin_down(name)
            -- The arch is on the stair house's west side: from the east the
            -- walk goes round it (run 1: 20 ticks from kiln 4 stalled).
            t.exec("walk-" .. name, t.player.walk_to, 3491, 3090, 70)
            t.exec(name, t.player.climb, { loc = "golem_insidestairs_top", op = 1, op_name = "Climb-down",
                at = { 3492, 3090, 0 }, src = { 3491, 3090 }, dest = { 2721, 4886, 0 }, slack = 0,
                same_level = "maplink_0_54_48_35_18_down" })
        end
        -- Up out of the ruin by golem_insidestairs_base (maplink_0_42_76_33_22_up
        -- -> the arch 3491,3090,0).
        local function ruin_up(name)
            t.exec("walk-" .. name, t.player.walk_to, 2721, 4886, 50)
            t.exec(name, t.player.climb, { loc = "golem_insidestairs_base", op = 1, op_name = "Climb-up",
                at = { 2721, 4884, 0 }, dest = { 3491, 3090, 0 }, slack = 0,
                same_level = "maplink_0_42_76_33_22_up" })
        end
        local function in_throne_room(tile)
            return type(tile) == "table" and tile.level == 2 and tile.x >= 2709 and tile.x <= 2731
                and tile.z >= 4879 and tile.z <= 4919
        end
        -- Into the throne room by the Uzer portal: golem_portal's child
        -- golem_demon_door_always_open (golem_a = 10), SotS arm -> ^sots_throne
        -- 2720,4912,2 (golem_portal.rs2; the source's own landing is
        -- 2720,4884,2 -- a parity note, graded as the content gives it).
        local function enter_portal(name)
            t.exec("walk-" .. name, t.player.walk_to, 2720, 4911, 50)
            local before = reading()
            t.exec(name, t.player.click_loc, "golem_portal", 1)
            t.await({
                level = function()
                    local r, tile = t.world.tile()
                    return r == "ok" and in_throne_room(tile)
                end,
                note = name .. ": the portal's landing in the throne room (plane 2)",
            }, 10)
            local _, tile = t.world.tile()
            t.check(name .. ".landed", in_throne_room(tile), "in the throne room at " .. reading() .. " (from " .. before .. ")")
        end
        -- Out of the throne room by golem_demon_portal (2719,4883,2) to the
        -- ruin in front of the Uzer portal (^sots_ruin_dave 2721,4911,0).
        local function leave_throne(name)
            t.exec("walk-" .. name, t.player.walk_to, 2720, 4886, 40)
            t.exec(name, t.player.climb, { loc = "golem_demon_portal", op = 1, op_name = "Enter",
                at = { 2719, 4883, 2 }, dest = { 2721, 4911, 0 }, slack = 0 })
        end

        local bind_r, bind_d = t.quest.bind({
            varp = "varb1372_agrith_quest",
            constants = {
                not_started = 0,
                see_badden = 10,
                infiltrate = 20,
                denath = 30,
                sigil_tasks = 40,
                matthew = 50,
                golem_ask = 60,
                ritual = 70,
                ritual_done = 90,
                recruit = 100,
                summon = 110,
                fight = 120,
                unequip = 124,
                complete = 125,
            },
            row = "quest_shadowofthestorm",
            display = "Shadow of the Storm",
            points = 1,
        })
        t.step("quest.bind", bind_r == "ok" and "PASS" or "FAIL", bind_d)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- ---- Father Reen, south of the Al Kharid bank ----
        -- shadowstorm.rs2 [opnpc1,agrith_reen_alkharid] @sots_reen_talk;
        -- live spawn is the _alkharid variant (areas/world/configs/m51_49.spawn).
        t.exec("goto-reen", t.player.goto_tile, 3271, 3159, 0)
        t.exec("talkToReen", t.player.talk_to, "agrith_reen_alkharid")
        t.exec("talkToReen-dialog", t.chat.play, {
            "npc:urgent job for you",
            "npc:Thank Saradomin",
            "npc:recognise this sword",
            "player:Silverlight! That's the sword",
            "mesbox:Father Reen gives you Silverlight",
            "npc:only with Silverlight",
            "npc:dark wizard Denath",
            "npc:moved to Uzer",
            "player:you want me to stop him",
            "npc:kill him once and for all",
            "npc:not an ordinary demon",
            "npc:rid the world of his evil influence",
            "npc:find Badden at once",
            "end",
        })
        t.expect("quest.stage.see_badden", t.quest.expect_stage("see_badden"))
        t.expect("has.silverlight", t.inv.await("silverlight", 1, 5))

        -- ---- Father Badden, Uzer ruins surface ----
        -- shadowstorm.rs2 [opnpc1,agrith_badden_uzer] @sots_badden_talk;
        -- into the desert through the Shantay doorway, then the desert walk
        -- (3304,3113 -> 3486,3090 REACH closed-doors len=227) is travel.
        into_desert("talkToBadden", true)
        t.exec("goto-badden", t.player.goto_tile, 3486, 3090, 0)
        t.exec("talkToBadden", t.player.talk_to, "agrith_badden_uzer")
        t.exec("talkToBadden-dialog", t.chat.play, {
            "npc:godsforsaken desert",
            "choose:Reen sent me.",
            "player:Reen sent me.",
            "npc:you have Silverlight",
            "choose:So what do you want me to do?",
            "player:So what do you want me to do?",
            "npc:infiltrate the group",
            "player:How can I do that",
            "npc:convince them you're one of them",
            "end",
        })
        t.expect("quest.stage.infiltrate", t.quest.expect_stage("infiltrate"))

        -- ---- Pick black mushrooms, dye Silverlight ----
        -- shadowstorm_dye.rs2 [oploc1,golem_black_mushrooms]; [opheldu,silverlight]
        -- -> ~sots_dye_silverlight.
        -- The mushrooms are a loc on 3495,3088 (solid); pressed from the open
        -- tile west of it.
        t.exec("walk-mushroom", t.player.walk_to, 3494, 3088)
        t.exec("pickMushroom", t.player.click_loc, "golem_black_mushrooms", 1)
        t.expect("has.mushroom", t.inv.await("golem_mushroom", 1, 5))
        t.exec("dyeSilverlight", t.player.use_item_on_item, "silverlight", "golem_mushroom")
        t.expect("has.dyed_silverlight", t.inv.await("agrith_silverlight_dyed", 1, 5))
        t.check("dyeSilverlight.consumed", count("silverlight") == 0 and count("golem_mushroom") == 0,
            "silverlight " .. count("silverlight") .. ", golem_mushroom " .. count("golem_mushroom")
                .. " after the dye (~sots_dye_silverlight: both deleted), agrith_silverlight_dyed " .. count("agrith_silverlight_dyed"))

        -- ---- Enter the Uzer ruins, pick up the strange implement ----
        -- shadowstorm_dye.rs2 [oploc1,golem_insidestairs_top] -> ^sots_ruin_dave.
        -- The "strange implement" is a real ground obj, golem_golemkey
        -- (all.obj: name=Strange implement), spawned at 2713,4913,0
        -- (areas/world/configs/m42_76.spawn) -- not a gap, QH's own WorldPoint
        -- for pickUpStrangeImplement.
        ruin_down("goIntoRuin")
        -- Inside the ruin: 2721,4886 -> 2713,4912 REACH closed-doors len=42,
        -- the open tile south of the implement (standing on the obj's own
        -- tile hides it under the player: run 1 'menu has no row for it').
        t.exec("walk-implement", t.player.walk_to, 2713, 4912, 70)
        -- click_obj is hollow on success (trap 12/8): call it directly and
        -- write the count back ourselves.
        local imp_r, imp_d = t.player.click_obj("golem_golemkey")
        t.check("pickUpStrangeImplement", imp_r == "ok", "click_obj golem_golemkey -> " .. tostring(imp_r) .. " " .. tostring(imp_d))
        t.expect("has.implement", t.inv.await("golem_golemkey", 1, 5))

        -- ---- Evil Dave at the portal ----
        -- shadowstorm_ritual.rs2 [opnpc1,agrith_dave]/[opnpc1,agrith_dave_at_portal].
        -- 2713,4913 -> 2721,4911 REACH closed-doors len=36.
        t.exec("walk-dave", t.player.walk_to, 2721, 4911, 50)
        -- seam26: transcript "Infiltrating the wizards" -- undressed first, the
        -- clothing check refuses (shadowstorm_ritual.rs2 ~sots_dark_items_worn).
        t.exec("talkToEvilDave.undressed", t.player.talk_to, "agrith_dave_at_portal")
        t.exec("talkToEvilDave.undressed-dialog", t.chat.play, {
            "npc:What do you want",
            "choose:I want to join your group.",
            "player:I want to join your group",
            "npc:we do need one more person",
            "npc:you have to be evil",
            "choose:I'm evil!",
            "player:I'm evil!",
            "npc:You don't look evil",
            "player:evil in disguise",
            "npc:no need for the disguise",
        })
        t.expect("quest.stage.still_infiltrate", t.quest.expect_stage("infiltrate"))
        -- QH talkToEvilDave: dyed Silverlight and three black items EQUIPPED.
        t.exec("equip.priest_gown", t.player.equip, "priest_gown")
        t.exec("equip.priest_robe", t.player.equip, "priest_robe")
        t.exec("equip.black_cape", t.player.equip, "black_cape")
        t.exec("equip.silverlight_dyed", t.player.equip, "agrith_silverlight_dyed")
        t.exec("talkToEvilDave", t.player.talk_to, "agrith_dave_at_portal")
        t.exec("talkToEvilDave-dialog", t.chat.play, {
            "npc:What do you want",
            "choose:I want to join your group.",
            "player:I want to join your group",
            "npc:we do need one more person",
            "npc:you have to be evil",
            "choose:I'm evil!",
            "player:I'm evil!",
            "npc:totally evil",
            "npc:take you through to see Denath",
            "mesbox:escorts you into the demonic throne room",
            "npc:Master! This person wants to join us",
            "npc:one wizard short",
            "npc:totally evil",
            "npc:Thank you, Dave",
        })
        t.expect("quest.stage.denath", t.quest.expect_stage("denath"))
        t.expect("reen.moved_to_uzer", t.var.await_server("varb1382_agrith_reen_uzer", 1, 5))
        t.ticks(3)
        local tr0, tile0 = t.world.tile()
        local inroom0 = tr0 == "ok" and tile0.level == 2 and tile0.x >= 2709 and tile0.x <= 2731 and tile0.z >= 4879 and tile0.z <= 4919
        t.step("escort.lands_in_throne_room", inroom0 and "PASS" or "FAIL",
            "after Evil Dave's escort: " .. (tile0 and (tile0.x .. "," .. tile0.z .. "," .. tile0.level) or "?"))
        t.ticks(2)

        -- ---- seam24: the throne room's cast (shadowstorm_ritual.rs2 ~sots_throne_cast) ----
        t.exec("talkToDenath", t.player.talk_to, "agrith_denath")
        t.exec("talkToDenath-dialog", t.chat.play, {
            "npc:another apprentice", "player:What do I have to do", "npc:Speak to Jennifer",
        })
        t.exec("talkToJennifer", t.player.talk_to, "agrith_jennifer")
        t.exec("talkToJennifer-dialog", t.chat.play, { "player:demonic sigil mould", "npc:Take a silver bar" })
        t.expect("has.mould", t.inv.await("agrith_sigil_mould", 1, 5))
        t.expect("quest.stage.sigil_tasks", t.quest.expect_stage("sigil_tasks"))
        t.exec("talkToMatthew", t.player.talk_to, "agrith_matthew")
        t.exec("talkToMatthew-dialog", t.chat.play, { "player:what happened to Josef", "npc:Search the kilns" })
        t.expect("quest.stage.matthew", t.quest.expect_stage("matthew"))

        -- ---- QH smeltSigil (after Matthew, before the golem): smelt the sigil (silver bar on a furnace, silver_crafting:agrith_sigil) ----
        -- Out of the throne room, out of the ruin, north through the Shantay
        -- doorway to Al Kharid's furnace (the nearest: see the header).
        leave_throne("smeltSigil.leaveThroneRoom")
        ruin_up("smeltSigil.leaveRuin")
        out_of_desert("smeltSigil")
        -- 3304,3119 -> 3275,3186 REACH closed-doors len=96; the furnace room
        -- is open to the street.
        t.exec("goto-furnace", t.player.goto_tile, 3275, 3186, 0)
        local fr, furnace = t.world.loc_near("fai_falador_furnace", 10, { level = "here" })
        t.check("furnace.locate", fr == "ok", "world.loc_near fai_falador_furnace -> " .. tostring(fr)
            .. " at " .. tostring(furnace and furnace.tile_x) .. "," .. tostring(furnace and furnace.tile_z))
        local bars0 = count("silver_bar")
        t.exec("smeltSigil.use", t.player.use_on, "silver_bar", furnace)
        -- ui.await_open / npc.await_present answer a bare ok; the row states
        -- what was awaited so no PASS row is empty-detail (seam26 closer).
        local smelt_open_r = t.ui.await_open("silver_crafting", 10)
        t.step("smeltSigil.open", smelt_open_r == "ok" and "PASS" or "FAIL",
            "silver_crafting interface open within 10 ticks -> " .. tostring(smelt_open_r))
        local wr, cell = t.ui.widget("silver_crafting:agrith_sigil")
        t.check("smeltSigil.cell", wr == "ok", "ui.widget silver_crafting:agrith_sigil -> " .. tostring(wr))
        local ir = t.ui.invoke(cell, 1)
        t.check("smeltSigil", ir == "ok", "ui.invoke silver_crafting:agrith_sigil op1 -> " .. tostring(ir))
        t.expect("has.sigil", t.inv.await("agrith_sigil", 1, 10))
        t.check("smeltSigil.barUsed", bars0 == 1 and count("silver_bar") == 0 and count("agrith_sigil") == 1
            and count("agrith_sigil_mould") == 1,
            "silver_bar " .. bars0 .. " -> " .. count("silver_bar") .. ", agrith_sigil " .. count("agrith_sigil")
                .. ", mould kept " .. count("agrith_sigil_mould") .. " (jewellery.rs2 agrith_sigil: the mould is the tool)")
        -- Back south through the doorway with a second pass, to the golem.
        into_desert("talkToGolem", false)
        -- ---- The golem (QH talkToGolem, with the sigil) ----
        t.exec("goto-golem", t.player.goto_tile, 3486, 3088, 0)
        t.exec("talkToGolem", t.player.talk_to, "golem_golem")
        t.exec("talkToGolem-dialog", t.chat.play, { "player:Did you see anything", "npc:Denath came", "npc:hid it in one of the kilns" })
        t.expect("quest.stage.golem_ask", t.quest.expect_stage("golem_ask"))

        -- ---- The four kilns (QH searchKiln1..4) ----
        -- Each kiln is a 2x2 loc (its footprint is solid); the walk ends on an
        -- open tile beside it (reach.py from the golem 3486,3088: k1 3468,3122
        -- len=52, k2 3478,3081 len=19, k3 3472,3091 len=17, k4 3499,3084 len=17).
        -- The right kiln is %varb1378_agrith_kiln, rolled at random by the
        -- golem's interrogation (golem.rs2), so the search stops at the book.
        local kilns = {
            { "agrith_kiln_1", 3468, 3122 }, { "agrith_kiln_2", 3478, 3081 },
            { "agrith_kiln_3", 3472, 3091 }, { "agrith_kiln_4", 3499, 3084 },
        }
        local _, kiln_roll = t.var.server("varb1378_agrith_kiln")
        t.note("varb1378_agrith_kiln = " .. tostring(kiln_roll) .. " (0-3: kiln " .. tostring(type(kiln_roll) == "number" and kiln_roll + 1 or "?") .. " holds the book)")
        for i = 1, #kilns do
            local k = kilns[i]
            t.exec("walk-kiln" .. i, t.player.walk_to, k[2], k[3], 80)
            t.exec("searchKiln" .. i, t.player.click_loc, k[1], 1)
            t.ticks(2)
            if count("agrith_book") > 0 then break end
        end
        t.expect("has.book", t.inv.await("agrith_book", 1, 5))
        t.expect("quest.stage.ritual", t.quest.expect_stage("ritual"))
        -- QH readBook: the tome's Read op (shadowstorm_ritual.rs2:168 [opheld1,agrith_book]).
        t.exec("readBook", t.player.inv_op, "agrith_book", 1)
        t.exec("readBook-dialog", t.chat.play, { "mesbox:The tome describes the summoning of Agrith-Naar" })


        -- ---- QH enterRuinAfterBook / enterPortalAfterBook / talkToMatthewAfterBook (70 -> 80) ----
        ruin_down("enterRuinAfterBook")
        enter_portal("enterPortalAfterBook")
        t.ticks(2)
        t.exec("talkToMatthewAfterBook", t.player.talk_to, "agrith_matthew")
        t.exec("talkToMatthewAfterBook-dialog", t.chat.play, {
            "npc:Did you find that book", "player:Yes. The golem saw", "*", "*", "*", "*", "*",
            "*", "*", "npc:reverse order", "*", "*", "*", "*", "*",
            "npc:Thank goodness", "npc:time for the ritual",
        })
        t.expect("quest.stage.perform_ritual", t.var.await_server("varb1372_agrith_quest", 80, 5))
        local dn_r, dn_d = t.npc.nearest("agrith_denath", 20)
        local jn_r, jn_d = t.npc.nearest("agrith_jennifer", 20)
        t.check("circle.plain_forms_gone", dn_r ~= "ok" and jn_r ~= "ok", "plain denath " .. tostring(dn_r) .. " " .. tostring(dn_d and (dn_d.tile_x or dn_d)) .. "; plain jennifer " .. tostring(jn_r))
        local circle_denath_sigil_r = t.npc.await_present("agrith_denath_sigil", 20, 5)
        t.step("circle.denath_sigil", circle_denath_sigil_r == "ok" and "PASS" or "FAIL",
            "agrith_denath_sigil in the npc pool within 20 ticks -> " .. tostring(circle_denath_sigil_r))

        -- ---- QH standInCircle 2718,4902,2 + IncantationStep (Denath's order) ----
        local wr = t.player.walk_to(2718, 4902)
        local cr, ct = t.world.tile()
        t.check("standInCircle", wr == "ok" and ct and ct.x == 2718 and ct.z == 4902, "walk_to -> " .. tostring(wr) .. " at " .. (ct and (ct.x .. "," .. ct.z .. "," .. ct.level) or "?"))
        t.exec("chant", t.player.inv_op, "agrith_sigil", 1)
        t.exec("chant-dialog", t.chat.play, {
            "choose:Nahudu", "player:Nahudu", "choose:Camerinthum", "player:Camerinthum",
            "choose:Caldar", "player:Caldar", "choose:Agrith-Naar", "player:Agrith-Naar",
            "choose:Tarren", "player:Tarren!", "mesbox:A magic circle",
            -- seam26: the rest of the scene (transcript "Getting in place").
            "npc:Oh my gods", "npc:He disappeared", "npc:Where'd he go",
            "npc:How could we be so stupid", "player:What happened",
            "npc:summoning ritual backwards", "npc:Denath was Agrith-Naar all along",
            "mesbox:BOOM", "npc:What was that", "npc:The portal's closing",
            "npc:I'm getting out of here", "npc:No, don't leave",
            "npc:Who knows what Denath", "npc:we need eight people",
            "npc:get those three to come back",
        })
        t.expect("quest.stage.ritual_done", t.var.await_server("varb1372_agrith_quest", 90, 8))
        -- ---- QH steps.put(90): pickUpSigil (Denath's sigil left on the circle floor) ----
        local _, sigils_before = t.inv.count("agrith_sigil")
        local pr, pd = t.player.click_obj("agrith_sigil")
        t.check("pickUpSigil", pr == "ok", "click_obj agrith_sigil -> " .. tostring(pr) .. " " .. tostring(pd) .. " (sigils before " .. tostring(sigils_before) .. ")")
        t.expect("has.sigil_from_floor", t.inv.await("agrith_sigil", sigils_before + 1, 5))

        -- ---- leavePortal (QH 2720,4883,2): the placed exit golem_demon_portal ----
        -- seam26: golem_portal.rs2 [oploc1,golem_demon_portal] SotS branch ->
        -- ^sots_ruin_dave, maplink.dbrow's own destination for this portal.
        leave_throne("leavePortal")
        -- Transcript "Walking out of the portal"; wiki "take her sigil as well".
        t.check("tanya.killed_by_ghosts", t.msg.expect("Tanya killed by ghosts"))
        local _, sig_b2 = t.inv.count("agrith_sigil")
        local ps2, pd2 = t.player.click_obj("agrith_sigil")
        t.check("pickUpSigil2", ps2 == "ok", "click_obj agrith_sigil (Tanya's) -> " .. tostring(ps2) .. " " .. tostring(pd2))
        t.expect("has.tanya_sigil", t.inv.await("agrith_sigil", sig_b2 + 1, 5))

        -- ---- QH tellDaveToReturn (2721,4900,0): Evil Dave in the passage ----
        t.expect("dave.in_passage_var", t.var.await_server("varb1380_agrith_convinced_dave", 1, 3))
        local dave_in_passage_r = t.npc.await_present("agrith_dave_in_passage", 20, 5)
        t.step("dave.in_passage", dave_in_passage_r == "ok" and "PASS" or "FAIL",
            "agrith_dave_in_passage in the npc pool within 20 ticks -> " .. tostring(dave_in_passage_r))
        local _, sig_b3 = t.inv.count("agrith_sigil")
        -- Dave stands in the passage (m42_76.spawn:12, 2723,4897,0), 14 tiles
        -- south of the portal's landing: walk up to him first (account
        -- sotsb66alt2's camera put him off the top of the viewport from
        -- 2721,4911 and the press found no menu row).
        t.exec("walk-tellDaveToReturn", t.player.walk_to, 2722, 4899, 30)
        t.exec("tellDaveToReturn", t.player.talk_to, "agrith_dave_in_passage")
        t.exec("tellDaveToReturn-dialog", t.chat.play, {
            "npc:Eric's dead",
            "npc:In a BAD way",
            "choose:You've got to get back to the throne room!",
            "player:You've got to get back to the throne room",
            "npc:the portal is closing",
            "player:Our only hope",
            "npc:You can kill him",
            "npc:It was Eric's sigil",
        })
        t.expect("dave.moved", t.var.await_server("varb1380_agrith_convinced_dave", 2, 5))
        t.expect("has.eric_sigil", t.inv.await("agrith_sigil", sig_b3 + 1, 5))

        -- ---- QH goUpToBadden: leave the ruins by the stairs (2722,4885,0) ----
        ruin_up("goUpToBadden")

        -- ---- QH talkToBaddenAfterRitual / talkToReenAfterRitual ----
        -- 3491,3090 -> 3486,3091 REACH closed-doors len=6.
        t.exec("walk-badden2", t.player.walk_to, 3486, 3091)
        t.exec("talkToBaddenAfterRitual", t.player.talk_to, "agrith_badden_uzer")
        t.exec("talkToBaddenAfterRitual-dialog", t.chat.play, { "npc:Denath fled", "player:Will you join", "npc:Give me that sigil" })
        t.expect("badden.moved", t.var.await_server("varb1381_agrith_badden_uzer", 2, 5))
        t.exec("talkToReenAfterRitual", t.player.talk_to, "agrith_reen_uzer")
        t.exec("talkToReenAfterRitual-dialog", t.chat.play, { "npc:A demonic ritual", "player:simple-minded", "npc:For Saradomin" })
        t.expect("reen.moved", t.var.await_server("varb1382_agrith_reen_uzer", 2, 5))

        -- ---- QH talkToTheGolemAfterRitual / useImplementOnGolem / talkToGolemAfterReprogramming ----
        t.exec("walk-golem2", t.player.walk_to, 3486, 3088)
        t.exec("talkToTheGolemAfterRitual", t.player.talk_to, "golem_golem")
        t.exec("talkToTheGolemAfterRitual-dialog", t.chat.play, { "npc:I will not help", "mesbox:strange implement" })
        t.expect("golem.rejected", t.var.await_server("varb1379_agrith_convinced_golem", 1, 5))
        local golem_target, gtr = t.player.by_symbol("npc", "golem_golem")
        t.step("golem.by_symbol", gtr == "ok" and "PASS" or "FAIL", "by_symbol npc golem_golem -> " .. tostring(gtr))
        t.exec("useImplementOnGolem", t.player.use_on, "golem_golemkey", golem_target)
        t.exec("useImplementOnGolem-dialog", t.chat.play, { "mesbox:dusty scrolls", "mesbox:PORTAL OF THAMMARON" })
        t.expect("golem.reprogrammed", t.var.await_server("varb1379_agrith_convinced_golem", 2, 5))
        t.exec("talkToGolemAfterReprogramming", t.player.talk_to, "golem_golem")
        t.exec("talkToGolemAfterReprogramming-dialog", t.chat.play, { "npc:New task" })
        t.expect("golem.moved", t.var.await_server("varb1379_agrith_convinced_golem", 3, 5))

        -- ---- QH enterRuinAfterRecruiting / enterPortalAfterRecruiting ----
        ruin_down("enterRuinAfterRecruiting")
        enter_portal("enterPortalAfterRecruiting")
        t.ticks(2)

        -- ---- QH talkToMatthewToStartFight (90 -> 110) ----
        local matthew = "agrith_matthew"
        local mr = t.npc.nearest("agrith_matthew_sigil", 20)
        if mr == "ok" then matthew = "agrith_matthew_sigil" end
        t.exec("talkToMatthewToStartFight", t.player.talk_to, matthew)
        t.exec("talkToMatthewToStartFight-dialog", t.chat.play, {
            "npc:eight people now", "choose:Yes.", "player:Yes.", "npc:Okay, here we go",
        })
        t.expect("quest.stage.summon", t.quest.expect_stage("summon"))
        local circle2_golem_sigil_r = t.npc.await_present("agrith_golem_sigil", 20, 5)
        t.step("circle2.golem_sigil", circle2_golem_sigil_r == "ok" and "PASS" or "FAIL",
            "agrith_golem_sigil in the npc pool within 20 ticks -> " .. tostring(circle2_golem_sigil_r))

        -- ---- QH standInCircleAgain 2720,4903,2 + IncantationStep (the tome's order) ----
        local wr2 = t.player.walk_to(2720, 4903)
        local cr2, ct2 = t.world.tile()
        t.check("standInCircleAgain", wr2 == "ok" and ct2 and ct2.x == 2720 and ct2.z == 4903, "walk_to -> " .. tostring(wr2) .. " at " .. (ct2 and (ct2.x .. "," .. ct2.z .. "," .. ct2.level) or "?"))
        t.exec("incantRitual", t.player.inv_op, "agrith_sigil", 1)
        t.exec("incantRitual-dialog", t.chat.play, {
            "choose:Tarren", "player:Tarren", "choose:Agrith-Naar", "player:Agrith-Naar",
            "choose:Caldar", "player:Caldar", "choose:Camerinthum", "player:Camerinthum",
            "choose:Nahudu", "player:Nahudu!",
            "npc:Matthew!", "npc:How dare you summon me", "npc:Aaaargh",
            "player:He didn't summon you", "player:I did!", "npc:Then prepare to die",
        })
        t.expect("quest.stage.fight", t.quest.expect_stage("fight"))

        -- ---- QH killDemon: Agrith-Naar, final blow with the dyed Silverlight worn ----
        local naar_r = t.npc.await_present("agrith_naar", 20, 5)
        t.step("naar.summoned", naar_r == "ok" and "PASS" or "FAIL",
            "agrith_naar in the npc pool within 20 ticks of the incantation -> " .. tostring(naar_r))
        -- A real fight: Agrith-Naar has a server combat block (OSRS-Content
        -- 18a4ed96b9: hitpoints 95, attack 83, strength 90, defence 82, magic
        -- 100, ranged 100, max hit 10, aggressive), npc_combat/a/agrith_naar.combat and
        -- its own AI (shadowstorm_ritual.rs2 [ai_opplayer2,agrith_naar]: melee,
        -- Fire Blast, Telekinetic Grab), and docs/bosses/quest_combat_manifest.json
        -- quest-shadow-of-the-storm. The wiki's requirement is "the ability to
        -- defeat a level 100 demon": staged 80 melee stats and 10 sharks, eaten
        -- inside the presses and the wait. No protection prayer: the plan does
        -- not pray (Protect from Melee only turns his swing into Fire Blast).
        local EAT = { item = "shark", below = 50 }
        local _, xp0 = t.skill.snapshot()
        local _, gems_var0 = t.var.server("varb354_golem_throne_gems")
        local sapphire0, ruby0, emerald0 = count("sapphire"), count("ruby"), count("emerald")
        local lamp0, darklight0 = count("thosf_reward_lamp"), count("darklight")
        local sharks0 = count("shark")
        local att_r, att_d = t.player.attack("agrith_naar", 2, 15, { eat = EAT })
        t.step("naar.attack", att_r == "ok" and "PASS" or "FAIL", "player.attack agrith_naar -> " .. tostring(att_r) .. " " .. tostring(att_d))
        local dead_r, dead_d = t.npc.await_dead_engaged(240, 10, { eat = EAT })
        t.step("naar.dead", dead_r == "ok" and "PASS" or "FAIL", "await_dead_engaged -> " .. tostring(dead_r) .. " " .. tostring(dead_d))
        -- Margin: the lowest hitpoints either eater read, at least a quarter of
        -- the staged 80 (20), AND food left. A detail with no "lowest hp N/"
        -- reading fails the row.
        local low_a = tonumber(string.match(tostring(att_d), "lowest hp (%d+)/"))
        local low_d = tonumber(string.match(tostring(dead_d), "lowest hp (%d+)/"))
        local low = nil
        if low_a ~= nil and low_d ~= nil then
            low = math.min(low_a, low_d)
        end
        local sharks1 = count("shark")
        t.check("naar.margin", low ~= nil and low >= 20 and sharks1 >= 1,
            "Agrith-Naar (level 100): lowest hp " .. tostring(low) .. "/80 (attack press " .. tostring(low_a)
                .. ", kill wait " .. tostring(low_d) .. "), sharks " .. sharks0 .. " -> " .. sharks1
                .. " (margin: lowest hp >= 20, a quarter of 80, AND food left)")
        t.expect("player.aliveAfterNaar", t.player.alive())
        t.ticks(5)
        -- The blade fuses in the hand (seam26): Darklight is WORN, then QH
        -- steps.put(124) unequipDarklight (the wiki's fallback trigger).
        -- (player.unequip answers not_found when nothing of it is worn.)
        t.exec("unequipDarklight", t.player.unequip, "darklight")
        t.expect("naar.darklight", t.inv.await("darklight", 1, 5))
        t.quest.expect_complete()
        -- Literal rewards (shadowstorm_ritual.rs2:929-964 sots completion):
        -- Silverlight becomes Darklight; thosf_reward_lamp plus its 10,000 xp
        -- paid straight into Hitpoints (stat_advance(hitpoints,
        -- ^sots_agrith_lamp_xp = 100000 tenths)); six cut gems (2 sapphire,
        -- 2 ruby, 2 emerald) when the throne gems were never prised in The
        -- Golem (varb354_golem_throne_gems = 0); one quest point (quest.points).
        t.check("reward.darklight", darklight0 == 0 and count("darklight") == 1 and count("agrith_silverlight_dyed") == 0,
            "darklight " .. darklight0 .. " -> " .. count("darklight") .. ", agrith_silverlight_dyed left " .. count("agrith_silverlight_dyed"))
        t.check("reward.lamp", count("thosf_reward_lamp") == lamp0 + 1,
            "thosf_reward_lamp " .. lamp0 .. " -> " .. count("thosf_reward_lamp"))
        t.check("reward.gems", gems_var0 == 0 and count("sapphire") == sapphire0 + 2 and count("ruby") == ruby0 + 2
            and count("emerald") == emerald0 + 2,
            "varb354_golem_throne_gems before " .. tostring(gems_var0) .. "; sapphire " .. sapphire0 .. " -> " .. count("sapphire")
                .. ", ruby " .. ruby0 .. " -> " .. count("ruby") .. ", emerald " .. emerald0 .. " -> " .. count("emerald") .. " (want +2 each)")
        -- The fight's own Hitpoints xp rides in the same delta: per hit,
        -- give_combat_experience (skill_combat/combat.rs2) pays the style
        -- 4*base (or 1.33*base to each of three for controlled) and
        -- Hitpoints floor(1.33*base), so the combat part is the style xp's
        -- 133/400 (or a third), less at most one unit per hit for the floor.
        -- What is left over is the reward: 10,000 xp (100000 tenths).
        local _, xp1 = t.skill.snapshot()
        local function dxp(name)
            if type(xp0) ~= "table" or type(xp1) ~= "table" or type(xp0[name]) ~= "table" or type(xp1[name]) ~= "table" then
                return nil
            end
            return xp1[name].experience - xp0[name].experience
        end
        local d_att, d_str, d_def, d_hp = dxp("attack"), dxp("strength"), dxp("defence"), dxp("hitpoints")
        local reward_ok, reward_text = false, "no skill reading"
        if d_att ~= nil and d_str ~= nil and d_def ~= nil and d_hp ~= nil then
            local styles_moved = (d_att > 0 and 1 or 0) + (d_str > 0 and 1 or 0) + (d_def > 0 and 1 or 0)
            local style_total = d_att + d_str + d_def
            local combat_hp = styles_moved == 3 and style_total / 3 or style_total * 133 / 400
            local reward = d_hp - combat_hp
            -- The unit: tenths when the style delta is (the whole fight's
            -- melee xp is far above 10,000 tenths only in tenths).
            local want = 10000
            if math.abs(reward - 100000) < math.abs(reward - 10000) then
                want = 100000
            end
            local slack = want == 100000 and 100 or 10
            reward_ok = reward <= want + 1 and reward >= want - slack
            reward_text = string.format("hitpoints +%d, attack +%d, strength +%d, defence +%d (%d style(s)); combat share %.1f, reward %.1f, want %d (%s, floor slack %d)",
                d_hp, d_att, d_str, d_def, styles_moved, combat_hp, reward, want, want == 100000 and "tenths" or "whole xp", slack)
        end
        t.check("reward.hitpoints_xp", reward_ok, reward_text)
        t.finish(0)
        return
    end,
}
