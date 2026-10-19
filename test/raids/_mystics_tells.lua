-- _mystics_tells: the Skeletal Mystics send what Jagex's server sends.
-- RSProx 3282/3285/3389, 9 mystics, 20 casts in instance tiles
-- (docs/rsprox-analysis/cox/mystics.md, tools/cox/rsprox_mystics.py):
--   a cast (RM-A1..A5): skeleton_update_mage_casting_swansong (5528) with its
--     graphic at height 164 (1322 fire, 1321 Vulnerability); fireblast_travel
--     (130) / curse_travel (109) from the south-west tile, start 30, end
--     46 + 10 x the gap; on the cast, to the raider, the cast sound (155 / 127)
--     and, delayed to the end, the impact graphic at height 124 with its sound
--     (131 + 156 / 110 + 3012 landed, 85 + 227 splashed); the hit on
--     C + end / 30, none for a splash;
--   the opening (RM-W1, W2): a mystic that was not hit first opens with the
--     raider within 7 of its south-west tile (end of the tick before); one
--     that was casts 2 ticks after the hit lands;
--   a hit on a mystic (RM-B2): skelly_hit (779) to the raider;
--   the death (RM-D1..D4): skeleton_update_death (5491) and skeleton_death
--     (777) with the killing hit; 5 ticks later the despawn and, on its tile,
--     big bones for 200 ticks and 5-10 each of the three seeds for 3000; on
--     the last, invisible_type8_nonblocking and the three portals, rotation 0.
-- Played by the room solver in its own kit (both casts are magic, so its
-- Protect from Magic leaves every tell here to happen).
-- (the kit is test/raids/cox_mystics.lua's)
local WORN = {
    "fris_kingly_helm", "barbassault_penance_fighter_torso", "black_dragonhide_chaps", "hundred_gauntlets_level_10",
    "abyssal_tentacle", "dragon_parryingdagger", "enchanted_onyx_amulet", "tzhaar_cape_fire",
    "dragon_boots", "nzone_berzerker_ring",
}
local kit = { "::clearinv" }
for _, s in ipairs({ "attack", "strength", "defence", "hitpoints", "prayer", "magic", "ranged", "agility" }) do
    kit[#kit + 1] = "::setlevel " .. s .. " 99"
end
for _, line in ipairs({
    "::setvar varb3888_kr_quest ^kr_complete",
    "::setvar varb3909_kr_knightwaves_state 8",
    "::setvar varb5451_prayer_rigour_unlocked 1",
    "::setvar varb5452_prayer_augury_unlocked 1",
    "::setvar varb5453_prayer_preserve_unlocked 1",
}) do kit[#kit + 1] = line end
for _, obj in ipairs(WORN) do
    kit[#kit + 1] = "::give " .. obj .. " 1"
    kit[#kit + 1] = "::wield " .. obj
end
for _, line in ipairs({
    "::~charge abyssal_tentacle 10000",
    "::clearinv",
    "::blowpipe dragon_dart 2000 2000",
    "::give nzone_salve_amulet_e 1", "::give avas_assembler 1", "::give black_dragonhide_body 1",
    "::give raids_vial_overload_strong_4 1", "::give raids_vial_xericaid_strong_4 6",
    "::give raids_vial_revitalisation_strong_4 2",
}) do kit[#kit + 1] = line end
local MYSTIC = { [7604] = true, [7605] = true, [7606] = true }
local SIZE = 2

local function gap(sx, sz, x, z)
    local dx = (x < sx) and (sx - x) or ((x > sx + SIZE - 1) and (x - (sx + SIZE - 1)) or 0)
    local dz = (z < sz) and (sz - z) or ((z > sz + SIZE - 1) and (z - (sz + SIZE - 1)) or 0)
    return math.max(dx, dz)
end

local function by_tick(rows)
    local out = {}
    for _, r in ipairs(rows) do
        out[r.tick] = out[r.tick] or {}
        table.insert(out[r.tick], r)
    end
    return out
end

return {
    id = "_mystics_tells",
    fixture = "fresh_lumbridge.ini",
    max_frames = 480000,
    setup = kit,
    run = function(t)
        t.ticklog.start()
        local er, ed = t.raid.cox_test_enter("mystics", 1)
        t.check("tells.enter", er == "ok", tostring(er) .. " " .. tostring(ed and ed.seed))
        if er ~= "ok" then t.finish(1) return end
        local result, detail = t.raid.cox_mystics_solve({ max_ticks = 2000 })
        t.check("tells.solve", result == "ok", tostring(detail))
        if result ~= "ok" then t.finish(1) return end
        t.ticks(8)
        local rows = {}
        for _, kind in ipairs({ "projectile", "npc_anim", "npc_spotanim", "player_spotanim", "sound", "hit_player",
                                "hit_npc", "npc_spawn", "npc_tile", "player_tile", "npc_death", "npc_free",
                                "obj_add", "loc_set" }) do
            local r, got = t.ticklog.rows({ kind = kind })
            t.check("tells.rows." .. kind, r == "ok", tostring(r))
            rows[kind] = r == "ok" and got or {}
            t.ticks(1)
        end
        -- where each mystic stands, tick by tick (spawn, then each step)
        local slots, tiles = {}, {}
        for _, r in ipairs(rows.npc_spawn) do
            if MYSTIC[r.type] then
                slots[r.slot] = r.type
                tiles[r.slot] = { { r.tick, r.x, r.z } }
            end
        end
        for _, r in ipairs(rows.npc_tile) do
            if slots[r.slot] then table.insert(tiles[r.slot], { r.tick, r.x, r.z }) end
        end
        local function tile_at(slot, tick)
            local best = nil
            for _, e in ipairs(tiles[slot] or {}) do
                if e[1] <= tick then best = e end
            end
            return best
        end
        local me = {}
        for _, r in ipairs(rows.player_tile) do me[r.tick] = r end
        local anims, gfx, pgfx, snd = by_tick(rows.npc_anim), by_tick(rows.npc_spotanim),
            by_tick(rows.player_spotanim), by_tick(rows.sound)
        local hits = by_tick(rows.hit_player)
        local bad, n = {}, { fire = 0, vuln = 0, landed = 0, splash = 0 }
        local first_cast, lands = {}, {}
        local function find(list, pred)
            for i, r in ipairs(list or {}) do
                if not r.used and pred(r) then r.used = true return r end
            end
            return nil
        end
        -- EVERY CAST: a spell projectile from a mystic's tile
        for _, p in ipairs(rows.projectile) do
            if p.spotanim == 130 or p.spotanim == 109 then
                local slot = nil
                for s in pairs(slots) do
                    local at = tile_at(s, p.tick)
                    if at and at[2] == p.src_x and at[3] == p.src_z then slot = s end
                end
                local style = p.spotanim == 130 and "fire" or "vuln"
                local C, g = p.tick, gap(p.src_x, p.src_z, p.dst_x, p.dst_z)
                local function no(what) bad[#bad + 1] = string.format("t%d %s %s", C, style, what) end
                if slot == nil then
                    no("not from a mystic's south-west tile")
                else
                    n[style] = n[style] + 1
                    first_cast[slot] = first_cast[slot] or C
                    if p.start_cycle ~= 30 or p.end_cycle ~= 46 + 10 * g then
                        no(string.format("start %d end %d at gap %d", p.start_cycle, p.end_cycle, g))
                    end
                    if not find(anims[C], function(r) return r.slot == slot and r.seq == 5528 end) then no("no 5528") end
                    local cast_gfx = style == "fire" and 1322 or 1321
                    if not find(gfx[C], function(r) return r.slot == slot and r.spotanim == cast_gfx and r.height == 164 and r.delay == 0 end) then
                        no("no cast graphic at 164")
                    end
                    local landed_gfx = style == "fire" and 131 or 110
                    local imp = find(pgfx[C], function(r)
                        return (r.spotanim == landed_gfx or r.spotanim == 85) and r.height == 124 and r.delay == p.end_cycle
                    end)
                    if not imp then
                        no("no impact graphic delayed to the end")
                    else
                        local landed = imp.spotanim ~= 85
                        n[landed and "landed" or "splash"] = n[landed and "landed" or "splash"] + 1
                        local cast_snd = style == "fire" and 155 or 127
                        local hit_snd = landed and (style == "fire" and 156 or 3012) or 227
                        if not find(snd[C], function(r) return r.sound == cast_snd and r.delay == 0 end) then no("no cast sound") end
                        if not find(snd[C], function(r) return r.sound == hit_snd and r.delay == p.end_cycle end) then
                            no("no impact sound " .. hit_snd)
                        end
                        local L = C + p.end_cycle // 30
                        lands[L] = lands[L] or { landed = 0, splash = 0, tick = C }
                        lands[L][landed and "landed" or "splash"] = lands[L][landed and "landed" or "splash"] + 1
                        if L - C ~= 1 + (1 + g) // 3 then no("lands off the wiki's magic delay") end
                    end
                end
            end
        end
        -- the landings: a hit for every landed cast, none for a splash
        for L, e in pairs(lands) do
            local got = 0
            for _, h in ipairs(hits[L] or {}) do got = got + 1 end
            if got < e.landed then bad[#bad + 1] = string.format("t%d: %d landed, %d hits", L, e.landed, got) end
            if e.landed == 0 and got > 0 then bad[#bad + 1] = string.format("t%d: a splash drew a hit", L) end
        end
        -- THE OPENINGS: hunted (within 7 of its south-west tile) or answered (hit + 2)
        local first_hit = {}
        for _, h in ipairs(rows.hit_npc) do
            if slots[h.slot] and first_hit[h.slot] == nil then first_hit[h.slot] = h.tick end
        end
        local opened = {}
        for slot, C1 in pairs(first_cast) do
            local H1 = first_hit[slot]
            local at, p = tile_at(slot, C1), me[C1 - 1]
            local d = (at and p) and math.max(math.abs(p.x - at[2]), math.abs(p.z - at[3])) or 99
            if H1 ~= nil and C1 == H1 + 2 then
                opened[#opened + 1] = string.format("%d answered", slot)
            elseif (H1 == nil or C1 < H1 + 2) and d <= 7 then
                opened[#opened + 1] = string.format("%d hunted at %d", slot, d)
            else
                bad[#bad + 1] = string.format("slot %d opened t%d: first hit %s, raider %d from its south-west tile", slot, C1, tostring(H1), d)
            end
        end
        -- a hit on a mystic: skelly_hit to the raider, that tick
        for _, h in ipairs(rows.hit_npc) do
            if slots[h.slot] and not find(snd[h.tick], function(r) return r.sound == 779 and r.npc_slot == h.slot end) then
                bad[#bad + 1] = string.format("t%d: hit on %d without 779", h.tick, h.slot)
            end
        end
        -- THE DEATHS
        local deaths, last = 0, nil
        for _, dr in ipairs(rows.npc_death) do
            if slots[dr.slot] then
                deaths = deaths + 1
                local D = dr.tick
                if last == nil or D > last then last = D end
                if not find(anims[D], function(r) return r.slot == dr.slot and r.seq == 5491 end) then
                    bad[#bad + 1] = string.format("t%d: death seq not with the blow", D)
                end
                if not find(snd[D], function(r) return r.sound == 777 end) then bad[#bad + 1] = string.format("t%d: no 777", D) end
                local freed = false
                for _, f in ipairs(rows.npc_free) do
                    if f.slot == dr.slot and f.tick == D + 5 then freed = true end
                end
                if not freed then bad[#bad + 1] = string.format("t%d: not removed on D+5", D) end
                local drops = {}
                for _, o in ipairs(rows.obj_add) do
                    if o.tick == D + 5 and o.coord == dr.coord then drops[o.obj] = o end
                end
                local b = drops[532]
                if not b or b.count ~= 1 then bad[#bad + 1] = string.format("t%d: no big bones on D+5", D) end
                for _, seed in ipairs({ 20903, 20906, 20909 }) do
                    local o = drops[seed]
                    if not o or o.count < 5 or o.count > 10 then bad[#bad + 1] = string.format("t%d: seed %d", D, seed) end
                end
            end
        end
        local cleared = {}
        for _, l in ipairs(rows.loc_set) do
            if last and l.tick == last + 5 and l.angle == 0 then cleared[l.loc] = true end
        end
        for _, loc in ipairs({ 29733, 49996, 49997, 49998 }) do
            if not cleared[loc] then bad[#bad + 1] = string.format("clear: loc %d not added at rotation 0 on D+5", loc) end
        end
        -- (reported, not judged: a mystic that turned to melee walks, which
        -- the recordings never show -- no recorded raider stood where one could)
        local steps = 0
        for slot, C1 in pairs(first_cast) do
            for _, e in ipairs(tiles[slot]) do
                if e[1] > C1 then steps = steps + 1 end
            end
        end
        t.check("tells.both_styles", n.fire > 0 and n.vuln > 0 and n.landed > 0 and n.splash > 0,
            string.format("fire %d, vuln %d (landed %d, splashed %d)", n.fire, n.vuln, n.landed, n.splash))
        t.check("tells.deaths", deaths == 3, string.format("%d deaths; openings %s; steps after a first cast %d",
            deaths, table.concat(opened, ", "), steps))
        t.check("tells.recorded", #bad == 0, #bad == 0 and "every tell as recorded" or table.concat(bad, "; "))
        t.finish((#bad == 0 and deaths == 3 and n.fire > 0 and n.vuln > 0) and 0 or 1)
    end,
}
