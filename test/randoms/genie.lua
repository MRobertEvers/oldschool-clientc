-- genie: the Genie random event, its three endings, for the tick log, to diff
-- against RSProx index 4431 [1500..1521] (talked to, the lamp rubbed for
-- Agility), 3443 [14000..14103] (ignored to the end) and 3153 [3500..3574]
-- (dismissed). docs/rsprox-analysis/quests/death_plateau.md, "The Genie".
--
--   TORIRSSERVER_SCRIPTS=<pack> TORIRSSERVER_PACK_DIR=<servpack> \
--   ./src/build_opt/torirsserver --scriptrun test/randoms/genie.lua --bots 1 \
--       --name genie0 --ticks 1500 --session /tmp/genie --fixture test/quests/fixtures/fresh_lumbridge.ini
local function first_row(t, sym)
    local r, _, rows = t.npc.tiles(sym, 20)
    if r == "ok" and rows and rows[1] then return rows[1] end
    return nil
end

return {
    id = "genie",
    fixture = "fresh_lumbridge.ini",
    max_frames = 200000,
    setup = {
        "::clearinv",
        "::setlevel agility 30",
    },
    run = function(t)
        t.ticklog.start()

        -- 1. Talked to: the reward line, the lamp, the page; he leaves the
        -- tick after it closes. The lamp: 10 x the level (Agility 30 -> 300).
        t.ticklog.mark("genie.talk")
        t.cheat("::genie", false)
        t.ticks(3)
        t.check("talk.here", first_row(t, "macro_geni") ~= nil, "the genie within 20")
        t.ticks(9) -- past the first reminder (S+10)
        local r, d = t.player.talk_to("macro_geni", 1)
        t.check("talk.op", r == "ok", tostring(r) .. " " .. tostring(d))
        t.exec("talk.page", t.chat.play, {
            "npc:Ah, so you are there, Master",
        })
        t.ticks(3)
        t.check("talk.gone", first_row(t, "macro_geni") == nil, "the genie left after the page")
        t.expect("talk.reward", t.msg.expect("Your reward is:"))
        local lamp_r, lamp_n = t.inv.count("macro_genilamp")
        t.check("talk.lamp", lamp_r == "ok" and lamp_n == 1, "lamp count " .. tostring(lamp_n))
        t.exec("lamp.rub", t.player.inv_op, "macro_genilamp", 1)
        t.ticks(2)
        t.exec("lamp.agility", t.ui.press_pause, "xpreward:universe", 7, "xpreward:confirm")
        t.ticks(2)
        t.expect("lamp.granted", t.msg.expect("You have been awarded 300 Agility XP!"))
        t.exec("lamp.box", t.chat.drain, {})
        local lamp2_r, lamp2_n = t.inv.count("macro_genilamp")
        t.check("lamp.used", lamp2_r == "ok" and lamp2_n == 0, "lamp count after the rub " .. tostring(lamp2_n))

        -- 2. Dismissed: a wave, gone the next tick.
        t.ticklog.mark("genie.dismiss")
        t.cheat("::genie", false)
        t.ticks(4)
        r, d = t.player.talk_to("macro_geni", 5)
        t.note("dismiss.op: " .. tostring(r) .. " " .. tostring(d))
        t.ticks(4)
        t.check("dismiss.gone", first_row(t, "macro_geni") == nil, "the genie left on Dismiss")

        -- 3. Ignored: nine reminders, "Maybe later." at S+100, gone at S+103.
        t.ticklog.mark("genie.ignore")
        t.cheat("::genie", false)
        t.ticks(60)
        t.check("ignore.stays", first_row(t, "macro_geni") ~= nil, "the genie still waiting at ~S+60")
        t.ticks(50)
        t.check("ignore.gone", first_row(t, "macro_geni") == nil, "the genie left by ~S+110")

        t.finish(0)
    end,
}
