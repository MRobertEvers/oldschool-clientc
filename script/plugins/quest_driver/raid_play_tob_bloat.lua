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
    floor = { 24, 25, 37, 38 }, tank = { 28, 29, 33, 34 }, mirror = { 59, 61 },
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
        normal = { fly = 20, stomp = 80, hand = 50, stomp_plan = "leave" },
        hard = { fly = 20, stomp = 80, hand = 50, stomp_plan = "leave" },
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
    local in_stomp = math.max(math.abs(v.me.x - b.x), math.abs(v.me.z - b.z)) <= P.stomp_range
    local hidden = math.max(math.abs(v.me.x - hide_x), math.abs(v.me.z - hide_z)) <= 1
    local on_shadow = v.shadows[v.me.x * 100000 + v.me.z] == true
    local leave_age = P.stomp_age - 1 - math.ceil((P.stomp_range + 1) / QD.RAID_PLAY_RUN_TILES)
    local function threat(h)
        local total = 0
        for k = 1, h do
            if phase == "walk" then
                if not hidden then total = total + N.fly end
            else
                local a = age + k
                if a == P.stomp_age and (N.stomp_plan == "stay" or in_stomp) then total = total + N.stomp end
                if a >= P.up_age then total = total + N.fly end
            end
        end
        if on_shadow then total = total + N.hand end
        return total
    end
    -- prayers for the NEXT tick: down prayers through the attackable window,
    -- the walk's prayer from the tick before the first fly (T+33)
    local list = P.walk_prayers
    if phase == "down" and age < P.up_age - 1 then list = P.down_prayers end
    for _, name in ipairs(list) do intent.want[name] = true end
    local target_x, target_z = nil, nil
    if phase == "walk" then
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
    intent.eat, intent.drink, intent.need = QD.raid._play_supplies(st, v, threat)
    intent.attack = QD.raid._play_attack(st, v, intent.attack and intent.walk == nil)
    return intent
end
