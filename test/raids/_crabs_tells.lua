-- _crabs_tells: the crystal crab room sends what Jagex's server sends.
-- RSProx 3389 (solo, Challenge Mode), the only recording of the room, in
-- instance tiles (docs/rsprox-analysis/cox/crabs.md, tools/cox/rsprox_crabs.py):
--   the orbs (RC-O1..O5): one at a time, each added on the tick the last is
--     removed; a tile a tick from the tick after it is added; its end on one
--     tick E and its removal on E+1 -- on a small crystal the colour's bolt
--     impact at height 80, delay 10, on nothing no graphic at all, on a raider
--     the wave impact at height 80, delay 10;
--   a crab's swing (RC-A1): horror_crab_pincer_attack (2368) with
--     rock_crab_attack (718) to the raider that tick;
--   the smash (RC-S1): human_hammer_hit (1755); on the crab that tick
--     stunned_thieving (245) at height 100, delay 30, with kalthite_egg_crack
--     (556, delay 20) and stunned (2727, delay 30) round it;
--   the solve (RC-C2, C3): the big crystal's last stage on E+1 of the fourth
--     crystal E, the crabs removed one a tick from E+4, the shatter (1326 x3)
--     on E+5, the 2x2 opened (invisible_type8_nonblocking x4) on E+7.
-- Played by the room solver (cox2_crabs.lua) in the room test's kit.
local function kit()
    local lines = { "::clearinv" }
    for _, s in ipairs({ "attack", "strength", "defence", "hitpoints", "prayer", "magic", "ranged", "agility",
                         "woodcutting", "firemaking", "mining", "thieving", "herblore", "farming", "fishing", "hunter" }) do
        lines[#lines + 1] = "::setlevel " .. s .. " 99"
    end
    for _, line in ipairs({
        "::setvar varb3888_kr_quest ^kr_complete", "::setvar varb3909_kr_knightwaves_state 8",
        "::setvar varb5451_prayer_rigour_unlocked 1", "::setvar varb5452_prayer_augury_unlocked 1",
        "::setvar varb5453_prayer_preserve_unlocked 1", "::setvar varp7348_barbarian_vial_smash 1",
    }) do lines[#lines + 1] = line end
    for _, obj in ipairs({ "fris_kingly_helm", "barbassault_penance_fighter_torso", "black_dragonhide_chaps",
            "hundred_gauntlets_level_10", "abyssal_tentacle", "dragon_parryingdagger", "enchanted_onyx_amulet",
            "tzhaar_cape_fire", "dragon_boots", "nzone_berzerker_ring" }) do
        lines[#lines + 1] = "::give " .. obj .. " 1"
        lines[#lines + 1] = "::wield " .. obj
    end
    for _, line in ipairs({
        "::~charge abyssal_tentacle 10000",
        "::give dragon_bolts_enchanted_ruby 300", "::wield dragon_bolts_enchanted_ruby",
        "::clearinv",
        "::blowpipe dragon_dart 2000 2000",
        "::give avas_assembler 1", "::give toxic_tots_charged 1", "::~charge toxic_tots_charged 2500",
        "::give occult_necklace 1", "::give ma2_saradomin_cape 1",
        "::give xbows_crossbow_dragon 1", "::give black_dragonhide_body 1",
        "::give mystic_robe_top 1", "::give mystic_robe_bottom 1",
        -- (the raid's kit, cox_raid_solo: 27 in the pack, and its one free
        -- slot the scavengers' juice by the time a puzzle room comes)
        "::give dragon_shortsword 1", "::give dragon_pickaxe 1",
        "::give raids_vial_overload_strong_4 2", "::give raids_vial_xericaid_strong_4 5",
        "::give raids_vial_revitalisation_strong_4 3", "::give raids_vial_prayer_strong_4 3",
        "::give antidote++4 1", "::give nzone_salve_amulet_e 1",
        "::give bh_rune_pouch 1",
        "::give raids_endarkened_juice 14",
        "::setvar varb29_rune_pouch_type_1 1", "::setvar varb1624_rune_pouch_quantity_1 500",
        "::setvar varb1622_rune_pouch_type_2 2", "::setvar varb1625_rune_pouch_quantity_2 500",
        "::setvar varb1623_rune_pouch_type_3 5", "::setvar varb1626_rune_pouch_quantity_3 500",
    }) do lines[#lines + 1] = line end
    return lines
end


local CRAB = { [7576] = true, [7577] = true, [7578] = true, [7579] = true }
local ORB = { [7580] = true, [7581] = true, [7582] = true, [7583] = true }
local BOLT = { [119] = true, [128] = true, [122] = true, [125] = true }
local WAVE = { [160] = true, [157] = true, [161] = true, [165] = true }

local function by_tick(rows)
    local out = {}
    for _, r in ipairs(rows) do
        out[r.tick] = out[r.tick] or {}
        table.insert(out[r.tick], r)
    end
    return out
end

return {
    id = "_crabs_tells",
    fixture = "fresh_lumbridge.ini",
    max_frames = 400000,
    setup = kit(),
    run = function(t)
        t.ticklog.start()
        local er, ed = t.raid.cox_test_enter_long("crabs", t.raid.cox_run_n(), 80)
        t.check("tells.enter", er == "ok", tostring(er) .. " " .. tostring(ed and ed.seed))
        if er ~= "ok" then t.finish(1) return end
        t.ticks(2)
        local result, detail = t.raid.cox_crabs_solve({ max_ticks = 1500 })
        t.check("tells.solve", result == "ok", tostring(detail))
        if result ~= "ok" then t.finish(1) return end
        t.ticks(10)
        local KINDS = { "npc_anim", "npc_spotanim", "map_spotanim", "player_anim", "sound", "npc_spawn", "npc_tile",
                        "npc_free", "loc_set" }
        -- (a kind a tick: the instruction budget is per tick, and the room is
        -- quiet once solved -- no orb, no crab)
        local rows = {}
        for _, k in ipairs(KINDS) do
            local r, got = t.ticklog.rows({ kind = k })
            t.check("tells.rows." .. k, r == "ok", tostring(r))
            rows[k] = r == "ok" and got or {}
            t.ticks(1)
        end
        local snd, gfx, panim, nanim = by_tick(rows.sound), by_tick(rows.map_spotanim), by_tick(rows.player_anim),
            by_tick(rows.npc_anim)
        local bad = {}
        local function has(list, pred)
            for _, x in ipairs(list or {}) do if pred(x) then return true end end
            return false
        end
        -- THE ORBS: spawn .. free, the tiles between
        local orbs = {}
        for _, s in ipairs(rows.npc_spawn) do
            if ORB[s.type] then orbs[#orbs + 1] = { slot = s.slot, t0 = s.tick, x = s.x, z = s.z, tiles = {} } end
        end
        -- (indexed by slot once: a scan per orb runs out of the tick's budget)
        local frees_of, tiles_of = {}, {}
        for _, f in ipairs(rows.npc_free) do
            if ORB[f.type] then frees_of[f.slot] = frees_of[f.slot] or {} table.insert(frees_of[f.slot], f) end
        end
        for _, nt in ipairs(rows.npc_tile) do
            if ORB[nt.type] then tiles_of[nt.slot] = tiles_of[nt.slot] or {} table.insert(tiles_of[nt.slot], nt) end
        end
        local function life(o)
            for _, f in ipairs(frees_of[o.slot] or {}) do
                if f.tick > o.t0 and o.t1 == nil then o.t1 = f.tick o.fx, o.fz = f.x, f.z end
            end
            for _, nt in ipairs(tiles_of[o.slot] or {}) do
                if nt.tick > o.t0 and (o.t1 == nil or nt.tick < o.t1) then table.insert(o.tiles, nt) end
            end
        end
        t.ticks(1)
        local ends = { crystal = 0, wall = 0, raider = 0 }
        for i, o in ipairs(orbs) do
            life(o)
            if o.t1 then
                local nxt = orbs[i + 1]
                if nxt and nxt.t0 ~= o.t1 then bad[#bad + 1] = string.format("orb t%d: removed t%d, the next added t%d", o.t0, o.t1, nxt.t0) end
                local first = o.tiles[1]
                if first and first.tick ~= o.t0 + 1 then bad[#bad + 1] = string.format("orb t%d: first step t%d", o.t0, first.tick) end
                local px, pz = o.x, o.z
                for _, nt in ipairs(o.tiles) do
                    if math.max(math.abs(nt.x - px), math.abs(nt.z - pz)) ~= 1 then
                        bad[#bad + 1] = string.format("orb t%d: a %d-tile step at t%d", o.t0, math.max(math.abs(nt.x - px), math.abs(nt.z - pz)), nt.tick)
                    end
                    px, pz = nt.x, nt.z
                end
                local E = o.t1 - 1
                local on = gfx[E] or {}
                local bolt = has(on, function(g) return BOLT[g.spotanim] and g.x == o.fx and g.z == o.fz end)
                local wave = has(on, function(g) return WAVE[g.spotanim] and g.x == o.fx and g.z == o.fz end)
                if bolt then
                    ends.crystal = ends.crystal + 1
                    if not has(on, function(g) return BOLT[g.spotanim] and g.height == 80 and g.delay == 10 end) then
                        bad[#bad + 1] = string.format("orb t%d: a bolt impact not at height 80, delay 10", o.t0)
                    end
                elseif wave then
                    ends.raider = ends.raider + 1
                else
                    ends.wall = ends.wall + 1
                end
                -- a wall end stands still on E (removed 2 after its last
                -- step); a crystal or raider end steps onto it on E (1 after)
                local last = o.tiles[#o.tiles]
                if last then
                    local want = (bolt or wave) and 1 or 2
                    if o.t1 - last.tick ~= want then
                        bad[#bad + 1] = string.format("orb t%d: removed %d after its last step, not %d", o.t0, o.t1 - last.tick, want)
                    end
                end
                if has(gfx[o.t1], function(g) return (BOLT[g.spotanim] or WAVE[g.spotanim]) and g.x == o.fx and g.z == o.fz end) then
                    bad[#bad + 1] = string.format("orb t%d: an impact on its removal tick", o.t0)
                end
            end
        end
        t.ticks(1)
        -- THE CRABS' SWINGS
        local swings = 0
        for _, a in ipairs(rows.npc_anim) do
            if CRAB[a.type] and a.seq ~= 1313 then
                swings = swings + 1
                if a.seq ~= 2368 then bad[#bad + 1] = string.format("t%d: crab seq %d", a.tick, a.seq) end
                if not has(snd[a.tick], function(s) return s.sound == 718 end) then bad[#bad + 1] = string.format("t%d: a swing without rock_crab_attack", a.tick) end
            end
        end
        -- THE SMASHES
        local smashes = 0
        for _, g in ipairs(rows.npc_spotanim) do
            if CRAB[g.type] and g.spotanim == 245 then
                smashes = smashes + 1
                if g.height ~= 100 or g.delay ~= 30 then bad[#bad + 1] = string.format("t%d: the stun graphic at %d, delay %d", g.tick, g.height, g.delay) end
                if not has(panim[g.tick], function(p) return p.seq == 1755 end) then bad[#bad + 1] = string.format("t%d: a smash without human_hammer_hit", g.tick) end
                if not has(snd[g.tick], function(s) return s.sound == 556 and s.delay == 20 end) then bad[#bad + 1] = string.format("t%d: no kalthite_egg_crack", g.tick) end
                if not has(snd[g.tick], function(s) return s.sound == 2727 and s.delay == 30 end) then bad[#bad + 1] = string.format("t%d: no stunned", g.tick) end
                if has(nanim[g.tick], function(a) return a.slot == g.slot and a.seq == 1313 end) then bad[#bad + 1] = string.format("t%d: the crab defends on a smash", g.tick) end
            end
        end
        -- THE SOLVE: the fourth crystal is the last 29762
        local lit = {}
        for _, l in ipairs(rows.loc_set) do if l.loc == 29762 then lit[#lit + 1] = l.tick end end
        local E = lit[4]
        if not E then
            bad[#bad + 1] = string.format("%d small crystals lit, not 4", #lit)
        else
            if not has(rows.loc_set, function(l) return l.loc == 29757 and l.tick == E + 1 end) then bad[#bad + 1] = "the big crystal's last stage not on E+1" end
            local frees = {}
            for _, f in ipairs(rows.npc_free) do if CRAB[f.type] and f.tick >= E then frees[#frees + 1] = f.tick - E end end
            table.sort(frees)
            for i, d in ipairs(frees) do
                if d ~= 3 + i then bad[#bad + 1] = string.format("crab %d removed on E+%d", i, d) end
            end
            local shatter = 0
            for _, g in ipairs(gfx[E + 5] or {}) do if g.spotanim == 1326 then shatter = shatter + 1 end end
            if shatter ~= 3 then bad[#bad + 1] = string.format("%d shatter graphics on E+5", shatter) end
            local open = 0
            for _, l in ipairs(rows.loc_set) do if l.loc == 29733 and l.tick == E + 7 then open = open + 1 end end
            if open ~= 4 then bad[#bad + 1] = string.format("%d invisible locs on E+7", open) end
        end
        t.check("tells.counts", #orbs > 0 and swings > 0 and smashes > 0,
            string.format("%d orbs (ended on a crystal %d, on nothing %d, on me %d), %d swings, %d smashes",
                #orbs, ends.crystal, ends.wall, ends.raider, swings, smashes))
        t.check("tells.recorded", #bad == 0, #bad == 0 and "every tell as recorded" or table.concat(bad, "; "))
        t.finish((#bad == 0 and #orbs > 0 and swings > 0 and smashes > 0) and 0 or 1)
    end,
}
