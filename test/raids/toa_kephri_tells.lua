-- toa_kephri_tells: Kephri's room for the tick log. A solo raider (kept alive with hitpoint boosts,
-- no invocations, raid level 0: the solo Entry recording's setting) goes through the teleport
-- crystal, stands east of her through the radial attacks and two specials, then `::toakephri`
-- breaks each shield in turn: two stuns run their whole length (guardians, swarms, kamikaze
-- scarabs, the wake), the third break enrages her with its dung strike, and the last `::toakephri`
-- kills her. Every tell of the room is sent at least once. The raider stands
-- where recorded throws went diagonally: (4,3) from her middle for the first
-- dung strike (thrown north-east, a line the floor rule draws wrong) and (6,-3)
-- for the enraging one (the half-way tie, thrown south-east). A bow shoots her
-- for a few ticks while shielded (the shield splat, her facing the raider) and
-- a scarab swarm in the first stun (swarms are attackable). The tick log is
-- what docs/rsprox-analysis/toa/kephri.md compares against RSProx.
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
    for _, line in ipairs(t.raid.cox_messages(60)) do
        if line:find(text, 1, true) then return true end
    end
    return false
end

return {
    id = "toa_kephri_tells",
    fixture = "fresh_lumbridge.ini",
    max_frames = 90000,
    setup = {
        "::clearinv", "::setlevel hitpoints 99", "::setlevel defence 99", "::setlevel prayer 99",
        "::setlevel ranged 99", "::give magic_shortbow", "::give rune_arrow 1000",
    },
    run = function(t)
        t.ticklog.start()
        t.exec("equip.bow", t.player.equip, "magic_shortbow")
        t.exec("equip.arrows", t.player.equip, "rune_arrow")
        t.cheat("::toa 5", false)
        wait(t, 4)
        t.ticklog.mark("kephri.crystal")
        t.expect("kephri.crystal", t.player.click_loc("kephri_teleport", 2))
        wait(t, 3)
        -- the crystal puts the raider on (25,32); her middle is (31,32)
        local _, at = t.world.tile()
        local ox, oz = at.x - 25, at.z - 32
        t.player.walk_to(ox + 35, oz + 35, 20)
        -- a few arrows at her shield: shield splats, and she turns to the raider
        local shot_from = now(t)
        t.player.attack("toa_kephri_boss_shielded", 2, 2, { quick = true })
        wait(t, 6)
        t.player.walk_to(ox + 35, oz + 34, 4)
        t.player.walk_to(ox + 35, oz + 35, 4)
        local splats, red = 0, 0
        for _, row in ipairs(rows_since(t, "hit_npc", shot_from)) do
            if row.type == 11719 and row.damage > 0 then
                if row.hitsplat == 34 then splats = splats + 1 else red = red + 1 end
            end
        end
        t.expect("kephri.shield_splat", (splats > 0 and red == 0) and "ok" or "fail",
            string.format("%d shield splats, %d other damage splats on her shield", splats, red))
        local faced = 0
        for _, row in ipairs(rows_since(t, "npc_face", shot_from)) do
            if row.type == 11719 and math.abs(row.x - (ox + 35)) <= 1 and math.abs(row.z - (oz + 35)) <= 1 then
                faced = faced + 1
            end
        end
        t.expect("kephri.faces_raider", faced > 0 and "ok" or "fail",
            string.format("%d npc_face rows at the raider's tile", faced))
        -- the radials, the first dung strike (S+29) and the first eggs (S+67)
        t.ticklog.mark("kephri.shielded")
        wait(t, 55)
        -- the first eggs are down (S+71): shoot a hatching one. A killed egg pops
        -- (spotanim_dungball_explosion01 2166, sound 6479) and is gone that tick,
        -- with no animation of its own.
        local egg_from = now(t)
        t.player.attack("kephri_egg_hatch", 2, 2, { quick = true })
        wait(t, 10)
        local died, popped, sounded, anims = nil, 0, 0, 0
        for _, row in ipairs(rows_since(t, "npc_free", egg_from)) do
            if row.type == 11729 and not died then died = row end
        end
        if died then
            for _, row in ipairs(rows_since(t, "map_spotanim", died.tick)) do
                if row.tick == died.tick and row.spotanim == 2166 and row.coord == died.coord then popped = popped + 1 end
            end
            for _, row in ipairs(rows_since(t, "sound", died.tick)) do
                if row.tick == died.tick and row.sound == 6479 then sounded = sounded + 1 end
            end
            for _, row in ipairs(rows_since(t, "npc_anim", egg_from)) do
                if row.slot == died.slot and row.tick >= died.tick - 2 then anims = anims + 1 end
            end
        end
        -- the tick log has a sound row only for a raider inside its range (5)
        local near = false
        if died then
            local ex, ez = (died.coord >> 14) & 0x3fff, died.coord & 0x3fff
            local _, at = t.world.tile()
            near = math.max(math.abs(at.x - ex), math.abs(at.z - ez)) <= 5
        end
        t.expect("kephri.egg_killed",
            (died and popped > 0 and (sounded > 0 or not near) and anims == 0) and "ok" or "fail",
            string.format("egg freed %s, pop %d, sound %d (raider in range: %s), anims %d",
                died and tostring(died.tick) or "never", popped, sounded, tostring(near), anims))
        wait(t, 9)
        for n = 1, 2 do
            t.ticklog.mark("kephri.break" .. n)
            t.cheat("::toakephri", false)
            if n == 1 then
                -- a swarm is out from D + 5; shoot one
                wait(t, 12)
                local swarm_from = now(t)
                local ar, ad = t.player.attack("toa_kephri_shield_scarab", 2, 2, { quick = true })
                wait(t, 6)
                t.player.walk_to(ox + 35, oz + 34, 4)
                local hits = 0
                for _, row in ipairs(rows_since(t, "hit_npc", swarm_from)) do
                    if row.type == 11723 then hits = hits + 1 end
                end
                t.expect("kephri.swarm_attackable", hits > 0 and "ok" or "fail",
                    string.format("attack %s (%s); %d hits on swarms", tostring(ar), tostring(ad), hits))
                wait(t, 42)
            else
                wait(t, 60)
            end
            t.ticklog.mark("kephri.woke" .. n)
            wait(t, 25)
        end
        t.player.walk_to(ox + 37, oz + 29, 20)
        wait(t, 2)
        t.ticklog.mark("kephri.enrage")
        t.cheat("::toakephri", false)
        wait(t, 50)
        t.ticklog.mark("kephri.kill")
        t.cheat("::toakephri", false)
        wait(t, 8)
        if said(t, "Challenge complete: Kephri") then
            t.expect("kephri.run", "ok", string.format("complete on tick %d", now(t)))
        else
            t.expect("kephri.run", "fail", "Kephri's room did not complete")
        end
    end,
}
