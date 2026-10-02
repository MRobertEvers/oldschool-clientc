-- Animal Magnetism: Ava -> Malcolm/Alice (Ectofuntus farm) -> Old crone -> chickens ->
-- witch/magnet -> undead trees/Turael -> research notes -> container.
-- The chicken-catching cutscene is not played by the content (CUTSCENES.tsv ported=no):
-- the scene is a ~mesbox page, driven through as dialogue.
return {
    id = "animalmagnetism",
    fixture = "fresh_lumbridge.ini",
    setup = {
        "::clearinv",
        "::give mithril_axe 1",
        "::give iron_bar 5",
        "::give amulet_of_ghostspeak 1",
        "::give ectotoken 20",
        "::give hard_leather 1",
        "::give blessedstar 1",
        "::give anma_p_buttons 1",
        "::give hammer 1",
        "::setlevel slayer 18",
        "::setlevel crafting 19",
        "::setlevel ranged 30",
        "::setlevel woodcutting 35",
        "::complete quest_restlessghost",
        "::complete quest_ernestthechicken",
        "::complete quest_priestinperil",
    },

    run = function(t)
        local bind_result, bind_detail = t.quest.bind({
            varp = "varb3185_anma_main",
            constants = {
                not_started = 0, fetch_chickens = 10, talk_alice = 20, return_malcolm = 30,
                return_alice = 40, return_malcolm2 = 50, return_alice2 = 60, talk_crone = 70,
                crone_mirror = 73, give_amulet = 76, buy_chickens = 100, give_ava = 110,
                talk_witch = 120, witch_bars = 130, make_magnet = 140, undead_trees = 150,
                tree_bounce = 160, turael_axe = 170, cut_twigs = 180, give_twigs = 190,
                notes = 200, translate = 210, pattern = 220, give_container = 230,
                complete = 240,
            },
            row = "quest_animalmagnetism",
            display = "Animal Magnetism",
            points = 1,
        })
        t.step("quest.bind", bind_result == "ok" and "PASS" or "FAIL", bind_detail)
        t.ticks(3)
        t.exec("wear-ghostspeak", t.player.equip, "amulet_of_ghostspeak")
        t.ticks(2)
        t.expect("quest.stage.not_started", t.quest.expect_stage("not_started"))

        -- 1.1 talkToAva
        t.exec("goto-talkToAva", t.player.goto_tile, 3093, 3358, 0)
        t.exec("talkToAva", t.player.talk_to, "anma_assistant", 1)
        t.ticks(2)
        local pip_r, pip_v = t.var.varp("varp302_priestperil")
        t.check("prereq-priestinperil-state", pip_r == "ok" and pip_v == 60,
            "after ::complete quest_priestinperil varp302_priestperil = " .. tostring(pip_v)
            .. " (quest_cheat.rs2:998 sets ^priestperil_complete = 60)")
        t.check("talkToAva-refused", select(2, t.msg.last(3)) ~= nil, "Ava refused to start: 'You need to complete Priest in Peril first.'")
        t.expect("quest.stage.not_started-still", t.quest.expect_stage("not_started"))
        t.blocked("content_bug: anma.constant:37 ^anma_pip_gate = 61 but Priest in Peril is complete at ^priestperil_complete = 60 "
            .. "(quest_priestperil.constant:20; 61 is only the optional Drezel Morytania warning, mausoleum_drezel.rs2:147), "
            .. "so anma.rs2:30 (anma_missing_requirement) refuses Ava to a player who finished Priest in Peril, "
            .. "and ::complete quest_priestinperil (quest_cheat.rs2:998) cannot satisfy it")
        return
    end,
}
