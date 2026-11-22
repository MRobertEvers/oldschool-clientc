-- varrock_easy_diary: every Varrock easy task, the recorded medium ones, the
-- journal, the tier's box, Toby's reward and the lamp, as RSProx index 4649
-- does them (docs/rsprox-analysis/varrock_diary_and_museum.md). The four easy
-- tasks 4649 had already done are done the way 4648, 208 and 4346 did them.
--
--   TORIRSSERVER_DEV=1 TORIRSSERVER_SCRIPTS=<pack> TORIRSSERVER_PACK_DIR=<servpack> \
--   ./src/build_opt/torirsserver --scriptrun test/varrock/varrock_easy_diary.lua --bots 1 \
--       --name vdiary0 --ticks 9000 --session /tmp/vdiary --fixture test/raids/fixtures/fresh_lumbridge.ini

local DIARY = "varp1176_varrock_achievement_diary"
local EASY_LINE = "<col=dc143c>Well done! You have completed an easy task in the Varrock area. Your Achievement Diary has been updated.</col>"
local MEDIUM_LINE = "<col=dc143c>Well done! You have completed a medium task in the Varrock area. Your Achievement Diary has been updated.</col>"

local function bit(t, n)
    local r, v = t.var.server(DIARY)
    if r ~= "ok" or type(v) ~= "number" then return false, tostring(r) .. " " .. tostring(v) end
    return (v // (1 << n)) % 2 == 1, string.format("%s = %d", DIARY, v)
end

-- Await the task's bit (the claim's own packet), then check the line.
local function claimed(t, name, n, line, ticks)
    local ok, detail = false, "never read"
    for _ = 1, (ticks or 30) do
        ok, detail = bit(t, n)
        if ok then break end
        t.ticks(1)
    end
    t.check("bit." .. name, ok, "bit " .. n .. ": " .. detail)
    t.check("line." .. name, t.msg.expect(line or EASY_LINE))
end

local function go(t, x, z, level)
    t.cheat(string.format("::goto %d %d %d", x, z, level or 0))
    t.ticks(2)
end

-- Open the Varrock journal (area_task row 8, op 1) and read rows.
local function journal(t, name, rows)
    local _, tab = t.ui.widget("side_journal:task_list")
    t.ui.invoke(tab, 1)
    t.ticks(2)
    local wr, row = t.ui.widget("area_task:taskbox", 8)
    t.check("journal." .. name .. ".row", wr == "ok", tostring(row))
    if wr ~= "ok" then return end
    t.check("journal." .. name .. ".press", t.ui.invoke(row, 1))
    t.exec("journal." .. name .. ".open", t.ui.await_open, "journalscroll", 10)
    t.check("journal." .. name .. ".title", t.ui.expect_text("journalscroll:title", "Achievement Diary - Varrock", 3))
    for n, want in pairs(rows) do
        t.check("journal." .. name .. ".qj" .. n, t.ui.expect_text("journalscroll:qj" .. n, want, 1))
    end
    t.ui.close_modal()
    t.ticks(1)
end

return {
    id = "varrock_easy_diary",
    fixture = "fresh_lumbridge.ini",
    max_frames = 400000,
    setup = {
        "::setlevel mining 20", "::setlevel woodcutting 20", "::setlevel fishing 30",
        "::setlevel crafting 20", "::setlevel runecraft 10", "::setlevel magic 30",
        "::setlevel thieving 10", "::setlevel agility 42",
        "::complete quest_runemysteries", "::complete quest_soulsbane",
        "::clearinv",
        "::give coins 40000", "::give bronze_pickaxe 1", "::give bronze_axe 1",
        "::give bat_bones 1", "::give red_spiders_eggs 1", "::give limpwurt_root 1",
        "::give lawrune 1", "::give airrune 3", "::give firerune 1",
    },
    run = function(t)
        t.ticks(3)
        t.ticklog.start()

        -- Easy 14, the tea stall (4649 [15..20]).
        go(t, 3268, 3411)
        t.ticklog.mark("easy.tea")
        t.exec("tea.steal", t.player.click_loc, "tea_stall", 2)
        claimed(t, "tea", 14)

        -- Easy 9, a bone for the stray dog (4649 [69..77]).
        t.ticklog.mark("easy.dog")
        t.exec("dog.bone", t.player.use_on, "bat_bones", t.player.by_symbol("npc", "dog_stray"))
        claimed(t, "dog", 9)
        t.check("dog.line", t.msg.expect("You give the dog some nice bat bones.<br>It happily gnaws on them."))

        -- Easy 1, Thessalia's store (4648 [274..275]).
        go(t, 3206, 3417)
        t.ticklog.mark("easy.thessalia")
        t.exec("thessalia.trade", t.player.talk_to, "thessalia", 3)
        claimed(t, "thessalia", 1)
        t.ui.close_modal()

        -- Easy 8, Benny's newspaper (4649 [544..553]).
        go(t, 3219, 3430)
        t.ticklog.mark("easy.newspaper")
        t.exec("benny.talk", t.player.talk_to, "qip_soa_newspaperseller", 1)
        t.exec("benny.dialog", t.chat.play, {
            "choose:Can I have a newspaper, please?", "player:Can I have a newspaper, please?",
            "npc:Certainly, guv. That'll be 50 coins, please.",
            "choose:Sure, here you go...", "player:Sure, here you go...",
            "objbox:Benny sells you a newspaper.",
        })
        claimed(t, "newspaper", 8)

        -- Medium 1, the Apothecary's strength potion (4649 [333..335]).
        go(t, 3196, 3403)
        t.ticklog.mark("medium.potion")
        t.exec("apothecary.potions", t.player.talk_to, "apothecary", 3)
        t.exec("apothecary.open", t.ui.await_open, "apothecary_potions", 10)
        local _, strength = t.ui.widget("apothecary_potions:strength")
        t.check("apothecary.buy", t.ui.invoke(strength, 1))
        claimed(t, "potion", 15, MEDIUM_LINE)
        t.exec("apothecary.potion", t.inv.await, "strength4", 1, 5)
        t.ui.close_modal()

        -- Medium 8, the Varrock teleport (4489 [182]).
        t.ticklog.mark("medium.teleport")
        t.exec("teleport.cast", t.player.teleport_cast, "varrock_teleport", { 3213, 3424, 0 },
            { name = "teleport", runes = { { "lawrune", 1 }, { "airrune", 3 }, { "firerune", 1 } }, radius = 3 })
        claimed(t, "teleport", 23, MEDIUM_LINE)

        -- Easy 2, Aubury's teleport (208 [855]), and back.
        go(t, 3253, 3402)
        t.ticklog.mark("easy.aubury")
        t.exec("aubury.teleport", t.player.talk_to, "aubury", 4)
        claimed(t, "aubury", 2)
        t.ticks(8)

        -- Easy 3, iron in the south-east mine (4648 [194..197]).
        go(t, 3286, 3370)
        t.ticklog.mark("easy.iron")
        for i = 1, 20 do
            if bit(t, 3) then break end
            t.player.click_loc("ironrock2", 1, { at = { 3286, 3369 } })
            t.ticks(8)
        end
        claimed(t, "iron", 3)

        -- Easy 6, the fence south of Varrock (4649 [993..1002]).
        go(t, 3239, 3335)
        t.ticklog.mark("easy.fence")
        t.exec("fence.jump", t.player.click_loc, "lumbridge_sc_fencejump", 1)
        claimed(t, "fence", 6)
        local _, landed = t.world.tile()
        t.check("fence.landing", landed.x == 3240 and landed.z == 3334, landed.x .. "," .. landed.z)

        -- Easy 4 and medium 10, the sawmill (4649 [479..487]): its own kit.
        t.cheat("::clearinv")
        t.cheat("::give coins 40000")
        t.cheat("::give logs 1")
        t.cheat("::give mahogany_logs 20")
        t.ticks(2)
        go(t, 3302, 3491)
        t.ticklog.mark("medium.mahogany")
        t.player.talk_to("poh_sawmill_opp", 3)
        t.exec("sawmill.menu", t.ui.await_open, "skillmulti", 10)
        t.exec("sawmill.twenty", t.ui.press_pause, "skillmulti:d", 20, "skillmulti:d")
        claimed(t, "mahogany", 25, MEDIUM_LINE)
        t.exec("sawmill.planks", t.inv.await, "plank_mahogany", 20, 5)
        t.ticklog.mark("easy.plank")
        t.player.talk_to("poh_sawmill_opp", 3)
        t.exec("sawmill.normal", t.ui.make, "a")
        claimed(t, "plank", 4)

        -- Easy 7, the dying tree in the Lumber Yard (4649 [498..502]).
        t.cheat("::clearinv")
        t.cheat("::give bronze_axe 1")
        t.cheat("::give fly_fishing_rod 1")
        t.cheat("::give feather 200")
        t.cheat("::give softclay 1")
        t.cheat("::give blankrune_high 5")
        t.ticks(2)
        go(t, 3308, 3494)
        t.ticklog.mark("easy.dyingtree")
        for i = 1, 20 do
            if bit(t, 7) then break end
            t.player.click_loc("fai_varrock_dead_tree", 1, { at = { 3308, 3495 } })
            t.ticks(6)
        end
        claimed(t, "dyingtree", 7)

        -- Medium 6, the Tolna dungeon (4649 [1110..1120]), and back up.
        go(t, 3309, 3452)
        t.ticklog.mark("medium.tolna")
        t.exec("tolna.rift", t.player.click_loc, "soulbane_falloff2_rope_multi", 1, { at = { 3310, 3452 } })
        claimed(t, "tolna", 21, MEDIUM_LINE)

        -- Easy 13, trout at Barbarian Village (4649 [826..842]).
        go(t, 3109, 3433)
        t.ticklog.mark("easy.trout")
        for i = 1, 30 do
            if bit(t, 13) then break end
            t.player.talk_to("0_48_53_freshfish", 1)
            t.ticks(10)
        end
        claimed(t, "trout", 13)

        -- Easy 10, the bowl: spun (the stage, [870]) then fired ([873..880]).
        go(t, 3086, 3409)
        t.ticklog.mark("easy.bowl")
        t.player.click_loc("potterywheel", 1)
        t.exec("bowl.menu", t.ui.make, "c")
        t.ticks(4)
        t.check("bowl.stage", t.msg.expect("<col=0040ff>Achievement Diary Stage Task - Current stage: 1.</col>"))
        t.exec("bowl.oven", t.player.use_on, "bowl_unfired", t.player.by_symbol("loc", "fai_barbarian_pottery_oven"))
        claimed(t, "bowl", 10, nil, 20)

        -- Easy 12, earth runes (4649 [1153..1154]).
        go(t, 2658, 4839)
        t.ticklog.mark("easy.earth")
        t.exec("earth.craft", t.player.click_loc, "earth_altar", 1)
        claimed(t, "earth", 12)

        -- Easy 5, the Stronghold's second level (4346 [418..427]).
        go(t, 1902, 5222)
        t.ticklog.mark("easy.stronghold")
        t.exec("sos.ladder", t.player.click_loc, "sos_war_ladd_down", 1)
        t.exec("sos.warning", t.ui.await_open, "cws_warning_4", 10)
        local _, warn = t.ui.widget("cws_warning_4:warn1")
        t.check("sos.yes", t.ui.invoke(warn, 1))
        claimed(t, "stronghold", 5)

        -- The journal before the last task (4649 [1166]): 13 of 14 struck.
        t.ticklog.mark("journal.before")
        journal(t, "before", {
            [3] = "Easy", [6] = "Browse Thessalia's store.", [18] = "Speak to Haig Halen after obtaining at least 50 Kudos.",
            [26] = "Have the Apothecary in Varrock make you a strength", [69] = "Essence without the use of Extracts.",
        })

        -- Easy 11, Haig Halen with 50 kudos (4649 [1794..1798]): the last.
        t.cheat("::setvar varb3637_vm_kudos 50")
        go(t, 3258, 3449)
        t.ticklog.mark("easy.haig")
        t.exec("haig.talk", t.player.talk_to, "curator", 1)
        t.exec("haig.welcome", t.chat.play, { "npc:Welcome to the museum of Varrock." })
        claimed(t, "haig", 11)
        t.ticks(3)
        t.check("tier.box", t.msg.expect("Congratulations! You have completed all of the easy tasks in the Varrock area. Speak to Toby in Varrock square to claim your reward."))
        local _, done = t.var.server("varb4479_varrock_diary_easy_complete")
        t.check("tier.flag", done == 1, "varrock_diary_easy_complete = " .. tostring(done))
        t.chat.close()

        -- Toby's reward (4649 [1814..1821]).
        go(t, 3224, 3415)
        t.ticklog.mark("toby.reward")
        t.exec("toby.talk", t.player.talk_to, "toby_varrock_diary", 1)
        t.exec("toby.dialog", t.chat.play, {
            "player:I've completed all of the easy tasks in my Varrock",
            "npc:I can see that, well done! You'll be wanting your",
            "player:Yes please!",
            "npc:This armour is a symbol of your exploration of",
            "npc:furnace. Zaff will stock 15 Battlestaves per day.",
            "player:Wow, thanks!",
            "npc:If you ever lose your armour, come back to me to",
        })
        t.check("toby.menu", t.chat.kind() == "options", tostring(t.chat.kind()))
        t.chat.close()
        t.exec("toby.armour", t.inv.await, "varrock_armour_easy", 1, 5)
        t.exec("toby.lamp", t.inv.await, "diary_easy_lamp", 1, 5)
        local _, reward = t.var.server("varb4519_varrock_easy_reward")
        t.check("toby.reward_bit", reward == 1, "varrock_easy_reward = " .. tostring(reward))

        -- The lamp (4649 [1825..1830]): 2,500 Agility.
        t.ticklog.mark("lamp")
        t.skill.snapshot()
        t.exec("lamp.rub", t.player.inv_op, "diary_easy_lamp", 1)
        t.exec("lamp.open", t.ui.await_open, "xpreward", 10)
        t.exec("lamp.agility", t.ui.press_pause, "xpreward:universe", 7, "xpreward:confirm")
        t.ticks(3)
        t.check("lamp.box", t.msg.expect("<col=0000ff>Your wish has been granted!</col> You have been awarded 2,500 Agility experience!"))
        journal(t, "after", {
            [22] = "If I ever lose my Varrock armour I can speak to Toby in", [23] = "Varrock square.",
            [25] = "Medium", [71] = "Essence without the use of Extracts.",
        })
        t.ticklog.mark("end")
        t.expect("varrock_easy_diary.run", "ok", "done")
    end,
}
