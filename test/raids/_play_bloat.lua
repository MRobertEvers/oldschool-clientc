-- _play_bloat: Pestilent Bloat, NORMAL, a party of three, played through the
-- PLAY LIBRARY (t.raid.play; docs/minigames/raid_loop/PLAY_NOTES.md "Bloat,
-- Normal trio").  Moved out of _play_smoke.lua's `--party 3` branch by raid
-- seam32 play_tob_bloat_normal, so the trio has its own id: run.py reads the
-- party size from the `party` field (`seed_survey.py _play_bloat` and
-- `party_repeat.py _play_bloat --runs 3` need no --party), and _play_smoke is
-- the Entry solo harness again.  The roles are the sources': p1 is the one
-- raider in the room on the first walk (it crosses when Bloat is on the far
-- side, hides, and does the Defence-drain run-by: W:687), p2/p3 enter on the
-- first down (W:689 "As soon as Bloat deactivates, the rest of the team should
-- enter"); every raider then walks with Bloat behind the tank, attacks every
-- down and runs from the stomp (W:689).  The fight itself is one call: the
-- library plus the room's plan.  Everything after it reads the leader's tick log.
local role = (QD_PARTY and QD_PARTY.role) or 1
local size = (QD_PARTY and QD_PARTY.size) or 1
-- tob_bloat_normal.lua's party kit: ::maxmelee (the scythe of vitur, wiki
-- "Recommended equipment"), anglerfish ("make sure that you eat your angler",
-- transcripts/yt_KF9y2GYTJ-A.md:675), restores, brews, a super combat
local kit = {
    "::clearinv", "::maxmelee", "::setlevel prayer 99",
    "::give anglerfish 14", "::give br_4dose2restore 2", "::give 4dose2combat 1",
    "::give br_4dosepotionofsaradomin 4",
}
-- raid seam32: the run-by's Dragon warhammer, carried by the raider who is
-- in the room on the first walk (W:687 "one or two players should do a
-- run-by on the boss with a Bandos godsword special to lower its Defence";
-- the BGS is not in this cache, tob_bloat_normal.lua:5; "a dragon
-- warhammer is basically essential", yt_4i4lv-srJkw.md:45)
-- raid seam42 play_tob_bloat_follows_blert: no run-by, so no hammer.  The 30
-- recorded Normal trio rooms (reference/bloat_normal_3.json; build/blert/bloat)
-- hold no Dragon warhammer special and one Bandos godsword special.
-- raid seam32: a raider's Agility.  Every raider hides on the walk and runs
-- the ring at Bloat's own speed (it RUNS at 40-60%, W:679), and a fresh
-- character's run energy is spent by the third walk: svbplaysmoke t346-361
-- the party walked one tile a tick beside a running Bloat and took a fly a
-- tick each for twenty ticks.  "If you're in a melee role, especially if you
-- have less than 70 agility, I'd strongly advise buying a stamina potion"
-- (yt_4i4lv-srJkw.md 0:12:09): the guide's raider has the level.
kit[#kit + 1] = "::setlevel agility 99"

return {
    id = "_play_bloat",
    party = 3,
    fixture = "fresh_lumbridge.ini",
    max_frames = 120000,
    setup = kit,

    run = function(t)
        local mode = "normal"
        local boss = "tob_bloat"
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
            -- raid seam42: the whole trio is in from the start.  The wiki's
            -- "As soon as Bloat deactivates, the rest of the team should enter"
            -- (W:689) is what 3 of 22 recorded death-free Normal trio rooms
            -- did; in 15 all three were in before the first down (Blert,
            -- build/blert/bloat; seam42 progress notes), and a raider who
            -- enters on the down swings first at age 7-9 against the
            -- reference's 3 (seam42 _play_bloat before: down1 swings 4 and 2).
            local xr, xd = t.player.click_loc("tob_arena_barrier", 1)
            t.check("play.barrier_cross", xr == "ok", "p" .. role .. " crossed with the leader: " .. tostring(xd))
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
        local fly_ticks, fly_list, fly_at_me = {}, {}, {}
        local _, projectiles = t.ticklog.rows({ kind = "projectile", spotanim = 1568 })
        for i = 1, #projectiles do
            local row = projectiles[i]
            if row.spotanim == 1568 and fly_ticks[row.tick] == nil then
                fly_ticks[row.tick] = true
                fly_list[#fly_list + 1] = row.tick
            end
            -- raid seam32: in a party a fly flies at EVERY raider Bloat sees and
            -- spreads between raiders (W:673), so the hide row counts only the
            -- flies whose landing tile (dst, a packed coord) is the leader's own
            -- tile that tick or the one before
            if row.spotanim == 1568 and row.dst ~= nil then
                local dx, dz = math.floor(row.dst / 16384) % 16384, row.dst % 16384
                if (px_at[row.tick] == dx and pz_at[row.tick] == dz) or (px_at[row.tick - 1] == dx and pz_at[row.tick - 1] == dz) then
                    fly_at_me[row.tick] = true
                end
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
        -- the most one fly lands unprotected: Entry 8 (tob.constant :752),
        -- Normal 20 (tob.constant :746; W:673 "up to 20 damage every tick")
        local fly_max = (mode == "entry") and 8 or 20
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
                elseif row.damage <= fly_max then
                    -- raid seam29: the window is the fly's own flight, its launch
                    -- tick to its landing.  The damage is rolled and the prayer read
                    -- AT LAUNCH (tob_bloat.rs2 ~tob_bloat_fly_hit queues
                    -- combat_damage_player with ~tob_bloat_fly_damage, which reads
                    -- ~prayer_is_on(protectfrommissiles)).  tob_bloat.lua's six
                    -- ticks back could only count a fly that landed 7+ ticks after
                    -- the prayer went up, so a raider who hid on every walk had only
                    -- the rise tick's fly (prayer up on T+33, the plan's) and the row
                    -- had no evidence (seam29 survey: _play_smoke, svcplaysmoke).
                    -- Which fly landed is not in the log, so the window starts at the
                    -- EARLIEST launch that could have (six ticks back at most): svbplaysmoke's
                    -- 7 at t27 was launched t24, not t25, before its prayer was in force
                    -- (a press is in force from the NEXT tick's npc phase, DRIVER_NOTES
                    -- "A prayer press is in force for the next npc phase").
                    -- No launch row within six ticks: the six-tick rule, unchanged.
                    local from = row.tick - 6
                    for k = #fly_list, 1, -1 do
                        if fly_list[k] <= row.tick and fly_list[k] >= row.tick - 6 then from = fly_list[k] end
                    end
                    local shielded = true
                    for tk = from, row.tick do
                        if shield_filled[tk] ~= true then shielded = false end
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

        -- THE TECHNIQUE ROWS, each check copied unchanged from tob_bloat.lua :1795-1849,
        -- except protect_from_missiles' window: the fly's flight (raid seam29, above)
        local behind_ticks, behind_flies = 0, 0
        if mark_tick ~= nil then
            for tk = mark_tick + 2, end_tick do
                if not down_at[tk] and not down_at[tk - 1] and bxf[tk - 1] ~= nil and pxf[tk - 1] ~= nil then
                    local mirror_x = 2 * ox + 59 - bxf[tk - 1]
                    local mirror_z = 2 * oz + 61 - bzf[tk - 1]
                    if math.max(math.abs(pxf[tk - 1] - mirror_x), math.abs(pzf[tk - 1] - mirror_z)) <= 1 then
                        behind_ticks = behind_ticks + 1
                        if (size == 1 and fly_ticks[tk]) or (size > 1 and fly_at_me[tk]) then behind_flies = behind_flies + 1 end
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
            "Bloat was up on " .. walk_total_ticks .. " ticks and a fly projectile flew on " .. #fly_list .. " of them; on the " .. behind_ticks .. " walking ticks the player stood directly behind the tank " .. behind_flies .. " flies flew" .. (size > 1 and " at the player's own tile (a party: flies fly at every raider Bloat sees and spread, W:673)" or ""))
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
        -- NORMAL (raid seam32 play_tob_bloat_normal): the Entry rows above
        -- assert Entry's numbers and Entry's stay-and-flinch; Normal leaves.
        -- "Unless the boss is below 3% health, it is recommended to run
        -- away after the last attack, since stomps and flies have a high
        -- chance of killing a player" (W:689): no raider takes a stomp on
        -- any down that reached it (T+29, ET 3.1; a hand's splat tick is
        -- not a stomp).  Every raider's hit_player row is in the leader's log.
        local reached, stomp_taken, stomp_list = 0, 0, {}
        for i = 1, #downs do
            if downs[i] + 29 <= end_tick then reached = reached + 1 end
        end
        for i = 1, #player_hits do
            local row = player_hits[i]
            for k = 1, #downs do
                if row.tick == downs[k] + 29 and not splat_ticks[row.tick] then
                    stomp_taken = stomp_taken + 1
                    stomp_list[#stomp_list + 1] = "p" .. tostring(row.pid) .. " " .. row.damage .. " t" .. row.tick
                end
            end
        end
        t.check("tech.leave_before_stomp", reached > 0 and stomp_taken == 0,
            reached .. " downs reached the stomp (T+29) and " .. stomp_taken .. " stomp hits landed on the party" .. (#stomp_list > 0 and (": " .. table.concat(stomp_list, ", ")) or ""))
        local protected_max = 0
        for i = 1, #fly_hits_protected do
            if fly_hits_protected[i] > protected_max then protected_max = fly_hits_protected[i] end
        end
        -- W:673 "up to 20 damage every tick, reduced by 25% if Protect from Missiles are active"
        t.check("tech.protect_from_missiles", #fly_hits_protected > 0 and protected_max <= 15,
            #fly_hits_protected .. " fly hits landed with Protect from Missiles lit from the fly's launch to its landing, the largest was " .. protected_max .. " against the protected Normal maximum of 15 (unprotected 20)")
        -- raid seam42: THE REFERENCE ROWS.  The room against Blert's 19 recorded
        -- death-free Normal trio rooms (docs/minigames/theater_of_blood/sources/
        -- blert_api/reference/bloat_normal_3.json): outcome.room_ticks 137
        -- [75-195], and no recorded room needed more than three downs inside
        -- that range (outcome.phase.down3 n=2, no down4).
        t.ticks(1)
        local _, boss_hits = t.ticklog.rows({ kind = "hit_npc", slot = boss_slot })
        local room_ticks = (death_tick ~= nil and mark_tick ~= nil) and (death_tick - mark_tick) or nil
        t.check("blert.room_ticks", room_ticks ~= nil and room_ticks >= 75 and room_ticks <= 195,
            "room " .. tostring(room_ticks) .. " ticks from the mark to Bloat's death; the reference's 19 rooms: 137 [75-195]")
        t.check("blert.downs", #downs >= 1 and #downs <= 3,
            #downs .. " downs to the kill; the reference: 2 in 17 of 19 rooms, 3 in 2 (outcome.phase.down3 n=2)")
        -- the measure per down: dealt, zeros (hit_npc carries no dealer pid,
        -- so zeros are per down)
        local per_down = {}
        for i = 1, #downs do
            local dealt, hits, zeros = 0, 0, 0
            for k = 1, #boss_hits do
                local row = boss_hits[k]
                if row.tick > downs[i] and row.tick <= downs[i] + 33 then
                    dealt, hits = dealt + row.damage, hits + 1
                    if row.damage == 0 then zeros = zeros + 1 end
                end
            end
            per_down[#per_down + 1] = string.format("d%d t%d %d in %d hits, %d zeros", i, downs[i], dealt, hits, zeros)
        end
        local taken_by, deaths = {}, {}
        for i = 1, #player_hits do
            local pid = tostring(player_hits[i].pid)
            taken_by[pid] = (taken_by[pid] or 0) + player_hits[i].damage
        end
        local taken_list = {}
        for pid, n in pairs(taken_by) do taken_list[#taken_list + 1] = "pid" .. pid .. " " .. n end
        table.sort(taken_list)
        t.check("play.measure_party", true, "downs " .. #downs .. " to the kill; " .. table.concat(per_down, "; ") .. "; damage taken by pid: " .. table.concat(taken_list, ", "))
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
