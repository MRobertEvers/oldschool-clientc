-- kourend_pickpocket: Great Kourend's men, women and town guards offer
-- Pickpocket in the cache and did nothing before 2026-11-17 (no binding:
-- tools/rsprox/op_coverage.py static Pickpocket). They are the Man, Woman and
-- Guard rows (pickpocket.dbrow header; the wiki's Thieving table).
local function try(t, label, npc, pocket)
    local seen = false
    for _ = 1, 8 do
        t.player.talk_to(npc, 3)
        t.ticks(6)
        if t.msg.expect("You pick the " .. pocket .. "'s pocket.") == "ok" then
            seen = true
            break
        end
    end
    t.check(label, seen, npc .. (seen and ": picked" or ": never \"You pick the " .. pocket .. "'s pocket.\""))
end

return {
    id = "kourend_pickpocket",
    fixture = "fresh_lumbridge.ini",
    max_frames = 40000,
    setup = {
        "::setlevel thieving 99",
        "::setlevel hitpoints 99",
    },
    run = function(t)
        t.ticks(2)
        t.cheat("::goto 1741 3615 0")
        t.ticks(3)
        try(t, "zeah_man", "zeah_man", "man")
        t.cheat("::goto 1728 3614 0")
        t.ticks(3)
        try(t, "zeah_woman", "zeah_woman", "woman")
        t.cheat("::goto 1769 3632 0")
        t.ticks(3)
        try(t, "hos_town_guard", "hos_town_guard_01", "guard")
    end,
}
