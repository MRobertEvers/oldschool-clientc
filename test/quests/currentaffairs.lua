-- Current Affairs.
-- content: OSRS-Content/osrs239-content/server/scripts/quests/quest_currentaffairs/scripts/currentaffairs.rs2
-- Arhein's opnpc1 delegates to `ca_arhein_talk` via
-- areas/area_catherby/scripts/arhein.rs2 (op 1, confirmed). Harry's
-- opnpc1 delegates to `ca_harry_talk` the same way, only inside the
-- quest's ^ca_get_mayor..^ca_show_mayor window (harry.rs2).

return {
    id = "currentaffairs",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv", -- the fixture's fourteen tutorial slots, so a requirement fits
        "::currentaffairs", -- debugproc: resets the quest varp, teleports to Arhein
        "::give coins 50", -- Harry's mayoral-fishbowl-and-net purchase, a real currency cost
        "::setlevel sailing 22",
        "::setlevel fishing 10",
        "::complete quest_pandemonium",
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "current_affairs_main", -- the actual varp symbol (all.varp.compack 4956); "current_affairs" does not resolve
            constants = {
                not_started = 0,
                councillor = 5,
                form = 10,
                arhein_mayor = 15,
                get_mayor = 20,
                show_mayor = 25,
                sign = 30,
                news = 35,
                duck = 40,
                complete = 45,
            },
            row = "quest_currentaffairs",
            display = "Current Affairs", -- all.dbrow [quest_currentaffairs] columndef 2:displayname
            points = 1,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.ticks(3) -- the ::currentaffairs debug reset's effect is not client-side yet
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- Talk to Arhein on the Catherby docks to start the quest.
        t.exec("goto-startQuest", t.player.goto_tile, 2803, 3430, 0) -- arhein.spawn row (m43_53.spawn)
        t.exec("startQuest", t.player.talk_to, "arhein", 1)
        -- ca_arhein_talk, ^ca_not_started branch: chatplayer, chatnpc, p_choice2(Yes/No), chatnpc.
        t.exec("startQuest-dialog", t.chat.play, {
            "player:What's with the duck?",
            "npc:Red tape. Councillor Catherine won't let me use my Current duck until the by-laws change.",
            "choose:Yes.",
            "npc:Talk to Councillor Catherine in north-east Catherby. Tell her you're my new employee.",
        })
        t.expect("quest.stage.councillor", t.quest.expect_stage("councillor"))

        -- Councillor Catherine's ca_councillor_talk label owns her dialogue,
        -- but current_affairs_councillor (npc.compack id 14952) has NO
        -- placement anywhere in this content pack: `grep -rn --include='*.spawn'
        -- "current_affairs_councillor" OSRS-Content/osrs239-content/server/scripts/`
        -- and a whole-tree `grep -rl current_affairs_councillor OSRS-Content/`
        -- both come back with only the definition (npc.compack, all.npc,
        -- 7_models.pack) and the .rs2's own [opnpc1,...] trigger -- no
        -- *.spawn row, no npc_add call, nothing that puts a live instance of
        -- her in the world. Confirm live before blocking: goto her documented
        -- coordinate (^ca_councillor_coord = 0_44_53_9_62 decodes to
        -- 2825,3454,0, matching Quest Helper's WorldPoint) and try both a
        -- direct click and a symbol lookup.
        t.exec("goto-talkToCouncillor", t.player.goto_tile, 2825, 3454, 0)
        local talk_result, talk_detail = t.player.talk_to("current_affairs_councillor", 1)
        t.check("talkToCouncillor.probe", talk_result ~= "ok",
            string.format("talk_to(current_affairs_councillor) at 2825,3454,0 -> %s: %s",
                tostring(talk_result), tostring(talk_detail)))
        local lookup_result, lookup_detail = t.npc.by_symbol("current_affairs_councillor")
        t.check("councillor.lookup", lookup_result ~= "ok",
            string.format("npc.by_symbol(current_affairs_councillor) -> %s: %s (id resolves in npc.compack, no live instance anywhere in the loaded world)",
                tostring(lookup_result), tostring(lookup_detail)))

        t.blocked("current_affairs_councillor (npc.compack id 14952) has no spawn row anywhere in OSRS-Content/osrs239-content/server/scripts/ (whole-tree grep, not just areas/) and no live instance is found in the world at her documented tile 2825,3454,0 (ca_councillor_coord) or via npc.by_symbol -- the quest cannot progress past %current_affairs_main = ca_councillor (5) because ca_councillor_talk's owning npc was never placed. talk_to answered '" .. tostring(talk_result) .. ": " .. tostring(talk_detail) .. "'.")
        return
    end,
}
