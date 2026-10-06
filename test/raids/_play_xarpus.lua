-- _play_xarpus: Xarpus played through the PLAY LIBRARY (t.raid.play,
-- script/plugins/quest_driver/raid_play.lua) and the room's plan
-- (raid_play_tob_xarpus.lua; docs/minigames/raid_loop/PLAY_NOTES.md
-- "Xarpus").  An underscore harness, not a kept room (raid seam30
-- play_tob_xarpus): solo Entry with test/raids/tob_xarpus.lua's own
-- bring-alongs (:5-33, unchanged) and its entry (:37-72, the ::tobboss spec
-- reads left out), then ONE call, then the tick log.  The kept room's
-- technique rows technique.exhumed_cover and technique.spit_dodge
-- (tob_xarpus.lua :1253-1254) and its room-complete rows fight.done (:668)
-- and exit.walk_to_gate, exit.gate_crossed, exit.skeleton, exit.dawnbringer
-- (:1259-1271) are copied UNCHANGED; the reads that feed them are the kept
-- ANALYSIS (:671-710, :765-769, :845-848, :939-965) cut to what they use, with the
-- fight's own record (the covers and dodges the play made) from the play.
-- Not copied: technique.lag_step (the kept room holds a tile one from a
-- landing on purpose to read the splash radius), technique.stomp_skip (it
-- stands under him on purpose), technique.gaze_probe (it swings into his
-- gaze on purpose); a play does none of these.  The spec.* rows are
-- measurement rows, not technique or room-complete rows, and are not copied
-- either -- among them xarpus.av.death_a.seq (:1232-1234), whose nil `kill`
-- is the script error the kept room hits on a seed with no npc_death row.
return {
    id = "_play_xarpus",
    fixture = "fresh_lumbridge.ini",
    max_frames = 90000,
    setup = {
        "::clearinv",
        -- an Entry-mode Xarpus player has trained melee stats: attack and strength for the scythe, defence for the armour, hitpoints and prayer to live
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99",
        "::setlevel hitpoints 99",
        "::setlevel prayer 99",
        -- the scythe is the damage weapon of phase 2, the rapier the one-hitsplat weapon of phase 3 (Xarpus answers per hitsplat)
        "::give scythe_of_vitur",
        "::give ghrazi_rapier",
        -- the Dragon claws special (Slice and Dice) twice is the burst a raider opens phase 2 with: 200 percent of the bar is two uses
        "::give dragon_claws",
        -- the light weapon that finishes the stretch above the screech threshold: its hit cannot jump over the bracket<=5 of xarpus.p3.screech_pct_entry
        "::give bronze_dagger",
        -- melee armour a Xarpus player wears
        "::give torva_helm",
        "::give torva_chest",
        "::give torva_legs",
        "::give ferocious_gloves",
        "::give primordial_boots",
        "::give infernal_cape",
        "::give berzerker_ring",
        "::give zenyte_amulet_enchanted",
        -- food, eaten through the run (the room's own kit adds eight potions to the rest of the backpack)
        "::give shark 15",
        -- a super combat potion, drunk before the swings
        "::give 4dose2combat",
    },

    run = function(t)
        t.check("spec.scope", true, "mode=entry party=1")
        local srl, sdl = t.ticklog.start()
        t.expect("ticklog.start", srl, tostring(sdl))
        local sym = "tob_xarpus_combat_story"
        local AS = {}
        local F = { covers = {}, reads = {}, dodges = {}, retal = {}, turns = {} }
        -- the weapon and armour are worn before the room: Lumbridge is where a player dresses
        t.exec("equip.scythe", t.player.equip, "scythe_of_vitur")
        t.exec("equip.helm", t.player.equip, "torva_helm")
        t.exec("equip.body", t.player.equip, "torva_chest")
        t.exec("equip.legs", t.player.equip, "torva_legs")
        t.exec("equip.gloves", t.player.equip, "ferocious_gloves")
        t.exec("equip.boots", t.player.equip, "primordial_boots")
        t.exec("equip.cape", t.player.equip, "infernal_cape")
        t.exec("equip.ring", t.player.equip, "berzerker_ring")
        t.exec("equip.amulet", t.player.equip, "zenyte_amulet_enchanted")

        -- (1) the room, the way a player arrives
        local er, ed = t.raid.enter("tob", "xarpus", { mode = "entry" })
        t.expect("raid.enter", er, tostring(ed))
        local sr2, st2 = t.raid.state()
        t.expect("raid.state", (sr2 == "ok" and st2.mode == "entry" and st2.started == false) and "ok" or "bad", tostring(st2 and st2.line))
        local bossr, bossrow = t.npc.nearest("tob_xarpus_static_story", 20)
        local slr, bslot = t.ticklog.slot(bossrow)
        t.expect("boss.slot", (bossr == "ok" and slr == "ok") and "ok" or "bad", "boss client slot " .. tostring(bossrow and bossrow.slot) .. " -> world slot " .. tostring(bslot))
        local szr, szrow = t.npc.state("tob_xarpus_static_story")
        local size1 = (szr == "ok") and szrow.size or -1
        local _, ft0 = t.tick()

        -- (3) the room starts by the player's own click on the barrier
        local frr, fight = t.raid.start_tile()
        t.player.walk_to(fight.x, fight.z - 3, 20)
        local cr, cd = t.player.click_loc("tob_arena_barrier", 1)
        t.check("barrier.click", cr == "ok", tostring(cd))
        t.chat.play({ "options", "choose:Yes, begin the fight." })
        t.ticklog.mark("room start")
        t.check("fight.begins", t.msg.expect("The fight begins") == "ok", "the fight-begins line is in the chat ring")

        -- (2) THE FIGHT: the library and the room's plan, nothing else
        local result, detail, rec = t.raid.play("tob_xarpus", { mode = "entry", weapon = "scythe_of_vitur", max_ticks = 900 })
        t.check("play.fight", result == "ok", tostring(detail))
        local XA = rec.xa or { covers = {}, dodges = {}, turns = {}, phase = 0, stops = 0, moves3 = 0, waits3 = 0 }
        F.covers = XA.covers
        F.dodges = XA.dodges
        local _, now_end = t.tick()
        t.ticks(8)

        -- (3) the tick log: the kept room's readings (tob_xarpus.lua :671-710)
        local _, drr = t.ticklog.rows({ kind = "npc_death", slot = bslot })
        local kill = drr and drr[1] or nil
        local phase3 = XA.phase == 3
        local eats = #rec.eats
        local _, rtu = t.ticklog.rows({ kind = "npc_retype", slot = bslot })
        -- the stand-up tick: the second retype after the barrier (feeding ->
        -- combat; tob_xarpus.lua :171-174 reads it the same way)
        local U = nil
        do
            local seen = 0
            for i = 1, #rtu do
                if rtu[i].tick >= ft0 then
                    seen = seen + 1
                    if seen == 2 then U = rtu[i].tick end
                end
            end
        end
        U = U or ft0
        local _, ptr = t.ticklog.rows({ kind = "player_tile" })
        local tile_at = {}
        for i = 1, #ptr do tile_at[ptr[i].tick] = { x = ptr[i].x, z = ptr[i].z } end
        local _, pjr = t.ticklog.rows({ kind = "projectile" })
        local orbs, acid, chains = {}, {}, {}
        for i = 1, #pjr do
            if pjr[i].tick >= ft0 then
                if pjr[i].spotanim == 1550 then
                    orbs[#orbs + 1] = pjr[i]
                elseif pjr[i].spotanim == 1555 then
                    if pjr[i].src_x >= 6432 and pjr[i].src_x <= 6436 and pjr[i].src_z >= 97 and pjr[i].src_z <= 101 then
                        acid[#acid + 1] = pjr[i]
                    else
                        chains[#chains + 1] = pjr[i]
                    end
                end
            end
        end
        local _, lsr = t.ticklog.rows({ kind = "loc_set" })
        local ex_rise, ex_gone, pool_set, pool_gone = {}, {}, {}, {}
        for i = 1, #lsr do
            if lsr[i].tick >= ft0 then
                if lsr[i].loc == 32743 then
                    ex_rise[#ex_rise + 1] = lsr[i]
                elseif lsr[i].loc == 32744 then
                    pool_set[#pool_set + 1] = lsr[i]
                elseif lsr[i].loc == -1 then
                    if lsr[i].tick < U then ex_gone[#ex_gone + 1] = lsr[i] else pool_gone[#pool_gone + 1] = lsr[i] end
                end
            end
        end
        local _, hur = t.ticklog.rows({ kind = "hit_player" })
        local _, hnr = t.ticklog.rows({ kind = "hit_npc", slot = bslot })
        local _, anr = t.ticklog.rows({ kind = "npc_anim", slot = bslot })
        local spit_t = {}
        for i = 1, #anr do
            if anr[i].seq == 8059 and anr[i].tick >= U then spit_t[#spit_t + 1] = anr[i].tick end
        end
        local is_spit = {}
        for i = 1, #spit_t do is_spit[spit_t[i]] = true end
        -- the retaliation hits the kept room reads at :325-330 (an Entry
        -- poison hit is 1-6; a retaliation is 38 and up)
        for i = 1, #hur do
            if XA.p3_tick ~= nil and hur[i].tick > XA.p3_tick and hur[i].hitsplat == 28 and hur[i].damage >= 30 then
                F.retal[#F.retal + 1] = { tick = hur[i].tick, damage = hur[i].damage }
            end
        end
        -- the boss pool as the log tells it: 75% of 520 at the wake
        -- (X xarpus.p1.start_hp_pct), 6 per heal orb landed (heal_amount.entry),
        -- less every hitsplat dealt
        local hp_est = 390
        for i = 1, #orbs do hp_est = hp_est + 6 end
        for i = 1, #hnr do hp_est = hp_est - (hnr[i].damage or 0) end
        t.expect("fight.done", (kill ~= nil) and "ok" or "bad", "boss npc_death row " .. tostring(kill and kill.tick) .. ", phase 3 " .. tostring(phase3) .. ", ended at tick " .. now_end .. ", hp est " .. hp_est .. ", eats " .. eats .. ", dodges " .. #F.dodges .. ", retaliation rows " .. #F.retal)
        local cdesc = ""
        for i = 1, #F.covers do
            cdesc = cdesc .. string.format("[rise %d at %d,%d arrived %d stood %d,%d] ", F.covers[i].rise, F.covers[i].x, F.covers[i].z, F.covers[i].arrive, F.covers[i].at_x, F.covers[i].at_z)
        end
        local covered_heals = 0
        for i = 1, #orbs do
            local me1 = tile_at[orbs[i].tick - 1]
            if me1 and me1.x == orbs[i].src_x and me1.z == orbs[i].src_z then covered_heals = covered_heals + 1 end
        end
        local dodge_n, dodge_ok, dodge_near, dodge_on = 0, 0, 0, 0
        local dodge_bad = ''
        local dodge_late = 0
        local probe_rec = nil
        for i = 1, #F.dodges do
            local rec = F.dodges[i]
            if not rec.probe and rec.r2 ~= nil then
                dodge_n = dodge_n + 1
                local t1 = tonumber(string.match(tostring(rec.d1), "resolved at tick (%d+)"))
                local t2 = tonumber(string.match(tostring(rec.d2), "resolved at tick (%d+)"))
                if t1 == rec.S and t2 == rec.S + 1 then dodge_ok = dodge_ok + 1 end
                if t1 ~= nil and t1 > rec.S then dodge_n = dodge_n - 1; dodge_late = dodge_late + 1 end
                if not (t1 == rec.S and t2 == rec.S + 1) then dodge_bad = dodge_bad .. ' [spit ' .. rec.S .. ': steps resolved ' .. tostring(t1) .. ',' .. tostring(t2) .. ']' end
                local arow = nil
                for j = 1, #acid do
                    if acid[j].tick == rec.S then arow = acid[j] end
                end
                if arow ~= nil then
                    local Lr = nil
                    for j = 1, #pool_set do
                        if Lr == nil and pool_set[j].x == arow.dst_x and pool_set[j].z == arow.dst_z and pool_set[j].tick >= rec.S + 1 and pool_set[j].tick <= rec.S + 5 then Lr = pool_set[j].tick end
                    end
                    if Lr == nil then Lr = rec.S + 3 end
                    local m1 = tile_at[Lr - 1]
                    if m1 and m1.x == arow.dst_x and m1.z == arow.dst_z then dodge_on = dodge_on + 1 end
                    if rec.score < 20 and m1 and math.max(math.abs(m1.x - arow.dst_x), math.abs(m1.z - arow.dst_z)) <= 1 then dodge_near = dodge_near + 1 end
                end
            elseif rec.probe then
                probe_rec = rec
            end
        end

        -- (4) THE TECHNIQUE ROWS, copied unchanged from tob_xarpus.lua :1253-1254
        t.expect("technique.exhumed_cover", (#F.covers >= 6 and covered_heals == 0) and "ok" or "bad", "stood on " .. #F.covers .. " of " .. #ex_rise .. " exhumed, " .. covered_heals .. " heal orbs fired on a tick the player held the tile at the end of the tick before; arrivals " .. cdesc)
        t.expect("technique.spit_dodge", (dodge_n >= 3 and dodge_ok == dodge_n and dodge_on == 0 and dodge_near * 4 <= dodge_n) and "ok" or "bad", dodge_n .. " dodges timed on the spit cadence, the first step resolved on the spit tick and the second on the next in " .. dodge_ok .. " of " .. dodge_n .. ", " .. dodge_on .. " landings on the tile the player held, " .. dodge_near .. " within one tile of it, the recipe accepts the splash of an adjacent landing" .. dodge_bad .. ", " .. dodge_late .. " late after an eat, not counted as timed (dodges with a clean tile to go to; one that had only pool tiles left is not counted)")
        -- the play's own guard on the row above: technique.spit_dodge reads a
        -- dodge against the spit the plan TIMED it on (rec.S), so every rec.S
        -- must be a real spit tick (an 8059 row), or the row checks nothing
        -- (a dodge on the one slot after his last spit is the slot the screech
        -- cancelled: the plan cannot see a screech before the spit fails to
        -- come, E:212; it is listed, not counted)
        local on_spit, off_list, after_last = 0, "", 0
        local last_spit = spit_t[#spit_t] or -1
        for i = 1, #F.dodges do
            if is_spit[F.dodges[i].S] then
                on_spit = on_spit + 1
            elseif F.dodges[i].S > last_spit and F.dodges[i].S <= last_spit + 4 then
                after_last = after_last + 1
            else
                off_list = off_list .. " " .. F.dodges[i].S
            end
        end
        local lag_txt = ""
        for k2, c2 in pairs(XA.seen_lag or {}) do lag_txt = lag_txt .. " lag" .. k2 .. "x" .. c2 end
        t.check("play.dodge_on_spit", #F.dodges >= 3 and on_spit + after_last == #F.dodges and after_last <= 1, on_spit .. " of " .. #F.dodges .. " dodges timed on a tick with an 8059 row, " .. after_last .. " on the slot after his last spit (off:" .. off_list .. "); " .. #spit_t .. " spits; seen" .. lag_txt .. ", regrids " .. tostring(XA.regrid or 0))
        -- the play's own phase 3 row (not in the kept room, which probes the
        -- gaze on purpose): "never attack the quadrant he faces" (W:851)
        t.check("play.gaze_kept", phase3 and #F.retal == 0, #F.retal .. " retaliation hitsplat(s) after the screech; " .. #XA.turns .. " turns seen, " .. XA.moves3 .. " ticks moving off his gaze, " .. XA.stops .. " stops before his next turn, " .. XA.waits3 .. " ticks waiting")
        -- the measurement (the seam's report reads it)
        local mark_tick = nil
        do
            local _, mrows = t.ticklog.rows({ kind = "mark" })
            for i = 1, #mrows do if mrows[i].label == "room start" then mark_tick = mrows[i].tick end end
        end
        local taken, taken_n = 0, 0
        for i = 1, #hur do
            if mark_tick ~= nil and hur[i].tick >= mark_tick and (kill == nil or hur[i].tick <= kill.tick) and (hur[i].damage or 0) > 0 then
                taken = taken + hur[i].damage
                taken_n = taken_n + 1
            end
        end
        local drinks = #rec.drinks
        local dtxt = ""
        for i = 1, #F.dodges do
            local d = F.dodges[i]
            dtxt = dtxt .. string.format("[S%d %d,%d>%d,%d s%d] ", d.S, d.from_x, d.from_z, d.t2x or -1, d.t2z or -1, d.score)
        end
        t.check("play.measure", true, string.format("kill %s ticks from the mark (%s to %s), stand-up U %s (plan saw %s), screech seen %s; taken %d in %d hits; food %d, drinks %d; dodges %s; %s",
            tostring(kill and mark_tick and (kill.tick - mark_tick)), tostring(mark_tick), tostring(kill and kill.tick), tostring(U), tostring(XA.u_tick), tostring(XA.p3_tick),
            taken, taken_n, eats, drinks, dtxt, string.sub(tostring(detail), 1, 600)))
        t.ticks(6)

        -- (5) the room's end, copied unchanged from tob_xarpus.lua :1259-1271
        -- the room's end: the skeleton in the corridor holds the Dawnbringer
        local wr, wd = t.player.walk_to(6434, 106, 12)
        t.check("exit.walk_to_gate", wr == "ok", "walk to the exit gate's near side: " .. tostring(wr) .. " " .. tostring(wd))
        local gr, gd = t.player.click_loc("tob_arena_barrier", 1, { at = { 6434, 107 } })
        t.ticks(2)
        local _, gat = t.world.tile()
        local gx, gz = gat.x, gat.z
        t.check("exit.gate_crossed", gz == 108, "pressed the exit gate (" .. tostring(gr) .. "), stood on " .. tostring(gx) .. "," .. tostring(gz) .. " (the gate answers timeout even when crossed)")
        local xr, xd = t.player.click_loc("tob_skeleton_with_weapon", 1)
        t.check("exit.skeleton", xr == "ok", tostring(xd))
        t.chat.continue_()
        local wok = t.inv.await("verzik_special_weapon", 1, 5)
        t.check("exit.dawnbringer", wok == "ok" or wok == true, "inventory holds verzik_special_weapon after the skeleton: " .. tostring(wok))
        t.finish(0)
        return
    end,
}
