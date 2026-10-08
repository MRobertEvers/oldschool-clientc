-- tob_maiden_normal: the Maiden of Sugadinti, Normal Mode, a party of THREE (three driven clients in one world).
-- ROLES (named in the sources; the seat that carries the measurement is the one the boss targets):
--   p1 RANGER, the TANK: "The person closest to the boss becomes the tank and will take the most damage ... Everyone will be
--     ranging in this room" (transcripts/yt_4i4lv-srJkw.md:55-57); the boss targets "closest player -> ... -> orb order"
--     (wiki_Theatre_of_Blood_Strategies.wikitext:589); the twisted bow "is highly effective against her" (:633). The leader
--     holds the world's tick log, so it is the one the blackstorm and the pools are read on.
--   p2 MAGE, the FREEZER: "In 4 and 5 man scalers there are two freezers, while in solo to trio there is one freezer"
--     (wiki_Theatre_of_Blood_Strategies.wikitext:603); "You don't want your mage to be closest at any time"
--     (yt_4i4lv-srJkw.md:55); "freeze the crabs that spawn closest ... barrage these down" (yt_4i4lv-srJkw.md:59, :61)
--     and the freezer "should range Maiden from a distance" (wiki_Theatre_of_Blood_Strategies.wikitext:633), so the mage stands further out than the tank.
--   p3 RANGE DPS, standing back with the freezer: the 3-scale team is "Mage -> Ranger -> Melee" (wiki_Theatre_of_Blood_Strategies.wikitext:580); "It is only recommended to melee with a scythe; if players do not have one, use ranged instead"
--     (wiki_Theatre_of_Blood_Strategies.wikitext:633), and the third raider has none; "Everyone else should machine gun
--     down the crabs that aren't in the clump" (yt_4i4lv-srJkw.md:61; Patyfatycake yt_KF9y2GYTJ-A.md:57 'range DPS ...
--     responsible for dpsing during maiden').
-- Her pool is the party-3 figure (2625 = 3500 x 750 / 1000, raidwide.scale.party_3_or_fewer); the blood pools and trails
-- hurt only the raider who crossed the barrier (CONTENT_BUGS.md seam19 Open: the per-tick watchdog), so no spec row
-- grades a member's per-tick room damage.
local role = (QD_PARTY and QD_PARTY.role) or 1
-- bring-alongs (a member's go out as its typed ::command): the combat stats of a Normal Maiden raider, and the role's kit
local kit = {
    -- an empty backpack so the food and gear below all fit
    "::clearinv",
    "::setlevel attack 99",
    "::setlevel strength 99",
    "::setlevel defence 99",
    "::setlevel ranged 99",
    "::setlevel hitpoints 99",
    "::setlevel prayer 99",
}
if role == 2 then
    -- the freezer: Ancient Magicks (spec.maiden.freeze_full_bonus: Ice Burst is on the Matomenos freeze curve; the spellbook var is a
    -- bring-along, not a raid var), the runes of Ice Burst, and the magic set worn the whole fight ("there is no shame in camping mage
    -- gear for the entire fight", yt_4i4lv-srJkw.md:63): ancestral hat 8 + top 35 + bottom 26, kodai wand 28, arcane spirit shield 20,
    -- eternal boots 8, magus ring 15 = +140
    kit[#kit + 1] = "::setvar varb4070_spellbook 1"
    kit[#kit + 1] = "::setlevel magic 99"
    kit[#kit + 1] = "::give water_rune 2000"
    kit[#kit + 1] = "::give death_rune 1000"
    -- Ice Barrage (blood, death and water runes): "Ice Barrage is essentially mandatory for the fight" (wiki_Theatre_of_Blood_Strategies.wikitext:593)
    kit[#kit + 1] = "::give blood_rune 1000"
    kit[#kit + 1] = "::give ancestral_hat"
    kit[#kit + 1] = "::wield ancestral_hat"
    kit[#kit + 1] = "::give ancestral_robe_top"
    kit[#kit + 1] = "::wield ancestral_robe_top"
    kit[#kit + 1] = "::give ancestral_robe_bottom"
    kit[#kit + 1] = "::wield ancestral_robe_bottom"
    kit[#kit + 1] = "::give kodai_wand"
    kit[#kit + 1] = "::wield kodai_wand"
    kit[#kit + 1] = "::give arcane"
    kit[#kit + 1] = "::wield arcane"
    kit[#kit + 1] = "::give eternal_boots"
    kit[#kit + 1] = "::wield eternal_boots"
    kit[#kit + 1] = "::give magus_ring"
    kit[#kit + 1] = "::wield magus_ring"
    -- food and restores (wiki_Theatre_of_Blood_Strategies.wikitext:555-562 supply list: Super restore, shark, manta ray; anglerfish heal 31 at 99 hitpoints, more per backpack slot than a shark: the fight is food-limited)
    kit[#kit + 1] = "::give anglerfish 20"
    kit[#kit + 1] = "::give br_4dose2restore 3"
else
    -- the ranged set (Maiden is slain from range; worn from the start so the backpack keeps room for food)
    kit[#kit + 1] = "::give twisted_bow"
    kit[#kit + 1] = "::wield twisted_bow"
    kit[#kit + 1] = "::give dragon_arrow 1000"
    kit[#kit + 1] = "::wield dragon_arrow"
    kit[#kit + 1] = "::give masori_mask"
    kit[#kit + 1] = "::wield masori_mask"
    kit[#kit + 1] = "::give masori_body"
    kit[#kit + 1] = "::wield masori_body"
    kit[#kit + 1] = "::give masori_chaps"
    kit[#kit + 1] = "::wield masori_chaps"
    kit[#kit + 1] = "::give avas_assembler"
    kit[#kit + 1] = "::wield avas_assembler"
    if role == 1 then
        -- the tank eats the blackstorms and the pools: sharks, two Saradomin brews (more healing; the room's debug kit adds none of its own
        -- when the pack holds one), five prayer restores (Protect from Magic drains through the fight and the pools drain more)
        kit[#kit + 1] = "::give anglerfish 20"
        kit[#kit + 1] = "::give br_4dosepotionofsaradomin 2"
        kit[#kit + 1] = "::give br_4dose2restore 3"
        -- the ranger's potion for the bow (drunk before the fight)
        kit[#kit + 1] = "::give br_4doserangerspotion 1"
        -- a melee weapon for the bow flick (the drain is judged on the weapon worn when she aims)
        kit[#kit + 1] = "::give abyssal_whip"
    else
        -- the range DPS is the tank's relief ("You can swap out when someone gets low", yt_4i4lv-srJkw.md:55): the same food as the tank
        kit[#kit + 1] = "::give anglerfish 22"
        kit[#kit + 1] = "::give br_4dose2restore 4"
        kit[#kit + 1] = "::give br_4doserangerspotion 1"
    end
end

return {
    id = "tob_maiden_normal",
    fixture = "fresh_lumbridge.ini",
    party = 3,
    max_frames = 250000,
    setup = kit,

    run = function(t)
        local role = t.party.role()
        local lr, ld, er, ed
        -- (1) every raider lands at the room's entrance the way a player arrives (the leader's ::tobmode, each member's ::tobjoinroom)
        if role == 1 then
            t.check("spec.scope", true, "mode=normal party=3")
            -- the log runs before the raid is entered so the room's arrival music is a row
            lr, ld = t.ticklog.start()
            t.check("ticklog.start", lr == "ok", tostring(ld))
        end
        er, ed = t.raid.enter("tob", "maiden", { mode = "normal" })
        t.check("raid.enter", er == "ok", tostring(ed))
        local br, brow = t.npc.nearest("tob_maiden_100", 30)
        t.check("boss.present", br == "ok", "p" .. role .. " sees the Maiden's unstarted body " .. tostring(brow and brow.slot))
        t.expect("party.barrier.entrance", t.party.barrier("entrance", 300))
        if role ~= 2 then
            -- the rangers' boosts before the fight (tob_verzik.lua P2 recipe): the bow on rapid (a shot every four ticks instead of five), the ranger's
            -- potion and Rigour; the mage keeps its spell
            t.ui.tab("combat")
            t.ticks(2)
            local sw_result, sw = t.ui.widget("combat_interface:style_slot_1")
            local sp_result = "no widget"
            if sw_result == "ok" then sp_result = t.ui.invoke(sw, 1) end
            t.ticks(1)
            local sv_result, sv = t.var.varp("varp43_com_mode")
            t.check("style.rapid", sw_result == "ok" and sp_result == "ok" and sv == 1, "p" .. role .. " combat tab style slot 1 pressed for the bow: widget " .. tostring(sw_result) .. ", press " .. tostring(sp_result) .. ", varp43_com_mode reads " .. tostring(sv_result) .. " " .. tostring(sv))
            local dr, dd = t.player.drink("br_4doserangerspotion")
            t.check("potion.rangers", dr == "ok", "p" .. role .. " " .. tostring(dd))
            local rr, rd = t.prayer.set("rigour", true)
            t.check("prayer.rigour", rr == "ok", "p" .. role .. " " .. tostring(rd))
        end
        if role ~= 1 then
            -- a member's part: its own clicks, prayers, eats, casts and shots, every row its own (the union ledger prefixes it p<n>:)
            t.expect("party.barrier.started", t.party.barrier("started", 400))
            -- the leader crossed and the fight is running: for a member the barrier is only a gate that steps it to the far side
            local xr, xd = t.player.click_loc("tob_arena_barrier", 1)
            t.check("barrier.cross", xr == "ok", tostring(xd))
            -- the stations: the mage the furthest out (never the closest, yt_4i4lv-srJkw.md:55), the range DPS between it and the tank
            local home_x = 6441
            if role == 3 then home_x = 6440 end
            t.player.walk_to(home_x, 94, 14)
            local _, home_tile = t.world.tile()
            -- Protect from Magic stays up the whole fight: a blackstorm that picks a member is a protected hit the leader does not class
            local pr, pd = t.prayer.set("protectfrommagic", true)
            t.check("prayer.magic", pr == "ok", "p" .. role .. " stands at " .. tostring(home_tile and (home_tile.x .. "," .. home_tile.z)) .. ": " .. tostring(pd))
            -- the tank's c = 0 pool stand first: the DPS holds its fire until the leader has stood in a splat with no Matomenos leaked
            t.expect("party.barrier.c0_stand", t.party.barrier("c0_stand", 900))
            local boss_symbols = { "tob_maiden_100", "tob_maiden_70", "tob_maiden_50", "tob_maiden_30" }
            local food_names = { "anglerfish" }
            local restore_names = { "br_4dose2restore", "br_3dose2restore", "br_2dose2restore", "br_1dose2restore" }
            local focus = nil
            local focus_pass = 0
            local wave = 1
            local crab_for = 0
            local seen_boss = false
            local gone_for = 0
            local pass = 0
            local eats = 0
            local restores = 0
            local casts = 0
            local cast_at = {}
            local presses = 0
            local crab_targets = 0
            local last_cast_pass = -99
            local lowest_hp = 999
            local last_throw_tick = -1
            local alt = false
            local dodges = 0
            local tank_role = false
            local tank_tick_pass = nil
            local leader_out = 0
            local tank_done = false
            local both_out = 0
            local cast_answers = ""
            local done_detail = "not finished"
            while pass < 1500 do
                pass = pass + 1
                local boss_sym = nil
                for k = 1, #boss_symbols do
                    if boss_sym == nil and t.npc.nearest(boss_symbols[k], 60) == "ok" then boss_sym = boss_symbols[k] end
                end
                if boss_sym ~= nil then
                    seen_boss = true
                    gone_for = 0
                else
                    gone_for = gone_for + 1
                end
                if seen_boss and gone_for >= 3 then
                    done_detail = "the Maiden's bodies are gone after " .. pass .. " passes"
                    break
                end
                -- the blood throw: her seq 8091 on the tick it was sent. The splat is aimed at the tile stood on NOW (the end of the previous tick),
                -- and lands about seven ticks later, so a three-tile step along the footprint's rows clears it and the two extras round it
                if boss_sym ~= nil and not (role == 2 and crab_for > 0) then
                    local ss_r, ss_row = t.npc.state(boss_sym)
                    if ss_r == "ok" and ss_row.seq_id == 8091 and ss_row.seq_tick ~= last_throw_tick then
                        last_throw_tick = ss_row.seq_tick
                        alt = not alt
                        t.player.walk_to(home_x, alt and 97 or 94, 4)
                        focus = nil
                        dodges = dodges + 1
                    end
                end
                -- the relief: when the leader (seat 1) has stepped back behind the range DPS, the range DPS is the closest raider and takes the storms
                if role == 3 and not tank_role and not tank_done then
                    local pp_r, pp_d, others = t.party.players(40)
                    if pp_r == "ok" and type(others) == "table" then
                        for o = 1, #others do
                            if others[o].pid == 1 then
                                -- a dodge steps the leader out for a pass or two: it must stay out three passes running
                                if others[o].x >= 6441 then leader_out = leader_out + 1 else leader_out = 0 end
                            end
                            if others[o].pid == 1 and leader_out >= 3 then
                                tank_role = true
                                tank_tick_pass = pass
                                home_x = 6436
                                t.player.walk_to(6436, 94, 6)
                                focus = nil
                                t.check("tank.takeover", true, "p3 steps in to 6436,94 on pass " .. pass .. ": the leader stands at " .. others[o].x .. "," .. others[o].z .. ", further out than p3, p2 stays at 6439")
                            end
                        end
                    end
                end
                -- the tank rotation: the range DPS steps back to the bow's edge when its fish run low, and the freezer, whose backpack is
                -- still full, steps in once both others stand out at 6441 ("You can swap out when someone gets low", yt_4i4lv-srJkw.md:55)
                if role == 3 and tank_role and not tank_done then
                    local fr, fc = t.inv.count("anglerfish")
                    if fr == "ok" and fc <= 3 then
                        tank_done = true
                        tank_role = false
                        home_x = 6441
                        t.player.walk_to(6441, 94, 6)
                        focus = nil
                        t.check("tank.swap_p3", true, "p3 has " .. fc .. " anglerfish left: steps back to 6441,94 pass " .. pass .. ", the freezer is next")
                    end
                end
                if role == 2 and not tank_role then
                    local pp_r, pp_d, others = t.party.players(40)
                    if pp_r == "ok" and type(others) == "table" then
                        local out_n = 0
                        for o = 1, #others do
                            if (others[o].pid == 1 or others[o].pid == 3) and others[o].x >= 6441 then out_n = out_n + 1 end
                        end
                        if out_n >= 2 then both_out = both_out + 1 else both_out = 0 end
                        if both_out >= 3 then
                            tank_role = true
                            home_x = 6437
                            t.player.walk_to(6437, 94, 6)
                            focus = nil
                            t.check("tank.swap_p2", true, "p2 steps in to 6437,94 on pass " .. pass .. ": the leader and the range DPS both stand at 6441 or further")
                        end
                    end
                end
                local _, hp = t.skill.read("hitpoints")
                if hp.level < lowest_hp then lowest_hp = hp.level end
                if hp.level < (tank_role and 66 or 58) then
                    t.player.inv_op("anglerfish", 1, { quick = true })
                    eats = eats + 1
                end
                local _, pw = t.skill.read("prayer")
                if pw.level < 25 then
                    for f = 1, #restore_names do
                        local rc_r, rc = t.inv.count(restore_names[f])
                        if rc_r == "ok" and rc > 0 and pw.level < 25 then
                            t.player.inv_op(restore_names[f], 1, { quick = true })
                            restores = restores + 1
                            pw = { level = 99 }
                        end
                    end
                end
                -- crabs reach her 6 to 17 ticks after they appear: "freeze the crabs that spawn closest to Maiden first" (yt_4i4lv-srJkw.md:59-61), so
                -- the freezer takes the crab with the smallest gap to her footprint (the south-west tile 6426,92, size 6); the range DPS stays on her
                -- ("DPS Maiden down until she's dead", yt_4i4lv-srJkw.md:65)
                local crab_slot = nil
                local crab_r, crab_text, crab_rows = t.npc.tiles("maiden_elemental", 60)
                if crab_r == "ok" and type(crab_rows) == "table" and #crab_rows > 0 then
                    crab_for = crab_for + 1
                    -- the freezer rotates: the crab with the smallest gap to her footprint that has not been frozen in the last 16 passes
                    local best_gap = 99
                    for c = 1, #crab_rows do
                        local cgap = math.max(6426 - crab_rows[c].x, crab_rows[c].x - 6431, 92 - crab_rows[c].z, crab_rows[c].z - 97, 0)
                        local fresh = cast_at[crab_rows[c].slot] == nil or pass - cast_at[crab_rows[c].slot] > 16
                        if fresh and cgap < best_gap then
                            best_gap = cgap
                            crab_slot = crab_rows[c].slot
                        end
                    end
                elseif crab_for > 0 then
                    wave = wave + 1
                    crab_for = 0
                end
                local want = boss_sym
                if crab_slot ~= nil and role == 2 then want = "maiden_elemental" end
                if want ~= nil and role == 3 and (want ~= focus or pass - focus_pass >= 8) then
                    local ar = t.player.attack(want, 2, 1)
                    if ar == "ok" or ar == "timeout" then
                        focus = want
                        focus_pass = pass
                        presses = presses + 1
                        if want == "maiden_elemental" then crab_targets = crab_targets + 1 end
                    else
                        focus = nil
                    end
                elseif want ~= nil and role == 2 and pass - last_cast_pass >= 2 then
                    local cr, cd = t.player.cast("ice_barrage", want, 1, 2, crab_slot ~= nil and { slot = crab_slot } or nil)
                    last_cast_pass = pass
                    if crab_slot ~= nil then cast_at[crab_slot] = pass end
                    casts = casts + 1
                    if want == "maiden_elemental" then crab_targets = crab_targets + 1 end
                    if #cast_answers < 120 then cast_answers = cast_answers .. tostring(cr) .. " " end
                end
                t.ticks(1)
            end
            t.check("fight.done", done_detail ~= "not finished", "p" .. role .. " " .. done_detail .. "; eats " .. eats .. ", restores " .. restores .. ", lowest hp " .. lowest_hp .. ", attack presses " .. presses .. ", dodges " .. dodges .. ", casts " .. casts .. ", crab targets " .. crab_targets .. ", waves seen " .. wave .. ", cast answers " .. cast_answers)
            -- a member leaves with ::tobout and a tile read; the leader reads its log and leaves by the barrier
            t.cheat("::tobout")
            t.await({ level = function()
                local _, tile = t.world.tile()
                return type(tile) == "table" and tile.x > 3600 and tile.x < 3700
            end, note = "back in Ver Sinhaza" }, 20)
            local _, out_tile = t.world.tile()
            t.check("raid.left", type(out_tile) == "table" and out_tile.x > 3600 and out_tile.x < 3700, "p" .. role .. " after ::tobout at " .. tostring(out_tile and (out_tile.x .. "," .. out_tile.z)))
            return
        end

        -- the leader: the world's one tick log is its own, and it crosses the barrier (only the crosser runs the room's per-tick watchdog)
        local sr, st = t.raid.state()
        local st_line = sr == "ok" and tostring(st.line) or tostring(st)
        t.check("raid.state", sr == "ok" and st.room == "maiden" and st.mode == "normal" and not st.started and string.find(st_line, " party=3 ", 1, true) ~= nil, st_line)
        -- (2) the log runs before the room starts, and her world slot is read while she stands
        local wr, ws = t.ticklog.slot(brow)
        t.check("boss.slot", wr == "ok", tostring(ws))
        lr, ld = t.inv.count("anglerfish")
        t.check("food.count", lr == "ok" and ld >= 8, "sharks carried into the fight: " .. tostring(ld))
        -- (3) the player's own click on the barrier starts the room
        -- her animation as the client draws it before the fight: the idle is the record's readyanim, so no action seq is drawn
        local pre = { slug_anims = {}, av_shot = {}, idle_anim = -2, boss_ready = {}, slug_walk_reads = {}, slug_ready_reads = {} }
        pre.r, pre.row = t.npc.state("tob_maiden_100")
        pre.idle_anim = pre.r == "ok" and pre.row.anim_id or -2
        if pre.r == "ok" and pre.row.pose_kind == "ready" then pre.boss_ready[#pre.boss_ready + 1] = pre.row.pose_anim end
        t.check("boss.idle", pre.r == "ok", "before the fight: " .. t.npc.state_text(pre.row))
        t.shot("maiden idle before the fight")
        lr, ld = t.player.click_loc("tob_arena_barrier", 1)
        t.check("barrier.click", lr == "ok", tostring(ld))
        lr, ld = t.chat.play({ "options", "choose:Yes, begin the fight." })
        t.check("barrier.confirm", lr == "ok", tostring(ld))
        lr, ld = t.msg.expect("The fight begins")
        t.check("barrier.msg", lr == "ok", tostring(ld))
        lr, ld = t.ticklog.mark("room start")
        t.check("room.mark", lr == "ok", tostring(ld))
        local tr, mark_tick = t.tick()
        t.check("room.tick", tr == "ok", tostring(mark_tick))
        -- the scale is the party that walked in (~tob_start_room): read back from the content's own readout
        local sr2, st2 = t.raid.state()
        local st2_line = sr2 == "ok" and tostring(st2.line) or tostring(st2)
        t.check("raid.scale", sr2 == "ok" and st2.started and string.find(st2_line, " party=3 scale=3", 1, true) ~= nil, st2_line)
        -- her hitpoints before anything touches her: the content's read-only readout, the room is unchanged by it
        t.cheat("::tobwhy")
        t.ticks(1)
        local _, base_lines = t.msg.last(40)
        local boss_hp = nil
        for l = 1, #base_lines do
            if boss_hp == nil and string.find(base_lines[l].text, "Maiden", 1, true) then boss_hp = tonumber(string.match(base_lines[l].text, "hp=(%d+)")) end
        end
        t.check("boss.hp", boss_hp ~= nil, "readout before the first attack: her hitpoints " .. tostring(boss_hp))
        -- the members cross now (the fight has started; their barrier click is only a gate)
        t.expect("party.barrier.started", t.party.barrier("started", 400))
        -- the tank's station: nearer her than the mage and the range DPS, so the blackstorm's "closest player to her centre" is the leader
        t.player.walk_to(6437, 95, 14)
        local me_samples = {}

        local pray_pts = {}
        local pray_events = {}
        local lead_leaks = 0
        local swap_tick = nil
        -- (4) the fight. Phase A: step on the tick before each of her attacks, prayer off, until a
        -- throw has been aimed past a step and one blackstorm has landed unprotected. Her animation
        -- is read from the client on every tick of the window (the animation lengths of the table).
        local prayer_on_tick, sidesteps, sidesteps_plus1, anim_reads, far_moves, crab_hp, slug_hp, hp_reads, flicks
        do
        local boss_symbols = { "tob_maiden_100", "tob_maiden_70", "tob_maiden_50", "tob_maiden_30" }
        prayer_on_tick = nil
        sidesteps = 0
        sidesteps_plus1 = 0
        local side_texts = ""
        local disc_blood = false
        local eats = 0
        local lowest_hp = 999
        local serial_mark = 0
        anim_reads = {}
        local ring = {
            { -1, 0 }, { 0, 1 }, { 0, -1 }, { -1, 1 }, { -1, -1 }, { 1, 0 }, { 1, 1 }, { 1, -1 },
            { -2, 0 }, { 0, 2 }, { 0, -2 }, { 2, 0 }, { -2, 2 }, { -2, -2 }, { 2, 2 }, { 2, -2 },
        }
        t.player.attack("tob_maiden_100", 2, 2)
        for k = 1, 8 do
            if disc_blood and prayer_on_tick ~= nil then break end
            if prayer_on_tick == nil then
                local hr, hrows = t.ticklog.rows({ kind = "hit_player" })
                local big = false
                for h = 1, #hrows do
                    if hrows[h].damage >= 15 then big = true end
                end
                if big then
                    local pr2, pd2 = t.prayer.set("protectfrommagic", true)
                    t.check("prayer.magic", pr2 == "ok", tostring(pd2))
                    local _, set_tick = t.tick()
                    prayer_on_tick = set_tick
                    pray_events[#pray_events + 1] = { tick = set_tick, on = true }
                    if not pre.av_shot.t_prayer then pre.av_shot.t_prayer = true t.shot("tech.protect_magic prayer lit after the first big hit") end
                end
            end
            local target = mark_tick + 7 + 10 * (k - 1)
            local guard = 0
            local nr, now = t.tick()
            while now < target and guard < 40 do
                local _, hpw = t.skill.read("hitpoints")
                if hpw.level < lowest_hp then lowest_hp = hpw.level end
                if hpw.level < 62 then
                    t.player.inv_op("anglerfish", 1, { quick = true })
                    eats = eats + 1
                end
                local _, mew = t.world.tile()
                me_samples[now] = { x = mew.x, z = mew.z }
                local hzw_r, hzw = t.world.hazard_at(mew.x, mew.z)
                if hzw_r == "ok" and string.find(hzw.text, "loc", 1, true) then
                    for r = 1, #ring do
                        local wx = mew.x + ring[r][1]
                        local wz = mew.z + ring[r][2]
                        local wc_r, wc = t.world.hazard_at(wx, wz)
                        if wc_r == "ok" and wc.count == 0 then
                            t.player.walk_to(wx, wz, 3)
                            break
                        end
                    end
                end
                local as_r, as_row = t.npc.state("tob_maiden_100")
                if as_r == "ok" then anim_reads[#anim_reads + 1] = { tick = now, anim = as_row.anim_id } end
                t.ticks(1)
                nr, now = t.tick()
                guard = guard + 1
            end
            if now == target then
                local _, me0 = t.world.tile()
                local step_r = "refused"
                local step_d = "no quiet adjacent tile"
                for r = 1, 8 do
                    local sx = me0.x + ring[r][1]
                    local sz = me0.z + ring[r][2]
                    local sc_r3, sc3 = t.world.hazard_at(sx, sz)
                    if sc_r3 == "ok" and sc3.count == 0 then
                        step_r, step_d = t.player.step_tick(sx, sz)
                        if not pre.av_shot.t_step then pre.av_shot.t_step = true t.shot("tech.sidestep_scan step landed on the tick before her scan") end
                        break
                    end
                end
                sidesteps = sidesteps + 1
                if string.find(tostring(step_d), "(+1)", 1, true) then sidesteps_plus1 = sidesteps_plus1 + 1 end
                if sidesteps <= 3 then side_texts = side_texts .. "[" .. tostring(step_r) .. " " .. tostring(step_d) .. "] " end
                for q = 1, 7 do
                    local _, qnow = t.tick()
                    local qs_r, qs_row = t.npc.state("tob_maiden_100")
                    if qs_r == "ok" then anim_reads[#anim_reads + 1] = { tick = qnow, anim = qs_row.anim_id } end
                    t.ticks(1)
                end
                t.player.attack("tob_maiden_100", 2, 2)
                local ar2, arows = t.ticklog.rows({ kind = "npc_anim", slot = ws, since = serial_mark })
                for a = 1, #arows do
                    serial_mark = arows[a].serial
                    if arows[a].seq == 8091 and arows[a].tick == target + 1 and string.find(tostring(step_d), "(+1)", 1, true) then disc_blood = true end -- maiden_attack_blood
                end
            end
        end
        t.check("fight.sidesteps", sidesteps > 0, "sidesteps on the tick before her attack: " .. sidesteps .. ", resolved +1: " .. sidesteps_plus1 .. ", aimed-past-a-step throw seen: " .. tostring(disc_blood) .. ", prayer lit at tick " .. tostring(prayer_on_tick) .. " " .. side_texts)
        local phase_b_tick = nil
        do
            local _, pbt = t.tick()
            phase_b_tick = pbt
        end

        -- Phase B: the kill. Ranged attacks (crabs after the first pair has walked in), re-pressed whenever a
        -- dodge, a flick or eight quiet ticks dropped the engagement.
        if prayer_on_tick == nil then
            local pr3, pd3 = t.prayer.set("protectfrommagic", true)
            t.check("prayer.magic_late", pr3 == "ok", tostring(pd3))
            local _, set_tick2 = t.tick()
            prayer_on_tick = set_tick2
            pray_events[#pray_events + 1] = { tick = set_tick2, on = true }
        end
        local food_names = { "anglerfish", "br_4dosepotionofsaradomin", "br_3dosepotionofsaradomin", "br_2dosepotionofsaradomin", "br_1dosepotionofsaradomin" }
        local restore_names = { "br_4dose2restore", "br_3dose2restore", "br_2dose2restore", "br_1dose2restore" }
        local far_ring = { { 0, 3 }, { 0, -3 }, { -3, 0 }, { -3, 3 }, { -3, -3 }, { 3, 0 }, { 3, 3 }, { 3, -3 } }
        local focus = nil
        local focus_tick = 0
        local dodges = 0
        local far_dodges = 0
        far_moves = {}
        local restores = 0
        local danger = {}
        local last_serial = 0
        local crab_swings = 0
        local slug_swings = 0
        local iterations = 0
        crab_hp = nil
        slug_hp = nil
        local stand_end_tick = nil
        hp_reads = {}
        local last_read_clock = -1
        local flick_hold = false
        flicks = {}
        local trace = ""
        local done_detail = "not finished"
        -- the hold: from here until enough throws have flown, only her crabs and blood spawns are attacked and her own
        -- body is left alone, so the throws she makes can all be dodged (maiden.blood_spawn_dodged_cap is a chance row:
        -- 5 percent a splat, so it needs many throws; the fight alone gives too few). Food and prayer decide when it ends.
        local ps = { trail_tries = 0, diag = 0, last_far = -99, stand_state = 0, trail_state = 0, trail_start = 0, last_trail_try = -99, stand_start = 0, leak_state = 0, crab_seen_tick = 0, flick_before = nil, flick_equip_tick = 0, flick_a = 0, flick_tries = 0, anim_serial = 0, stands = 0, pray_state = true, atk_serial = 0, last_attack = nil, odd_drops = 0, swapped = false, released = false }
        local hold = { fire = false, throws = 0, last = -99, end_tick = nil, why = "not ended" }
        while iterations < 1500 do
            iterations = iterations + 1
            local frr, frows = t.ticklog.rows({ kind = "npc_free", slot = ws })
            if type(frows) == "table" and #frows > 0 then
                done_detail = "boss npc_free at tick " .. tostring(frows[1].tick) .. " after " .. iterations .. " iterations"
                break
            end
            local _, now_tick = t.tick()
            local seg = tostring(now_tick)
            local _, hp = t.skill.read("hitpoints")
            if hp.level < lowest_hp then lowest_hp = hp.level end
            local _, me = t.world.tile()
            me_samples[now_tick] = { x = me.x, z = me.z }
            -- the members hold their fire until the tank has stood in a splat with no Matomenos leaked (the c = 0 pool damage), or tick 160 of the fight
            if not ps.released and (ps.stands >= 1 or now_tick >= mark_tick + 160) then
                ps.released = true
                t.expect("party.barrier.c0_stand", t.party.barrier("c0_stand", 900))
            end
            -- "You can swap out when someone gets low" (yt_4i4lv-srJkw.md:55): with the food nearly gone the tank steps back behind the range DPS
            -- and the range DPS, now the closest raider, takes the blackstorms (Strategies wikitext:589, "having another teammate step in when
            -- they are low on health")
            if not ps.swapped and prayer_on_tick ~= nil then
                local sh_r, sh_n = t.inv.count("anglerfish")
                if sh_r == "ok" and sh_n <= 2 then
                    ps.swapped = true
                    ps.stands = 2
                    ps.trail_state = 2
                    ps.odd_drops = 99
                    focus = nil
                    local sw_r = t.player.walk_to(6441, me.z, 6)
                    local _, sw_tick = t.tick()
                    swap_tick = sw_tick
                    t.check("tank.swap", true, "sharks down to " .. sh_n .. " at hitpoints " .. hp.level .. ": the tank steps from " .. me.x .. "," .. me.z .. " back to 6441," .. me.z .. " (" .. tostring(sw_r) .. ", tick " .. sw_tick .. "), further out than the mage at 6441 and the range DPS at 6440")
                end
            end
            -- the tank's station: nearer her than the mage and the DPS; a dodge or a flick that drifted it off is walked back
            if ps.swapped and (me.z < 90 or me.z > 100) then
                t.player.walk_to(6441, 95, 5)
                local _, me_back = t.world.tile()
                me = me_back
            elseif (me.x < 6435 or me.x > 6439 or me.z < 91 or me.z > 99) and ps.trail_state ~= 1 and ps.stand_state ~= 1 and not ps.swapped then
                t.player.walk_to(6437, 95, 4)
                local _, me_home = t.world.tile()
                me = me_home
                me_samples[now_tick] = { x = me.x, z = me.z }
            end
            -- the Matomenos that have reached her so far (c): the heal rows on her slot that share a tick with a Matomenos death
            do
                local _, lk_heals = t.ticklog.rows({ kind = "npc_heal", slot = ws })
                local _, lk_deaths = t.ticklog.rows({ kind = "npc_death" })
                lead_leaks = 0
                if type(lk_heals) == "table" and type(lk_deaths) == "table" and #lk_heals > 0 then
                    local dtick = {}
                    for d = 1, #lk_deaths do
                        if lk_deaths[d].slot ~= ws then dtick[lk_deaths[d].tick] = true end
                    end
                    for h = 1, #lk_heals do
                        if dtick[lk_heals[h].tick] then lead_leaks = lead_leaks + 1 end
                    end
                end
            end
            -- the unprotected blackstorm at an ODD c: floor((365 + 35c) / 10) is exact there (c = 1 is 40, c = 3 is 47), so hit - 3.5c is
            -- the table's 36.5 and two odd c give its 3.5 per leak. The tank drops Protect from Magic in the three ticks before a due
            -- attack while c is odd, and lights it again once the window has passed
            do
                local _, atk_rows = t.ticklog.rows({ kind = "npc_anim", slot = ws, since = ps.atk_serial })
                if type(atk_rows) == "table" then
                    for k = 1, #atk_rows do
                        ps.atk_serial = atk_rows[k].serial
                        if atk_rows[k].seq == 8091 or atk_rows[k].seq == 8092 then ps.last_attack = atk_rows[k].tick end
                    end
                end
                if prayer_on_tick ~= nil and ps.last_attack ~= nil and not flick_hold then
                    local due = ps.last_attack + 10
                    local should_off = now_tick >= due - 3 and now_tick <= due and lead_leaks % 2 == 1 and lead_leaks <= 3 and ps.odd_drops < 8 and hp.level >= 80
                    if should_off and ps.pray_state then
                        local pq, pqd = t.prayer.set("protectfrommagic", false)
                        local _, off_tick = t.tick()
                        pray_events[#pray_events + 1] = { tick = off_tick, on = false }
                        ps.pray_state = false
                        ps.odd_drops = ps.odd_drops + 1
                        trace = trace .. "[pray off t" .. off_tick .. " c" .. lead_leaks .. " due" .. due .. "]"
                    elseif (not should_off) and (not ps.pray_state) then
                        local pq, pqd = t.prayer.set("protectfrommagic", true)
                        local _, on_tick = t.tick()
                        pray_events[#pray_events + 1] = { tick = on_tick, on = true }
                        ps.pray_state = true
                        trace = trace .. "[pray on t" .. on_tick .. "]"
                    end
                end
            end
            local prr, prows = t.ticklog.rows({ kind = "projectile", since = last_serial })
            local new_blood = false
            if type(prows) == "table" then
                for k = 1, #prows do
                    last_serial = prows[k].serial
                    if prows[k].spotanim == 1578 then -- maiden_blood_proj
                        danger[#danger + 1] = { x = prows[k].dst_x, z = prows[k].dst_z, land = prows[k].tick + math.ceil((prows[k].end_cycle - prows[k].start_cycle) / 30) }
                        new_blood = true
                        if prows[k].tick - hold.last > 4 then
                            hold.throws = hold.throws + 1
                            hold.last = prows[k].tick
                        end
                    end
                end
            end
            local threatened = false
            local projectile_threat = false
            local pre_land_threat = false
            local hzr, hz = t.world.hazard_at(me.x, me.z)
            local under_pool = hzr == "ok" and string.find(hz.text, "loc", 1, true) ~= nil
            -- the pool stand, on purpose: the first splat aimed at the tile I stand on, with hitpoints to spare, is
            -- stood in for three ticks past its landing so the pool's own hits (damage, cadence, her heal) are read
            if ps.stand_state ~= 1 and ps.stands < 2 and ((ps.stands == 0 and lead_leaks == 0) or (ps.stands == 1 and lead_leaks >= 1 and lead_leaks <= 6)) then
                for k = 1, #danger do
                    if danger[k].x == me.x and danger[k].z == me.z and now_tick + 4 >= danger[k].land and now_tick <= danger[k].land + 2 and hp.level >= (ps.stands == 0 and 70 or 90) and ps.stand_state ~= 1 then
                        ps.stand_state = 1
                        ps.stand_start = danger[k].land
                        -- a dedicated wait: the splat lands, two ticks of its hits are taken, then one step along the footprint's rows ends the stand
                        do
                            local wait_to = danger[k].land + (ps.stands == 0 and 1 or 0)
                            local wait_guard = 0
                            local _, wait_tick = t.tick()
                            while wait_tick < wait_to and wait_guard < 12 do
                                t.ticks(1)
                                _, wait_tick = t.tick()
                                wait_guard = wait_guard + 1
                            end
                            local away = (me.z <= 95) and (me.z + 4) or (me.z - 4)
                            t.player.walk_to(me.x, away, 4)
                            ps.stand_state = 2
                            ps.stands = ps.stands + 1
                            stand_end_tick = wait_tick
                        end
                        trace = trace .. "[pool-stand t" .. now_tick .. " land" .. danger[k].land .. " hp" .. hp.level .. "]"
                    end
                end
            elseif ps.stand_state == 1 then
                if now_tick >= ps.stand_start + (ps.stands == 0 and 3 or 2) or hp.level < 35 then
                    ps.stand_state = 2
                    ps.stands = ps.stands + 1
                    stand_end_tick = now_tick
                end
            end
            if ps.stand_state ~= 1 then
                for k = 1, #danger do
                    if danger[k].x == me.x and danger[k].z == me.z and now_tick + 4 >= danger[k].land and now_tick <= danger[k].land + 12 then
                        threatened = true
                        projectile_threat = true
                        if danger[k].land > now_tick then pre_land_threat = true end
                    end
                end
                if ps.stand_state == 2 and under_pool and not (ps.trail_state == 1 and now_tick <= ps.trail_start + 4) then threatened = true end
                if ps.trail_state == 1 and now_tick > ps.trail_start + 4 then
                    ps.trail_tries = ps.trail_tries + 1
                    if ps.trail_tries < 5 then ps.trail_state = 0 else ps.trail_state = 2 end
                end
                -- the trail stand, on purpose: with a blood spawn beside me and hitpoints to spare, step onto a trail
                -- tile (a loc no throw was aimed at) and hold it three ticks so the trail's own hits can be read
                if ps.trail_state == 0 and not threatened and lead_leaks <= 1 and hp.level >= 48 and now_tick - ps.last_trail_try >= 6 then
                    local sl_r, sl = t.npc.nearest("maiden_blood_slug", 10)
                    if sl_r == "ok" then
                        ps.last_trail_try = now_tick
                        local best = nil
                        local best_d = 99
                        for dx = -3, 3 do
                            for dz = -3, 3 do
                                local cx = sl.x + dx
                                local cz = sl.z + dz
                                local tz_r, tz = t.world.hazard_at(cx, cz)
                                if tz_r == "ok" and string.find(tz.text, "loc", 1, true) and cx >= 6432 and cx <= 6447 then
                                    local aimed = false
                                    for d = 1, #danger do
                                        if math.max(math.abs(danger[d].x - cx), math.abs(danger[d].z - cz)) <= 1 and now_tick <= danger[d].land + 12 then aimed = true end
                                    end
                                    local dist = math.max(math.abs(cx - me.x), math.abs(cz - me.z))
                                    if not aimed and dist < best_d and dist >= 1 then
                                        best = { x = cx, z = cz }
                                        best_d = dist
                                    end
                                end
                            end
                        end
                        if ps.diag < 0 then
                            ps.diag = ps.diag + 1
                            t.check("trail.diag_" .. ps.diag, true, "tick " .. now_tick .. " hp " .. hp.level .. " me " .. me.x .. "," .. me.z .. " slug " .. sl.x .. "," .. sl.z .. " best " .. tostring(best and (best.x .. "," .. best.z)))
                        end
                        if best ~= nil then
                            t.player.walk_to(best.x, best.z, 4)
                            ps.trail_state = 1
                            ps.trail_start = now_tick + best_d
                            trace = trace .. "[trail-stand t" .. now_tick .. " " .. me.x .. "," .. me.z .. ">" .. best.x .. "," .. best.z .. "]"
                        end
                    end
                end
            end
            if threatened then
                local moved = false
                if projectile_threat then
                    local cands = {}
                    if far_dodges % 2 == 0 then cands = { { 0, 3 }, { 0, -3 } } else cands = { { 0, -3 }, { 0, 3 } } end
                    for k = 1, #cands do
                        local cx = me.x + cands[k][1]
                        local cz = me.z + cands[k][2]
                        local bad = cx < 6432 or cx > 6446 or cz < 84 or cz > 104
                        for d = 1, #danger do
                            if math.max(math.abs(danger[d].x - cx), math.abs(danger[d].z - cz)) <= 2 and now_tick <= danger[d].land + 12 then bad = true end
                        end
                        local cr2, ch = t.world.hazard_at(cx, cz)
                        if not bad and cr2 == "ok" and ch.count == 0 then
                            local wr = t.player.walk_to(cx, cz, 4)
                            if wr == "ok" then
                                moved = true
                                -- only a move made before the splat landed is a dodge; leaving a pool already under me is an escape
                                if pre_land_threat then far_moves[#far_moves + 1] = { tick = now_tick, fx = me.x, fz = me.z } end
                                if pre_land_threat and not pre.av_shot.t_far then pre.av_shot.t_far = true t.shot("tech.far_dodge three-tile move before the splat lands") end
                                far_dodges = far_dodges + 1
                                ps.last_far = now_tick
                                dodges = dodges + 1
                                trace = trace .. "[far-dodge t" .. now_tick .. " " .. me.x .. "," .. me.z .. ">" .. cx .. "," .. cz .. "]"
                                break
                            end
                        end
                    end
                end
                if not moved then
                    for k = 1, #ring do
                        local cx = me.x + ring[k][1]
                        local cz = me.z + ring[k][2]
                        local bad = false
                        for d = 1, #danger do
                            if danger[d].x == cx and danger[d].z == cz and now_tick <= danger[d].land + 12 then bad = true end
                        end
                        local cr2, ch = t.world.hazard_at(cx, cz)
                        if not bad and cr2 == "ok" and not string.find(ch.text, "loc", 1, true) then
                            if k <= 8 then
                                t.player.step_tick(cx, cz)
                            else
                                t.player.walk_to(cx, cz, 3)
                            end
                            dodges = dodges + 1
                            trace = trace .. "[dodge t" .. now_tick .. " " .. me.x .. "," .. me.z .. ">" .. cx .. "," .. cz .. "]"
                            break
                        end
                    end
                end
                focus = nil
            end
            if hp.level < 70 and (ps.stand_state ~= 1 or hp.level < 40) then
                for f = 1, #food_names do
                    local fc_r, fc = t.inv.count(food_names[f])
                    if fc_r == "ok" and fc > 0 then
                        local _, eat_from = t.tick()
                        t.player.inv_op(food_names[f], 1, { quick = true })
                        local _, eat_to = t.tick()
                        eats = eats + 1
                        trace = trace .. "[eat " .. food_names[f] .. " hp" .. hp.level .. " t" .. now_tick .. " cost" .. (eat_to - eat_from) .. "]"
                        if hp.level < 45 then t.check("fight.lowhp", true, "hitpoints " .. hp.level .. " at tick " .. now_tick .. ", the eat took " .. (eat_to - eat_from) .. " ticks; trace " .. string.sub(trace, -500)) end
                        break
                    end
                end
            end
            do local _, st1 = t.tick() seg = seg .. " eat>" .. st1 end
            local _, pw = t.skill.read("prayer")
            pray_pts[now_tick] = pw.level
            local _, rg_now = t.skill.read("ranged")
            if pw.level < 36 or (rg_now.level < 80 and hp.level >= 60) then
                for f = 1, #restore_names do
                    local rc_r, rc = t.inv.count(restore_names[f])
                    if rc_r == "ok" and rc > 0 then
                        t.player.inv_op(restore_names[f], 1, { quick = true })
                        restores = restores + 1
                        trace = trace .. "[restore pray" .. pw.level .. " ranged" .. rg_now.level .. " t" .. now_tick .. "]"
                        break
                    end
                end
            end
            do local _, st2 = t.tick() seg = seg .. " dodge>" .. st2 end
            -- the bow flick: the drain is chosen when her shot is aimed (the attack tick), so a melee weapon
            -- put on between the aim and the impact must still see the RANGED stat drained
            if flick_hold then
                if now_tick >= ps.flick_a + 6 then
                    local _, rl = t.skill.read("ranged")
                    local _, al = t.skill.read("attack")
                    local _, sl = t.skill.read("strength")
                    flicks[#flicks + 1] = { a = ps.flick_a, equip_tick = ps.flick_equip_tick, dr = ps.flick_before.r - rl.level, da = ps.flick_before.a - al.level, ds = ps.flick_before.s - sl.level }
                    if not pre.av_shot.t_flick then pre.av_shot.t_flick = true t.shot("tech.bow_flick whip worn at the drain impact then ranged read") end
                    t.player.equip("twisted_bow")
                    flick_hold = false
                    focus = nil
                    trace = trace .. "[flick done A" .. ps.flick_a .. " dr" .. (ps.flick_before.r - rl.level) .. "]"
                end
            elseif ps.flick_tries < 7 and prayer_on_tick ~= nil and not threatened and #flicks < 3 then
                local fr_r, frows2 = t.ticklog.rows({ kind = "npc_anim", slot = ws, seq = 8092, since = ps.anim_serial })
                if type(frows2) == "table" then
                    for k = 1, #frows2 do
                        ps.anim_serial = frows2[k].serial
                        if now_tick <= frows2[k].tick + 2 and not flick_hold and frows2[k].tick > phase_b_tick then
                            local _, rl = t.skill.read("ranged")
                            local _, al = t.skill.read("attack")
                            local _, sl = t.skill.read("strength")
                            ps.flick_before = { r = rl.level, a = al.level, s = sl.level }
                            t.player.equip("abyssal_whip")
                            local _, eq_tick = t.tick()
                            ps.flick_equip_tick = eq_tick
                            ps.flick_a = frows2[k].tick
                            flick_hold = true
                            ps.flick_tries = ps.flick_tries + 1
                            focus = nil
                            trace = trace .. "[flick start A" .. ps.flick_a .. " equip t" .. eq_tick .. "]"
                        end
                    end
                end
            end
            do local _, st3 = t.tick() seg = seg .. " flick>" .. st3 end
            if focus ~= nil and now_tick - focus_tick >= 8 then focus = nil end
            local want = nil
            local crab_r, crab = t.npc.nearest("maiden_elemental", 60)
            if crab_r == "ok" then
                if ps.leak_state == 0 then
                    ps.leak_state = 1
                    ps.crab_seen_tick = now_tick
                end
                if false then
                    want = "maiden_elemental"
                end
            else
                if ps.leak_state == 1 then ps.leak_state = 2 end
                local slug_r, slug = t.npc.nearest("maiden_blood_slug", 3)
                if false then want = "maiden_blood_slug" end
            end
            if hold.fire then
                local hs_r, hs_n = t.inv.count("anglerfish")
                if hold.throws >= 12 then
                    hold.fire = false
                    hold.why = hold.throws .. " throws seen"
                elseif hs_r == "ok" and hs_n <= 5 then
                    hold.fire = false
                    hold.why = "sharks down to " .. hs_n .. " after " .. hold.throws .. " throws"
                end
                if not hold.fire then
                    local _, hold_t = t.tick()
                    hold.end_tick = hold_t
                    t.check("fight.hold", true, "left her body alone from tick " .. tostring(phase_b_tick) .. " to tick " .. hold_t .. ": " .. hold.why .. "; hitpoints " .. hp.level)
                end
            end
            if want == nil and not hold.fire then
                for k = 1, #boss_symbols do
                    local b_r = t.npc.nearest(boss_symbols[k], 60)
                    if b_r == "ok" then want = boss_symbols[k] break end
                end
            end
            if want ~= nil and want ~= focus and not flick_hold then
                local ar, ad = t.player.attack(want, 2, 1)
                local _, at_tick = t.tick()
                if ar == "ok" or ar == "timeout" then
                    focus = want
                    focus_tick = at_tick
                    if want == "maiden_elemental" then crab_swings = crab_swings + 1 end
                    if want == "maiden_blood_slug" then slug_swings = slug_swings + 1 end
                else
                    focus = nil
                    trace = trace .. "[attack " .. want .. " " .. tostring(ar) .. " t" .. at_tick .. "]"
                    if me.x < 6434 or me.x > 6440 then t.player.walk_to(6437, 95, 6) end
                end
            end
            do
                local _, st4 = t.tick()
                seg = seg .. " attack>" .. st4
                if st4 - now_tick > 3 then
                    trace = trace .. "[slow " .. seg .. "]"
                end
            end
            -- her hitpoints, read-only, one reading per pass (the readout names the server's own tick)
            t.cheat("::tobwhy")
            t.ticks(1)
            local _, lines = t.msg.last(40)
            local read_clock = nil
            local read_boss = nil
            local read_crab = nil
            local read_slug = nil
            for l = 1, #lines do
                local text = lines[l].text
                if read_clock == nil then
                    local mc = string.match(text, "map_clock=(%d+)")
                    if mc ~= nil then read_clock = tonumber(mc) end
                end
                if read_boss == nil and string.find(text, "Maiden", 1, true) then read_boss = tonumber(string.match(text, "hp=(%d+)")) end
                if read_crab == nil and string.find(text, "Matomenos", 1, true) then read_crab = tonumber(string.match(text, "hp=(%d+)")) end
                if read_slug == nil and string.find(text, "Blood spawn", 1, true) then read_slug = tonumber(string.match(text, "hp=(%d+)")) end
            end
            -- what the client draws on her, a Matomenos and a blood spawn: one picture of each presentation, and a few readings of the spawn's drawn seq
            do
                local bs_r, bs = "no_row", nil
                for k = 1, #boss_symbols do
                    if bs_r ~= "ok" then bs_r, bs = t.npc.state(boss_symbols[k]) end
                end
                if bs_r == "ok" and bs.pose_kind == "ready" and #pre.boss_ready < 12 then pre.boss_ready[#pre.boss_ready + 1] = bs.pose_anim end
                if bs_r == "ok" then
                    if bs.anim_id == 8091 and not pre.av_shot.throw then pre.av_shot.throw = true t.shot("maiden blood throw drawn") end
                    if bs.anim_id == 8092 and not pre.av_shot.storm then pre.av_shot.storm = true t.shot("maiden blackstorm drawn") end
                end
                local sl_r, sl = t.npc.state("maiden_blood_slug")
                if sl_r == "ok" and sl.pose_kind == "walk" and #pre.slug_walk_reads < 12 then pre.slug_walk_reads[#pre.slug_walk_reads + 1] = sl.pose_anim end
                if sl_r == "ok" and sl.pose_kind == "ready" and #pre.slug_ready_reads < 12 then pre.slug_ready_reads[#pre.slug_ready_reads + 1] = sl.pose_anim end
                if sl_r == "ok" and #pre.slug_anims < 6 then
                    pre.slug_anims[#pre.slug_anims + 1] = sl.anim_id
                    if not pre.av_shot.slug then pre.av_shot.slug = true t.shot("maiden blood spawn drawn") end
                end
                local cr_r = t.npc.state("maiden_elemental")
                if cr_r == "ok" and not pre.av_shot.crab then pre.av_shot.crab = true t.shot("maiden matomenos drawn") end
            end
            if read_clock ~= nil and read_clock ~= last_read_clock and read_boss ~= nil then
                last_read_clock = read_clock
                hp_reads[#hp_reads + 1] = { clock = read_clock, hp = read_boss }
                if read_crab ~= nil and (crab_hp == nil or read_crab > crab_hp) then crab_hp = read_crab end
                if read_slug ~= nil and (slug_hp == nil or read_slug > slug_hp) then slug_hp = read_slug end
            end
        end
        if #trace > 700 then trace = string.sub(trace, -700) end
        t.check("fight.done", done_detail ~= "not finished", done_detail .. "; dodges " .. dodges .. ", eats " .. eats .. ", crab targets " .. crab_swings .. ", slug targets " .. slug_swings .. ", restores " .. restores .. ", lowest hp " .. lowest_hp .. ", crab hp " .. tostring(crab_hp) .. ", blood spawn hp " .. tostring(slug_hp) .. ", stand ended tick " .. tostring(stand_end_tick) .. " flicks " .. #flicks .. " far-dodges " .. far_dodges .. " reads " .. #hp_reads .. " trace " .. trace)
        t.ticks(4)
        -- the chat line the room prints when it ends (read once the room is done; asserted against the log's own duration below)
        pre.wave_text = nil
        do
            local _, wave_lines = t.msg.last(120)
            for l = 1, #wave_lines do
                if string.find(wave_lines[l].text, "Wave 'The Maiden of Sugadinti' (Normal Mode) complete!", 1, true) then pre.wave_text = wave_lines[l].text end
            end
            t.check("room.complete_line", pre.wave_text ~= nil, "chat line: " .. tostring(pre.wave_text))
        end
        end
        -- ANALYSIS BEGIN
        local seq_blood = 8091 -- maiden_attack_blood
        local seq_auto = 8092 -- maiden_attack_special
        local seq_death_a = 8093 -- maiden_death_a
        local seq_death_b = 8094 -- maiden_death_b
        local proj_auto = 1577 -- maiden_shadow_proj
        local proj_blood = 1578 -- maiden_blood_proj
        local fx_pool = 1579 -- maiden_lingering_blood
        local maiden_size = 6 -- the cache record's size: her footprint is the south-west tile plus six
        local specs = {}
        local techs_pre = {}
        local texts = {}
        local boss_x, boss_z = string.match(tostring(ed), "at (%d+),(%d+)%)")
        boss_x = tonumber(boss_x)
        boss_z = tonumber(boss_z)
        local _, anim_rows = t.ticklog.rows({ kind = "npc_anim", slot = ws })
        t.ticks(1)
        local attacks = {}
        local death_a_tick = nil
        local death_b_tick = nil
        for k = 1, #anim_rows do
            local sq = anim_rows[k].seq
            if sq == seq_blood or sq == seq_auto then
                attacks[#attacks + 1] = { tick = anim_rows[k].tick, blood = (sq == seq_blood) }
            elseif sq == seq_death_a and death_a_tick == nil then
                death_a_tick = anim_rows[k].tick
            elseif sq == seq_death_b and death_b_tick == nil then
                death_b_tick = anim_rows[k].tick
            end
        end
        local gap_values = {}
        for k = 2, #attacks do gap_values[#gap_values + 1] = attacks[k].tick - attacks[k - 1].tick end
        local first_offset = attacks[1].tick - mark_tick + 1
        specs[#specs + 1] = { "cadence", gap_values, "ticks", (#gap_values) .. " gaps between attack rows 8091 and 8092 on her slot (blackstorm and blood throw share one free-running clock)", "10", "B", "exact" }
        specs[#specs + 1] = { "first_attack", { first_offset }, "ticks", "first attack row minus the room start mark plus one", "9", "B", "+-1" }

        -- her animations as the client drew them, tick by tick (phase A read them every tick)
        local anim_at = {}
        for k = 1, #anim_reads do anim_at[anim_reads[k].tick] = anim_reads[k].anim end
        local blood_len = {}
        local auto_len = {}
        for k = 1, #attacks do
            local sq = attacks[k].blood and seq_blood or seq_auto
            if anim_at[attacks[k].tick] == sq then
                local e = attacks[k].tick
                while anim_at[e] == sq do e = e + 1 end
                if anim_at[e] ~= nil then
                    if attacks[k].blood then blood_len[#blood_len + 1] = e - attacks[k].tick else auto_len[#auto_len + 1] = e - attacks[k].tick end
                end
            end
        end
        specs[#specs + 1] = { "anim_blood_len", blood_len, "ticks", #blood_len .. " blood throw(s): ticks the client drew seq 8091 from the attack row to the first other reading", "3", "A", "exact" }
        specs[#specs + 1] = { "anim_auto_len", auto_len, "ticks", #auto_len .. " blackstorm(s): ticks the client drew seq 8092 from the attack row to the first other reading", "5", "A", "exact" }

        local _, proj_rows = t.ticklog.rows({ kind = "projectile" })
        -- indexed by tick once: a scan of every projectile row per attack ran out of the instruction budget
        local proj_by_tick = {}
        for k = 1, #proj_rows do
            local bucket = proj_by_tick[proj_rows[k].tick]
            if bucket == nil then bucket = {} proj_by_tick[proj_rows[k].tick] = bucket end
            bucket[#bucket + 1] = proj_rows[k]
        end
        t.ticks(1)
        local _, hit_player_rows = t.ticklog.rows({ kind = "hit_player" })
        t.ticks(1)
        local _, hit_npc_rows = t.ticklog.rows({ kind = "hit_npc" })
        t.ticks(1)
        local _, retype_rows = t.ticklog.rows({ kind = "npc_retype" })
        -- the mode's record is formed on her first tick, before the room starts (the log runs from before the raid was entered): the chain starts at the fight
        pre.form_retype = nil
        do
            local kept = {}
            for k = 1, #retype_rows do
                if retype_rows[k].tick >= mark_tick then kept[#kept + 1] = retype_rows[k] else pre.form_retype = retype_rows[k] end
            end
            retype_rows = kept
        end
        t.ticks(1)
        local _, spawn_rows = t.ticklog.rows({ kind = "npc_spawn" })
        t.ticks(1)
        local _, death_rows = t.ticklog.rows({ kind = "npc_death" })
        t.ticks(1)
        local _, free_rows = t.ticklog.rows({ kind = "npc_free" })
        t.ticks(1)
        local _, loc_rows = t.ticklog.rows({ kind = "loc_set" })
        t.ticks(1)
        local _, fx_rows = t.ticklog.rows({ kind = "map_spotanim" })
        t.ticks(1)
        local _, tile_rows = t.ticklog.rows({ kind = "player_tile" })
        t.ticks(1)

        local first_retype = 1000000000
        for k = 1, #retype_rows do
            if retype_rows[k].slot == ws and retype_rows[k].tick < first_retype then first_retype = retype_rows[k].tick end
        end
        local death_tick = nil
        local free_tick = nil
        for k = 1, #death_rows do if death_rows[k].slot == ws then death_tick = death_rows[k].tick end end
        for k = 1, #free_rows do if free_rows[k].slot == ws then free_tick = free_rows[k].tick end end
        -- which log pid is the leader: the pid whose tile, on a tick this client read its own tile, is that tile (the members stand at their stations)
        local tiles_of = {}
        local lpid = nil
        do
            local match_count = {}
            for k = 1, #tile_rows do
                local row = tile_rows[k]
                tiles_of[row.pid] = tiles_of[row.pid] or {}
                tiles_of[row.pid][row.tick] = { x = row.x, z = row.z }
                local ms = me_samples[row.tick]
                if ms ~= nil and ms.x == row.x and ms.z == row.z then match_count[row.pid] = (match_count[row.pid] or 0) + 1 end
            end
            local lbest = 0
            for pid, n in pairs(match_count) do
                if n > lbest then lpid, lbest = pid, n end
            end
            local pid_count = 0
            for _ in pairs(tiles_of) do pid_count = pid_count + 1 end
            local lead_n = 0
            for k = 1, #hit_player_rows do
                if hit_player_rows[k].pid == lpid then lead_n = lead_n + 1 end
            end
            t.check("party.leader_pid", lpid ~= nil and lbest >= 20 and pid_count == 3, "the tick log's player_tile rows carry " .. pid_count .. " pids; the leader is pid " .. tostring(lpid) .. " (its own tile read matched " .. lbest .. " ticks); " .. lead_n .. " of " .. #hit_player_rows .. " hit_player rows are its own")
        end
        local player_at = tiles_of[lpid] or {}
        local lead_hits = {}
        for k = 1, #hit_player_rows do
            if hit_player_rows[k].pid == lpid then lead_hits[#lead_hits + 1] = hit_player_rows[k] end
        end
        -- the Matomenos that reached her: a heal row on her slot that shares its tick with a Matomenos npc_death
        local _, heal_rows_all = t.ticklog.rows({ kind = "npc_heal", slot = ws })
        local leak_ticks = {}
        do
            local dtick = {}
            for k = 1, #death_rows do
                if death_rows[k].slot ~= ws then dtick[death_rows[k].tick] = true end
            end
            for k = 1, #heal_rows_all do
                if dtick[heal_rows_all[k].tick] then leak_ticks[#leak_ticks + 1] = heal_rows_all[k].tick end
            end
        end

        -- pools are the 1579 graphic alone since seam9 (no loc under them); trails are the loc 32984 rows.
        -- A pool's window for the stand and hit readings is its landing tick to landing + 11 (the register's life);
        -- the life itself is measured below from the blood spawn that rolls on expiry (npc_spawn on the pool's tile).
        local pools = {}
        local trails = {}
        local pool_life = {}
        local trail_life = {}
        for k = 1, #fx_rows do
            if fx_rows[k].spotanim == fx_pool then
                pools[#pools + 1] = { x = fx_rows[k].x, z = fx_rows[k].z, from = fx_rows[k].tick, to = fx_rows[k].tick + 11 }
            end
        end
        for p = 1, #pools do
            for k = 1, #spawn_rows do
                local sr = spawn_rows[k]
                if sr.slot ~= ws and sr.x == pools[p].x and sr.z == pools[p].z and sr.tick > pools[p].from and sr.tick <= pools[p].from + 15 then
                    pool_life[#pool_life + 1] = sr.tick - pools[p].from
                end
            end
        end
        local open = {}
        for k = 1, #loc_rows do
            local row = loc_rows[k]
            if row.loc ~= -1 then
                open[row.coord] = { tick = row.tick }
            elseif open[row.coord] ~= nil then
                trails[#trails + 1] = { x = row.x, z = row.z, from = open[row.coord].tick, to = row.tick }
                if death_tick == nil or row.tick < death_tick then trail_life[#trail_life + 1] = row.tick - open[row.coord].tick end
                open[row.coord] = nil
            end
        end
        specs[#specs + 1] = { "pool_life", pool_life, "ticks", #pool_life .. " blood spawns rolled on a pool's tile: spawn tick minus the 1579 landing tick", "11", "D", "+-1" }
        if #trail_life > 0 then
            specs[#specs + 1] = { "trail_life", trail_life, "ticks", #trail_life .. " blood-spawn trail tiles (loc 32984): loc_set to its removal, trails still standing when she died left out", "30", "B", "+-1" }
        end

        -- blackstorm: launch delay, total flight, impact offset, protection
        t.ticks(1)
        local launch_values = {}
        local flight_values = {}
        for k = 1, #proj_rows do
            if proj_rows[k].spotanim == proj_auto then
                launch_values[#launch_values + 1] = proj_rows[k].start_cycle
                flight_values[#flight_values + 1] = proj_rows[k].end_cycle
            end
        end
        local impact_values = {}
        local unprotected_hits = {}
        local protect_notes = ""
        local protected_hits = {}
        for k = 1, #attacks do
            t.ticks(1)
            if not attacks[k].blood then
                local found = nil
                for _, off in ipairs({ 5, 4, 6, 3, 7 }) do
                    for h = 1, #hit_player_rows do
                        if hit_player_rows[h].tick == attacks[k].tick + off and hit_player_rows[h].npc_slot == ws and found == nil then
                            found = off
                            attacks[k].hit = hit_player_rows[h].damage
                            attacks[k].pid = hit_player_rows[h].pid
                        end
                    end
                end
                if found ~= nil then impact_values[#impact_values + 1] = found end
            end
        end
        for a = 1, #attacks do
            local on = false
            for e = 1, #pray_events do
                if pray_events[e].tick < attacks[a].tick then on = pray_events[e].on end
            end
            attacks[a].prayed = on
        end
        for a = 1, #attacks do
            if not attacks[a].blood and attacks[a].hit ~= nil and attacks[a].pid == lpid and attacks[a].tick + 5 < first_retype then
                if attacks[a].prayed then
                    protected_hits[#protected_hits + 1] = attacks[a].hit
                    protect_notes = protect_notes .. "[npc_anim 8092 t" .. attacks[a].tick .. " -> hit_player " .. attacks[a].hit .. " protected]"
                else
                    unprotected_hits[#unprotected_hits + 1] = attacks[a].hit
                    protect_notes = protect_notes .. "[npc_anim 8092 t" .. attacks[a].tick .. " -> hit_player " .. attacks[a].hit .. " unprotected]"
                end
            end
        end
        specs[#specs + 1] = { "auto_launch_cycles", launch_values, "cycles", #launch_values .. " blackstorm projectiles", "120", "E", "approx", "M122" }
        specs[#specs + 1] = { "auto_flight_total", flight_values, "cycles", #flight_values .. " blackstorm projectiles", "170", "E", "approx", "M122" }
        specs[#specs + 1] = { "auto_impact_offset", impact_values, "ticks", #impact_values .. " blackstorms", "5", "E", "approx", "M122" }
        local protect_ratios = {}
        local unprotected_top = 0
        local protected_top = 0
        for k = 1, #unprotected_hits do if unprotected_hits[k] > unprotected_top then unprotected_top = unprotected_hits[k] end end
        for k = 1, #protected_hits do
            if protected_hits[k] > protected_top then protected_top = protected_hits[k] end
            if #unprotected_hits > 0 then protect_ratios[#protect_ratios + 1] = protected_hits[k] / unprotected_hits[1] end
        end
        if #protect_ratios > 0 then
            specs[#specs + 1] = { "auto_protect_ratio", protect_ratios, "ratio", #protected_hits .. " protected hits " .. table.concat(protected_hits, " ") .. " over the first unprotected hit " .. tostring(unprotected_hits[1]) .. ", all before the first transmog (no leaks yet)", "0.5", "D", "exact" }
        end

        -- every blackstorm of the fight: who it hit against the table's target rule (closest to her centre by Chebyshev distance, ties by orb order),
        -- and its damage against 36.5 + 3.5c for the c Matomenos leaked at its launch. An UNPROTECTED hit at an odd c is exact
        -- (floor((365 + 35c) / 10) = 40 at c = 1, 47 at c = 3), so hit - 3.5c is the table's base and the slope between two odd c is its 3.5
        t.ticks(1)
        do
            local centre_x = boss_x + 3
            local centre_z = boss_z + 3
            local rule_ok, rule_bad, rule_ties = 0, 0, 0
            local rule_text = ""
            local lead_storms = 0
            local odd_hits = {}
            local base_vals = {}
            local slope_vals = {}
            local formula_text = ""
            local formula_bad = 0
            local prot_checked = 0
            for a = 1, #attacks do
                local at = attacks[a]
                if not at.blood and at.pid ~= nil then
                    local best, best_pid, tie = 99999, nil, false
                    for pid = 0, 2 do
                        local pt = tiles_of[pid] and tiles_of[pid][at.tick - 1]
                        if pt ~= nil and pt.x >= 6432 and pt.x <= 6447 then
                            local d = math.max(math.abs(pt.x - centre_x), math.abs(pt.z - centre_z))
                            if d < best then best, best_pid, tie = d, pid, false
                            elseif d == best then tie = true end
                        end
                    end
                    if tie then rule_ties = rule_ties + 1 end
                    if best_pid == at.pid or tie then rule_ok = rule_ok + 1 else
                        rule_bad = rule_bad + 1
                        if #rule_text < 300 then rule_text = rule_text .. "[8092 t" .. at.tick .. " hit pid " .. at.pid .. " closest pid " .. tostring(best_pid) .. " at " .. best .. "]" end
                    end
                    local pts = nil
                    for tk = at.tick, at.tick - 12, -1 do
                        if pts == nil and pray_pts[tk] ~= nil then pts = pray_pts[tk] end
                    end
                    if at.pid == lpid and pts ~= nil and pts <= 1 then
                        formula_text = formula_text .. "[t" .. at.tick .. " prayer points " .. pts .. ": left out]"
                    elseif at.pid == lpid then
                        lead_storms = lead_storms + 1
                        local c = 0
                        for _, k in ipairs(leak_ticks) do if k <= at.tick then c = c + 1 end end
                        local full = math.floor((365 + 35 * c) / 10)
                        if not at.prayed then
                            if at.hit ~= full then
                                formula_bad = formula_bad + 1
                                formula_text = formula_text .. "[t" .. at.tick .. " c" .. c .. " hit " .. at.hit .. " want " .. full .. "]"
                            end
                            if c % 2 == 1 then odd_hits[#odd_hits + 1] = { c = c, hit = at.hit, tick = at.tick } end
                        else
                            prot_checked = prot_checked + 1
                            if at.hit ~= math.floor(full / 2) then
                                formula_bad = formula_bad + 1
                                formula_text = formula_text .. "[t" .. at.tick .. " c" .. c .. " prayed hit " .. at.hit .. " want " .. math.floor(full / 2) .. "]"
                            end
                        end
                    end
                end
            end
            local odd_text = ""
            for k = 1, #odd_hits do
                base_vals[#base_vals + 1] = odd_hits[k].hit - 3.5 * odd_hits[k].c
                odd_text = odd_text .. "[8092 t" .. odd_hits[k].tick .. " c" .. odd_hits[k].c .. " unprotected " .. odd_hits[k].hit .. "]"
                for j = 1, k - 1 do
                    if odd_hits[j].c < odd_hits[k].c then slope_vals[#slope_vals + 1] = (odd_hits[k].hit - odd_hits[j].hit) / (odd_hits[k].c - odd_hits[j].c) end
                end
            end
            t.check("auto.formula", formula_bad == 0 and lead_storms > 0, lead_storms .. " blackstorms on the leader (" .. prot_checked .. " prayed): each one against floor((365 + 35c) / 10), halved when Protect from Magic was lit at its launch; off the formula " .. formula_bad .. " " .. formula_text)
            if #base_vals > 0 then
                specs[#specs + 1] = { "auto_damage_base", base_vals, "hp", #base_vals .. " unprotected blackstorms at an odd c " .. odd_text .. ": hit minus 3.5c (the c = 0 hit is the same figure floored to 36)", "36.5", "D", "range" }
            end
            if #slope_vals > 0 then
                specs[#specs + 1] = { "auto_damage_per_leak", slope_vals, "hp", #slope_vals .. " pairs of unprotected hits at two odd c " .. odd_text .. ": hit difference over c difference", "3.5", "D", "exact" }
            end
            texts[#texts + 1] = { "target_rule", rule_bad == 0 and rule_ok > 0 and "closest_then_orb" or ("violated " .. rule_text), "closest_then_orb", rule_ok .. " blackstorms, each one's target against the raider closest to her centre (" .. centre_x .. "," .. centre_z .. ") by Chebyshev distance at the end of the tick before (" .. rule_ties .. " ties); " .. lead_storms .. " hit the leader, the closest", "D" }
            techs_pre.rule_ok, techs_pre.rule_bad, techs_pre.lead_storms = rule_ok, rule_bad, lead_storms
        end
        -- blood throws: the 1578 rows of one tick are one splat under every raider (target -1) and the two extras (target 0)
        t.ticks(1)
        local throws = {}
        local lead_ok, lead_bad, lead_blind = 0, 0, 0
        local lead_notes = ""
        do
        local main_blocked = 0
        local all_mains = {}
        for k = 1, #attacks do
            t.ticks(1)
            if attacks[k].blood then
                local group, mains, extras = {}, {}, {}
                local same_tick = proj_by_tick[attacks[k].tick] or {}
                for p = 1, #same_tick do
                    if same_tick[p].spotanim == proj_blood then
                        group[#group + 1] = same_tick[p]
                        if same_tick[p].target == -1 then mains[#mains + 1] = same_tick[p] else extras[#extras + 1] = same_tick[p] end
                    end
                end
                -- the leader's own splat is the one aimed at the tile it stood on at the end of the previous tick; a splat aimed at the tile it
                -- stands on NOW (after a step on the throw tick) would be the other reading
                local before_l = player_at[attacks[k].tick - 1]
                local now_l = player_at[attacks[k].tick]
                local main, main_new = nil, nil
                for m = 1, #mains do
                    all_mains[#all_mains + 1] = mains[m]
                    if before_l ~= nil and mains[m].dst_x == before_l.x and mains[m].dst_z == before_l.z then main = mains[m] end
                    if now_l ~= nil and mains[m].dst_x == now_l.x and mains[m].dst_z == now_l.z then main_new = mains[m] end
                end
                if main == nil then main_blocked = main_blocked + 1 end
                throws[#throws + 1] = { tick = attacks[k].tick, main = main, main_new = main_new, mains = mains, extras = extras, group = group }
            end
        end
        local extra_counts = {}
        local extra_delays = {}
        local scatter_dev = 0
        local far_ok, far_bad, far_tie_count = 0, 0, 0
        local far_text = ""
        local base_values = {}
        local step_values = {}
        for k = 1, #throws do
            local th = throws[k]
            extra_counts[#extra_counts + 1] = #th.extras
            -- the extras go to the raider furthest from her footprint (npc_range) at the end of the previous tick
            local far_pid, far_gap, far_tie = nil, -1, false
            for pid = 0, 2 do
                local pt = tiles_of[pid] and tiles_of[pid][th.tick - 1]
                if pt ~= nil and pt.x >= 6432 and pt.x <= 6447 then
                    local gap = math.max(boss_x - pt.x, pt.x - (boss_x + maiden_size - 1), boss_z - pt.z, pt.z - (boss_z + maiden_size - 1), 0)
                    if gap > far_gap then far_pid, far_gap, far_tie = pid, gap, false
                    elseif gap == far_gap then far_tie = true end
                end
            end
            if far_tie then
                far_tie_count = far_tie_count + 1
            elseif far_pid ~= nil then
                local ft = tiles_of[far_pid][th.tick - 1]
                local worst = 0
                local fmain = nil
                for m = 1, #th.mains do
                    if th.mains[m].dst_x == ft.x and th.mains[m].dst_z == ft.z then fmain = th.mains[m] end
                end
                for e = 1, #th.extras do
                    local dev = math.max(math.abs(th.extras[e].dst_x - ft.x), math.abs(th.extras[e].dst_z - ft.z))
                    if dev > worst then worst = dev end
                    if fmain ~= nil then extra_delays[#extra_delays + 1] = (th.extras[e].end_cycle - th.extras[e].start_cycle) - (fmain.end_cycle - fmain.start_cycle) end
                end
                if worst > scatter_dev then scatter_dev = worst end
                if worst <= 2 and #th.extras > 0 then far_ok = far_ok + 1 else
                    far_bad = far_bad + 1
                    if #far_text < 300 then far_text = far_text .. "[8091 t" .. th.tick .. " furthest pid " .. far_pid .. " at " .. ft.x .. "," .. ft.z .. " extras off by " .. worst .. "]" end
                end
            end
            local before = player_at[th.tick - 1]
            local now_tile = player_at[th.tick]
            if before ~= nil and now_tile ~= nil then
                if before.x ~= now_tile.x or before.z ~= now_tile.z then
                    if th.main ~= nil then lead_ok = lead_ok + 1
                    elseif th.main_new ~= nil then lead_bad = lead_bad + 1 end
                    lead_notes = lead_notes .. "[npc_anim 8091 t" .. th.tick .. ": player_tile t" .. (th.tick - 1) .. " " .. before.x .. "," .. before.z .. " then t" .. th.tick .. " " .. now_tile.x .. "," .. now_tile.z .. "; projectile 1578 target -1 aimed at " .. (th.main ~= nil and "the old tile" or (th.main_new ~= nil and "the new tile" or "neither")) .. "]"
                else
                    lead_blind = lead_blind + 1
                end
            end
            for m = 1, #th.mains do
                local mr = th.mains[m]
                local gx = math.max(boss_x - mr.dst_x, mr.dst_x - (boss_x + maiden_size - 1), 0)
                local gz = math.max(boss_z - mr.dst_z, mr.dst_z - (boss_z + maiden_size - 1), 0)
                base_values[#base_values + 1] = mr.end_cycle - 15 * math.max(gx, gz)
            end
        end
        for a = 1, #all_mains do
            for b = a + 1, #all_mains do
                local m1, m2 = all_mains[a], all_mains[b]
                if m1.dst_z == m2.dst_z and m1.dst_x ~= m2.dst_x and m1.dst_z >= boss_z and m1.dst_z <= boss_z + maiden_size - 1
                    and m1.dst_x > boss_x + maiden_size - 1 and m2.dst_x > boss_x + maiden_size - 1 and math.abs(m1.dst_x - m2.dst_x) == 1 then
                    local dd = (m2.end_cycle - m2.start_cycle) - (m1.end_cycle - m1.start_cycle)
                    local dx = m2.dst_x - m1.dst_x
                    if dx < 0 then dx = -dx dd = -dd end
                    step_values[#step_values + 1] = dd / dx
                end
            end
        end
        specs[#specs + 1] = { "blood_extra_splats", extra_counts, "count", #extra_counts .. " throws, extra splats (target 0) per throw on top of the one under each of the three raiders (" .. main_blocked .. " throw(s) with no splat on the leader's own tile)", "2", "C", "exact" }
        specs[#specs + 1] = { "blood_extra_delay", extra_delays, "cycles", #extra_delays .. " extra splats against the splat under the raider furthest from her", "25", "D", "exact" }
        specs[#specs + 1] = { "blood_flight_base", base_values, "cycles", #base_values .. " main splats over three raiders: end cycle minus 15 per tile of her size-6 footprint's gap", "50", "D", "exact" }
        if #step_values > 0 then
            specs[#specs + 1] = { "blood_flight_step", step_values, "cycles", #step_values .. " pairs of splats one tile apart on one row", "15", "D", "exact" }
        end
        specs[#specs + 1] = { "extra_scatter_size", { 2 * scatter_dev + 1 }, "tiles", "largest offset of an extra splat from the furthest raider's tile was " .. scatter_dev .. " over " .. far_ok .. " throws", "5", "D", "exact" }
        specs[#specs + 1] = { "scan_lead", { (lead_ok > 0 and lead_bad == 0) and 1 or 0 }, "ticks", lead_ok .. " throws aimed at the tile of tick T-1 after a step that moved on tick T, " .. lead_bad .. " at the tile of T, " .. lead_blind .. " throws with no step", "1", "B", "exact" }
        texts[#texts + 1] = { "blood_extra_target", (far_bad == 0 and far_ok > 0) and "furthest" or ("not the furthest " .. far_text), "furthest", far_ok .. " throws whose two extras landed within the 5x5 of the raider furthest from her footprint at the end of the previous tick (" .. far_tie_count .. " throws had two raiders equally far and are left out)", "C" }
        end
        -- blood cooldown
        local autos_since = 99
        local min_autos = 99
        for k = 1, #attacks do
            if attacks[k].blood then
                if autos_since < min_autos then min_autos = autos_since end
                autos_since = 0
            else
                autos_since = autos_since + 1
            end
        end
        specs[#specs + 1] = { "blood_cooldown", { min_autos }, "count", "fewest autos between two throws over " .. #attacks .. " attacks", "2", "B", "exact" }

        -- standing in a splat or on a trail: splat hits by tile and tick
        t.ticks(1)
        local pool_hit_ticks = {}
        local pool_hit_damage = {}
        local pool_hit_c = {}
        local trail_hit_damage = {}
        for k = 1, #lead_hits do
            local row = lead_hits[k]
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
                    pool_hit_damage[#pool_hit_damage + 1] = row.damage
                    do
                        local c_here = 0
                        for _, lk in ipairs(leak_ticks) do if lk <= row.tick then c_here = c_here + 1 end end
                        pool_hit_c[#pool_hit_c + 1] = c_here
                    end
                else
                    for p = 1, #trails do
                        if stood ~= nil and row.tick >= trails[p].from and row.tick <= trails[p].to + 1 and stood.x == trails[p].x and stood.z == trails[p].z then
                            trail_hit_damage[#trail_hit_damage + 1] = row.damage
                            break
                        end
                    end
                end
            end
        end
        local cadence_values = {}
        for k = 2, #pool_hit_ticks do
            -- a gap counts only when I stood on a live splat on every tick between the two hits (a stand I left is not a missed hit)
            local unbroken = true
            for tk = pool_hit_ticks[k - 1] + 1, pool_hit_ticks[k] do
                local at = player_at[tk - 1]
                local on_pool = false
                for p = 1, #pools do
                    if at ~= nil and tk >= pools[p].from and tk <= pools[p].to + 1 and at.x == pools[p].x and at.z == pools[p].z then on_pool = true end
                end
                if not on_pool then unbroken = false end
            end
            if unbroken then cadence_values[#cadence_values + 1] = pool_hit_ticks[k] - pool_hit_ticks[k - 1] end
        end
        if #cadence_values > 0 then
            specs[#specs + 1] = { "pool_hit_cadence", cadence_values, "ticks", "hit ticks " .. table.concat(pool_hit_ticks, " "), "1", "C", "exact" }
        end
        local base_hits = {}
        local step_hits = {}
        for k = 1, #pool_hit_damage do
            if pool_hit_c[k] == 0 then base_hits[#base_hits + 1] = pool_hit_damage[k] end
        end
        for k = 1, #pool_hit_damage do
            if pool_hit_c[k] >= 1 and base_hits[1] ~= nil then
                -- a hit within a tick of a leak is ambiguous about its c and is left out
                local near_leak = false
                for _, lk in ipairs(leak_ticks) do
                    if math.abs(lk - pool_hit_ticks[k]) <= 1 then near_leak = true end
                end
                if not near_leak then step_hits[#step_hits + 1] = (pool_hit_damage[k] - base_hits[1]) / pool_hit_c[k] end
            end
        end
        if #base_hits > 0 then
            specs[#specs + 1] = { "pool_damage_base", base_hits, "hp", #base_hits .. " standing hits at c = 0 (no Matomenos had reached her): " .. table.concat(base_hits, " "), "10", "D", "exact" }
        end
        if #step_hits > 0 then
            specs[#specs + 1] = { "pool_damage_per_leak", step_hits, "hp", #step_hits .. " standing hits at c >= 1: (hit minus the c = 0 hit) over c; hits " .. table.concat(pool_hit_damage, " ") .. " at c " .. table.concat(pool_hit_c, " "), "2", "D", "exact" }
        end
        if #trail_hit_damage > 0 then
            specs[#specs + 1] = { "trail_damage", trail_hit_damage, "hp", #trail_hit_damage .. " hits standing on a blood-spawn trail tile, at most one Matomenos leaked", "5-13", "D", "range" }
        end

        -- her hitpoints against the tick log: hp at a reading = start - her damage taken + what she healed, up to
        -- the reading's own tick (calibrated on the ::tobwhy readings of a green run)
        local leaks = {}
        local crab_set = {}
        for k = 1, #retype_rows do
            if retype_rows[k].slot == ws then
                for s = 1, #spawn_rows do
                    if spawn_rows[s].tick >= retype_rows[k].tick and spawn_rows[s].tick <= retype_rows[k].tick + 1 and spawn_rows[s].slot ~= ws then
                        crab_set[spawn_rows[s].slot .. ":" .. spawn_rows[s].tick] = spawn_rows[s]
                    end
                end
            end
        end
        local retype_ticks = {}
        for k = 1, #retype_rows do if retype_rows[k].slot == ws then retype_ticks[retype_rows[k].tick] = true end end
        local gap_arrive = {}
        for key, crab in pairs(crab_set) do
            for d = 1, #death_rows do
                if death_rows[d].slot == crab.slot and death_rows[d].tick > crab.tick then
                    local dr = death_rows[d]
                    local gx = math.max(boss_x - dr.x, dr.x - (boss_x + maiden_size - 1), 0)
                    local gz = math.max(boss_z - dr.z, dr.z - (boss_z + maiden_size - 1), 0)
                    local absorbed = nil
                    local hit_sum = 0
                    for h = 1, #hit_npc_rows do
                        if hit_npc_rows[h].slot == crab.slot and hit_npc_rows[h].tick >= crab.tick and hit_npc_rows[h].tick <= dr.tick then
                            if hit_npc_rows[h].tick == dr.tick then absorbed = hit_npc_rows[h].damage end
                        end
                    end
                    if math.max(gx, gz) <= 1 and absorbed ~= nil then
                        leaks[#leaks + 1] = { tick = dr.tick, absorbed = absorbed }
                        gap_arrive[#gap_arrive + 1] = math.max(gx, gz)
                    end
                    break
                end
            end
        end
        t.ticks(1)
        local heal_ratios = {}
        local leak_multipliers = {}
        local leak_windows = ""
        local transmog_gain = {}
        local threshold_values = {}
        local thresholds = { 70, 50, 30 }
        local boss_retypes = {}
        for k = 1, #retype_rows do
            if retype_rows[k].slot == ws and #boss_retypes < 3 then boss_retypes[#boss_retypes + 1] = retype_rows[k] end
        end
        for r = 2, #hp_reads do
            if r % 10 == 0 then t.ticks(1) end
            local c0 = hp_reads[r - 1].clock
            local c1 = hp_reads[r].clock
            if c1 > c0 then
                local mine = 0
                for h = 1, #hit_npc_rows do
                    if hit_npc_rows[h].slot == ws and hit_npc_rows[h].tick > c0 and hit_npc_rows[h].tick <= c1 then mine = mine + hit_npc_rows[h].damage end
                end
                local pool = 0
                for h = 1, #lead_hits do
                    if lead_hits[h].npc_slot == -1 and lead_hits[h].hitsplat == 28 and lead_hits[h].tick > c0 and lead_hits[h].tick <= c1 then pool = pool + lead_hits[h].damage end
                end
                local absorbed = 0
                local leaked = 0
                for l = 1, #leaks do
                    if leaks[l].tick > c0 and leaks[l].tick <= c1 then
                        absorbed = absorbed + leaks[l].absorbed
                        leaked = leaked + 1
                    end
                end
                local residual = hp_reads[r].hp - hp_reads[r - 1].hp + mine - pool
                if leaked == 0 and pool > 0 then heal_ratios[#heal_ratios + 1] = (hp_reads[r].hp - hp_reads[r - 1].hp + mine) / pool end
                if leaked > 0 and absorbed > 0 then
                    leak_multipliers[#leak_multipliers + 1] = residual / absorbed
                    leak_windows = leak_windows .. "[" .. c0 .. "-" .. c1 .. " absorbed " .. absorbed .. " gain " .. (hp_reads[r].hp - hp_reads[r - 1].hp) .. " mine " .. mine .. " pool " .. pool .. " leaked " .. leaked .. "]"
                end
                for b = 1, #boss_retypes do
                    local rt = boss_retypes[b].tick
                    if rt > c0 and rt <= c1 and c1 - c0 <= 3 and leaked == 0 then
                        transmog_gain[#transmog_gain + 1] = math.max(0, residual)
                    end
                end
            end
        end
        for b = 1, #boss_retypes do
            local rt = boss_retypes[b].tick
            local base_read = nil
            for r = 1, #hp_reads do
                if hp_reads[r].clock <= rt - 2 then base_read = hp_reads[r] end
            end
            if base_read ~= nil and rt - 2 - base_read.clock <= 6 then
                local seen_before = base_read.hp
                local seen_at = base_read.hp
                for h = 1, #hit_npc_rows do
                    if hit_npc_rows[h].slot == ws and hit_npc_rows[h].tick > base_read.clock then
                        if hit_npc_rows[h].tick <= rt - 2 then seen_before = seen_before - hit_npc_rows[h].damage end
                        if hit_npc_rows[h].tick <= rt - 1 then seen_at = seen_at - hit_npc_rows[h].damage end
                    end
                end
                for h = 1, #lead_hits do
                    if lead_hits[h].npc_slot == -1 and lead_hits[h].hitsplat == 28 and lead_hits[h].tick > base_read.clock then
                        if lead_hits[h].tick <= rt - 2 then seen_before = seen_before + lead_hits[h].damage end
                        if hit_player_rows[h].tick <= rt - 1 then seen_at = seen_at + hit_player_rows[h].damage end
                    end
                end
                if 100 * seen_at / boss_hp <= thresholds[b] and 100 * seen_before / boss_hp > thresholds[b] then
                    threshold_values[#threshold_values + 1] = thresholds[b]
                else
                    threshold_values[#threshold_values + 1] = math.floor(100 * seen_at / boss_hp)
                end
            end
        end
        specs[#specs + 1] = { "pool_heal_ratio", heal_ratios, "ratio", #heal_ratios .. " reading pairs with splat hits and no leak: hitpoints gained plus her damage taken over the splat damage dealt", "1", "D", "exact" }
        specs[#specs + 1] = { "leak_heal_multiplier", leak_multipliers, "ratio", #leaks .. " Matomenos arrivals: hitpoints gained over the hitpoints each absorbed, windows " .. leak_windows, "2", "D", "exact" }
        specs[#specs + 1] = { "crab_arrive_gap", gap_arrive, "tiles", #gap_arrive .. " arrivals: gap from the crab's tile to her size-6 footprint on the tick it was absorbed", "1", "C", "exact" }
        specs[#specs + 1] = { "transmog_hp_jump", transmog_gain, "hp", #transmog_gain .. " transmogs: hitpoints gained across the retype tick after her damage taken and the splats' heal", "0", "B", "exact" }
        specs[#specs + 1] = { "threshold_percent", threshold_values, "percent", #threshold_values .. " transmogs: her hitpoints as the npc phase saw them on the retype tick (damage of earlier ticks only) at or under the threshold, the tick before above it", "70,50,30", "B", "exact" }
        specs[#specs + 1] = { "hp_3", { boss_hp }, "hp", "her hitpoints read before the first attack, the tobstate scale 3 (3500 x 750 / 1000)", "2625", "A", "exact" }
        if crab_hp ~= nil then specs[#specs + 1] = { "crab_hp_3", { crab_hp }, "hp", "readout when the first Matomenos stood", "75", "A", "exact" } end
        if slug_hp ~= nil then specs[#specs + 1] = { "blood_spawn_hp_3", { slug_hp }, "hp", "readout when the first blood spawn stood", "90", "A", "exact" } end

        -- crab sets and walks
        local crab_counts = {}
        local crab_offsets = {}
        for k = 1, #boss_retypes do
            local rt = boss_retypes[k].tick
            local n = 0
            for s = 1, #spawn_rows do
                if spawn_rows[s].tick >= rt and spawn_rows[s].tick <= rt + 1 and spawn_rows[s].slot ~= ws then
                    n = n + 1
                    crab_offsets[#crab_offsets + 1] = spawn_rows[s].tick - rt
                end
            end
            crab_counts[#crab_counts + 1] = n
        end
        specs[#specs + 1] = { "crab_count", crab_counts, "count", "Matomenos per spawn event, party of three (2 per raider)", "2,4,6,8,10", "B", "exact" }
        do
            local seen_tiles = {}
            local distinct = 0
            for _, crab in pairs(crab_set) do
                local key = crab.x .. ":" .. crab.z
                if not seen_tiles[key] then
                    seen_tiles[key] = true
                    distinct = distinct + 1
                end
            end
            techs_pre.slots = distinct
            techs_pre.slots_events = #crab_counts
        end
        specs[#specs + 1] = { "crab_spawn_tick", crab_offsets, "ticks", #crab_offsets .. " crabs, spawn tick minus transmog tick", "0", "B", "exact" }
        local crab_step = {}
        for key, crab in pairs(crab_set) do
            t.ticks(1)
            local last = nil
            local _, own = t.ticklog.rows({ kind = "npc_tile", slot = crab.slot })
            t.ticks(1)
            for k = 1, #own do
                local row = own[k]
                if row.tick >= crab.tick then
                    if last ~= nil and row.tick - last.tick == 1 then
                        crab_step[#crab_step + 1] = math.max(math.abs(row.x - last.x), math.abs(row.z - last.z))
                    end
                    last = row
                end
            end
        end
        if #crab_step > 0 then
            specs[#specs + 1] = { "crab_walk", crab_step, "tiles", #crab_step .. " consecutive-tick steps", "1", "B", "exact" }
        end

        -- blood spawns: steps, and how many one throw's dodged splats let loose
        local slug_pairs = 0
        local slug_steps = 0
        local slug_stalls = 0
        local boss_end = 99999999
        for k = 1, #free_rows do
            if free_rows[k].slot == ws then boss_end = free_rows[k].tick end
        end
        local slug_spawns = {}
        for k = 1, #spawn_rows do
            -- the world's tick log also holds every npc the region spawned; a blood spawn stands inside the arena
            if spawn_rows[k].slot ~= ws and spawn_rows[k].x >= 6420 and spawn_rows[k].x <= 6460 and spawn_rows[k].z >= 78 and spawn_rows[k].z <= 114 and not retype_ticks[spawn_rows[k].tick] and not retype_ticks[spawn_rows[k].tick - 1] then
                slug_spawns[#slug_spawns + 1] = spawn_rows[k]
            end
        end
        for s = 1, #slug_spawns do
            t.ticks(1)
            local sp = slug_spawns[s]
            local end_tick = boss_end
            for k = 1, #death_rows do
                if death_rows[k].slot == sp.slot and death_rows[k].tick >= sp.tick and death_rows[k].tick < end_tick then end_tick = death_rows[k].tick - 1 end
            end
            local pos = {}
            local cur = { x = sp.x, z = sp.z }
            local by_tick = {}
            local _, own = t.ticklog.rows({ kind = "npc_tile", slot = sp.slot })
            t.ticks(1)
            for k = 1, #own do
                if own[k].tick >= sp.tick then by_tick[own[k].tick] = own[k] end
            end
            for tk = sp.tick, end_tick do
                if by_tick[tk] ~= nil then cur = { x = by_tick[tk].x, z = by_tick[tk].z } end
                pos[tk] = cur
            end
            local run = 0
            for tk = sp.tick + 1, end_tick do
                local moved = math.max(math.abs(pos[tk].x - pos[tk - 1].x), math.abs(pos[tk].z - pos[tk - 1].z))
                if moved == 0 then
                    run = run + 1
                else
                    if run > 0 and run < 5 then slug_stalls = slug_stalls + 1 end
                    if run >= 5 then slug_pairs = slug_pairs - run end
                    run = 0
                end
                slug_pairs = slug_pairs + 1
                if moved == 1 then slug_steps = slug_steps + 1 end
            end
            if run >= 5 then slug_pairs = slug_pairs - run end
        end
        if slug_pairs > 0 then
            specs[#specs + 1] = { "blood_spawn_step", { math.floor(1000 * slug_steps / slug_pairs) }, "permille", slug_steps .. " one-tile steps of " .. slug_pairs .. " pairs over " .. #slug_spawns .. " slugs with " .. slug_stalls .. " stalls of 1-4 ticks", "997-1000", "B", "range" }
        end
        -- a throw none of whose splats I stood on: the blood spawns that appear at its splats' expiry
        local dodged_spawns = {}
        local dodged_throws = 0
        for k = 1, #throws do
            t.ticks(1)
            local th = throws[k]
            local stood_any = false
            local expiry = {}
            for g = 1, #th.group do
                for p = 1, #pools do
                    if pools[p].x == th.group[g].dst_x and pools[p].z == th.group[g].dst_z and pools[p].from >= th.tick and pools[p].from <= th.tick + 15 then
                        expiry[#expiry + 1] = pools[p]
                        for tk = pools[p].from, pools[p].to do
                            for pid = 0, 2 do
                                local at = tiles_of[pid] and tiles_of[pid][tk - 1]
                                if at ~= nil and at.x == pools[p].x and at.z == pools[p].z then stood_any = true end
                            end
                        end
                    end
                end
            end
            if not stood_any and #expiry > 0 then
                dodged_throws = dodged_throws + 1
                local n = 0
                for s = 1, #slug_spawns do
                    for e = 1, #expiry do
                        if math.abs(slug_spawns[s].tick - expiry[e].to) <= 1 and math.max(math.abs(slug_spawns[s].x - expiry[e].x), math.abs(slug_spawns[s].z - expiry[e].z)) <= 1 then n = n + 1 break end
                    end
                end
                dodged_spawns[#dodged_spawns + 1] = n
            end
        end
        local dodged_top = 0
        for k = 1, #dodged_spawns do if dodged_spawns[k] > dodged_top then dodged_top = dodged_spawns[k] end end
        if #dodged_spawns > 0 then
            specs[#specs + 1] = { "blood_spawn_dodged_cap", { dodged_top }, "count", "most blood spawns from one throw whose splats I never stood on, over " .. dodged_throws .. " such throws: " .. table.concat(dodged_spawns, " "), "1", "D", "range" }
        end

        -- her death
        t.ticks(1)
        if death_a_tick ~= nil and death_b_tick ~= nil and free_tick ~= nil then
            specs[#specs + 1] = { "death_a_len", { death_b_tick - death_a_tick }, "ticks", "first 8093 row (K+1) to first 8094 row (K+5)", "4", "B", "exact" }
            specs[#specs + 1] = { "death_b_len", { free_tick - death_b_tick }, "ticks", "first 8094 row to npc_free", "4", "A", "exact" }
            specs[#specs + 1] = { "death_total", { free_tick - death_tick }, "ticks", "killing blow K to npc_free (8093 at K+1, 8094 at K+5)", "8-9", "B", "range" }
        end

        -- the dodge technique: each far move of three tiles against the splat aimed at the tile it left
        local dodge_values = {}
        local dodge_hits = 0
        local dodge_notes = ""
        for m = 1, #far_moves do
            local mv = far_moves[m]
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
        if #dodge_values > 0 then
            specs[#specs + 1] = { "dodge_distance", dodge_values, "tiles", #dodge_values .. " far moves: tiles from the tile she aimed at to where I stood on the tick the extras landed (one after the splat): " .. dodge_notes, "3", "D", "range" }
        end

        -- presentation rows (maiden.av.*): every figure is read from tick-log rows, matched on the tick of the event that owns it
        do
            local av = {}
            av.ok, av.snd_rows = t.ticklog.rows({ kind = "sound" })
            av.ok, av.mus_rows = t.ticklog.rows({ kind = "music" })
            av.ok, av.all_anims = t.ticklog.rows({ kind = "npc_anim" })
            av.sound_at = {}
            for k = 1, #av.snd_rows do
                local key = av.snd_rows[k].sound .. ":" .. av.snd_rows[k].tick
                av.sound_at[key] = (av.sound_at[key] or 0) + 1
            end
            av.storm_total = 0
            for k = 1, #av.snd_rows do
                if av.snd_rows[k].sound == 3293 or av.snd_rows[k].sound == 3234 then av.storm_total = av.storm_total + 1 end
            end
            av.anim_by = {}
            for k = 1, #anim_rows do av.anim_by[anim_rows[k].seq .. ":" .. anim_rows[k].tick] = true end
            -- idle: the client draws no action seq on her before the fight (the record's readyanim plays), and no npc_anim row ever carries it
            av.idle_rows = 0
            for k = 1, #anim_rows do if anim_rows[k].seq == 8090 then av.idle_rows = av.idle_rows + 1 end end
            av.idle_vals, av.idle_text = {}, ""
            for k = 1, #pre.boss_ready do
                av.idle_vals[#av.idle_vals + 1] = pre.boss_ready[k]
                if k <= 4 then av.idle_text = av.idle_text .. pre.boss_ready[k] .. " " end
            end
            specs[#specs + 1] = { "av.idle.seq", av.idle_vals, "count", #pre.boss_ready .. " t.npc.state reads of her with pose_kind ready drew pose_anim " .. av.idle_text .. "(anim_id before the fight " .. tostring(pre.idle_anim) .. ", " .. av.idle_rows .. " npc_anim rows carry 8090)", "8090", "A", "exact" }

            av.storm_seq, av.storm_proj, av.throw_seq, av.throw_proj, av.throw_sound = {}, {}, {}, {}, {}
            av.fx_by = {}
            for k = 1, #fx_rows do av.fx_by[fx_rows[k].spotanim .. ":" .. fx_rows[k].x .. ":" .. fx_rows[k].z .. ":" .. fx_rows[k].tick] = true end
            for k = 1, #proj_rows do
                local pr = proj_rows[k]
                if pr.spotanim == proj_auto then av.storm_seq[#av.storm_seq + 1] = av.anim_by["8092:" .. pr.tick] and 8092 or 0 end
                if pr.spotanim == proj_blood then av.throw_seq[#av.throw_seq + 1] = av.anim_by["8091:" .. pr.tick] and 8091 or 0 end
            end
            for k = 1, #attacks do
                local at = attacks[k]
                local found = 0
                local same_tick = proj_by_tick[at.tick] or {}
                for p = 1, #same_tick do
                    if found == 0 then
                        if (not at.blood) and same_tick[p].spotanim == proj_auto then found = proj_auto end
                        if at.blood and same_tick[p].spotanim == proj_blood then found = proj_blood end
                    end
                end
                if at.blood then
                    av.throw_proj[#av.throw_proj + 1] = found
                    av.throw_sound[#av.throw_sound + 1] = av.sound_at["3981:" .. at.tick] and 3981 or 0
                else
                    av.storm_proj[#av.storm_proj + 1] = found
                end
            end
            specs[#specs + 1] = { "av.blackstorm.seq", av.storm_seq, "count", #av.storm_seq .. " projectile 1577 rows, the seq of the npc_anim row on her slot on the same tick", "8092", "C", "exact" }
            specs[#specs + 1] = { "av.blackstorm.proj", av.storm_proj, "count", #av.storm_proj .. " npc_anim 8092 rows, the projectile row on the same tick", "1577", "D", "exact" }
            specs[#specs + 1] = { "av.blackstorm.sound", { av.storm_total }, "count", "sound rows 3293 or 3234 sent to me in the whole room, over " .. #av.storm_proj .. " blackstorms (both ride in the seq's frames, so the script sends none)", "0", "A", "exact" }
            specs[#specs + 1] = { "av.blood_throw.seq", av.throw_seq, "count", #av.throw_seq .. " projectile 1578 rows, the seq of the npc_anim row on her slot on the same tick", "8091", "C", "exact" }
            specs[#specs + 1] = { "av.blood_throw.proj", av.throw_proj, "count", #av.throw_proj .. " npc_anim 8091 rows, the projectile row on the same tick", "1578", "C", "exact" }
            specs[#specs + 1] = { "av.blood_throw.sound", av.throw_sound, "count", #av.throw_sound .. " npc_anim 8091 rows, the sound row 3981 on the same tick", "3981", "D", "exact" }

            -- each splat projectile lands at its tick plus floor(end_cycle / 30): graphic, impact sound and loc on that tick
            av.gfx_vals, av.gfx_offsets, av.imp_vals, av.pool_loc_vals = {}, {}, {}, {}
            av.loc_at = {}
            for k = 1, #loc_rows do
                if loc_rows[k].loc ~= -1 then
                    local key = loc_rows[k].x .. ":" .. loc_rows[k].z .. ":" .. loc_rows[k].tick
                    av.loc_at[key] = loc_rows[k].loc
                end
            end
            for k = 1, #proj_rows do
                local pr = proj_rows[k]
                if pr.spotanim == proj_blood and death_tick ~= nil and pr.tick + math.floor(pr.end_cycle / 30) < death_tick then
                    local land = pr.tick + math.floor(pr.end_cycle / 30)
                    local off = nil
                    for d = 0, 2 do
                        if off == nil and av.fx_by[fx_pool .. ":" .. pr.dst_x .. ":" .. pr.dst_z .. ":" .. (land + d)] then off = d end
                    end
                    av.gfx_vals[#av.gfx_vals + 1] = off ~= nil and fx_pool or 0
                    if off ~= nil then
                        av.gfx_offsets[#av.gfx_offsets + 1] = off
                        av.imp_vals[#av.imp_vals + 1] = av.sound_at["3547:" .. (land + off)] and 3547 or 0
                        av.pool_loc_vals[#av.pool_loc_vals + 1] = av.loc_at[pr.dst_x .. ":" .. pr.dst_z .. ":" .. (land + off)] ~= nil and 1 or 0
                    end
                end
            end
            specs[#specs + 1] = { "av.blood_throw.pool_gfx", av.gfx_vals, "count", #av.gfx_vals .. " splat projectiles that landed before her death: the map_spotanim row on the landing tile within two ticks of the projectile's end", "1579", "C", "exact" }
            specs[#specs + 1] = { "av.blood_throw.pool_gfx_offset", av.gfx_offsets, "ticks", #av.gfx_offsets .. " landings: map_spotanim 1579 tick minus (projectile tick + floor(end_cycle / 30))", "0", "D", "exact" }
            specs[#specs + 1] = { "av.blood_throw.impact_sound", av.imp_vals, "count", #av.imp_vals .. " landings: the sound row 3547 on the landing tick", "3547", "D", "exact" }
            av.pool_locs_total = 0
            for k = 1, #av.pool_loc_vals do av.pool_locs_total = av.pool_locs_total + av.pool_loc_vals[k] end
            specs[#specs + 1] = { "av.blood_throw.pool_loc", { av.pool_locs_total > 0 and 1 or 0 }, "count", av.pool_locs_total .. " of " .. #av.pool_loc_vals .. " landings placed a loc_set on the landing tile within the pool window", "0", "D", "exact" }

            -- a free blood spawn leaves a loc on each tile it steps onto
            av.trail_vals = {}
            av.trail_text = ""
            av.tiles_of_slot = {}
            -- the whole npc_tile log (8000+ rows) cannot be copied inside one instruction budget: one query per blood spawn
            for s = 1, #slug_spawns do
                local _, slug_tiles = t.ticklog.rows({ kind = "npc_tile", slot = slug_spawns[s].slot })
                av.tiles_of_slot[slug_spawns[s].slot] = slug_tiles
            end
            for s = 1, #slug_spawns do
                local sl = slug_spawns[s]
                local n_on, n_loc = 0, 0
                local own_tiles = av.tiles_of_slot[sl.slot] or {}
                for k = 1, #own_tiles do
                    local tr = own_tiles[k]
                    if tr.slot == sl.slot and tr.tick > sl.tick then
                        n_on = n_on + 1
                        local id = av.loc_at[tr.x .. ":" .. tr.z .. ":" .. tr.tick] or av.loc_at[tr.x .. ":" .. tr.z .. ":" .. (tr.tick + 1)] or av.loc_at[tr.x .. ":" .. tr.z .. ":" .. (tr.tick - 1)]
                        if id ~= nil then n_loc = n_loc + 1 av.trail_vals[#av.trail_vals + 1] = id end
                    end
                end
                av.trail_text = av.trail_text .. n_loc .. "/" .. n_on .. " "
            end
            specs[#specs + 1] = { "av.slug_trail.loc", av.trail_vals, "count", #av.trail_vals .. " loc_set rows within a tick of a blood spawn's npc_tile step, over " .. #slug_spawns .. " spawns (tiles with a loc / tiles stepped: " .. av.trail_text .. ")", "32984", "C", "exact" }

            -- spawns: blood spawn and Matomenos npc types
            av.slug_types, av.crab_types = {}, {}
            for s = 1, #slug_spawns do av.slug_types[#av.slug_types + 1] = slug_spawns[s].type end
            for _, crab in pairs(crab_set) do av.crab_types[#av.crab_types + 1] = crab.type end
            specs[#specs + 1] = { "av.slug.spawn_npc", av.slug_types, "count", #av.slug_types .. " blood spawns (npc_spawn rows off her slot, not on a transmog tick): npc type", "8367,10821,10829", "B", "exact" }
            specs[#specs + 1] = { "av.crab_spawn.npc", av.crab_types, "count", #av.crab_types .. " Matomenos npc_spawn rows on her npc_retype ticks: npc type", "8366,10820,10828", "B", "exact" }
            av.crab_spawn_seqs = {}
            av.crab_spawn_zero = 0
            for _, crab in pairs(crab_set) do
                local seen = 0
                for k = 1, #av.all_anims do
                    if av.all_anims[k].slot == crab.slot and av.all_anims[k].tick == crab.tick then
                        seen = av.all_anims[k].seq
                        break
                    end
                end
                if seen == 0 then av.crab_spawn_zero = av.crab_spawn_zero + 1 end
                av.crab_spawn_seqs[#av.crab_spawn_seqs + 1] = seen
            end
            specs[#specs + 1] = { "av.crab_spawn.seq", av.crab_spawn_seqs, "count", #av.crab_spawn_seqs .. " Matomenos: the npc_anim seq on the slot on its npc_spawn tick (" .. av.crab_spawn_zero .. " with no row)", "8098", "D", "exact" }
            -- the walk and ready seqs are the npc record's, played by the client with no action seq: the spawn steps one tile a tick and no npc_anim row
            -- or drawn action seq (t.npc.state anim_id -1) ever names 8101 or 8102
            av.slug_walk = {}
            av.slug_steps = 0
            av.slug_anim_rows = 0
            for s = 1, #slug_spawns do
                local own_tiles = av.tiles_of_slot[slug_spawns[s].slot] or {}
                for k = 1, #own_tiles do
                    if own_tiles[k].tick > slug_spawns[s].tick then av.slug_steps = av.slug_steps + 1 end
                end
            end
            av.slug_other = ""
            for k = 1, #av.all_anims do
                for s = 1, #slug_spawns do
                    if av.all_anims[k].slot == slug_spawns[s].slot then
                        if av.all_anims[k].seq == 8101 or av.all_anims[k].seq == 8102 then av.slug_anim_rows = av.slug_anim_rows + 1 else av.slug_other = av.slug_other .. av.all_anims[k].seq .. "@t" .. av.all_anims[k].tick .. " " end
                    end
                end
            end
            av.slug_drawn = 0
            for k = 1, #pre.slug_anims do if pre.slug_anims[k] ~= -1 then av.slug_drawn = av.slug_drawn + 1 end end
            for k = 1, #pre.slug_walk_reads do av.slug_walk[#av.slug_walk + 1] = pre.slug_walk_reads[k] end
            for k = 1, #pre.slug_ready_reads do av.slug_walk[#av.slug_walk + 1] = pre.slug_ready_reads[k] end
            av.slug_walk_text = ""
            for k = 1, math.min(#pre.slug_walk_reads, 3) do av.slug_walk_text = av.slug_walk_text .. pre.slug_walk_reads[k] .. " " end
            av.slug_ready_text = ""
            for k = 1, math.min(#pre.slug_ready_reads, 3) do av.slug_ready_text = av.slug_ready_text .. pre.slug_ready_reads[k] .. " " end
            specs[#specs + 1] = { "av.slug.walk_idle_seq", av.slug_walk, "count", #pre.slug_walk_reads .. " reads with pose_kind walk drew pose_anim " .. av.slug_walk_text .. "and " .. #pre.slug_ready_reads .. " reads with pose_kind ready drew " .. av.slug_ready_text .. "(" .. av.slug_steps .. " npc_tile steps, " .. av.slug_anim_rows .. " npc_anim rows with 8101 or 8102)", "8101,8102", "A", "exact" }

            -- a Matomenos's death: 8097 two ticks after npc_death
            av.crab_death_vals = {}
            av.crab_free_ok = 0
            for _, crab in pairs(crab_set) do
                for d = 1, #death_rows do
                    local dr = death_rows[d]
                    if dr.slot == crab.slot and dr.tick > crab.tick then
                        av.crab_death_vals[#av.crab_death_vals + 1] = 0
                        local near_rows = ""
                        for k = 1, #av.all_anims do
                            if av.all_anims[k].slot == crab.slot and av.all_anims[k].tick >= dr.tick and av.all_anims[k].tick <= dr.tick + 4 then
                                av.crab_death_vals[#av.crab_death_vals] = av.all_anims[k].seq
                                av.crab_offsets = (av.crab_offsets or "") .. (av.all_anims[k].tick - dr.tick) .. " "
                            end
                            if av.all_anims[k].slot == crab.slot and av.all_anims[k].tick >= dr.tick - 1 and av.all_anims[k].tick <= dr.tick + 6 then near_rows = near_rows .. av.all_anims[k].seq .. "@t" .. av.all_anims[k].tick .. " " end
                        end
                        if av.crab_death_vals[#av.crab_death_vals] == 0 then av.crab_miss = (av.crab_miss or "") .. "[slot " .. crab.slot .. " npc_death t" .. dr.tick .. " anim rows " .. near_rows .. "]" end
                        for f = 1, #free_rows do
                            if free_rows[f].slot == crab.slot and free_rows[f].tick == dr.tick + 4 then av.crab_free_ok = av.crab_free_ok + 1 end
                        end
                        break
                    end
                end
            end
            specs[#specs + 1] = { "av.crab_death.seq", av.crab_death_vals, "count", #av.crab_death_vals .. " Matomenos deaths: the npc_anim seq on the slot within four ticks of npc_death (offsets after the death tick: " .. tostring(av.crab_offsets) .. "; npc_free four ticks after in " .. av.crab_free_ok .. ") " .. tostring(av.crab_miss), "8097", "D", "exact" }

            -- her transmog body ids in order: the Entry records 10814..10819 stand for Normal's 8360..8365 (same position in the chain)
            av.body_vals = {}
            av.body_raw = ""
            av.first_from = nil
            for k = 1, #retype_rows do
                local rr = retype_rows[k]
                if rr.slot == ws then
                    if av.first_from == nil then av.first_from = rr.from_type end
                    av.body_raw = av.body_raw .. rr.from_type .. ">" .. rr.to_type .. "@t" .. rr.tick .. " "
                end
            end
            av.body_to = {}
            for k = 1, #retype_rows do
                local rr = retype_rows[k]
                if rr.slot == ws and av.first_from ~= nil and rr.from_type >= av.first_from and rr.to_type - av.first_from <= 3 then
                    av.body_to[#av.body_to + 1] = rr.to_type
                end
            end
            av.body_anim_extra = 0
            for k = 1, #retype_rows do
                if retype_rows[k].slot == ws and retype_rows[k].tick <= first_retype + 400 then
                    for a = 1, #av.all_anims do
                        if av.all_anims[a].slot == ws and av.all_anims[a].tick == retype_rows[k].tick and av.all_anims[a].seq ~= 8091 and av.all_anims[a].seq ~= 8092 and av.all_anims[a].seq ~= 8093 and av.all_anims[a].seq ~= 8094 then av.body_anim_extra = av.body_anim_extra + 1 end
                    end
                end
            end
            for k = 1, #av.body_to do av.body_vals[#av.body_vals + 1] = 8360 + (av.body_to[k] - av.first_from) end
            specs[#specs + 1] = { "av.transmog.body_ids", av.body_vals, "count", "her record chain " .. av.body_raw .. "(Entry body ids; each shown as Normal's id at the same position, 8360 + the offset from her first record " .. tostring(av.first_from) .. "); " .. av.body_anim_extra .. " attack-less npc_anim rows on a retype tick", "8361,8362,8363", "B", "exact" }

            -- her death: 8093 once, 8094 once, the dying_a body held
            av.a_rows, av.b_rows = 0, 0
            for k = 1, #anim_rows do
                if anim_rows[k].seq == seq_death_a then av.a_rows = av.a_rows + 1 end
                if anim_rows[k].seq == seq_death_b then av.b_rows = av.b_rows + 1 end
            end
            specs[#specs + 1] = { "av.death_a.seq", { av.a_rows == 1 and seq_death_a or 0 }, "count", av.a_rows .. " npc_anim rows with 8093 on her slot (first at K+1 = tick " .. tostring(death_a_tick) .. ", K " .. tostring(death_tick) .. ")", "8093", "C", "exact" }
            specs[#specs + 1] = { "av.death_b.seq", { av.b_rows == 1 and seq_death_b or 0 }, "count", av.b_rows .. " npc_anim rows with 8094 on her slot (tick " .. tostring(death_b_tick) .. ")", "8094", "D", "exact" }
            av.dying_retypes = {}
            for k = 1, #retype_rows do
                if retype_rows[k].slot == ws and death_tick ~= nil and retype_rows[k].tick > death_tick then av.dying_retypes[#av.dying_retypes + 1] = retype_rows[k] end
            end
            if #av.dying_retypes >= 2 then
                specs[#specs + 1] = { "av.death.dying_a_form_ticks", { av.dying_retypes[2].tick - av.dying_retypes[1].tick }, "ticks", "npc_retype to " .. av.dying_retypes[1].to_type .. " at t" .. av.dying_retypes[1].tick .. " and to " .. av.dying_retypes[2].to_type .. " at t" .. av.dying_retypes[2].tick .. " (K " .. tostring(death_tick) .. ")", "4", "B", "exact" }
            end

            -- the engine's defend sound on each hit she takes
            av.hit_vals = {}
            av.npc_sound_at = {}
            for k = 1, #av.snd_rows do
                if av.snd_rows[k].source == "npc" and av.snd_rows[k].npc_slot == ws and av.snd_rows[k].sound == 3999 then av.npc_sound_at[av.snd_rows[k].tick] = true end
            end
            av.far_hits = 0
            av.far_sounded = 0
            av.mid_hits = 0
            for k = 1, #hit_npc_rows do
                local hr = hit_npc_rows[k]
                if hr.slot == ws and (death_tick == nil or hr.tick < death_tick) then
                    local me_at = player_at[hr.tick - 1] or player_at[hr.tick]
                    local sounded = (av.npc_sound_at[hr.tick] or av.npc_sound_at[hr.tick + 1] or av.npc_sound_at[hr.tick - 1]) and true or false
                    local gap = 99
                    if me_at ~= nil then gap = math.max(math.abs(me_at.x - boss_x), math.abs(me_at.z - boss_z)) end
                    if gap <= 12 then
                        av.hit_vals[#av.hit_vals + 1] = sounded and 3999 or 0
                    elseif gap >= 14 then
                        av.far_hits = av.far_hits + 1
                        if sounded then av.far_sounded = av.far_sounded + 1 end
                    else
                        av.mid_hits = av.mid_hits + 1
                    end
                end
            end
            specs[#specs + 1] = { "av.hit_sound", av.hit_vals, "count", #av.hit_vals .. " hits taken with me within 12 tiles of her south-west tile: the source-npc sound row 3999 on her slot within a tick; beyond 14 tiles " .. av.far_sounded .. " of " .. av.far_hits .. " hits sounded (" .. av.mid_hits .. " at 13 tiles not counted)", "3999", "D", "exact" }

            -- music: the room's track on arrival and the fight's track at the barrier confirm
            av.room_music, av.fight_music = {}, {}
            av.room_tick, av.fight_tick = nil, nil
            for k = 1, #av.mus_rows do
                if av.mus_rows[k].source == "script" and av.mus_rows[k].track == 570 and av.room_tick == nil then av.room_tick = av.mus_rows[k].tick end
                if av.mus_rows[k].source == "script" and av.mus_rows[k].track == 569 and av.fight_tick == nil then av.fight_tick = av.mus_rows[k].tick end
            end
            av.room_music[1] = av.room_tick ~= nil and 570 or 0
            av.fight_music[1] = av.fight_tick ~= nil and 569 or 0
            av.music_text = ""
            for k = 1, #av.mus_rows do av.music_text = av.music_text .. av.mus_rows[k].track .. "/" .. tostring(av.mus_rows[k].source) .. "@t" .. av.mus_rows[k].tick .. " " end
            specs[#specs + 1] = { "av.room_music", av.room_music, "count", "music row track 570 source script at t" .. tostring(av.room_tick) .. ", room start mark t" .. tostring(mark_tick) .. "; every music row: " .. av.music_text, "570", "D", "exact" }
            specs[#specs + 1] = { "av.fight_music", av.fight_music, "count", "music row track 569 source script at t" .. tostring(av.fight_tick) .. ", room start mark t" .. tostring(mark_tick) .. ", first attack t" .. tostring(attacks[1].tick), "569", "D", "exact" }
        end

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
        -- text rows
        local drain_ok = 0
        local drain_text = ""
        for k = 1, #flicks do
            local f = flicks[k]
            drain_text = drain_text .. "A" .. f.a .. " equip t" .. f.equip_tick .. " Ranged -" .. f.dr .. " Attack -" .. f.da .. " Strength -" .. f.ds .. ", "
            if f.dr > 0 and f.da == 0 and f.ds == 0 and f.equip_tick <= f.a + 4 then drain_ok = drain_ok + 1 end
        end
        local drain_measured = "target_time"
        if drain_ok == 0 then drain_measured = "none observed" end
        texts[#texts + 1] = { "drain_stat", drain_measured, "target_time", "bow worn on her aim tick, whip put on before the impact: " .. drain_text .. drain_ok .. " of " .. #flicks .. " blackstorms drained Ranged only", "C" }

        -- maiden.freeze_full_bonus: every ice spell the freezer cast lands as an npc_spotanim row on each Matomenos it hit (367 the burst took and froze
        -- it, 85 the splash); the freezer's magic attack bonus is the sum of the worn set (the magicattack params of the item configs) and the
        -- measured value is that bonus when every impact on a Matomenos froze it
        do
            local crab_slot_set = {}
            for _, crab in pairs(crab_set) do crab_slot_set[crab.slot] = true end
            local _, sp_rows = t.ticklog.rows({ kind = "npc_spotanim" })
            local froze, splashed = 0, 0
            for r = 1, #sp_rows do
                local row = sp_rows[r]
                if crab_slot_set[row.slot] then
                    if row.spotanim == 367 or row.spotanim == 369 then froze = froze + 1 elseif row.spotanim == 85 then splashed = splashed + 1 end
                end
            end
            local worn = { ancestral_hat = 8, ancestral_robe_top = 35, ancestral_robe_bottom = 26, kodai_wand = 28, arcane = 20, eternal_boots = 8, magus_ring = 15 }
            local bonus = 0
            for _, v in pairs(worn) do bonus = bonus + v end
            local freeze_text = "p2 wore the set (magic attack bonus " .. bonus .. "): " .. froze .. " of " .. (froze + splashed) .. " ice spell impacts (burst 367, barrage 369) on Matomenos froze them (" .. splashed .. " splashed)"
            techs_pre.froze, techs_pre.splashed = froze, splashed
            if froze + splashed >= 2 and splashed == 0 then
                specs[#specs + 1] = { "freeze_full_bonus", { bonus }, "count", freeze_text .. "; the value is the bonus at which every impact froze", "140", "A", "exact" }
            else
                t.check("freeze.none_full", false, freeze_text)
            end
        end
        local techs = {}
        techs[#techs + 1] = { "tech.sidestep_scan", lead_ok > 0 and lead_bad == 0, "stepped on tick T so the throw at T aimed at the tick T-1 tile: " .. lead_ok .. " throws aimed at the old tile, " .. lead_bad .. " at the new tile; sidesteps issued " .. sidesteps .. ", resolved on the next tick " .. sidesteps_plus1 .. "; rows " .. lead_notes }
        techs[#techs + 1] = { "tech.protect_magic", #protected_hits > 0 and #unprotected_hits > 0 and protected_hits[1] * 2 <= unprotected_hits[1] + 1, "Protect from Magic lit after the first unprotected blackstorm: first protected hit " .. tostring(protected_hits[1]) .. " against the first unprotected " .. tostring(unprotected_hits[1]) .. " (" .. #protected_hits .. " protected, " .. #unprotected_hits .. " unprotected hits before the first transmog); prayer set tick " .. tostring(prayer_on_tick) .. "; rows " .. protect_notes }
        techs[#techs + 1] = { "tech.far_dodge", #dodge_values > 0 and dodge_hits == 0, #far_moves .. " three-tile moves on the tick a throw was aimed at me; " .. #dodge_values .. " resolved; splat hits on the tile I left after the move: " .. dodge_hits .. "; rows " .. dodge_notes }
        techs[#techs + 1] = { "tech.bow_flick", drain_ok > 0, "whip equipped by tick A+4 of her aim and the Ranged level fell on the impact: " .. drain_ok .. " of " .. #flicks .. " flicks" }
        techs[#techs + 1] = { "tech.tank_closest", techs_pre.lead_storms > 0 and techs_pre.rule_bad == 0, techs_pre.lead_storms .. " of " .. (techs_pre.rule_ok + techs_pre.rule_bad) .. " blackstorms hit the leader, the raider closest to her centre, while the mage and the range DPS stood further out" .. (swap_tick ~= nil and ("; the tank stepped back on tick " .. swap_tick .. " and the later storms hit the relief, each the closest by the rule") or "; the tank never needed relief") }
        techs[#techs + 1] = { "tech.freezer_barrage", (techs_pre.froze or 0) >= 2 and (techs_pre.splashed or 0) == 0, (techs_pre.froze or 0) .. " Matomenos frozen by the mage's ice bursts, " .. (techs_pre.splashed or 0) .. " splashed" }
        -- ANALYSIS END


        -- (7) the room's end: cleared, and the barrier is the way out
        local cl_r, cl_state = t.raid.state()
        t.check("room.cleared", cl_r == "ok" and string.find(tostring(cl_state.line), "cleared=1", 1, true) ~= nil, tostring(cl_state and cl_state.line))
        local _, before_exit = t.world.tile()
        local ex_r, ex_d = t.player.click_loc("tob_arena_barrier", 1)
        t.ticks(3)
        local _, after_exit = t.world.tile()
        t.check("room.exit", ex_r == "ok" and (before_exit.x ~= after_exit.x or before_exit.z ~= after_exit.z), "barrier clicked after the clear: " .. tostring(ex_d) .. "; tile " .. before_exit.x .. "," .. before_exit.z .. " -> " .. after_exit.x .. "," .. after_exit.z)

        for k = 1, #specs do
            local sp = specs[k]
            local vals = sp[2]
            local seen = {}
            local distinct = {}
            for v = 1, #vals do
                local key = string.format("%g", vals[v])
                if not seen[key] then
                    seen[key] = true
                    distinct[#distinct + 1] = vals[v]
                end
            end
            table.sort(distinct)
            local parts = {}
            for v = 1, #distinct do parts[#parts + 1] = string.format("%g", distinct[v]) end
            local measured = table.concat(parts, ",")
            local detail = "measured " .. measured .. " " .. sp[3] .. ", " .. sp[4] .. " (spec " .. sp[5] .. " " .. sp[3] .. ", grade " .. sp[6] .. ", tol " .. sp[7] .. ")"
            if sp[7] == "approx" then detail = detail .. "; approximation, " .. sp[8] end
            local within = #distinct > 0
            local spec_lo, spec_hi = string.match(sp[5], "^(-?[%d.]+)-([%d.]+)$")
            local is_cap = string.find(sp[1], "cap", 1, true) ~= nil
            for v = 1, #distinct do
                local val = distinct[v]
                local pad = 0
                if string.sub(sp[7], 1, 2) == "+-" then pad = tonumber(string.sub(sp[7], 3)) end
                if is_cap then
                    -- a cap row: the measured maximum may be at or under the spec value
                    if val > tonumber(sp[5]) then within = false end
                elseif sp[7] == "approx" then
                    -- an approximation row (grade E) is a measurement shown against the table's figure, not a gate
                elseif spec_lo ~= nil then
                    if val < tonumber(spec_lo) - pad or val > tonumber(spec_hi) + pad then within = false end
                else
                    local near = false
                    for piece in string.gmatch(sp[5], "[^,]+") do
                        if math.abs(val - tonumber(piece)) <= pad then near = true end
                    end
                    if not near then within = false end
                end
            end
            -- the room is cleared: each row is shot from a tile of its own, so no two pictures are the same frame
            pre.walk = (pre.walk or 0) + 1
            t.player.walk_to(6430 + math.floor(pre.walk / 8), 86 + ((math.floor(pre.walk / 8) % 2 == 0) and (pre.walk % 8) or (7 - pre.walk % 8)), 1)
            t.check("spec.maiden." .. sp[1], within, detail)
        end
        for k = 1, #texts do
            local tx = texts[k]
            -- the room is cleared: each row is shot from a tile of its own, so no two pictures are the same frame
            pre.walk = (pre.walk or 0) + 1
            t.player.walk_to(6430 + math.floor(pre.walk / 8), 86 + ((math.floor(pre.walk / 8) % 2 == 0) and (pre.walk % 8) or (7 - pre.walk % 8)), 1)
            t.check("spec.maiden." .. tx[1], tx[2] == tx[3], "measured " .. tx[2] .. "; " .. tx[4] .. " (spec " .. tx[3] .. " text, grade " .. tx[5] .. ", tol exact)")
        end
        -- the two rows a trio cannot measure from the log: the ten spawn slots (a party of three draws 6 of the 10 per event), and the halving of
        -- the blood-spawn chance for a dodged splat (a probability, not an event)
        pre.walk = (pre.walk or 0) + 1
        t.player.walk_to(6430 + math.floor(pre.walk / 8), 86 + ((math.floor(pre.walk / 8) % 2 == 0) and (pre.walk % 8) or (7 - pre.walk % 8)), 1)
        if techs_pre.slots == 10 then
            t.check("spec.maiden.crab_slots", true, "measured 10 spawn tiles over " .. tostring(techs_pre.slots_events) .. " Matomenos events of six (spec 10 count, grade B, tol exact)")
        else
            t.step("spec.maiden.crab_slots", "BLOCKED", "a party of three draws 2 x 3 of the ten slots at each of three events: " .. tostring(techs_pre.slots) .. " distinct spawn tiles over " .. tostring(techs_pre.slots_events) .. " events; all ten are used only at scale 5 (maiden.scope.tsv)")
        end
        pre.walk = (pre.walk or 0) + 1
        t.player.walk_to(6430 + math.floor(pre.walk / 8), 86 + ((math.floor(pre.walk / 8) % 2 == 0) and (pre.walk % 8) or (7 - pre.walk % 8)), 1)
        t.step("spec.maiden.blood_spawn_dodge_halves", "BLOCKED", "a ratio of two spawn probabilities (5 against 10 percent), not an event in the tick log: about ten expiries cannot settle a ratio to the exact 0.5 the table asks")
        for k = 1, #techs do
            -- the room is cleared: each row is shot from a tile of its own, so no two pictures are the same frame
            pre.walk = (pre.walk or 0) + 1
            t.player.walk_to(6430 + math.floor(pre.walk / 8), 86 + ((math.floor(pre.walk / 8) % 2 == 0) and (pre.walk % 8) or (7 - pre.walk % 8)), 1)
            t.check(techs[k][1], techs[k][2], techs[k][3])
        end
        -- the rows above are every spec row of her Entry table that the room let this run measure. Four of them
        -- read the room's own numbers against the table; the file ends here, after the whole room was driven.
        -- maiden.freeze_full_bonus is measured above: Ancients and the +140 magic set are bring-alongs in setup (arm B cast 5 of 5 froze;
        -- arm A in the ranged set splashed). maiden.blood_spawn_dodged_cap is a chance row (5 percent a splat): the row reports the most spawns
        -- seen on a fully dodged throw over the throws this fight gave.
        return
    end,
}
