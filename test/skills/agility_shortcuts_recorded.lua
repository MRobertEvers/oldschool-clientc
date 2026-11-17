-- Agility shortcuts that offered their op and did nothing before 2026-11-17
-- (skill_agility/scripts/shortcuts_recorded.rs2,
-- quest_losttribe/scripts/losttribe_hole.rs2): each lands where its
-- recording lands.
local function at(t)
    local _, tile = t.world.tile()
    return tile
end
local function cross(t, label, tele, loc, op, want)
    if tele then
        t.cheat("::tele " .. tele)
        t.ticks(3)
    end
    t.player.click_loc(loc, op)
    t.ticks(9)
    local tile = at(t)
    local ok = tile ~= nil and tile.x == want[1] and tile.z == want[2] and (want[3] == nil or tile.level == want[3])
    t.check(label, ok, "at " .. tostring(tile and tile.x) .. "," .. tostring(tile and tile.z) .. "," .. tostring(tile and tile.level))
end

return {
    id = "agility_shortcuts_recorded",
    fixture = "fresh_lumbridge.ini",
    max_frames = 40000,
    setup = {
        "::setlevel agility 99",
        "::setlevel hitpoints 99",
        "::complete quest_losttribe",
    },
    run = function(t)
        t.ticks(3)
        cross(t, "taverley.up", "0_45_153_6_31", "taverley_dragon_jumpup", 1, { 2888, 9823, 1 })
        cross(t, "taverley.down", nil, "taverley_dragon_jumpdown", 1, { 2886, 9823, 0 })
        cross(t, "zul.east", "0_33_48_42_0", "snakeboss_steppingstone", 1, { 2160, 3072 })
        cross(t, "zul.west", nil, "snakeboss_steppingstone", 1, { 2154, 3072 })
        cross(t, "anglers.north", "0_38_42_43_38", "anglers_retreat_shortcut", 1, { 2475, 2732 })
        cross(t, "crevice.in", "3_28_68_60_54", "dagannoth_crevice", 1, { 1862, 4406, 1 })
        cross(t, "crevice.out", nil, "dagannoth_crevice", 1, { 1852, 4406, 3 })
        cross(t, "losttribe.north", "0_50_150_24_0", "lost_tribe_hole_2", 1, { 3224, 9604 })
        cross(t, "losttribe.south", nil, "lost_tribe_hole_2", 1, { 3224, 9600 })
    end,
}
