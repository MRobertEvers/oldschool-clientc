-- toa_baba_tells: Ba-Ba's room for the tick log. A solo raider (kept alive with hitpoint boosts,
-- raid level 0) goes through the teleport crystal and stands off: the rockfall, the boulder throw
-- (the first taken beside the rubble) and her baboons come; then `::toababahit` takes her to
-- 66 % for the first dig (seven volleys of rolling boulders, the scream and knockback), to 33 %
-- for the second, and kills her. Nobody
-- melees her while she digs, so both digs run their whole length. The tick log is what
-- docs/rsprox-analysis/toa/baba.md compares against RSProx.
local function now(t) local _, k = t.tick() return k or -1 end
local function keep_alive(t) t.cheat("::boost hitpoints 255 0", false) end
local function wait(t, n)
    local left = n
    while left > 0 do
        local step = math.min(left, 5)
        t.ticks(step)
        keep_alive(t)
        left = left - step
    end
end
local function said(t, text)
    for _, line in ipairs(t.raid.cox_messages(60)) do
        if line:find(text, 1, true) then return true end
    end
    return false
end

return {
    id = "toa_baba_tells",
    fixture = "fresh_lumbridge.ini",
    max_frames = 90000,
    setup = {
        "::clearinv", "::setlevel hitpoints 99", "::setlevel defence 99", "::setlevel prayer 99",
    },
    run = function(t)
        t.ticklog.start()
        t.cheat("::toa 9", false)
        wait(t, 4)
        t.ticklog.mark("baba.crystal")
        t.expect("baba.crystal", t.player.click_loc("baba_teleport", 2))
        wait(t, 3)
        t.ticklog.mark("baba.specials")
        -- once the first rocks have left rubble, stand beside it for the throw: an eighth of
        -- the hit, and the rubble breaks the tick after it lands. The later throws find the
        -- raider in the open.
        local ox, oz, rx, rz
        local waited = 0
        while waited < 30 and not rx do
            wait(t, 1)
            waited = waited + 1
            t.cheat("::toababa", false)
            t.ticks(1)
            waited = waited + 1
            for _, line in ipairs(t.raid.cox_messages(10)) do
                local x, z = line:match("^toababa origin (%-?%d+) (%-?%d+)")
                if x and not ox then ox, oz = tonumber(x), tonumber(z) end
                local a, b = line:match("^toababa rubble (%-?%d+) (%-?%d+)")
                if a and not rx then rx, rz = tonumber(a), tonumber(b) end
            end
        end
        t.expect("baba.rubble", (rx and ox) and "ok" or "fail")
        if rx and ox then
            -- a side of the rubble inside the arena: its north side, or its south on the top row
            local x, z = ox + rx + 1, oz + rz + 3
            if rz >= 34 then z = oz + rz - 1 end
            t.cheat(string.format("::tele 0_%d_%d_%d_%d", math.floor(x / 64), math.floor(z / 64), x % 64, z % 64), false)
        end
        wait(t, 70 - waited)
        t.ticklog.mark("baba.dig1")
        t.cheat("::toababahit 35", false)
        -- the dig's 60 ticks, then 50 more for her baboons to reach a sarcophagus and open it
        wait(t, 110)
        t.ticklog.mark("baba.dig2")
        t.cheat("::toababahit 35", false)
        wait(t, 70)
        t.ticklog.mark("baba.kill")
        t.cheat("::toababahit 100", false)
        wait(t, 8)
        if said(t, "Challenge complete: Ba-Ba") then
            t.expect("baba.run", "ok", string.format("complete on tick %d", now(t)))
            for _, line in ipairs(t.raid.cox_messages(60)) do
                if line:find("Challenge ", 1, true) then t.note(line) end
            end
        else
            t.expect("baba.run", "fail", "Ba-Ba's room did not complete")
        end
        -- Osmumten's "Proceed": the fade, and the Nexus at +3
        t.ticklog.mark("baba.proceed")
        -- beside him: he is 19 tiles from the rubble side, past our 15-tile npc view
        if ox then
            local x, z = ox + 38, oz + 32
            t.cheat(string.format("::tele 0_%d_%d_%d_%d", math.floor(x / 64), math.floor(z / 64), x % 64, z % 64), false)
            wait(t, 2)
        end
        local r, d = t.player.press("toa_osmumten_baba", 3, 20)
        wait(t, 10)
        t.expect("baba.proceed", r == "ok" and "ok" or "fail", tostring(r) .. " " .. tostring(d))
    end,
}
