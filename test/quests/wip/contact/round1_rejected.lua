-- Contact! driven from the guide ladder (contact.notes.md, b53 parity scratch).
-- Prerequisites by ::complete; light source + tinderbox + gear are brought-along kit.
-- Maze note: travel to the south-west ladder uses goto_tile (see goto-bossladder).
return {
    id = "contact",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::complete quest_gertrudescat",
        "::complete quest_princealirescue",
        "::complete quest_icthlarinslittlehelper",
        "::give tinderbox 1",
        "::give bullseye_lantern 1",
        "::give abyssal_whip 1",
        "::give lobster 10",
        "::setlevel hitpoints 99",
        "::setlevel agility 99",
        "::setlevel thieving 99",
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99",
        "::wield abyssal_whip",
    },
    run = function(t)
        t.quest.bind({
            varp = "varb3274_contact",
            constants = { not_started = 0, told_jex = 30, investigating = 40, read = 50, met_maisa = 60, told_osman = 70, osman_outside = 80, in_chasm = 90, scarab_killed = 110, ready = 120, complete = 130 },
            display = "Contact!",
            points = 1,
        })
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        t.exec("goto-highpriest", t.player.goto_tile, 3281, 2774, 0)
        t.exec("talkToHighPriest", t.player.talk_to, "ics_little_hipriest_vis", 1)
        t.exec("talkToHighPriest-dialog", t.chat.play, {
            "npc:Adventurer, welcome",
            "choose:/Sounds like a quest/",
            "player:Sounds like a quest for me",
            "npc:I tried to negotiate",
            "mesbox:disguises",
            "player:Is there any way into Menaphos from below",
            "npc:Sect of Scabaras",
        })
        t.ticks(2)
        t.expect("quest.stage.told_jex", t.quest.expect_stage("told_jex"))

        t.exec("goto-jex", t.player.goto_tile, 3312, 2797, 0)
        t.exec("talkToJex", t.player.talk_to, "contact_jex", 1)
        t.exec("talkToJex-dialog", t.chat.play, {
            "player:The High Priest sent me",
            "npc:this temple once belonged",
            "npc:bring a light source",
            "choose:Better get down there.",
            "player:Better get down there",
            "mesbox:Jex waves you",
        })
        t.ticks(2)
        t.expect("quest.stage.investigating", t.quest.expect_stage("investigating"))

        t.exec("goDownToBank", t.player.click_loc, "contact_temple_trapdoor_open", 1)
        t.exec("goDownToBank.continue", t.chat.continue_, true)
        t.ticks(6)
        local r, d = t.world.tile()
        t.check("bank.arrived", r, tostring(d and d.x) .. "," .. tostring(d and d.z) .. "," .. tostring(d and d.level))
        t.exec("goDownToDungeon", t.player.click_loc, "contact_ladder_barricaded", 1)
        t.ticks(8)
        r, d = t.world.tile()
        t.check("dungeon.arrived", r, tostring(d and d.x) .. "," .. tostring(d and d.z) .. "," .. tostring(d and d.level))
        t.exec("goto-bossladder", t.player.goto_tile, 2116, 4364, 2)
        t.exec("goDownToChasm", t.player.click_loc, "contact_ug_boss_ladder", 1)
        t.ticks(8)
        r, d = t.world.tile()
        t.check("chasm.arrived", r, tostring(d and d.x) .. "," .. tostring(d and d.z) .. "," .. tostring(d and d.level))

        t.exec("goto-body", t.player.goto_tile, 2284, 4313, 0)
        t.exec("searchKaleef", t.player.click_loc, "contact_dead_body_kaleef_vis", 1)
        t.chat.continue_(true)
        t.ticks(3)
        t.inv.await("contact_kaleef_scroll", 1, 10)
        t.check("parchment.in_inv", t.inv.count("contact_kaleef_scroll"))
        t.exec("readParchment", t.player.inv_op, "contact_kaleef_scroll", 1)
        t.chat.continue_(true)
        t.ticks(2)
        t.chat.continue_(true)
        t.ticks(2)
        t.expect("quest.stage.read", t.quest.expect_stage("read"))

        t.exec("goto-maisa", t.player.goto_tile, 2256, 4317, 0)
        t.exec("talkToMaisa", t.player.talk_to, "contact_maisa_multi", 1)
        t.exec("maisa.q1", t.chat.play, {
            "npc:Kaleef? No",
            "player:Kaleef is dead",
            "npc:prove you're no Menaphite spy",
            "choose:Draynor Village.",
            "player:Draynor Village",
            "npc:Correct",
        })
        t.ticks(2)
        t.exec("talkToMaisa2", t.player.talk_to, "contact_maisa_multi", 1)
        t.exec("maisa.q2", t.chat.play, {
            "choose:Leela.",
            "player:Leela",
            "npc:Correct",
            "npc:I need Osman",
        })
        t.ticks(3)
        t.expect("quest.stage.met_maisa", t.quest.expect_stage("met_maisa"))

        t.exec("goto-osman", t.player.goto_tile, 3287, 3177, 0)
        t.exec("talkToOsman", t.player.talk_to, "contact_osman_multi", 1)
        t.exec("talkToOsman-dialog", t.chat.play, {
            "player:I've just come from Maisa",
            "npc:Maisa?",
            "choose:/drive a wedge/",
            "player:It could drive a wedge",
            "npc:Now that is interesting",
        })
        t.ticks(2)
        t.expect("quest.stage.told_osman", t.quest.expect_stage("told_osman"))

        t.exec("goto-osman-desert", t.player.goto_tile, 3285, 2810, 0)
        t.exec("talkToOsmanOutsideSoph", t.player.talk_to, "contact_osman_desert_multi", 1)
        t.exec("talkToOsmanOutsideSoph-dialog", t.chat.play, {
            "npc:how do you propose",
            "choose:/secret entrance/",
            "player:I know of a secret entrance",
            "npc:Scabaras tunnels",
            "mesbox:Osman heads down",
        })
        t.ticks(2)
        t.expect("quest.stage.osman_outside", t.quest.expect_stage("osman_outside"))

        t.exec("goto-trapdoor2", t.player.goto_tile, 3315, 2799, 0)
        t.exec("goDownToBankAgain", t.player.click_loc, "contact_temple_trapdoor_open", 1)
        t.exec("goDownToBankAgain.continue", t.chat.continue_, true)
        t.ticks(6)
        t.exec("goDownToDungeonAgain", t.player.click_loc, "contact_ladder_barricaded", 1)
        t.ticks(8)
        t.exec("goto-bossladder2", t.player.goto_tile, 2116, 4364, 2)
        t.exec("goDownToChasmAgain", t.player.click_loc, "contact_ug_boss_ladder", 1)
        t.ticks(8)
        r, d = t.world.tile()
        t.check("instance.arrived", r, tostring(d and d.x) .. "," .. tostring(d and d.z) .. "," .. tostring(d and d.level))
        t.chat.continue_(true)
        t.ticks(2)
        t.expect("quest.stage.in_chasm", t.quest.expect_stage("in_chasm"))

        t.exec("goto-boss", t.player.goto_tile, 6430, 99, 0)
        local rb, db = t.npc.nearest("contact_scarab_boss", 40)
        t.check("scarab.present", rb, tostring(db and db.x) .. "," .. tostring(db and db.z))
        t.exec("killGiantScarab.attack", t.player.attack, "contact_scarab_boss", 2, 60)
        t.exec("killGiantScarab", t.npc.await_dead_engaged, 400, 3, { eat = { item = "lobster", below = 40 } })
        t.ticks(3)
        t.chat.continue_(true)
        t.ticks(3)
        t.expect("quest.stage.scarab_killed", t.quest.expect_stage("scarab_killed"))

        t.exec("pickUpKeris", t.player.click_obj, "contact_keris", 3)
        t.ticks(3)
        t.check("keris.in_inv", t.inv.count("contact_keris"))
        local ro, dopos = t.npc.nearest("contact_osman_cave_instance", 40)
        t.check("osman.cave_present", ro, tostring(dopos and dopos.x) .. "," .. tostring(dopos and dopos.z))
        t.exec("talkToOsmanChasm", t.player.talk_to, "contact_osman_cave_instance", 1)
        t.exec("talkToOsmanChasm-dialog", t.chat.drain, { max_pages = 12 })
        t.ticks(2)
        t.expect("quest.stage.ready", t.quest.expect_stage("ready"))

        t.exec("goto-priest", t.player.goto_tile, 3281, 2774, 0)
        local _s, snap = t.skill.snapshot()
        t.exec("returnToHighPriest", t.player.talk_to, "ics_little_hipriest_vis", 1)
        t.exec("returnToHighPriest-dialog", t.chat.drain, { max_pages = 12 })
        t.ticks(3)
        t.quest.expect_complete()
        local rl, nl = t.inv.count("contact_lantern")
        t.check("reward.lamp", rl == "ok" and nl == 1, "combat lamp count " .. tostring(nl))
        t.exec("lamp.rub", t.player.inv_op, "contact_lantern", 1)
        t.exec("lamp.choose1", t.chat.choose, "Strength")
        t.chat.continue_(true)
        t.ticks(3)
        t.check("reward.xp_strength", t.skill.expect_gain("strength", 7000, snap), "lamp wish 1: 7000 strength xp")
        t.exec("lamp.rub2", t.player.inv_op, "contact_lantern", 1)
        t.exec("lamp.choose2", t.chat.choose, "More...")
        t.exec("lamp.choose2b", t.chat.choose, "Magic")
        t.chat.continue_(true)
        t.ticks(3)
        t.check("reward.xp_magic", t.skill.expect_gain("magic", 7000, snap), "lamp wish 2: 7000 magic xp")
        local rc, nc = t.inv.count("contact_lantern")
        t.check("reward.lamp_consumed", rc == "ok" and nc == 0, "lamp count after two wishes " .. tostring(nc))
        t.finish(0)
    end,
}
