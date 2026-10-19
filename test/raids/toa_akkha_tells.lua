-- toa_akkha_tells: Akkha's room for the tick log. A solo raider (kept alive with hitpoint boosts, a
-- bow, no invocations: the solo Entry recording's setting) goes through the teleport crystal and
-- lets the fight run: six swings and the trailing orbs (walking through them, so orbs are made),
-- then `::toaakkhanext` takes him to each threshold and fells the shadow guarding his quadrant,
-- with the hourglasses, a slam and a Memory Blast running between. After the fourth threshold the
-- final stand: the heal, the corners, three hits for a swap, the unstable orbs, then the kill.
-- The tick log is what docs/rsprox-analysis/toa/akkha.md compares against RSProx.
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
local function said(t, text)
    for _, line in ipairs(t.raid.cox_messages(80)) do
        if line:find(text, 1, true) then return true end
    end
    return false
end
-- The room as `::toaakkha` prints it.
local function ak(t)
    t.cheat("::toaakkha", false)
    t.ticks(1)
    local st = { state = -1, phase = -1, dead = -1, hp = -1, charge = -1, immune = -1 }
    for _, line in ipairs(t.raid.cox_messages(20)) do
        local s, p, d, h = line:match("^toaakkha state (%-?%d+) phase (%-?%d+) dead (%-?%d+) hp (%-?%d+)")
        if s then st.state, st.phase, st.dead, st.hp = tonumber(s), tonumber(p), tonumber(d), tonumber(h) end
        local c, im = line:match("^toaakkha charge (%-?%d+) immune (%-?%d+)")
        if c then st.charge, st.immune = tonumber(c), tonumber(im) end
    end
    return st
end
-- Fell shadows until he can be hurt.
local function unguard(t)
    for _ = 1, 4 do
        if ak(t).immune ~= 1 then return end
        t.cheat("::toaakkhanext", false)
        wait(t, 3)
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

return {
    id = "toa_akkha_tells",
    fixture = "fresh_lumbridge.ini",
    max_frames = 120000,
    setup = {
        "::clearinv", "::setlevel hitpoints 99", "::setlevel defence 99", "::setlevel prayer 99",
        "::setlevel ranged 99", "::give magic_shortbow", "::give rune_arrow 1000",
    },
    run = function(t)
        t.ticklog.start()
        t.exec("equip.bow", t.player.equip, "magic_shortbow")
        t.exec("equip.arrows", t.player.equip, "rune_arrow")
        t.cheat("::toa 7", false)
        wait(t, 4)
        t.ticklog.mark("akkha.crystal")
        local _, before = t.world.tile()
        t.expect("akkha.crystal", t.player.click_loc("akkha_teleport", 2))
        local at = before
        for _ = 1, 12 do
            wait(t, 1)
            local _, now_at = t.world.tile()
            at = now_at
            if before.x - at.x >= 4 then break end
        end
        -- the crystal puts the raider on (41,32)
        local ox, oz = at.x - 41, at.z - 32
        wait(t, 2)
        t.expect("akkha.started", said(t, "Challenge started: Akkha") and "ok" or "fail")
        local start = now(t)

        -- a few arrows at his melee form (he prays melee: ranged lands)
        t.player.attack("akkha_melee", 2, 2, { quick = true })
        wait(t, 12)
        local landed = 0
        for _, row in ipairs(rows_since(t, "hit_npc", start)) do
            if row.type == 11790 and row.damage > 0 then landed = landed + 1 end
        end
        t.expect("akkha.ranged_lands", landed > 0 and "ok" or "fail", string.format("%d ranged hits landed", landed))
        -- the HUD's damage counters: the raid's total and this room's, both counting his hits
        local _, done = t.var.server("varb14325_toa_damage_done")
        local _, room = t.var.server("varb14374_toa_damage_done_current")
        t.expect("akkha.damage_counters", ((tonumber(done) or 0) > 0 and tonumber(room) == tonumber(done)) and "ok" or "fail",
            string.format("done %s, this room %s", tostring(done), tostring(room)))

        -- the trailing orbs (S + 42): walk a loop through the window so orbs are made behind
        wait(t, 30)
        t.ticklog.mark("akkha.trail")
        for k = 1, 4 do
            t.player.walk_to(ox + 36 + (k % 2) * 3, oz + 33 + (k % 2) * 2, 4)
            wait(t, 3)
        end
        wait(t, 6)
        local orbs = 0
        for _, row in ipairs(rows_since(t, "npc_spawn", start)) do
            if row.type >= 11800 and row.type <= 11803 then orbs = orbs + 1 end
        end
        t.expect("akkha.trail_orbs", orbs > 0 and "ok" or "fail", string.format("%d trailing orbs", orbs))

        -- four thresholds: shadows, then the guard of his quadrant felled
        for k = 1, 4 do
            t.ticklog.mark("akkha.threshold" .. k)
            -- (a Memory Blast takes him out of reach: try again until he is back)
            local st = ak(t)
            for _ = 1, 12 do
                if st.phase >= k then break end
                unguard(t)
                t.cheat("::toaakkhanext", false)
                wait(t, 3)
                st = ak(t)
            end
            t.expect("akkha.threshold" .. k, st.phase == k and "ok" or "fail",
                string.format("phase %d dead %d hp %d", st.phase, st.dead, st.hp))
            if k == 1 then
                t.expect("akkha.shadows", said(t, "Shadows appear throughout the room!") and "ok" or "fail")
                -- a detonation (a party's special, cast here by hand): the mark, then the plus
                t.ticklog.mark("akkha.detonate")
                local det_from = now(t)
                t.cheat("::toaakkhaspecial 0", false)
                wait(t, 12)
                local blasts = 0
                local sounded = 0
                for _, row in ipairs(rows_since(t, "map_spotanim", det_from)) do
                    -- the plus lands at C + 10 at path level 0, delay 0 (a flood's tiles are 30+)
                    if row.spotanim >= 2256 and row.spotanim <= 2259 and row.delay == 0 then blasts = blasts + 1 end
                end
                for _, row in ipairs(rows_since(t, "sound", det_from)) do
                    if row.sound == 156 or row.sound == 5591 or row.sound == 3887 or row.sound == 173 then sounded = sounded + 1 end
                end
                t.expect("akkha.detonate", (said(t, "You have been marked for detonation!") and blasts > 0 and sounded > 0) and "ok" or "fail",
                    string.format("%d plus tiles, %d element sounds", blasts, sounded))
                -- the hourglasses fill, and a slam floods (50 steps)
                wait(t, 56)
            elseif k == 2 then
                -- the Memory Blast comes round in here
                wait(t, 40)
            else
                wait(t, 6)
            end
        end
        unguard(t)

        t.ticklog.mark("akkha.final")
        t.cheat("::toaakkhanext", false)
        wait(t, 3)
        t.expect("akkha.final_stand", said(t, "Akkha makes his final stand!") and "ok" or "fail")
        wait(t, 10)
        for _ = 1, 3 do
            t.cheat("::toaakkhanext", false)
            wait(t, 2)
        end
        wait(t, 12)
        local unstable = 0
        for _, row in ipairs(rows_since(t, "npc_spawn", start)) do
            if row.type == 11804 then unstable = unstable + 1 end
        end
        t.expect("akkha.unstable_orbs", unstable > 0 and "ok" or "fail", string.format("%d unstable orbs", unstable))
        t.ticklog.mark("akkha.kill")
        t.cheat("::toaakkhakill", false)
        wait(t, 8)
        if said(t, "Challenge complete: Akkha") then
            t.expect("akkha.run", "ok", string.format("complete on tick %d", now(t)))
        else
            t.expect("akkha.run", "fail", "Akkha's room did not complete")
        end
    end,
}
