-- levelup: the level-up box, fireworks and jingle as rev 241 sends them
-- (docs/rsprox-analysis/levelup/README.md), for the wire audit.
--
--   1. Cooking 1 -> 4 from one drop: the box on the tick after the cheat's
--      (xp an op grants lands after the queue phase, so its box is two ticks
--      on, as recorded), the plain jingle (index 4339 [177], Cook's
--      Assistant's reward).
--   2. Attack and Thieving from drops a tick apart, Attack raising combat: the
--      three boxes in Jagex's order, each on the last one's continue, combat
--      last (index 4396 [603], [605], [606]), and the total-level line.
--   3. Strength alone raising combat: the combat box the tick after the
--      continue (index 4394 [2142] -> [2143]).
--
--   TORIRSSERVER_DEV=1 TORIRSSERVER_SCRIPTS=<pack> ./src/build_opt/torirsserver --scriptrun test/levelup/levelup.lua \
--       --bots 1 --name levelup0 --ticks 200 --session /tmp/levelup \
--       --fixture test/quests/fixtures/fresh_lumbridge.ini
local function continue_(t, name)
    local r, d = t.levelup.continue_()
    t.check("continue." .. name, r == "ok", tostring(r) .. " " .. tostring(d))
    -- The next box opens on the continue's tick (the stat behind it) or the
    -- tick after (combat): let both land before reading.
    t.ticks(2)
end

local function box(t, name, skill, line)
    local r, d = t.levelup.skill()
    local ok = r == "ok" and type(d) == "table" and d.skill == skill
        and string.find(d.level_text or "", line, 1, true) ~= nil
    t.check("box." .. name, ok, tostring(r) .. " "
        .. (type(d) == "table" and (tostring(d.skill) .. " | " .. tostring(d.level_text)) or tostring(d)))
    return ok
end

return {
    id = "levelup",
    fixture = "fresh_lumbridge.ini",
    max_frames = 20000,
    setup = {},
    run = function(t)
        t.ticks(3)
        t.ticklog.start()

        t.ticklog.mark("leg.cooking")
        -- ::xp takes tenths, as stat_advance does.
        t.cheat("::xp cooking 3000")
        box(t, "cooking", "cooking", "Your Cooking level is now 4.")
        t.exec("cooking.msg", t.msg.expect,
            "Congratulations, you've just advanced your Cooking level. You are now level 4.")
        continue_(t, "cooking")
        t.ticks(4)

        t.ticklog.mark("leg.chain")
        t.cheat("::xp attack 44700")
        t.cheat("::xp thieving 20000")
        box(t, "attack", "attack", "Your Attack level is now 20.")
        t.exec("attack.msg", t.msg.expect,
            "Congratulations, you've just advanced your Attack level. You are now level 20.")
        t.exec("total.msg", t.msg.expect, "Congratulations, you've reached a total level of 50.")
        continue_(t, "attack")
        box(t, "thieving", "thieving", "Your Thieving level is now 13.")
        continue_(t, "thieving")
        box(t, "combat", "combat", "Your combat level is now 9.")
        t.exec("combat.msg", t.msg.expect,
            "Congratulations, you've just advanced your Combat level. You are now level 9.")
        continue_(t, "combat")
        t.ticks(4)

        t.ticklog.mark("leg.strength")
        t.cheat("::xp strength 1740")
        box(t, "strength", "strength", "Your Strength level is now 3.")
        continue_(t, "strength")
        box(t, "combat10", "combat", "Your combat level is now 10.")
        continue_(t, "combat10")
        t.ticks(4)
    end,
}
