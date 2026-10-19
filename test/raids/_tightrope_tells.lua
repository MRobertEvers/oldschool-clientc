-- _tightrope_tells: the tightrope's guards and rope send what Jagex's server
-- sends. RSProx 820, 843, 3282, 3285, 3389, 7 rooms in instance tiles
-- (docs/rsprox-analysis/cox/tightrope.md, tools/cox/rsprox_tightrope.py):
--   a mage's cast (RT-A1, A3): human_caststrike_staff (1162), fireblast_casting
--     (129) at height 92, fireblast_cast_and_fire (155) round the mage;
--     fireblast_travel (130) start 51, end 46 + 10 x the gap; delayed to the
--     end, on the raider, fireblast_impact (131) at 124 with fireblast_hit
--     (156) round his tile, or failedspell_impact (85) with spellfail (227);
--     the hit on C + end / 30, none for a splash;
--   a ranger's shot (RT-A2): human_bow (426), rune_arrow_launch (24) at 96,
--     arrowlaunch2 (2693) round the ranger; rune_arrow_travel (15) start 41,
--     end 46 + 5 x the gap; the hit on C + end / 30;
--   one attack every 4 ticks (RT-A5); the opening 2 ticks after the first hit
--     lands (RT-W1), or the tick after the crosser's first step (RT-X3);
--   blocks (RT-B1): human_staff_block (415) / human_unarmed_def (425) delayed
--     to the raider's projectile; godwars_spirit_hit (3878) / blade2 (29);
--   the death (RT-D1): human_death (836) and its sound with the killing hit,
--     the guard gone and bones (526) under it 2 ticks later;
--   the crossing (RT-X1, X2): a two-tile step (T0), a tile a tick to T0+8,
--     tightrope (2495) on T0+1..T0+7, loops 2 and 1 on the last; every hit
--     landing on the rope waits for T0+9;
--   the keystone (RT-K1..K3): human_pickuptable with pick2 (2582); the
--     dispel's with magical_barrier (1657); every guard left standing dies
--     the tick after the dispel.
-- Played by the room solver with its tells options: `tank` (stands in the
-- fire under the protection) and `cross_with = 1` (one ranger is left alive
-- for the crossings: the keystone kills it).
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
    "::setvar varp7348_barbarian_vial_smash 1",
}) do kit[#kit + 1] = line end
for _, obj in ipairs(WORN) do
    kit[#kit + 1] = "::give " .. obj .. " 1"
    kit[#kit + 1] = "::wield " .. obj
end
for _, line in ipairs({
    "::~charge abyssal_tentacle 10000",
    "::give dragon_bolts_enchanted_ruby 300", "::wield dragon_bolts_enchanted_ruby",
    "::clearinv",
    "::blowpipe dragon_dart 2000 2000",
    "::give avas_assembler 1",
    "::give toxic_tots_charged 1", "::~charge toxic_tots_charged 2500",
    "::give occult_necklace 1", "::give ma2_saradomin_cape 1",
    "::give xbows_crossbow_dragon 1", "::give black_dragonhide_body 1",
    -- (the wiki minimum's mage switch: the fighter torso is -40 magic attack)
    "::give mystic_robe_top 1", "::give mystic_robe_bottom 1",
    -- (Vasa's crystals take stab, the Guardians a pickaxe of the raider's own)
    "::give dragon_shortsword 1", "::give dragon_pickaxe 1",
    -- the supplies: test/raids/cox_raid_solo_prep.lua's, line for line --
    -- the wiki's minimum setup brought in from outside (seven Saradomin
    -- brews, three super restores), the Shamans' antidote and the Mystics'
    -- salve amulet (ei); the raid's own potions are brewed in the raid
    "::give 4dosepotionofsaradomin 7", "::give 4dose2restore 3",
    "::give antidote++4 1", "::give nzone_salve_amulet_e 1",
    -- a rune pouch of Water Strike (air, water, mind: enum_982 indices 1, 2, 5)
    -- to douse a fire-wall segment ("the lowest water spell in the rune
    -- pouch", Noodely's budget solo guide)
    "::give bh_rune_pouch 1",
    "::setvar varb29_rune_pouch_type_1 1", "::setvar varb1624_rune_pouch_quantity_1 500",
    "::setvar varb1622_rune_pouch_type_2 2", "::setvar varb1625_rune_pouch_quantity_2 500",
    "::setvar varb1623_rune_pouch_type_3 5", "::setvar varb1626_rune_pouch_quantity_3 500",
}) do kit[#kit + 1] = line end

-- (::god 1: every splat is still sent, only the subtraction is skipped, so
-- the run lives through the crossings under the ranger)
kit[#kit + 1] = "::god 1"

local GUARD = { [7559] = "ranger", [7560] = "mage" }
local A = {
    mage = { proj = 130, start = 51, per = 10, seq = 1162, gfx = 129, gfx_h = 92, snd = 155,
             block = 415, hit_snd = 3878, death_snd = 3891 },
    ranger = { proj = 15, start = 41, per = 5, seq = 426, gfx = 24, gfx_h = 96, snd = 2693,
               block = 425, hit_snd = 29, death_snd = 512 },
}

local function by_tick(rows)
    local out = {}
    for _, r in ipairs(rows) do
        out[r.tick] = out[r.tick] or {}
        table.insert(out[r.tick], r)
    end
    return out
end

local function cheb(ax, az, bx, bz)
    return math.max(math.abs(ax - bx), math.abs(az - bz))
end

return {
    id = "_tightrope_tells",
    fixture = "fresh_lumbridge.ini",
    max_frames = 480000,
    setup = kit,
    run = function(t)
        t.ticklog.start()
        local er, ed = t.raid.cox_test_enter("tightrope", t.raid.cox_run_n())
        t.check("tells.enter", er == "ok", tostring(er) .. " " .. tostring(ed and ed.seed))
        if er ~= "ok" then t.finish(1) return end
        t.ticks(2)
        local result, detail = t.raid.cox_tightrope_solve({ max_ticks = 1500, tank = true, cross_with = 1 })
        t.check("tells.solve", result == "ok", tostring(detail))
        if result ~= "ok" then t.finish(1) return end
        t.ticks(4)
        local rows = {}
        for _, kind in ipairs({ "projectile", "npc_anim", "npc_spotanim", "player_spotanim", "player_anim", "sound",
                                "hit_player", "hit_npc", "npc_spawn", "npc_tile", "player_tile", "npc_death",
                                "npc_free", "obj_add", "npc_say" }) do
            local r, got = t.ticklog.rows({ kind = kind })
            t.check("tells.rows." .. kind, r == "ok", tostring(r))
            rows[kind] = r == "ok" and got or {}
            t.ticks(1)
        end
        local kinds, tiles = {}, {}
        for _, r in ipairs(rows.npc_spawn) do
            if GUARD[r.type] then
                kinds[r.slot] = GUARD[r.type]
                tiles[r.slot] = { { r.tick, r.x, r.z } }
            end
        end
        for _, r in ipairs(rows.npc_tile) do
            if kinds[r.slot] then table.insert(tiles[r.slot], { r.tick, r.x, r.z }) end
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
        local anims, gfx, pgfx, snd, panim = by_tick(rows.npc_anim), by_tick(rows.npc_spotanim),
            by_tick(rows.player_spotanim), by_tick(rows.sound), by_tick(rows.player_anim)
        local hits = by_tick(rows.hit_player)
        local bad = {}
        local n = { mage = 0, ranger = 0, landed = 0, splash = 0, dumped = 0, crossings = 0 }
        local function find(list, pred)
            for _, r in ipairs(list or {}) do
                if not r.used and pred(r) then r.used = true return r end
            end
            return nil
        end
        -- THE CROSSINGS: the rope's creaks, T0 the tick before the first
        local creaks = {}
        for _, r in ipairs(rows.sound) do
            if r.sound == 2495 then creaks[#creaks + 1] = r end
        end
        local crossings = {}
        for _, r in ipairs(creaks) do
            local c = crossings[#crossings]
            if c and r.tick - c.last <= 1 then
                c.last = r.tick
                c.loops[#c.loops + 1] = r.loops
            else
                crossings[#crossings + 1] = { t0 = r.tick - 1, last = r.tick, loops = { r.loops } }
            end
        end
        local function on_rope(tick)
            for _, c in ipairs(crossings) do
                if tick >= c.t0 and tick <= c.t0 + 8 then return c end
            end
            return nil
        end
        for _, c in ipairs(crossings) do
            n.crossings = n.crossings + 1
            local T0 = c.t0
            local function no(what) bad[#bad + 1] = string.format("crossing T0=t%d: %s", T0, what) end
            local want = { 2, 2, 2, 2, 2, 2, 1 }
            if c.last ~= T0 + 7 or table.concat(c.loops, ",") ~= table.concat(want, ",") then
                no("creaks " .. table.concat(c.loops, ",") .. " ending T0+" .. (c.last - T0))
            end
            local before, at0 = me_at(T0 - 1), me[T0]
            if not (before and at0) or cheb(before.x, before.z, at0.x, at0.z) ~= 2 then no("T0 is not a two-tile step") end
            for k = 1, 8 do
                local a, b = me[T0 + k - 1], me[T0 + k]
                if not (a and b) or cheb(a.x, a.z, b.x, b.z) ~= 1 then no("T0+" .. k .. " is not one tile") break end
            end
            for k = 0, 8 do
                if hits[T0 + k] then no("a hit landed on the rope at T0+" .. k) end
            end
        end
        -- EVERY GUARD ATTACK: a projectile from a guard's tile
        local attacks = {}
        local dumped_at = {}
        for _, p in ipairs(rows.projectile) do
            if p.spotanim == 130 or p.spotanim == 15 then
                -- (two guards can share a tile: the one that swung this tick)
                local slot = nil
                for s in pairs(kinds) do
                    local at = tile_at(s, p.tick)
                    if at and at[2] == p.src_x and at[3] == p.src_z then
                        local swung = false
                        for _, r in ipairs(anims[p.tick] or {}) do
                            if r.slot == s and r.seq == A[kinds[s]].seq then swung = true end
                        end
                        if slot == nil or swung then slot = s end
                    end
                end
                local C = p.tick
                local function no(what) bad[#bad + 1] = string.format("t%d %s", C, what) end
                if slot == nil then
                    no("projectile " .. p.spotanim .. " not from a guard's tile")
                else
                    local kind = kinds[slot]
                    local a = A[kind]
                    local g = cheb(p.src_x, p.src_z, p.dst_x, p.dst_z)
                    n[kind] = n[kind] + 1
                    attacks[slot] = attacks[slot] or {}
                    table.insert(attacks[slot], C)
                    if p.spotanim ~= a.proj then no(kind .. " sent projectile " .. p.spotanim) end
                    if p.start_cycle ~= a.start or p.end_cycle ~= 46 + a.per * g then
                        no(string.format("%s start %d end %d at gap %d", kind, p.start_cycle, p.end_cycle, g))
                    end
                    if not find(anims[C], function(r) return r.slot == slot and r.seq == a.seq end) then no(kind .. " no seq " .. a.seq) end
                    if not find(gfx[C], function(r) return r.slot == slot and r.spotanim == a.gfx and r.height == a.gfx_h and r.delay == 0 end) then
                        no(kind .. " no launch graphic")
                    end
                    if not find(snd[C], function(r) return r.sound == a.snd and r.delay == 0 and r.radius == 10 and r.x == p.src_x and r.z == p.src_z end) then
                        no(kind .. " no launch sound round the guard")
                    end
                    local landed = true
                    if kind == "mage" then
                        local imp = find(pgfx[C], function(r)
                            return (r.spotanim == 131 or r.spotanim == 85) and r.height == 124 and r.delay == p.end_cycle
                        end)
                        if not imp then
                            no("mage: no impact graphic delayed to the end")
                        else
                            landed = imp.spotanim == 131
                            n[landed and "landed" or "splash"] = n[landed and "landed" or "splash"] + 1
                            local s = landed and 156 or 227
                            if not find(snd[C], function(r) return r.sound == s and r.delay == p.end_cycle and r.radius == 10 and r.x == p.dst_x and r.z == p.dst_z end) then
                                no("mage: no impact sound " .. s .. " round the raider")
                            end
                        end
                    end
                    local L = C + p.end_cycle // 30
                    local c = on_rope(L)
                    local at = c and (c.t0 + 9) or L
                    if c then n.dumped = n.dumped + (landed and 1 or 0) end
                    if landed then
                        dumped_at[at] = (dumped_at[at] or 0) + 1
                    elseif hits[L] and not c then
                        -- (a splash's tick may carry another guard's hit: judged in the sum below)
                    end
                end
            end
        end
        -- (a landing off the rope may wait a tick or two the same way: the
        -- keystone's pickup delays the raider, and his queue with him)
        local deferred = 0
        for at, want in pairs(dumped_at) do
            local got = #(hits[at] or {})
            if got < want then
                local later = #(hits[at + 1] or {}) + #(hits[at + 2] or {})
                if got + later < want then
                    bad[#bad + 1] = string.format("t%d: %d landings, %d hits", at, want, got)
                else
                    deferred = deferred + (want - got)
                end
            end
        end
        -- THE OPENINGS AND THE CADENCE
        local first_hit = {}
        for _, h in ipairs(rows.hit_npc) do
            if kinds[h.slot] and first_hit[h.slot] == nil then first_hit[h.slot] = h.tick end
        end
        local opened = {}
        for slot, cs in pairs(attacks) do
            local C1, H1 = cs[1], first_hit[slot]
            local woke = nil
            for _, c in ipairs(crossings) do
                if C1 == c.t0 + 1 then woke = c end
            end
            if H1 ~= nil and H1 < C1 and C1 == H1 + 2 then
                opened[#opened + 1] = string.format("%s%d hit+2", kinds[slot], slot)
            elseif woke then
                opened[#opened + 1] = string.format("%s%d woken", kinds[slot], slot)
            else
                bad[#bad + 1] = string.format("%s %d opened t%d: first hit %s", kinds[slot], slot, C1, tostring(H1))
            end
            for i = 2, #cs do
                if cs[i] - cs[i - 1] ~= 4 then bad[#bad + 1] = string.format("%s %d attacked t%d and t%d", kinds[slot], slot, cs[i - 1], cs[i]) end
            end
        end
        -- BLOCKS AND HIT SOUNDS
        for _, p in ipairs(rows.projectile) do
            local slot = p.target ~= nil and p.target > 0 and (p.target - 1) or nil
            if slot and kinds[slot] then
                local a = A[kinds[slot]]
                if not find(anims[p.tick], function(r) return r.slot == slot and r.seq == a.block and r.delay == p.end_cycle end) then
                    bad[#bad + 1] = string.format("t%d: no block %d on %d delayed to %d", p.tick, a.block, slot, p.end_cycle)
                end
            end
        end
        for _, h in ipairs(rows.hit_npc) do
            if kinds[h.slot] and not find(snd[h.tick], function(r) return r.sound == A[kinds[h.slot]].hit_snd and r.npc_slot == h.slot end) then
                bad[#bad + 1] = string.format("t%d: hit on %d without %d", h.tick, h.slot, A[kinds[h.slot]].hit_snd)
            end
        end
        -- THE DEATHS (the keystone's kill on the tick after the dispel is
        -- judged below; its removal is reported: npc_die reaps a tick sooner
        -- than a killing blow, tightrope.md Open)
        local dispel_tick = nil
        for tick, list in pairs(panim) do
            for _, r in ipairs(list) do
                if r.seq == 832 then
                    for _, x in ipairs(snd[tick] or {}) do
                        if x.sound == 1657 then dispel_tick = tick end
                    end
                end
            end
        end
        local deaths, reaped = 0, {}
        for _, dr in ipairs(rows.npc_death) do
            if kinds[dr.slot] and dispel_tick and dr.tick == dispel_tick + 1 then
                for _, f in ipairs(rows.npc_free) do
                    if f.slot == dr.slot and f.tick >= dr.tick then reaped[#reaped + 1] = "D+" .. (f.tick - dr.tick) break end
                end
            elseif kinds[dr.slot] then
                deaths = deaths + 1
                local D, a = dr.tick, A[kinds[dr.slot]]
                if not find(anims[D], function(r) return r.slot == dr.slot and r.seq == 836 end) then bad[#bad + 1] = string.format("t%d: 836 not with the blow", D) end
                if not find(snd[D], function(r) return r.sound == a.death_snd end) then bad[#bad + 1] = string.format("t%d: no %d", D, a.death_snd) end
                local freed, bones = false, false
                for _, f in ipairs(rows.npc_free) do
                    if f.slot == dr.slot and f.tick == D + 2 then freed = true end
                end
                for _, o in ipairs(rows.obj_add) do
                    if o.tick == D + 2 and o.coord == dr.coord and o.obj == 526 then bones = true end
                end
                if not freed then bad[#bad + 1] = string.format("t%d: not removed on D+2", D) end
                if not bones then bad[#bad + 1] = string.format("t%d: no bones on D+2", D) end
            end
        end
        -- THE KEYSTONE: the take (pick2) and the dispel (magical_barrier)
        local took, dispelled, K = 0, 0, nil
        for tick, list in pairs(panim) do
            for _, r in ipairs(list) do
                if r.seq == 832 then
                    if find(snd[tick], function(s) return s.sound == 2582 end) then took = took + 1
                    elseif find(snd[tick], function(s) return s.sound == 1657 end) then dispelled = dispelled + 1 K = tick
                    else bad[#bad + 1] = string.format("t%d: human_pickuptable with neither pick2 nor magical_barrier", tick) end
                end
            end
        end
        -- THE KEYSTONE'S KILL (RT-K3): every guard still standing at the
        -- dispel dies the tick after, as above
        local killed = 0
        if K then
            local died = {}
            for _, dr in ipairs(rows.npc_death) do died[dr.slot] = dr.tick end
            for slot in pairs(kinds) do
                local gone = nil
                for _, f in ipairs(rows.npc_free) do
                    if f.slot == slot and f.tick >= K then gone = f.tick end
                end
                local before = died[slot] ~= nil and died[slot] <= K
                if not before and gone ~= nil then
                    killed = killed + 1
                    if died[slot] ~= K + 1 then
                        bad[#bad + 1] = string.format("%s %d: dispel t%d, death t%s", kinds[slot], slot, K, tostring(died[slot]))
                    end
                end
            end
        end
        local says = {}
        for _, r in ipairs(rows.npc_say) do says[#says + 1] = string.format("t%d %s", r.tick, r.text) end
        t.check("tells.attacks", n.mage > 0 and n.ranger > 0 and n.landed > 0,
            string.format("mage %d (landed %d, splashed %d), ranger %d", n.mage, n.landed, n.splash, n.ranger))
        t.check("tells.crossings", n.crossings == 2 and n.dumped > 0,
            string.format("%d crossings, %d landings dumped to T0+9 (%d more deferred a tick or two); openings %s; shouts %s",
                n.crossings, n.dumped, deferred,
                table.concat(opened, ", "), #says > 0 and table.concat(says, ", ") or "none"))
        t.check("tells.keystone", took == 1 and dispelled == 1 and killed >= 1,
            string.format("took %d, dispelled %d, %d killed by it (removed %s; RSProx D+2); %d other deaths", took, dispelled,
                killed, table.concat(reaped, ","), deaths))
        t.check("tells.recorded", #bad == 0, #bad == 0 and "every tell as recorded" or table.concat(bad, "; "))
        t.finish((#bad == 0 and n.mage > 0 and n.ranger > 0 and n.dumped > 0 and took == 1 and dispelled == 1 and killed >= 1) and 0 or 1)
    end,
}
