-- colo_smoke: into the Colosseum by the lobby gate, a card, wave 1 and its
-- clear, for the tick log against RSProx (docs/rsprox-analysis/colosseum).
--
--   ./src/build_opt/torirsserver --scriptrun test/minigames/colosseum/colo_smoke.lua --bots 1 \
--       --name colo0 --ticks 3000 --session /tmp/colo --fixture test/raids/fixtures/fresh_lumbridge.ini
local function now(t) local _, k = t.tick() return k or -1 end

local EAT = { eat = { item = "shark", below = 50 } }

local TRIO = { "colosseum_warbander_melee_male", "colosseum_warbander_ranged_female", "colosseum_warbander_mage_male", "colosseum_standard_mager",
    "colosseum_jaguar_warrior", "colosseum_javelin_colossus" }

local function any_present(t)
    for _, s in ipairs(TRIO) do
        if t.npc.nearest(s, 40) == "ok" then return s end
    end
    return nil
end

return {
    id = "colo_smoke",
    fixture = "fresh_lumbridge.ini",
    max_frames = 900000,
    setup = {
        "::clearinv",
        "::setlevel attack 99", "::setlevel strength 99", "::setlevel defence 99",
        "::setlevel hitpoints 99", "::setlevel ranged 99", "::setlevel magic 99", "::setlevel prayer 99",
        "::give bow_of_faerdhinen 1", "::wield bow_of_faerdhinen",
        "::give black_dragonhide_body 1", "::wield black_dragonhide_body",
        "::give shark 20",
        "::setvar varb9807_colosseum_master_intro 1",
        "::setvar varb9993_colosseum_entry_warning 1",
        "::setvar varb9809_colosseum_boss_cutscene_seen 1",
        "::goto 1808 9506 0",
    },
    run = function(t)
        t.player._death_fence = function() return false end
        t.ticklog.start()
        t.ticks(2)
        t.ticklog.mark("gate")
        local r, d = t.player.click_loc("colosseum_entrance", 1)
        t.check("gate.click", r == "ok", tostring(r) .. " " .. tostring(d))
        local ok = t.await({ level = function() return t.npc.nearest("colosseum_master", 20) == "ok" end, note = "minimus" }, 30)
        t.check("arena.minimus", ok == "ok", "no Minimus")
        t.ticks(4)
        t.ticklog.mark("startwave")
        r, d = t.player.talk_to("colosseum_master", 1)
        local open, why = t.ui.await_open("colosseum_intermission_2", 10)
        t.check("cards.open", open, why)
        if open ~= "ok" then return end
        local _, card = t.ui.widget("colosseum_intermission_2:mod_button_1")
        t.ui.invoke(card, 1)
        t.ticks(1)
        local _, go = t.ui.widget("colosseum_intermission_2:right_button")
        t.ui.invoke(go, 1)
        t.ticklog.mark("confirm")
        t.prayer.set("protectfrommagic", true)
        ok = t.await({ level = function() return any_present(t) ~= nil end, note = "wave 1" }, 20)
        t.check("wave1.spawn", ok == "ok", "no wave 1")
        t.ticklog.mark("wave1")
        local deadline = now(t) + 400
        while now(t) < deadline do
            local s = any_present(t)
            if not s then break end
            t.player.attack(s, 2, 2, EAT)
            t.ticks(1)
        end
        t.ticklog.mark("cleared")
        ok = t.await({ level = function() return t.ui.is_modal("colosseum_intermission_2") end, note = "cards 2" }, 20)
        t.check("wave1.cards", ok == "ok", "no cards after wave 1")
        t.ticks(4)
        t.ticklog.mark("end")
        t.expect("colo_smoke.run", "ok", string.format("ended on tick %d", now(t)))
    end,
}
