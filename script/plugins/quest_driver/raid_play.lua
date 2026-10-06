-- quest-driver / raid_play: THE PLAY LIBRARY (raid seam27 raid_play_by_tick_intent,
-- moved out of raid.lua's tail unchanged by raid seam29 play_library_own_files).
-- The loop t.raid.play and the shared skills live here; each room's PLAN lives in
-- its own driver part, raid_play_<raid>_<room>.lua, loaded after this file
-- (DRIVE_SCRIPT_PARTS, src/plugin/torirs_plugin_drive.c), so a room's plan is
-- written without touching another's.  docs/minigames/raid_loop/PLAY_NOTES.md.

-- ==========================================================================
-- SEAM raid_play_by_tick_intent (raid seam27, 2026-10-05) -- THE PLAY
-- LIBRARY.  Everything below this banner is this seam's.
--
--   t.raid.play(plan_id, opts) -> ok | died | timeout | unsupported, detail, record
--
-- The owner, 2026-10-05: "the driver is not very fast or good. That is not
-- going to work in normal mode. You will need to code up the agents a lot
-- smarter using the actual strategies."  Each room test used to carry its own
-- fight loop: one action per pass, reacting after the fact.  This is ONE loop
-- every room test calls, plus one strategy table per room (the PLAN), each
-- line citing its source; docs/minigames/raid_loop/PLAY_NOTES.md is the
-- table of skills and plans with the same citations.
--
-- THE LOOP (QD.raid._play_tick).  Every server tick: SEE what a person at the
-- screen can see (the boss's animation and tile, the shadows on the floor,
-- its own hitpoints, prayer points, lit prayers, tile, and the swings of its
-- own weapon), DECIDE the tick's whole intent (the plan's decide function),
-- SEND it together (prayers, potion, food and the step in ONE t.together; the
-- attack press after it).  Nothing waits across ticks: a walk is re-issued
-- only when its target changes, never waited out.  What the loop never reads:
-- the server's registers, `::tob*` readouts, the seed, the tick log's hidden
-- columns.  The one tick-log kind it reads is `player_anim` for its own pid
-- (the swing it sees itself make) and, to stop, the boss's `npc_death`; a
-- member (no tick log) counts its swings from its presses and the speed.
--
-- THE SKILLS, each a small function below with its source in a comment
-- (PLAY_NOTES.md "Skills"): _play_attack (attack on cooldown), _play_pray
-- (pray by the telegraph), _play_supplies (eat by the largest hit before the
-- next chance to eat; potions on their own timer), _play_hazard (step off a
-- marked tile by the shortest safe step), and since raid seam29
-- _play_safe_step (no tick of a walk ends on a marker) and _play_reach (the
-- attack press's own path: while a marker is down the player walks to an
-- unmarked reach tile and presses from there).  A plan sends EVERY move
-- through them: its own walk, the approach to attack, and a step off a
-- marker that appears under a standing raider.  A loadout swap is the
-- intent's `gear` list in the same t.together, and `spec` arms the special
-- from the orb before the attack press (raid seam32: Bloat Normal's run-by).
-- ==========================================================================

-- Weapons: attack speed in ticks and the swing animation the player sees.
-- scythe_of_vitur: wiki Scythe of vitur, attack speed 5; seq 8056 measured in
-- build/quest_gate/tob_bloat/ticklog.tsv (player_anim every 5 ticks, 104..124).
QD.RAID_PLAY_WEAPONS = {
    scythe_of_vitur = { speed = 5, seqs = { [8056] = true } },
    scythe_of_vitur_uncharged = { speed = 5, seqs = { [8056] = true } },
}

-- Food, best heal first, at 99 Hitpoints: wiki Anglerfish (n/10 + 13 = 22,
-- overheals), Shark 20.  A brew is a POTION: its own timer and no attack
-- delay ("Potions do not incur the standard 3 tick attack or eat delay",
-- consume_shared.rs2:49), so it combos with a food in one tick ("marlin,
-- Saradomin brew, and halibut - in that order", wiki Food/Fast foods).
-- Saradomin brew heals 2 + 15% = 16 at 99 (wiki Saradomin brew).
-- The Theatre's bandages (raid seam35e play_tob_entry_relay): a food that
-- "not only heals 20 Hitpoints" (Entry Mode page, wiki_Theatre_of_Blood_
-- Entry_Mode.wikitext:151; tob_spectate.rs2 [opheld1,tob_bandages]
-- stat_heal(hitpoints, 20, 0) and ~consume_food_taken).  Last, so a pack
-- with fish eats the fish first as before; a raider whose fish are gone
-- after the supply chest eats them (the relay's Xarpus died at 14 hp with
-- ten bandages in the pack because this table did not know them).
QD.RAID_PLAY_FOOD = {
    { item = "anglerfish", heal = 22 },
    { item = "shark", heal = 20 },
    { item = "tob_bandages", heal = 20 },
}
QD.RAID_PLAY_BREWS = { "br_1dosepotionofsaradomin", "br_2dosepotionofsaradomin",
    "br_3dosepotionofsaradomin", "br_4dosepotionofsaradomin" }
QD.RAID_PLAY_BREW_HEAL = 16
-- Super restore: 8 + 25% of the Prayer level = 32 at 99 (wiki Super restore).
QD.RAID_PLAY_RESTORES = { "br_1dose2restore", "br_2dose2restore", "br_3dose2restore", "br_4dose2restore" }
QD.RAID_PLAY_RESTORE_AMOUNT = 32
-- Food "adds a 3 tick penalty to when a player may eat again"; a potion
-- "delay[s] your next potion consumption by 3 ticks" (consume_shared.rs2:31-48).
QD.RAID_PLAY_EAT_DELAY = 3
QD.RAID_PLAY_DRINK_DELAY = 3
-- Running moves two tiles a tick (wiki Energy: run); used to time a leave.
QD.RAID_PLAY_RUN_TILES = 2

-- THE PLANS.  One table per room, each registered by its own driver part
-- (raid_play_tob_bloat.lua, raid_play_tob_maiden.lua, ...: after this file in
-- DRIVE_SCRIPT_PARTS) through QD.raid._play_plan; `modes` holds what changes
-- with the mode.  A plan with no `decide` answers `unsupported` with its
-- `unsupported` line (the room seam that will write it).
QD.RAID_PLAY_PLANS = {}

-- A room part registers its plan ONCE under its id (a second registration of
-- one id is two files claiming one room: a contract violation, not a merge).
function QD.raid._play_plan(plan_id, plan)
    assert(type(plan_id) == "string", "raid.play: a plan id is a string")
    assert(type(plan) == "table", "raid.play: plan " .. tostring(plan_id) .. " is not a table")
    assert(QD.RAID_PLAY_PLANS[plan_id] == nil, "raid.play: plan " .. plan_id .. " registered twice")
    QD.RAID_PLAY_PLANS[plan_id] = plan
end

-- t.raid.play(plan_id, opts): play the room by its plan until the boss dies
-- (ok), the player dies (died) or opts.max_ticks server ticks pass (timeout).
--   opts.mode    "entry" | "normal" | "hard" (default "entry")
--   opts.weapon  the worn weapon's symbol (default scythe_of_vitur)
--   opts.max_ticks (default 1500)
-- Returns result, detail, record (PLAY_NOTES.md "The record").
function QD.raid.play(plan_id, opts)
    opts = opts or {}
    local plan = QD.RAID_PLAY_PLANS[plan_id]
    if plan == nil then
        return "unsupported", "raid.play: no plan named " .. tostring(plan_id)
    end
    if plan.decide == nil then
        return "unsupported", "raid.play: plan " .. plan_id .. " has no decide function yet: "
            .. (plan.unsupported or "its strategy is in PLAY_NOTES.md (the re-author pass)")
    end
    local mode = opts.mode or "entry"
    local numbers = plan.modes[mode]
    assert(numbers, "raid.play: plan " .. plan_id .. " has no mode " .. tostring(mode))
    local weapon_name = opts.weapon or "scythe_of_vitur"
    local weapon = QD.RAID_PLAY_WEAPONS[weapon_name]
    assert(weapon, "raid.play: no weapon row for " .. tostring(weapon_name))
    local st = QD.raid._play_state(plan, plan_id, mode, numbers, weapon, opts)
    local max_ticks = opts.max_ticks or 1500
    local result = "timeout"
    while true do
        local outcome = QD.raid._play_tick(st)
        if outcome ~= nil then
            result = outcome
            break
        end
        local _, now = QD.tick()
        if now - st.start_tick >= max_ticks then
            break
        end
    end
    st.result = result
    return result, QD.raid._play_summary(st), st
end

function QD.raid._play_state(plan, plan_id, mode, numbers, weapon, opts)
    local _, now = QD.tick()
    local _, me = QD.world.tile()
    local st = {
        plan = plan, plan_id = plan_id, mode = mode, numbers = numbers, weapon = weapon,
        boss_symbol = plan.boss[mode], role = QD.party.role(), party = QD.party.size(),
        start_tick = now, origin = { x = math.floor(me.x / 64) * 64, z = math.floor(me.z / 64) * 64 },
        -- what happened, per server tick (the room test's rows read these)
        inputs = {}, hp_at = {}, prayer_at = {}, tile_at = {}, swings = {}, eats = {}, drinks = {},
        downs = {}, flinches = {}, dodges = 0, blocks = {}, refusals = 0, lines = {},
        last_swing = -1000, engaged = false, engaged_tick = -1000, last_eat = -1000, last_drink = -1000,
        walk_target = nil, boss_seen = false, boss_gone = 0, boss_slot = nil, anim_serial = 0,
        death_serial = 0, log = false, my_pid = nil,
    }
    -- The tick log is the leader's; a member reads none (README "A party run").
    local lr = QD.ticklog.rows({ kind = "mark" })
    st.log = (lr == "ok")
    -- raid seam35e play_tob_entry_relay: the swings this room's loop counts
    -- start NOW.  A room test starts its tick log at the room, so serial 0
    -- was the room's start; a whole-raid relay's log starts in the lobby, and
    -- the first read took every earlier room's player_anim rows whose seq is
    -- the weapon row's as this room's swings.  Verzik's first read ran on the
    -- default row (the scythe, 8056) before her plan put on its own, counted
    -- the 60 scythe swings of Bloat, Sotetseg and Xarpus as P1 punches, and
    -- swapped to the bow fifteen ticks in (svdplayentry, the five-name survey:
    -- a P1 that outlasted the food).
    if st.log then
        local ar, rows = QD.ticklog.rows({ kind = "player_anim" })
        if ar == "ok" then
            for _, row in ipairs(rows) do st.anim_serial = math.max(st.anim_serial, row.serial) end
        end
    end
    local pr, rows = api_drive.players()
    if pr == "ok" then
        for _, r in ipairs(rows) do
            if r.me then st.my_pid = r.pid end
        end
    end
    return st
end

-- SEE: what a person at the screen reads this tick.
function QD.raid._play_see(st)
    local v = {}
    local _, now = QD.tick()
    v.tick = now
    v.api_now = api_drive.tick()
    local _, me = QD.world.tile()
    v.me = me
    local _, hp = QD.skill.read("hitpoints")
    v.hp = hp.level
    v.hp_base = hp.base or hp.base_level or 99
    local _, pp = QD.skill.read("prayer")
    v.prayer = pp.level
    v.prayer_base = pp.base or pp.base_level or 99
    local _, _, lit = QD.prayer.read()
    v.lit = lit or {}
    local br, b = QD.npc.state(st.boss_symbol)
    if br == "ok" then v.boss = b end
    v.shadows = {}
    local sr, spots = QD.world.spotanims(0)
    if sr == "ok" then
        for k = 1, #spots do
            local sid = spots[k].spotanim_id
            if sid >= (st.plan.shadow_lo or -1) and sid <= (st.plan.shadow_hi or -2) then
                v.shadows[spots[k].x * 100000 + spots[k].z] = true
            end
        end
    end
    -- the swing animation of the player's own weapon (player_anim, own pid)
    if st.log then
        local ar, rows = QD.ticklog.rows({ kind = "player_anim", since = st.anim_serial })
        if ar == "ok" then
            for _, row in ipairs(rows) do
                st.anim_serial = math.max(st.anim_serial, row.serial)
                if (st.party <= 1 or st.my_pid == nil or row.pid == st.my_pid) and st.weapon.seqs[row.seq] then
                    if row.tick > st.last_swing then
                        st.last_swing = row.tick
                        st.swings[#st.swings + 1] = row.tick
                    end
                end
            end
        end
    end
    st.hp_at[now] = v.hp
    st.tile_at[now] = { x = me.x, z = me.z }
    local on = {}
    for name, lit_now in pairs(v.lit) do
        if lit_now then on[#on + 1] = name end
    end
    st.prayer_at[now] = v.lit
    -- raid seam31: a pressed press answers on the tick its effect shows
    QD.raid._play_press_confirm(st, v)
    return v
end

-- SKILL: ATTACK ON COOLDOWN.  Once a target is clicked the player swings on
-- its own every `speed` ticks (wiki Attack speed: "the number of ticks
-- between attacks"); a click is needed only to START the fight or after a
-- step cleared it.  So the press goes out when the plan wants a swing and
-- the player is not engaged, or no swing was seen for speed + 2 ticks.  A
-- member with no tick log counts a swing every `speed` ticks of engagement.
-- Returns true when this tick should carry an attack press.
function QD.raid._play_attack(st, v, want)
    if not want then
        return false
    end
    local speed = st.weapon.speed
    if not st.log and st.engaged and v.tick - math.max(st.last_swing, st.engaged_tick) >= speed then
        st.last_swing = v.tick
        st.swings[#st.swings + 1] = v.tick
    end
    if not st.engaged then
        return true
    end
    return v.tick - math.max(st.last_swing, st.engaged_tick) > speed + 2
end

-- The next tick the weapon is ready (the "free" tick to eat on: "If your
-- weapon is ready to attack again then eating does not add any new delay",
-- wiki Food, quoted at consume_shared.rs2:34-38).
function QD.raid._play_next_swing(st, v)
    if not st.engaged then
        return v.tick
    end
    local next_swing = st.last_swing + st.weapon.speed
    if st.last_swing < st.engaged_tick then
        next_swing = st.engaged_tick + 1
    end
    if next_swing < v.tick then
        next_swing = v.tick
    end
    return next_swing
end

-- SKILL: PRAY BY THE TELEGRAPH.  `want` is the set the plan wants lit on the
-- NEXT tick (a prayer pressed between ticks T-1 and T is in force for T:
-- DRIVER_NOTES "Several inputs in one tick", varbit read back +0).  Returns
-- the switches to send: { {name, on}, ... }.
--
-- raid seam31 play_library_faults, FAULT 1: a press is a TOGGLE and the
-- server puts out every prayer that shares an exclusion group with the one
-- it lights (prayer.rs2:309-318 ~prayer_deactivate_conflicting, before the
-- new one is set; QD.prayer.GROUPS).  So an "off" for a prayer that
-- conflicts with one this tick lights is never sent: the server already put
-- it out, and the press would light it again and put the new one out (raid
-- seam30 ny30d: "on magic" + "off missiles" held Protect from Missiles
-- t67-362 while Magic was asked seven times).
function QD.raid._play_pray(st, v, want, all)
    local out = {}
    if v.prayer <= 0 then
        return out
    end
    local lighting = {}
    for _, name in ipairs(all) do
        if want[name] == true and v.lit[name] ~= true then
            lighting[#lighting + 1] = name
        end
    end
    for _, name in ipairs(all) do
        local on = want[name] == true
        if (v.lit[name] == true) ~= on then
            local put_out_by = nil
            if not on then
                for _, lit_name in ipairs(lighting) do
                    if put_out_by == nil and QD.prayer.conflicts(lit_name, name) then
                        put_out_by = lit_name
                    end
                end
            end
            if put_out_by == nil then
                out[#out + 1] = { name, on }
            else
                st.pray_skips = (st.pray_skips or 0) + 1
            end
        end
    end
    return out
end

-- SKILL: SUPPLIES.  `threat(h)` is the most damage that can land in the next
-- h ticks (the plan's).  Eat when the hitpoints would not survive the hits
-- that can land before the NEXT chance to eat: on a free tick (not attacking,
-- or the weapon is ready) that chance is the next swing (engaged) or the eat
-- delay (3) away; on a tick between swings eating costs the attack 3 ticks,
-- so between swings only a hit that can land before the next tick's bite is
-- read forces one.  A brew rides along when the food alone is short (combo eating).
-- A restore is drunk when the prayer missing is at least one dose's worth
-- (no dose wasted) or prayer is about to run out.  Returns eat, drink names.
function QD.raid._play_supplies(st, v, threat)
    local eat, drink = nil, nil
    local food, heal = nil, 0
    for _, row in ipairs(QD.RAID_PLAY_FOOD) do
        local cr, n = QD.inv.count(row.item)
        if food == nil and cr == "ok" and n > 0 then
            food = row.item
            heal = row.heal
        end
    end
    local brew = nil
    for _, name in ipairs(QD.RAID_PLAY_BREWS) do
        local cr, n = QD.inv.count(name)
        if brew == nil and cr == "ok" and n > 0 then brew = name end
    end
    local next_swing = QD.raid._play_next_swing(st, v)
    local free = (not st.engaged) or next_swing <= v.tick
    local eat_ready = v.tick - st.last_eat >= QD.RAID_PLAY_EAT_DELAY
    local drink_ready = v.tick - st.last_drink >= QD.RAID_PLAY_DRINK_DELAY
    -- the next chance to eat: a free tick's next free tick is the next swing
    -- (engaged) or the eat delay away, and a bite pressed then is confirmed
    -- up to QD.TOGETHER_CONFIRM_TICKS later (measured: svbbloat's bite on
    -- the swing two ticks before the stomp was not yet read one tick later),
    -- plus one tick to spare: the seam's margin, no source gives one
    -- (PLAY_NOTES.md "Supplies").  Between swings only a hit that can land
    -- before the next tick's bite is read forces one.
    local horizon = 2
    if free then
        horizon = (st.engaged and st.weapon.speed or QD.RAID_PLAY_EAT_DELAY) + QD.TOGETHER_CONFIRM_TICKS + 1
    end
    local need = threat(horizon)
    if v.hp <= need then
        if eat_ready and food ~= nil then
            eat = food
            if v.hp + heal <= need and drink_ready and brew ~= nil then drink = brew end
        elseif drink_ready and brew ~= nil then
            drink = brew
        end
    end
    if drink == nil and drink_ready then
        local missing = v.prayer_base - v.prayer
        if missing >= QD.RAID_PLAY_RESTORE_AMOUNT or v.prayer <= 2 then
            for _, name in ipairs(QD.RAID_PLAY_RESTORES) do
                local cr, n = QD.inv.count(name)
                if drink == nil and cr == "ok" and n > 0 then drink = name end
            end
        end
    end
    return eat, drink, need
end

-- SKILL: HAZARDS.  A tile is dangerous from the tick a person can see its
-- marker (Bloat's shadow: ENCOUNTER_TIMING.md 3.4, judged on the previous
-- tick's tile, so a step on the tick it is seen lands in time).  Returns the
-- tile to stand on: `want` if it is safe, else the safe tile nearest it, the
-- shortest step from the player breaking ties.  `ok(x, z)` says a tile is
-- floor the player may stand on.
function QD.raid._play_hazard(st, v, want_x, want_z, ok)
    local key = want_x * 100000 + want_z
    if not v.shadows[key] and ok(want_x, want_z) then
        return want_x, want_z, false
    end
    local best, bx, bz = nil, v.me.x, v.me.z
    for r = 1, 3 do
        for dx = -r, r do
            for dz = -r, r do
                local x, z = want_x + dx, want_z + dz
                if ok(x, z) and not v.shadows[x * 100000 + z] then
                    local score = math.max(math.abs(dx), math.abs(dz)) * 10
                        + math.max(math.abs(x - v.me.x), math.abs(z - v.me.z))
                    if best == nil or score < best then
                        best, bx, bz = score, x, z
                    end
                end
            end
        end
        if best ~= nil then break end
    end
    return bx, bz, true
end

-- SKILL: A SAFE STEP (raid seam29).  A walk is judged where the player stands
-- at the END of each tick on the way, not only where it stops: a hand lands
-- on whoever stood on its tile at the end of the tick before it (ET 3.4, the
-- T-1 rule of ET 1.1), and the server moves a path one tile a tick walking,
-- two running.  Seam27's Normal trio (build/quest_gate/playn3, ticks 437-440)
-- lost p3 that way: shadows at t437, p3 stepped off 6435,92 to 6435,93, the
-- attack press pathed it back across 6435,92 at t439, the hand landed at t440.
-- Returns the tile to send the walk to THIS tick: `want` itself when it and
-- the first two tiles of both route shapes toward it carry no marker (the
-- server's route on open floor was seen both ways: diagonal first, and
-- straight along the longer axis first, svaplaysmoke t164-166 6426,94 ->
-- 6426,96 toward 6425,101); else the unmarked floor tile within two that gets
-- nearest `want`, with every tile a route to it can end a tick on unmarked
-- (for a two-tile move the first step of either shape), the shorter move
-- breaking ties; the player's own tile (no walk) when none gets nearer and
-- the player's own tile is not marked.  `ok(x, z)` as for _play_hazard.
--
-- Measured and kept (seam29): three other shapes were tried and dropped.  A
-- one-tile walk whenever a marker was within three tiles walked the Normal
-- trio at half speed behind a running Bloat (s29n3b t345-366: no hand, all
-- three dead to flies by t369); a two-tile version of it and a "never stay
-- while a walk is in flight" rule each put two hands on svbplaysmoke (t119,
-- t196) that this one does not.
function QD.raid._play_safe_step(st, v, want_x, want_z, ok)
    local mx, mz = v.me.x, v.me.z
    local function marked(x, z) return v.shadows[x * 100000 + z] == true end
    local function sign(n) if n > 0 then return 1 elseif n < 0 then return -1 end return 0 end
    local function next_tile(x, z, straight)
        local dx, dz = want_x - x, want_z - z
        if straight and math.abs(dx) > math.abs(dz) then return x + sign(dx), z end
        if straight and math.abs(dz) > math.abs(dx) then return x, z + sign(dz) end
        return x + sign(dx), z + sign(dz)
    end
    local clear = not marked(want_x, want_z)
    for _, straight in ipairs({ false, true }) do
        local s1x, s1z = next_tile(mx, mz, straight)
        local s2x, s2z = next_tile(s1x, s1z, straight)
        if marked(s1x, s1z) or marked(s2x, s2z) then clear = false end
    end
    if clear then
        return want_x, want_z, false
    end
    local here = math.max(math.abs(want_x - mx), math.abs(want_z - mz))
    local best, bx, bz = nil, mx, mz
    for dx = -2, 2 do
        for dz = -2, 2 do
            local x, z = mx + dx, mz + dz
            local route = true
            if math.max(math.abs(dx), math.abs(dz)) == 2 then
                local fx, fz = mx + sign(dx), mz + sign(dz)
                local lx, lz = x - sign(dx), z - sign(dz)
                route = ok(fx, fz) and not marked(fx, fz) and not marked(lx, lz)
            end
            if (dx ~= 0 or dz ~= 0) and ok(x, z) and route and not marked(x, z) then
                local left = math.max(math.abs(want_x - x), math.abs(want_z - z))
                local score = left * 10 + math.max(math.abs(dx), math.abs(dz))
                if (left < here or marked(mx, mz)) and (best == nil or score < best) then
                    best, bx, bz = score, x, z
                end
            end
        end
    end
    return bx, bz, true
end

-- SKILL: THE ATTACK'S OWN PATH (raid seam29).  An attack press on an npc out
-- of reach makes the SERVER path the player to it, and that path goes through
-- no skill: seam27's playn3 p3 died on it.  Melee reaches an npc of size n at
-- south-west tile (bx, bz) from a tile sharing an edge with its footprint
-- (the row's `size`: Bloat's "5x5 area", Mod Ash quoted in ET 3.3), never a
-- diagonal corner -- the seam's conservative choice, no source here states
-- it: leaving a corner out only ever picks an edge tile instead.  While no
-- marker is on the floor the press goes
-- out as it always did (nil).  While one is: from a reach tile that is not
-- marked, the press too (nil, it paths nowhere); otherwise the tile to WALK to
-- (the unmarked floor reach tile nearest the player, through
-- _play_safe_step), and the press waits for the player to stand on it.  When
-- every reach tile is marked: nil, nil, true -- hold the press this tick.
function QD.raid._play_reach(st, v, ok)
    local b = v.boss
    assert(b ~= nil, "raid.play: _play_reach is asked only with the boss in view")
    assert(type(b.size) == "number", "raid.play: the npc row has no size (a binary before raid seam4)")
    if next(v.shadows) == nil then
        return nil, nil, false
    end
    local n = b.size
    local function reach(x, z)
        local along_x = x >= b.x and x <= b.x + n - 1
        local along_z = z >= b.z and z <= b.z + n - 1
        return (along_x and (z == b.z - 1 or z == b.z + n)) or (along_z and (x == b.x - 1 or x == b.x + n))
    end
    local mx, mz = v.me.x, v.me.z
    if reach(mx, mz) and not v.shadows[mx * 100000 + mz] then
        return nil, nil, false
    end
    local best, rx, rz = nil, nil, nil
    for x = b.x - 1, b.x + n do
        for z = b.z - 1, b.z + n do
            if reach(x, z) and ok(x, z) and not v.shadows[x * 100000 + z] then
                local score = math.max(math.abs(x - mx), math.abs(z - mz)) * 100 + math.abs(x - mx) + math.abs(z - mz)
                if best == nil or score < best then
                    best, rx, rz = score, x, z
                end
            end
        end
    end
    if best == nil then
        return nil, nil, true
    end
    return rx, rz, false
end

-- SEND: the tick's whole intent, together (prayers first, potions and food,
-- the step last: DRIVER_NOTES "How a fight loop is written now"); the attack
-- press after the block (a slow verb may not sit inside one).
function QD.raid._play_send(st, v, intent)
    local all = {}
    for _, name in ipairs(st.plan.walk_prayers) do all[#all + 1] = name end
    for _, name in ipairs(st.plan.down_prayers) do all[#all + 1] = name end
    local switches = QD.raid._play_pray(st, v, intent.want, all)
    local eat, drink, walk = intent.eat, intent.drink, intent.walk
    -- raid seam32 play_tob_bloat_normal: THE LOADOUT.  `intent.gear` is a list
    -- of worn items to put on in this tick's block (held items, after the
    -- food, before the step: the block's tab order), so a swap rides the same
    -- tick as the step that leaves ("the scythe back the same tick").  A plan
    -- that sets no gear sends exactly what it sent before.
    local gear = intent.gear or {}
    local n = 0
    if #switches > 0 or eat ~= nil or drink ~= nil or walk ~= nil or #gear > 0 then
        local r, d = QD.together(function()
            for _, s in ipairs(switches) do QD.prayer.set(s[1], s[2]) end
            if drink ~= nil then QD.player.drink(drink) end
            if eat ~= nil then QD.player.eat(eat) end
            for _, item in ipairs(gear) do QD.player.equip(item) end
            if walk ~= nil then QD.player.walk_to(walk.x, walk.z, 1) end
        end)
        n = #switches + (eat and 1 or 0) + (drink and 1 or 0) + (walk and 1 or 0) + #gear
        if #gear > 0 then
            st.gear_swaps = st.gear_swaps or {}
            st.gear_swaps[#st.gear_swaps + 1] = { tick = v.tick, items = table.concat(gear, "+"), result = r }
        end
        st.blocks[r] = (st.blocks[r] or 0) + 1
        if r ~= "ok" and r ~= "split" then
            st.refusals = st.refusals + 1
            if #st.lines < 6 then st.lines[#st.lines + 1] = "t" .. v.tick .. " " .. tostring(r) .. ": " .. string.sub(tostring(d), 1, 160) end
        end
        if eat ~= nil then
            st.last_eat = v.tick
            st.eats[#st.eats + 1] = { tick = v.tick, item = eat, hp = v.hp, need = intent.need }
        end
        if drink ~= nil then
            st.last_drink = v.tick
            st.drinks[#st.drinks + 1] = { tick = v.tick, item = drink, hp = v.hp, prayer = v.prayer }
        end
        if walk ~= nil then
            st.engaged = false
            st.walk_target = walk
        end
    end
    if intent.spec then
        -- raid seam32: THE SPECIAL.  Armed from the minimap orb, never the
        -- combat tab's bar (DRIVER_NOTES "Bloat: Defence reads 80 of 80 after a
        -- Dragon warhammer special": the bar pressed while idle does nothing),
        -- on the tick the plan presses the attack that should carry it.  The
        -- plan proves it fired by the energy it spends (varp300), never by
        -- varp301 or the first splat (the same section).
        local wr, wid = QD.ui.widget("orbs:specbutton")
        local ir = "no widget"
        if wr == "ok" then ir = QD.ui.invoke(wid, 1) end
        n = n + 1
        st.spec_arms = st.spec_arms or {}
        st.spec_arms[#st.spec_arms + 1] = { tick = v.tick, result = tostring(ir) }
    end
    if intent.attack then
        -- raid seam31: through the library's press (true answers): an Attack
        -- row that landed is engaged whether or not its hit showed inside the
        -- one-tick settle ("pressed"), so it is not pressed again next tick.
        local ar, why = QD.raid._play_press(st, v, { symbol = st.boss_symbol, op = 2 })
        n = n + 1
        st.attack_presses = (st.attack_presses or 0) + 1
        if ar == "ok" or ar == "pressed" then
            st.engaged = true
            st.engaged_tick = v.tick
            st.walk_target = nil
        elseif #st.lines < 6 then
            st.lines[#st.lines + 1] = "t" .. v.tick .. " attack " .. tostring(ar) .. ": " .. tostring(why)
        end
    end
    st.inputs[v.tick] = (st.inputs[v.tick] or 0) + n
end

-- ==========================================================================
-- raid seam31 play_library_faults, (3): THE PRESS AND ITS TRUE ANSWER.
--
--   QD.raid._play_press(st, v, spec) -> answer, reason, raw_result, raw_detail
--   spec = { symbol = <npc symbol>, slot = <client slot or nil>, op = 2,
--            spell = <spell symbol or nil> }
--
-- One quick press (t.player.attack / t.player.cast with `quick = true` and a
-- one-tick settle: combat.lua QD._combat_press_quick, spell.lua
-- QD.player._cast_press, pointer.lua QD.drive._press_quick).  What those
-- verbs answer, read (seam30 Nylocas: presses "timeout" 125-159 a run and
-- still landing, "refused" up to 38):
--   * `timeout` is "the row WAS pressed and no hit showed inside the ONE tick
--     the settle waits" (combat.lua t.player.attack banner: "A `timeout` is
--     NOT the click failed").  A bow or a spell cannot hit inside one tick
--     (the projectile's flight), so for a ranged or magic press it is the
--     ordinary answer to a press that landed.  Here it is `pressed`, and the
--     press is WATCHED: it answers `ok` on the tick its effect shows (a new
--     hitsplat on the copy pressed, or the copy leaving the pool), or
--     `unconfirmed` when nothing showed in QD.RAID_PLAY_PRESS_CONFIRM ticks.
--   * `refused` carries a one-line reason, the first thing that said no:
--     the server's own sentence ("server: That target is already frozen."),
--     the row pressed when it was not the Attack row, or "the spell was not
--     armed when the menu opened" (pointer.lua _select_row_is_held).  It is
--     counted by reason (st.press_reasons) and never re-pressed here: the
--     plan decides what to press next.
--   * everything else (`covered`, `not_visible`, `no_row`, `no_runes`) is
--     the verb's own word and its first clause.
-- st.press_answers counts the FINAL answers (ok / unconfirmed / refused / ...),
-- st.press_log keeps the first 40 presses (tick, answer, effect tick).
-- ==========================================================================
-- Ticks a pressed press may take to show its effect: the slowest weapon the
-- plans carry is 5 ticks (the scythe; wiki Attack speed) and the longest
-- flight in a room is 4-5 ticks (ENCOUNTER_TIMING.md 1.2: f = cycles / 30),
-- plus the press's own tick.
QD.RAID_PLAY_PRESS_CONFIRM = 10

-- (answer, reason) for one raw verb answer.  Pure: no reads.
function QD.raid._play_press_answer(r, d)
    local text = tostring(d)
    if r == "ok" then
        return "ok", nil
    end
    if r == "timeout" then
        local row = string.match(text, "%[(.-)%]")
        return "pressed", "row '" .. tostring(row) .. "' pressed; its hit had not shown inside the one-tick settle"
    end
    if r == "refused" then
        local who, said = string.match(text, "the SERVER refused the (%a+): '([^']*)'")
        if said ~= nil then
            return "refused", "server refused the " .. who .. ": " .. said
        end
        local other = string.match(text, "pressed '([^']*)', which is not an Attack row")
        if other ~= nil then
            return "refused", "pressed a row that is not Attack: " .. other
        end
        local ordinary = string.match(text, "pressed '([^']*)', an ordinary op row and not the held%-item row")
        if ordinary ~= nil then
            return "refused", "the spell was not armed when the menu opened (pressed " .. ordinary .. ")"
        end
        return "refused", string.sub(text, 1, 140)
    end
    local first = string.match(text, "^(.-) %-%- ") or text
    return tostring(r), string.sub(first, 1, 140)
end

function QD.raid._play_press(st, v, spec)
    assert(type(spec) == "table", "raid._play_press: spec must be a table")
    assert(type(spec.symbol) == "string", "raid._play_press: spec.symbol must be an npc symbol")
    local opts = { quick = true }
    if spec.slot ~= nil then
        opts.slot = spec.slot
    end
    local r, d
    if spec.spell ~= nil then
        r, d = QD.player.cast(spec.spell, spec.symbol, 1, spec.op or 2, opts)
    else
        r, d = QD.player.attack(spec.symbol, spec.op or 2, 1, opts)
    end
    local answer, reason = QD.raid._play_press_answer(r, d)
    st.press_answers = st.press_answers or {}
    st.press_reasons = st.press_reasons or {}
    st.press_log = st.press_log or {}
    st.press_pending = st.press_pending or {}
    local entry = { tick = v.tick, answer = answer, spell = spec.spell }
    if answer == "pressed" then
        local slot = spec.slot or tonumber(string.match(tostring(d), "watching slot (%-?%d+)"))
        local hit = nil
        if slot ~= nil then
            local rr, row = QD._combat_row_by_slot(slot)
            if rr == "ok" and row ~= nil then
                hit = row.hit_cycle
            end
        end
        entry.slot = slot
        entry.hit_cycle = hit
        st.press_pending[#st.press_pending + 1] = entry
    else
        st.press_answers[answer] = (st.press_answers[answer] or 0) + 1
        if answer ~= "ok" and reason ~= nil then
            local key = answer .. ": " .. reason
            st.press_reasons[key] = (st.press_reasons[key] or 0) + 1
        end
        entry.effect = (answer == "ok") and v.tick or nil
    end
    if #st.press_log < 40 then
        st.press_log[#st.press_log + 1] = entry
    end
    return answer, reason, r, d
end

-- Called by the SEE step every tick: a pressed press answers `ok` on the tick
-- its effect shows (a new hitsplat on the copy pressed, or the copy gone from
-- the pool: what a person at the screen sees), `unconfirmed` after
-- QD.RAID_PLAY_PRESS_CONFIRM ticks of nothing.
function QD.raid._play_press_confirm(st, v)
    if st.press_pending == nil or #st.press_pending == 0 then
        return
    end
    local nr, rows = api_drive.npcs(0)
    if nr ~= "ok" then
        return
    end
    local by_slot = {}
    for _, row in ipairs(rows) do
        by_slot[row.slot] = row
    end
    local keep = {}
    for _, p in ipairs(st.press_pending) do
        local row = p.slot ~= nil and by_slot[p.slot] or nil
        local done = nil
        if p.slot ~= nil and row == nil then
            done = "ok"
            p.gone = true
        elseif row ~= nil and p.hit_cycle ~= nil and row.hit_cycle > p.hit_cycle then
            done = "ok"
        elseif v.tick - p.tick >= QD.RAID_PLAY_PRESS_CONFIRM then
            done = "unconfirmed"
        end
        if done ~= nil then
            p.answer = done
            if done == "ok" then p.effect = v.tick end
            st.press_answers[done] = (st.press_answers[done] or 0) + 1
            if done == "ok" then
                st.press_lag = st.press_lag or {}
                local lag = v.tick - p.tick
                st.press_lag[lag] = (st.press_lag[lag] or 0) + 1
            end
        else
            keep[#keep + 1] = p
        end
    end
    st.press_pending = keep
end

-- One clause: the final answers, the effect lag histogram, the refusal reasons.
function QD.raid._play_press_text(st)
    local a = {}
    for k, c in pairs(st.press_answers or {}) do a[#a + 1] = k .. " " .. c end
    table.sort(a)
    local lag = {}
    for k, c in pairs(st.press_lag or {}) do lag[#lag + 1] = { k, c } end
    table.sort(lag, function(x, y) return x[1] < y[1] end)
    local lt = {}
    for _, e in ipairs(lag) do lt[#lt + 1] = "+" .. e[1] .. "x" .. e[2] end
    local rs = {}
    for k, c in pairs(st.press_reasons or {}) do rs[#rs + 1] = k .. " x" .. c end
    table.sort(rs)
    while #rs > 4 do table.remove(rs) end
    return "[" .. table.concat(a, ", ") .. "] effect lag [" .. table.concat(lt, " ") .. "] pending "
        .. tostring(#(st.press_pending or {})) .. (#rs > 0 and (" reasons {" .. table.concat(rs, " | ") .. "}") or "")
end

-- One turn of the loop: SEE, stop if the room is over, DECIDE, SEND, then
-- wait for the next server tick (the loop's beat, never a wait for an effect).
function QD.raid._play_tick(st)
    local v = QD.raid._play_see(st)
    for _, f in ipairs(st.flinches) do
        if f.moved == nil and v.tick >= f.tick + 3 then
            f.moved = math.max(math.abs(v.me.x - f.from.x), math.abs(v.me.z - f.from.z))
        end
    end
    -- A jump of more than 20 tiles is a death's respawn, EXCEPT a teleport the
    -- room makes on purpose that the plan saw coming (raid seam30
    -- play_tob_sotetseg: Sotetseg's portal moves the runner to the shadow
    -- realm and back; the plan sets st.teleport_until from the portal
    -- animation, W:23 "a bright white light").  A plan that never sets it
    -- (bloat, maiden, nylocas) is judged exactly as before.
    local jumped = st.last_me ~= nil and math.abs(v.me.x - st.last_me.x) + math.abs(v.me.z - st.last_me.z) > 20
    if jumped and st.teleport_until ~= nil and v.tick <= st.teleport_until then
        jumped = false
    end
    if v.hp <= 0 or jumped then
        st.end_tick = v.tick
        return "died"
    end
    if v.boss ~= nil then
        st.boss_seen = true
        st.boss_gone = 0
        if st.log and st.boss_slot == nil then
            local sr, slot = QD.ticklog.slot(v.boss)
            if sr == "ok" then
                st.boss_slot = slot
                -- raid seam31 play_library_faults, FAULT 2: a server slot is
                -- reused, so a death row already on it is an EARLIER npc's
                -- (raid seam30 ny30h: Vasilias took slot 1079, where a wave
                -- nylocas had died, and the play ended on her spawn tick
                -- t644).  Only a death after the boss was first seen counts.
                local dr, rows = QD.ticklog.rows({ kind = "npc_death", slot = slot, since = st.death_serial })
                if dr == "ok" then
                    for _, row in ipairs(rows) do
                        st.death_serial = math.max(st.death_serial, row.serial)
                    end
                    st.death_serial_seeded = { tick = v.tick, serial = st.death_serial, earlier = #rows }
                end
            end
        end
    elseif st.boss_seen then
        st.boss_gone = st.boss_gone + 1
    end
    if st.log and st.boss_slot ~= nil then
        local dr, rows = QD.ticklog.rows({ kind = "npc_death", slot = st.boss_slot, since = st.death_serial })
        if dr == "ok" and #rows > 0 then
            st.death_tick = rows[1].tick
            st.end_tick = v.tick
            st.stop = "npc_death of slot " .. st.boss_slot .. " on t" .. st.death_tick
            return "ok"
        end
    end
    -- The room's own cleared state, when the plan reads one a person sees
    -- (the room's "complete!" line, the barrier dropping): optional.
    if st.plan.room_cleared ~= nil and QD.raid[st.plan.room_cleared](st, v) then
        st.end_tick = v.tick
        st.stop = "room cleared (" .. st.plan.room_cleared .. ") on t" .. v.tick
        return "ok"
    end
    -- raid seam31 play_library_faults, FAULT 4: "the boss is gone" is NOT
    -- "the boss is dead".  A retype changes the symbol the SEE step looks up
    -- (svdplaynyloc: Vasilias retyped t792 and kept attacking t795, t799; the
    -- loop answered ok on t793 with no npc_death row and the room never
    -- completed).  With the tick log and her slot known, only the death row
    -- (or the plan's room_cleared) ends the play; a gone boss is recorded and
    -- the plan plays on.  A member with no tick log, or a boss whose slot was
    -- never resolved, still ends on 3 ticks gone, and the stop says so.
    if st.boss_gone >= 3 then
        if st.log and st.boss_slot ~= nil then
            if st.boss_gone == 3 then
                st.gone_without_death = (st.gone_without_death or 0) + 1
                if #st.lines < 6 then
                    st.lines[#st.lines + 1] = "t" .. v.tick .. " boss " .. tostring(st.boss_symbol)
                        .. " gone 3 ticks with no npc_death on slot " .. st.boss_slot .. ": played on"
                end
            end
        else
            st.end_tick = v.tick
            st.stop = "boss gone 3 ticks, no tick log or no slot (not proved dead)"
            return "ok"
        end
    end
    local intent = QD.raid[st.plan.decide](st, v)
    QD.raid._play_send(st, v, intent)
    st.last_me = { x = v.me.x, z = v.me.z }
    local _, after = QD.tick()
    if after == v.tick then
        QD.ticks(1)
    end
    return nil
end

-- One line: what the play did, and the inputs-per-tick histogram.
function QD.raid._play_summary(st)
    local hist = { 0, 0, 0, 0 }
    local ticks = 0
    for _, n in pairs(st.inputs) do
        if n > 0 then
            ticks = ticks + 1
            local k = math.min(n, 4)
            hist[k] = hist[k] + 1
        end
    end
    local flinch = "none"
    if st.flinches[1] ~= nil then flinch = tostring(st.flinches[1].moved) .. " tiles" end
    local blocks = {}
    for r, c in pairs(st.blocks) do blocks[#blocks + 1] = r .. " " .. c end
    table.sort(blocks)
    return string.format("plan %s mode %s role %d: %s after %d ticks (tick %d..%s); %d downs, %d swings, %d eats, "
        .. "%d drinks, %d shadow steps, first flinch %s; inputs on %d ticks: 1 on %d, 2 on %d, 3 on %d, 4+ on %d; "
        .. "attack presses %d; blocks [%s]; %s",
        st.plan_id, st.mode, st.role, tostring(st.result), (st.end_tick or st.start_tick) - st.start_tick,
        st.start_tick, tostring(st.end_tick), #st.downs, #st.swings, #st.eats, #st.drinks, st.dodges, flinch,
        ticks, hist[1], hist[2], hist[3], hist[4], st.attack_presses or 0, table.concat(blocks, ", "),
        #st.lines > 0 and table.concat(st.lines, " | ") or "no refusals")
        .. QD.raid._play_summary_seam31(st)
end

-- raid seam31 play_library_faults: the stop's reason, the prayer offs the
-- exclusion rule kept back, and the press answers (QD.raid._play_press).
-- Appended so every line the summary printed before keeps its bytes.
function QD.raid._play_summary_seam31(st)
    local parts = {}
    if st.stop ~= nil then parts[#parts + 1] = "stop: " .. st.stop end
    if (st.pray_skips or 0) > 0 then
        parts[#parts + 1] = "prayer offs kept back (the server put them out) " .. st.pray_skips
    end
    if (st.gone_without_death or 0) > 0 then
        parts[#parts + 1] = "boss gone with no death row " .. st.gone_without_death .. " time(s)"
    end
    if st.press_answers ~= nil then
        parts[#parts + 1] = "presses " .. QD.raid._play_press_text(st)
    end
    if #parts == 0 then
        return ""
    end
    return "; " .. table.concat(parts, "; ")
end
