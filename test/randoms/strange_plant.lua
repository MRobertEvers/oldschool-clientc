-- strange_plant: the Strange plant random event
-- (macro_strange_plant/scripts/macro_strange_plant.rs2): picked too early,
-- picked grown, eaten, left alone. To diff against RSProx index 4512
-- [500..631] and 4075 [1000..1131].
--
--   TORIRSSERVER_SCRIPTS=<pack> TORIRSSERVER_PACK_DIR=<servpack> \
--   ./src/build_opt/torirsserver --scriptrun test/randoms/strange_plant.lua --bots 1 \
--       --name plant0 --ticks 1500 --session /tmp/plant --fixture test/quests/fixtures/fresh_lumbridge.ini
local function plant(t)
    local r, _, rows = t.npc.tiles("macro_triffidseed", 20)
    return r == "ok" and rows and rows[1] ~= nil
end

return {
    id = "strange_plant",
    fixture = "fresh_lumbridge.ini",
    max_frames = 200000,
    setup = { "::clearinv" },
    run = function(t)
        t.ticklog.start()

        -- 1. Too early [511], then grown (S+43): the fruit; gone 21 ticks on.
        t.ticklog.mark("plant.pick")
        t.cheat("::strangeplant", false)
        t.ticks(3)
        t.check("pick.here", plant(t), "the plant within 20")
        local r, d = t.player.talk_to("macro_triffidseed", 1)
        t.check("early.op", r == "ok", tostring(r) .. " " .. tostring(d))
        t.ticks(2)
        t.expect("early.line", t.msg.expect("The fruit isn't ready to be picked yet."))
        t.ticks(42)
        -- No dialogue answers a pick, so the op's own verdict is only noted.
        r, d = t.player.talk_to("macro_triffidseed", 1)
        t.note("pick.op: " .. tostring(r) .. " " .. tostring(d))
        t.ticks(2)
        local fr, fn = t.inv.count("macro_triffidfruit")
        t.check("pick.fruit", fr == "ok" and fn == 1, "strange fruit count " .. tostring(fn))
        t.ticks(23)
        t.check("pick.gone", not plant(t), "the plant left 21 ticks after the pick")

        -- 2. Eat it: the wiki's line.
        t.exec("eat.op", t.player.inv_op, "macro_triffidfruit", 1)
        t.ticks(2)
        t.expect("eat.line", t.msg.expect("It tastes great, some of your energy is restored!"))

        -- 3. Left alone: the fruit falls at S+110, the plant gone at S+131.
        t.ticklog.mark("plant.ignore")
        t.cheat("::strangeplant", false)
        t.ticks(100)
        t.check("ignore.stays", plant(t), "the plant still standing at ~S+100")
        t.ticks(40)
        t.check("ignore.gone", not plant(t), "the plant gone by ~S+140")

        t.finish(0)
    end,
}
