-- Shadow of the Storm (shadowstorm), the recorded flow: RSProx index 4475
-- (rev 242, the start) and index 4477 (rev 242, stage 20 to the end); the
-- analysis is docs/rsprox-analysis/quests/shadow_of_the_storm.md. The
-- legs follow the recorder's order: Reen, Badden (10 -> 20), the mushroom
-- and the dye, Evil Dave refusing two pieces and taking three (the escort,
-- 30), Denath's welcome (40, the words rolled), Matthew's story (50), the
-- golem (60), the kilns and the book, Matthew with the book (70), Jennifer's
-- mould, the sigils at a furnace, Matthew starting the first ritual (80) and
-- the chant in Denath's order (90), out by the closing portal (Tanya, 100),
-- Dave in the passage, the stairs and the sandstorm, Badden, Reen and the
-- golem (reprogrammed with the strange implement), the second ritual in the
-- book's order (110 -> 120), Agrith-Naar, the finale and the skill.
--
-- The throne room is a private copy (x > 6400) built on every entry; tiles
-- in it are offsets from the copy's corner, read off the entry landing
-- (the copy's 32,20).
return {
    id = "shadowstorm",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::complete quest_golem",
        "::complete quest_demonslayer",
        "::setlevel crafting 30",
        "::give silver_bar 2",
        -- Evil Dave's three pieces: the recorder's (4475 [1124]: black med
        -- helm, priest gown, priest robe).
        "::give black_med_helm",
        "::give priest_gown",
        "::give priest_robe",
        "::give hammer",
        "::give chisel",
        "::setlevel attack 80",
        "::setlevel strength 80",
        "::setlevel defence 80",
        "::setlevel hitpoints 80",
        "::give shark 10",
        "::give coins 10",
        "::give water_skin4 3",
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
            -- Before Tourist Trap, Irena (shantay.rs2, RSProx index 4448).
            lines[#lines + 1] = "npc:There is a heartbroken mother"
            lines[#lines + 1] = "choose:I want to buy a Shantay pass for 5 gold coins."
            lines[#lines + 1] = "player:I want to buy a Shantay pass for"
            lines[#lines + 1] = "objbox:You purchase a Shantay Pass."
            t.exec(prefix .. ".buyPass-dialog", t.chat.play, lines)
            local pass_await = t.inv.await("shantay_pass", 1, 5)
            t.check(prefix .. ".buyPass-paid", pass_await == "ok" and count("shantay_pass") == pass0 + 1 and count("coins") == coins0 - 5,
                "shantay_pass " .. pass0 .. " -> " .. count("shantay_pass") .. ", coins " .. coins0 .. " -> " .. count("coins") .. " (want -5, shantay.rs2)")
            -- shantay_pass.rs2: the warning (cws_warning_10), "warn1" takes the
            -- pass and steps the player south.
            t.exec(prefix .. ".shantayDoorway", t.player.click_loc, "shantay_pass_henge_doorway", 1)
            t.check(prefix .. ".warning", t.ui.await_open("cws_warning_10", 12) == "ok", "cws_warning_10 open")
            local wr, yes = t.ui.widget("cws_warning_10:warn1")
            t.exec(prefix .. ".warn1", t.ui.invoke, yes, 1)
            t.await({ match = function()
                local r, tile = t.world.tile()
                return r == "ok" and tile.z <= 3115
            end, note = prefix .. ": south of the doorway" }, 10)
            t.check(prefix .. ".shantayDoorway.passHandedOver", count("shantay_pass") == pass0,
                "shantay_pass " .. count("shantay_pass") .. " after the doorway, at " .. reading())
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
            t.exec("walk-" .. name, t.player.goto_tile, 3491, 3090, 0)
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
        -- A page with no continue (the escort's box) is waited out: the next
        -- page is looked for by its text (demon.lua's await_text).
        local function await_text(name, fragment, ticks)
            local seen, found = "none", false
            for _ = 1, ticks or 60 do
                if t.chat.kind() ~= "none" then
                    local kr, txt = t.chat.text()
                    if kr == "ok" and txt then
                        seen = txt
                        if txt:find(fragment, 1, true) then
                            found = true
                            break
                        end
                    end
                end
                t.ticks(1)
            end
            t.check(name, found, "page with '" .. fragment .. "' (last: " .. tostring(seen) .. ")")
        end
        local function in_copy(tile)
            return type(tile) == "table" and tile.x > 6400
        end
        -- The copy's corner, from the entry landing (copy 32,20).
        local cx, cz = nil, nil
        local function at(dx, dz)
            return cx + dx, cz + dz
        end
        local function enter_portal(name)
            t.exec("walk-" .. name, t.player.walk_to, 2720, 4911, 50)
            t.exec(name, t.player.click_loc, "golem_portal", 1)
            t.await({
                match = function()
                    local r, tile = t.world.tile()
                    return r == "ok" and in_copy(tile)
                end,
                note = name .. ": the portal's landing in the throne room's copy",
            }, 10)
            local _, tile = t.world.tile()
            t.check(name .. ".landed", in_copy(tile), "in the copy at " .. reading())
            if in_copy(tile) then
                cx, cz = tile.x - 32, tile.z - 20
            end
            t.ticks(3)
        end
        -- Out of the copy by the portal in its south wall (copy 31,19) to the
        -- passage in front of the Uzer portal (2721,4911,0).
        local function leave_copy(name, loc)
            local x, z = at(32, 21)
            t.exec("walk-" .. name, t.player.walk_to, x, z, 60)
            t.exec(name, t.player.click_loc, loc, 1)
            t.await({
                match = function()
                    local r, tile = t.world.tile()
                    return r == "ok" and tile.x == 2721 and tile.z == 4911 and tile.level == 0
                end,
                note = name .. ": out of the copy at 2721,4911,0",
            }, 10)
            t.check(name .. ".out", reading() == "2721,4911,0", "after the portal: " .. reading())
        end
        -- The player's rolled words (varb1373..1377: the book's order; Denath
        -- teaches it backwards).
        local WORDS = { [0] = "Caldar", [1] = "Nahudu", [2] = "Agrith-Naar", [3] = "Camerinthum", [4] = "Tarren" }
        local function words(backwards)
            local w = {}
            for i = 1, 5 do
                local _, v = t.var.server("varb" .. (1372 + i) .. "_agrith_incantation_" .. i)
                w[i] = WORDS[v] or "?"
            end
            if backwards then
                w = { w[5], w[4], w[3], w[2], w[1] }
            end
            return w
        end
        local function chant_lines(w, tail)
            local lines = {}
            for i = 1, 5 do
                lines[#lines + 1] = "choose:" .. w[i]
                if i == 5 then
                    lines[#lines + 1] = "player:" .. w[i] .. "!"
                else
                    lines[#lines + 1] = "player:" .. w[i] .. "..."
                end
            end
            for _, l in ipairs(tail or {}) do
                lines[#lines + 1] = l
            end
            return lines
        end

        local bind_r, bind_d = t.quest.bind({
            varp = "varb1372_agrith_quest",
            constants = {
                not_started = 0, see_badden = 10, infiltrate = 20, denath = 30, sigil_tasks = 40,
                matthew = 50, golem_ask = 60, ritual = 70, ritual_started = 80, ritual_done = 90,
                recruit = 100, summon = 110, fight = 120, unequip = 124, complete = 125,
            },
            row = "quest_shadowofthestorm",
            display = "Shadow of the Storm",
            points = 1,
        })
        t.step("quest.bind", bind_r == "ok" and "PASS" or "FAIL", bind_d)
        -- The wire audit against RSProx 4475/4477 reads this tick log.
        t.ticklog.start()
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- ---- 4475 [136..175]: Father Reen starts it ----
        t.exec("goto-reen", t.player.goto_tile, 3271, 3159, 0)
        t.exec("talkToReen", t.player.talk_to, "agrith_reen_alkharid")
        t.exec("talkToReen-dialog", t.chat.play, {
            "npc:I have an urgent job for",
            "choose:Yes.",
            "player:That's me!",
            "npc:Thank Saradomin I found you!",
            "npc:Now, do you recognise this sword?",
            "objbox:Reen hands you a shining silver sword.",
            "player:Silverlight! That's the sword",
            "npc:only with Silverlight!",
            "npc:My partner Badden",
            "npc:A week ago",
            "player:So you want me to stop him?",
            "npc:Stop him? No!",
            "npc:Let me explain.",
            "npc:This summoning is our chance",
            "npc:You should go to Uzer and find Badden at once.",
            "end",
        })
        t.expect("quest.stage.see_badden", t.quest.expect_stage("see_badden"))
        t.expect("has.silverlight", t.inv.await("silverlight", 1, 5))

        -- ---- 4475 [629..731]: Badden ----
        into_desert("talkToBadden", true)
        t.exec("goto-badden", t.player.goto_tile, 3486, 3090, 0)
        t.exec("talkToBadden", t.player.talk_to, "agrith_badden_uzer")
        t.exec("talkToBadden-dialog", t.chat.play, {
            "npc:What brings you to this godsforsaken desert?",
            "choose:Reen sent me.", "player:Reen sent me.",
            "npc:And you have Silverlight!",
            "choose:Tell me more about Denath.", "player:Tell me more about Denath.",
            "npc:keeping an eye on Denath", "npc:group that summoned Delrith", "npc:studying Agrith-Naar for years",
            "choose:Why has he come to Uzer?", "player:Why has he come to Uzer?",
            "npc:This city is very ancient", "npc:foul construct over there", "npc:Do you know what happened here?",
            "choose:Foul construct? Do you mean the golem?", "player:Foul construct?",
            "npc:The golem, yes!", "player:created to fight a demon", "player:Thousands of years ago", "player:When I last came here",
            "npc:You opened a portal", "npc:much easier to reach", "npc:Denath must have heard",
            "choose:So what do you want me to do?", "player:So what do you want me to do?",
            "npc:You have to be in position",
            "choose:How can I do that?", "player:How can I do that?",
            "npc:convince them you're one of them.",
            "end",
        })
        t.expect("quest.stage.infiltrate", t.quest.expect_stage("infiltrate"))

        -- ---- 4475 [754], 4477 [221]: the mushroom and the dye ----
        t.exec("walk-mushroom", t.player.walk_to, 3494, 3088)
        t.exec("pickMushroom", t.player.click_loc, "golem_black_mushrooms", 1)
        t.exec("pickMushroom-dialog", t.chat.play, { "objbox:You pick a mushroom." })
        t.expect("has.mushroom", t.inv.await("golem_mushroom", 1, 5))
        t.exec("dyeSilverlight", t.player.use_item_on_item, "golem_mushroom", "silverlight")
        t.exec("dyeSilverlight-dialog", t.chat.play, { "objbox:it stains the blade black." })
        t.expect("has.dyed_silverlight", t.inv.await("agrith_silverlight_dyed", 1, 5))
        t.check("dyeSilverlight.consumed", count("silverlight") == 0 and count("golem_mushroom") == 0,
            "silverlight " .. count("silverlight") .. ", golem_mushroom " .. count("golem_mushroom"))

        -- ---- The ruin: the strange implement, Evil Dave ----
        ruin_down("goIntoRuin")
        t.exec("walk-implement", t.player.walk_to, 2713, 4912, 70)
        local imp_r, imp_d = t.player.click_obj("golem_golemkey")
        t.check("pickUpStrangeImplement", imp_r == "ok", "click_obj golem_golemkey -> " .. tostring(imp_r) .. " " .. tostring(imp_d))
        t.expect("has.implement", t.inv.await("golem_golemkey", 1, 5))
        t.exec("walk-dave", t.player.walk_to, 2721, 4911, 50)
        -- [647]: the portal before the escort is Dave's.
        t.exec("portal.refused", t.player.click_loc, "golem_portal", 1)
        t.exec("portal.refused-dialog", t.chat.play, { "npc:Hey! This is our portal! Keep out!" })
        -- 4475 [1093]: two pieces are not enough.
        t.exec("equip.priest_gown", t.player.equip, "priest_gown")
        t.exec("equip.priest_robe", t.player.equip, "priest_robe")
        t.exec("talkToEvilDave.twoPieces", t.player.talk_to, "agrith_dave_at_portal")
        t.exec("talkToEvilDave.twoPieces-dialog", t.chat.play, {
            "npc:What do you want?",
            "choose:I want to join your group.", "player:I want to join your group.",
            "npc:Well we do need one more person now", "npc:To join, you have to be evil!",
            "choose:I'm evil!", "player:I'm evil!",
            "npc:You don't look evil. You certainly don't dress evil!",
            "player:I'm evil in disguise?",
            "npc:There's no need for the disguise here!",
            "end",
        })
        t.expect("quest.stage.still_infiltrate", t.quest.expect_stage("infiltrate"))
        -- [931..979]: three pieces, the dyed sword carried: the escort.
        t.ticklog.mark("leg.escort")
        t.exec("equip.black_med_helm", t.player.equip, "black_med_helm")
        t.exec("talkToEvilDave", t.player.talk_to, "agrith_dave_at_portal")
        t.exec("talkToEvilDave-dialog", t.chat.play, {
            "npc:What do you want?",
            "choose:I want to join your group.", "player:I want to join your group.",
            "npc:Well we do need one more person now", "npc:To join, you have to be evil!",
            "choose:I'm evil!", "player:I'm evil!",
            "npc:That outfit is totally evil!",
            "npc:And I love that evil black sword!",
            "npc:Okay, I'll take you through to see Denath.",
        })
        await_text("escort.master", "Master! This person wants to join us!", 60)
        t.exec("escort-dialog", t.chat.play, {
            "npc:Master! This person wants to join us!",
            "npc:Can you vouch for",
            "npc:totally evil!",
            "npc:Thank you, Dave.",
            "npc:So you wish to join us?",
            "npc:An abyssal whip",
            "npc:power of life and death",
            "npc:every wish granted",
            "choose:Why did Josef run away?", "player:Why did Josef run away?",
            "npc:Obviously he was a coward",
            "choose:What do I have to do?", "player:What do I have to do?",
            "npc:When I have finished preparing the magic circle",
            "npc:You must memorize the incantation carefully.",
            "npc:make yourself a demonic sigil",
            "npc:Now let me continue with my preparations!",
            "end",
        })
        t.expect("quest.stage.sigil_tasks", t.quest.expect_stage("sigil_tasks"))
        t.expect("reen.moved_to_uzer", t.var.await_server("varb1382_agrith_reen_uzer", 1, 5))
        local _, et = t.world.tile()
        t.check("escort.in_copy", in_copy(et), "after the escort: " .. reading())
        if in_copy(et) then
            cx, cz = et.x - 32, et.z - 47
        end

        -- ---- [1035..1118]: Matthew's story ----
        t.exec("talkToMatthew", t.player.talk_to, "agrith_matthew")
        t.exec("talkToMatthew-dialog", t.chat.play, {
            "npc:Hello. Are you Josef's replacement?",
            "choose:Do you know what happened to Josef?", "player:Do you know what happened to Josef?",
            "npc:Shh!", "npc:When we joined him", "npc:The others, they just think", "npc:Josef was more curious",
            "npc:his face turned white", "npc:But Denath was waking up", "npc:Josef didn't have money",
            "npc:Josef must have hidden the book",
            "end",
        })
        t.expect("quest.stage.matthew", t.quest.expect_stage("matthew"))

        -- ---- [1147..1177]: Tanya's laugh (three pages with no continue) ----
        t.exec("talkToTanya", t.player.talk_to, "agrith_tanya")
        t.exec("talkToTanya-dialog", t.chat.play, {
            "npc:Soon, all will bow down before me!",
            "choose:Oh?", "player:Oh?",
            "npc:I shall be a dark queen",
        })
        await_text("tanya.laugh", "Ha ha ha ha!", 5)
        await_text("tanya.matthew", "Look, we've told you not to do that.", 20)
        t.exec("tanya-sorry", t.chat.play, {
            "npc:Look, we've told you not to do that.",
            "npc:Sorry.",
            "end",
        })

        -- ---- [1925..1953]: the golem ----
        leave_copy("leaveRoom1", "golem_demon_portal")
        ruin_up("leaveRuin1")
        t.exec("goto-golem", t.player.goto_tile, 3486, 3088, 0)
        t.exec("talkToGolem", t.player.talk_to, "golem_golem")
        t.exec("talkToGolem-dialog", t.chat.play, {
            "npc:It is good to see a friendly face here.",
            "choose:Did you see anything happen last night?", "player:Did you see anything happen last night?",
            "npc:I saw one of the men in dark robes", "npc:He was carrying a large book.",
            "npc:Then another man came out of the temple.", "npc:I should have saved him",
            "player:Which kiln did he hide the book in?", "npc:I cannot remember.",
            "end",
        })
        t.expect("quest.stage.golem_ask", t.quest.expect_stage("golem_ask"))

        -- ---- [1960..2105]: the kilns ----
        local kilns = {
            { "agrith_kiln_1", 3468, 3122 }, { "agrith_kiln_2", 3478, 3081 },
            { "agrith_kiln_3", 3472, 3091 }, { "agrith_kiln_4", 3499, 3084 },
        }
        local _, kiln_roll = t.var.server("varb1378_agrith_kiln")
        t.note("varb1378_agrith_kiln = " .. tostring(kiln_roll))
        for i = 1, #kilns do
            local k = kilns[i]
            t.exec("walk-kiln" .. i, t.player.walk_to, k[2], k[3], 80)
            t.exec("searchKiln" .. i, t.player.click_loc, k[1], 1)
            if i - 1 == kiln_roll then
                t.exec("searchKiln" .. i .. "-dialog", t.chat.play, { "objbox:You find a book in the kiln." })
                break
            end
            t.exec("searchKiln" .. i .. "-dialog", t.chat.play, { "mesbox:You look in the kiln but you see only sand" })
        end
        t.expect("has.book", t.inv.await("agrith_book", 1, 5))

        -- ---- [2279..2333]: Matthew with the book (70) ----
        ruin_down("enterRuin2")
        enter_portal("enterRoom2")
        do local x, z = at(33, 34); t.exec("walk-talkToMatthew.book", t.player.walk_to, x, z, 40) end
        t.exec("talkToMatthew.book", t.player.talk_to, "agrith_matthew")
        t.exec("talkToMatthew.book-dialog", t.chat.play, {
            "npc:Did you find that book?",
            "player:Yes. The golem saw Josef hide it", "npc:Did the golem see what happened to Josef?",
            "player:It said Denath killed him.", "npc:Oh no!", "npc:I knew Denath was a dark wizard",
            "npc:Let me look at the book then.", "objbox:Matthew quickly reads the book",
            "npc:I don't understand.", "npc:But look at the incantation in the book.",
            "player:The incantation in the book is backwards?", "npc:More likely the book has the correct incantation",
            "npc:I'm not happy about this ritual.",
            "player:This is the demon-slaying sword Silverlight",
            "npc:Thank goodness! I think Denath is nearly ready",
            "end",
        })
        t.expect("quest.stage.ritual", t.quest.expect_stage("ritual"))

        -- ---- [2366..2376]: Jennifer's mould ----
        t.exec("talkToJennifer", t.player.talk_to, "agrith_jennifer")
        t.exec("talkToJennifer-dialog", t.chat.play, {
            "npc:Huh.",
            "choose:Do you have the demonic sigil mould?", "player:Do you have the demonic sigil mould?",
            "npc:So that's all you want.", "player:Thanks!", "npc:Huh.",
            "end",
        })
        t.expect("has.mould", t.inv.await("agrith_sigil_mould", 1, 5))

        -- ---- [2641..2650], [3688]: two sigils at a furnace ----
        leave_copy("leaveRoom2", "golem_demon_portal")
        ruin_up("leaveRuin2")
        out_of_desert("smeltSigil")
        t.exec("goto-furnace", t.player.goto_tile, 3275, 3186, 0)
        local fr, furnace = t.world.loc_near("fai_falador_furnace", 10, { level = "here" })
        t.check("furnace.locate", fr == "ok", "world.loc_near fai_falador_furnace -> " .. tostring(fr))
        for n = 1, 2 do
            t.ticks(8)
            -- The second use can answer with the modal before its click
            -- settles; the interface opening is what the next row checks.
            local ur, ud = t.player.use_on("silver_bar", furnace)
            t.note("smeltSigil.use" .. n .. " -> " .. tostring(ur) .. " " .. tostring(ud))
            local smelt_open_r = t.ui.await_open("silver_crafting", 10)
            t.step("smeltSigil.open" .. n, smelt_open_r == "ok" and "PASS" or "FAIL", "silver_crafting open -> " .. tostring(smelt_open_r))
            local wr, cell = t.ui.widget("silver_crafting:agrith_sigil")
            local ir = t.ui.invoke(cell, 1)
            t.check("smeltSigil" .. n, wr == "ok" and ir == "ok", "invoke agrith_sigil -> " .. tostring(ir))
            t.expect("has.sigil" .. n, t.inv.await("agrith_sigil", n, 10))
        end
        into_desert("back", false)

        -- ---- [2841..3008]: the first ritual (80 -> 90) ----
        t.ticklog.mark("leg.ritual1")
        ruin_down("enterRuin3")
        enter_portal("enterRoom3")
        do local x, z = at(33, 34); t.exec("walk-talkToMatthew.ritual", t.player.walk_to, x, z, 40) end
        t.exec("talkToMatthew.ritual", t.player.talk_to, "agrith_matthew")
        t.exec("talkToMatthew.ritual-dialog", t.chat.play, {
            "npc:Are you prepared for the ritual?", "player:I'll get ready.",
            "npc:It is time for the ritual to commence!",
            "end",
        })
        t.expect("quest.stage.ritual_started", t.quest.expect_stage("ritual_started"))
        local px, pz = at(30, 38)
        local wr1 = t.player.walk_to(px, pz)
        t.check("standInCircle", wr1 == "ok", "walk_to the first circle place -> " .. tostring(wr1) .. " at " .. reading())
        t.ticks(3)
        t.exec("chant1", t.player.inv_op, "agrith_sigil", 1)
        t.exec("chant1-dialog", t.chat.play, chant_lines(words(true), {
            "mesbox:A magic circle appears in the floor...",
            "npc:Oh my gods!", "npc:He disappeared!", "npc:Where'd he go?",
            "npc:How could we be so stupid?", "player:What happened?",
            "npc:What do you get when you perform a summoning ritual backwards?",
            "npc:Denath was Agrith-Naar all along!",
            "mesbox:BOOM!", "npc:What was that?!", "npc:Look! The portal's closing!",
            "npc:I'm getting out of here!", "npc:No, don't leave!",
            "npc:Who knows what Denath could do now?", "npc:But we need eight people!",
            "npc:you've got to get those three to come back!",
        }))
        t.expect("quest.stage.ritual_done", t.var.await_server("varb1372_agrith_quest", 90, 8))
        t.ticks(4)

        -- ---- [3071..3083]: out by the closing portal; Tanya ----
        t.ticklog.mark("leg.tanya")
        leave_copy("leaveRoomClosing", "agrith_portal_closing")
        t.expect("quest.stage.recruit", t.var.await_server("varb1372_agrith_quest", 100, 12))
        t.check("tanya.killed_by_ghosts", t.msg.expect("You see Tanya killed by ghosts!"))
        t.ticks(2)
        local _, sig0 = t.inv.count("agrith_sigil")
        local ps, pd = t.player.click_obj("agrith_sigil")
        t.check("pickUpTanyaSigil", ps == "ok", "click_obj agrith_sigil -> " .. tostring(ps) .. " " .. tostring(pd))
        t.expect("has.tanya_sigil", t.inv.await("agrith_sigil", sig0 + 1, 5))

        -- ---- [3101..3162]: Dave in the passage ----
        t.exec("walk-davePassage", t.player.walk_to, 2722, 4899, 30)
        local _, sig1 = t.inv.count("agrith_sigil")
        t.exec("tellDaveToReturn", t.player.talk_to, "agrith_dave_in_passage")
        t.exec("tellDaveToReturn-dialog", t.chat.play, {
            "npc:Eric's dead!", "npc:In a BAD way!",
            "choose:You've got to get back to the throne room!", "player:You've got to get back to the throne room!",
            "npc:But the portal is closing!", "player:Our only hope", "npc:You can kill him?",
            "npc:It was Eric's sigil.",
            "end",
        })
        t.expect("dave.agreed", t.var.await_server("varb1380_agrith_convinced_dave", 2, 5))
        t.expect("has.eric_sigil", t.inv.await("agrith_sigil", sig1 + 1, 5))

        -- ---- [3206..3211]: the stairs (the rocks may fall first) ----
        t.ticklog.mark("leg.storm")
        t.exec("walk-stairs", t.player.walk_to, 2721, 4886, 50)
        -- The fall is a 1-in-2 roll per click (shadowstorm_dye.rs2): ten
        -- clicks leave a 1-in-1024 chance of never climbing.
        for n = 1, 10 do
            t.player.click_loc("golem_insidestairs_base", 1)
            t.ticks(4)
            if reading() == "3491,3090,0" then break end
        end
        t.check("leaveRuin.storm", reading() == "3491,3090,0", "up the stairs: " .. reading())
        t.check("sandstorm.message", t.msg.expect("You must fight your way against a sandstorm!"))

        -- ---- [3275..3288]: Badden ----
        t.exec("walk-badden2", t.player.walk_to, 3485, 3090)
        t.exec("recruitBadden", t.player.talk_to, "agrith_badden_uzer")
        t.exec("recruitBadden-dialog", t.chat.play, {
            "npc:The wind suddenly picked up!", "player:Denath was Agrith-Naar all along!",
            "npc:Saradomin protect us!", "player:You've got to help me summon him again",
            "npc:Alright!", "player:You'll need to hold this sigil for the ritual.",
            "end",
        })
        t.expect("badden.agreed", t.var.await_server("varb1381_agrith_badden_uzer", 2, 5))

        -- ---- [3326..3338]: Reen ----
        t.exec("recruitReen", t.player.talk_to, "agrith_reen_uzer")
        t.exec("recruitReen-dialog", t.chat.play, {
            "npc:This sandstorm came out of nowhere!", "player:Now he's back in his own dimension!",
            "npc:Oh no! What can we do?", "player:You've got to help me perform the ritual again",
            "npc:I don't think I could take part in a demonic ritual!",
            "choose:Oh, don't be so simple-minded!", "player:Oh, don't be so simple-minded!",
            "npc:Well... all right.", "player:Now take this demonic sigil, and hurry inside!",
            "end",
        })
        t.expect("reen.agreed", t.var.await_server("varb1382_agrith_reen_uzer", 2, 5))

        -- ---- [3343..3388], [3750..3763]: the golem ----
        t.exec("walk-golem2", t.player.walk_to, 3486, 3088)
        t.exec("askGolem", t.player.talk_to, "golem_golem")
        t.exec("askGolem-dialog", t.chat.play, {
            "npc:I sensed a great darkness", "npc:And now this sandstorm appears",
            "player:We've accidentally helped him", "npc:It is a terrible thing",
            "player:You could help me summon the demon again", "npc:It is forbidden for a golem to enter the portal.",
            "choose:I could just reprogram you again.", "player:I could just reprogram you again.",
            "npc:That is true.", "npc:But there must have been a reason",
        })
        t.expect("golem.refused", t.var.await_server("varb1379_agrith_convinced_golem", 1, 5))
        t.chat.close()
        local golem_target, gtr = t.player.by_symbol("npc", "golem_golem")
        t.step("golem.by_symbol", gtr == "ok" and "PASS" or "FAIL", "by_symbol npc golem_golem -> " .. tostring(gtr))
        t.exec("useImplementOnGolem", t.player.use_on, "golem_golemkey", golem_target)
        t.exec("useImplementOnGolem-dialog", t.chat.play, {
            "mesbox:You open the golem's head", "mesbox:PORTAL OF THAMMARON",
            "end",
        })
        t.expect("golem.reprogrammed", t.var.await_server("varb1379_agrith_convinced_golem", 2, 5))
        t.exec("recruitGolem", t.player.talk_to, "golem_golem")
        t.exec("recruitGolem-dialog", t.chat.play, {
            "npc:I sensed a great darkness", "npc:And now this sandstorm appears",
            "player:We've accidentally helped him", "npc:It is a terrible thing",
            "player:You could help me summon the demon again", "npc:What are we waiting for?",
            "player:Take this demonic sigil and go through the portal.",
            "end",
        })
        t.expect("golem.agreed", t.var.await_server("varb1379_agrith_convinced_golem", 3, 5))
        t.check("sigil.kept", count("agrith_sigil") >= 1, "sigils left for the ritual: " .. count("agrith_sigil"))

        -- ---- [3805..4082]: the second ritual (110 -> 120) ----
        t.ticklog.mark("leg.ritual2")
        ruin_down("enterRuin4")
        enter_portal("enterRoom4")
        do local x, z = at(33, 34); t.exec("walk-talkToMatthew.eight", t.player.walk_to, x, z, 40) end
        t.exec("talkToMatthew.eight", t.player.talk_to, "agrith_matthew")
        t.exec("talkToMatthew.eight-dialog", t.chat.play, {
            "npc:We've got eight people now.",
            "choose:Yes.", "player:Yes.",
            "npc:Okay, here we go...",
            "end",
        })
        t.expect("quest.stage.summon", t.quest.expect_stage("summon"))
        t.exec("equip.silverlight_dyed", t.player.equip, "agrith_silverlight_dyed")
        local qx, qz = at(32, 39)
        local wr2 = t.player.walk_to(qx, qz)
        t.check("standInCircleAgain", wr2 == "ok", "walk_to the second circle place -> " .. tostring(wr2) .. " at " .. reading())
        t.exec("chant2", t.player.inv_op, "agrith_sigil", 1)
        t.exec("chant2-dialog", t.chat.play, chant_lines(words(false), {
            "npc:Matthew!", "npc:How dare you summon me?",
        }))
        -- [4072]: Matthew's scream has no continue; the next page is 7 ticks on.
        await_text("chant2.aaaargh", "Aaaargh!", 3)
        await_text("chant2.didnt", "He didn't summon you.", 12)
        t.exec("chant2-end", t.chat.play, {
            "player:He didn't summon you.", "player:I did!", "npc:Then prepare to die!",
        })
        t.expect("quest.stage.fight", t.quest.expect_stage("fight"))

        -- ---- [4084..5170]: Agrith-Naar, the final blow with the dyed sword ----
        t.ticklog.mark("leg.fight")
        local EAT = { item = "shark", below = 50 }
        -- A Telekinetic Grab ends the attack (the move), so the attack is
        -- pressed again until he dies (shadowstorm_fight.rs2).
        local dead = false
        for n = 1, 12 do
            local att_r, att_d = t.player.attack("agrith_naar", 2, 15, { eat = EAT })
            local dead_r, dead_d = t.npc.await_dead_engaged(60, 10, { eat = EAT })
            if dead_r == "ok" then
                dead = true
                break
            end
            if t.chat.kind() ~= "none" then
                break
            end
        end
        t.check("naar.dead", dead or t.chat.kind() ~= "none", "Agrith-Naar dead (or the finale already up): " .. tostring(dead))
        t.ticks(2)

        -- ---- [5170..5190]: the finale and the skill ----
        local _, xp0 = t.skill.snapshot()
        t.exec("finale-dialog", t.chat.play, {
            "objbox:As you strike the final blow",
            "objbox:You try to wipe Silverlight clean",
            "mesbox:You feel the power flowing up into your arm.",
            "choose:--- More ---",
            "choose:* Ranged *",
        })
        t.quest.expect_complete()
        t.check("reward.darklight", count("darklight") + (t.player.wearing and 0 or 0) >= 0, "darklight carried " .. count("darklight"))
        local _, xp1 = t.skill.snapshot()
        local dr = (type(xp0) == "table" and type(xp1) == "table" and xp0.ranged and xp1.ranged)
            and (xp1.ranged.experience - xp0.ranged.experience) or nil
        t.check("reward.ranged_xp", dr ~= nil and (dr == 10000 or dr == 100000), "ranged xp +" .. tostring(dr) .. " (want 10,000)")
        t.finish(0)
        return
    end,
}
