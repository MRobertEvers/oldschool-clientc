-- Daddy's Home (miniquest) as RSProx index 4350 plays it (rev 241,
-- 2026-10-23): a player with no house and Construction 1. Marlo's beginner
-- greeting [173..185], Yarlo [234..237], the removals and his list
-- [244..247], the lecture's cutscene with no question first [280..450], the
-- free tools [463..477], the bed clicked before its materials [517..519], the
-- builds, the crate, the sawmill, the bed, and Marlo's no-house reward: a
-- house in Rimmington and the guide book in place of the coins [769..777].
-- The house-owner's path (4291) is test/quests/daddyshome.lua. Pages asserted
-- here are 4350's (docs/rsprox-analysis/quests/daddyshome.md).
return {
    id = "daddyshome_nohouse",
    fixture = "fresh_lumbridge.ini",
    max_frames = 120000,
    setup = {
        "::clearinv",
        -- Exactly what the builds take: ten planks, five bolts of cloth, fourteen
        -- steel nails (2 + 2 + 4 + 4 + 2; nails never bend here).
        "::give woodplank 10",
        "::give cloth 5",
        "::give nails 14",
        "::tele varrock",
    },

    run = function(t)
        -- the wire audit against RSProx 4350 reads this (build/quest_gate/<run>/ticklog.tsv)
        t.ticklog.start()
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

        -- ---- Marlo (index 4350 [173..185]) ----
        t.exec("marlo.walk", t.player.goto_tile, 3240, 3473, 0)
        t.exec("marlo.talk", t.player.talk_to, "con_contractor_varrock", 1)
        t.exec("marlo.welcome", t.chat.play, { "npc:I wonder... could you maybe do me a little favour?", "options" })
        menu_rows("marlo.menu1", "What does this Mahogany Homes company do?|What kind of favour do you want me to do?|Maybe another time.")
        t.exec("marlo.favour", t.chat.play, {
            "choose:What kind of favour do you want me to do?",
            "player:What kind of favour do you want me to do?",
            "npc:it's about my dad",
            "options",
        })
        menu_rows("marlo.menu2", "Aren't you in a construction company yourself though?|Why has he got such a bad house?|Tell me more about the job.|I'd rather not.")
        t.exec("marlo.job", t.chat.play, {
            "choose:Tell me more about the job.",
            "player:Tell me more about the job.",
            "npc:Nothing too hard.",
            "options",
        })
        menu_rows("marlo.menu3", "Aren't you in a construction company yourself though?|Why has he got such a bad house?|Tell me where he lives, and I'll do the job.|I'd rather not.")
        t.exec("marlo.accept", t.chat.play, {
            "choose:Tell me where he lives, and I'll do the job.",
            "player:Tell me where he lives, and I'll do the job.",
            "npc:Old Man Yarlo, they call him.",
            "end",
        })
        t.expect("quest.stage.find_yarlo", t.quest.expect_stage("find_yarlo"))

        -- ---- Yarlo (4350 [234..237]) ----
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

        -- ---- The removals; the bed first, then Yarlo's list (4350 [244..247]) ----
        local pieces = {
            { "daddyshome_bed", 1, 3241, 3397, "varb10568_daddyshome_bed" },
            { "daddyshome_carpet_middle", 5, 3239, 3395, "varb10569_daddyshome_carpet" },
            { "daddyshome_stool_2", 1, 3239, 3394, "varb10564_daddyshome_stool_2" },
            { "daddyshome_table_2", 1, 3240, 3394, "varb10567_daddyshome_table_2" },
            { "daddyshome_chair", 1, 3241, 3393, "varb10565_daddyshome_chair" },
            { "daddyshome_stool_1", 1, 3244, 3394, "varb10563_daddyshome_stool_1" },
            { "daddyshome_table_1", 1, 3245, 3394, "varb10566_daddyshome_table_1" },
        }
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

        -- ---- The lecture's cutscene, no question first (4350 [276..460]) ----
        t.exec("yarlo.done.talk", t.player.talk_to, "daddyshome_daddy", 1)
        t.exec("yarlo.done", t.chat.play, {
            "npc:how's the redecoration coming on?",
            "player:I've finished removing all the old furniture.",
            "npc:Jolly good, young man.",
        })
        local lecture_mark = t.cutscene.mark()
        t.exec("yarlo.lecture", t.chat.play, { "npc:I used to be a carpenter myself" })
        t.await({ level = function() return t.chat.kind() ~= "none" end, note = "yarlo.lecture.box" }, 12)
        t.exec("yarlo.lecture.boxes", t.chat.drain, { max_pages = 40, stop_at = "npc" })
        t.exec("yarlo.lecture.cutscene", t.cutscene.await, "yarloLecture", { since = lecture_mark, quiet = 200, expect = {
            { op = "moveto", height = 1000 },
            { op = "lookat", height = 250 },
            { op = "reset" },
        } })
        t.exec("yarlo.after", t.chat.play, {
            "npc:For a start, I'd like two new stools, two new tables and a new chair.",
            "objbox:Some basic wooden planks and some nails should do",
            "objbox:I'll want a new carpet too.",
            "npc:Now, for the bed, I've got something special in mind.",
            "objbox:Those abandoned crates in the kitchen",
            "objbox:You'll have to take three logs from the crate to the sawmill.",
            "npc:I think that's everything",
            "end",
        })
        t.expect("lecture.unlocked", t.var.expect("varb542_cutscene_status", 0))
        t.expect("quest.stage.waxwood", t.quest.expect_stage("waxwood"))

        -- ---- The help menu: the tools (4350 [463..478]) ----
        t.exec("yarlo.help.talk", t.player.talk_to, "daddyshome_daddy", 1)
        t.exec("yarlo.help", t.chat.play, {
            "player:I've been working on your furniture.",
            "npc:Do you need some help with the building work",
            "options",
        })
        menu_rows("yarlo.help_menu", "How do I build furniture?|Where do I get planks, nails and cloth?|I need some tools.|Tell me about the waxwood for the bed.|I'll get on with it.")
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
        menu_rows("yarlo.help_menu2", "How do I build furniture?|Where do I get planks, nails and cloth?|Tell me about the waxwood for the bed.|I'll get on with it.")
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
        -- The bed before its planks (4350 [517..519]): the row's materials box.
        t.exec("bed.early", t.player.click_loc, "daddyshome_bed", 1, { at = { 3241, 3397 } })
        t.check("bed.early.panel", t.ui.await_open("poh_furniture_creation", 8) == "ok", "build panel open")
        local er, ew = t.ui.widget("poh_furniture_creation:01")
        t.check("bed.early.row", er == "ok", "row 01 -> " .. tostring(er))
        t.exec("bed.early.pick", t.ui.invoke, ew, 1)
        t.exec("bed.early.box", t.chat.play, { "mesbox:You don't have the right materials:", "end" })
        t.expect("bed.early.unbuilt", t.var.expect("varb10568_daddyshome_bed", 2))

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

        -- ---- Marlo's reward, a house instead of the coins (4350 [769..777]) ----
        t.exec("reward.walk", t.player.goto_tile, 3240, 3473, 0)
        t.exec("reward.talk", t.player.talk_to, "con_contractor_varrock", 1)
        t.exec("reward.pages", t.chat.play, {
            "player:I've finished building the new furniture for your dad.",
            "npc:That's wonderful news!",
            "choose:Yeah, what have you got for me?",
            "player:Yeah, what have you got for me?",
            "npc:persuaded the Estate Agent to let me give you a house",
            "npc:Normally a new house costs 1,000 coins",
            "npc:handy spell tablets",
            "objbox:Marlo rewards you with a crate of supplies, a book about how to use them",
            "end",
        })
        t.expect("quest.stage.complete", t.quest.expect_stage("complete"))
        t.expect("reward.crate", t.inv.expect_has("daddyshome_reward", 1))
        t.expect("reward.book", t.inv.expect_has("poh_guide_book", 1))
        t.expect("reward.no_coins", t.inv.expect_absent("coins"))
        t.expect("reward.house", t.var.expect("varb2187_poh_house_location", 1))
        t.expect("reward.miniquests", t.var.expect("varb14162_miniquests_completed_count", 1))

        -- ---- Marlo's crate (4350 [784]) ----
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

        -- 4350's stat lines, 4291's: 45 + 29 + 86 + 57 + 29 + 86 + 207 + 400.
        local _, after = t.skill.read("construction")
        local gained = (type(after) == "table" and type(before) == "table") and (after.experience - before.experience) or nil
        t.check("reward.xp", gained == 939, "construction xp gained " .. tostring(gained) .. " (want 939)")
        t.finish(0)
    end,
}
