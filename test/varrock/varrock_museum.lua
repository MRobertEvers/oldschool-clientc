-- varrock_museum: the museum's first and second floors as RSProx index 4649
-- does them (docs/rsprox-analysis/varrock_diary_and_museum.md): the kudos
-- counter on entering, Historian Minas told eleven quests in one talk (the
-- bits, 83 kudos, three lamps), a lamp rubbed, Torrcs and Marfet, every one of
-- the 34 timeline displays studied, the wall map read, floors paged and taken.
--
--   TORIRSSERVER_DEV=1 TORIRSSERVER_SCRIPTS=<pack> TORIRSSERVER_PACK_DIR=<servpack> \
--   ./src/build_opt/torirsserver --scriptrun test/varrock/varrock_museum.lua --bots 1 \
--       --name vmuseum0 --ticks 6000 --session /tmp/vmuseum --fixture test/raids/fixtures/fresh_lumbridge.ini

local MINAS_PAGES = {
    "npc:That's grand! We're always glad to have",
    "player:Well, I was asked to retrieve the Staff",
    "player:I saw the staff and can describe it to y",
    "npc:Yes! Please do!",
    "mesbox:You describe the staff in detail while t",
    "npc:That's excellent, I'll get a display put",
    "player:Well, I infiltrated one of the gangs her",
    "npc:We have gangs?",
    "player:Erm, yes... Anyway, I managed to get hal",
    "npc:Only half?",
    "player:Yes. The gangs had a bit of a tiff, you",
    "npc:Yes, yes, we were most pleased to have t",
    "npc:Oh, and before I forget, we found this o",
    "player:Oh, random bits of tat, thank you very m",
    "npc:You're welcome!",
    "mesbox:You receive an old lamp.",
    "player:I helped to rebuild the Observatory near",
    "npc:That would help immensely. Restored it,",
    "player:I helped to free the wizard, Merlin, by",
    "npc:Is that some kind of spell?",
    "player:No, it's a sword. King Arthur's sword.",
    "npc:What kingdom does he rule then?",
    "player:Britain...",
    "npc:Where on Gielinor is that?",
    "player:It's not! That's what I've been trying t",
    "npc:That's amazing! So he's really from anot",
    "npc:Oh, and before I forget, we found this o",
    "player:Oh, random bits of tat, thank you very m",
    "npc:You're welcome!",
    "mesbox:You receive an old lamp.",
    "player:Well, I recently defeated Delrith, a dem",
    "npc:Delrith! Incredible! I am grateful to yo",
    "player:I helped to save the gnomes' Grand Tree!",
    "npc:Oh? You mean those little green fellows",
    "player:Yes. I uncovered this nasty character ca",
    "npc:A black demon! How did you survive!?",
    "player:Oh, I was fine. After the demon it was d",
    "npc:You mean that those flimsy things can fl",
    "player:I found out more about King Lathas's anc",
    "npc:Oh? I'd be interested to hear this, we h",
    "player:Indeed, it turns out that there was a bi",
    "npc:Ahh, is this relating to that small muse",
    "player:Yes!",
    "npc:Ahh yes, we've been meaning to make a di",
    "npc:Oh, and before I forget, we found this o",
    "player:Oh, random bits of tat, thank you very m",
    "npc:You're welcome!",
    "mesbox:You receive an old lamp.",
    "player:I discovered some disturbing yet heroic",
    "npc:Oh?",
    "player:Yes, the vampyres once tried to invade M",
    "npc:Quite worrying that it's just a bit of w",
    "player:I found out more about Ivandis Seergaze,",
    "npc:What is it?",
    "player:It seems that he wasn't buried at Paterd",
    "npc:How fascinating. I'll have the display u",
    "player:I discovered the truth behind the Carnil",
    "npc:Oh? Is that the family over near Ardougn",
    "player:Yes. I got a bit involved in family busi",
    "npc:And?",
    "player:I found a secret cult in Ardougne and fo",
    "mesbox:The historian pulls out his writing impl",
    "npc:How exciting, I'll get a display put up",
    "player:I helped the Order of Wizards rediscover",
    "npc:Ah, I heard about that. Tell me more.",
    "player:Hmm. I can't tell you much, but I do kno",
    "npc:Sounds like magic to me! I'll get a disp",
}
local DISPLAYS = {
    { "vm_timeline_map_of_runescape", 3263, 3453, 1, "16" },
    { "vm_timeline_staff_of_armadyll", 3261, 3453, 1, "28" },
    { "vm_timeline_three_god_symbols", 3259, 3453, 1, "31" },
    { "vm_timeline_terracotta_statue", 3257, 3453, 1, "30" },
    { "vm_timeline_random_stuff", 3255, 3453, 1, "19" },
    { "vm_timeline_robert_the_strong", 3253, 3450, 1, "20" },
    { "vm_timeline_druid_stone_circle", 3255, 3450, 1, "8" },
    { "vm_timeline_star_chart", 3257, 3450, 1, "29" },
    { "vm_timeline_world_map", 3259, 3450, 1, "34" },
    { "vm_timeline_shield_of_arrav", 3261, 3450, 1, "24" },
    { "vm_timeline_seven_kights_helms", 3263, 3450, 1, "23" },
    { "vm_timeline_map_of_morytania", 3267, 3447, 1, "15" },
    { "vm_timeline_bridge_over_water_model", 3265, 3447, 1, "6" },
    { "vm_timeline_runestone_rock_model", 3263, 3447, 1, "21" },
    { "vm_timeline_broken_arrow", 3261, 3447, 1, "7" },
    { "vm_timeline_black_knight_armour", 3259, 3447, 1, "4" },
    { "vm_timeline_infinity_armour", 3257, 3447, 1, "11" },
    { "vm_timeline_observatory_model", 3255, 3444, 1, "17" },
    { "vm_timeline_silverlight", 3257, 3444, 1, "25" },
    { "vm_timeline_werewolf_skin", 3259, 3444, 1, "32" },
    { "vm_timeline_barbarian_weapons", 3261, 3444, 1, "2" },
    { "vm_timeline_bow_and_sword_surrounding_runes", 3263, 3444, 1, "5" },
    { "vm_timeline_barbarian_village", 3265, 3444, 1, "1" },
    { "vm_timeline_mage_training_guardian", 3257, 3444, 2, "12" },
    { "vm_timeline_map_of_edgeville", 3261, 3444, 2, "14" },
    { "vm_timeline_white_knights_armour", 3265, 3444, 2, "33" },
    { "vm_timeline_skeleton", 3265, 3447, 2, "26" },
    { "vm_timeline_phoenix_crossbow_and_black_arm_bands", 3265, 3450, 2, "18" },
    { "vm_timeline_map_of_ardougne", 3265, 3453, 2, "47" },
    { "vm_timeline_excalibur", 3261, 3453, 2, "9" },
    { "surok_timeline_tomb", 3257, 3450, 2, "3" },
    { "vm_timeline_zamorakian_symbol", 3257, 3447, 2, "35" },
    { "vm_timeline_gnome_glider", 3260, 3447, 2, "10" },
    { "vm_timeline_king_painting", 3256, 3455, 2, "46" },
}

local function go(t, x, z, level)
    t.cheat(string.format("::goto %d %d %d", x, z, level or 0))
    t.ticks(2)
end

return {
    id = "varrock_museum",
    fixture = "fresh_lumbridge.ini",
    max_frames = 300000,
    setup = {
        "::complete quest_templeofikov", "::complete quest_shieldofarrav", "::complete quest_observatory",
        "::complete quest_merlinscrystal", "::complete quest_demonslayer", "::complete quest_grandtree",
        "::complete quest_makinghistory", "::complete quest_priestinperil", "::complete quest_inaidofthemyreque",
        "::complete quest_hazeelcult", "::complete quest_runemysteries",
        "::setvar varb3637_vm_kudos 28",
        "::setlevel agility 42",
        "::clearinv",
    },
    run = function(t)
        t.ticks(3)
        t.ticklog.start()

        -- The counter (4649 [1203..1204]): walked in from the west door.
        go(t, 3251, 3447)
        t.ticklog.mark("museum.enter")
        t.player.walk_to(3256, 3449, 10)
        t.ticks(2)
        t.check("kudos.overlay", t.ui.await_open("vm_kudos", 4))

        -- Historian Minas (4649 [1570..1686]).
        go(t, 3265, 3455, 1)
        t.ticklog.mark("minas")
        t.exec("minas.talk", t.player.talk_to, "vm_timeline_historian", 1)
        local pages = { "player:Hello!", "npc:Hello there, welcome to Varrock Museum!",
            "choose:I have some information that might be of use in your displays.",
            "player:I have some information that might be of use" }
        for _, p in ipairs(MINAS_PAGES) do pages[#pages + 1] = p end
        t.exec("minas.dialog", t.chat.play, pages)
        t.ticks(2)
        local _, kudos = t.var.server("varb3637_vm_kudos")
        t.check("minas.kudos", kudos == 83, "vm_kudos = " .. tostring(kudos))
        for _, v in ipairs({ "varb3650_vm_staff_of_armadyl", "varb3656_vm_soa", "varb3652_vm_observatory",
            "varb3655_vm_excalibur", "varb3653_vm_silverlight", "varb3658_vm_glider", "varb3654_vm_lathas",
            "varb3659_vm_ardy", "varb3661_vm_runes" }) do
            local _, val = t.var.server(v)
            t.check("minas." .. v, val == 1, v .. " = " .. tostring(val))
        end
        local _, salve = t.var.server("varb3651_vm_salveknights")
        t.check("minas.salve", salve == 2, "vm_salveknights = " .. tostring(salve))
        for _, lamp in ipairs({ "vm_reward_arrav", "vm_reward_merlin", "vm_reward_history" }) do
            t.exec("minas." .. lamp, t.inv.await, lamp, 1, 3)
        end

        -- A lamp (4649 [1690..1702]): 1,000 Agility.
        t.ticklog.mark("lamp")
        t.exec("lamp.rub", t.player.inv_op, "vm_reward_history", 1)
        t.exec("lamp.open", t.ui.await_open, "xpreward", 10)
        t.exec("lamp.agility", t.ui.press_pause, "xpreward:universe", 7, "xpreward:confirm")
        t.ticks(3)
        t.check("lamp.box", t.msg.expect("<col=0000ff>Your wish has been granted!</col> You have been awarded 1,000 Agility experience!"))
        t.chat.close()

        -- Torrcs and Marfet (4649 [1266..1282], [1359..1368]).
        -- 4649 [1357]: Marfet spawns at 3266,3442 on the first floor and walks.
        go(t, 3264, 3443, 1)
        t.ticklog.mark("marfet")
        t.exec("marfet.talk", t.player.talk_to, "vm_advent_02", 1)
        t.exec("marfet.dialog", t.chat.play, { "player:Hello.", "npc:Hi there. So much to see here!",
            "player:Yep, could be looking all day!", "player:Updated any displays yet?",
            "npc:Yes, I just learned more", "player:Sounds fun, how did you do that?",
            "npc:Ahh, I'm big on quests me", "npc:and sometimes it earns me", "player:That sounds really good.",
            "npc:You should, as well as everything", "player:I'll check it out.", "npc:Have a good day!" })

        -- Every display (4649 [1224..1453]).
        for _, d in ipairs(DISPLAYS) do
            local loc, x, z, level, num = d[1], d[2], d[3], d[4], d[5]
            go(t, x, z - 1, level)
            t.ticklog.mark("display." .. loc)
            local r = t.player.click_loc(loc, 1, { at = { x, z } })
            local o = t.ui.await_open("vm_timeline", 8)
            t.check("display." .. loc, t.ui.expect_text("vm_timeline:display_num", tostring(num), 2))
            t.ui.close_modal()
            t.ticks(1)
        end
        -- The told displays show their text ([1748], [1752]).
        go(t, 3261, 3452, 1)
        t.player.click_loc("vm_timeline_staff_of_armadyll", 1, { at = { 3261, 3453 } })
        t.ui.await_open("vm_timeline", 8)
        t.check("display.staff.told", t.ui.expect_text("vm_timeline:vm_timeline_text", "This is a replica of the Staff of Armadyl", 2))
        t.ui.close_modal()
        t.ticks(1)

        -- The wall map: Look-at, the floors, Take, Read (4649 [1454..1734]).
        go(t, 3253, 3452, 1)
        t.ticklog.mark("map")
        t.player.click_loc("vm_museum_wallmounted_map_02", 1, { at = { 3253, 3453 } })
        t.exec("map.open", t.ui.await_open, "vm_museum_map", 8)
        t.check("map.empty.robert", t.ui.expect_text("vm_museum_map:vm_robert_the_strong", "Empty", 2))
        local _, up = t.ui.widget("vm_museum_map:lvl0_level_up")
        t.check("map.up", t.ui.invoke(up, 1))
        t.ticks(1)
        local _, close = t.ui.widget("vm_museum_map:vm_map_close")
        t.check("map.close", t.ui.invoke(close, 1))
        t.ticks(1)
        t.check("map.overlay.back", t.ui.await_open("vm_kudos", 3))
        t.player.click_loc("vm_museum_wallmounted_map_02", 2, { at = { 3253, 3453 } })
        t.exec("map.take", t.inv.await, "vm_museum_map", 1, 5)
        t.chat.close()
        t.exec("map.read", t.player.inv_op, "vm_museum_map", 1)
        t.exec("map.read.open", t.ui.await_open, "vm_museum_map", 5)
        t.ui.close_modal()
        t.ticklog.mark("end")
        t.expect("varrock_museum.run", "ok", "done")
    end,
}
