-- _scavengers_tells: the scavenger beasts send what Jagex's server sends.
-- RSProx 820, 843, 3282, 3285, 3389, 17 beasts in 13 room visits, in instance
-- tiles (docs/rsprox-analysis/cox/scavengers.md, tools/cox/rsprox_scavengers.py):
--   a swing (RS-A1): skavid_attack (255) with goblin_attack (469) to the
--     target that tick, and the hit on the target that tick;
--   the opening (RS-W1): a beast opens on a raider within 5 of its south-west
--     tile without being hit -- here: a swing before any hit on it, with the
--     raider within 5 of its south-west tile at the end of the tick before
--     the first face of the fight;
--   a hit on a beast: goblin_hit (472);
--   the death (RS-D1, D2): skavid_death (258) with goblin_death (471); 2
--     ticks later the beast is gone and under it lie bones (526, 200 ticks)
--     and mallignum root planks x2 (21036, 3000 ticks), and every other
--     drop lies 200 ticks;
--   the next beast (RS-D3): added at most 4 ticks after the death seq.
-- Played by the room solver (cox2_supplies.lua) in the room test's light kit.
local function kit()
    local lines = { "::clearinv" }
    for _, s in ipairs({ "attack", "strength", "defence", "hitpoints", "prayer", "magic", "ranged", "agility",
            "mining", "woodcutting" }) do
        lines[#lines + 1] = "::setlevel " .. s .. " 99"
    end
    for _, obj in ipairs({ "fris_kingly_helm", "barbassault_penance_fighter_torso", "black_dragonhide_chaps",
            "hundred_gauntlets_level_10", "abyssal_tentacle", "dragon_parryingdagger", "enchanted_onyx_amulet",
            "tzhaar_cape_fire", "dragon_boots", "nzone_berzerker_ring" }) do
        lines[#lines + 1] = "::give " .. obj .. " 1"
        lines[#lines + 1] = "::wield " .. obj
    end
    lines[#lines + 1] = "::~charge abyssal_tentacle 10000"
    lines[#lines + 1] = "::clearinv"
    -- the shortcut's tools, and two brews for the scavengers' 13s
    for _, g in ipairs({ "rune_pickaxe 1", "rune_axe 1", "raids_vial_xericaid_strong_4 2" }) do
        lines[#lines + 1] = "::give " .. g
    end
    return lines
end



local WANT = { raids_endarkened_juice = 8, raids_stinkhorn_mushroom = 3, raids_cicely = 1, raids_plank = 2 }
local BEAST = { [7548] = true, [7549] = true }

local function by_tick(rows)
    local out = {}
    for _, r in ipairs(rows) do
        out[r.tick] = out[r.tick] or {}
        table.insert(out[r.tick], r)
    end
    return out
end

return {
    id = "_scavengers_tells",
    fixture = "fresh_lumbridge.ini",
    max_frames = 400000,
    setup = kit(),
    run = function(t)
        t.ticklog.start()
        local n = t.raid.cox_run_n()
        local name = (n % 2 == 0) and "scavenger_small" or "scavenger_large"
        local er, ed = t.raid.cox_test_enter_long(name, n // 2, 80)
        t.check("tells.enter", er == "ok", name .. " " .. tostring(er) .. " " .. tostring(ed and ed.seed))
        if er ~= "ok" then t.finish(1) return end
        t.ticks(2)
        local result, detail = t.raid.cox_scavengers_solve({ want = WANT, max_ticks = 1200 })
        t.check("tells.solve", result == "ok", tostring(detail))
        if result ~= "ok" then t.finish(1) return end
        -- (the solver hands back with the last kill's corpse still falling)
        t.ticks(12)
        -- (one read: the room lives on after the solver hands back, and kinds
        -- read a tick apart disagree)
        local KINDS = { "npc_anim", "sound", "hit_player", "hit_npc", "npc_spawn", "npc_tile", "player_tile",
                        "npc_death", "npc_free", "obj_add" }
        local r, got = t.ticklog.rows({ kind = KINDS })
        t.check("tells.rows", r == "ok", tostring(r))
        local rows, last = {}, 0
        for _, kind in ipairs(KINDS) do rows[kind] = {} end
        for _, row in ipairs(r == "ok" and got or {}) do
            table.insert(rows[row.kind], row)
            if row.tick > last then last = row.tick end
        end
        local slots, tiles = {}, {}
        for _, r in ipairs(rows.npc_spawn) do
            if BEAST[r.type] then
                slots[r.slot] = slots[r.slot] or {}
                table.insert(slots[r.slot], r.tick)
                tiles[r.slot] = tiles[r.slot] or {}
                table.insert(tiles[r.slot], { r.tick, r.x, r.z })
            end
        end
        for _, r in ipairs(rows.npc_tile) do
            if tiles[r.slot] and BEAST[r.type] then table.insert(tiles[r.slot], { r.tick, r.x, r.z }) end
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
        local function me_at(tick)
            for k = tick, tick - 50, -1 do
                if me[k] then return me[k] end
            end
            return nil
        end
        local anims, snd, hits = by_tick(rows.npc_anim), by_tick(rows.sound), by_tick(rows.hit_player)
        local bad, n_swing, n_open, n_death = {}, 0, 0, 0
        local first_hit = {}
        for _, h in ipairs(rows.hit_npc) do
            if BEAST[h.type] then
                first_hit[h.slot] = first_hit[h.slot] or {}
                local found = false
                for _, s in ipairs(snd[h.tick] or {}) do
                    if s.sound == 472 and s.npc_slot == h.slot then found = true end
                end
                if not found then bad[#bad + 1] = string.format("t%d: a hit on %d without goblin_hit", h.tick, h.slot) end
            end
        end
        -- THE SWINGS, and each life's opening (a life: spawn .. free)
        local life_hit = {}
        for _, r in ipairs(rows.npc_anim) do
            if BEAST[r.type] and r.seq == 255 then
                n_swing = n_swing + 1
                local C = r.tick
                local s469, hit = false, false
                for _, s in ipairs(snd[C] or {}) do if s.sound == 469 then s469 = true end end
                if hits[C] then hit = true end
                if not s469 then bad[#bad + 1] = string.format("t%d: a swing without goblin_attack", C) end
                if not hit then bad[#bad + 1] = string.format("t%d: a swing without a hit on the raider that tick", C) end
            end
        end
        for slot, spawns in pairs(slots) do
            for i, s0 in ipairs(spawns) do
                local s1 = spawns[i + 1] or math.huge
                local hit1, swing1 = nil, nil
                for _, h in ipairs(rows.hit_npc) do
                    if h.slot == slot and h.tick >= s0 and h.tick < s1 and hit1 == nil then hit1 = h.tick end
                end
                for _, r in ipairs(rows.npc_anim) do
                    if r.slot == slot and r.seq == 255 and r.tick >= s0 and r.tick < s1 and swing1 == nil then swing1 = r.tick end
                end
                if swing1 and (hit1 == nil or swing1 < hit1) then
                    n_open = n_open + 1
                    -- the raider within 5 of its south-west tile at some tick
                    -- end in the 3 before the swing (the face is a tick or two
                    -- ahead of it: RS-W1)
                    local near = false
                    for k = swing1 - 3, swing1 - 1 do
                        local at, p = tile_at(slot, k), me_at(k)
                        if at and p and math.max(math.abs(p.x - at[2]), math.abs(p.z - at[3])) <= 5 then near = true end
                    end
                    if not near then bad[#bad + 1] = string.format("slot %d opened t%d with nobody within 5", slot, swing1) end
                end
            end
        end
        -- THE DEATHS
        local freed = {}
        for _, f in ipairs(rows.npc_free) do
            if BEAST[f.type] then freed[#freed + 1] = f end
        end
        for _, dr in ipairs(rows.npc_death) do
            if BEAST[dr.type] and dr.tick <= last - 6 then
                local D = nil
                for _, r in ipairs(rows.npc_anim) do
                    if r.slot == dr.slot and r.seq == 258 and r.tick >= dr.tick and r.tick <= dr.tick + 3 then D = r.tick end
                end
                if D == nil then
                    bad[#bad + 1] = string.format("t%d: no skavid_death", dr.tick)
                else
                    n_death = n_death + 1
                    local s471 = false
                    for _, s in ipairs(snd[D] or {}) do if s.sound == 471 then s471 = true end end
                    if not s471 then bad[#bad + 1] = string.format("t%d: no goblin_death", D) end
                    local gone = nil
                    for _, f in ipairs(freed) do
                        if f.slot == dr.slot and f.tick >= D and gone == nil then gone = f end
                    end
                    if not gone or gone.tick ~= D + 2 then
                        bad[#bad + 1] = string.format("t%d: removed %s, not D+2", D, gone and ("D+" .. (gone.tick - D)) or "never")
                    else
                        local bones, planks = false, false
                        for _, o in ipairs(rows.obj_add) do
                            if o.tick == gone.tick and o.coord == gone.coord then
                                if o.obj == 526 then bones = o.receiver_pid ~= nil end
                                if o.obj == 21036 and o.count == 2 then planks = true end
                            end
                        end
                        if not bones then bad[#bad + 1] = string.format("t%d: no bones under it", D) end
                        if not planks then bad[#bad + 1] = string.format("t%d: no planks under it", D) end
                        local next_add = nil
                        for _, r in ipairs(rows.npc_spawn) do
                            if BEAST[r.type] and r.tick > D and next_add == nil then next_add = r.tick end
                        end
                        if next_add and next_add - D > 4 and next_add - D < 30 then
                            bad[#bad + 1] = string.format("t%d: the next beast %d ticks after the death", D, next_add - D)
                        end
                    end
                end
            end
        end
        -- (an opening is reported, not required: the solver often strikes first)
        t.check("tells.counts", n_swing > 0 and n_death > 0,
            string.format("%d swings, %d unprovoked openings, %d deaths", n_swing, n_open, n_death))
        t.check("tells.recorded", #bad == 0, #bad == 0 and "every tell as recorded" or table.concat(bad, "; "))
        t.finish((#bad == 0 and n_swing > 0 and n_death > 0) and 0 or 1)
    end,
}
