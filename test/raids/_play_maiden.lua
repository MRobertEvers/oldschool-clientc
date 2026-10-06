-- _play_maiden: the Maiden of Sugadinti played through the PLAY LIBRARY
-- (t.raid.play, script/plugins/quest_driver/raid_play.lua) and the room's plan
-- (raid_play_tob_maiden.lua; docs/minigames/raid_loop/PLAY_NOTES.md "Maiden").
-- An underscore harness, not a kept room (raid seam30 play_tob_maiden): solo
-- Entry with test/raids/tob_maiden.lua's own bring-alongs and entry, then ONE
-- call, then the tick log.  The kept room's technique rows (tech.sidestep_scan,
-- tech.protect_magic, tech.far_dodge, tech.bow_flick) and room-complete rows
-- (room.complete_line, room.complete_duration, room.cleared) are copied
-- UNCHANGED from tob_maiden.lua :1706-1710, :682-691, :1635-1649, :1714-1715;
-- the reads that feed them are tob_maiden.lua's ANALYSIS, cut to what they use,
-- with the fight's own record (far moves, flicks, the prayer tick) from the play.
return {
    id = "_play_maiden",
    fixture = "fresh_lumbridge.ini",
    max_frames = 200000,
    setup = {
        -- an empty backpack so the food and gear below all fit
        "::clearinv",
        -- combat stats an Entry Mode maiden player brings along
        "::setlevel attack 99",
        "::setlevel strength 99",
        "::setlevel defence 99",
        "::setlevel ranged 99",
        "::setlevel hitpoints 99",
        "::setlevel prayer 99",
        -- ranged gear worn for the fight (Maiden is slain from range); worn from the start so the backpack keeps room for food
        "::give twisted_bow",
        "::wield twisted_bow",
        "::give dragon_arrow 1000",
        "::wield dragon_arrow",
        "::give masori_mask",
        "::wield masori_mask",
        "::give masori_body",
        "::wield masori_body",
        "::give masori_chaps",
        "::wield masori_chaps",
        "::give avas_assembler",
        "::wield avas_assembler",
        -- Ancient Magicks for spec.maiden.freeze_full_bonus (Ice Burst is on the Matomenos freeze curve); the spellbook var is a bring-along, not a raid var
        "::setvar varb4070_spellbook 1",
        "::setlevel magic 99",
        -- the runes of Ice Barrage for the plan's freeze (W:594 "Ice Barrage is
        -- essentially mandatory"; magic_combat_spells.dbrow [magic_spell_ice_barrage]
        -- runesrequired water 6, blood 2, death 4): the kept room's chaos runes
        -- (Ice Burst) give their slot to blood runes, the pack is full
        "::give water_rune 2000",
        "::give blood_rune 1000",
        "::give death_rune 1000",
        -- magic attack gear for the same row: ancestral hat 8 + top 35 + bottom 26, kodai wand 28 and arcane spirit shield 20 are carried and put on
        -- for the second arm; eternal boots 8 and magus ring 15 are worn from the start
        "::give ancestral_hat",
        "::give ancestral_robe_top",
        "::give ancestral_robe_bottom",
        "::give kodai_wand",
        "::give arcane",
        "::give eternal_boots",
        "::wield eternal_boots",
        "::give magus_ring",
        "::wield magus_ring",
        -- food for the blackstorm and pool damage (10 sharks and six brews: the pack's other slots hold the magic set and runes)
        "::give shark 10",
        -- two Saradomin brews: more healing, and the room's debug kit adds none of its own when the pack holds one
        "::give br_4dosepotionofsaradomin 6",
        -- three prayer restores: Protect from Magic drains through the fight and the pools drain more
        "::give br_4dose2restore 3",
        -- a melee weapon for the bow flick (the drain is judged on the weapon worn when she aims)
        "::give abyssal_whip",
    },

    run = function(t)
        -- (1) the room's entry, as tob_maiden.lua :58-90 does it
        t.check("spec.scope", true, "mode=entry party=1")
        local lr, ld = t.ticklog.start()
        t.check("ticklog.start", lr == "ok", tostring(ld))
        local er, ed = t.raid.enter("tob", "maiden", { mode = "entry" })
        t.check("raid.enter", er == "ok", tostring(ed))
        local br, brow = t.npc.nearest("tob_maiden_100_story", 30)
        t.check("boss.present", br == "ok", tostring(brow and brow.slot))
        local wr, ws = t.ticklog.slot(brow)
        t.check("boss.slot", wr == "ok", tostring(ws))
        local pre = {}
        lr, ld = t.player.click_loc("tob_arena_barrier", 1)
        t.check("barrier.click", lr == "ok", tostring(ld))
        lr, ld = t.chat.play({ "options", "choose:Yes, begin the fight." })
        t.check("barrier.confirm", lr == "ok", tostring(ld))
        lr, ld = t.ticklog.mark("room start")
        t.check("room.mark", lr == "ok", tostring(ld))
        local tr, mark_tick = t.tick()
        t.check("room.tick", tr == "ok", tostring(mark_tick))

        -- (2) THE FIGHT: the library and the room's plan, nothing else
        local result, detail, rec = t.raid.play("tob_maiden", { mode = "entry", weapon = "twisted_bow", max_ticks = 1100 })
        t.check("play.fight", result == "ok", tostring(detail))
        local m = rec.m or { far_moves = {}, flicks = {}, casts = {}, swaps = {}, attacks = {}, presteps = 0, forms = {} }
        -- her death (seq 8093, 8094) and the room's line take nine ticks (K spec death_total)
        t.ticks(12)
        -- the room's line, copied unchanged from tob_maiden.lua :682-691
        t.ticks(4)
        -- the chat line the room prints when it ends (read once the room is done; asserted against the log's own duration below)
        pre.wave_text = nil
        do
            local _, wave_lines = t.msg.last(120)
            for l = 1, #wave_lines do
                if string.find(wave_lines[l].text, "Wave 'The Maiden of Sugadinti' (Entry Mode) complete!", 1, true) then pre.wave_text = wave_lines[l].text end
            end
            t.check("room.complete_line", pre.wave_text ~= nil, "chat line: " .. tostring(pre.wave_text))
        end
        -- ANALYSIS (tob_maiden.lua :692-1012, :1325-1356, :1651-1656, cut to the rows below)
        local seq_blood = 8091 -- maiden_attack_blood
        local seq_auto = 8092 -- maiden_attack_special
        local proj_blood = 1578 -- maiden_blood_proj
        local fx_pool = 1579 -- maiden_lingering_blood
        local _, anim_rows = t.ticklog.rows({ kind = "npc_anim", slot = ws })
        t.ticks(1)
        local attacks = {}
        for k = 1, #anim_rows do
            local sq = anim_rows[k].seq
            if sq == seq_blood or sq == seq_auto then
                attacks[#attacks + 1] = { tick = anim_rows[k].tick, blood = (sq == seq_blood) }
            end
        end
        local _, proj_rows = t.ticklog.rows({ kind = "projectile" })
        t.ticks(1)
        local _, hit_player_rows = t.ticklog.rows({ kind = "hit_player" })
        t.ticks(1)
        local _, retype_rows = t.ticklog.rows({ kind = "npc_retype" })
        t.ticks(1)
        local _, death_rows = t.ticklog.rows({ kind = "npc_death" })
        t.ticks(1)
        local _, free_rows = t.ticklog.rows({ kind = "npc_free" })
        t.ticks(1)
        local _, fx_rows = t.ticklog.rows({ kind = "map_spotanim" })
        t.ticks(1)
        local _, tile_rows = t.ticklog.rows({ kind = "player_tile" })
        t.ticks(1)
        local first_retype = 1000000000
        for k = 1, #retype_rows do
            if retype_rows[k].slot == ws and retype_rows[k].tick >= mark_tick and retype_rows[k].tick < first_retype then first_retype = retype_rows[k].tick end
        end
        local death_tick = nil
        local free_tick = nil
        for k = 1, #death_rows do if death_rows[k].slot == ws then death_tick = death_rows[k].tick end end
        for k = 1, #free_rows do if free_rows[k].slot == ws then free_tick = free_rows[k].tick end end
        local player_at = {}
        for k = 1, #tile_rows do player_at[tile_rows[k].tick] = { x = tile_rows[k].x, z = tile_rows[k].z } end
        local pools = {}
        for k = 1, #fx_rows do
            if fx_rows[k].spotanim == fx_pool then
                pools[#pools + 1] = { x = fx_rows[k].x, z = fx_rows[k].z, from = fx_rows[k].tick, to = fx_rows[k].tick + 11 }
            end
        end
        -- the prayer: the tick the play first READ Protect from Magic lit (the kept room's set tick)
        local prayer_on_tick = rec.prayer_on_tick
        -- blackstorm hits before the first transmog, protected or not (tob_maiden.lua :830-857)
        local unprotected_hits = {}
        local protect_notes = ""
        local protected_hits = {}
        for k = 1, #attacks do
            if not attacks[k].blood then
                local found = nil
                for _, off in ipairs({ 5, 4, 6, 3, 7 }) do
                    for h = 1, #hit_player_rows do
                        if hit_player_rows[h].tick == attacks[k].tick + off and hit_player_rows[h].npc_slot == ws and found == nil then
                            found = off
                            attacks[k].hit = hit_player_rows[h].damage
                        end
                    end
                end
            end
        end
        for a = 1, #attacks do
            if not attacks[a].blood and attacks[a].hit ~= nil and attacks[a].tick + 5 < first_retype then
                if prayer_on_tick ~= nil and attacks[a].tick >= prayer_on_tick then
                    protected_hits[#protected_hits + 1] = attacks[a].hit
                    protect_notes = protect_notes .. "[npc_anim 8092 t" .. attacks[a].tick .. " -> hit_player " .. attacks[a].hit .. " protected]"
                else
                    unprotected_hits[#unprotected_hits + 1] = attacks[a].hit
                    protect_notes = protect_notes .. "[npc_anim 8092 t" .. attacks[a].tick .. " -> hit_player " .. attacks[a].hit .. " unprotected]"
                end
            end
        end
        -- blood throws: the splat under the player is the 1578 row with target -1 and the shortest flight (:927-950)
        local throws = {}
        for k = 1, #attacks do
            if attacks[k].blood then
                local main = nil
                local shortest = nil
                for p = 1, #proj_rows do
                    local row = proj_rows[p]
                    if row.spotanim == proj_blood and row.tick == attacks[k].tick and row.target == -1 then
                        local dur = row.end_cycle - row.start_cycle
                        if shortest == nil or dur < shortest then shortest = dur main = row end
                    end
                end
                throws[#throws + 1] = { tick = attacks[k].tick, main = main }
            end
        end
        -- the scan lead (:951-1012): a throw on the tick the player moved aims at the tile of the tick before
        local lead_ok = 0
        local lead_notes = ""
        local lead_bad = 0
        for k = 1, #throws do
            local th = throws[k]
            if th.main ~= nil then
                local before = player_at[th.tick - 1]
                local now_tile = player_at[th.tick]
                if before ~= nil and now_tile ~= nil and (before.x ~= now_tile.x or before.z ~= now_tile.z) then
                    if th.main.dst_x == before.x and th.main.dst_z == before.z then lead_ok = lead_ok + 1
                    elseif th.main.dst_x == now_tile.x and th.main.dst_z == now_tile.z then lead_bad = lead_bad + 1 end
                    lead_notes = lead_notes .. "[npc_anim 8091 t" .. th.tick .. ": player_tile t" .. (th.tick - 1) .. " " .. before.x .. "," .. before.z .. " then t" .. th.tick .. " " .. now_tile.x .. "," .. now_tile.z .. "; projectile 1578 target -1 dst " .. th.main.dst_x .. "," .. th.main.dst_z .. "]"
                end
            end
        end
        -- the play's steps on the tick before her attack, and how many moved on the next tick
        local sidesteps = m.presteps or 0
        local sidesteps_plus1 = 0
        for _, mv in ipairs(m.far_moves) do
            if mv.kind == "prestep" then
                local a0, a1 = player_at[mv.tick], player_at[mv.tick + 1]
                if a0 ~= nil and a1 ~= nil and (a0.x ~= a1.x or a0.z ~= a1.z) then sidesteps_plus1 = sidesteps_plus1 + 1 end
            end
        end
        -- standing in a splat: pool hits by tile and tick (:1028-1047)
        local pool_hit_ticks = {}
        local pool_damage, trail_damage, storm_damage = 0, 0, 0
        for k = 1, #hit_player_rows do
            local row = hit_player_rows[k]
            if row.npc_slot == -1 and row.hitsplat == 28 then
                local stood = player_at[row.tick - 1]
                local in_pool = false
                for p = 1, #pools do
                    if stood ~= nil and row.tick >= pools[p].from and row.tick <= pools[p].to + 1 and stood.x == pools[p].x and stood.z == pools[p].z then
                        in_pool = true
                        break
                    end
                end
                if in_pool then
                    pool_hit_ticks[#pool_hit_ticks + 1] = row.tick
                    pool_damage = pool_damage + row.damage
                else
                    trail_damage = trail_damage + row.damage
                end
            elseif row.npc_slot == ws then
                storm_damage = storm_damage + row.damage
            end
        end
        -- the dodge technique: each far move against the splat aimed at the tile it left (:1333-1354)
        local far_moves = m.far_moves
        local dodge_values = {}
        local dodge_hits = 0
        local dodge_notes = ""
        for mm = 1, #far_moves do
            local mv = far_moves[mm]
            for k = 1, #throws do
                local th = throws[k]
                if th.main ~= nil and th.main.dst_x == mv.fx and th.main.dst_z == mv.fz and th.tick <= mv.tick + 1 and th.tick >= mv.tick - 12 then
                    local land = th.tick + math.ceil(th.main.end_cycle / 30)
                    local at = player_at[land + 1]
                    if at ~= nil then
                        dodge_values[#dodge_values + 1] = math.max(math.abs(at.x - mv.fx), math.abs(at.z - mv.fz))
                        dodge_notes = dodge_notes .. "[projectile 1578 target -1 t" .. th.tick .. " dst " .. mv.fx .. "," .. mv.fz .. ", I moved on t" .. mv.tick .. ", player_tile t" .. (land + 1) .. " " .. at.x .. "," .. at.z .. "]"
                    end
                    for h = 1, #pool_hit_ticks do
                        local stood = player_at[pool_hit_ticks[h] - 1]
                        if pool_hit_ticks[h] > mv.tick + 2 and stood ~= nil and stood.x == mv.fx and stood.z == mv.fz and pool_hit_ticks[h] <= land + 12 then dodge_hits = dodge_hits + 1 end
                    end
                    break
                end
            end
        end
        -- the flick (:1651-1656): the whip on by A+4 and Ranged alone drained
        local flicks = m.flicks
        local drain_ok = 0
        for k = 1, #flicks do
            local f = flicks[k]
            if f.dr > 0 and f.da == 0 and f.ds == 0 and f.equip_tick <= f.a + 4 then drain_ok = drain_ok + 1 end
        end
        -- ANALYSIS END

        -- room.complete_duration, copied unchanged from tob_maiden.lua :1635-1649
        do
            local dur_ok = false
            local dur_text = "no line"
            if pre.wave_text ~= nil and free_tick ~= nil then
                local plain = string.gsub(pre.wave_text, "<[^>]*>", "")
                local minutes, seconds = string.match(plain, "Duration: (%d+):(%d+)")
                if minutes ~= nil then
                    local shown = tonumber(minutes) * 60 + tonumber(seconds)
                    local ticks_run = free_tick - mark_tick
                    dur_text = "line reads " .. minutes .. ":" .. seconds .. " (" .. shown .. " s); the log runs from the room start mark t" .. mark_tick .. " to her npc_free t" .. free_tick .. " = " .. ticks_run .. " ticks = " .. string.format("%.1f", ticks_run * 0.6) .. " s"
                    dur_ok = math.abs(shown - ticks_run * 0.6) <= 8
                end
            end
            t.check("room.complete_duration", dur_ok, dur_text)
        end
        -- THE TECHNIQUE ROWS, copied unchanged from tob_maiden.lua :1706-1710
        local techs = {}
        techs[#techs + 1] = { "tech.sidestep_scan", lead_ok > 0 and lead_bad == 0, "stepped on tick T so the throw at T aimed at the tick T-1 tile: " .. lead_ok .. " throws aimed at the old tile, " .. lead_bad .. " at the new tile; sidesteps issued " .. sidesteps .. ", resolved on the next tick " .. sidesteps_plus1 .. "; rows " .. lead_notes }
        techs[#techs + 1] = { "tech.protect_magic", #protected_hits > 0 and #unprotected_hits > 0 and protected_hits[1] * 2 <= unprotected_hits[1] + 1, "Protect from Magic lit after the first unprotected blackstorm: first protected hit " .. tostring(protected_hits[1]) .. " against the first unprotected " .. tostring(unprotected_hits[1]) .. " (" .. #protected_hits .. " protected, " .. #unprotected_hits .. " unprotected hits before the first transmog); prayer set tick " .. tostring(prayer_on_tick) .. "; rows " .. protect_notes }
        techs[#techs + 1] = { "tech.far_dodge", #dodge_values > 0 and dodge_hits == 0, #far_moves .. " three-tile moves on the tick a throw was aimed at me; " .. #dodge_values .. " resolved; splat hits on the tile I left after the move: " .. dodge_hits .. "; rows " .. dodge_notes }
        techs[#techs + 1] = { "tech.bow_flick", drain_ok > 0, "whip equipped by tick A+4 of her aim and the Ranged level fell on the impact: " .. drain_ok .. " of " .. #flicks .. " flicks" }
        -- the room's end, copied unchanged from tob_maiden.lua :1714-1715
        local cl_r, cl_state = t.raid.state()
        t.check("room.cleared", cl_r == "ok" and string.find(tostring(cl_state.line), "cleared=1", 1, true) ~= nil, tostring(cl_state and cl_state.line))
        for k = 1, #techs do
            t.check(techs[k][1], techs[k][2], techs[k][3])
        end

        -- THE MEASURE (reported against the kept tob_maiden run and the 2026-10-05 survey)
        local hist = { 0, 0, 0, 0 }
        for _, n in pairs(rec.inputs) do
            if n > 0 then hist[math.min(n, 4)] = hist[math.min(n, 4)] + 1 end
        end
        local casts = ""
        for _, c in ipairs(m.casts) do casts = casts .. "[w" .. c.wave .. " t" .. c.tick .. " slot " .. c.slot .. " " .. c.result .. "]" end
        local kinds = { dodge = 0, prestep = 0 }
        for _, mv in ipairs(far_moves) do kinds[mv.kind] = (kinds[mv.kind] or 0) + 1 end
        local flick_text = ""
        for _, f in ipairs(flicks) do flick_text = flick_text .. "[A" .. f.a .. " equip t" .. f.equip_tick .. " dr " .. f.dr .. " da " .. f.da .. " ds " .. f.ds .. "]" end
        t.check("play.flicks", true, #flicks .. " flicks " .. flick_text)
        t.check("play.freeze", #m.casts > 0, #m.casts .. " Ice Barrage casts over " .. tostring(m.waves) .. " waves " .. casts)
        t.check("play.measure", true, string.format("room %s ticks (mark %s, npc_free %s); damage taken %d (blackstorm %d, pools %d, trails/other %d); food %d, drinks %d; swings %d; far moves %d (dodge %d, prestep %d); flicks %d; inputs per tick: 1 on %d, 2 on %d, 3 on %d, 4+ on %d; %s",
            tostring(free_tick and (free_tick - mark_tick)), tostring(mark_tick), tostring(free_tick), storm_damage + pool_damage + trail_damage,
            storm_damage, pool_damage, trail_damage, #rec.eats, #rec.drinks, #rec.swings, #far_moves, kinds.dodge, kinds.prestep, #flicks,
            hist[1], hist[2], hist[3], hist[4], tostring(detail)))
        t.finish(0)
    end,
}
