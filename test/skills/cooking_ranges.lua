-- Ranges that offered Cook and answered nothing before 2026-11-17 (no
-- category; OSRS-Content skill_cooking/configs/cooking_sources.loc): Port
-- Phasmatys's range (ahoy_range) and a Kourend clay oven (iznot_clay_range).
local function cook_at(t, label, loc, tele)
    t.cheat("::clearinv")
    t.cheat("::give raw_shrimp 2")
    t.cheat(tele)
    t.ticks(4)
    t.player.use_on("raw_shrimp", t.player.by_symbol("loc", loc))
    t.exec(label .. ".menu", t.ui.make, "a")
    t.exec(label .. ".cooked", t.inv.await, "shrimp", 1, 15)
end

return {
    id = "cooking_ranges",
    fixture = "fresh_lumbridge.ini",
    max_frames = 30000,
    setup = {
        "::setlevel cooking 99",
    },
    run = function(t)
        t.ticks(3)
        cook_at(t, "ahoy", "ahoy_range", "::tele 0_57_54_29_11")
        cook_at(t, "iznot", "iznot_clay_range", "::tele 0_25_56_48_42")
    end,
}
