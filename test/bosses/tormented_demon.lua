-- tormented_demon: one Tormented Demon killed in the Ancient Guthixian Temple,
-- solo, by the v2 solver (script/plugins/quest_driver/td_solver.lua; plan
-- docs/rsprox-analysis/tormented_demons/solver.md, mechanics README.md
-- beside it, from RSProx).
--
-- The kit is the wiki's "Max Melee/Range" (Tormented Demon/Strategies): max
-- melee worn with the emberlight and avernic defender, the scorching bow and
-- dragon arrows for the melee-prayer stretches, a super combat, a ranging
-- potion, super restores and anglerfish. The player is put in the temple a few
-- tiles east of the variant-2 spawn at 4123,4381 (the recorded spawn the
-- tells test fights) and engages it from there.
--
-- GREEN (solver.md section 5, written before the first run): the demon's
-- death seen, the player alive, within 600 ticks of the engage; 0 bomb hits,
-- 0 melee swings on a wrong overhead, 0 fireball / rib swings on a wrong
-- overhead once the segment's style was known; at most one guessed throw a
-- segment.
--
--   ./src/build_td_opt/torirsserver --scriptrun test/bosses/tormented_demon.lua --bots 1 \
--       --name td0 --ticks 3000 --session /tmp/td --fixture test/raids/fixtures/fresh_lumbridge.ini

local function run(t)
    t.check("td.ticklog", t.ticklog.start() == "ok", "")
    local result, detail, S = t.raid.tormented_demon_solve({ max_ticks = 900 })
    t.check("td.solve", result == "ok", tostring(detail))
    local m = S and S.m or {}
    local engaged_for = (m.dead and m.engaged) and (m.dead - m.engaged) or -1
    t.check("td.kill", m.dead ~= nil, "death seen on " .. tostring(m.dead))
    t.check("td.alive", result ~= "died", tostring(result))
    t.check("td.time", engaged_for >= 0 and engaged_for <= 600, "engage to death " .. engaged_for .. " ticks")
    t.check("td.no_bomb_hit", (m.bomb_hits or 0) == 0, "bomb hits " .. tostring(m.bomb_hits) .. " of " .. tostring(m.bombs))
    t.check("td.no_wrong_melee", (m.wrong_melee or 0) == 0, "melee swings on a wrong overhead " .. tostring(m.wrong_melee))
    t.check("td.wrong_known", (m.wrong_known or 0) == 0, "known-style swings on a wrong overhead " .. tostring(m.wrong_known))
    t.check("td.guesses", (m.guessed or 0) <= (m.segments or 0),
        string.format("guessed throws %s in %s segments (%s wrong)", tostring(m.guessed), tostring(m.segments),
            tostring(m.wrong_guess)))
    local green = result == "ok" and engaged_for >= 0 and engaged_for <= 600 and (m.bomb_hits or 0) == 0
        and (m.wrong_melee or 0) == 0 and (m.wrong_known or 0) == 0 and (m.guessed or 0) <= (m.segments or 0)
    t.finish(green and 0 or 1)
end

local kit = { "::clearinv" }
for _, s in ipairs({ "attack", "strength", "defence", "ranged", "hitpoints", "prayer" }) do
    kit[#kit + 1] = "::setlevel " .. s .. " 99"
end
for _, line in ipairs({
    -- Piety (Knight Waves) and Rigour
    "::setvar varb3888_kr_quest ^kr_complete",
    "::setvar varb3909_kr_knightwaves_state 8",
    "::setvar varb5451_prayer_rigour_unlocked 1",
    -- max melee worn (cheat_max_gear.rs2 [debugproc,maxmelee]), its scythe
    -- traded for the emberlight and avernic defender, dragon arrows in the
    -- ammo slot for the bow
    "::~maxmelee",
    "::give emberlight 1", "::wield emberlight",
    "::give infernal_defender 1", "::wield infernal_defender",
    "::give dragon_arrow 1000", "::wield dragon_arrow",
    "::clearinv",
    "::give scorching_bow 1",
    "::give 4dose2combat 1", "::give 4doserangerspotion 1",
    "::give 4dose2restore 6", "::give anglerfish 8",
    -- the Ancient Guthixian Temple, east of the 4123,4381 demon
    "::goto 4129 4381 0",
}) do kit[#kit + 1] = line end

return {
    id = "tormented_demon",
    fixture = "fresh_lumbridge.ini",
    max_frames = 400000,
    setup = kit,
    run = run,
}
