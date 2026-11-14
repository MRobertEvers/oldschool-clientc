-- Monkey Madness I: the jail guard's punch, as RSProx index 4562..4564 recorded
-- it 22 times (4562 [4958]..[4969] is the first). A human stands where the
-- recorder stood outside the cell and waits for Aberab or Trefaji; the tick log
-- is what is compared (docs/rsprox-analysis/quests/monkey_madness_i.md, "The
-- jail guard's punch").
--
--     python3 tools/quest_gate/run.py --script test/quests/_mm_punch.lua --name mm_punch --fixture fresh_lumbridge.ini --no-publish
local function tile(t)
    local r, w = t.world.tile()
    if r ~= "ok" then return nil end
    return w
end

local function in_cell(w)
    return w ~= nil and w.x >= 2770 and w.x <= 2776 and w.z >= 2793 and w.z <= 2794 and w.level == 0
end

return {
    id = "mm_punch",
    fixture = "fresh_lumbridge.ini",
    max_frames = 120000,
    setup = {
        "::setvar varp365_mm_main 3",
        "::setlevel hitpoints 60", -- the recorder's at [4959]: the blow was 12
        "::tele 0_43_43_20_43", -- 2772,2795: the tile of [4959]
    },

    run = function(t)
        local function hitpoints()
            local r, hp = t.skill.read("hitpoints")
            if r ~= "ok" or hp == nil then return nil end
            return hp.level
        end
        t.ticklog.start()
        for punch = 1, 3 do
            t.ticklog.mark("leg.punch" .. punch)
            local before = hitpoints()
            local start = tile(t)
            -- the guards patrol past the cell door; the blow lands the tick after one turns to the player
            t.expect("punch" .. punch .. "-thrown", t.await({ level = function()
                local hp = hitpoints()
                return hp ~= nil and before ~= nil and hp < before
            end, note = "a guard reaches the player and punches" }, 150))
            local after = hitpoints()
            t.check("punch" .. punch .. "-damage", before ~= nil and after ~= nil and before - after == math.floor(before / 5),
                "hitpoints " .. tostring(before) .. " -> " .. tostring(after) .. ", want a fifth: " .. tostring(before and math.floor(before / 5)))
            t.expect("punch" .. punch .. "-jailed", t.await({ level = function() return in_cell(tile(t)) end,
                note = "the cell five ticks after the blow" }, 12))
            t.ticks(8)
            t.note("punch " .. punch .. ": from " .. tostring(start and start.x) .. "," .. tostring(start and start.z))
            -- out again to a tile a recorded punch landed on: 2773,2795 ([4989]), 2771,2797 ([5089])
            if punch == 1 then t.cheat("::tele 0_43_43_21_43") end
            if punch == 2 then t.cheat("::tele 0_43_43_19_45") end
            t.ticks(2)
        end
        t.ticklog.mark("leg.end")
    end,
}
