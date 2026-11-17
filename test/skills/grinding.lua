-- Pestle and mortar on anchovies and on a lava scale (skill_herblore
-- grind_ingredient.rs2 / grind.dbrow), RSProx 467 and 3262. Before 2026-11-17
-- the anchovies answered "Nothing interesting happens." (the pizza topping
-- binding claimed the pair) and the lava scale had no row.
local function count(t, name)
    local _, n = t.inv.count(name)
    return n or 0
end
return {
    id = "grinding",
    fixture = "fresh_lumbridge.ini",
    max_frames = 20000,
    setup = {
        "::clearinv",
        "::complete quest_druidicritual",
        "::give pestle_and_mortar 1",
        "::give anchovies 1",
        "::give lava_scale 1",
    },
    run = function(t)
        t.ticks(3)
        t.player.use_item_on_item("pestle_and_mortar", "anchovies")
        t.ticks(3)
        t.check("paste", count(t, "ii_anchovy_paste") == 1 and count(t, "anchovies") == 0, "paste " .. count(t, "ii_anchovy_paste"))
        t.check("paste.line", t.msg.expect("You grind the anchovies into a fishy, sticky paste.") == "ok", "line")
        t.player.use_item_on_item("pestle_and_mortar", "lava_scale")
        t.ticks(3)
        local n = count(t, "lava_shard")
        t.check("shards", n >= 3 and n <= 6 and count(t, "lava_scale") == 0, "shards " .. n)
        t.check("shards.line", t.msg.expect("You grind the lava dragon scale into " .. n .. " shards.") == "ok", "line")
    end,
}
