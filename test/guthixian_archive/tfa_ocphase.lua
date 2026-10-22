-- tfa_ocphase: `::tfaocphase <n>` puts Ocellus at the start of phase n through
-- his own transitions (tfa_debug.rs2). From the lobby straight to phase 4,
-- then a fresh room back at phase 2, then on to 3: each lands, the raider
-- alive, his form the phase's, and phase 4 on the one quadrant left.
--
--   ./src/build_tfadoor_opt/torirsserver --scriptrun test/guthixian_archive/tfa_ocphase.lua --bots 1 \
--       --name tfa0 --ticks 6000 --session /tmp/tfa --fixture test/raids/fixtures/fresh_lumbridge.ini
local function now(t) local _, k = t.tick() return k or -1 end

local function present(t, sym)
    local r = t.npc.nearest(sym, 40)
    return r == "ok"
end

-- Wait for the cheat's own "phase n" line, healing the raider as it goes.
local function reach(t, n, ticks)
    local deadline = now(t) + ticks
    local started = now(t)
    while now(t) < deadline do
        t.cheat("::tfaheal", false)
        if t.msg.expect("Ocellus: phase " .. n .. ".") == "ok" then
            return now(t) - started
        end
        t.ticks(1)
    end
    return nil
end

local function alive(t, name)
    local r, hp = t.skill.read("hitpoints")
    t.check(name .. ".alive", r == "ok" and type(hp) == "table" and (hp.level or 0) > 0,
        "hitpoints " .. tostring(hp and hp.level))
end

return {
    id = "tfa_ocphase",
    fixture = "fresh_lumbridge.ini",
    max_frames = 900000,
    setup = {
        "::clearinv",
        "::complete quest_whileguthixsleeps",
        "::setvar varb16354_wgs_tfa_shortcut_unlocked 1",
        "::setvar varb16426_tfa_miniquest 2",
        "::setlevel hitpoints 99",
        "::setlevel prayer 99",
        "::goto 4128 5556 0",
    },
    run = function(t)
        t.player._death_fence = function() return false end
        t.ticks(2)

        t.cheat("::tfaocphase 4", false)
        local took = reach(t, 4, 400)
        t.check("p4.reached", took ~= nil, "no phase 4 line")
        t.note("phase 4 in " .. tostring(took) .. " ticks")
        t.check("p4.form", present(t, "tfa_ocellus_p3"), "his phase 3/4 form not in view")
        alive(t, "p4")
        -- On the one quadrant left (his deflect pulses land every tick from
        -- +4: the bot stands still, so it is healed through them).
        local _, tile = t.world.tile()
        t.note(string.format("p4 tile (%d,%d,%d)", tile.x % 64, tile.z % 64, tile.level))
        for _ = 1, 10 do
            t.cheat("::tfaheal", false)
            t.ticks(1)
        end
        alive(t, "p4_after")

        t.cheat("::tfaocphase 2", false)
        took = reach(t, 2, 200)
        t.check("p2.reached", took ~= nil, "no phase 2 line")
        t.note("phase 2 in " .. tostring(took) .. " ticks")
        t.check("p2.form", present(t, "tfa_ocellus_p2"), "his phase 2 form not in view")
        alive(t, "p2")

        t.cheat("::tfaocphase 3", false)
        took = reach(t, 3, 200)
        t.check("p3.reached", took ~= nil, "no phase 3 line")
        t.note("phase 3 in " .. tostring(took) .. " ticks")
        t.check("p3.form", present(t, "tfa_ocellus_p3"), "his phase 3 form not in view")
        alive(t, "p3")

        t.cheat("::tfaocphase 1", false)
        took = reach(t, 1, 200)
        t.check("p1.reached", took ~= nil, "no phase 1 line")
        t.check("p1.form", present(t, "tfa_ocellus_p1"), "his phase 1 form not in view")
        t.ticks(4)
        t.expect("tfa_ocphase.run", "ok", string.format("ended on tick %d", now(t)))
    end,
}
