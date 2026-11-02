-- Probe: Twilight's Promise from Metzli at the Teomat (stage 42): the battle,
-- the aftermath and the ending. QUALIFY=1 gives the Heart of Darkness levels,
-- so the ending must carry on past the scroll into its offer (RSProx 1679);
-- set it true for that branch.
local QUALIFY = false
local setup = {
    "::clearinv",
    "::complete quest_childrenofthesun",
    "::setvar varb9652_vmq2_first_travel 4",
    "::setvar varb9649_vmq2 42",
    "::setlevel hitpoints 80", "::setlevel attack 70", "::setlevel strength 70", "::setlevel defence 60",
    "::give rune_scimitar 1", "::give shark 12",
    "::tele 0_22_49_40_58",
}
if QUALIFY then
    for _, c in ipairs({ "::setlevel mining 55", "::setlevel thieving 48", "::setlevel slayer 48", "::setlevel agility 46" }) do
        table.insert(setup, c)
    end
end
return {
    max_frames = 120000,
    setup = setup,
    run = function(t)
        t.ticks(4)
        t.ticklog.start()
        t.player.equip("rune_scimitar")
        t.exec("goto-metzli", t.player.goto_tile, 1448, 3194, 0)
        t.exec("metzli", t.player.talk_to, "vmq2_metzli_teomat", 1)
        t.chat.drain({ max_pages = 80 })
        t.ticks(6)
        local cultists = { "vmq2_cultist_m_1", "vmq2_cultist_f_1", "vmq2_cultist_m_2", "vmq2_cultist_f_2", "vmq2_cultist_m_3", "vmq2_cultist_f_3" }
        for round = 1, 60 do
            local _, stg = t.var.server("varb9649_vmq2")
            if stg ~= 44 then break end
            for _, sym in ipairs(cultists) do
                if t.player.attack(sym, 2, 12) == "ok" then break end
            end
            t.ticks(8)
            local _, hp = t.skill.read("hitpoints")
            if type(hp) == "table" and (hp.level or 99) < 40 then t.player.eat("shark") end
        end
        local _, s46 = t.var.server("varb9649_vmq2")
        t.expect("battle.over", "ok", "stage " .. tostring(s46))
        t.ticks(8)
        t.chat.drain({ max_pages = 30 })
        t.ticks(6)
        t.chat.drain({ max_pages = 30 })
        t.ticks(3)
        local _, s50 = t.var.server("varb9649_vmq2")
        t.check("complete", s50 == 50, "stage " .. tostring(s50))
        if QUALIFY then
            t.ticks(2)
            local scroll = t.ui.await_open("questscroll", 5)
            t.check("scroll.open", scroll == "ok", tostring(scroll))
            local _, cb = t.ui.widget("questscroll:close_button")
            t.check("scroll.close", t.ui.invoke(cb, 1) == "ok", tostring(cb))
            t.ticks(3)
            local played = t.chat.play({ "player:So what's this plan", "npc:You want to hear it already?" })
            t.check("hod.offer.pages", played == "ok", tostring(played))
        end
        t.finish(0)
    end,
}
