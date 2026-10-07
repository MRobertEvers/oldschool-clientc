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
-- member (no tick log) reads the swings its own screen shows it start
-- (t.raid.own_anim, raid seam48 member_swings_seen).
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
-- 30 client cycles per game tick (ENCOUNTER_TIMING.md 1.2:
-- CYCLES_PER_GAME_TICK = 600/20; src/app.h APP_SERVER_TICK_LOGIC_CYCLES).
QD.RAID_PLAY_CYCLES_PER_TICK = 30

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
--   opts.role    the plan's role number this raider plays (default its party
--                seat, t.party.role()).  raid seam39 play_tob_normal_relay: a
--                seat is not a role.  The whole-raid relay carries ONE kit per
--                seat, and the seat whose pack has room for the most food
--                tanks Maiden (the plan's role 1 is the tank: 26 of 27
--                blackstorms on the harness's p1) though it is seat 3.  A
--                party of one ignores it (no roles).
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
        boss_symbol = plan.boss[mode], role = opts.role or QD.party.role(), party = QD.party.size(),
        start_tick = now, origin = { x = math.floor(me.x / 64) * 64, z = math.floor(me.z / 64) * 64 },
        -- what happened, per server tick (the room test's rows read these)
        inputs = {}, hp_at = {}, prayer_at = {}, tile_at = {}, swings = {}, eats = {}, drinks = {},
        downs = {}, flinches = {}, dodges = 0, blocks = {}, refusals = 0, lines = {},
        last_swing = -1000, engaged = false, engaged_tick = -1000, last_eat = -1000, last_drink = -1000,
        walk_target = nil, boss_seen = false, boss_gone = 0, boss_slot = nil, anim_serial = 0,
        death_serial = 0, log = false, my_pid = nil,
        -- raid seam48: the own-animation starts read so far (t.raid.own_anim)
        -- and the weapon swings among them, on every raider; a member's
        -- swings ARE these, the leader keeps its log's and records these
        -- beside them (the summary's "seen" count)
        own_starts = 0, seen_swings = {},
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
    -- the starts before this room are not this room's swings
    local orr, own = QD.raid.own_anim()
    if orr == "ok" then st.own_starts = own.starts end
    -- raid seam55: the plan's triggers and watches (QD.raid._play_triggers_init)
    QD.raid._play_triggers_init(st, opts)
    return st
end

-- t.raid.own_anim() -> "ok", {seq, tick, starts, history, anim} |
-- not_found | unsupported.  Raid seam48 member_swings_seen: what this
-- raider's own screen shows its character START playing -- the action seq
-- the server sent it, as the model draws it (api_drive.players' `me` row,
-- watched every frame by torirs_plugin_drive.c drive_own_animation_watch).
-- `seq`/`tick`: the newest start (-1 before the first); `starts`: how many
-- this client has seen; `history`: the newest starts, oldest first, as
-- {n, seq, tick} (n counts from 1, so a reader keeps the last n it took);
-- `anim`: the seq drawn now (-1 none).  Ticks are t.tick's (the server
-- tick, or the party's lockstep tick on a member): the tick read now less
-- the whole ticks the start is old (its age in client cycles / 30), so a
-- member's swing and the leader's log row sit on one axis.  A party member has no tick log; before this it COUNTED
-- a swing every weapon-speed ticks and never re-pressed after the server
-- stopped its swings (seam42 _play_bloat: p3 in reach, no input for 20
-- ticks of a down, twice).  What the screen shows and no more: a seq a hit's
-- higher-priority block seq refused, or one re-sent while still playing
-- under replay mode 2, shows no start.
function QD.raid.own_anim()
    local pr, rows = api_drive.players()
    if pr ~= "ok" then
        return pr, "t.raid.own_anim: api_drive.players answered " .. tostring(pr)
    end
    local tr, now = QD.tick()
    if tr ~= "ok" then
        now = api_drive.tick()
    end
    for _, row in ipairs(rows) do
        if row.me then
            if row.seq_starts == nil then
                return "unsupported", "t.raid.own_anim: this binary's api_drive.players has no seq_starts (rebuild)"
            end
            local history = {}
            for _, h in ipairs(row.seq_history) do
                history[#history + 1] = { n = h.n, seq = h.seq, tick = now - h.age // QD.RAID_PLAY_CYCLES_PER_TICK }
            end
            local newest = history[#history]
            return "ok", {
                seq = row.seq, tick = newest and newest.tick or -1,
                starts = row.seq_starts, history = history, anim = row.anim,
            }
        end
    end
    return "not_found", "t.raid.own_anim: this client has not placed its own player yet"
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
    -- the swing animation of the player's own weapon (player_anim, own pid).
    -- SOLO only (raid seam48): in a party the library's st.my_pid is
    -- api_drive.players' pid, which is not the log's (the maiden seam32
    -- finding, s32mzn1), so the leader read another raider's swings as its
    -- own (seam48 survey _play_bloat: the leader's log pid 0 swung 13 times,
    -- the library counted 12, pid 1's).  In a party every raider, the leader
    -- included, takes its swings from its own screen (below), which matched
    -- the log tick for tick (scratch s48oa2: 7 of 7, offset 0).
    if st.log and (st.party or 1) <= 1 then
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
    -- raid seam48: the swings this raider's own screen shows it start
    -- (t.raid.own_anim).  A member has no log, and a party leader's log read
    -- cannot tell its own pid (above): for both, these are its swings.
    local orr, own = QD.raid.own_anim()
    if orr == "ok" then
        for _, h in ipairs(own.history) do
            if h.n > st.own_starts then
                st.own_starts = h.n
                if st.weapon.seqs[h.seq] then
                    st.seen_swings[#st.seen_swings + 1] = h.tick
                    if not (st.log and (st.party or 1) <= 1) and h.tick > st.last_swing then
                        st.last_swing = h.tick
                        st.swings[#st.swings + 1] = h.tick
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
-- the player is not engaged, or no swing was seen inside the speed window:
-- the swing due `speed` ticks after the last one (or after the press) did
-- not show, and one tick of grace for the frame it shows on, so the press
-- goes out the tick after that (raid seam48; it was speed + 2 before).
-- Every swing here is SEEN: the leader's from its tick log, every raider's
-- from its own screen (t.raid.own_anim, _play_see; raid seam48: a member
-- used to count a phantom swing every `speed` ticks of engagement, so the
-- re-press below never fired for it after the server stopped its swings).
-- Returns true when this tick should carry an attack press.
function QD.raid._play_attack(st, v, want)
    if not want then
        return false
    end
    local speed = st.weapon.speed
    if not st.engaged then
        return true
    end
    local seen = st.seen_swings[#st.seen_swings] or -1000
    return v.tick - math.max(st.last_swing, seen, st.engaged_tick) > speed + 1
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
    -- raid seam55: a trigger's press on a named row, or a cast on one (the
    -- freezer's barrage on the crab its crab_spawn picked), through the same
    -- library press and its true answer
    for _, f in ipairs({ "press", "cast" }) do
        local p = intent[f]
        if p ~= nil then
            local ar, why = QD.raid._play_press(st, v, { symbol = p.symbol, slot = p.slot, op = p.op or 2, spell = p.spell })
            n = n + 1
            st.trig_presses = st.trig_presses or {}
            if #st.trig_presses < 80 then
                st.trig_presses[#st.trig_presses + 1] = { tick = v.tick, field = f, slot = p.slot, spell = p.spell, answer = tostring(ar), why = p.why }
            end
            if ar == "ok" or ar == "pressed" then
                if f == "press" then st.engaged, st.engaged_tick = true, v.tick end
                st.walk_target = nil
            elseif #st.lines < 6 then
                st.lines[#st.lines + 1] = "t" .. v.tick .. " " .. f .. " " .. tostring(ar) .. ": " .. string.sub(tostring(why), 1, 120)
            end
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
-- ==========================================================================
-- SEAM play_tob_maiden_triggers_like_blert (raid seam55, 2026-10-06) --
-- TRIGGERS AND WATCHES.  The owner, 2026-10-06: "You need to do what the
-- blert raid players do and you need to add triggers and watch capabilities
-- to the script api so you can do so."
--
--   st.on(event, handler)              handler(st, v, ev) -> intent | nil
--   st.watch(name, reader, on_change)  reader(st, v) -> value;
--                                      on_change(st, v, value, old) -> intent | nil
--   QD.raid.watch(st, name, reader, on_change)   the same, as a library call
--
-- Every server tick, before the plan's decide, the loop DIFFS what the
-- client shows against the previous tick (QD.raid._play_events) and raises
-- the events below; each registered handler, then each watch whose reading
-- changed, may answer an INTENT for this tick: the fields of the plan's own
-- intent (walk, attack, press = {symbol, slot, op}, cast = {spell, symbol,
-- slot}, want = {prayer = bool}, eat, drink, gear, spec) plus `pri` (default
-- 1) and `why`.  THE FOLD (QD.raid._play_fold): field by field, the highest
-- `pri` wins (a tie keeps the first registered); `want` merges prayer by
-- prayer the same way.  The plan's decide then runs with v.events (this
-- tick's events) and v.trigger (the folded intent) in view, and its intent
-- is the DEFAULT: every field the fold set replaces the default's.  A walk
-- and a press/cast in one tick cannot both land (the press's path replaces
-- the step on the server), so the walk is sent and the press/cast is HELD to
-- the next tick at its own priority (st.trig_hold; "step and KEEP the
-- cast"), dropped after QD.RAID_PLAY_HOLD_TICKS.
--
-- The events, all from the client's own view (npc rows, projectiles,
-- spotanims, its own hitpoints), configured by the plan's `events` table:
--   <add>_spawn  {slot, x, z, dx, dz, wave, index, label}  a new add row
--                (events.add names the plan key whose [mode] is the add's
--                symbol; `add_event` its event prefix, "crab" -> crab_spawn).
--                dx/dz from the boss's SW tile; `wave` counts spawn bursts
--                (a gap of more than 3 ticks starts a new one), `index` the
--                n-th of its burst, `label` events.label(dx, dz) if given.
--   <add>_walk   {slot, x, z, gap, was}   its tile moved closer to the boss
--   <add>_frozen {slot, x, z, how}        how = "graphic" (a spotanim in
--                events.freeze_spotanims landed on it) or "halted" (it had
--                walked and did not move this tick); once per freeze
--   <add>_gone   {slot, x, z, at_her}     its row left the scene; at_her = it
--                was within one tile of her footprint (a leak, not a kill)
--   <proj>       events.projectiles[spotanim] names it: {x, z, cycles, ticks,
--                mine, near}: a projectile row not there last tick (by
--                spotanim and destination); `mine` = it lands on this tile
--   <pool>       events.pools[spotanim]: {x, z, mine} a ground spotanim new
--                at a tile this tick
--   <boss seq>   events.boss_seqs[seq]: {seq, tick, target} the boss started
--                that sequence on a new seq_tick (target = the row's
--                interacting target when the binary carries one)
--   hit_taken    {amount, hp, was}  own hitpoints fell since last tick
--                (net of food eaten in between; the client sees the bar and
--                the number, not who hit)
--   boss_phase   {from, to, symbol}  the boss's npc id changed (a retype)
-- The ticklog: the loop records per tick which events fired and which
-- intent won each field (st.trig_log, the record's `triggers`, and one
-- summary clause); the leader also writes a ticklog MARK on a tick a
-- trigger's intent won ("trig <event>:<field>"), when opts.trigger_marks.
-- ==========================================================================
QD.RAID_PLAY_HOLD_TICKS = 2
QD.RAID_PLAY_INTENT_FIELDS = { "walk", "attack", "press", "cast", "eat", "drink", "gear", "spec" }

function QD.raid._play_triggers_init(st, opts)
    st.handlers, st.watches, st.trig_log, st.trig_counts, st.trig_wins = {}, {}, {}, {}, {}
    st.ev = { adds = {}, add_ids = nil, projs = {}, pools = {}, boss_seq = nil, boss_id = nil, hp = nil,
        wave = 0, wave_tick = -1000, wave_n = 0 }
    st.on = function(name, handler)
        assert(type(name) == "string", "st.on: an event name is a string")
        assert(handler ~= nil, "st.on: no handler for " .. name)
        local list = st.handlers[name]
        if list == nil then
            list = {}
            st.handlers[name] = list
        end
        list[#list + 1] = handler
    end
    st.watch = function(name, reader, on_change)
        QD.raid.watch(st, name, reader, on_change)
    end
    if st.plan.on_start ~= nil then QD.raid[st.plan.on_start](st) end
    if opts ~= nil and opts.on ~= nil then
        for name, handler in pairs(opts.on) do st.on(name, handler) end
    end
    st.trigger_marks = opts ~= nil and opts.trigger_marks == true
end

function QD.raid.watch(st, name, reader, on_change)
    assert(type(st) == "table", "t.raid.watch: no play state")
    assert(type(name) == "string", "t.raid.watch: a watch name is a string")
    assert(reader ~= nil, "t.raid.watch: no reader for " .. name)
    assert(on_change ~= nil, "t.raid.watch: no on_change for " .. name)
    st.watches[#st.watches + 1] = { name = name, reader = reader, on_change = on_change, primed = false }
end

function QD.raid._play_call(f, ...)
    if type(f) == "string" then return QD.raid[f](...) end
    return f(...)
end

-- The boss footprint's Chebyshev gap to a tile (0 = under or adjacent edge).
function QD.raid._play_gap(b, x, z)
    local size = b.size or 1
    local gx = math.max(b.x - x, 0, x - (b.x + size - 1))
    local gz = math.max(b.z - z, 0, z - (b.z + size - 1))
    return math.max(gx, gz)
end

-- DIFF: this tick's events, from what the client shows now against what it
-- showed last tick (st.ev).  Pure reads; nothing is sent.
function QD.raid._play_events(st, v)
    local E, ev = st.plan.events or {}, st.ev
    local out = {}
    local function raise(name, e)
        e.name = name
        out[#out + 1] = e
    end
    -- one npc scan serves the boss (every form the plan names, so a retype
    -- is seen on its own tick) and the adds
    local nr, rows = api_drive.npcs(0)
    if nr ~= "ok" then rows = {} end
    local b = v.boss
    if E.forms ~= nil then
        if ev.form_ids == nil then
            ev.form_ids = {}
            for _, sym in ipairs(st.plan[E.forms][st.mode]) do
                local r, id = api_drive.symbol("npc", sym)
                if r == "ok" then ev.form_ids[id] = true end
            end
        end
        for _, row in ipairs(rows) do
            if ev.form_ids[row.npc_id] or ev.form_ids[row.base_npc_id] then b = row end
        end
    end
    v.ev_boss = b
    -- own hitpoints
    if ev.hp ~= nil and v.hp < ev.hp then
        raise("hit_taken", { amount = ev.hp - v.hp, hp = v.hp, was = ev.hp })
    end
    ev.hp = v.hp
    -- the boss: a retype, a new sequence
    if b ~= nil then
        if ev.boss_id ~= nil and b.npc_id ~= ev.boss_id then
            raise("boss_phase", { from = ev.boss_id, to = b.npc_id, symbol = st.boss_symbol })
        end
        ev.boss_id = b.npc_id
        if E.boss_seqs ~= nil and b.seq_id ~= nil and E.boss_seqs[b.seq_id] ~= nil and b.seq_tick ~= ev.boss_seq then
            raise(E.boss_seqs[b.seq_id], { seq = b.seq_id, tick = b.seq_tick, target = b.target })
        end
        if b.seq_tick ~= nil then ev.boss_seq = b.seq_tick end
    end
    -- the adds: spawn, walk, frozen, gone
    if E.add ~= nil and b ~= nil then
        if ev.add_ids == nil then
            ev.add_ids = {}
            local sym = st.plan[E.add][st.mode]
            local r, id = api_drive.symbol("npc", sym)
            if r == "ok" then ev.add_ids[id] = true end
        end
        local pre = E.add_event or E.add
        local here = {}
        do
            for _, row in ipairs(rows) do
                if (ev.add_ids[row.npc_id] or ev.add_ids[row.base_npc_id]) and (row.health_ratio == nil or row.health_ratio ~= 0) then
                    here[row.slot] = true
                    local a = ev.adds[row.slot]
                    local dx, dz = row.x - b.x, row.z - b.z
                    if a == nil or a.gone then
                        if v.tick - ev.wave_tick > 3 then
                            ev.wave, ev.wave_n = ev.wave + 1, 0
                        end
                        ev.wave_tick = v.tick
                        ev.wave_n = ev.wave_n + 1
                        a = { x = row.x, z = row.z, first = v.tick, moved = false, frozen = false, spot = row.spotanim_tick }
                        ev.adds[row.slot] = a
                        local label = nil
                        if E.label ~= nil then label = QD.raid._play_call(E.label, dx, dz) end
                        a.label = label
                        raise(pre .. "_spawn", { slot = row.slot, x = row.x, z = row.z, dx = dx, dz = dz, wave = ev.wave,
                            index = ev.wave_n, label = label, row = row })
                    else
                        local moved = row.x ~= a.x or row.z ~= a.z
                        if moved then
                            local was = QD.raid._play_gap(b, a.x, a.z)
                            local gap = QD.raid._play_gap(b, row.x, row.z)
                            if gap < was then
                                raise(pre .. "_walk", { slot = row.slot, x = row.x, z = row.z, gap = gap, was = was, label = a.label, row = row })
                            end
                            a.moved, a.frozen = true, false
                        end
                        local graphic = E.freeze_spotanims ~= nil and row.spotanim_sent_id ~= nil
                            and E.freeze_spotanims[row.spotanim_sent_id] and row.spotanim_tick ~= a.spot
                        if not a.frozen and (graphic or (not moved and a.moved)) then
                            a.frozen = true
                            raise(pre .. "_frozen", { slot = row.slot, x = row.x, z = row.z, label = a.label,
                                how = graphic and "graphic" or "halted", row = row })
                        end
                        a.x, a.z = row.x, row.z
                    end
                    a.spot = row.spotanim_tick
                    -- (owner_tob_normal: its health bar, what the screen shows;
                    -- -1 / nil before the first hit draws one)
                    a.hr, a.hs = row.health_ratio, row.health_scale
                end
            end
        end
        for slot, a in pairs(ev.adds) do
            if not a.gone and not here[slot] then
                a.gone = true
                raise(pre .. "_gone", { slot = slot, x = a.x, z = a.z, label = a.label, at_her = QD.raid._play_gap(b, a.x, a.z) <= 1 })
            end
        end
    end
    -- projectiles: new by spotanim and destination.  A throw is remembered
    -- while it is listed and two ticks past its flight at first sight; its
    -- cycles_left is NOT a re-throw signal (probe m55trig: re-raising on a
    -- larger cycles_left read 9 throws as 18, one extra a tick in flight)
    if E.projectiles ~= nil then
        local known = {}
        for key, k in pairs(ev.projs) do
            if v.tick <= k.until_tick then known[key] = k end
        end
        local pr, projs = QD.world.projectiles(0)
        if pr == "ok" and type(projs) == "table" then
            for _, p in ipairs(projs) do
                local name = E.projectiles[p.spotanim_id]
                if name ~= nil then
                    -- a throw AT an entity follows it on the client (its dst
                    -- moves with the raider: probe m55trig read one throw a
                    -- tick while the solo walked), so such a throw is keyed by
                    -- its source and target, and its dst is the one at first
                    -- sight (where the server aimed it); a ground throw by its
                    -- destination
                    -- (target: an npc's slot + 1, a player's -(pid + 1),
                    -- 0 a tile; the probe's moving dst was a player's)
                    local target = p.target or 0
                    local key
                    if target ~= 0 then
                        key = p.spotanim_id .. ":" .. tostring(p.src_x) .. ":" .. tostring(p.src_z) .. ":t" .. target
                    else
                        key = p.spotanim_id .. ":" .. p.dst_x .. ":" .. p.dst_z
                    end
                    local cyc = p.cycles_left or 0
                    local k = known[key]
                    if k == nil then
                        local ticks = (cyc + QD.RAID_PLAY_CYCLES_PER_TICK - 1) // QD.RAID_PLAY_CYCLES_PER_TICK
                        raise(name, { x = p.dst_x, z = p.dst_z, cycles = cyc, ticks = ticks,
                            mine = p.dst_x == v.me.x and p.dst_z == v.me.z,
                            near = math.max(math.abs(p.dst_x - v.me.x), math.abs(p.dst_z - v.me.z)) <= 1 })
                        known[key] = { cycles = cyc, until_tick = v.tick + math.max(ticks, 1) + 2, x = p.dst_x, z = p.dst_z }
                        ev.proj_log = ev.proj_log or {}
                        if #ev.proj_log < 40 then ev.proj_log[#ev.proj_log + 1] = "t" .. v.tick .. " " .. key .. " c" .. cyc .. " d" .. p.dst_x .. "," .. p.dst_z end
                    else
                        -- still listed: the same throw (its cycles_left is
                        -- not monotonic before launch, probe m55trig)
                        k.cycles = cyc
                        k.until_tick = math.max(k.until_tick, v.tick + 1)
                    end
                end
            end
        end
        ev.projs = known
    end
    -- ground spotanims: new at a tile
    if E.pools ~= nil then
        local now = {}
        local sr, spots = QD.world.spotanims(0)
        if sr == "ok" and type(spots) == "table" then
            for _, s in ipairs(spots) do
                local name = E.pools[s.spotanim_id]
                if name ~= nil then
                    local key = s.spotanim_id .. ":" .. s.x .. ":" .. s.z
                    if not ev.pools[key] then
                        raise(name, { x = s.x, z = s.z, mine = s.x == v.me.x and s.z == v.me.z })
                    end
                    now[key] = true
                end
            end
        end
        ev.pools = now
    end
    return out
end

-- FOLD: the intents of this tick's handlers and watches, by priority.
function QD.raid._play_fold(intents)
    local fold, win = { want = {} }, {}
    for _, it in ipairs(intents) do
        local pri = it.pri or 1
        for _, f in ipairs(QD.RAID_PLAY_INTENT_FIELDS) do
            if it[f] ~= nil and it[f] ~= false and (win[f] == nil or pri > win[f].pri) then
                fold[f] = it[f]
                win[f] = { pri = pri, by = it.by }
            end
        end
        if it.want ~= nil then
            for name, on in pairs(it.want) do
                local k = "want." .. name
                if win[k] == nil or pri > win[k].pri then
                    fold.want[name] = on
                    win[k] = { pri = pri, by = it.by }
                end
            end
        end
    end
    -- a step and a press cannot both land in one tick: the step goes, the
    -- press is held (QD.raid._play_fire)
    return fold, win
end

-- FIRE: raise the events, run the handlers and the watches, fold.  Returns
-- the events and the folded trigger intent (nil when nothing answered).
function QD.raid._play_fire(st, v)
    local events = QD.raid._play_events(st, v)
    local intents = {}
    local fired = {}
    if st.trig_hold ~= nil then
        local h = st.trig_hold
        st.trig_hold = nil
        if v.tick <= h.until_tick then
            local it = { pri = h.pri, by = "hold:" .. h.by, why = "held" }
            it[h.field] = h.value
            intents[#intents + 1] = it
        end
    end
    for _, e in ipairs(events) do
        st.trig_counts[e.name] = (st.trig_counts[e.name] or 0) + 1
        fired[#fired + 1] = e.name
        local list = st.handlers[e.name]
        if list ~= nil then
            for _, handler in ipairs(list) do
                local it = QD.raid._play_call(handler, st, v, e)
                if it ~= nil then
                    it.by = it.by or e.name
                    intents[#intents + 1] = it
                end
            end
        end
    end
    for _, w in ipairs(st.watches) do
        local value = QD.raid._play_call(w.reader, st, v)
        if w.primed and value ~= w.value then
            st.trig_counts["watch:" .. w.name] = (st.trig_counts["watch:" .. w.name] or 0) + 1
            fired[#fired + 1] = "watch:" .. w.name
            local it = QD.raid._play_call(w.on_change, st, v, value, w.value)
            if it ~= nil then
                it.by = it.by or ("watch:" .. w.name)
                intents[#intents + 1] = it
            end
        end
        w.value, w.primed = value, true
    end
    v.events = events
    if #intents == 0 then
        if #fired > 0 and #st.trig_log < 600 then st.trig_log[#st.trig_log + 1] = { tick = v.tick, fired = table.concat(fired, ","), won = "" } end
        return nil
    end
    local fold, win = QD.raid._play_fold(intents)
    if fold.walk ~= nil then
        for _, f in ipairs({ "press", "cast" }) do
            if fold[f] ~= nil then
                st.trig_hold = { field = f, value = fold[f], pri = win[f].pri, by = win[f].by,
                    until_tick = v.tick + QD.RAID_PLAY_HOLD_TICKS }
                st.trig_holds = (st.trig_holds or 0) + 1
                fold[f], win[f] = nil, nil
            end
        end
        if fold.attack ~= nil then fold.attack, win.attack = nil, nil end
    end
    fold.wins = win
    local won = {}
    for f, w in pairs(win) do
        won[#won + 1] = f .. "=" .. tostring(w.by) .. "(" .. w.pri .. ")"
        local k = f .. ":" .. tostring(w.by)
        st.trig_wins[k] = (st.trig_wins[k] or 0) + 1
    end
    table.sort(won)
    if #st.trig_log < 600 then
        st.trig_log[#st.trig_log + 1] = { tick = v.tick, fired = table.concat(fired, ","), won = table.concat(won, " ") }
    end
    if st.trigger_marks and st.log and #won > 0 then QD.ticklog.mark("trig " .. table.concat(won, " ")) end
    return fold
end

-- MERGE: the plan's default intent with the folded trigger intent.
function QD.raid._play_merge(st, v, intent, fold)
    if fold == nil then return intent end
    intent = intent or { want = {} }
    for _, f in ipairs(QD.RAID_PLAY_INTENT_FIELDS) do
        if fold[f] ~= nil then intent[f] = fold[f] end
    end
    intent.want = intent.want or {}
    for name, on in pairs(fold.want) do intent.want[name] = on end
    if fold.walk ~= nil then intent.attack = false end
    if fold.press ~= nil or fold.cast ~= nil then
        intent.attack = false
        if fold.walk == nil then intent.walk = nil end
    end
    intent.trigger = fold
    return intent
end

-- The summary clause: the events seen and the intents that won.
function QD.raid._play_summary_triggers(st)
    if st.trig_counts == nil or next(st.trig_counts) == nil then return "" end
    local c, w = {}, {}
    for k, n in pairs(st.trig_counts) do c[#c + 1] = k .. " " .. n end
    for k, n in pairs(st.trig_wins) do w[#w + 1] = k .. " " .. n end
    table.sort(c)
    table.sort(w)
    return "; triggers [" .. table.concat(c, ", ") .. "] won [" .. table.concat(w, ", ") .. "] held " .. (st.trig_holds or 0)
end

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
    -- raid seam55: the events of this tick fire first; the plan's decide sees
    -- them (v.events, v.trigger) and its intent is the default the folded
    -- trigger intent overrides field by field (QD.raid._play_merge)
    v.trigger = QD.raid._play_fire(st, v)
    local intent = QD.raid[st.plan.decide](st, v)
    intent = QD.raid._play_merge(st, v, intent, v.trigger)
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
        .. string.format("; own screen saw %d weapon starts", #st.seen_swings)
        .. QD.raid._play_summary_seam31(st)
        .. QD.raid._play_summary_triggers(st)
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

-- ==========================================================================
-- owner_tob_normal 2026-10-06: THE PARTY THROUGH THE DOOR ON ONE TICK.
--
--   t.raid.cross_together(name, opts) -> ok | timeout | refused, detail
--
-- The owner, 2026-10-06: "PARTY ENTRY IN LOCKSTEP".  A room harness used to
-- have the leader walk to the barrier, answer "Yes, begin the fight." and only
-- then let the members (behind a party barrier) click it from wherever they
-- stood: they crossed three to four ticks after the leader, eighteen tiles away
-- at the Nylocas door (owner probe _probe_door: the entry tile 6431,113, the
-- barrier 6431,95), and Maiden's phase 100 ran 54-59 ticks against Blert's 42
-- [32-52] with the trio's first swings at +4 / +8 / +8 (Blert: +5 every seat).
-- Here every seat first walks to the tile beside the barrier on its own side
-- (the barrier's nearest copy, the long axis read off the gap to it), the
-- starter opens the question, and on the tick the party barrier releases the
-- starter answers and every other seat presses the barrier: the server takes
-- the start and the members' steps in one tick.  A member whose press beat the
-- start (it sees the question) answers "Not yet." and presses again.
--   opts.loc      the barrier loc (default tob_arena_barrier)
--   opts.starter  the seat that begins the fight (default 1)
--   opts.answer   the option text (default "Yes, begin the fight.")
--   opts.before   function() the starter runs at the door before the press
--   opts.at_answer function() the starter runs on the tick it answers (a mark)
--   opts.timeout  party-barrier ticks (default 900)
function QD.raid.cross_together(name, opts)
    assert(type(name) == "string")
    opts = opts or {}
    local loc = opts.loc or "tob_arena_barrier"
    local starter = opts.starter or 1
    local answer = opts.answer or "Yes, begin the fight."
    local timeout = opts.timeout or 900
    local role = QD.party.role()
    local lr, row = QD.world.loc_near(loc, 40)
    if lr ~= "ok" or type(row) ~= "table" then
        return "refused", "cross_together " .. name .. ": no " .. loc .. " within 40 (" .. tostring(lr) .. ")"
    end
    local _, here = QD.world.tile()
    local dx, dz = here.x - row.tile_x, here.z - row.tile_z
    local sx, sz
    if math.abs(dx) >= math.abs(dz) then
        sx, sz = row.tile_x + ((dx >= 0) and 1 or -1), here.z
    else
        sx, sz = here.x, row.tile_z + ((dz >= 0) and 1 or -1)
    end
    QD.player.walk_to(sx, sz, 40)
    local _, at = QD.world.tile()
    local br = QD.party.barrier(name .. "_door", timeout)
    if br ~= "ok" then return "timeout", "cross_together " .. name .. ": the door barrier " .. tostring(br) end
    if role == starter and opts.before ~= nil then opts.before() end
    if role == starter then
        local presses, opened = 0, false
        while presses < 3 and not opened do
            presses = presses + 1
            QD.player.click_loc(loc, 1)
            QD.await({ level = function() return QD.chat.kind() == "options" end, note = name .. ": the barrier's question" }, 6)
            opened = QD.chat.kind() == "options"
        end
        if not opened then
            QD.party.barrier(name .. "_asked", timeout)
            return "refused", "cross_together " .. name .. ": no question after " .. presses .. " press(es)"
        end
    end
    local ar = QD.party.barrier(name .. "_asked", timeout)
    if ar ~= "ok" then return "timeout", "cross_together " .. name .. ": the asked barrier " .. tostring(ar) end
    local _, go_tick = QD.tick()
    local tries, detail = 0, ""
    if role == starter then
        if opts.at_answer ~= nil then opts.at_answer() end
        local pr, pd = QD.chat.play({ "options", "choose:" .. answer })
        detail = "answered " .. tostring(pr)
        if pr ~= "ok" then return "refused", "cross_together " .. name .. ": " .. tostring(pd) end
    else
        -- a press, then the next tick: the question open means the press beat
        -- the start ("Not yet." and press again); otherwise the step is the
        -- server's on this tick and the fight starts now (no wait for the tile
        -- to read moved: that read is a tick behind and cost the members two
        -- ticks of the run in, owner sva probe: play from t67 against t65)
        while tries < 4 do
            tries = tries + 1
            QD.player.click_loc(loc, 1)
            QD.ticks(1)
            if QD.chat.kind() ~= "options" then break end
            QD.chat.play({ "options", "choose:Not yet." })
        end
        detail = tries .. " press(es)"
    end
    local _, after = QD.world.tile()
    local _, t_after = QD.tick()
    return "ok", string.format("cross_together %s: p%d from %d,%d (door tile %d,%d), on %d,%d at t%d; go t%d; %s",
        name, role, at.x, at.z, sx, sz, after.x, after.z, t_after, go_tick, detail)
end
