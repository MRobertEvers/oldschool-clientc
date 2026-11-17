-- zulrah: one long fight for the wire audit (docs/rsprox-analysis/zulrah/FIGHT.md),
-- its tick log diffed against RSProx index 3259's first fight [8318..8847]:
-- rotation 4 to its end, then rotation 2 from its phase 2 (::zulrahnext 2 stands
-- in for the roll). Into the shrine by ::zulrahrotation (the quick-board), the
-- arrival box continued; the player walks to the recorder's tile at each
-- surfacing (attempt 1's tile ten ticks into each phase), prays against the
-- form (green: missiles, tanzanite: magic), is healed by cheat, and does not
-- attack (auto-retaliate off) until rise + 525 -- but for one snakeling shot
-- from rise + 175, for the killed snakeling's blocks (the recorder killed 43 of
-- 48). Then ::zulrahhp 1 and a bow for the death (D, D+6, the session's kill
-- lines).
--
--   TORIRSSERVER_DEV=1 TORIRSSERVER_SCRIPTS=<pack dir> TORIRSSERVER_PACK_DIR=<servpack> \
--     ./src/build_zulrah_opt/torirsserver --scriptrun test/bosses/zulrah.lua --bots 1 --name z0 \
--     --ticks 2400 --session <dir> --fixture test/bosses/fixtures/fresh_lumbridge.ini

local FORMS = { "snakeboss_boss_ranged", "snakeboss_boss_melee", "snakeboss_boss_magic" }

-- Where the recorder stood ten ticks into each phase (index 3259 attempt 1),
-- template tiles; phase 1 is the run to the north-east tip.
local ROUTE = {
    { 2272, 3078 }, { 2272, 3078 }, { 2265, 3070 }, { 2263, 3073 }, { 2264, 3072 },
    { 2273, 3073 }, { 2272, 3071 }, { 2264, 3073 }, { 2264, 3072 }, { 2266, 3070 },
    { 2273, 3072 }, { 2274, 3074 }, { 2274, 3074 }, { 2264, 3074 }, { 2264, 3078 },
    { 2264, 3070 }, { 2264, 3072 }, { 2272, 3078 }, { 2273, 3073 }, { 2269, 3070 },
}

local function now(t) local _, k = t.tick() return k or -1 end
local function heal(t) t.cheat("::boost hitpoints 255 0", false) end

local function boss(t)
    for i, sym in ipairs(FORMS) do
        local r, row = t.npc.nearest(sym, 40)
        if r == "ok" then return i, sym, row end
    end
    return nil, nil, nil
end

return {
    id = "zulrah",
    fixture = "fresh_lumbridge.ini",
    max_frames = 400000,
    setup = {
        "::setlevel hitpoints 99", "::setlevel prayer 99", "::setlevel defence 99",
        "::setlevel ranged 99", "::setlevel magic 99",
        "::setvar varp172_option_nodef 1",
        "::give magic_shortbow 1", "::give rune_arrow 1000",
    },
    run = function(t)
        t.check("zulrah.ticklog", t.ticklog.start() == "ok", "")
        t.player.equip("magic_shortbow")
        t.player.equip("rune_arrow")
        t.cheat("::zulrahrotation 4", false)
        -- The row: the move into the copy, then the box to continue.
        for _ = 1, 20 do
            local r, tl = t.world.tile()
            if r == "ok" and tl.x > 6000 then break end
            t.ticks(1)
        end
        t.ticks(2)
        t.chat.continue_()
        local present = t.npc.await_present("snakeboss_boss_ranged", 30, 8)
        t.check("zulrah.rise", present == "ok", "Zulrah rose: " .. tostring(present))
        t.cheat("::zulrahnext 2", false)
        local rise = now(t)
        local _, arrive = t.world.tile()
        -- The arrival tile is 2268,3068.
        local ox, oz = arrive.x - 2268, arrive.z - 3068
        local function go(i)
            local tile = ROUTE[math.min(i, #ROUTE)]
            t.player.walk_to(tile[1] + ox, tile[2] + oz, 0)
        end
        go(1)

        local pray = nil
        local forms_seen = {}
        local phase, last_key = 1, nil
        local surfaced = rise
        -- The snakeling shot: from rise + 175 every snakeling standing hatched
        -- in phase 7 (rotation 4's south eggs), so none dies of age under the
        -- arrow; within six tiles, four ticks or more into a phase, the walk
        -- held until the arrow has landed.
        local snake_shot, snake_result, hold_until, walk_owed = nil, nil, -1, false
        while now(t) < rise + 525 do
            heal(t)
            local since = now(t) - surfaced
            if not snake_shot and now(t) >= rise + 175 and since >= 4 then
                for _, sym in ipairs({ "snakeboss_minion_melee", "snakeboss_minion_magic" }) do
                    if t.npc.nearest(sym, 6) == "ok" then
                        snake_shot = now(t)
                        snake_result = t.player.attack(sym, 2, 4)
                        hold_until = snake_shot + 6
                        break
                    end
                end
            end
            local f, _, row = boss(t)
            if f then
                forms_seen[f] = true
                local key = f .. ":" .. tostring(row.x) .. "," .. tostring(row.z)
                if last_key and key ~= last_key then
                    phase = phase + 1
                    surfaced = now(t)
                    walk_owed = true
                end
                last_key = key
            end
            if walk_owed and now(t) >= hold_until then
                go(phase)
                walk_owed = false
            end
            local want = (f == 1 and "protectfrommissiles") or (f == 3 and "protectfrommagic") or nil
            if want ~= pray then
                if pray then t.prayer.set(pray, false) end
                if want then t.prayer.set(want, true) end
                pray = want
            end
            t.ticks(1)
        end
        t.check("zulrah.forms", forms_seen[1] and forms_seen[2] and forms_seen[3],
            "forms seen: green " .. tostring(forms_seen[1]) .. ", crimson " .. tostring(forms_seen[2])
                .. ", tanzanite " .. tostring(forms_seen[3]))
        t.check("zulrah.phases", phase >= 19, "surfacings seen " .. phase)
        t.check("zulrah.snakeling", snake_result == "ok",
            "a snakeling shot on tick " .. tostring(snake_shot) .. ": " .. tostring(snake_result))

        -- The death: down to 1, then the bow until it is gone.
        t.cheat("::zulrahhp 1", false)
        local gone = nil
        local started = now(t)
        while now(t) < started + 120 do
            heal(t)
            local f, sym = boss(t)
            if not f then gone = now(t) break end
            t.player.attack(sym, 2, 1)
            t.ticks(1)
        end
        t.check("zulrah.killed", gone ~= nil, "Zulrah gone on tick " .. tostring(gone))
        t.ticks(10)
        local _, kills = t.var.server("varp1518_total_snakeboss_kills")
        t.check("zulrah.handoff", kills == 1, "the session's kill count after the death: " .. tostring(kills))
    end,
}
