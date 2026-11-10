-- yama: a real Yama fight for the wire audit (docs/rsprox-analysis/yama/README.md,
-- the tick log diffed against RSProx index 4498). Solo: into the domain by ::yamaenter
-- (signed contract, the Voice and the throne's pages are the access side's tests), the
-- challenge straight on, a bow at him, phase edges by ::yamahp. Run:
--   TORIRSSERVER_SCRIPTS=<pack> TORIRSSERVER_PACK_DIR=<servpack> \
--     ./src/build_yama_combat_opt/torirsserver --scriptrun test/bosses/yama.lua --name y0 --ticks 4000 \
--     --session <dir> --fixture test/bosses/fixtures/fresh_lumbridge.ini
local function now(t) local _, k = t.tick() return k or -1 end

local function heal(t) t.cheat("::boost hitpoints 255 0", false) end

local function wait(t, n)
    local left = n
    while left > 0 do
        local step = math.min(left, 5)
        t.ticks(step)
        heal(t)
        left = left - step
    end
end

local function yama_row(t)
    local r, row = t.npc.nearest("yama", 40)
    if r == "ok" then return row end
    return nil
end

return {
    id = "yama",
    fixture = "fresh_lumbridge.ini",
    max_frames = 80000,
    setup = {
        "::clearinv", "::setlevel ranged 99", "::setlevel hitpoints 99", "::setlevel prayer 99",
        "::setlevel defence 99", "::setlevel magic 99",
        "::give magic_shortbow", "::give rune_arrow 5000",
    },
    run = function(t)
        t.ticklog.start()
        t.exec("equip.bow", t.player.equip, "magic_shortbow")
        t.exec("equip.arrows", t.player.equip, "rune_arrow")
        t.cheat("::yamaenter", false)
        wait(t, 8)
        local _, tile = t.world.tile()
        local bx, bz = tile.x - tile.x % 64, tile.z - tile.z % 64
        t.ticklog.mark("entered")
        t.player.walk_to(bx + 31, bz + 40, 2)
        wait(t, 12)
        t.cheat("::yamachallenge", false)
        t.ticklog.mark("challenge")
        wait(t, 6)
        t.player.attack("yama", 2, 1)
        wait(t, 8)
        t.check("yama.up", yama_row(t) ~= nil, "yama in view")
        -- Phase 1 by bow, then the Judge, phase 2, the Judge, phase 3 to the kill.
        local deadline = now(t) + 2600
        local hp_steps = { {60, 1650}, {480, 820}, {780, 300}, {980, 40}, {1060, 2} }
        local killed = false
        local started = now(t)
        local next_judge_try = 0
        local judge_seen = nil
        -- The north-east, north-west and east glyphs' middle tiles.
        local glyph_visits = { { at = 30, dx = 36, dz = 37 }, { at = 240, dx = 26, dz = 37 },
            { at = 560, dx = 38, dz = 30 } }
        while now(t) < deadline do
            heal(t)
            local age = now(t) - started
            for _, s in ipairs(hp_steps) do
                if age >= s[1] and not s.done then s.done = true; t.cheat("::yamahp " .. s[2], false) end
            end
            -- Onto a glyph's middle tile now and then (the north-east one).
            for _, g in ipairs(glyph_visits) do
                if age >= g.at and not g.done and yama_row(t) then
                    g.done = true
                    t.player.walk_to(bx + g.dx, bz + g.dz, 0)
                end
            end
            if age >= 1200 and not killed then
                killed = true
                t.cheat("::yamakill", false)
            end
            local r = t.npc.nearest("yama_voidflare", 30)
            if t.npc.nearest("yama_judge_of_yama", 60) == "ok" then
                -- The bot does not hop the stones: the Judge's death by cheat, his
                -- attacks and surges are on the wire regardless.
                judge_seen = judge_seen or now(t)
                if now(t) >= judge_seen + 45 and now(t) >= next_judge_try then
                    next_judge_try = now(t) + 20
                    t.cheat("::yamajudgekill", false)
                end
                t.ticks(2)
            elseif r == "ok" then
                judge_seen = nil
                t.player.attack("yama_voidflare", 2, 1)
            elseif yama_row(t) then
                t.player.attack("yama", 2, 1)
            end
            t.ticks(1)
        end
        t.check("yama.killed", killed, "kill sequence reached")
        t.expect("yama.run", "ok", string.format("ended on tick %d", now(t)))
    end,
}
