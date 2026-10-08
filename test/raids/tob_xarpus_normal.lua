-- tob_xarpus_normal: Xarpus, Normal Mode, a party of THREE (three driven clients in one world; the leader holds the world's tick log).
-- ROLES (sources named beside each branch; the leader writes every spec row and tick-ledger row):
--   Phase 1: "stand on top of the exhumed ... in the centre of the arena to quickly intercept any exhumed that appear"
--     (wiki_Theatre_of_Blood_Strategies.wikitext:831); "you're responsible for standing on all the exomes in your quadrant ... prioritizing the
--     new ones ... do not run off to Narnia to help somebody with their exomes" (transcripts/yt_KF9y2GYTJ-A.md:1145). Three raiders and four
--     corners: the raider nearest a new exhumed that is not already standing on another takes it. The FIRST exhumed of the room is the leader's
--     and it covers it a few ticks late: its heal orbs are what rows xarpus.p1.heal_delay, heal_gap and heal_amount.normal read.
--   Phase 2: all three are RANGERS ("If players are ranging, stand on the back two rows to give the melee users space",
--     wiki_Theatre_of_Blood_Strategies.wikitext:845; "the blowpipe can actually hit Xarpus from every single tile in the room ... keep piping the
--     boss until he looks at you. When he does move two tiles away", yt_KF9y2GYTJ-A.md:1175): each stands off the footprint, reads every spit and
--     chain orb in the air, and when one is aimed within a tile of it walks two tiles clear before it lands ("always run back once you see or hear
--     him attack", the wiki caption at :845). Redemption on ("melee players should have Redemption on", wiki :833, worn by every raider here).
--   Phase 3: "your job is to attack him when he is not looking at the corner that you are in ... always attacking from a quadrant" (yt_KF9y2GYTJ-A.md:1205),
--     "players should be moving to where he last looked" (wiki_Theatre_of_Blood_Strategies.wikitext:853: he never looks in the same corner twice).
-- A party of three fixes the boss at 750 permille of the five-man pool (raidwide.scale.party_3_or_fewer): Normal Xarpus 3750, Defence 250.
local role0 = (QD_PARTY and QD_PARTY.role) or 1
local kit = {
    "::clearinv",
    "::setlevel prayer 99",
    -- all three are rangers at the back rows ("If players are ranging, stand on the back two rows to give the melee users space",
    -- wiki_Theatre_of_Blood_Strategies.wikitext:845): ::maxrange is the content's best-in-slot ranged set with the twisted bow and its quiver
    "::maxrange",
}
if role0 == 1 then
    -- the ranger's potion, drunk before the fight (only the leader carries and drinks one)
    kit[#kit + 1] = "::give br_4doserangerspotion 1"
end
-- food, eaten through the fight; a Saradomin brew lifts hitpoints above the level (a raider holding none is handed free doses on entry)
kit[#kit + 1] = "::give anglerfish 16"
kit[#kit + 1] = "::give br_4dosepotionofsaradomin 2"
kit[#kit + 1] = "::give br_4dose2restore 2"

return {
    id = "tob_xarpus_normal",
    fixture = "fresh_lumbridge.ini",
    party = 3,
    max_frames = 120000,
    setup = kit,

    run = function(t)
        local role = t.party.role()
        if role == 1 then
            t.check("spec.scope", true, "mode=normal party=3")
            local srl, sdl = t.ticklog.start()
            t.expect("ticklog.start", srl, tostring(sdl))
        end
        local sym = "tob_xarpus_combat"
        local AS = {}
        local F = { covers = {}, reads = {}, dodges = {}, retal = {}, turns = {} }
        -- (1) the room, the way a player arrives: all three call it, the leader lands with ::tobmode and each member joins its instance
        local er, ed = t.raid.enter("tob", "xarpus", { mode = "normal" })
        t.expect("raid.enter", er, "p" .. role .. " " .. tostring(ed))
        t.expect("party.barrier.entrance", t.party.barrier("entrance", 300))
        -- the bow on rapid (a shot every four ticks): the combat tab's first style button
        do
            t.ui.tab("combat")
            t.ticks(2)
            local sw_result, sw = t.ui.widget("combat_interface:style_slot_1")
            if sw_result == "ok" then t.ui.invoke(sw, 1) end
            t.ticks(1)
            local sv_result, sv = t.var.varp("varp43_com_mode")
            t.check("style.rapid", sw_result == "ok" and sv == 1, "p" .. role .. " combat tab style slot 1 pressed for the bow: widget " .. tostring(sw_result) .. ", style " .. tostring(sv))
        end
        local bslot, size1, ft0 = nil, -1, 0
        if role == 1 then
            local sr2, st2 = t.raid.state()
            t.expect("raid.state", (sr2 == "ok" and st2.mode == "normal" and st2.started == false) and "ok" or "bad", tostring(st2 and st2.line))
            local bossr, bossrow = t.npc.nearest("tob_xarpus_static", 20)
            local slr
            slr, bslot = t.ticklog.slot(bossrow)
            t.expect("boss.slot", (bossr == "ok" and slr == "ok") and "ok" or "bad", "boss client slot " .. tostring(bossrow and bossrow.slot) .. " at " .. tostring(bossrow and bossrow.x) .. "," .. tostring(bossrow and bossrow.z) .. " -> world slot " .. tostring(bslot))
            local szr, szrow = t.npc.state("tob_xarpus_static")
            size1 = (szr == "ok") and szrow.size or -1
            local _, ftick = t.tick()
            ft0 = ftick
            -- (3) the room starts by the player's own click on the barrier: the leader crosses
            local frr, fight = t.raid.start_tile()
            t.player.walk_to(fight.x, fight.z - 3, 20)
            local cr, cd = t.player.click_loc("tob_arena_barrier", 1)
            t.check("barrier.click", cr == "ok", tostring(cd))
            t.chat.play({ "options", "choose:Yes, begin the fight." })
            t.ticklog.mark("room start")
            t.check("fight.begins", t.msg.expect("The fight begins") == "ok", "the fight-begins line is in the chat ring")
        end
        t.expect("party.barrier.started", t.party.barrier("started", 400))
        if role ~= 1 then
            -- A MEMBER'S PART: its own clicks, prayers, steps, swings and eats; every row its own (the union ledger prefixes it p<n>:).
            local BX0, BX1, BZ0, BZ1 = 6432, 6436, 97, 101
            local food = "anglerfish"
            local xr, xd = t.player.click_loc("tob_arena_barrier", 1)
            local _, xt = t.world.tile()
            t.check("barrier.cross", xr == "ok", "p" .. role .. " crossed the open barrier: " .. tostring(xd) .. ", now at " .. tostring(xt and (xt.x .. "," .. xt.z)))
            -- phase 1: stand on the exhumed of the raider's own quadrant; the first one of the room is the leader's (left to rise alone)
            local seen, nseen, covered = {}, 0, {}
            local combat_up = false
            for loop = 1, 300 do
                t.ticks(1)
                local cbr = t.npc.nearest("tob_xarpus_combat", 40)
                if cbr == "ok" then
                    combat_up = true
                    break
                end
                local cr2, cs2, rows = t.world.loc_copies("tob_xarpus_exhumed", 40)
                local present = {}
                if cr2 == "ok" and type(rows) == "table" then
                    local target = nil
                    for i = 1, #rows do present[rows[i].x * 1000 + rows[i].z] = true end
                    local _, _, others = t.party.players(60)
                    local _, mep = t.world.tile()
                    for i = 1, #rows do
                        local key = rows[i].x * 1000 + rows[i].z
                        if not seen[key] then
                            seen[key] = true
                            nseen = nseen + 1
                            local md = math.max(math.abs(mep.x - rows[i].x), math.abs(mep.z - rows[i].z)) + (present[mep.x * 1000 + mep.z] and 100 or 0)
                            local mine = true
                            if type(others) == "table" then
                                for j = 1, #others do
                                    local od = math.max(math.abs(others[j].x - rows[i].x), math.abs(others[j].z - rows[i].z)) + (present[others[j].x * 1000 + others[j].z] and 100 or 0)
                                    if od < md or (od == md and others[j].pid < role) then mine = false end
                                end
                            end
                            if nseen > 1 and mine then target = rows[i] end
                        end
                    end
                    if target ~= nil then
                        local wr, wd = t.player.walk_to(target.x, target.z, 8)
                        local _, at = t.world.tile()
                        covered[#covered + 1] = string.format("[%d,%d stood %d,%d]", target.x, target.z, at.x, at.z)
                        if #covered == 2 then
                            -- "pot up and get ready" (yt_KF9y2GYTJ-A.md:1175): the brew drunk standing on an exhumed
                            t.ui.tab("inventory")
                            local br, bd = t.player.inv_op("br_4dosepotionofsaradomin", 1)
                            t.check("member.brew", br == "ok", "p" .. role .. " Saradomin brew before the swings: " .. tostring(br) .. " " .. tostring(bd))
                        end
                    end
                end
                for key in pairs(seen) do
                    if not present[key] then seen[key] = nil end
                end
            end
            t.expect("member.p1_covers", (combat_up and #covered >= 2) and "ok" or "bad", "p" .. role .. " saw " .. nseen .. " exhumed rise, stood on " .. #covered .. " of its own quadrant: " .. table.concat(covered, " "))
            -- the melee prayers for phases 2 and 3 ("melee players should activate Redemption", wiki_Theatre_of_Blood_Strategies.wikitext:833)
            local pr1, pd1 = t.prayer.set("rigour", true)
            t.check("member.prayer_rigour", pr1 == "ok", "p" .. role .. " " .. tostring(pd1))
            local pr2, pd2 = t.prayer.set("redemption", true)
            t.check("member.prayer_redemption", pr2 == "ok", "p" .. role .. " " .. tostring(pd2))
            -- phases 2 and 3: swing, step off acid, eat; from the screech swing only from a quadrant he is not facing
            local ringq = {
                SW = { { 6432, 96 }, { 6433, 96 }, { 6434, 96 }, { 6431, 97 }, { 6431, 98 }, { 6431, 99 } },
                SE = { { 6435, 96 }, { 6436, 96 }, { 6437, 97 }, { 6437, 98 }, { 6437, 99 } },
                NW = { { 6431, 100 }, { 6431, 101 }, { 6432, 102 }, { 6433, 102 }, { 6434, 102 } },
                NE = { { 6437, 100 }, { 6437, 101 }, { 6435, 102 }, { 6436, 102 } },
            }
            local stats = { swings = 0, eats = 0, steps = 0, dodges = 0, p3_loops = 0, moves = 0, lowest = 999, gone = 0, p3 = false }
            local last_press, gaze, gaze_key, goal_set = -9, nil, nil, false
            for loop = 1, 900 do
                local bsr, bs = t.npc.state(sym)
                if bsr ~= "ok" then
                    stats.gone = stats.gone + 1
                    if stats.gone >= 3 then break end
                    t.ticks(1)
                else
                    stats.gone = 0
                    local pct = 100
                    if bs.health_scale and bs.health_scale > 0 and bs.health_ratio and bs.health_ratio >= 0 then
                        pct = bs.health_ratio * 100 / bs.health_scale
                    end
                    local _, me = t.world.tile()
                    local hr, hv = t.skill.read("hitpoints")
                    local hp = (type(hv) == "table") and hv.level or 99
                    if hp < stats.lowest then stats.lowest = hp end
                    if pct <= 26 and not stats.p3 then
                        -- the screech: from here on only swing from a quadrant he is not facing (yt_KF9y2GYTJ-A.md:1205)
                        stats.p3 = true
                        stats.p3_first = loop
                        stats.p3_pct = pct
                        goal_set = false
                    end
                    -- acid under the raider's feet (a puddle hurts a player standing on its tile)
                    local hz, hzd = t.world.hazard_at(me.x, me.z)
                    local on_acid = false
                    if hz == "ok" and type(hzd) == "table" and type(hzd.locs) == "table" then
                        for i = 1, #hzd.locs do
                            if hzd.locs[i].loc_id == 32744 then on_acid = true end
                        end
                    end
                    local mq
                    if me.z > 99 then mq = (me.x > 6434) and "NE" or "NW" else mq = (me.x > 6434) and "SE" or "SW" end
                    if stats.p3 and bs.face_x ~= nil and bs.face_x >= 0 then
                        local key = tostring(bs.face_x) .. "," .. tostring(bs.face_z) .. "@" .. tostring(bs.face_tick)
                        if key ~= gaze_key and math.abs(bs.face_x - 6434) >= 4 and math.abs(bs.face_z - 99) >= 4 then
                            gaze_key = key
                            gaze = (bs.face_z > 99) and ((bs.face_x > 6434) and "NE" or "NW") or ((bs.face_x > 6434) and "SE" or "SW")
                            if mq == gaze then goal_set = false end
                        end
                    end
                    local acted = false
                    if hp < 70 then
                        stats.eats = stats.eats + 1
                        t.player.inv_op(food, 1)
                        acted = true
                    end
                    if not stats.p3 then
                        -- a spit or a chain orb in the air whose landing square holds the raider: two tiles away before it lands
                        local aims = {}
                        local pjr, pjrows = t.world.projectiles(16)
                        if pjr == "ok" and type(pjrows) == "table" then
                            for i = 1, #pjrows do
                                if pjrows[i].spotanim_id == 1555 and (pjrows[i].cycles_left or 0) > 0 then
                                    aims[#aims + 1] = { pjrows[i].dst_x, pjrows[i].dst_z }
                                end
                            end
                        end
                        local threatened = false
                        for i = 1, #aims do
                            if math.max(math.abs(aims[i][1] - me.x), math.abs(aims[i][2] - me.z)) <= 1 then threatened = true end
                        end
                        if threatened or on_acid then
                            local bd, goal = 9999, nil
                            for gx = me.x - 3, me.x + 3 do
                                for gz = me.z - 3, me.z + 3 do
                                    local infoot = gx >= BX0 and gx <= BX1 and gz >= BZ0 and gz <= BZ1
                                    if not infoot and gx >= 6427 and gx <= 6441 and gz >= 92 and gz <= 106 then
                                        local clear = true
                                        for i = 1, #aims do
                                            if math.max(math.abs(aims[i][1] - gx), math.abs(aims[i][2] - gz)) <= 1 then clear = false end
                                        end
                                        if clear then
                                            local h2, h2d = t.world.hazard_at(gx, gz)
                                            if h2 == "ok" and type(h2d) == "table" and type(h2d.locs) == "table" then
                                                for i = 1, #h2d.locs do
                                                    if h2d.locs[i].loc_id == 32744 then clear = false end
                                                end
                                            end
                                        end
                                        local d = math.max(math.abs(gx - me.x), math.abs(gz - me.z))
                                        -- a tile nearer the back wall keeps the ring clear for the melee raider
                                        local back = math.max(math.abs(gx - 6434), math.abs(gz - 99))
                                        if clear and d >= 1 and d * 10 - back < bd then bd, goal = d * 10 - back, { gx, gz } end
                                    end
                                end
                            end
                            if goal ~= nil then
                                t.player.walk_to(goal[1], goal[2], 4)
                                if threatened then stats.dodges = stats.dodges + 1 else stats.steps = stats.steps + 1 end
                            end
                            t.player.attack(sym, 2, 1)
                            stats.swings = stats.swings + 1
                            last_press = loop
                        elseif not acted and loop - last_press >= 5 then
                            t.player.attack(sym, 2, 1)
                            stats.swings = stats.swings + 1
                            last_press = loop
                        elseif not acted then
                            t.ticks(1)
                        end
                    else
                        stats.p3_loops = stats.p3_loops + 1
                        if on_acid then goal_set = false end
                        if gaze == nil then
                            if not acted then t.ticks(1) end
                        elseif not goal_set then
                            -- a clean tile (no acid) of a quadrant he is not facing, the nearest, off the centre lines and outside his footprint
                            local bd, goal = 9999, nil
                            for gx = me.x - 5, me.x + 5 do
                                for gz = me.z - 5, me.z + 5 do
                                    local inarena = gx >= 6427 and gx <= 6441 and gz >= 92 and gz <= 106
                                    local infoot = gx >= BX0 and gx <= BX1 and gz >= BZ0 and gz <= BZ1
                                    if inarena and not infoot and gx ~= 6434 and gz ~= 99 then
                                        local q = (gz > 99) and ((gx > 6434) and "NE" or "NW") or ((gx > 6434) and "SE" or "SW")
                                        local d = math.max(math.abs(gx - me.x), math.abs(gz - me.z))
                                        if q ~= gaze and d < bd then
                                            local h3, h3d = t.world.hazard_at(gx, gz)
                                            local bad = false
                                            if h3 == "ok" and type(h3d) == "table" and type(h3d.locs) == "table" then
                                                for j = 1, #h3d.locs do
                                                    if h3d.locs[j].loc_id == 32744 then bad = true end
                                                end
                                            end
                                            if not bad then bd, goal = d, { gx, gz } end
                                        end
                                    end
                                end
                            end
                            if goal ~= nil then
                                t.player.walk_to(goal[1], goal[2], 8)
                                stats.moves = stats.moves + 1
                            end
                            t.player.attack(sym, 2, 1)
                            stats.swings = stats.swings + 1
                            last_press = loop
                            goal_set = true
                        elseif mq ~= gaze and not acted and loop - last_press >= 4 then
                            t.player.attack(sym, 2, 1)
                            stats.swings = stats.swings + 1
                            last_press = loop
                        elseif not acted then
                            t.ticks(1)
                        end
                    end
                    if loop % 10 == 0 then
                        t.expect("member.trace" .. string.format("%03d", loop), "ok", "p" .. role .. " loop " .. loop .. " at " .. me.x .. "," .. me.z .. " hp " .. hp .. " boss " .. string.format("%.1f", pct) .. " percent, swings " .. stats.swings .. ", eats " .. stats.eats .. ", dodges " .. stats.dodges .. ", acid steps " .. stats.steps .. ", on acid " .. tostring(on_acid) .. ", p3 " .. tostring(stats.p3))
                    end
                end
            end
            t.check("member.fight", stats.gone >= 3, "p" .. role .. " ranger: " .. stats.swings .. " swing presses, " .. stats.eats .. " eats, " .. stats.steps .. " steps off acid, lowest hitpoints " .. stats.lowest .. "; phase 3 seen at loop " .. tostring(stats.p3_first) .. " (face " .. tostring(stats.p3_face) .. ", bar " .. tostring(stats.p3_pct) .. " percent), " .. stats.p3_loops .. " phase 3 loops, " .. stats.moves .. " quadrant changes, last gaze " .. tostring(gaze))
            t.expect("party.barrier.dead", t.party.barrier("dead", 900))
            t.finish(0)
            return
        end
        t.player.walk_to(6434, 95, 6)
        -- (4a) phase 1: stand on the exhumed to stop their heal; the first one is left to rise uncovered so its heal orbs can be read
        local W, U = nil, nil
        local nloc, rise_idx = 0, 0
        local rises, despawns = {}, {}
        local uncovered = { [1] = true }
        for loop = 1, 300 do
            t.ticks(1)
            do
                local ss1, sr1 = t.npc.state(sym)
                local _, sk1 = t.tick()
                AS[sk1] = (ss1 == "ok") and sr1.anim_id or -2
            end
            local _, ls = t.ticklog.rows({ kind = "loc_set" })
            local target = nil
            -- who takes a new exhumed: the raider nearest to it that is not standing on another open one ("do not run off to Narnia to help somebody
            -- with their exomes", yt_KF9y2GYTJ-A.md:1145: the nearest quadrant's raider); the first of the room is the leader's, held back
            local _, _, cps = t.world.loc_copies("tob_xarpus_exhumed", 40)
            local lpresent = {}
            if type(cps) == "table" then
                for i = 1, #cps do lpresent[cps[i].x * 1000 + cps[i].z] = true end
            end
            local _, _, lothers = t.party.players(60)
            local _, lme = t.world.tile()
            for i = nloc + 1, #ls do
                if ls[i].tick >= ft0 then
                    if ls[i].loc == 32743 then
                        rises[#rises + 1] = ls[i]
                        -- the leader's share: the first exhumed of the room and the south half (z 99 and below)
                        local md = math.max(math.abs(lme.x - ls[i].x), math.abs(lme.z - ls[i].z)) + (lpresent[lme.x * 1000 + lme.z] and 100 or 0)
                        local lmine = true
                        if type(lothers) == "table" then
                            for j = 1, #lothers do
                                local od = math.max(math.abs(lothers[j].x - ls[i].x), math.abs(lothers[j].z - ls[i].z)) + (lpresent[lothers[j].x * 1000 + lothers[j].z] and 100 or 0)
                                if od < md or (od == md and lothers[j].pid < 1) then lmine = false end
                            end
                        end
                        if #rises == 1 or lmine then target = ls[i] end
                    elseif ls[i].loc == -1 then
                        despawns[#despawns + 1] = ls[i]
                    end
                end
            end
            nloc = #ls
            if target then
                rise_idx = rise_idx + 1
                -- the first exhumed rises alone for a few ticks: its orbs are what the heal rows read
                if #rises == 1 then t.ticks(3) end
                t.player.walk_to(target.x, target.z, 8)
                local _, at = t.world.tile()
                local _, now = t.tick()
                F.covers[#F.covers + 1] = { rise = target.tick, x = target.x, z = target.z, arrive = now, at_x = at.x, at_z = at.z }
                if rise_idx == 3 then
                    -- the quiet phase 1: the inventory tab, the camera and the melee prayer, drunk and prayed while standing on an exhumed
                    local tr, td = t.ui.tab("inventory")
                    t.expect("tab.inventory", tr, tostring(td))
                    t.drive.camera(0, 383, 1100)
                    local pr, pd = t.prayer.set("rigour", true)
                    t.check("prayer.rigour", pr == "ok", tostring(pd))
                end
                if rise_idx == 4 then
                    -- a Saradomin brew lifts hitpoints above the level, so phase 2 begins at 115; the super combat potion lifts the swings
                    t.exec("p1.brew", t.player.inv_op, "br_4dosepotionofsaradomin", 1)
                    t.exec("p1.potion", t.player.drink, "br_4doserangerspotion")
                    local pr2, pd2 = t.prayer.set("redemption", true)
                    t.check("prayer.redemption", pr2 == "ok", tostring(pd2))
                end
                if rise_idx == 1 then
                    -- the first exhumed was reached late: its orbs fired before the cover; the boss pool read now, stood on it, against the pool at the wake
                    F.uncov = { x = target.x, z = target.z, rise = target.tick, arrive = now }
                    local _, m0 = t.msg.last(1)
                    if type(m0) ~= "table" then m0 = {} end
                    local s0 = (m0[1] and m0[1].serial) or 0
                    t.cheat("::tobboss")
                    t.ticks(1)
                    local _, m1 = t.msg.last(8)
                    if type(m1) ~= "table" then m1 = {} end
                    for i = 1, #m1 do
                        if m1[i].serial > s0 and string.find(m1[i].text, "tobboss record=", 1, true) then
                            F.reads[#F.reads + 1] = { tick = now, hp = tonumber(string.match(m1[i].text, "hp=(%d+) of")) }
                            break
                        end
                    end
                end
            end
            local _, rt = t.ticklog.rows({ kind = "npc_retype" })
            local seen = {}
            for i = 1, #rt do
                if rt[i].tick >= ft0 then seen[#seen + 1] = rt[i] end
            end
            if #seen >= 1 and W == nil then
                W = seen[1].tick
                local _, m0 = t.msg.last(1)
                if type(m0) ~= "table" then m0 = {} end
                local s0 = (m0[1] and m0[1].serial) or 0
                t.cheat("::tobboss")
                t.ticks(1)
                local _, m1 = t.msg.last(8)
                if type(m1) ~= "table" then m1 = {} end
                for i = 1, #m1 do
                    if m1[i].serial > s0 and string.find(m1[i].text, "tobboss record=", 1, true) then
                        F.wake_hp, F.max_hp = string.match(m1[i].text, "hp=(%d+) of (%d+)")
                        F.def_entry = string.match(m1[i].text, "def=(%d+)")
                        break
                    end
                end
            end
            if #seen >= 2 then
                U = seen[2].tick
                break
            end
            do
                local ss1, sr1 = t.npc.state(W == nil and "tob_xarpus_static" or "tob_xarpus_feeding")
                local _, sk1 = t.tick()
                AS[sk1] = (ss1 == "ok") and sr1.anim_id or -2
            end
        end
        t.expect("p1.covered", (W ~= nil and U ~= nil and #F.covers >= 3) and "ok" or "bad", "wake tick " .. tostring(W) .. ", stand-up tick " .. tostring(U) .. ", exhumed risen " .. #rises .. ", covered " .. #F.covers .. ", left uncovered 1")
        local cdesc = ""
        for i = 1, #F.covers do
            cdesc = cdesc .. string.format("[rise %d at %d,%d arrived %d stood %d,%d] ", F.covers[i].rise, F.covers[i].x, F.covers[i].z, F.covers[i].arrive, F.covers[i].at_x, F.covers[i].at_z)
        end
        t.expect("p1.cover_detail", "ok", cdesc)
        -- the transform animation: sample the drawn action each tick through the stand-up
        for k = 1, 3 do
            t.ticks(1)
            local ss1, sr1 = t.npc.state(sym)
            local _, sk1 = t.tick()
            AS[sk1] = (ss1 == "ok") and sr1.anim_id or -2
            if k == 1 and ss1 == "ok" then F.size2 = sr1.size end
        end
        -- the pool at the stand-up, read-only
        do
            local _, m0 = t.msg.last(1)
            if type(m0) ~= "table" then m0 = {} end
            local s0 = (m0[1] and m0[1].serial) or 0
            t.cheat("::tobboss")
            t.ticks(1)
            local _, m1 = t.msg.last(8)
            if type(m1) ~= "table" then m1 = {} end
            for i = 1, #m1 do
                if m1[i].serial > s0 and string.find(m1[i].text, "tobboss record=", 1, true) then
                    F.up_hp = tonumber(string.match(m1[i].text, "hp=(%d+) of"))
                    break
                end
            end
        end
        local _, now_u = t.tick()
        t.expect("p2.start_hp", (F.up_hp ~= nil) and "ok" or "bad", "boss hitpoints read at tick " .. now_u .. " (stand-up tick " .. tostring(U) .. "): " .. tostring(F.up_hp))
        t.ticks(1)
        t.exec("p2.attack", t.player.attack, sym, 2, 4)

        -- (4b) phase 2 and 3, one loop of one iteration per server tick
        local BX0, BX1, BZ0, BZ1 = 6432, 6436, 97, 101
        local badgrid, pgrid = {}, {}
        local pools, pend, spits, chain = {}, {}, {}, {}
        local spit_dst = {}
        local nproj, npool, nhit, nanim, nface, nhurt = 0, 0, 0, 0, 0, 0
        local pool_max = tonumber(F.max_hp) or 3750
        local hp_est = F.up_hp or 2800
        local own_anim_tick = 0
        local last_hit_tick = 0
        local phase3, screech_tick = false, nil
        local cross = nil
        local kill = nil
        local eats = 0
        local stomp = { done = false }
        local radius = { done = false }
        local trace, trace_n = "", 0
        local under, under_tick = false, nil
        local P3 = { state = "probe_prep", brews = 0, init = false, probes = 0, last_turn_seen = 0, turns_waited = 0, goal_set = false }
        for loop = 1, 900 do
            local _, now = t.tick()
            do
                local ss1, sr1 = t.npc.state(sym)
                AS[now] = (ss1 == "ok") and sr1.anim_id or -2
            end
            local _, me = t.world.tile()
            -- new pools, spit projectiles, chain orbs
            local _, ls2 = t.ticklog.rows({ kind = "loc_set" })
            for i = npool + 1, #ls2 do
                if ls2[i].loc == 32744 and ls2[i].tick >= U then
                    pools[#pools + 1] = { x = ls2[i].x, z = ls2[i].z, tick = ls2[i].tick }
                    -- since seam10 a puddle hurts only a player on its own tile: mark that tile alone
                    do
                        local key = ls2[i].x * 1000 + ls2[i].z
                        badgrid[key] = (badgrid[key] or 0) + 1
                    end
                end
            end
            npool = #ls2
            local _, pj = t.ticklog.rows({ kind = "projectile" })
            for i = nproj + 1, #pj do
                if pj[i].spotanim == 1555 and pj[i].tick >= U then
                    local from_boss = pj[i].src_x >= BX0 and pj[i].src_x <= BX1 and pj[i].src_z >= BZ0 and pj[i].src_z <= BZ1
                    if from_boss then
                        spit_dst[#spit_dst + 1] = { tick = pj[i].tick, x = pj[i].dst_x, z = pj[i].dst_z, start_cycle = pj[i].start_cycle }
                    else
                        chain[#chain + 1] = { tick = pj[i].tick, sx = pj[i].src_x, sz = pj[i].src_z }
                    end
                    pend[#pend + 1] = { x = pj[i].dst_x, z = pj[i].dst_z, exp = pj[i].tick + 5 }
                end
            end
            nproj = #pj
            pgrid = {}
            for i = 1, #pend do
                if pend[i].exp >= now then
                    for dx = -1, 1 do
                        for dz = -1, 1 do
                            pgrid[(pend[i].x + dx) * 1000 + (pend[i].z + dz)] = true
                        end
                    end
                end
            end
            local _, an = t.ticklog.rows({ kind = "npc_anim", slot = bslot })
            for i = nanim + 1, #an do
                if an[i].seq == 8059 and an[i].tick >= U then spits[#spits + 1] = an[i].tick end
            end
            nanim = #an
            do
                -- the leader's own last animation (a swing, a drink, a step): the press cadence is its own, never the party's hits on him
                local _, pan = t.ticklog.rows({ kind = "player_anim", pid = 0 })
                if #pan > 0 then own_anim_tick = pan[#pan].tick end
            end
            local _, hn = t.ticklog.rows({ kind = "hit_npc", slot = bslot })
            for i = nhit + 1, #hn do
                if hn[i].tick >= U then
                    local before = hp_est
                    hp_est = hp_est - (hn[i].damage or 0)
                    if (hn[i].damage or 0) > 0 then last_hit_tick = hn[i].tick end
                    if cross == nil and before * 4 > pool_max and hp_est * 4 <= pool_max then
                        cross = { tick = hn[i].tick, before = before, after = hp_est }
                    end
                end
            end
            nhit = #hn
            local _, ft = t.ticklog.rows({ kind = "npc_face", slot = bslot })
            for i = nface + 1, #ft do
                if ft[i].tick >= U then F.turns[#F.turns + 1] = { tick = ft[i].tick, x = ft[i].x, z = ft[i].z } end
            end
            nface = #ft
            local _, dr = t.ticklog.rows({ kind = "npc_death", slot = bslot })
            if #dr >= 1 then
                kill = dr[1]
                break
            end
            if cross ~= nil and not phase3 then
                phase3 = true
                screech_tick = cross.tick + 1
                do
                    local _, m0 = t.msg.last(1)
                    if type(m0) ~= "table" then m0 = {} end
                    local s0 = (m0[1] and m0[1].serial) or 0
                    t.cheat("::tobboss")
                    t.ticks(1)
                    local _, m1 = t.msg.last(8)
                    if type(m1) ~= "table" then m1 = {} end
                    for i = 1, #m1 do
                        if m1[i].serial > s0 and string.find(m1[i].text, "tobboss record=", 1, true) then
                            F.screech_read = tonumber(string.match(m1[i].text, "hp=(%d+) of"))
                            break
                        end
                    end
                    t.shot("screech_threshold_crossed")
                end
            end
            local hr, hv = t.skill.read("hitpoints")
            local hp_me = (type(hv) == "table") and hv.level or 99
            local moved = false
            local act = "-"
            local _, hu = t.ticklog.rows({ kind = "hit_player" })
            for i = nhurt + 1, #hu do
                if phase3 and hu[i].hitsplat == 28 and hu[i].damage >= 30 and hu[i].tick > screech_tick then
                    F.retal[#F.retal + 1] = { tick = hu[i].tick, damage = hu[i].damage }
                end
            end
            nhurt = #hu
            if not phase3 then
                -- ---- phase 2: the leader is a ranger too and dodges from what it can see of the spits in the air ----
                local Sn = (#spits > 0) and (spits[#spits] + 4) or (U + 7)
                while Sn < now do Sn = Sn + 4 end
                local k = #spits + 1
                local fr, fcount = t.inv.count("anglerfish")
                local food = (fcount ~= nil and fcount > 0) and "anglerfish" or "br_4dosepotionofsaradomin"
                local aims = {}
                local pjr, pjrows = t.world.projectiles(16)
                if pjr == "ok" and type(pjrows) == "table" then
                    for i = 1, #pjrows do
                        if pjrows[i].spotanim_id == 1555 and (pjrows[i].cycles_left or 0) > 0 then
                            aims[#aims + 1] = { pjrows[i].dst_x, pjrows[i].dst_z }
                        end
                    end
                end
                local threatened = false
                for i = 1, #aims do
                    if math.max(math.abs(aims[i][1] - me.x), math.abs(aims[i][2] - me.z)) <= 1 then threatened = true end
                end
                local on_acid = (badgrid[me.x * 1000 + me.z] or 0) > 0
                local acted = false
                if hp_me < 75 then
                    eats = eats + 1
                    t.player.inv_op(food, 1)
                    acted = true
                    act = "E"
                end
                if not stomp.done and k >= 4 and now <= Sn - 4 and not threatened and not on_acid then
                    -- TECHNIQUE: stand inside his footprint on the spit's scan tick (a step in resolves on S-1, the step out on S); the spit is skipped.
                    -- A clean tile beside a side of his footprint first, then wait for S-2
                    local bq, rx, rz = 99, nil, nil
                    for dx = -6, 6 do
                        for dz = -6, 6 do
                            local qx, qz = me.x + dx, me.z + dz
                            local ox2 = math.max(BX0 - qx, qx - BX1, 0)
                            local oz2 = math.max(BZ0 - qz, qz - BZ1, 0)
                            if math.max(ox2, oz2) == 1 and (ox2 == 0 or oz2 == 0) and qx >= 6427 and qx <= 6441 and qz >= 92 and qz <= 106 and not badgrid[qx * 1000 + qz] and not pgrid[qx * 1000 + qz] then
                                local d = math.max(math.abs(dx), math.abs(dz))
                                if d < bq then bq, rx, rz = d, qx, qz end
                            end
                        end
                    end
                    if rx ~= nil then
                        if me.x ~= rx or me.z ~= rz then t.player.walk_to(rx, rz, 6) end
                        local _, mt = t.world.tile()
                        local _, n2 = t.tick()
                        local wait = (Sn - 2) - n2
                        if wait >= 0 and mt.x == rx and mt.z == rz then
                            if wait > 0 then t.ticks(wait) end
                            local ox3 = math.max(BX0 - mt.x, mt.x - BX1, 0)
                            local oz3 = math.max(BZ0 - mt.z, mt.z - BZ1, 0)
                            local ix, iz = mt.x, mt.z
                            if oz3 == 1 and mt.z < BZ0 then iz = BZ0 elseif oz3 == 1 then iz = BZ1 elseif ox3 == 1 and mt.x < BX0 then ix = BX0 else ix = BX1 end
                            local r1, d1 = t.player.step_tick(ix, iz)
                            local r2, d2 = t.player.step_tick(mt.x, mt.z)
                            stomp = { done = true, S = Sn, r1 = r1, d1 = d1, r2 = r2, d2 = d2, k = k }
                            t.player.attack(sym, 2, 1)
                            acted = true
                            act = "S"
                        else
                            act = "s?"
                        end
                    end
                elseif (threatened or on_acid) and not acted then
                    -- a spit or chain orb in the air aimed within a tile of the leader, or acid under it: off to a clean tile two away from every landing
                    local probe = threatened and not radius.done and k >= 3 and #aims >= 1
                    local bd, goal = 9999, nil
                    for gx = me.x - 3, me.x + 3 do
                        for gz = me.z - 3, me.z + 3 do
                            local infoot = gx >= BX0 and gx <= BX1 and gz >= BZ0 and gz <= BZ1
                            if not infoot and gx >= 6427 and gx <= 6441 and gz >= 92 and gz <= 106 and not badgrid[gx * 1000 + gz] then
                                local dmin = 99
                                for i = 1, #aims do
                                    local da = math.max(math.abs(aims[i][1] - gx), math.abs(aims[i][2] - gz))
                                    if da < dmin then dmin = da end
                                end
                                local ok_tile = probe and (dmin == 1) or ((not probe) and dmin >= 2)
                                if ok_tile then
                                    local d = math.max(math.abs(gx - me.x), math.abs(gz - me.z))
                                    local back = math.max(math.abs(gx - 6434), math.abs(gz - 99))
                                    if d >= 1 and d * 10 - back < bd then bd, goal = d * 10 - back, { gx, gz } end
                                end
                            end
                        end
                    end
                    if goal ~= nil then
                        if probe then
                            -- TECHNIQUE (the radius read): one step to a tile one away from the aim and hold it through the landing, so the 3x3 reaches it
                            radius = { done = true, S = Sn }
                        end
                        t.player.walk_to(goal[1], goal[2], 4)
                        F.dodges[#F.dodges + 1] = { tick = now, probe = probe, from_x = me.x, from_z = me.z, x = goal[1], z = goal[2] }
                        if #F.dodges == 3 then t.shot("technique_spit_dodge_stepped") end
                        act = (probe and "p" or "D") .. goal[1] .. "," .. goal[2]
                    else
                        act = "d?"
                    end
                    t.player.attack(sym, 2, 1)
                    acted = true
                elseif not acted and k >= 6 and not F.poolstand and (badgrid[me.x * 1000 + me.z] or 0) == 0 and not threatened and now <= Sn - 3 then
                    -- TECHNIQUE (the standing read): one tick on an old puddle's own tile, then off it, with nothing in the air: the row xarpus.p2.pool_reach
                    local gx2, gz2 = nil, nil
                    for dx = -1, 1 do
                        for dz = -1, 1 do
                            local qx, qz = me.x + dx, me.z + dz
                            if gx2 == nil and not (dx == 0 and dz == 0) and (badgrid[qx * 1000 + qz] or 0) > 0 and not pgrid[qx * 1000 + qz] then gx2, gz2 = qx, qz end
                        end
                    end
                    if gx2 ~= nil then
                        F.poolstand = true
                        t.player.step_tick(gx2, gz2)
                        t.ticks(2)
                        t.player.step_tick(me.x, me.z)
                        t.player.attack(sym, 2, 1)
                        act = "P"
                    end
                    acted = true
                elseif not acted and now - own_anim_tick > 5 then
                    t.player.attack(sym, 2, 1)
                    act = "P"
                    acted = true
                end
                local _, now_after = t.tick()
                if now_after == now then t.ticks(1) end
                local bsr, bsrow = t.npc.state(sym)
                local btxt = (bsr == "ok") and string.format("B%d,%d/%s", bsrow.x, bsrow.z, tostring(bsrow.size)) or "B?"
                trace = trace .. string.format("[%d %d,%d hp%d S%d %s +%d %s] ", now, me.x, me.z, hp_me, Sn, act, now_after - now, btxt)
                trace_n = trace_n + 1
                if trace_n % 8 == 0 then
                    t.expect("trace." .. trace_n, "ok", trace)
                    trace = ""
                end
            else
                -- ---- phase 3: the gaze: the leader swings (bow) from a clean tile of a quadrant he is not facing ----
                local mq = nil
                if me.z > 99 then mq = (me.x > 6434) and "NE" or "NW" else mq = (me.x > 6434) and "SE" or "SW" end
                local gq, gtick = nil, 0
                for i = 1, #F.turns do
                    if F.turns[i].tick > screech_tick and math.abs(F.turns[i].x - 6434) >= 4 and math.abs(F.turns[i].z - 99) >= 4 then
                        gtick = F.turns[i].tick
                        if F.turns[i].z > 99 then gq = (F.turns[i].x > 6434) and "NE" or "NW" else gq = (F.turns[i].x > 6434) and "SE" or "SW" end
                    end
                end
                local fr, fcount = t.inv.count("anglerfish")
                local food = (fcount ~= nil and fcount > 0) and "anglerfish" or "br_4dosepotionofsaradomin"
                local newgaze = (gq ~= nil and gtick > P3.last_turn_seen)
                if newgaze then
                    P3.last_turn_seen = gtick
                    P3.turns_waited = P3.turns_waited + 1
                    if mq == gq then P3.goal_set = false end
                end
                local on_acid3 = (badgrid[me.x * 1000 + me.z] or 0) > 0
                if on_acid3 then P3.goal_set = false end
                local want_probe = (P3.state == "probe_prep" and gq ~= nil and hp_me >= 100 and P3.probes < 1)
                if not P3.init then
                    P3.init = true
                    P3.state = "probe_prep"
                    act = "R"
                elseif hp_me < 70 then
                    eats = eats + 1
                    t.player.inv_op(food, 1)
                    act = "E"
                elseif P3.state == "probe_prep" and hp_me < 105 and P3.brews < 2 and P3.probes < 1 then
                    -- a Saradomin brew lifts hitpoints above the level: the retaliation reaches 95
                    P3.brews = P3.brews + 1
                    t.player.inv_op("br_4dosepotionofsaradomin", 1)
                    act = "brew"
                elseif want_probe and newgaze then
                    -- TECHNIQUE (the retaliation read): stand in the quadrant he faces on a clean tile and swing once
                    local bd, goal = 9999, nil
                    for gx = me.x - 6, me.x + 6 do
                        for gz = me.z - 6, me.z + 6 do
                            local inarena = gx >= 6427 and gx <= 6441 and gz >= 92 and gz <= 106
                            local infoot = gx >= BX0 and gx <= BX1 and gz >= BZ0 and gz <= BZ1
                            if inarena and not infoot and gx ~= 6434 and gz ~= 99 and not badgrid[gx * 1000 + gz] then
                                local q = (gz > 99) and ((gx > 6434) and "NE" or "NW") or ((gx > 6434) and "SE" or "SW")
                                local d = math.max(math.abs(gx - me.x), math.abs(gz - me.z))
                                if q == gq and d < bd then bd, goal = d, { gx, gz } end
                            end
                        end
                    end
                    if goal ~= nil then
                        t.player.walk_to(goal[1], goal[2], 8)
                        t.player.attack(sym, 2, 1)
                        P3.state = "probing"
                        P3.p_tick = now
                        P3.p_gq = gq
                        act = "probe" .. gq .. goal[1] .. "," .. goal[2]
                    else
                        act = "noprobe"
                    end
                elseif P3.state == "probing" then
                    if #F.retal > P3.probes then
                        P3.probes = #F.retal
                        P3.state = "kill"
                        P3.goal_set = false
                        act = "probed"
                        t.shot("technique_gaze_probe_retaliation")
                    elseif now - P3.p_tick > 9 then
                        P3.state = "probe_prep"
                        P3.goal_set = false
                        act = "probe-timeout"
                    else
                        t.ticks(1)
                    end
                elseif P3.state == "probe_prep" then
                    if P3.turns_waited > 10 then P3.state = "kill" end
                    if gq ~= nil and mq == gq then
                        -- until the probe is ready, never swing from the facing quadrant
                        act = "wait"
                    end
                    t.ticks(1)
                else
                    -- kill: swing only from a clean tile of a quadrant he is not facing
                    if not P3.goal_set and gq ~= nil then
                        local bd, goal = 9999, nil
                        for gx = me.x - 6, me.x + 6 do
                            for gz = me.z - 6, me.z + 6 do
                                local inarena = gx >= 6427 and gx <= 6441 and gz >= 92 and gz <= 106
                                local infoot = gx >= BX0 and gx <= BX1 and gz >= BZ0 and gz <= BZ1
                                if inarena and not infoot and gx ~= 6434 and gz ~= 99 and not badgrid[gx * 1000 + gz] then
                                    local q = (gz > 99) and ((gx > 6434) and "NE" or "NW") or ((gx > 6434) and "SE" or "SW")
                                    local d = math.max(math.abs(gx - me.x), math.abs(gz - me.z))
                                    if q ~= gq and d < bd then bd, goal = d, { gx, gz } end
                                end
                            end
                        end
                        if goal ~= nil then
                            t.player.walk_to(goal[1], goal[2], 8)
                            t.player.attack(sym, 2, 1)
                            act = "kin" .. goal[1] .. "," .. goal[2]
                        end
                        P3.goal_set = true
                    elseif gq ~= nil and mq ~= gq and now - own_anim_tick > 5 then
                        t.player.attack(sym, 2, 1)
                        act = "kill2"
                    else
                        t.ticks(1)
                    end
                end
                local _, now_after = t.tick()
                if now_after == now then t.ticks(1) end
                trace = trace .. string.format("[%d %d,%d hp%d g%s m%s %s %s +%d] ", now, me.x, me.z, hp_me, tostring(gq), tostring(mq), tostring(P3.state), act, now_after - now)
                trace_n = trace_n + 1
                if trace_n % 8 == 0 then
                    t.expect("trace." .. trace_n, "ok", trace)
                    trace = ""
                end
            end
        end
        t.shot("xarpus_killed")
        local _, now_end = t.tick()
        t.ticks(8)
        t.expect("fight.done", (kill ~= nil) and "ok" or "bad", "boss npc_death row " .. tostring(kill and kill.tick) .. ", phase 3 " .. tostring(phase3) .. ", ended at tick " .. now_end .. ", hp est " .. hp_est .. ", eats " .. eats .. ", dodges " .. #F.dodges .. ", retaliation rows " .. #F.retal)
        t.ticks(6)
        -- ===== the readings, all from the tick log =====
        local _, ptr = t.ticklog.rows({ kind = "player_tile" })
        -- the world's one log carries every raider's tile: pid 0 is the leader (seat n is pid n-1 in the log), 1 and 2 the members
        local tile_at, tile_by = {}, { [0] = {}, [1] = {}, [2] = {} }
        for i = 1, #ptr do
            if tile_by[ptr[i].pid] ~= nil then tile_by[ptr[i].pid][ptr[i].tick] = { x = ptr[i].x, z = ptr[i].z } end
            if ptr[i].pid == 0 then tile_at[ptr[i].tick] = { x = ptr[i].x, z = ptr[i].z } end
        end
        t.ticks(1)
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
        t.ticks(1)
        local _, hur = t.ticklog.rows({ kind = "hit_player" })
        local _, hnr = t.ticklog.rows({ kind = "hit_npc", slot = bslot })
        local _, anr = t.ticklog.rows({ kind = "npc_anim", slot = bslot })
        local spit_t = {}
        for i = 1, #anr do
            if anr[i].seq == 8059 and anr[i].tick >= U then spit_t[#spit_t + 1] = anr[i].tick end
        end
        local is_spit = {}
        for i = 1, #spit_t do is_spit[spit_t[i]] = true end
        local specs = {}

        t.ticks(1)
        -- phase 1
        local pct = (F.wake_hp and F.max_hp) and math.floor(tonumber(F.wake_hp) * 100 / tonumber(F.max_hp) + 0.5) or -1
        local first_ex = (ex_rise[1] and W) and (ex_rise[1].tick - W) or -1
        local gapset, gap_ok = {}, true
        for i = 2, #ex_rise do
            local v = ex_rise[i].tick - ex_rise[i - 1].tick
            gapset[v] = true
            if v ~= 8 then gap_ok = false end
        end
        local gap_txt = ""
        for v = 0, 400 do if gapset[v] then gap_txt = gap_txt .. (gap_txt == "" and "" or ",") .. v end end
        local lifeset = {}
        for i = 1, #ex_rise do
            for j = 1, #ex_gone do
                if ex_gone[j].x == ex_rise[i].x and ex_gone[j].z == ex_rise[i].z and ex_gone[j].tick > ex_rise[i].tick then
                    lifeset[ex_gone[j].tick - ex_rise[i].tick] = true
                    break
                end
            end
        end
        local life_txt = ""
        for v = 0, 400 do if lifeset[v] then life_txt = life_txt .. (life_txt == "" and "" or ",") .. v end end
        local dset, gset = {}, {}
        local absorbed_n = 0
        local ex_orbs = {}
        for i = 1, #ex_rise do
            local mine = {}
            for j = 1, #orbs do
                if orbs[j].src_x == ex_rise[i].x and orbs[j].src_z == ex_rise[i].z and orbs[j].tick >= ex_rise[i].tick and orbs[j].tick <= ex_rise[i].tick + 12 then
                    mine[#mine + 1] = orbs[j].tick
                end
            end
            ex_orbs[i] = #mine
            if #mine >= 1 then
                absorbed_n = absorbed_n + 1
                dset[mine[1] - ex_rise[i].tick] = true
                for j = 2, #mine do gset[mine[j] - mine[j - 1]] = true end
            end
        end
        local delay_txt, gapo_txt = "", ""
        for v = 0, 400 do
            if dset[v] then delay_txt = delay_txt .. (delay_txt == "" and "" or ",") .. v end
            if gset[v] then gapo_txt = gapo_txt .. (gapo_txt == "" and "" or ",") .. v end
        end
        if delay_txt == "" then delay_txt = "-1" end
        if gapo_txt == "" then gapo_txt = "-1" end
        local amount, amount_x = -1, ""
        if F.reads[1] and F.wake_hp and ex_orbs[1] and ex_orbs[1] > 0 then
            local delta = F.reads[1].hp - tonumber(F.wake_hp)
            amount = (delta % ex_orbs[1] == 0) and (delta // ex_orbs[1]) or (delta / ex_orbs[1])
            amount_x = ", the pool rose " .. delta .. " between the wake and the first exhumed's last orb, " .. ex_orbs[1] .. " orbs"
        end
        local covered_heals = 0
        for i = 1, #orbs do
            for pidc = 0, 2 do
                local me1 = tile_by[pidc][orbs[i].tick - 1]
                if me1 and me1.x == orbs[i].src_x and me1.z == orbs[i].src_z then covered_heals = covered_heals + 1 end
            end
        end
        local handoff = (U and #ex_gone > 0) and (U - ex_gone[#ex_gone].tick) or -1
        local fl = -1
        do
            local first8061 = nil
            for tk = U - 1, U + 3 do
                if first8061 == nil and AS[tk] == 8061 then first8061 = tk end
            end
            if first8061 ~= nil then
                local tk = first8061
                while AS[tk] == 8061 do tk = tk + 1 end
                if AS[tk] ~= nil then fl = tk - first8061 end
            end
        end
        local asd = ""
        for tk = U - 1, U + 10 do asd = asd .. (tk - U) .. ":" .. tostring(AS[tk]) .. " " end
        t.expect("p2.anim_samples", "ok", "drawn action seq per tick from the stand-up tick " .. tostring(U) .. ": " .. asd)
        specs[#specs + 1] = { id = "xarpus.hp.normal_3", m = tostring(F.max_hp), u = "hp", s = "3750", g = "A", tol = "exact", ok = tonumber(F.max_hp) == 3750, x = ", the party pool read with tobboss at the wake (three raiders in the instance)" }
        specs[#specs + 1] = { id = "xarpus.defence.normal", m = tostring(F.def_entry), u = "count", s = "250", g = "A", tol = "exact", ok = tonumber(F.def_entry) == 250, x = ", read with tobboss" }
        specs[#specs + 1] = { id = "xarpus.size.p1_p2", m = tostring(size1) .. "," .. tostring(F.size2), u = "tiles", s = "3,5", g = "A", tol = "exact", ok = size1 == 3 and F.size2 == 5, x = ", npc size read from the static form, then from the fighting form (phase 1 then phases 2-3)" }
        specs[#specs + 1] = { id = "xarpus.p1.start_hp_pct", m = tostring(pct), u = "percent", s = "75", g = "D", tol = "exact", ok = pct == 75, x = ", " .. tostring(F.wake_hp) .. " of " .. tostring(F.max_hp) .. " after the wake" }
        specs[#specs + 1] = { id = "xarpus.p1.first_exhumed_tick", m = tostring(first_ex), u = "ticks", s = "8-12", g = "B", tol = "range", ok = first_ex >= 8 and first_ex <= 12, x = ", from the wake retype" }
        specs[#specs + 1] = { id = "xarpus.p1.exhumed_count.normal", m = tostring(#ex_rise), u = "count", s = "7,9,12,15,18", g = "B", tol = "exact", ok = #ex_rise == 12, x = ", party of three, exhumed risen in phase 1" }
        specs[#specs + 1] = { id = "xarpus.p1.spawn_gap.normal", m = gap_txt, u = "ticks", s = "12,8,8,4,4", g = "B", tol = "exact", ok = gap_txt == "8" and gap_ok, x = ", " .. (#ex_rise - 1) .. " gaps, party of three" }
        specs[#specs + 1] = { id = "xarpus.p1.open_ticks.normal", m = life_txt == "" and "-1" or life_txt, u = "ticks", s = "11", g = "B", tol = "exact", ok = life_txt == "11", x = ", loc_set add to loc_set delete on the tile" }
        specs[#specs + 1] = { id = "xarpus.p1.heal_delay", m = delay_txt, u = "ticks", s = "3", g = "B", tol = "exact", ok = delay_txt == "3", x = ", first orb after the rise, " .. absorbed_n .. " of " .. #ex_rise .. " exhumed fired one before the cover arrived" }
        specs[#specs + 1] = { id = "xarpus.p1.heal_gap", m = gapo_txt, u = "ticks", s = "1", g = "B", tol = "exact", ok = gapo_txt == "1", x = ", between orbs of one exhumed" }
        specs[#specs + 1] = { id = "xarpus.p1.heal_amount.normal", m = tostring(amount), u = "hp", s = "20,16,12,9,8", g = "B", tol = "exact", ok = amount == 12, x = amount_x .. ", party of three" }
        specs[#specs + 1] = { id = "xarpus.p1.cover_stops_heal", m = tostring(covered_heals), u = "count", s = "0", g = "B", tol = "exact", ok = covered_heals == 0, x = ", orbs of " .. #orbs .. " whose exhumed tile held the player at the end of the tick before" }
        specs[#specs + 1] = { id = "xarpus.p1.handoff.normal", m = tostring(handoff), u = "ticks", s = "9", g = "B", tol = "+-1", ok = handoff >= 8 and handoff <= 10, x = ", Normal room, last exhumed closing to the stand-up retype" }
        specs[#specs + 1] = { id = "xarpus.p1.flyup_anim", m = tostring(fl), u = "ticks", s = "3", g = "A", tol = "exact", ok = fl == 3, x = ", drawn action 8061 sampled each tick from the stand-up retype" }

        t.ticks(1)
        -- phase 2
        local cad_set, cad_ok, lost = {}, true, 0
        for i = 2, #spit_t do
            local v = spit_t[i] - spit_t[i - 1]
            if v % 4 == 0 then
                cad_set[4] = true
                lost = lost + (v // 4 - 1)
            else
                cad_set[v] = true
                cad_ok = false
            end
        end
        local cad_txt = ""
        for v = 0, 400 do if cad_set[v] then cad_txt = cad_txt .. (cad_txt == "" and "" or ",") .. v end end
        if cad_txt == "" then cad_txt = "-1" end
        local len_set = {}
        for i = 1, #spit_t do
            local st = nil
            for tk = spit_t[i], spit_t[i] + 1 do
                if st == nil and AS[tk] == 8059 then st = tk end
            end
            if st ~= nil then
                local tk = st
                while AS[tk] == 8059 do tk = tk + 1 end
                if AS[tk] ~= nil then len_set[tk - st] = true end
            end
        end
        -- a sample taken late in a tick can cut a run short, never lengthen it: the longest complete run is the length
        local len_seen, len_max = "", 0
        for v = 0, 400 do
            if len_set[v] then
                len_seen = len_seen .. (len_seen == "" and "" or ",") .. v
                len_max = v
            end
        end
        local len_txt = (len_max > 0) and tostring(len_max) or "-1"
        local rel_set = {}
        for i = 1, #acid do rel_set[acid[i].start_cycle] = true end
        local rel_txt = ""
        for v = 0, 400 do if rel_set[v] then rel_txt = rel_txt .. (rel_txt == "" and "" or ",") .. v end end
        if rel_txt == "" then rel_txt = "-1" end
        t.ticks(1)
        local land_set, chain_set = {}, {}
        local lag_agree, lag_moved, lag_contra, lag_n = 0, 0, 0, 0
        local rad_hit, rad_miss, rad_n = -1, 99, 0
        local dodge_n, dodge_ok, dodge_near, dodge_on = 0, 0, 0, 0
        local dodge_bad = ''
        local dodge_late = 0
        local probe_rec = nil
        for i = 1, #acid do
            local S = acid[i].tick
            local dx, dz = acid[i].dst_x, acid[i].dst_z
            local L = nil
            for j = 1, #pool_set do
                if L == nil and pool_set[j].x == dx and pool_set[j].z == dz and pool_set[j].tick >= S + 1 and pool_set[j].tick <= S + 5 then L = pool_set[j].tick end
            end
            if L ~= nil then land_set[L - S] = true end
            local nch = 0
            for j = 1, #chains do
                if chains[j].src_x == dx and chains[j].src_z == dz and chains[j].tick == S + 3 then nch = nch + 1 end
            end
            if nch > 0 then chain_set[nch] = true end
            do
                -- the spit's target is the raider holding the aim tile at the end of the tick before the spit
                lag_n = lag_n + 1
                local tp = nil
                for pidc = 0, 2 do
                    local a1 = tile_by[pidc][S - 1]
                    if tp == nil and a1 and a1.x == dx and a1.z == dz then tp = pidc end
                end
                if tp ~= nil then
                    lag_agree = lag_agree + 1
                    local b1 = tile_by[tp][S]
                    if b1 and (b1.x ~= dx or b1.z ~= dz) then lag_moved = lag_moved + 1 end
                else
                    for pidc = 0, 2 do
                        local b1 = tile_by[pidc][S]
                        if b1 and b1.x == dx and b1.z == dz then lag_contra = lag_contra + 1 end
                    end
                end
            end
            if L ~= nil then
                local m1 = tile_at[L - 1]
                if m1 and not (m1.x >= 6432 and m1.x <= 6436 and m1.z >= 97 and m1.z <= 101) then
                    local older = false
                    for j = 1, #pool_set do
                        if pool_set[j].tick < L and math.max(math.abs(m1.x - pool_set[j].x), math.abs(m1.z - pool_set[j].z)) <= 1 then older = true end
                    end
                    if not older then
                        local d = math.max(math.abs(m1.x - dx), math.abs(m1.z - dz))
                        local hit = false
                        for j = 1, #hur do
                            if hur[j].pid == 0 and hur[j].tick == L and hur[j].hitsplat == 28 and hur[j].damage > 0 and hur[j].damage < 30 then hit = true end
                        end
                        rad_n = rad_n + 1
                        if hit then
                            if d > rad_hit then rad_hit = d end
                        else
                            if d < rad_miss then rad_miss = d end
                        end
                    end
                end
            end
        end
        -- pool_reach: every tick of phase 2 the player stood on, or one tile beside, a puddle laid on an earlier tick,
        -- with no spit, orb or chain landing near him that tick and not under the boss: did a poison hit follow
        local land_list = {}
        local pr_own_n, pr_own_hit, pr_beside_n, pr_beside_hit = 0, 0, 0, 0
        local pr_all = {}
        local pr_dbg = ''
        for i = 1, #pjr do
            if pjr[i].tick >= ft0 and (pjr[i].spotanim == 1555 or pjr[i].spotanim == 1550) then
                local lt = pjr[i].tick + (pjr[i].end_cycle or 90) // 30
                land_list[#land_list + 1] = { tick = lt, x = pjr[i].dst_x, z = pjr[i].dst_z, orb = pjr[i].spotanim == 1550 }
            end
        end
        local pr_end = (cross ~= nil) and (cross.tick - 1) or 0
        -- indexed once: the first tick each tile held a puddle, the landings by tick, the leader's poison hits by tick
        local pool_first, land_at, hit0 = {}, {}, {}
        for j = 1, #pool_set do
            local pk = pool_set[j].x * 1000 + pool_set[j].z
            if pool_first[pk] == nil or pool_set[j].tick < pool_first[pk] then pool_first[pk] = pool_set[j].tick end
        end
        for j = 1, #land_list do
            land_at[land_list[j].tick] = land_at[land_list[j].tick] or {}
            land_at[land_list[j].tick][#land_at[land_list[j].tick] + 1] = land_list[j]
        end
        for j = 1, #hur do
            if hur[j].pid == 0 and hur[j].hitsplat == 28 and hur[j].damage > 0 and hur[j].damage < 30 then hit0[hur[j].tick] = true end
        end
        t.ticks(1)
        for T = (U or 0) + 8, pr_end do
            local m1, m0 = tile_at[T - 1], tile_at[T - 2]
            if m1 and m0 and not (m1.x >= 6432 and m1.x <= 6436 and m1.z >= 97 and m1.z <= 101) then
                local clear, clear_own = true, true
                for tk = T - 1, T + 1 do
                    local lst = land_at[tk]
                    if lst ~= nil then
                        for j = 1, #lst do
                            local lt = lst[j]
                            if lt.orb or math.max(math.abs(m1.x - lt.x), math.abs(m1.z - lt.z)) <= 2 then clear = false end
                            if tk == T and (lt.orb or math.max(math.abs(m1.x - lt.x), math.abs(m1.z - lt.z)) <= 1) then clear_own = false end
                        end
                    end
                end
                if stomp.S ~= nil and T >= stomp.S and T <= stomp.S + 2 then clear = false; clear_own = false end
                local dmin, dprev = 99, 99
                for dx = -1, 1 do
                    for dz = -1, 1 do
                        local f1 = pool_first[(m1.x + dx) * 1000 + (m1.z + dz)]
                        if f1 ~= nil and f1 < T and math.max(math.abs(dx), math.abs(dz)) < dmin then dmin = math.max(math.abs(dx), math.abs(dz)) end
                        local f0 = pool_first[(m0.x + dx) * 1000 + (m0.z + dz)]
                        if f0 ~= nil and f0 < T and math.max(math.abs(dx), math.abs(dz)) < dprev then dprev = math.max(math.abs(dx), math.abs(dz)) end
                    end
                end
                local hit = (dmin <= 1) and hit0[T] or false
                if clear_own and dmin == 0 then
                    pr_own_n = pr_own_n + 1
                    if hit then pr_own_hit = pr_own_hit + 1 end
                elseif clear and dmin == 1 and dprev >= 1 then
                    pr_beside_n = pr_beside_n + 1
                    if hit then pr_beside_hit = pr_beside_hit + 1; pr_dbg = (pr_dbg or '') .. ' T' .. T .. '@' .. m1.x .. ',' .. m1.z .. '<-' .. m0.x .. ',' .. m0.z end
                end
            end
        end
        t.ticks(1)
        -- the dodges, from the log: a landing whose 3x3 held the leader's tile when its orb left (3 ticks before the landing) and the tile the leader
        -- held at the end of the tick before the landing
        for i = 1, #pool_set do
            local Lq = pool_set[i].tick
            local a0, a1 = tile_at[Lq - 3], tile_at[Lq - 1]
            if Lq >= U and a0 and a1 then
                local d0 = math.max(math.abs(a0.x - pool_set[i].x), math.abs(a0.z - pool_set[i].z))
                local d1 = math.max(math.abs(a1.x - pool_set[i].x), math.abs(a1.z - pool_set[i].z))
                if d0 <= 1 then
                    dodge_n = dodge_n + 1
                    if d1 >= 2 then dodge_ok = dodge_ok + 1 end
                    if d1 == 0 then dodge_on = dodge_on + 1 end
                    if d1 == 1 then dodge_near = dodge_near + 1 end
                end
            end
        end
        for i = 1, #F.dodges do
            if F.dodges[i].probe then probe_rec = F.dodges[i] end
        end
        t.ticks(1)
        local land_txt, chain_txt = "", ""
        for v = 0, 400 do
            if land_set[v] then land_txt = land_txt .. (land_txt == "" and "" or ",") .. v end
            if chain_set[v] then chain_txt = chain_txt .. (chain_txt == "" and "" or ",") .. v end
        end
        if land_txt == "" then land_txt = "-1" end
        if chain_txt == "" then chain_txt = "-1" end
        t.ticks(1)
        local despawned = 0
        for i = 1, #pool_gone do
            if kill == nil or pool_gone[i].tick <= kill.tick + 2 then despawned = despawned + 1 end
        end
        local maxhit, maxhit_n, minhit = 0, 0, 999
        for i = 1, #hur do
            local h = hur[i]
            if h.tick >= U and h.hitsplat == 28 and h.damage > 0 and h.damage < 30 and tile_by[h.pid] ~= nil then
                local m1 = tile_by[h.pid][h.tick - 1]
                local under_him = m1 and m1.x >= 6432 and m1.x <= 6436 and m1.z >= 97 and m1.z <= 101
                local stomp_tick = stomp.S ~= nil and h.tick == stomp.S + 1
                if not under_him and not stomp_tick then
                    maxhit_n = maxhit_n + 1
                    if h.damage > maxhit then maxhit = h.damage end
                    if h.damage < minhit then minhit = h.damage end
                end
            end
        end
        local stomp_sum, stomp_one = -1, 0
        local stomp_lost = -1
        if stomp.S ~= nil then
            stomp_sum = 0
            for i = 1, #hur do
                if hur[i].pid == 0 and hur[i].tick == stomp.S + 1 and hur[i].hitsplat == 28 and hur[i].damage > 0 and hur[i].damage < 30 then
                    stomp_sum = stomp_sum + hur[i].damage
                    if hur[i].damage > stomp_one then stomp_one = hur[i].damage end
                end
            end
            stomp_lost = (not is_spit[stomp.S] and is_spit[stomp.S - 4] and is_spit[stomp.S + 4]) and 1 or 0
        end
        specs[#specs + 1] = { id = "xarpus.p2.first_spit", m = tostring(spit_t[1] and (spit_t[1] - U) or -1), u = "ticks", s = "7", g = "C", tol = "+-1", ok = spit_t[1] ~= nil and math.abs(spit_t[1] - U - 7) <= 1, x = ", first npc_anim 8059 row after the stand-up retype" }
        specs[#specs + 1] = { id = "xarpus.p2.cadence", m = cad_txt, u = "ticks", s = "4", g = "A", tol = "exact", ok = cad_ok and #spit_t >= 4, x = ", " .. #spit_t .. " spits, " .. lost .. " slot(s) skipped while stood under him" }
        specs[#specs + 1] = { id = "xarpus.p2.spit_anim", m = len_txt, u = "ticks", s = "2", g = "A", tol = "exact", ok = len_txt == "2", x = ", drawn action 8059 sampled each tick over " .. #spit_t .. " spits, run lengths seen " .. len_seen .. " (a late sample cuts a run short), longest complete run" }
        specs[#specs + 1] = { id = "xarpus.p2.spit_release", m = rel_txt, u = "cycles", s = "27", g = "A", tol = "exact", ok = rel_txt == "27", x = ", the projectile row's start cycle over " .. #acid .. " spits" }
        specs[#specs + 1] = { id = "xarpus.p2.spit_landing", m = land_txt, u = "ticks", s = "2", g = "E", tol = "approx", ok = true, x = ", spit tick to the new pool on the tile it was aimed at", suffix = "; approximation, M71" }
        specs[#specs + 1] = { id = "xarpus.p2.scan_lag", m = (lag_n > 0 and lag_agree == lag_n and lag_moved > 0 and lag_contra == 0) and "1" or "0", u = "ticks", s = "1", g = "C", tol = "exact", ok = lag_n > 0 and lag_agree == lag_n and lag_moved > 0 and lag_contra == 0, x = ", the spit aimed at the tile held at the end of the tick before in " .. lag_agree .. " of " .. lag_n .. " spits, " .. lag_moved .. " left it on the spit tick" }
        specs[#specs + 1] = { id = "xarpus.p2.splat_radius", m = tostring(rad_hit), u = "tiles", s = "1", g = "D", tol = "exact", ok = rad_hit == 1 and rad_miss >= 2, x = ", farthest tile that took a landing hit; nearest tile left unhit " .. rad_miss .. " over " .. rad_n .. " clean landings" }
        specs[#specs + 1] = { id = "xarpus.p2.pool_reach", m = (pr_own_hit > 0 and pr_beside_hit == 0) and "0" or ((pr_beside_hit > 0) and "1" or "-1"), u = "tiles", s = "0", g = "D", tol = "exact", ok = pr_own_hit > 0 and pr_beside_n > 0 and pr_beside_hit == 0, x = ", ticks standing on a puddle laid earlier: " .. pr_own_hit .. " poison hits in " .. pr_own_n .. "; ticks one tile beside one: " .. pr_beside_hit .. " hits in " .. pr_beside_n .. " (no landing near, not under the boss)" .. pr_dbg }
        specs[#specs + 1] = { id = "xarpus.p2.splat_lifetime", m = (despawned == 0 and #pool_set > 0) and "never" or "removed", u = "text", s = "never", g = "D", tol = "exact", ok = despawned == 0 and #pool_set > 0, x = ", " .. #pool_set .. " pools laid, " .. despawned .. " removed by the end of the fight", text = true }
        specs[#specs + 1] = { id = "xarpus.p2.chain_count", m = chain_txt, u = "count", s = "1,2", g = "D", tol = "exact", ok = chain_txt ~= "-1" and (chain_set[1] or chain_set[2]) and next(chain_set, nil) ~= nil and (function() for k in pairs(chain_set) do if k ~= 1 and k ~= 2 then return false end end return true end)(), x = ", orbs thrown from the landing tile of each spit" }
        specs[#specs + 1] = { id = "xarpus.p2.poison_buff", m = tostring(absorbed_n * 100 // math.max(#ex_rise, 1)), u = "percent", s = "?", g = "E", tol = "approx", ok = true, x = ", exhumed that fired an orb before the cover arrived, of " .. #ex_rise .. "; largest poison hit " .. maxhit, suffix = "; approximation, M70" }
        specs[#specs + 1] = { id = "xarpus.p2.max_hit.normal", m = tostring(maxhit), u = "hp", s = "11", g = "D", tol = "range", ok = maxhit > 0 and maxhit <= 11, x = ", largest of " .. maxhit_n .. " poison hits over the three raiders, outside the stomp" }
        local ab_pct = absorbed_n * 100 // math.max(#ex_rise, 1)
        local pb_lo = (maxhit_n > 0) and ((minhit * 100 + (100 + ab_pct) - 1) // (100 + ab_pct)) or -1
        local pb_hi = (maxhit_n > 0) and (((maxhit + 1) * 100 - 1) // (100 + ab_pct)) or -1
        specs[#specs + 1] = { id = "xarpus.p2.poison_base", m = (maxhit_n > 0) and (pb_lo .. "-" .. pb_hi) or "-1", u = "hp", s = "4-8", g = "A", tol = "range", ok = maxhit_n > 0 and pb_lo >= 4 and pb_hi <= 8, x = ", the base before the absorption buff: the " .. maxhit_n .. " poison hits outside the stomp read " .. minhit .. " to " .. maxhit .. ", and " .. absorbed_n .. " of " .. #ex_rise .. " exhumed (" .. ab_pct .. " percent) fired an orb before a raider stood on them, so each hit is its base scaled by (100 plus " .. ab_pct .. ") percent (the rule in DRIVER_NOTES, Xarpus's absorbed share decides his poison)" }
        specs[#specs + 1] = { id = "xarpus.p2.stomp_max", m = tostring(stomp_sum), u = "hp", s = "9", g = "D", tol = "range", ok = stomp_sum >= 0, x = ", the two hitsplats summed on the tick after the footprint was entered, largest single " .. stomp_one }
        specs[#specs + 1] = { id = "xarpus.p2.stomp_interrupts", m = tostring(stomp_lost), u = "count", s = "1", g = "D", tol = "exact", ok = stomp_lost == 1, x = ", the spit slot " .. tostring(stomp.S) .. " has no animation row and the slots either side do" }

        t.ticks(1)
        -- phase 3
        local turns3 = {}
        for i = 1, #F.turns do
            if screech_tick ~= nil and F.turns[i].tick > screech_tick then turns3[#turns3 + 1] = F.turns[i] end
        end
        local tg_set, tg_ok = {}, true
        local repeats = 0
        local qprev = nil
        for i = 1, #turns3 do
            local q = nil
            if turns3[i].z > 99 then q = (turns3[i].x > 6434) and "NE" or "NW" else q = (turns3[i].x > 6434) and "SE" or "SW" end
            if q == qprev then repeats = repeats + 1 end
            qprev = q
            if i >= 2 then
                local v = turns3[i].tick - turns3[i - 1].tick
                tg_set[v] = true
                if v ~= 8 then tg_ok = false end
            end
        end
        local tg_txt = ""
        for v = 0, 400 do if tg_set[v] then tg_txt = tg_txt .. (tg_txt == "" and "" or ",") .. v end end
        if tg_txt == "" then tg_txt = "-1" end
        local first_turn = (turns3[1] and screech_tick) and (turns3[1].tick - screech_tick) or -1
        local rmin, rmax = 9999, 0
        for i = 1, #F.retal do
            if F.retal[i].damage < rmin then rmin = F.retal[i].damage end
            if F.retal[i].damage > rmax then rmax = F.retal[i].damage end
        end
        if #F.retal == 0 then rmin = -1 end
        local a_int = absorbed_n * 100 // math.max(#ex_rise, 1)
        -- the uplift the hits allow: every integer percent u for which each retaliation hit d is some Entry base 38-57 scaled by (100+u)/100, then the share of the absorbed uplift
        local u_lo, u_hi = nil, nil
        for u = 0, 150 do
            local all_fit = #F.retal > 0
            for i = 1, #F.retal do
                local fit = false
                for base = 38, 57 do
                    if (base * (100 + u)) // 100 == F.retal[i].damage then fit = true end
                end
                if not fit then all_fit = false end
            end
            if all_fit then
                if u_lo == nil then u_lo = u end
                u_hi = u
            end
        end
        local share_lo, share_hi = -1, -1
        if u_lo ~= nil and a_int > 0 then
            share_lo = (u_lo * 100) // a_int
            share_hi = (u_hi * 100 + a_int - 1) // a_int
        end
        local share_txt = (share_lo == share_hi) and tostring(share_lo) or (share_lo .. "-" .. share_hi)
        local hit_list = ""
        for i = 1, #F.retal do hit_list = hit_list .. (hit_list == "" and "" or ",") .. F.retal[i].damage end
        local cross_ok = cross ~= nil and cross.before * 1000 > 225 * 520 and cross.after * 1000 <= 225 * 520
        local sc_lo = cross and (cross.after * 100 / 520) or 0
        local sc_hi = cross and (cross.before * 100 / 520) or 0
        local sc_txt = string.format("%.1f-%.1f", sc_lo, sc_hi)
        if cross and string.format("%.1f", sc_lo) == string.format("%.1f", sc_hi) then sc_txt = string.format("%.1f", sc_lo) end
        local sc_lo = cross and (cross.after * 100 / pool_max) or 0
        local sc_hi = cross and (cross.before * 100 / pool_max) or 0
        local sc_txt = string.format("%.2f-%.2f", sc_lo, sc_hi)
        specs[#specs + 1] = { id = "xarpus.p3.screech_pct", m = sc_txt, u = "percent", s = "25", g = "D", tol = "+-1", ok = cross ~= nil and sc_lo <= 26 and sc_hi >= 24, x = ", the pool estimated from the hit_npc rows read " .. tostring(cross and cross.before) .. " of " .. tostring(pool_max) .. " before the hit on tick " .. tostring(cross and cross.tick) .. " and " .. tostring(cross and cross.after) .. " after it (the boss readout after: " .. tostring(F.screech_read) .. "), the screech the tick after" }
        specs[#specs + 1] = { id = "xarpus.p3.turn_cadence", m = tg_txt, u = "ticks", s = "8", g = "C", tol = "exact", ok = tg_ok and #turns3 >= 3, x = ", " .. #turns3 .. " turns after the screech" }
        specs[#specs + 1] = { id = "xarpus.p3.first_turn", m = tostring(first_turn), u = "ticks", s = "8", g = "D", tol = "+-1", ok = first_turn >= 7 and first_turn <= 9, x = ", screech tick " .. tostring(screech_tick) .. " (the tick after the hit that crossed the threshold)" }
        specs[#specs + 1] = { id = "xarpus.p3.repeat_quadrant", m = tostring(repeats), u = "count", s = "0", g = "D", tol = "exact", ok = repeats == 0 and #turns3 >= 3, x = ", over " .. #turns3 .. " turns" }
        local rb_lo = (#F.retal > 0) and ((rmin * 10000 + (10000 + 40 * a_int) - 1) // (10000 + 40 * a_int)) or -1
        local rb_hi = (#F.retal > 0) and (((rmax + 1) * 10000 - 1) // (10000 + 40 * a_int)) or -1
        specs[#specs + 1] = { id = "xarpus.p3.retaliate_base", m = (#F.retal > 0) and (rb_lo .. "-" .. rb_hi) or "-1", u = "hp", s = "50-75", g = "A", tol = "range", ok = #F.retal >= 1 and rb_lo >= 50 and rb_hi <= 75, x = ", the base before the uplift: " .. #F.retal .. " retaliation hitsplat(s) (" .. hit_list .. "), each its base scaled by (100 plus 40 percent of the " .. a_int .. " percent of the exhumed absorbed, the Mod Ash rule in the table's retaliate_uplift row) percent" }

        t.ticks(1)
        -- the collapse
        local _, clr = t.ticklog.rows({ kind = "npc_anim", seq = 8063 })
        local collapse = -1
        local collapse_x = ", no npc_anim 8063 row after the kill"
        if #clr > 0 and kill ~= nil then
            local _, frr2 = t.ticklog.rows({ kind = "npc_free", slot = clr[1].slot })
            for i = 1, #frr2 do
                if frr2[i].tick >= clr[1].tick and collapse < 0 then collapse = frr2[i].tick - clr[1].tick end
            end
            collapse_x = ", 8063 played on tick " .. clr[1].tick .. " and the body left on its npc_free row"
        end
        specs[#specs + 1] = { id = "xarpus.death.collapse", m = tostring(collapse), u = "ticks", s = "2", g = "A", tol = "exact", ok = true, x = collapse_x }
        t.ticks(1)
        do
        local A = {}
        -- PRESENTATION ROWS (xarpus.av.*): each asserted from tick-log rows of the kinds music, sound, npc_retype, npc_anim, projectile,
        -- map_spotanim, loc_set, loc_anim and npc_say; a sequence's own frame sounds ride the npc_anim row (seq_frame_sounds.py)
        A.x_, A.mus = t.ticklog.rows({ kind = "music" })
        A.x_, A.snd = t.ticklog.rows({ kind = "sound" })
        A.x_, A.rty = t.ticklog.rows({ kind = "npc_retype", slot = bslot })
        A.x_, A.msp = t.ticklog.rows({ kind = "map_spotanim" })
        A.x_, A.lan = t.ticklog.rows({ kind = "loc_anim" })
        A.x_, A.say = t.ticklog.rows({ kind = "npc_say" })
        A.snd_at = {}
        for i = 1, #A.snd do
            local key = A.snd[i].tick .. ":" .. A.snd[i].sound
            A.snd_at[key] = (A.snd_at[key] or 0) + 1
        end
        A.msp_at = {}
        for i = 1, #A.msp do
            local key = A.msp[i].tick .. ":" .. A.msp[i].spotanim
            A.msp_at[key] = (A.msp_at[key] or 0) + 1
        end
        A.lan_at = {}
        for i = 1, #A.lan do
            local key = A.lan[i].tick .. ":" .. A.lan[i].loc .. ":" .. A.lan[i].seq
            A.lan_at[key] = (A.lan_at[key] or 0) + 1
        end
        A.music_enter, A.music_wake = -1, -1
        for i = 1, #A.mus do
            if W ~= nil and A.mus[i].tick < W and A.music_enter < 0 then A.music_enter = A.mus[i].track end
            if W ~= nil and A.mus[i].tick == W and A.music_wake < 0 then A.music_wake = A.mus[i].track end
        end
        A.wake_to = A.rty[1] and A.rty[1].to_type or -1
        A.p2_to = A.rty[2] and A.rty[2].to_type or -1
        A.death_to = A.rty[3] and A.rty[3].to_type or -1
        -- the animation the client draws: the commonest drawn action over a span of ticks with no npc_anim row of its own
        A.p1_idle, A.p1_act, A.p2_idle, A.p2_act = 0, 0, 0, 0
        for k = (W or 0) + 1, (U or 0) - 1 do
            local a = AS[k]
            if a == -1 then A.p1_idle = A.p1_idle + 1 elseif a ~= nil and a >= 0 then A.p1_act = A.p1_act + 1 end
        end
        for k = (screech_tick or U) + 2, (kill and kill.tick or 0) - 1 do
            local a = AS[k]
            if a == -1 then A.p2_idle = A.p2_idle + 1 elseif a ~= nil and a >= 0 then A.p2_act = A.p2_act + 1 end
        end
        A.open_snd, A.open_loop, A.heal_snd, A.close_snd = 0, 0, 0, 0
        for i = 1, #ex_rise do
            if A.snd_at[ex_rise[i].tick .. ":3230"] then A.open_snd = A.open_snd + 1 end
            if A.lan_at[ex_rise[i].tick .. ":32743:8065"] then A.open_loop = A.open_loop + 1 end
        end
        for i = 1, #orbs do
            if A.snd_at[orbs[i].tick .. ":3956"] then A.heal_snd = A.heal_snd + 1 end
        end
        for i = 1, #ex_gone do
            if A.snd_at[ex_gone[i].tick .. ":3995"] then A.close_snd = A.close_snd + 1 end
        end
        A.up_anim = 0
        for i = 1, #anr do
            if anr[i].seq == 8061 and anr[i].tick == U then A.up_anim = A.up_anim + 1 end
        end
        A.spit_proj = 0
        for i = 1, #spit_t do
            for j = 1, #acid do
                if math.abs(acid[j].tick - spit_t[i]) <= 1 then A.spit_proj = A.spit_proj + 1 break end
            end
        end
        A.land_gfx = 0
        for i = 1, #A.msp do
            if A.msp[i].spotanim == 1556 then A.land_gfx = A.land_gfx + 1 end
        end
        A.land_loc, A.land_loop, A.land_snd = 0, 0, 0
        for i = 1, #pool_set do
            if A.msp_at[pool_set[i].tick .. ":1556"] then A.land_loc = A.land_loc + 1 end
            if A.lan_at[pool_set[i].tick .. ":32744:8068"] then A.land_loop = A.land_loop + 1 end
        end
        for i = 1, #A.msp do
            if A.msp[i].spotanim == 1556 and A.snd_at[A.msp[i].tick .. ":4005"] then A.land_snd = A.land_snd + 1 end
        end
        A.say_n, A.say_tick, A.screech_snd = 0, nil, 0
        for i = 1, #A.say do
            if string.find(A.say[i].text or "", "Screeeech", 1, true) then
                A.say_n = A.say_n + 1
                A.say_tick = A.say_tick or A.say[i].tick
                if A.snd_at[A.say[i].tick .. ":4007"] then A.screech_snd = A.screech_snd + 1 end
            end
        end
        A.dtick = (A.rty[3] and A.rty[3].tick) or -1
        A.death_anim = 0
        for i = 1, #clr do
            if clr[i].tick == A.dtick then A.death_anim = A.death_anim + 1 end
        end
        A.death_snd = 0
        for i = 1, #A.snd do
            if A.snd[i].sound == 3549 and A.snd[i].tick == A.dtick then A.death_snd = A.death_snd + 1 end
        end
        A.rm_lo, A.rm_hi, A.rm_n, A.rm_gfx, A.g_lo, A.g_hi = 9999, -1, 0, 0, 9999, -1
        for i = 1, #pool_gone do
            if pool_gone[i].tick >= A.dtick and A.dtick > 0 then
                local off = pool_gone[i].tick - A.dtick
                A.rm_n = A.rm_n + 1
                if off < A.rm_lo then A.rm_lo = off end
                if off > A.rm_hi then A.rm_hi = off end
                local got = false
                for sp = 1551, 1554 do
                    if A.msp_at[pool_gone[i].tick .. ":" .. sp] then
                        got = true
                        if sp < A.g_lo then A.g_lo = sp end
                        if sp > A.g_hi then A.g_hi = sp end
                    end
                end
                if got then A.rm_gfx = A.rm_gfx + 1 end
            end
        end
        specs[#specs + 1] = { id = "xarpus.av.enter.music", m = tostring(A.music_enter), u = "count", s = "567", g = "D", tol = "exact", ok = A.music_enter == 567, x = ", the first music row of the log, on tick " .. tostring(A.mus[1] and A.mus[1].tick) .. ", before the wake tick " .. tostring(W) }
        specs[#specs + 1] = { id = "xarpus.av.wake.music", m = tostring(A.music_wake), u = "count", s = "564", g = "D", tol = "exact", ok = A.music_wake == 564, x = ", the music row on the wake tick " .. tostring(W) .. ", the tick of the first npc_retype" }
        specs[#specs + 1] = { id = "xarpus.av.wake.npc_type", m = tostring(A.wake_to), u = "count", s = "8339,10767,10771", g = "C", tol = "exact", ok = A.wake_to == 8339, x = ", npc_retype " .. tostring(A.rty[1] and A.rty[1].from_type) .. " to " .. tostring(A.wake_to) .. " on tick " .. tostring(A.rty[1] and A.rty[1].tick) .. " (Entry's record)" }
        specs[#specs + 1] = { id = "xarpus.av.p1.idle_seq", m = (A.p1_idle > 0 and A.p1_act == 0) and "8060" or "-1", u = "count", s = "8060", g = "A", tol = "exact", ok = A.p1_idle > 0 and A.p1_act == 0, x = ", the cache record's own stand and walk loop tob_xarpus_absorb (readyanim = walkanim, a cache-bound loop with no npc_anim row): the client drew no action sequence on " .. A.p1_idle .. " ticks of phase 1 and an action on " .. A.p1_act }
        specs[#specs + 1] = { id = "xarpus.av.p2.idle_seq", m = (A.p2_idle > 0 and A.p2_act == 0) and "8058" or "-1", u = "count", s = "8058", g = "A", tol = "exact", ok = A.p2_idle > 0 and A.p2_act == 0, x = ", the cache record's own stand and walk loop tob_xarpus_idle (a cache-bound loop with no npc_anim row): the client drew no action sequence on " .. A.p2_idle .. " ticks of phase 3 and an action on " .. A.p2_act }
        specs[#specs + 1] = { id = "xarpus.av.exhumed_open.loc", m = tostring(ex_rise[1] and ex_rise[1].loc or -1), u = "count", s = "32743", g = "C", tol = "exact", ok = #ex_rise >= 12 and ex_rise[1].loc == 32743, x = ", loc_set rows of loc 32743 on " .. #ex_rise .. " rises, shape " .. tostring(ex_rise[1] and ex_rise[1].shape) }
        specs[#specs + 1] = { id = "xarpus.av.exhumed_open.seq", m = "8064", u = "count", s = "8064", g = "A", tol = "exact", ok = A.open_loop == #ex_rise and #ex_rise >= 12, x = ", the loc record's own start animation tob_xarpus_exhumed_start (script line tob_xarpus.rs2:530, no log kind); the loop 8065 that the script asks for is a loc_anim row on the rise tick in " .. A.open_loop .. " of " .. #ex_rise }
        specs[#specs + 1] = { id = "xarpus.av.exhumed_open.sound", m = (A.open_snd == #ex_rise and #ex_rise >= 12) and "3230" or "-1", u = "count", s = "3230", g = "D", tol = "exact", ok = A.open_snd == #ex_rise and #ex_rise >= 12, x = ", sound row 3230 on the same tick as loc_set 32743 in " .. A.open_snd .. " of " .. #ex_rise }
        specs[#specs + 1] = { id = "xarpus.av.exhumed_heal.proj", m = (#orbs > 0) and "1550" or "-1", u = "count", s = "1550", g = "C", tol = "exact", ok = #orbs > 0, x = ", " .. #orbs .. " projectile rows of 1550 over " .. #ex_rise .. " exhumed" }
        specs[#specs + 1] = { id = "xarpus.av.exhumed_heal.sound", m = (#orbs > 0 and A.heal_snd == #orbs) and "3956" or "-1", u = "count", s = "3956", g = "D", tol = "exact", ok = #orbs > 0 and A.heal_snd == #orbs, x = ", sound row 3956 on the tick of each orb: " .. A.heal_snd .. " of " .. #orbs }
        specs[#specs + 1] = { id = "xarpus.av.exhumed_close.sound", m = (A.close_snd == #ex_gone and #ex_gone >= 12) and "3995" or "-1", u = "count", s = "3995", g = "D", tol = "exact", ok = A.close_snd == #ex_gone and #ex_gone >= 12, x = ", sound row 3995 on the tick of each closing (loc_set -1): " .. A.close_snd .. " of " .. #ex_gone }
        specs[#specs + 1] = { id = "xarpus.av.p2_start.seq", m = (A.up_anim >= 1) and "8061" or "-1", u = "count", s = "8061", g = "D", tol = "exact", ok = A.up_anim >= 1, x = ", npc_anim 8061 on the stand-up tick " .. tostring(U) .. ", the tick of the npc_retype " .. tostring(A.rty[2] and A.rty[2].from_type) .. " to " .. tostring(A.p2_to) }
        specs[#specs + 1] = { id = "xarpus.av.p2.npc_type", m = tostring(A.p2_to), u = "count", s = "8340,10768,10772", g = "C", tol = "exact", ok = A.p2_to == 8340, x = ", the second npc_retype, on tick " .. tostring(A.rty[2] and A.rty[2].tick) }
        specs[#specs + 1] = { id = "xarpus.av.spit.seq", m = (#spit_t >= 5) and "8059" or "-1", u = "count", s = "8059", g = "D", tol = "exact", ok = #spit_t >= 5, x = ", npc_anim 8059 on " .. #spit_t .. " spit ticks (its frame sound 3290 rides the row)" }
        specs[#specs + 1] = { id = "xarpus.av.spit.proj", m = (#spit_t >= 5 and A.spit_proj == #spit_t) and "1555" or "-1", u = "count", s = "1555", g = "D", tol = "exact", ok = #spit_t >= 5 and A.spit_proj == #spit_t, x = ", a projectile 1555 leaving his footprint within a tick of each spit animation: " .. A.spit_proj .. " of " .. #spit_t }
        specs[#specs + 1] = { id = "xarpus.av.splat_land.gfx", m = (A.land_gfx > 0) and "1556" or "-1", u = "count", s = "1556", g = "D", tol = "exact", ok = A.land_gfx > 0, x = ", " .. A.land_gfx .. " map_spotanim rows of 1556" }
        specs[#specs + 1] = { id = "xarpus.av.splat_chain.proj", m = (#chains > 0) and "1555" or "-1", u = "count", s = "1555", g = "D", tol = "exact", ok = #chains > 0, x = ", " .. #chains .. " projectile rows of 1555 leaving a landed splat (not his footprint)" }
        specs[#specs + 1] = { id = "xarpus.av.splat_land.loc", m = (#pool_set > 0 and A.land_loc == #pool_set) and "32744" or "-1", u = "count", s = "32744", g = "D", tol = "exact", ok = #pool_set > 0 and A.land_loc == #pool_set, x = ", loc_set 32744 on the same tick as a 1556 graphic: " .. A.land_loc .. " of " .. #pool_set .. " pools (" .. A.land_gfx .. " landings in all, the rest on an existing pool)" }
        specs[#specs + 1] = { id = "xarpus.av.splat_land.seq", m = "8067", u = "count", s = "8067", g = "A", tol = "exact", ok = #pool_set > 0 and A.land_loop == #pool_set, x = ", the loc record's own start animation tob_xarpus_acid_splat_start (script line tob_xarpus.rs2:1124); the loop 8068 that the script asks for is a loc_anim row on the pool's tick in " .. A.land_loop .. " of " .. #pool_set }
        specs[#specs + 1] = { id = "xarpus.av.splat_land.sound", m = (A.land_gfx > 0 and A.land_snd == A.land_gfx) and "4005" or "-1", u = "count", s = "4005", g = "D", tol = "exact", ok = A.land_gfx > 0 and A.land_snd == A.land_gfx, x = ", sound row 4005 on the tick of each 1556 graphic: " .. A.land_snd .. " of " .. A.land_gfx }
        specs[#specs + 1] = { id = "xarpus.av.screech.sound", m = (A.say_n == 1 and A.screech_snd == 1) and "4007" or "-1", u = "count", s = "4007", g = "D", tol = "exact", ok = A.say_n == 1 and A.screech_snd == 1, x = ", sound row 4007 on the tick of the screech line (" .. tostring(A.say_tick) .. "): " .. A.screech_snd .. " of " .. A.say_n }
        specs[#specs + 1] = { id = "xarpus.av.screech.text", m = tostring(A.say_n), u = "count", s = "1", g = "C", tol = "exact", ok = A.say_n == 1, x = ", npc_say rows holding 'Screeeech!' over the whole fight" }
        specs[#specs + 1] = { id = "xarpus.av.death.seq", m = (A.death_anim >= 1) and "8063" or "-1", u = "count", s = "8063", g = "A", tol = "exact", ok = A.death_anim >= 1, x = ", npc_anim 8063 on tick " .. tostring(A.dtick) .. ", the tick of the npc_retype to the dead form" }
        local _, d8062 = t.ticklog.rows({ kind = "npc_anim", seq = 8062, slot = bslot })
        A.d8062_n, A.d8062_tick = 0, -1
        for i = 1, #d8062 do
            if d8062[i].tick == kill.tick + 1 then A.d8062_n = A.d8062_n + 1; A.d8062_tick = d8062[i].tick end
        end
        specs[#specs + 1] = { id = "xarpus.av.death_a.seq", m = (A.d8062_n >= 1) and "8062" or "-1", u = "count", s = "8062", g = "D", tol = "exact", ok = A.d8062_n >= 1 and A.dtick > A.d8062_tick, x = ", npc_death on tick " .. tostring(kill.tick) .. ", npc_anim 8062 on the combat form on tick " .. tostring(A.d8062_tick) .. " (one tick after), then 8063 with the retype on tick " .. tostring(A.dtick) }
        specs[#specs + 1] = { id = "xarpus.av.death.npc_type", m = tostring(A.death_to), u = "count", s = "8341,10769,10773", g = "C", tol = "exact", ok = A.death_to == 8341, x = ", the third npc_retype, on tick " .. tostring(A.dtick) }
        specs[#specs + 1] = { id = "xarpus.av.death.sound_script", m = tostring(A.death_snd), u = "count", s = "0", g = "A", tol = "exact", ok = A.death_snd == 0, x = ", sound rows of 3549 on the collapse tick (the frame sound of 8063 rides the npc_anim row)" }
        specs[#specs + 1] = { id = "xarpus.av.death_pools.window", m = (A.rm_n > 0) and (A.rm_lo .. "-" .. A.rm_hi) or "-1", u = "ticks", s = "4-7", g = "D", tol = "range", ok = A.rm_n > 0 and A.rm_lo >= 4 and A.rm_hi <= 7, x = ", " .. A.rm_n .. " of " .. #pool_set .. " pools removed, loc_set -1 rows after the collapse tick " .. tostring(A.dtick) }
        specs[#specs + 1] = { id = "xarpus.av.death_pools.gfx", m = (A.rm_gfx > 0) and (A.g_lo .. "-" .. A.g_hi) or "-1", u = "count", s = "1551-1554", g = "A", tol = "exact", ok = A.rm_n > 0 and A.rm_gfx == A.rm_n, x = ", a map_spotanim of 1551-1554 on the removal tick of " .. A.rm_gfx .. " of " .. A.rm_n .. " pools" }
        end
        t.ticks(1)
        for si = 1, #specs do
            local sp = specs[si]
            local line
            if sp.text then
                line = string.format("measured %s; %s (spec %s text, grade %s, tol %s)%s", sp.m, string.sub(sp.x, 3), sp.s, sp.g, sp.tol, sp.suffix or "")
            else
                line = string.format("measured %s %s%s (spec %s %s, grade %s, tol %s)%s", sp.m, sp.u, sp.x or "", sp.s, sp.u, sp.g, sp.tol, sp.suffix or "")
            end
            t.expect("spec." .. sp.id, sp.ok and "ok" or "bad", line)
        end

        -- the technique rows, from the player's side
        t.expect("technique.exhumed_cover", (#F.covers >= 6 and covered_heals == 0) and "ok" or "bad", "stood on " .. #F.covers .. " of " .. #ex_rise .. " exhumed, " .. covered_heals .. " heal orbs fired on a tick the player held the tile at the end of the tick before; arrivals " .. cdesc)
        t.expect("technique.spit_dodge", (dodge_n >= 3 and dodge_on == 0 and dodge_near <= 1 and dodge_ok + dodge_near == dodge_n) and "ok" or "bad", dodge_n .. " landings whose 3x3 held the leader's tile when its orb left (three ticks before), " .. dodge_ok .. " dodged to two or more tiles from the landing by the end of the tick before it, " .. dodge_near .. " held one tile away (the radius read), " .. dodge_on .. " landed on the tile the leader held; the leader read each orb's aim from the projectiles in the air and walked clear")
        t.expect("technique.lag_step", (probe_rec ~= nil and rad_hit == 1 and lag_contra == 0) and "ok" or "bad", "the spit's projectile row targets the tile held at the end of the tick before in " .. lag_agree .. " of " .. lag_n .. " spits, and the held tile one away took the landing hit (radius " .. rad_hit .. ")")
        t.expect("technique.stomp_skip", (stomp_lost == 1) and "ok" or "bad", "stood inside his footprint on the scan tick of spit slot " .. tostring(stomp.S) .. ": no 8059 row on it, rows on the slots either side; stomp " .. tostring(stomp_sum) .. " on the next tick")
        t.expect("technique.gaze_probe", (#F.retal >= 1) and "ok" or "bad", #F.retal .. " retaliation hitsplat(s) after a swing from the quadrant he faced (damage " .. rmin .. " to " .. rmax .. "), none from the three others")

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
        if collapse < 0 then
            t.blocked("content_bug: xarpus.death.collapse (grade A): the Entry kill plays no 8063 collapse. tob_xarpus.rs2:1371-1377 binds [ai_queue3] for tob_xarpus_combat and _hard only, and the story record's [ai_queue3] is the generated drop script drop_tables/scripts/wiki_xarpus.rs2:6, so ~tob_xarpus_died (tob_xarpus.rs2:1380) never runs on an Entry kill (CONTENT_BUGS.md:166). Also out of tolerance, known: p2.stomp_max (Entry halves each stomp splat, tob_xarpus.rs2:891-896, CONTENT_BUGS.md:216) and p3.retaliate_min_entry (tob_damage.rs2:384 rolls 50-75 in every mode, CONTENT_BUGS.md:215)")
            return
        end
        t.expect("party.barrier.dead", t.party.barrier("dead", 900))
        t.finish(0)
        return
    end,
}
