-- quest-driver / raid_play_tob_bloat: the Pestilent Bloat plan for t.raid.play
-- (raid_play.lua).  Moved unchanged out of raid.lua's tail (raid seam27's
-- QD.RAID_PLAY_PLANS.tob_bloat and QD.raid._play_bloat_decide) by raid seam29
-- play_library_own_files; PLAY_NOTES.md "Bloat" is its strategy table.

QD.raid._play_plan("tob_bloat", {
    room = "bloat",
    boss = { entry = "tob_bloat_story", normal = "tob_bloat", hard = "tob_bloat_hard" },
    -- ENCOUNTER_TIMING.md 3.1 (blert BLOAT_DOWN_CYCLE_TICKS = 32): DOWN on T
    -- (seq 8082), attackable T+1..T+28, STOMP T+29, rise T+30..T+32, UP T+33.
    down_seq = 8082, down_ticks = 32, stomp_age = 29, rise_age = 30, up_age = 33,
    -- tob.constant ^tob_bloat_stomp_range = 6 ([M65]: no source gives a
    -- number), a huntall from Bloat's south-west tile.
    stomp_range = 6,
    -- ENCOUNTER_TIMING.md 3.4: graphics 1570-1573 mark the landing tile.
    shadow_lo = 1570, shadow_hi = 1573,
    -- Geometry local to Bloat's 64x64 map square (tob_bloat.lua: floor
    -- 6424..6437 x 89..102, tank 6428..6433 x 93..98 in square 6400,64).
    -- `mirror`: the tile straight behind the tank from Bloat's centre is
    -- (mirror.x - bx, mirror.z - bz) for Bloat's south-west tile bx,bz
    -- (tob_bloat.lua :1206, 12859 - bx and 189 - bz).
    --
    -- raid seam32: the TANK is x 29..34, not 28..33.  Every Bloat tick log of
    -- this pass (b32n3*, sv*playbloat, _play_bloat: player_tile rows) has
    -- raiders standing on 6428,93..98 (16-142 ticks each) and never on any of
    -- 6429..6434 x 93..98; the old box called the west column tank and the
    -- east column floor, so a hide tile at x 6434 was unreachable and the
    -- server's route to it ran round through 6435,92 under a shadow
    -- (svcplaybloat t309-311: walk sent to 6434,97, the hand landed on the
    -- route, the stun held all three raiders for the next hand).
    floor = { 24, 25, 37, 38 }, tank = { 29, 29, 34, 34 }, mirror = { 59, 61 },
    ring = { 23, 24, 34, 35 },
    modes = {
        -- tob.constant :752 entry flies 8, :760 entry stomp 40,
        -- tob_bloat.constant :39 entry hand 25 ([video][M62]).  Entry: the
        -- stomp is tick-eaten in place (wiki :675 "It is possible to tick
        -- eat this attack"), the flinch is the click back on the rise.
        entry = { fly = 8, stomp = 40, hand = 25, stomp_plan = "stay" },
        -- tob.constant :746 flies 20, :756 stomp 80, :762 hand 50.  Normal:
        -- "Unless the boss is below 3% health, it is recommended to run
        -- away after the last attack" (wiki :689).
        --
        -- raid seam32 play_tob_bloat_normal, the trio's three additions:
        --   fly_prayed  "up to 20 damage every tick, reduced by 25% if Protect
        --               from Missiles are active" (W:673): 15 is the most one
        --               fly lands while the raider reads the prayer lit.  The
        --               supplies' threat used 20 for every unhidden tick ahead,
        --               so a raider at 120 still read "eat" (b32n3a: 99 doses
        --               and bites, most at 120 hitpoints).
        --   runby       W:687 "one or two players should do a run-by on the
        --               boss with a Bandos godsword special to lower its
        --               Defence"; the BGS is not in this cache
        --               (tob_bloat_normal.lua:5), the Dragon warhammer is the
        --               drain the trio carries (yt_4i4lv-srJkw.md:45 "a dragon
        --               warhammer is basically essential").  Done by the one
        --               raider in the room on the first walk (W:687-689: the
        --               rest enter on the first down).
        --   hand_dodge  a shadow the tick's own step leaves is not counted as
        --               a hand that lands (ET 3.4; the threat function).
        --   offence_pots a brew drains Attack and Strength (wiki Saradomin
        --               brew :56, sara_brew.rs2:6-14); a super restore puts
        --               them back (wiki Super restore) and the super combat is
        --               re-sipped on the walk (yt_4i4lv-srJkw.md:47 "three
        --               super combats" for the raid).
        --
        -- raid seam42 play_tob_bloat_follows_blert: Normal follows the Blert
        -- reference (reference/bloat_normal_3.json, 19 death-free trio rooms;
        -- PLAY_NOTES.md "Bloat, Normal trio").  NO run-by: 30 recorded Normal
        -- trio rooms hold no Dragon warhammer special and one Bandos godsword
        -- special, and the run-by kept p1 in the flies eating four times
        -- before the first down (seam42 _play_bloat t63-82, hp lost 269
        -- against the reference's 49-217).  leave_from_here: the last swing is
        -- taken as late as the raider's own distance out of the stomp allows
        -- (Blert last swing age median 26.5, p10 15, p90 31), not at the age
        -- that fits the farthest raider (24 for everyone).
        normal = { fly = 20, stomp = 80, hand = 50, stomp_plan = "leave",
            fly_prayed = 15, offence_pots = true, hand_dodge = true, leave_from_here = true,
            hug_tank = true },
        hard = { fly = 20, stomp = 80, hand = 50, stomp_plan = "leave",
            fly_prayed = 15, runby = "dragon_warhammer", offence_pots = true, hand_dodge = true },
    },
    -- wiki :673 "reduced by 25% if Protect from Missiles are active":
    -- lit on every tick Bloat is up (the flies are sent every tick).
    walk_prayers = { "protectfrommissiles" },
    -- the offensive prayer for the attackable window; no source flicks it here.
    down_prayers = { "piety" },
    decide = "_play_bloat_decide",
})

-- THE BLOAT PLAN'S DECIDE (PLAY_NOTES.md "Bloat").  Walk: hide straight
-- behind the tank from where Bloat will be ("Hug the pillar and hide from
-- Bloat as it walks around the room", wiki_Theatre_of_Blood_Strategies
-- :687), Protect from Missiles lit, off any shadow ("simply don't stand on
-- the shadows", transcripts/yt_4i4lv-srJkw.md:71).  Down: in at once and
-- swing on cooldown with Piety ("As soon as Bloat deactivates ... begin
-- attacking with melee ... five attacks when close", wiki :689).  The stomp:
-- Entry tick-eats it in place (wiki :675) and clicks back on the rise ("when
-- he starts to get back up ... that's when you click back", the flinch
-- guide, ENCOUNTER_TIMING.md 3.1); Normal/Hard runs out of its reach after
-- the last swing that fits ("run away after the last attack", wiki :689).
function QD.raid._play_bloat_decide(st, v)
    local P, N, O = st.plan, st.numbers, st.origin
    local intent = { want = {}, walk = nil, attack = false }
    local b = v.boss
    if b == nil then
        return intent
    end
    local phase, age = "walk", -1
    if b.seq_id == P.down_seq then
        local a = v.api_now - b.seq_tick
        if a >= 0 and a <= P.down_ticks then
            phase, age = "down", a
        end
    end
    if phase == "down" and st.down_key ~= b.seq_tick then
        st.down_key = b.seq_tick
        st.down = { tick = v.tick - age, seen_age = age, bx = b.x, bz = b.z, index = #st.downs + 1 }
        st.downs[#st.downs + 1] = st.down
    end
    st.phase = phase
    local function floor_ok(x, z)
        local inside = x >= O.x + P.floor[1] and x <= O.x + P.floor[3] and z >= O.z + P.floor[2] and z <= O.z + P.floor[4]
        local tank = x >= O.x + P.tank[1] and x <= O.x + P.tank[3] and z >= O.z + P.tank[2] and z <= O.z + P.tank[4]
        return inside and not tank
    end
    -- where Bloat will be two ticks on (its walk is visible); the hide tile
    -- is that tile mirrored through the tank (tob_bloat.lua :1206)
    local fx, fz = b.x, b.z
    if phase == "walk" and st.prev_b ~= nil then
        fx = math.max(O.x + P.ring[1], math.min(O.x + P.ring[3], b.x + 2 * (b.x - st.prev_b.x)))
        fz = math.max(O.z + P.ring[2], math.min(O.z + P.ring[4], b.z + 2 * (b.z - st.prev_b.z)))
    end
    st.prev_b = { x = b.x, z = b.z }
    local hide_x, hide_z = 2 * O.x + P.mirror[1] - fx, 2 * O.z + P.mirror[2] - fz
    -- raid seam42 (N.hug_tank): "Hug the pillar" (W:687).  The mirror tile is
    -- two tiles off the tank (seam42 _play_bloat: all three at 6428,101, nine
    -- from Bloat's footprint, first swing at age 6-7); the recorded Normal
    -- trios hid at seven (Blert walk distance mode 7, build/blert/bloat) and
    -- swung first at age 3 (reference react.phase.down1 3 [1-6]).  The tile
    -- one off the tank on the same line is still behind it.
    if N.hug_tank then
        hide_x = math.max(O.x + P.tank[1] - 1, math.min(O.x + P.tank[3] + 1, hide_x))
        hide_z = math.max(O.z + P.tank[2] - 1, math.min(O.z + P.tank[4] + 1, hide_z))
    end
    local in_stomp = math.max(math.abs(v.me.x - b.x), math.abs(v.me.z - b.z)) <= P.stomp_range
    local hidden = math.max(math.abs(v.me.x - hide_x), math.abs(v.me.z - hide_z)) <= 1
    local on_shadow = v.shadows[v.me.x * 100000 + v.me.z] == true
    local leave_age = P.stomp_age - 1 - math.ceil((P.stomp_range + 1) / QD.RAID_PLAY_RUN_TILES)
    -- raid seam42 (N.leave_from_here): the stomp is a huntall of stomp_range
    -- round Bloat's south-west tile (tob_bloat.rs2:823), so a raider is out of
    -- it after (stomp_range + 1 - its own distance from that tile) tiles, not
    -- after stomp_range + 1 from anywhere.  One tick more than that run: the
    -- old age's own arithmetic on the raider's distance (no spare tick) let
    -- the stomp land on 2-3 raiders a run in seam42 survey_5 (_play_bloat
    -- t193, t264: the run round the tank's corner is longer than the
    -- distance); with the tick none landed in survey_4.  The farthest case
    -- (distance 0) is the old age.
    if N.leave_from_here and phase == "down" then
        local d_sw = math.max(math.abs(v.me.x - b.x), math.abs(v.me.z - b.z))
        local need = math.max(0, P.stomp_range + 1 - d_sw)
        leave_age = math.max(leave_age, P.stomp_age - 2 - math.ceil(need / QD.RAID_PLAY_RUN_TILES))
        if st.down ~= nil and st.down.leave_at == nil and age >= leave_age then
            st.down.leave_at = { age = age, d_sw = d_sw }
        end
    end
    -- raid seam32: the most one fly lands with Protect from Missiles (W:673).
    -- The plan lights it on every walking tick and from T+32, the tick before
    -- the first fly of a rise (walk_prayers; the `list` below), so every fly
    -- the threat counts lands on a prayed raider while any prayer is left.
    -- Entry carries no fly_prayed and reads N.fly as before.
    local fly = N.fly
    if N.fly_prayed ~= nil and v.prayer > 0 then fly = N.fly_prayed end
    local function threat(h)
        local total = 0
        for k = 1, h do
            if phase == "walk" then
                if not hidden then total = total + fly end
            else
                local a = age + k
                -- raid seam42 (N.leave_from_here): a raider who will leave in
                -- time is not hit by the stomp; counting it while it still
                -- swings ate a bite at 85-99 hitpoints on a down's last swings
                -- (seam42 _play_bloat t125, t218: all three raiders)
                local caught = in_stomp and (not N.leave_from_here or age >= leave_age)
                if a == P.stomp_age and (N.stomp_plan == "stay" or caught) then total = total + N.stomp end
                if a >= P.up_age then total = total + fly end
            end
        end
        -- raid seam32 (N.hand_dodge): a hand is judged on the tile the raider
        -- ends the tick before its impact on (ET 3.4), so a shadow this tick's
        -- step leaves is not a hand that lands; counting it ate a bite and a
        -- brew at 99-115 hitpoints on every shadow (b32n3b t152, t189)
        local leaving = N.hand_dodge and intent.walk ~= nil and (intent.walk.x ~= v.me.x or intent.walk.z ~= v.me.z)
        if on_shadow and not leaving then total = total + N.hand end
        return total
    end
    -- prayers for the NEXT tick: down prayers through the attackable window,
    -- the walk's prayer from the tick before the first fly (T+33)
    local list = P.walk_prayers
    if phase == "down" and age < P.up_age - 1 then list = P.down_prayers end
    for _, name in ipairs(list) do intent.want[name] = true end
    -- raid seam32: THE RUN-BY (W:687), the first walk, the raider in the room.
    local runby = QD.raid._play_bloat_runby(st, v, phase, intent)
    local target_x, target_z = nil, nil
    if runby then
        -- the spec is being swung: no hide walk, the attack press paths in
    elseif phase == "walk" then
        target_x, target_z = hide_x, hide_z
    elseif N.stomp_plan == "stay" then
        intent.attack = age < P.stomp_age
        if age >= P.rise_age then
            target_x, target_z = 2 * O.x + P.mirror[1] - b.x, 2 * O.z + P.mirror[2] - b.z
        end
    else
        intent.attack = age < leave_age
        if age >= leave_age then
            target_x, target_z = 2 * O.x + P.mirror[1] - b.x, 2 * O.z + P.mirror[2] - b.z
        end
    end
    if phase == "down" and age >= P.stomp_age - 2 and age <= P.stomp_age - 1 and st.down.pre_stomp == nil then
        st.down.pre_stomp = v.hp
    end
    -- raid seam29: the plan's own walk (the hide, the flinch, the leave) is
    -- `plan_walk`; with none, the attack press's own path and a marker under a
    -- standing raider go through the same skills (raid_play.lua _play_reach,
    -- _play_hazard, _play_safe_step).
    local plan_walk = target_x ~= nil
    if not plan_walk and intent.attack then
        local rx, rz, hold = QD.raid._play_reach(st, v, floor_ok)
        if rx ~= nil then
            target_x, target_z = rx, rz
        elseif hold then
            intent.attack = false
        end
    end
    if target_x == nil and on_shadow then
        target_x, target_z = v.me.x, v.me.z
    end
    if target_x ~= nil then
        local sx, sz, moved = QD.raid._play_hazard(st, v, target_x, target_z, floor_ok)
        sx, sz = QD.raid._play_safe_step(st, v, sx, sz, floor_ok)
        if on_shadow then st.dodges = st.dodges + 1 end
        local same = st.walk_target ~= nil and st.walk_target.x == sx and st.walk_target.z == sz
        local stuck = st.last_me ~= nil and st.last_me.x == v.me.x and st.last_me.z == v.me.z
        if (v.me.x ~= sx or v.me.z ~= sz) and (not same or stuck) then
            intent.walk = { x = sx, z = sz }
            if plan_walk and phase == "down" and st.down.flinch == nil then
                st.down.flinch = { tick = v.tick, age = age, from = { x = v.me.x, z = v.me.z } }
                st.flinches[#st.flinches + 1] = st.down.flinch
            end
        end
    end
    -- raid seam32: while the run-by's special is being swung a bite costs the
    -- swing 3 ticks (wiki Food, consume_shared.rs2:28-49), and on a free tick
    -- the library looks six ticks ahead, so a raider standing in the flies ate
    -- every other tick and never swung (_play_bloat t67-92: 25 ticks targeted,
    -- no swing).  The run-by is a few ticks in the flies by design (W:687), so
    -- it eats only for what can land in the next RUNBY_EAT_TICKS ticks.
    local supplies_threat = threat
    if runby then
        supplies_threat = function(h) return threat(math.min(h, QD.RAID_PLAY_BLOAT_RUNBY_EAT_TICKS)) end
    end
    intent.eat, intent.drink, intent.need = QD.raid._play_supplies(st, v, supplies_threat)
    if N.offence_pots and intent.drink == nil then
        intent.drink = QD.raid._play_bloat_offence(st, v, phase)
    end
    intent.attack = QD.raid._play_attack(st, v, intent.attack and intent.walk == nil)
    return intent
end

-- raid seam32 play_tob_bloat_normal: THE RUN-BY.  W:687 "While optional, one
-- or two players should do a run-by on the boss with a Bandos godsword special
-- to lower its Defence"; the Dragon warhammer is the drain this cache has
-- (N.runby; tob_bloat_normal.lua:5) and drains 30% of the current Defence when
-- its hit is above 0 (DRIVER_NOTES "Bloat: Defence reads 80 of 80 after a
-- Dragon warhammer special").  Who: the one raider in the room before the
-- first down (W:687-689: the rest enter on the down), on the first walk.
-- The kit goes on in one block, the special is armed from the orb with the
-- attack press the next tick, and the swing is SEEN as the energy it spends
-- (varp300, the special orb's own number).  The hammer stays on until the
-- special's splat shows on Bloat (a 0 drains nothing and is swung again while
-- the energy lasts); on that tick the scythe goes back on in the same block as
-- the walk back to the hide tile ("the scythe back the same tick").  The stomp restores Defence (W:675), so the drain serves
-- the first down.  Returns true while the run-by owns the tick (no hide walk;
-- the attack press is the approach).  st.runby is the record.
QD.RAID_PLAY_BLOAT_SPEC_COST = 500      -- the Dragon warhammer's special (DRIVER_NOTES seam10: "falls by 500 (DWH)")
QD.RAID_PLAY_BLOAT_RUNBY_GIVE_UP = 24   -- ticks from the arm with no energy spent (the seam's bound: four DWH swings)
-- Specials one run-by may spend.  1000 energy is two hammer specials, but a
-- second swing on a 0 kept the raider in the flies for 26 ticks, eating every
-- other tick and never swinging (_play_bloat t65-91, hitpoints 18-47): one.
QD.RAID_PLAY_BLOAT_RUNBY_TRIES = 1
QD.RAID_PLAY_BLOAT_RUNBY_EAT_TICKS = 3  -- the run-by's supplies horizon (the seam's choice: three flies, 45 with the prayer)
function QD.raid._play_bloat_runby(st, v, phase, intent)
    local N = st.numbers
    if N.runby == nil or st.party <= 1 or st.role ~= 1 then
        return false
    end
    local rb = st.runby
    if rb == nil then
        rb = { stage = "wait", weapon = N.runby, splats = {} }
        st.runby = rb
    end
    if rb.stage == "done" or rb.stage == "gave_up" then
        return false
    end
    local _, energy = QD.var.varp("varp300_sa_energy")
    energy = tonumber(energy) or 0
    if rb.stage == "wait" then
        -- the first walk, with the energy for one special and the hitpoints
        -- to stand in the flies while it is swung
        if phase ~= "walk" or #st.downs > 0 then
            rb.stage = "gave_up"
            rb.why = "the first down came first"
            return false
        end
        if energy < QD.RAID_PLAY_BLOAT_SPEC_COST or v.hp < 70 then
            return false
        end
        rb.stage = "equip"
        rb.equip_tick = v.tick
        rb.energy0 = energy
        intent.gear = { rb.weapon }
        intent.want.piety = true
        return true
    end
    intent.want.piety = true
    if rb.stage == "equip" then
        rb.stage = "swing"
        rb.arm_tick = v.tick
        intent.spec = true
        intent.attack = true
        st.engaged = false
        return true
    end
    -- "fired": the hammer stays on until the special's own splat shows on
    -- Bloat (the npc row's latest hitsplat, what a person sees: one tick after
    -- the swing).  A 0 drains nothing (DRIVER_NOTES seam10 "A 0 splat drains
    -- nothing: try again ... while energy is 500 or more"): with the energy
    -- for another and the walk still on, the special is armed again.  Any
    -- other splat, or none in three ticks, ends the run-by: the scythe goes
    -- back on in the same block as the walk back to the hide tile.
    if rb.stage == "fired" then
        local seen = v.boss ~= nil and v.boss.hit_cycle ~= nil and rb.cycle_at_fire ~= nil and v.boss.hit_cycle > rb.cycle_at_fire
        if seen then
            rb.splat = v.boss.hit_damage
            rb.splats[#rb.splats + 1] = tostring(v.boss.hit_damage) .. "@t" .. v.tick
        end
        if seen and rb.splat == 0 and energy >= QD.RAID_PLAY_BLOAT_SPEC_COST and phase == "walk"
            and #rb.splats < QD.RAID_PLAY_BLOAT_RUNBY_TRIES then
            rb.stage = "swing"
            rb.arm_tick = v.tick
            rb.energy0 = energy
            intent.spec = true
            intent.attack = true
            st.engaged = false
            return true
        end
        if seen or v.tick - rb.fired >= 3 or phase ~= "walk" then
            rb.stage = "done"
            intent.gear = { "scythe_of_vitur" }
            intent.want.piety = nil
            return false
        end
        return true
    end
    -- "swing": the special is spent when the orb's energy falls by its cost
    if energy <= rb.energy0 - QD.RAID_PLAY_BLOAT_SPEC_COST then
        rb.stage = "fired"
        rb.fired = v.tick
        rb.energy1 = energy
        rb.cycle_at_fire = v.boss ~= nil and v.boss.hit_cycle or nil
        return true
    end
    if v.tick - rb.arm_tick > QD.RAID_PLAY_BLOAT_RUNBY_GIVE_UP or phase ~= "walk" then
        rb.stage = "gave_up"
        rb.why = "no energy spent in " .. (v.tick - rb.arm_tick) .. " ticks (phase " .. phase .. ")"
        intent.gear = { "scythe_of_vitur" }
        intent.want.piety = nil
        return false
    end
    -- re-arm if the orb reads unarmed two ticks on and nothing was spent
    local _, armed = QD.var.varp("varp301_sa_attack")
    if tonumber(armed) == 0 and v.tick - rb.arm_tick >= 2 and (rb.rearm or 0) < 3 then
        rb.rearm = (rb.rearm or 0) + 1
        rb.arm_tick_last = v.tick
        intent.spec = true
    end
    intent.attack = true
    return true
end

-- raid seam32: THE OFFENCE POTIONS (N.offence_pots).  A Saradomin brew drains
-- Attack and Strength (wiki Saradomin brew :56; br_potion.rs2:78-79), a
-- super restore brings them back to base (wiki Super restore; br_potion.rs2
-- :93-94), and the super combat's boost is re-sipped on a walk when it has
-- worn down (yt_4i4lv-srJkw.md:47).  Read from the raider's own skills (what a
-- person sees in the stats tab).  Returns a potion name or nil.
QD.RAID_PLAY_COMBAT_POTS = { "1dose2combat", "2dose2combat", "3dose2combat", "4dose2combat" }
function QD.raid._play_bloat_offence(st, v, phase)
    if v.tick - st.last_drink < QD.RAID_PLAY_DRINK_DELAY then
        return nil
    end
    local ar, att = QD.skill.read("attack")
    if ar ~= "ok" then
        return nil
    end
    local base = att.base or att.base_level or 99
    local list = nil
    if att.level < base then
        list = QD.RAID_PLAY_RESTORES
    elseif phase == "walk" and att.level < base + 10 then
        list = QD.RAID_PLAY_COMBAT_POTS
    end
    if list == nil then
        return nil
    end
    for _, name in ipairs(list) do
        local cr, n = QD.inv.count(name)
        if cr == "ok" and n > 0 then
            st.offence_pots = (st.offence_pots or 0) + 1
            return name
        end
    end
    return nil
end
