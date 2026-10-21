-- toa_invocations_tells: the invocations that change a raid outside any one
-- room's script, solo in Kephri's room (docs/rsprox-analysis/toa/invocations.md):
--   Dehydration      nectar refused: "You've been prevented from consuming potions
--                    within the Tombs of Amascut." (3431, 553), the dose kept
--   Deadly Prayers   a fifth of every hit off prayer, on the hit's tick (1852)
--   Overly Draining  a special attack spends the whole bar (wiki; 1751, 1757)
--   Lively Larvae    4-6 hatching eggs solo (14 casts in 1741, 1743, 1828, ...)
--   Medic!           a swarm at S+18 and S+30 before her first break, crawling a
--                    tile every 2 ticks and absorbed 14 after it rose (1741, 554)
local function now(t) local _, k = t.tick() return k or -1 end
local function keep_alive(t) t.cheat("::boost hitpoints 255 0", false) end
local function wait(t, n)
    local left = n
    while left > 0 do
        local step = math.min(left, 5)
        t.ticks(step)
        keep_alive(t)
        left = left - step
    end
end
local function rows_since(t, kind, from)
    local out = {}
    local _, rows = t.ticklog.rows({ kind = kind })
    for _, row in ipairs(rows or {}) do
        if row.tick >= from then out[#out + 1] = row end
    end
    return out
end
local function said(t, text)
    for _, line in ipairs(t.raid.cox_messages(80)) do
        if line:find(text, 1, true) then return true end
    end
    return false
end

return {
    id = "toa_invocations_tells",
    fixture = "fresh_lumbridge.ini",
    max_frames = 60000,
    setup = {
        "::clearinv", "::setlevel hitpoints 99", "::setlevel defence 99", "::setlevel prayer 99",
        "::setlevel ranged 99", "::give magic_shortbow", "::give rune_arrow 1000",
        "::give toa_supply_heal_4",
    },
    run = function(t)
        t.ticklog.start()
        t.exec("equip.bow", t.player.equip, "magic_shortbow")
        t.exec("equip.arrows", t.player.equip, "rune_arrow")
        -- `::toa` enters with none; in the raid `::toainv` turns them on: Deadly
        -- Prayers (17), Dehydration (19), Overly Draining (20), Lively Larvae (21),
        -- Medic! (24).
        t.cheat("::toa 5", false)
        wait(t, 4)
        for _, i in ipairs({ 17, 19, 20, 21, 24 }) do
            t.cheat("::toainv " .. i, false)
        end
        wait(t, 2)

        local _, doses = t.inv.count("toa_supply_heal_4")
        t.player.inv_op("toa_supply_heal_4", 1)
        wait(t, 2)
        local _, after = t.inv.count("toa_supply_heal_4")
        t.expect("dehydration.refused", said(t, "You've been prevented from consuming potions within the Tombs of Amascut.") and doses == after and "ok" or "fail",
            string.format("nectar %s -> %s", tostring(doses), tostring(after)))

        -- Through the crystal (S), east of her.
        local _, before = t.world.tile()
        t.expect("kephri.crystal", t.player.click_loc("kephri_teleport", 2))
        local _, at = t.world.tile()
        for _ = 1, 12 do
            wait(t, 1)
            local _, now_at = t.world.tile()
            at = now_at
            if at.x - before.x >= 8 then break end
        end
        local S = now(t)
        local ox, oz = at.x - 25, at.z - 32
        t.player.walk_to(ox + 35, oz + 35, 20)

        -- Overly Draining: the magic shortbow's 55% costs the whole bar.
        t.ui.tab("combat")
        t.ticks(2)
        local _, w = t.ui.widget("combat_interface:special_attack")
        local sr, sd = t.ui.invoke(w, 1)
        sr = tostring(sr) .. " " .. tostring(sd) .. " w=" .. tostring(w)
        t.ticks(2)
        local _, armed = t.var.varp("varp301_sa_attack")
        local _, e0 = t.var.varp("varp300_sa_energy")
        t.player.attack("toa_kephri_boss_shielded", 2, 2, { quick = true })
        wait(t, 4)
        local _, e1 = t.var.varp("varp300_sa_energy")
        t.expect("overly_draining.cost", e0 == 1000 and e1 == 0 and "ok" or "fail", string.format("energy %s -> %s (special %s, armed %s)", tostring(e0), tostring(e1), tostring(sr), tostring(armed)))
        t.player.walk_to(ox + 35, oz + 35, 4)

        -- Medic! and Lively Larvae: her first special eggs (S+67) and the swarms before it.
        wait(t, 75)
        local swarms = {}
        for _, row in ipairs(rows_since(t, "npc_spawn", S - 2)) do
            if row.type == 11723 then swarms[#swarms + 1] = row end
        end
        local offs = {}
        for _, row in ipairs(swarms) do offs[#offs + 1] = tostring(row.tick - S) end
        local gap = (#swarms >= 2) and (swarms[2].tick - swarms[1].tick) or -1
        t.expect("medic.swarms", (#swarms >= 2 and gap == 12) and "ok" or "fail", "at S+" .. table.concat(offs, ",S+"))
        -- the first one's steps: a tile every 2 ticks, absorbed 14 after it rose
        local moves, absorbed = {}, -1
        if swarms[1] then
            local slot, born = swarms[1].slot, swarms[1].tick
            local last = nil
            for _, row in ipairs(rows_since(t, "npc_tile", born)) do
                if row.slot == slot and row.tick <= born + 16 then
                    local here = tostring(row.coord or (tostring(row.x) .. "," .. tostring(row.z)))
                    if (last == nil and row.tick > born) or (last and here ~= last) then moves[#moves + 1] = tostring(row.tick - born) end
                    last = here
                end
            end
            for _, row in ipairs(rows_since(t, "npc_anim", born)) do
                if row.slot == slot and row.seq == 9607 and absorbed < 0 then absorbed = row.tick - born end
            end
        end
        local steady = #moves >= 3
        for i = 2, #moves do
            if tonumber(moves[i]) - tonumber(moves[i - 1]) ~= 2 then steady = false end
        end
        t.expect("medic.crawl", steady and absorbed == 14 and "ok" or "fail",
            "steps at +" .. table.concat(moves, ",+") .. " absorbed +" .. tostring(absorbed))
        local hatch = 0
        for _, row in ipairs(rows_since(t, "npc_spawn", S)) do
            if row.type == 11729 then hatch = hatch + 1 end
        end
        t.expect("larvae.hatching", (hatch >= 3 and hatch <= 6) and "ok" or "fail", tostring(hatch) .. " hatching eggs")

        -- Deadly Prayers: no prayer was on, so every point gone is the hits'.
        local want = 0
        local dmg = 0
        for _, row in ipairs(rows_since(t, "hit_player", S - 2)) do
            if type(row.damage) == "number" and row.damage > 0 then
                want = want + math.floor(row.damage / 5)
                dmg = dmg + row.damage
            end
        end
        local _, ps = t.skill.read("prayer")
        local prayer = ps and ps.level
        t.expect("deadly_prayers.drain", (dmg > 0 and 99 - (prayer or 99) == want) and "ok" or "fail",
            string.format("%d damage, prayer 99 -> %s, want -%d", dmg, tostring(prayer), want))
        t.cheat("::toaout", false)
        t.ticks(2)
    end,
}
