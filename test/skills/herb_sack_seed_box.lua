-- The herb sack and the seed box (skill_herblore/scripts/herb_sack.rs2,
-- skill_farming/scripts/seed_box.rs2), RSProx lines cited there. Before
-- 2026-11-17 none of their ops did anything.
local function count(t, name)
    local _, n = t.inv.count(name)
    return n or 0
end
local function line(t, label, text)
    t.check(label, t.msg.expect(text) == "ok", text)
end

return {
    id = "herb_sack_seed_box",
    fixture = "fresh_lumbridge.ini",
    max_frames = 30000,
    setup = {
        "::clearinv",
        "::give slayer_herb_sack 1",
        "::give seed_box 1",
        "::give unidentified_ranarr 3",
        "::give unidentified_torstol 2",
        "::give unidentified_guam 1",
        "::give ranarr_seed 5",
        "::give potato_seed 10",
    },
    run = function(t)
        t.ticks(3)
        -- Herb sack
        t.player.inv_op("slayer_herb_sack", 1)
        t.ticks(2)
        t.check("sack.fill", count(t, "unidentified_ranarr") + count(t, "unidentified_torstol") + count(t, "unidentified_guam") == 0,
            "herbs left in pack " .. tostring(count(t, "unidentified_ranarr") + count(t, "unidentified_torstol")))
        line(t, "sack.fill.line", "You add the herbs to your sack.")
        t.player.inv_op("slayer_herb_sack", 3)
        t.ticks(2)
        line(t, "sack.check.head", "You look in your herb sack and see:")
        line(t, "sack.check.row", "3 x Grimy ranarr weed")
        t.player.inv_op("slayer_herb_sack", 1)
        t.ticks(2)
        line(t, "sack.none", "You have no grimy herbs in your inventory that can be stored in the sack.")

        -- Open: a herb picked up goes into the sack
        t.player.inv_op("slayer_herb_sack", 2)
        t.ticks(2)
        t.check("sack.open", count(t, "slayer_herb_sack_open") == 1, "open sack " .. count(t, "slayer_herb_sack_open"))
        t.cheat("::give unidentified_irit 1")
        t.ticks(2)
        t.player.drop("unidentified_irit")
        t.ticks(3)
        t.player.click_obj("unidentified_irit", 3)
        t.ticks(4)
        t.check("sack.pickup", count(t, "unidentified_irit") == 0, "irit in pack " .. count(t, "unidentified_irit"))
        line(t, "sack.pickup.line", "You put the Grimy irit leaf herb into your herb sack.")

        -- Empty
        t.player.inv_op("slayer_herb_sack_open", 4)
        t.ticks(2)
        t.check("sack.empty", count(t, "unidentified_ranarr") == 3 and count(t, "unidentified_irit") == 1,
            "ranarr " .. count(t, "unidentified_ranarr") .. ", irit " .. count(t, "unidentified_irit"))
        line(t, "sack.empty.line", "Emptied 3 x Grimy ranarr weed to your inventory.")

        -- Seed box
        t.player.inv_op("seed_box", 1)
        t.ticks(2)
        t.check("box.fill", count(t, "ranarr_seed") == 0 and count(t, "potato_seed") == 0,
            "seeds left " .. count(t, "ranarr_seed") .. "/" .. count(t, "potato_seed"))
        line(t, "box.fill.line", "Stored 10 x Potato seed in your seed box.")
        t.player.inv_op("seed_box", 2)
        t.ticks(2)
        t.check("box.empty", count(t, "ranarr_seed") == 5 and count(t, "potato_seed") == 10,
            "seeds back " .. count(t, "ranarr_seed") .. "/" .. count(t, "potato_seed"))
        line(t, "box.empty.line", "Emptied 5 x Ranarr seed to your inventory.")
        t.player.inv_op("seed_box", 2)
        t.ticks(2)
        line(t, "box.already", "Your seed box is already empty.")
    end,
}
