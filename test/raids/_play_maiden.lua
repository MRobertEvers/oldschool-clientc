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
--
-- `--party 3` (raid seam32 play_tob_maiden_normal): the NORMAL TRIO, the same
-- library and plan with the party's roles (PLAY_NOTES.md "Maiden, Normal
-- trio"): seat 1 the ranger tank (the leader, who holds the tick log), seat 2
-- the freezer, seat 3 the north ranger.  A party of one runs the Entry solo
-- harness below unchanged.  `seed_survey.py _play_maiden --party 3`; the
-- determinism gate reads the party size from the file, so party_repeat.py
-- runs a copy that declares `party = 3,` (--script).
local role = (QD_PARTY and QD_PARTY.role) or 1
local size = (QD_PARTY and QD_PARTY.size) or 1
-- THE TRIO'S KIT (raid seam32).  Every raider: the Entry harness's combat
-- levels and its worn ranged set ("Everyone will be ranging in this room",
-- 10Boot transcripts/yt_4i4lv-srJkw.md 0:07:06; "the twisted bow is highly
-- effective against her", W:633), Protect from Magic all fight long (10Boot
-- 0:06:01 "keep it on for the entire fight").  The freezer (seat 2) also
-- carries the Entry harness's +140 magic set, Ancient Magicks and the runes
-- of Ice Barrage (W:594 "Ice Barrage is essentially mandatory"; W:603 "in
-- solo to trio there is one freezer"); the rangers carry food for the tank
-- seat instead ("The person closest to the boss becomes the tank and will
-- take the most damage", 10Boot 0:06:33).  Anglerfish as tob_bloat_normal's
-- party kit; super restores ("stay on top of those super restores",
-- 10Boot 0:06:33); Saradomin brews as the library's top-up.
-- raid seam33: every seat also carries the Dragon warhammer for the opener
-- ("When you run in, everyone should drop a dragon warhammer spec, then switch
-- to range gear", 10Boot 0:06:33; W:624 "Instantly hammer Maiden"); the
-- rangers a loaded toxic blowpipe for the Matomenos ("Everyone else should
-- machine gun down the crabs that aren't in the clump with their blowpipe",
-- 10Boot 0:08:14), loaded by the kit cheat through the content's own use-on
-- (test/raids/README.md "A loaded toxic blowpipe is one kit line"), and the
-- 10Boot kit's supplies, brews over anglerfish ("eight brews, four restores,
-- and three anglers", 10Boot 0:04:23; the slots the melee switches would take
-- hold anglerfish here; two more restores for Rigour's prayer, 10Boot
-- 0:02:45 "77 prayer for rigour").
-- raid seam40 play_tob_maiden_follows_blert: THE REAL TRIO'S KIT.  The
-- reference (docs/minigames/theater_of_blood/sources/blert_api/reference/
-- maiden_normal_3.json, 24 death-free Normal scale-3 rooms) has the two dps
-- on her with the SCYTHE in every phase (dps1 18/24 rooms, dps2 23/24;
-- role.dps*.melee_pct 88.9 / 89.3) from her north-east corner, and the
-- freezer on the twisted bow plus Ice Barrage (barrage_pct 42.4).  So seats
-- 1 and 3 wear ::maxmelee (the scythe, torva; cheat_max_gear.rs2) and seat 2
-- keeps the ranged set and the magic switch.  Every seat still opens with the
-- Dragon warhammer (10Boot 0:06:33; Blert HAMMER in dps1|100, 4 rooms).
local party_kit = {
    "::clearinv",
    "::setlevel attack 99", "::setlevel strength 99", "::setlevel defence 99",
    "::setlevel ranged 99", "::setlevel hitpoints 99", "::setlevel prayer 99",
    "::setlevel magic 99",
}
if role == 2 then
    -- 8 slots of magic set and runes, 20 of supplies
    local more = {
        "::give twisted_bow", "::wield twisted_bow",
        "::give dragon_arrow 1000", "::wield dragon_arrow",
        "::give masori_mask", "::wield masori_mask",
        "::give masori_body", "::wield masori_body",
        "::give masori_chaps", "::wield masori_chaps",
        "::give avas_assembler", "::wield avas_assembler",
        "::give eternal_boots", "::wield eternal_boots",
        "::give magus_ring", "::wield magus_ring",
        "::give dragon_warhammer",
        "::setvar varb4070_spellbook 1",
        "::give water_rune 2000", "::give blood_rune 1000", "::give death_rune 1000",
        "::give ancestral_hat", "::give ancestral_robe_top", "::give ancestral_robe_bottom",
        "::give kodai_wand", "::give arcane",
        "::give anglerfish 13", "::give br_4dose2restore 4", "::give br_4dosepotionofsaradomin 2",
    }
    for _, c in ipairs(more) do party_kit[#party_kit + 1] = c end
else
    -- the scythe worn (::maxmelee charges it), the hammer for the opener,
    -- the 10Boot supplies (brews over anglerfish, restores)
    -- raid seam40: the twisted bow worn at the door for the first shot on the
    -- run in (Blert: TWISTED_BOW in dps1|100 19/24 rooms, dps2|100 22/24,
    -- one attack each; every real raider's first attack at tick 5-6 from
    -- 8 tiles out), the scythe carried for the rest
    -- raid seam54 play_tob_maiden_whole: THE RECORDED SET.  The 24 reference
    -- streams' equipmentDeltas per slot (build/blert_maiden/<uuid>.json, the
    -- uuids of reference/maiden_normal_3.json): both dps wear sanguine torva
    -- helm (28254, 18 / 20 rooms), RADIANT OATHPLATE chest and legs (30779 /
    -- 30781, 16 / 13 rooms), ferocious gloves (24 / 24), avernic treads,
    -- ultor ring, rancour -- the melee set ::tobkit wears (cheat_max_gear.rs2,
    -- raid seam52), not ::maxmelee's torva body and legs.  They take her
    -- storms in it on Protect from Magic (prayerSet bit 16 on 68 / 60 percent
    -- of their ticks, Piety 26 / 20).
    local more = { "::tobkit", "::give twisted_bow", "::wield twisted_bow",
        "::give dragon_arrow 1000", "::wield dragon_arrow", "::give dragon_warhammer",
        "::give br_4dosepotionofsaradomin 8", "::give br_4dose2restore 5", "::give anglerfish 10",
        "::give 4dose2combat 2" }
    for _, c in ipairs(more) do party_kit[#party_kit + 1] = c end
end
-- THE TRIO'S RUN (raid seam32): every seat enters Normal, the leader starts
-- the fight at the barrier and the members follow through it, then ONE call
-- to the library; the leader reads the world's tick log after the kill.
local function party_run(t)
    local mode = "normal"
    if role == 1 then t.ticklog.start() end
    -- raid seam33: the bow on rapid, one tick faster ({{CombatStyles|Bow|
    -- speed=6}}, wiki_Twisted_bow.wikitext:51; the blowpipe's 3 becomes 2,
    -- wiki_Toxic_blowpipe.wikitext:108 "When using the rapid attack style");
    -- the press is _play_nylocas.lua's (combat tab style slot 1, varp43 = 1)
    -- raid seam54: the scythe seats keep style slot 0, Reap (slash): slot 1 is
    -- "Chop", which this content makes STAB (combat.dbrow:72; the wiki's
    -- Module:CombatStyles :547-549 has it Slash; CONTENT_BUGS.md seam52,
    -- open), and the scythe's stab bonus is 70 to its slash 125 against her
    -- 0 / 0 (all.npc tob_maiden_100).  The slot carries across the weapon
    -- swap (varp43), so the bow's rapid was the scythe's Chop all fight.
    -- The freezer keeps the bow's rapid.
    local style_slot = (role == 2) and 1 or 0
    t.ui.tab("combat")
    t.ticks(1)
    local style_widget_result, style_widget = t.ui.widget("combat_interface:style_slot_" .. style_slot)
    local style_press_result = t.ui.invoke(style_widget, 1)
    t.ticks(2)
    local _, style_read = t.var.varp("varp43_com_mode")
    t.check(role == 2 and "setup.rapid" or "setup.reap", style_widget_result == "ok" and style_press_result == "ok" and style_read == style_slot,
        "p" .. role .. " style slot " .. style_slot .. ": widget " .. tostring(style_widget_result) .. ", press " .. tostring(style_press_result) .. ", varp43_com_mode " .. tostring(style_read))
    local er, ed = t.raid.enter("tob", "maiden", { mode = mode })
    t.check("play.enter", er == "ok", "p" .. role .. " " .. tostring(ed))
    if role ~= 2 then
        -- raid seam40: a scythe seat's super combat at the door (_play_bloat.lua's
        -- press; the real dps's swings average about 45 on her, the reference's
        -- output.phase.50.boss_hp_per_tick 15.2 over 13.5 attacks in 40 ticks)
        local pr = t.player.inv_op("4dose2combat", 1, { quick = true })
        t.ticks(1)
        local ar2, att2 = t.skill.read("strength")
        t.check("play.potion", ar2 == "ok" and att2.level > 99, "p" .. role .. " super combat before the barrier: strength " .. tostring(att2.level) .. " (" .. tostring(pr) .. ")")
    end
    t.expect("party.barrier.entrance", t.party.barrier("entrance", 300))
    if role == 1 then
        t.exec("play.barrier", t.player.click_loc, "tob_arena_barrier", 1)
        local mr, md = t.ticklog.mark("room start")
        t.exec("play.begin", t.chat.play, { "options", "choose:Yes, begin the fight." })
        t.check("play.mark", mr == "ok", tostring(md))
        t.expect("party.barrier.started", t.party.barrier("started", 900))
    else
        t.expect("party.barrier.started", t.party.barrier("started", 900))
        local xr, xd = t.player.click_loc("tob_arena_barrier", 1)
        t.check("play.barrier_cross", xr == "ok", "p" .. role .. " " .. tostring(xd))
    end

    -- THE FIGHT: the library and the room's plan, nothing else
    -- raid seam40: the dps seats swing the scythe (the library's cadence row)
    local weapon = (role == 2) and "twisted_bow" or "scythe_of_vitur"
    local result, detail, rec = t.raid.play("tob_maiden", { mode = mode, weapon = weapon, max_ticks = 1400 })
    t.check("play.fight", result == "ok", "p" .. role .. " " .. tostring(detail))
    local m = rec.m or {}
    local casts, nc = "", 0
    for _, c in ipairs(m.casts or {}) do
        nc = nc + 1
        if nc <= 40 then casts = casts .. "[w" .. c.wave .. " t" .. c.tick .. " s" .. c.slot .. " " .. tostring(c.why) .. " " .. c.result .. "]" end
    end
    local seen = m.body_seen or {}
    t.check("play.role", m.role ~= nil, string.sub("p" .. role .. " role " .. tostring(m.role) .. "; her tile " .. tostring(seen.x) .. "," .. tostring(seen.z)
        .. " (offset " .. tostring(m.ox) .. "," .. tostring(m.oz) .. "); home walks " .. tostring(m.home_walks or 0) .. ", dodges " .. tostring(m.dodges)
        .. "; eats " .. #rec.eats .. ", drinks " .. #rec.drinks .. ", swings " .. #rec.swings .. ", add presses " .. tostring(m.add_presses)
        .. "; Ice Barrage casts " .. nc .. " " .. casts, 1, 1800))
    if role == 2 then
        -- raid seam40: the thresholds the casts were made in (the reference's
        -- freezer attacks adds in every crab phase: role.freezer.phase.70/50/30
        -- .attacks_add 4 [2-7] / 5 [3-8] / 4 [2-7], reference/maiden_normal_3.json)
        local forms_cast = {}
        local nforms = 0
        for _, c in ipairs(m.casts or {}) do
            if c.form ~= nil and not forms_cast[c.form] then forms_cast[c.form] = true nforms = nforms + 1 end
        end
        t.check("tech.freezer_casts", nc > 0 and nforms >= 3, "the freezer cast in " .. nforms .. " of her 3 thresholds (counter waves " .. tostring(m.waves) .. "), casts " .. nc)
        -- raid seam35m: what the freezer's plan said each wave could be frozen
        -- (raid_play_tob_maiden.lua _play_maiden_ice_plan, its first cast)
        local plans = {}
        for k, pl in pairs(m.ice_plans or {}) do plans[#plans + 1] = "w" .. k .. " t" .. pl.tick .. " " .. pl.frozen .. " of " .. pl.total end
        table.sort(plans)
        t.check("play.ice_plan", #plans > 0, "the plan's best at each wave's first cast: " .. table.concat(plans, "; "))
    end
    if role ~= 1 then
        t.expect("party.barrier.done", t.party.barrier("done", 9000))
        t.finish(0)
        return
    end

    -- THE LEADER'S TICK LOG
    -- her death (8093, 8094) and the room's line take nine ticks (maiden.death_total)
    t.ticks(16)
    local wave_text = nil
    local _, wave_lines = t.msg.last(120)
    for l = 1, #wave_lines do
        if string.find(wave_lines[l].text, "Wave 'The Maiden of Sugadinti' (Normal Mode) complete!", 1, true) then wave_text = wave_lines[l].text end
    end
    t.check("room.complete_line", wave_text ~= nil, "chat line: " .. tostring(wave_text))
    local cl_r, cl_state = t.raid.state()
    t.check("room.cleared", cl_r == "ok" and string.find(tostring(cl_state.line), "cleared=1", 1, true) ~= nil, tostring(cl_state and cl_state.line))
    local ws = rec.boss_slot
    local mark_tick = nil
    local _, mark_rows = t.ticklog.rows({ kind = "mark" })
    for i = 1, #mark_rows do
        if mark_rows[i].label == "room start" then mark_tick = mark_rows[i].tick end
    end
    t.ticks(1)
    local _, spawn_rows = t.ticklog.rows({ kind = "npc_spawn" })
    t.ticks(1)
    local _, death_rows = t.ticklog.rows({ kind = "npc_death" })
    t.ticks(1)
    local _, free_rows = t.ticklog.rows({ kind = "npc_free" })
    t.ticks(1)
    local _, retype_rows = t.ticklog.rows({ kind = "npc_retype" })
    t.ticks(1)
    local _, ntile_rows = t.ticklog.rows({ kind = "npc_tile" })
    t.ticks(1)
    local _, hitp_rows = t.ticklog.rows({ kind = "hit_player" })
    t.ticks(1)
    local _, hitn_rows = t.ticklog.rows({ kind = "hit_npc" })
    t.ticks(1)
    local _, anim_rows = t.ticklog.rows({ kind = "npc_anim", slot = ws })
    t.ticks(1)
    local ids = m.ids or { boss = {}, crab = {}, slug = {} }
    -- her free tick and her footprint (her npc_tile row; 6x6)
    local free_tick = nil
    for _, r in ipairs(free_rows) do
        if r.slot == ws and (mark_tick == nil or r.tick >= mark_tick) then free_tick = r.tick end
    end
    local bx, bz = seen.x, seen.z
    for _, r in ipairs(ntile_rows) do
        if r.slot == ws then bx, bz = r.x, r.z end
    end
    local function gap(x, z)
        if bx == nil then return 99 end
        return math.max(math.max(bx - x, 0, x - (bx + 5)), math.max(bz - z, 0, z - (bz + 5)))
    end
    -- raid seam35m: the content's own arrival rectangle for a size-2 crab
    -- over its south-west anchor (tob.constant:480-494, ^tob_maiden_arrive_*:
    -- two tiles out on her west and south faces, one on her north and east)
    local function arrived_at(x, z)
        if bx == nil then return false end
        return x - bx >= -2 and x - bx <= 6 and z - bz >= -2 and z - bz <= 6
    end
    -- the thresholds: her retypes to the 70/50/30 forms
    local thresholds = {}
    for _, r in ipairs(retype_rows) do
        if r.slot == ws and ids.boss[r.to_type] ~= nil and (mark_tick == nil or r.tick >= mark_tick) then
            thresholds[#thresholds + 1] = { tick = r.tick, symbol = ids.boss[r.to_type], spawned = 0, frozen = 0, reached = 0, killed = 0, heal = 0, alive_end = 0 }
        end
    end
    -- every Matomenos: spawn, tiles, death; frozen = two ticks or more on one
    -- tile outside her gap (they step every tick, maiden.crab_walk);
    -- reached = it died at her gap (maiden.crab_arrive_gap 1); the heal is
    -- twice the absorb's blow (its remaining hitpoints, maiden.leak_heal_multiplier)
    local crabs = {}
    for _, r in ipairs(spawn_rows) do
        if ids.crab[r.type] ~= nil and (mark_tick == nil or r.tick >= mark_tick) then
            crabs[#crabs + 1] = { slot = r.slot, spawn = r.tick, tiles = {}, death = nil, frozen = false, reached = false }
        end
    end
    for _, c in ipairs(crabs) do
        for _, r in ipairs(death_rows) do
            if r.slot == c.slot and r.tick >= c.spawn and c.death == nil then c.death = r.tick end
        end
        for _, r in ipairs(ntile_rows) do
            if r.slot == c.slot and r.tick >= c.spawn and (c.death == nil or r.tick <= c.death) then c.tiles[#c.tiles + 1] = r end
        end
        local last_end = c.death or free_tick or (c.tiles[#c.tiles] and c.tiles[#c.tiles].tick) or c.spawn
        for k = 1, #c.tiles do
            local nxt = (k < #c.tiles) and c.tiles[k + 1].tick or last_end
            if nxt - c.tiles[k].tick >= 3 and not arrived_at(c.tiles[k].x, c.tiles[k].z) then c.frozen = true end
        end
        -- raid seam35m: reached = it stood in the arrival rectangle at the end
        -- of a tick and died on a later one (the content absorbs it at the
        -- start of the next tick, before any hit: tob_maiden.rs2
        -- [proc,tob_maiden_crab_tick]); one killed on the tick it stepped in
        -- was killed (seam33's gap <= 1 missed every south absorb at gap 2:
        -- m35a w1 S1 absorbed on 5,-2 counted as killed)
        local lt = c.tiles[#c.tiles]
        if c.death ~= nil and lt ~= nil and arrived_at(lt.x, lt.z) and c.death > lt.tick then
            c.reached = true
            for _, r in ipairs(hitn_rows) do
                if r.slot == c.slot and r.tick == c.death then c.heal = 2 * r.damage end
            end
        end
        local w = nil
        for k = 1, #thresholds do
            if c.spawn >= thresholds[k].tick - 1 then w = thresholds[k] end
        end
        if w ~= nil then
            w.spawned = w.spawned + 1
            if c.frozen then w.frozen = w.frozen + 1 end
            if c.reached then
                w.reached = w.reached + 1
                w.heal = w.heal + (c.heal or 0)
            elseif c.death ~= nil then
                w.killed = w.killed + 1
            else
                w.alive_end = w.alive_end + 1
            end
        end
    end
    local wave_text_rows, freeze_ok, reached_total, heal_total = {}, #thresholds == 3, 0, 0
    for k, w in ipairs(thresholds) do
        wave_text_rows[#wave_text_rows + 1] = string.format("w%d %s t%d: %d spawned, %d frozen, %d reached her (heal %d), %d killed, %d alive at the end",
            k, w.symbol, w.tick, w.spawned, w.frozen, w.reached, w.heal, w.killed, w.alive_end)
        -- The bar is what real trios reach, not the guide's ideal: across 26 Regular
        -- scale-3 Maiden rooms on Blert (sources/blert_api/maiden_trio_crabs/README.md)
        -- the median stationary-20+-ticks crabs per wave are 5 / 3 / 4 of 6 at
        -- 70 / 50 / 30 percent, and "all but one" is reached in 26 of 78 waves.
        if w.spawned == 0 or w.frozen < 3 then freeze_ok = false end
        reached_total = reached_total + w.reached
        heal_total = heal_total + w.heal
    end
    t.check("tech.freeze", freeze_ok, "every threshold's Matomenos frozen at the real trios' rate, at least 3 of 6 a wave (Blert 26 Regular trio rooms, median 5/3/4 of 6; W:639-643; 10Boot 0:08:48): "
        .. table.concat(wave_text_rows, "; "))
    -- raid seam40: the bar is the reference's RANGE, not its median
    -- (docs/minigames/theater_of_blood/sources/blert_api/reference/
    -- maiden_normal_3.json, 24 death-free Normal scale-3 rooms:
    -- outcome.leaks 5 [0-13], outcome.boss_heal 223.5 [0-821])
    t.check("tech.crabs_killed", #thresholds == 3 and reached_total <= 13 and heal_total <= 821, "Matomenos that reached her " .. reached_total .. " (heals " .. heal_total
        .. "; W:593 'she will be healed by double the amount of their current Hitpoints'); the reference (reference/maiden_normal_3.json): leaks 5 [0-13], heal 223.5 [0-821]")
    -- raid seam40 THE REFERENCE'S CLOCK: her death and her first threshold
    -- from the room's start (the mark), the reference's outcome.room_ticks
    -- 157.5 [132-204] and outcome.phase.100.ticks 42 [32-52]
    -- (reference/maiden_normal_3.json; raid_report.py --against reads the same
    -- two from this tick log)
    local death_tick = nil
    for _, r in ipairs(death_rows) do
        if r.slot == ws and (mark_tick == nil or r.tick >= mark_tick) and death_tick == nil then death_tick = r.tick end
    end
    local room_ticks = (death_tick and mark_tick) and (death_tick - mark_tick) or nil
    t.check("ref.room_ticks", room_ticks ~= nil and room_ticks >= 132 and room_ticks <= 204,
        "her death " .. tostring(room_ticks) .. " ticks after the room's start (mark " .. tostring(mark_tick) .. ", death " .. tostring(death_tick) .. "); reference/maiden_normal_3.json outcome.room_ticks 157.5 [132-204]")
    local p100 = (thresholds[1] and mark_tick) and (thresholds[1].tick - mark_tick) or nil
    t.check("ref.phase_100_ticks", p100 ~= nil and p100 >= 32 and p100 <= 52,
        "her 70 percent form " .. tostring(p100) .. " ticks after the room's start; reference/maiden_normal_3.json outcome.phase.100.ticks 42 [32-52]")
    -- who took her blackstorms, and the blood (pools and trails: hitsplat 28 with no npc)
    -- the log's pid of each seat: the leader's is the plan's own (re-read by
    -- tile, raid_play_tob_maiden.lua), the freezer's is the one whose
    -- player_anim rows carry Ice Barrage's cast (seq 1979), the third is the
    -- remaining one (s32mzn7: a tile match at the end could not tell them)
    local pid_seat = {}
    local _, cast_rows = t.ticklog.rows({ kind = "player_anim", seq = 1979 })
    t.ticks(1)
    local _, ptile_rows = t.ticklog.rows({ kind = "player_tile" })
    t.ticks(1)
    if rec.my_pid ~= nil then pid_seat[rec.my_pid] = 1 end
    for _, r in ipairs(cast_rows) do
        if pid_seat[r.pid] == nil then pid_seat[r.pid] = 2 end
    end
    for _, r in ipairs(ptile_rows) do
        if pid_seat[r.pid] == nil then pid_seat[r.pid] = 3 end
    end
    local storm_by, blood_by, taken_by = {}, {}, {}
    for _, r in ipairs(hitp_rows) do
        local seat = pid_seat[r.pid] or ("_pid" .. tostring(r.pid))
        if mark_tick == nil or r.tick >= mark_tick then
            taken_by[seat] = (taken_by[seat] or 0) + r.damage
            if r.npc_slot == ws then storm_by[seat] = (storm_by[seat] or 0) + 1 end
            if r.npc_slot == -1 and r.hitsplat == 28 then blood_by[seat] = (blood_by[seat] or 0) + r.damage end
        end
    end
    local function seats(tbl)
        local parts = {}
        for k, val in pairs(tbl) do parts[#parts + 1] = "p" .. tostring(k) .. "=" .. tostring(val) end
        table.sort(parts)
        return table.concat(parts, " ")
    end
    -- raid seam40: the two scythe seats share her storms (the leader's step
    -- out on every other attack, raid_play_tob_maiden.lua THE SHARED TANK);
    -- the reference's role.dps1.boss_targeted_pct 38 [5.6-58.8] and dps2
    -- 41.45 [18.8-64.3] (reference/maiden_normal_3.json): each seat's share
    -- of all her storms inside [5.6, 64.3]
    local storm_all = 0
    for _, n in pairs(storm_by) do storm_all = storm_all + n end
    local share1 = storm_all > 0 and math.floor(100 * (storm_by[1] or 0) / storm_all + 0.5) or 0
    local share3 = storm_all > 0 and math.floor(100 * (storm_by[3] or 0) / storm_all + 0.5) or 0
    t.check("ref.storm_share", storm_all > 0 and share1 >= 5.6 and share1 <= 64.3 and share3 >= 5.6 and share3 <= 64.3,
        "her storms by seat " .. seats(storm_by) .. ": seat 1 " .. share1 .. "%, seat 3 " .. share3 .. "%; reference/maiden_normal_3.json dps boss_targeted_pct 38 [5.6-58.8] / 41.45 [18.8-64.3]")
    t.check("tech.tank", storm_by[2] == nil, "blackstorm hits by seat: " .. seats(storm_by) .. "; the freezer (p2) never the closest (10Boot 0:06:33 'You don't want your mage to be closest at any time')")
    local storms, bloods = 0, 0
    for k = 1, #anim_rows do
        if anim_rows[k].seq == 8092 then storms = storms + 1 elseif anim_rows[k].seq == 8091 then bloods = bloods + 1 end
    end
    t.check("play.measure_party", true, string.format("room %s ticks (mark %s, npc_free %s); her attacks %d blackstorm, %d blood; damage taken by seat %s; blood (pools+trails) by seat %s; pids %s",
        tostring(free_tick and mark_tick and (free_tick - mark_tick)), tostring(mark_tick), tostring(free_tick), storms, bloods, seats(taken_by), seats(blood_by), seats(pid_seat)))
    t.expect("party.barrier.done", t.party.barrier("done", 9000))
    t.finish(0)
end
return {
    id = "_play_maiden",
    fixture = "fresh_lumbridge.ini",
    max_frames = 200000,
    setup = (size > 1) and party_kit or {
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
        "::give shark 9",
        -- raid seam40: the scythe the solo puts on once its technique rows are
        -- measured (reference/maiden_entry_1.json: the real Entry solo is
        -- melee, SCYTHE in every phase), one shark's slot
        "::fullscythe",
        -- two Saradomin brews: more healing, and the room's debug kit adds none of its own when the pack holds one
        "::give br_4dosepotionofsaradomin 6",
        -- three prayer restores: Protect from Magic drains through the fight and the pools drain more
        "::give br_4dose2restore 3",
        -- a melee weapon for the bow flick (the drain is judged on the weapon worn when she aims)
        "::give abyssal_whip",
    },

    run = function(t)
        if size > 1 then
            party_run(t)
            return
        end
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
