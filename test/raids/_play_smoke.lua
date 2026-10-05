-- _play_smoke: Pestilent Bloat played through the PLAY LIBRARY (t.raid.play,
-- raid seam27 raid_play_by_tick_intent; docs/minigames/raid_loop/PLAY_NOTES.md).
-- An underscore harness, not a kept room: solo it plays Entry and asserts the
-- room's technique rows copied unchanged from test/raids/tob_bloat.lua; with
-- `--party 3` it plays Normal with the sourced roles (p1 first in and hides,
-- p2/p3 in on the first down: wiki_Theatre_of_Blood_Strategies.wikitext:687-689)
-- and is MEASURED, not graded green.  The fight itself is one call: the
-- library plus the room's plan.  Everything after it reads the tick log.
local role = (QD_PARTY and QD_PARTY.role) or 1
local size = (QD_PARTY and QD_PARTY.size) or 1
local kit
if size == 1 then
    -- tob_bloat.lua's own bring-alongs (Entry, solo), unchanged
    kit = {
        "::clearinv",
        "::setlevel attack 99", "::setlevel strength 99", "::setlevel defence 99",
        "::setlevel hitpoints 99", "::setlevel prayer 99",
        "::fullscythe", "::wield scythe_of_vitur",
        "::give shark 18",
        "::give br_4dose2restore 2",
        "::give dragon_warhammer 1",
        "::give 4dose2combat 1",
        "::give br_4dosepotionofsaradomin 6",
    }
else
    -- tob_bloat_normal.lua's party kit: ::maxmelee (the scythe of vitur, wiki
    -- "Recommended equipment"), anglerfish ("make sure that you eat your angler",
    -- transcripts/yt_KF9y2GYTJ-A.md:675), restores, brews, a super combat
    kit = {
        "::clearinv", "::maxmelee", "::setlevel prayer 99",
        "::give anglerfish 14", "::give br_4dose2restore 2", "::give 4dose2combat 1",
        "::give br_4dosepotionofsaradomin 4",
    }
end

return {
    id = "_play_smoke",
    fixture = "fresh_lumbridge.ini",
    max_frames = 120000,
    setup = kit,

    run = function(t)
        local mode = (size == 1) and "entry" or "normal"
        local boss = (size == 1) and "tob_bloat_story" or "tob_bloat"
        if role == 1 then t.ticklog.start() end
        local er, ed = t.raid.enter("tob", "bloat", { mode = mode })
        t.check("play.enter", er == "ok", "p" .. role .. " " .. tostring(ed))
        local pr = t.player.inv_op("4dose2combat", 1, { quick = true })
        t.ticks(1)
        local ar2, att2 = t.skill.read("attack")
        t.check("play.potion", ar2 == "ok" and att2.level > 99, "p" .. role .. " super combat before the barrier: attack " .. tostring(att2.level) .. " (" .. tostring(pr) .. ")")
        if size > 1 then t.expect("party.barrier.entrance", t.party.barrier("entrance", 300)) end
        local _, here = t.world.tile()
        local ox, oz = math.floor(here.x / 64) * 64, math.floor(here.z / 64) * 64
        if role == 1 then
            -- tob_bloat.lua :47-59: cross when Bloat is on the far (north) row heading
            -- west, "when Bloat is on the opposite side of the pillar" (wiki :687)
            t.player.walk_to(ox + 42, oz + 31, 1)
            local wait_ticks, wait_x = 0, nil
            while wait_ticks < 60 do
                local wr, wb = t.npc.state(boss)
                if wr == "ok" and wb.z == oz + 24 and wb.x <= ox + 32 and wb.x >= ox + 31 and wait_x ~= nil and wb.x < wait_x then break end
                if wr == "ok" then wait_x = wb.x end
                wait_ticks = wait_ticks + 1
                t.ticks(1)
            end
            t.exec("play.barrier", t.player.click_loc, "tob_arena_barrier", 1)
            local mr, md = t.ticklog.mark("room start")
            t.exec("play.begin", t.chat.play, { "options", "choose:Yes, begin the fight." })
            t.check("play.mark", mr == "ok", tostring(md))
            if size > 1 then t.expect("party.barrier.started", t.party.barrier("started", 900)) end
        else
            t.expect("party.barrier.started", t.party.barrier("started", 900))
            -- "As soon as Bloat deactivates, the rest of the team should enter" (wiki :689)
            local waited = 0
            while waited < 150 do
                local wr, wb = t.npc.state(boss)
                if wr == "ok" and wb.seq_id == 8082 then break end
                waited = waited + 1
                t.ticks(1)
            end
            local xr, xd = t.player.click_loc("tob_arena_barrier", 1)
            t.check("play.barrier_cross", xr == "ok", "p" .. role .. " waited " .. waited .. " ticks for the first down: " .. tostring(xd))
        end

        -- THE FIGHT: the library and the room's plan, nothing else
        local result, detail, rec = t.raid.play("tob_bloat", { mode = mode, max_ticks = 1400 })
        t.check("play.fight", result == "ok", "p" .. role .. " " .. tostring(detail))
        if role ~= 1 then
            t.expect("party.barrier.done", t.party.barrier("done", 9000))
            t.finish(0)
            return
        end

        -- THE TICK LOG (leader): the same reads tob_bloat.lua's ANALYSIS makes
        t.ticks(1)
        local mark_tick = nil
        local _, mark_rows = t.ticklog.rows({ kind = "mark" })
        for i = 1, #mark_rows do
            if mark_rows[i].label == "room start" then mark_tick = mark_rows[i].tick end
        end
        local sr0, row0 = t.npc.state(boss)
        local boss_slot = rec.boss_slot
        local death_tick = rec.death_tick
        local dead = result == "ok" and death_tick ~= nil
        local player_dead = result == "died"
        local end_tick = death_tick or 0
        local mine = function(row) return size == 1 or rec.my_pid == nil or row.pid == rec.my_pid end
        local downs = {}
        local _, anim_rows = t.ticklog.rows({ kind = "npc_anim", slot = boss_slot, seq = 8082 })
        for i = 1, #anim_rows do downs[#downs + 1] = anim_rows[i].tick end
        t.ticks(1)
        local bx_at, bz_at, move_ticks = {}, {}, {}
        local _, boss_tiles = t.ticklog.rows({ kind = "npc_tile", slot = boss_slot })
        for i = 1, #boss_tiles do
            bx_at[boss_tiles[i].tick] = boss_tiles[i].x
            bz_at[boss_tiles[i].tick] = boss_tiles[i].z
            move_ticks[#move_ticks + 1] = boss_tiles[i].tick
        end
        local bxf, bzf = {}, {}
        local last_bx, last_bz = nil, nil
        for tk = 0, end_tick + 40 do
            if bx_at[tk] ~= nil then last_bx, last_bz = bx_at[tk], bz_at[tk] end
            bxf[tk], bzf[tk] = last_bx, last_bz
        end
        t.ticks(1)
        local px_at, pz_at = {}, {}
        local _, player_tiles = t.ticklog.rows({ kind = "player_tile" })
        for i = 1, #player_tiles do
            if mine(player_tiles[i]) then
                px_at[player_tiles[i].tick] = player_tiles[i].x
                pz_at[player_tiles[i].tick] = player_tiles[i].z
            end
        end
        t.ticks(1)
        local fly_ticks, fly_list = {}, {}
        local _, projectiles = t.ticklog.rows({ kind = "projectile", spotanim = 1568 })
        for i = 1, #projectiles do
            local row = projectiles[i]
            if row.spotanim == 1568 and fly_ticks[row.tick] == nil then
                fly_ticks[row.tick] = true
                fly_list[#fly_list + 1] = row.tick
            end
        end
        t.ticks(1)
        local shadows = {}
        for sid = 1570, 1573 do
            local _, shadow_rows = t.ticklog.rows({ kind = "map_spotanim", spotanim = sid })
            for i = 1, #shadow_rows do
                local row = shadow_rows[i]
                if row.spotanim == sid then shadows[#shadows + 1] = { tick = row.tick, x = row.x, z = row.z } end
            end
            t.ticks(1)
        end
        local splat_ticks = {}
        local _, splat_rows = t.ticklog.rows({ kind = "map_spotanim", spotanim = 1576 })
        for i = 1, #splat_rows do splat_ticks[splat_rows[i].tick] = true end
        t.ticks(1)
        -- Protect from Missiles as the player READ it lit, per tick (the library's record)
        local shield_filled = {}
        local carried = false
        for tk = 0, end_tick + 40 do
            local lit = rec.prayer_at[tk]
            if lit ~= nil then carried = lit.protectfrommissiles == true end
            shield_filled[tk] = carried
        end
        local fly_hits_protected, stomp_hits, hand_hits = {}, {}, {}
        local taken = 0
        local _, player_hits = t.ticklog.rows({ kind = "hit_player", slot = boss_slot })
        for i = 1, #player_hits do
            local row = player_hits[i]
            if mine(row) then
                taken = taken + row.damage
                local in_down = false
                for k = 1, #downs do
                    if row.tick > downs[k] and row.tick < downs[k] + 33 then in_down = true end
                end
                if splat_ticks[row.tick] and row.damage >= 15 and not in_down then
                    hand_hits[#hand_hits + 1] = row.damage
                elseif in_down and row.damage >= 10 then
                    stomp_hits[#stomp_hits + 1] = { tick = row.tick, damage = row.damage }
                elseif row.damage <= 8 then
                    local shielded = true
                    for back = 0, 6 do
                        if shield_filled[row.tick - back] ~= true then shielded = false end
                    end
                    if shielded then fly_hits_protected[#fly_hits_protected + 1] = row.damage end
                end
            end
        end
        t.ticks(1)
        local up_ticks, stomp_tick_of = {}, {}
        for i = 1, #downs do
            for k = 1, #move_ticks do
                if move_ticks[k] > downs[i] and up_ticks[i] == nil then up_ticks[i] = move_ticks[k] end
            end
            for k = 1, #stomp_hits do
                if stomp_hits[k].tick > downs[i] and stomp_hits[k].tick < downs[i] + 33 and stomp_tick_of[i] == nil then
                    stomp_tick_of[i] = stomp_hits[k].tick
                end
            end
        end
        local pxf, pzf = {}, {}
        local last_px, last_pz = nil, nil
        for tk = 0, end_tick + 40 do
            if px_at[tk] ~= nil then last_px, last_pz = px_at[tk], pz_at[tk] end
            pxf[tk], pzf[tk] = last_px, last_pz
        end
        local down_at = {}
        for i = 1, #downs do
            for tk = downs[i], downs[i] + 32 do down_at[tk] = true end
        end
        t.ticks(1)

        -- THE TECHNIQUE ROWS, each check copied unchanged from tob_bloat.lua :1795-1849
        local behind_ticks, behind_flies = 0, 0
        if mark_tick ~= nil then
            for tk = mark_tick + 2, end_tick do
                if not down_at[tk] and not down_at[tk - 1] and bxf[tk - 1] ~= nil and pxf[tk - 1] ~= nil then
                    local mirror_x = 2 * ox + 59 - bxf[tk - 1]
                    local mirror_z = 2 * oz + 61 - bzf[tk - 1]
                    if math.max(math.abs(pxf[tk - 1] - mirror_x), math.abs(pzf[tk - 1] - mirror_z)) <= 1 then
                        behind_ticks = behind_ticks + 1
                        if fly_ticks[tk] then behind_flies = behind_flies + 1 end
                    end
                end
            end
        end
        local walk_total_ticks = 0
        if mark_tick ~= nil and downs[1] ~= nil then walk_total_ticks = downs[1] - mark_tick end
        for i = 1, #downs do
            if up_ticks[i] ~= nil then
                local next_down = downs[i + 1] or end_tick
                if next_down > up_ticks[i] then walk_total_ticks = walk_total_ticks + (next_down - up_ticks[i]) end
            end
        end
        t.check("tech.hide_behind_tank", behind_ticks > 0 and behind_flies == 0,
            "Bloat was up on " .. walk_total_ticks .. " ticks and a fly projectile flew on " .. #fly_list .. " of them; on the " .. behind_ticks .. " walking ticks the player stood directly behind the tank " .. behind_flies .. " flies flew")
        local on_my_tile, stayed = 0, 0
        for i = 1, #shadows do
            local row = shadows[i]
            if (pxf[row.tick] == row.x and pzf[row.tick] == row.z) or (pxf[row.tick - 1] == row.x and pzf[row.tick - 1] == row.z) then
                on_my_tile = on_my_tile + 1
                if pxf[row.tick + 2] == row.x and pzf[row.tick + 2] == row.z then stayed = stayed + 1 end
            end
        end
        t.check("tech.step_off_shadow", on_my_tile > 0 and (on_my_tile - stayed) * 2 >= on_my_tile,
            on_my_tile .. " falling-flesh shadows appeared on the player's own tile, the player was off it two ticks later for " .. (on_my_tile - stayed) .. " of them, " .. #hand_hits .. " hand hits landed in all")
        local pre_stomp = {}
        for i = 1, #rec.downs do
            if rec.downs[i].pre_stomp ~= nil then pre_stomp[#pre_stomp + 1] = rec.downs[i].pre_stomp end
        end
        local lowest = 999
        for i = 1, #pre_stomp do
            if pre_stomp[i] < lowest then lowest = pre_stomp[i] end
        end
        t.check("tech.eat_before_stomp", #pre_stomp > 0 and lowest > 40,
            "hitpoints read on the tick before each stomp: " .. table.concat(pre_stomp, ",") .. ", lowest " .. lowest .. " against the Entry stomp maximum of 40")
        local farther, compared = 0, 0
        for i = 1, #downs do
            local sx, sz = bxf[downs[i]], bzf[downs[i]]
            if stomp_tick_of[i] ~= nil and sx ~= nil and px_at[stomp_tick_of[i]] ~= nil and px_at[downs[i] + 33] ~= nil then
                local d_stomp = math.max(math.max(0, sx - px_at[stomp_tick_of[i]], px_at[stomp_tick_of[i]] - sx - 4), math.max(0, sz - pz_at[stomp_tick_of[i]], pz_at[stomp_tick_of[i]] - sz - 4))
                local d_rise = math.max(math.max(0, sx - px_at[downs[i] + 33], px_at[downs[i] + 33] - sx - 4), math.max(0, sz - pz_at[downs[i] + 33], pz_at[downs[i] + 33] - sz - 4))
                compared = compared + 1
                if d_rise > d_stomp then farther = farther + 1 end
            end
        end
        local flinch_moved = rec.flinches[1] and rec.flinches[1].moved
        t.check("tech.flinch_back", compared > 0 and farther >= 1 and flinch_moved ~= nil and flinch_moved >= 3,
            "after the stomp the player clicked back " .. tostring(flinch_moved) .. " tiles from the down Bloat and on the rise tick stood farther from it than on the stomp tick in " .. farther .. " of " .. compared .. " downs")
        local protected_max = 0
        for i = 1, #fly_hits_protected do
            if fly_hits_protected[i] > protected_max then protected_max = fly_hits_protected[i] end
        end
        t.check("tech.protect_from_missiles", #fly_hits_protected > 0 and protected_max <= 6,
            #fly_hits_protected .. " fly hits landed with Protect from Missiles lit on that tick and the six before it, the largest was " .. protected_max .. " against the unprotected Entry maximum of 8")
        t.check("bloat.killed", dead and not player_dead, "Bloat's npc_death row on tick " .. tostring(death_tick) .. "; " .. tostring(detail))

        -- THE MEASURE (reported against the kept tob_bloat run): duration, damage, supplies, inputs per tick
        local hist = { 0, 0, 0, 0 }
        for _, n in pairs(rec.inputs) do
            if n > 0 then hist[math.min(n, 4)] = hist[math.min(n, 4)] + 1 end
        end
        t.check("play.measure", true, string.format("room %s ticks (mark %s, death %s); damage taken %d; food %d, drinks %d; swings %d; inputs per tick: 1 on %d, 2 on %d, 3 on %d, 4+ on %d",
            tostring(death_tick and mark_tick and (death_tick - mark_tick)), tostring(mark_tick), tostring(death_tick), taken, #rec.eats, #rec.drinks, #rec.swings, hist[1], hist[2], hist[3], hist[4]))
        if size > 1 then t.expect("party.barrier.done", t.party.barrier("done", 9000)) end
        t.finish(0)
    end,
}
