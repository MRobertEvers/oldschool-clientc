-- Bird nests' Search (skill_woodcutting/scripts/bird_nests.rs2), RSProx 1095:
-- the nest becomes an empty nest in its slot, the find lands beside it, and a
-- seed nest says "You take <seed> out of the bird's nest." (a ring nest says
-- nothing). Before 2026-11-17 Search did nothing.
local SEEDS = { "acorn", "apple_tree_seed", "banana_tree_seed", "orange_tree_seed", "willow_seed",
    "teak_seed", "curry_tree_seed", "maple_seed", "pineapple_tree_seed", "mahogany_seed",
    "papaya_tree_seed", "palm_tree_seed", "calquat_tree_seed", "yew_seed", "magic_tree_seed",
    "spirit_tree_seed", "celastrus_tree_seed", "redwood_tree_seed", "dragonfruit_tree_seed" }
local RINGS = { "gold_ring", "sapphire_ring", "emerald_ring", "ruby_ring", "diamond_ring" }
local function count(t, name)
    local _, n = t.inv.count(name)
    return n or 0
end
local function total(t, list)
    local n = 0
    for _, name in ipairs(list) do n = n + count(t, name) end
    return n
end

return {
    id = "bird_nests",
    fixture = "fresh_lumbridge.ini",
    max_frames = 20000,
    setup = {
        "::clearinv",
        "::give bird_nest_seeds_jan2019 3",
        "::give bird_nest_ring 1",
    },
    run = function(t)
        t.ticks(3)
        for i = 1, 3 do
            t.exec("seed" .. i .. ".op", t.player.inv_op, "bird_nest_seeds_jan2019", 1)
            t.ticks(2)
        end
        t.check("seed.empties", count(t, "bird_nest_empty") == 3, "empty nests " .. count(t, "bird_nest_empty"))
        t.check("seed.seeds", total(t, SEEDS) == 3, "seeds " .. total(t, SEEDS))
        t.check("seed.line", t.msg.expect("out of the bird's nest.") == "ok", "line")
        -- No line follows a ring nest, so the driver's click settle (which waits
        -- for one) is not used: the inventory checks below are the verdict.
        t.player.inv_op("bird_nest_ring", 1)
        t.ticks(2)
        t.check("ring.empty", count(t, "bird_nest_empty") == 4, "empty nests " .. count(t, "bird_nest_empty"))
        t.check("ring.ring", total(t, RINGS) == 1, "rings " .. total(t, RINGS))
    end,
}
