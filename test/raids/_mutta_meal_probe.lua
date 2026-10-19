-- _mutta_meal_probe: the small Muttadile's way to the meat tree, read off the
-- server's tick log against RSProx (docs/rsprox-analysis/cox/muttadiles.md,
-- "Hitpoints and meals"). The tree is never chopped: the raider shoots the
-- small one with the blowpipe until it has eaten, and the probe reports where
-- it stood for each bite.
--
-- RSProx 3285, 843 and 820 (the cw room, tree south-west tile at template
-- (22, 5)): on the tick the hit takes it to half it faces the tree and walks to
-- it a tile a tick with the raider's hits landing, and bites three times on
-- consecutive ticks from the tick after its last step, touching the tree: the
-- small one's 3x3 at (19, 5) / (19, 4), the large one's 5x5 at (21, 7).
-- (Before the Muttadiles were retaliate=no, the engine's latch took the hitter
-- as target on the first hit of the walk and it went after the raider.)
--
--   ./src/<objdir>/torirsserver --scriptrun test/raids/_mutta_meal_probe.lua --bots 1 \
--       --name meal0 --ticks 1500 --session /tmp/meal --fixture test/raids/fixtures/fresh_lumbridge.ini
local SMALL, TREE = 7562, 7564

local function coord_of(c)
    return (c >> 14) & 0x3fff, c & 0x3fff
end

return {
    id = "_mutta_meal_probe",
    fixture = "fresh_lumbridge.ini",
    max_frames = 400000,
    setup = {
        "::setlevel hitpoints 99", "::setlevel defence 99", "::setlevel ranged 99", "::setlevel prayer 99",
        "::clearinv", "::blowpipe dragon_dart 2000 2000", "::wield toxic_blowpipe_loaded", "::god 1",
    },
    run = function(t)
        t.ticklog.start()
        local n = t.raid.cox_run_n()
        local er, ed = t.raid.cox_test_enter("muttadiles", n)
        t.check("meal.enter", er == "ok", tostring(er) .. " " .. tostring(ed and ed.seed))
        if er ~= "ok" then t.finish(1) return end
        t.ticks(2)
        -- in through the tendrils, as the room solver does (cox2_muttadiles MU.enter)
        for _ = 1, 3 do
            t.raid.tob_loc_op("raids_dogodile_tendrils_entrance", 1, nil, 40)
            t.ticks(8)
        end
        local ate = false
        local refused
        for _ = 1, 90 do
            local nr, mu = t.npc.nearest("raids_dogodile_junior", 40)
            if nr ~= "ok" then break end
            -- (the entry leaves me outside its reach: walk in to a few tiles off it)
            for _, d in ipairs({ { 0, -5 }, { -5, 0 }, { 5, 0 }, { 0, 5 }, { -4, -4 }, { 4, 4 } }) do
                if t.player.walk_to(mu.x + d[1], mu.z + d[2], 30) == "ok" then break end
            end
            local ar, ad = t.player.attack("raids_dogodile_junior", 2, 10)
            if ar ~= "ok" and refused == nil then
                refused = tostring(ar) .. " " .. tostring(ad)
                t.ticks(1)
            end
            local _, heals = t.ticklog.rows({ kind = "npc_heal" })
            for _, h in ipairs(heals or {}) do
                if h.type == SMALL and h.amount >= 10 then ate = true end
            end
            if ate then t.ticks(20) break end
        end
        local _, spawns = t.ticklog.rows({ kind = "npc_spawn" })
        t.ticks(1)
        local slot, tx, tz
        for _, s in ipairs(spawns or {}) do
            if s.type == SMALL then slot = s.slot end
            if s.type == TREE then tx, tz = coord_of(s.coord) end
        end
        local _, tiles = t.ticklog.rows({ kind = "npc_tile" })
        t.ticks(1)
        local _, heals = t.ticklog.rows({ kind = "npc_heal" })
        t.ticks(1)
        local _, faces = t.ticklog.rows({ kind = "npc_face" })
        t.ticks(1)
        local at = {}
        for _, r in ipairs(tiles or {}) do
            if r.slot == slot then at[#at + 1] = r end
        end
        local function tile_at(tick)
            local best
            for _, r in ipairs(at) do
                if r.tick <= tick then best = r end
            end
            return best
        end
        local bites = {}
        for _, h in ipairs(heals or {}) do
            if h.slot == slot and h.amount >= 10 then
                local r = tile_at(h.tick)
                bites[#bites + 1] = string.format("t%d +%d at %s", h.tick, h.amount,
                    r and string.format("%d,%d (tree-dx %d, tree-dz %d)", r.x, r.z, r.x - (tx or 0), r.z - (tz or 0)) or "spawn")
            end
        end
        local turned = {}
        for _, f in ipairs(faces or {}) do
            if f.slot == slot and tx and f.x == tx and f.z == tz then turned[#turned + 1] = "t" .. f.tick end
        end
        t.check("meal.attack", refused == nil, tostring(refused))
        t.check("meal.tree", tx ~= nil, string.format("tree at %s,%s; faced it %s", tostring(tx), tostring(tz),
            table.concat(turned, " ")))
        t.check("meal.bites", #bites > 0, table.concat(bites, "; "))
        t.finish(#bites > 0 and 0 or 1)
    end,
}
