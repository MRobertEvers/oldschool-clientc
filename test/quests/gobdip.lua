return {
    id = "gobdip",
    fixture = "fresh_lumbridge.ini",
    setup = { "::clearinv", "::give bluedye 1", "::give orangedye 1" },
    run = function(t)
        local br, bd = t.quest.bind({
            varp = "varb2378_gobdip_main",
            constants = { not_started = 0, waiting_orange = 3, waiting_blue = 4, waiting_brown = 5, complete = 6 },
            row = "quest_goblindiplomacy",
            display = "Goblin Diplomacy",
            points = 5,
        })
        t.step("quest.bind", br == "ok" and "PASS" or "FAIL", bd)
        t.ticks(3)
        t.expect("start.not_started", t.quest.expect_stage("not_started"))
        local snap_r, snap = t.skill.snapshot()
        local r, d, res, det

        -- Goblin Village is open ground from Lumbridge (reach.py 3206,3233 ->
        -- 2957,3507: REACH closed-doors len=563): the first goto lands on the
        -- open yard south of the generals' hall. The hall is entered and left
        -- by its double door (goblin_outpost_poordoor_double_inner 2957,3509,
        -- north wall; doubledoors.rs2 ~open_double_door_left) on every visit.
        local function hall_in(name)
            t.exec(name, t.player.pass_door, { closed = "goblin_outpost_poordoor_double_inner",
                open = "goblin_outpost_openpoordoor_double_inner", at = { 2957, 3509, 0 },
                near = { 2957, 3509 }, far = { 2957, 3510 } })
        end
        local function hall_out(name)
            t.exec(name, t.player.pass_door, { closed = "goblin_outpost_poordoor_double_inner",
                open = "goblin_outpost_openpoordoor_double_inner", at = { 2957, 3509, 0 },
                near = { 2957, 3510 }, far = { 2957, 3508 } })
        end
        -- The crate2 hut's door: goblin_outpost_poordoor 2954,3505 (west wall
        -- of that tile; the hut is 2952-2953 x 3505-3508).
        local function hut_door(name, near, far)
            t.exec(name, t.player.pass_door, { closed = "goblin_outpost_poordoor",
                open = "goblin_outpost_openpoordoor", at = { 2954, 3505, 0 }, near = near, far = far })
        end
        t.exec("goto-village", t.player.goto_tile, 2957, 3507, 0)
        hall_in("g1.hallDoorIn")
        t.ticks(2)
        -- stage 0: every menu branch, decline, then accept
        t.exec("s0a.talk", t.player.talk_to, "general_bentnoze_red", 1)
        r, d = t.chat.drain({ stop_at = "options" })
        t.expect("s0a.opener", r, d)
        t.exec("why", t.chat.choose, "Why are you arguing about the colour of your armour?")
        r, d = t.chat.drain({ stop_at = "options" })
        t.expect("why.next", r, d)
        t.exec("peace", t.chat.choose, "Wouldn't you prefer peace?")
        r, d = t.chat.drain({ stop_at = "options" })
        t.expect("peace.next", r, d)
        t.exec("pick1", t.chat.choose, "Do you want me to pick an armour colour for you?")
        r, d = t.chat.drain({ stop_at = "options" })
        t.expect("pick1.next", r, d)
        t.exec("red", t.chat.choose, "You should wear red.")
        r, d = t.chat.drain({ stop_at = "options" })
        t.expect("red.next", r, d)
        t.exec("pick2", t.chat.choose, "Do you want me to pick an armour colour for you?")
        r, d = t.chat.drain({ stop_at = "options" })
        t.expect("pick2.next", r, d)
        t.exec("green", t.chat.choose, "You should wear green.")
        r, d = t.chat.drain({ stop_at = "options" })
        t.expect("green.next", r, d)
        t.exec("pick3", t.chat.choose, "Do you want me to pick an armour colour for you?")
        r, d = t.chat.drain({ stop_at = "options" })
        t.expect("pick3.next", r, d)
        t.exec("diff", t.chat.choose, "What about a different colour?")
        r, d = t.chat.drain({ stop_at = "options" })
        t.expect("diff.next", r, d)
        t.exec("decline", t.chat.choose, "No.")
        r, d = t.chat.drain({ stop_at = "none" })
        t.expect("decline.close", r, d)
        t.expect("decline.stage0", t.quest.expect_stage("not_started"))
        t.exec("s0b.talk", t.player.talk_to, "general_bentnoze_red", 1)
        r, d = t.chat.drain({ stop_at = "options" })
        t.expect("s0b.opener", r, d)
        t.exec("pick4", t.chat.choose, "Do you want me to pick an armour colour for you?")
        r, d = t.chat.drain({ stop_at = "options" })
        t.expect("pick4.next", r, d)
        t.exec("diff2", t.chat.choose, "What about a different colour?")
        r, d = t.chat.drain({ stop_at = "options" })
        t.expect("diff2.next", r, d)
        t.exec("accept", t.chat.choose, "Yes.")
        r, d = t.chat.drain({ stop_at = "options" })
        t.expect("accept.help", r, d)
        t.exec("help.armour", t.chat.choose, "Where do I get goblin armour?")
        r, d = t.chat.drain({ stop_at = "options" })
        t.expect("help.armour.next", r, d)
        t.exec("help.dye", t.chat.choose, "Where do I get dye?")
        r, d = t.chat.drain({ stop_at = "options" })
        t.expect("help.dye.next", r, d)
        t.exec("help.leave", t.chat.choose, "Okay, I'll be back soon.")
        r, d = t.chat.drain({ stop_at = "none" })
        t.expect("help.close", r, d)
        t.expect("stage.waiting_orange", t.quest.expect_stage("waiting_orange"))
        for i = 1, 2 do
            res, det = t.player.talk_to("catwalk_goblin", 1)
            t.step("grub.default.talk.try" .. i, res == "ok" and "PASS" or "FAIL", tostring(det):sub(1, 200))
            if res == "ok" then break end
            t.ticks(2)
        end
        t.step("grub.default.talk", res == "ok" and "PASS" or "FAIL", tostring(det))
        t.exec("grub.default.chat", t.chat.play, {
            "npc:Grubfoot wear red armour! Grubfoot wear green armour!",
            "npc:Why they not make up their minds?",
            "npc:Shut up Grubfoot!",
        })
        -- nothing to show yet
        t.exec("noarmour.talk", t.player.talk_to, "general_bentnoze_red", 1)
        t.exec("noarmour.chat", t.chat.play, {
            "npc:Have you got some orange goblin armour yet?",
            "player:Err no.",
            "npc:Come back when you have some.",
        })
        -- the crates
        -- crate1 (2959,3514) stands OUTSIDE the hall, north of its wall:
        -- out by the double door, then round the east side on foot
        -- (reach.py 2957,3508 -> 2959,3515: REACH closed-doors len=15).
        hall_out("crate1.hallDoorOut")
        t.exec("crate1.walk", t.player.walk_to, 2959, 3515)
        t.exec("crate1", t.player.click_loc, "goblin_outpost_large_crate_armour1", 1)
        t.check("crate1.mail", t.inv.await("goblin_armour", 1, 10))
        t.exec("crate1.dismiss", t.chat.play, { "mesbox:You find some goblin mail" })
        -- crate2 (2951,3508) is in the west hut: walk to its door, in, search, out.
        t.exec("crate2.walk", t.player.walk_to, 2955, 3505)
        hut_door("crate2.hutDoorIn", { 2955, 3505 }, { 2953, 3505 })
        t.exec("crate2", t.player.click_loc, "goblin_outpost_large_crate_armour2", 1)
        t.check("crate2.mail", t.inv.await("goblin_armour", 2, 10))
        t.exec("crate2.dismiss", t.chat.play, { "mesbox:You find some goblin mail" })
        hut_door("crate2.hutDoorOut", { 2953, 3505 }, { 2955, 3505 })
        -- crate3 (2955,3498, level 2) is on the platform the two-plane ladder
        -- goblin_ladder_bottom 2954,3497 climbs to (gobdip_crates.rs2:61
        -- ~climb_ladder_to(movecoord(coord, 0, 2, 0)): the player's own tile,
        -- two planes up). Stand on the open tile west of the ladder (the
        -- ladder's tile is solid) and climb from there both ways.
        t.exec("crate3.walk", t.player.walk_to, 2953, 3497)
        t.exec("crate3.ladderUp", t.player.climb, { loc = "goblin_ladder_bottom", op = 1, op_name = "Climb-up",
            at = { 2954, 3497, 0 }, src = { 2953, 3497 }, dest = { 2953, 3497, 2 } })
        t.exec("crate3", t.player.click_loc, "goblin_outpost_large_crate_armour3", 1)
        t.check("crate3.mail", t.inv.await("goblin_armour", 3, 10))
        t.exec("crate3.dismiss", t.chat.play, { "mesbox:You find some goblin mail" })
        t.exec("crate3.ladderDown", t.player.climb, { loc = "goblin_ladder_top", op = 1, op_name = "Climb-down",
            at = { 2954, 3497, 2 }, src = { 2953, 3497 }, dest = { 2953, 3497, 0 } })
        -- dye
        t.exec("dye.orange", t.player.use_item_on_item, "goblin_armour", "orangedye")
        t.check("dye.orange.have", t.inv.await("goblin_armour_orange", 1, 6))
        t.exec("dye.blue", t.player.use_item_on_item, "goblin_armour", "bluedye")
        t.check("dye.blue.have", t.inv.await("goblin_armour_darkblue", 1, 6))
        local _, brown_n = t.inv.count("goblin_armour")
        t.check("dye.brown.left", brown_n == 1, "goblin_armour left " .. tostring(brown_n) .. " (want 1)")
        -- back to the generals
        -- (reach.py 2953,3497 -> 2957,3508: REACH closed-doors len=19)
        t.exec("g2.walk", t.player.walk_to, 2957, 3508)
        hall_in("g2.hallDoorIn")
        t.ticks(2)
        -- wrong colour rebuke (blue while orange wanted)
        t.exec("rebuke.use", t.player.use_on, "goblin_armour_darkblue", t.player.by_symbol("npc", "general_bentnoze_red"))
        t.exec("rebuke.chat", t.chat.play, {
            "player:What do you think of this colour?",
            "npc:That wrong colour.",
            "npc:We tell you get orange armour.",
        })
        t.expect("rebuke.still_orange", t.quest.expect_stage("waiting_orange"))
        -- fit orange by use-on
        t.exec("orange.use", t.player.use_on, "goblin_armour_orange", t.player.by_symbol("npc", "general_bentnoze_red"))
        t.exec("orange.chat", t.chat.play, {
            "player:I have some orange armour here.",
            "mesbox:You give some goblin armour to the goblins.",
            "npc:Grubfoot!",
            "npc:Yes General Wartface?",
            "npc:Put on this armour!",
            "npc:What do you think?",
            "npc:No I don't like that much.",
            "npc:It clashes with skin colour.",
            "npc:We need darker colour, like blue.",
            "npc:Yeah blue might be good.",
            "npc:Human! Get us blue armour!",
        })
        t.expect("stage.waiting_blue", t.quest.expect_stage("waiting_blue"))
        local _, orange_n = t.inv.count("goblin_armour_orange")
        t.check("orange.consumed", orange_n == 0, "goblin_armour_orange left " .. tostring(orange_n) .. " (want 0)")
        -- Grubfoot in orange
        t.expect("grub.vis.orange", t.var.expect("varb13594_gobdip_grubfoot_vis", 1))
        for i = 1, 2 do
            res, det = t.player.talk_to("catwalk_goblin", 1)
            t.step("grub.orange.talk.try" .. i, res == "ok" and "PASS" or "FAIL", tostring(det):sub(1, 200))
            if res == "ok" then break end
            t.ticks(2)
        end
        t.step("grub.orange.talk", res == "ok" and "PASS" or "FAIL", tostring(det))
        t.exec("grub.orange.chat", t.chat.play, {
            "npc:Me not like this orange armour",
            "player:Look like what thing?",
            "npc:That fruit thing",
            "player:An orange?",
            "npc:That right. This armour make me look same colour",
            "npc:Shut up Grubfoot!",
        })
        -- fit blue by Talk-to
        t.exec("blue.talk", t.player.talk_to, "general_bentnoze_red", 1)
        t.exec("blue.chat", t.chat.play, {
            "player:I have some blue armour here.",
            "mesbox:You give some goblin armour to the goblins.",
            "npc:Grubfoot!",
            "npc:Yes General Wartface?",
            "npc:Put on this armour!",
            "npc:What do you think?",
            "npc:That not right. Not goblin colour at all.",
            "npc:Goblins wear dark earthy colours like brown.",
            "npc:Yeah brown might be good.",
            "npc:Human! Get us brown armour!",
            "player:But I thought brown was the armour",
            "player:Never mind, anything is worth a try.",
        })
        t.expect("stage.waiting_brown", t.quest.expect_stage("waiting_brown"))
        t.expect("grub.vis.blue", t.var.expect("varb13594_gobdip_grubfoot_vis", 2))
        for i = 1, 2 do
            res, det = t.player.talk_to("catwalk_goblin", 1)
            t.step("grub.blue.talk.try" .. i, res == "ok" and "PASS" or "FAIL", tostring(det):sub(1, 200))
            if res == "ok" then break end
            t.ticks(2)
        end
        t.step("grub.blue.talk", res == "ok" and "PASS" or "FAIL", tostring(det))
        t.exec("grub.blue.chat", t.chat.play, {
            "npc:Me not like this blue colour.",
            "player:Why not?",
            "npc:Me not know. It just make me feel",
            "player:Makes you feel blue?",
            "npc:Makes me feel kind of sad.",
            "npc:Shut up Grubfoot!",
        })
        local gold_before_r, gold_before = t.inv.count("gold_bar")
        t.exec("brown.use", t.player.use_on, "goblin_armour", t.player.by_symbol("npc", "general_bentnoze_red"))
        t.exec("brown.chat", t.chat.play, {
            "player:I have some brown armour here.",
            "mesbox:You give some goblin armour to the goblins.",
            "npc:Grubfoot!",
            "npc:Yes General Wartface?",
            "npc:Put on this armour!",
            "npc:What do you think?",
            "npc:That colour quite nice.",
            "npc:It a deal then. Brown armour it is.",
            "npc:Thank you for sorting out argument, human.",
            "player:Thanks... I think...",
        })
        t.ticks(3)
        t.quest.expect_complete()
        t.exec("reward.crafting", t.skill.expect_gain, "crafting", 200, snap)
        local _, gold_after = t.inv.count("gold_bar")
        t.check("reward.gold", gold_after == (gold_before or 0) + 1, "gold_bar " .. tostring(gold_before) .. " -> " .. tostring(gold_after))
        t.exec("post.talk", t.player.talk_to, "general_bentnoze_red", 1)
        t.exec("post.chat", t.chat.play, {
            "npc:Now you've solved our argument",
            "npc:Yep, we bored now.",
        })
        t.finish(0)
    end,
}
