-- Daddy's Home (miniquest, 0 quest points), played the way RSProx index 4291
-- plays it: Marlo's favour menu in the recorded order, Yarlo, the seven
-- removals, the lecture skipped, the free hammer and saw, the waxwood
-- branch with its world-map box, the six builds, the crate, the sawmill's
-- Buy-plank, the bed, and Marlo's reward and crate. Every page asserted here
-- is a recorded page (docs/rsprox-analysis/quests/daddyshome.md).
--
-- The recorder owned a house in Taverley ("Is this necessary? I've got a house
-- of my own, in Taverley"), so this player does too: `varb2187_poh_house_location`
-- = 2 (^poh_loc_taverley) is the only thing Daddy's Home reads about it.
-- Daddy's Home ends with no quest scroll (4291 [742..745]: the reward box,
-- then miniquests_completed_count), so the end is graded on the status, the
-- rewards and the xp rather than quest.expect_complete's scroll rows.
return {
    id = "daddyshome",
    fixture = "fresh_lumbridge.ini",
    max_frames = 120000,
    setup = {
        "::clearinv",
        "::setvar varb2187_poh_house_location 2",
        -- Exactly what the builds take: ten planks, five bolts of cloth, fourteen
        -- steel nails (2 + 2 + 4 + 4 + 2; nails never bend here).
        "::give woodplank 10",
        "::give cloth 5",
        "::give nails 14",
        "::tele varrock",
    },

    run = function(t)
        t.quest.bind({
            varp = "varb10570_daddyshome_status",
            constants = {
                not_started = 0, find_yarlo = 1, removing = 2, removed = 3, lectured = 4,
                waxwood = 5, planks = 6, built = 10, paying = 11, complete = 13, crate_opened = 14,
            },
            row = "miniquest_daddyshome",
            display = "Daddy's Home",
            points = 0,
        })
        t.ticks(3)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))
        local _, before = t.skill.read("construction")

        local function menu_rows(name, want)
            local r, rows = t.chat.options()
            local got = (r == "ok" and type(rows) == "table") and table.concat(rows, "|") or tostring(rows)
            t.check(name, got == want, "rows '" .. got .. "' want '" .. want .. "'")
        end

        -- ---- Marlo (index 4291 [269..314]) ----
        t.exec("marlo.walk", t.player.goto_tile, 3240, 3473, 0)
        t.exec("marlo.talk", t.player.talk_to, "con_contractor_varrock", 1)
        t.exec("marlo.welcome", t.chat.play, { "npc:Welcome to the Varrock branch of Mahogany Homes", "options" })
        menu_rows("marlo.menu1", "Tell me about working for Mahogany Homes.|What kind of favour do you want me to do?|Maybe another time.")
        t.exec("marlo.company", t.chat.play, {
            "choose:Tell me about working for Mahogany Homes.",
            "player:Tell me about working for Mahogany Homes.",
            "npc:Just like you do in your house in Taverley.",
            "npc:When a client hires us",
            "npc:go and see Amy in our Falador office",
            "options",
        })
        menu_rows("marlo.menu2", "What kind of favour do you want me to do?|Maybe another time.")
        t.exec("marlo.favour", t.chat.play, {
            "choose:What kind of favour do you want me to do?",
            "player:What kind of favour do you want me to do?",
            "npc:it's about my dad",
            "options",
        })
        menu_rows("marlo.menu3", "Aren't you in a construction company yourself though?|Why has he got such a bad house?|Tell me more about the job.|I'd rather not.")
        t.exec("marlo.why", t.chat.play, {
            "choose:Why has he got such a bad house?",
            "player:Why has he got such a bad house?",
            "npc:Some kind of economic downturn",
            "options",
        })
        menu_rows("marlo.menu4", "Aren't you in a construction company yourself though?|Tell me more about the job.|I'd rather not.")
        t.exec("marlo.company_too", t.chat.play, {
            "choose:Aren't you in a construction company yourself though?",
            "player:Aren't you in a construction company",
            "npc:See, my dad just - um - found the house.",
            "npc:Basically, he's not really meant to be there.",
            "options",
        })
        menu_rows("marlo.menu5", "Why has he got such a bad house?|Tell me more about the job.|I'd rather not.")
        t.exec("marlo.job", t.chat.play, {
            "choose:Tell me more about the job.",
            "player:Tell me more about the job.",
            "npc:Nothing too hard.",
            "options",
        })
        menu_rows("marlo.menu6", "Aren't you in a construction company yourself though?|Why has he got such a bad house?|Tell me where he lives, and I'll do the job.|I'd rather not.")
        t.exec("marlo.accept", t.chat.play, {
            "choose:Tell me where he lives, and I'll do the job.",
            "player:Tell me where he lives, and I'll do the job.",
            "npc:Old Man Yarlo, they call him.",
            "end",
        })
        t.expect("quest.stage.find_yarlo", t.quest.expect_stage("find_yarlo"))

        -- ---- Yarlo (4291 [380..388]) ----
        t.exec("yarlo.walk", t.player.goto_tile, 3241, 3396, 0)
        t.exec("yarlo.talk", t.player.talk_to, "daddyshome_daddy", 1)
        t.exec("yarlo.first", t.chat.play, {
            "npc:What brings you here, young man?",
            "player:Marlo sent me to fix your furniture.",
            "npc:My boy Marlo sent you?",
            "npc:There are two stools, two tables, a chair and a rotten",
            "end",
        })
        t.expect("quest.stage.removing", t.quest.expect_stage("removing"))

        -- ---- The removals (4291 [390..438]) ----
        local pieces = {
            { "daddyshome_bed", 1, 3241, 3397, "varb10568_daddyshome_bed" },
            { "daddyshome_carpet_middle", 5, 3239, 3395, "varb10569_daddyshome_carpet" },
            { "daddyshome_stool_2", 1, 3239, 3394, "varb10564_daddyshome_stool_2" },
            { "daddyshome_table_2", 1, 3240, 3394, "varb10567_daddyshome_table_2" },
            { "daddyshome_chair", 1, 3241, 3393, "varb10565_daddyshome_chair" },
            { "daddyshome_stool_1", 1, 3244, 3394, "varb10563_daddyshome_stool_1" },
            { "daddyshome_table_1", 1, 3245, 3394, "varb10566_daddyshome_table_1" },
        }
        -- Remove only the bed first, and ask Yarlo: 1686 [1283..1284]'s list.
        t.exec("remove.bed", t.player.click_loc, "daddyshome_bed", 1, { at = { 3241, 3397 } })
        t.ticks(4)
        t.exec("yarlo.list.talk", t.player.talk_to, "daddyshome_daddy", 1)
        t.exec("yarlo.list", t.chat.play, {
            "npc:how's the redecoration coming on?",
            "player:I've been removing the old furniture.",
            "npc:You still need to remove:",
            "npc:The rotten carpet",
            "end",
        })
        for i = 2, #pieces do
            local p = pieces[i]
            t.exec("remove." .. p[1], t.player.click_loc, p[1], p[2], { at = { p[3], p[4] } })
            t.ticks(4)
        end
        for _, p in ipairs(pieces) do
            t.expect("removed." .. p[1], t.var.expect(p[5], 2))
        end

        -- ---- The lecture, skipped (4291 [444..480]) ----
        t.exec("yarlo.done.talk", t.player.talk_to, "daddyshome_daddy", 1)
        t.exec("yarlo.done", t.chat.play, {
            "npc:how's the redecoration coming on?",
            "player:I've finished removing all the old furniture.",
            "npc:Jolly good, young man.",
            "npc:I used to be a carpenter myself",
            "player:Is this necessary? I've got a house of my own, in Taverley",
            "options",
        })
        menu_rows("yarlo.lecture_menu", "Listen to Yarlo.|Skip Yarlo's lecture. He'll offer it later if you like.")
        t.exec("yarlo.skip", t.chat.play, {
            "choose:Skip Yarlo's lecture. He'll offer it later if you like.",
            "npc:You young people are always rushing around",
            "npc:For a start, I'd like two new stools, two new tables and a new chair.",
            "objbox:Some basic wooden planks and some nails should do",
            "objbox:I'll want a new carpet too.",
            "npc:Now, for the bed, I've got something special in mind.",
            "objbox:Those abandoned crates in the kitchen",
            "objbox:You'll have to take three logs from the crate to the sawmill.",
            "npc:I think that's everything",
            "end",
        })
        t.expect("quest.stage.waxwood", t.quest.expect_stage("waxwood"))

        -- ---- The help menu: waxwood, then the tools (4291 [483..536]) ----
        t.exec("yarlo.help.talk", t.player.talk_to, "daddyshome_daddy", 1)
        t.exec("yarlo.help", t.chat.play, {
            "player:I've been working on your furniture.",
            "npc:Do you need some help with the building work",
            "options",
        })
        menu_rows("yarlo.help_menu", "How do I build furniture?|Where do I get planks, nails and cloth?|I need some tools.|Tell me about the waxwood for the bed.|I'll get on with it.")
        t.exec("yarlo.waxwood", t.chat.play, {
            "choose:Tell me about the waxwood for the bed.",
            "player:Tell me about the waxwood for the bed.",
            "npc:This town's quite damp",
            "objbox:Those abandoned crates in the kitchen",
            "objbox:if you mention my name.",
            "objbox:Look on your World Map to find the sawmill.",
            "options",
        })
        menu_rows("yarlo.help_menu2", "How do I build furniture?|Where do I get planks, nails and cloth?|I need some tools.|I'll get on with it.")
        t.exec("yarlo.tools", t.chat.play, {
            "choose:I need some tools.",
            "player:I need some tools.",
            "npc:Would you like a hammer? I can spare one.",
            "choose:Yes please.",
            "player:Yes please.",
            "objbox:Yarlo gives you a hammer.",
            "npc:How about a saw, though?",
            "choose:Yes please.",
            "player:Yes please.",
            "objbox:Yarlo gives you a saw.",
            "options",
        })
        menu_rows("yarlo.help_menu3", "How do I build furniture?|Where do I get planks, nails and cloth?|Tell me about the waxwood for the bed.|I'll get on with it.")
        t.exec("yarlo.go", t.chat.play, { "choose:I'll get on with it.", "player:I'll get on with it.", "end" })
        t.expect("tools.hammer", t.inv.expect_has("hammer", 1))
        t.expect("tools.saw", t.inv.expect_has("poh_saw", 1))

        -- ---- The six builds (4291 [544..593]) ----
        local function build(p)
            t.exec("build." .. p[1], t.player.click_loc, p[1], p[2], { at = { p[3], p[4] } })
            t.check("build." .. p[1] .. ".panel", t.ui.await_open("poh_furniture_creation", 8) == "ok", "build panel open")
            local wr, w = t.ui.widget("poh_furniture_creation:01")
            t.check("build." .. p[1] .. ".row", wr == "ok", "row 01 -> " .. tostring(wr))
            t.exec("build." .. p[1] .. ".pick", t.ui.invoke, w, 1)
            t.ticks(7)
            t.expect("built." .. p[1], t.var.expect(p[5], 3))
        end
        for i = 2, #pieces do
            build(pieces[i])
        end
        t.expect("build.nails_used", t.inv.expect_absent("nails"))
        t.expect("build.planks_used", t.inv.expect_absent("woodplank"))

        -- ---- The crate (4291 [604..607]) ----
        t.exec("crates.search", t.player.click_loc, "daddyshome_crates", 1, { at = { 3243, 3398 } })
        t.exec("crates.box", t.chat.play, { "objbox:The crate contains that water-repellent waxwood", "end" })
        t.expect("crates.logs", t.inv.expect_has("daddyshome_waxwood_logs", 3))

        -- ---- The sawmill's Buy-plank (4291 [623..631]) ----
        t.exec("sawmill.walk", t.player.goto_tile, 3302, 3490, 0)
        t.exec("sawmill.buy", t.player.talk_to, "poh_sawmill_opp", 3)
        t.check("sawmill.menu", t.ui.await_open("skillmulti", 10) == "ok", "skillmulti open")
        -- The 519x142 panel needs chatbox:chatmodal unclamped (varbit 10670, set
        -- with the open as Jagex does, 4291 [627]); clamped it loses its title.
        t.expect("sawmill.unclamped", t.var.expect("varb10670_chatmodal_unclamp", 1))
        local mr, cell = t.ui.widget("skillmulti:h")
        t.check("sawmill.waxwood_row", mr == "ok", "skillmulti:h -> " .. tostring(mr))
        t.exec("sawmill.pick", t.ui.invoke, cell, 1)
        t.ticks(3)
        t.expect("sawmill.planks", t.inv.expect_has("daddyshome_waxwood_plank", 3))
        t.expect("quest.stage.planks", t.quest.expect_stage("planks"))

        -- ---- The bed, and Yarlo's thanks (4291 [661..684]) ----
        t.exec("bed.walk", t.player.goto_tile, 3242, 3396, 0)
        build(pieces[1])
        t.exec("yarlo.thanks.talk", t.player.talk_to, "daddyshome_daddy", 1)
        t.exec("yarlo.thanks", t.chat.play, {
            "player:I've been working on your furniture.",
            "npc:That's everything! Thank you so much, young man.",
            "player:Okay, I'll head back to Marlo.",
            "end",
        })
        t.expect("quest.stage.built", t.quest.expect_stage("built"))

        -- ---- Marlo's reward (4291 [725..745]) ----
        t.exec("reward.walk", t.player.goto_tile, 3240, 3473, 0)
        t.exec("reward.talk", t.player.talk_to, "con_contractor_varrock", 1)
        t.exec("reward.pages", t.chat.play, {
            "player:I've finished building the new furniture for your dad.",
            "npc:That's wonderful news!",
            "choose:Yeah, what have you got for me?",
            "player:Yeah, what have you got for me?",
            "npc:some supplies in this crate",
            "npc:handy spell tablets",
            "objbox:Marlo rewards you with a crate of supplies, some money, and 400 Construction XP.",
            "end",
        })
        t.expect("quest.stage.complete", t.quest.expect_stage("complete"))
        t.expect("reward.crate", t.inv.expect_has("daddyshome_reward", 1))
        t.expect("reward.coins", t.inv.expect_has("coins", 1000))
        t.expect("reward.miniquests", t.var.expect("varb14162_miniquests_completed_count", 1))

        -- ---- Marlo's crate (4291 [759..763]) ----
        t.exec("crate.open", t.player.inv_op, "daddyshome_reward", 1)
        t.exec("crate.box", t.chat.play, { "objbox:You find a variety of useful Construction supplies.", "end" })
        t.expect("crate.planks", t.inv.expect_has("cert_woodplank", 25))
        t.expect("crate.nails", t.inv.expect_has("nails_mithril", 50))
        t.expect("crate.bars", t.inv.expect_has("cert_steel_bar", 5))
        t.expect("crate.oak", t.inv.expect_has("cert_plank_oak", 10))
        t.expect("crate.cloth", t.inv.expect_has("cert_cloth", 8))
        t.expect("crate.house_tabs", t.inv.expect_has("poh_tablet_teleporttohouse", 5))
        t.expect("crate.falador_tab", t.inv.expect_has("poh_tablet_faladorteleport", 1))
        t.expect("quest.stage.crate_opened", t.quest.expect_stage("crate_opened"))

        -- 4291's stat lines: 45 + 29 + 86 + 57 + 29 + 86 + 207 + 400.
        local _, after = t.skill.read("construction")
        local gained = (type(after) == "table" and type(before) == "table") and (after.experience - before.experience) or nil
        t.check("reward.xp", gained == 939, "construction xp gained " .. tostring(gained) .. " (want 939)")
        t.finish(0)
    end,
}
