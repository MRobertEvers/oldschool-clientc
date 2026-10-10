-- tob_relay: the whole Theatre of Blood, Normal, a trio, lobby to lobby, every
-- room played by its solver (script/plugins/quest_driver/tob_*.lua and
-- raid_solve_verzik_p1/p2/p3.lua) in ONE Learner/Void kit
-- (docs/minigames/theater_of_blood/ROOM_SOLVERS.md 6, 6.1):
--
--   the board's party (form, apply, accept), the door's ready check, follow in;
--   per room: arrive, the room's own start sequence (the room test's), the
--     solver on every seat, prayers off, the leader's way out (the gate, the
--     passage: ~tob_carry_party takes every raider in the room along);
--   the supply chest after Bloat and after Sotetseg (restores, then brews);
--   the Dawnbringer: the Xarpus solver's leader takes it from the skeleton;
--   Verzik P1, P2, P3; the trapdoor, the reward chest, the teleport out.
--
-- GREEN: every room cleared with no raider dead, the party back in the lobby.
--
--   ./src/build_rooms_opt/torirsserver --scriptrun test/raids/tob_relay.lua --bots 3 \
--       --name sa --session /tmp/relay --fixture tests/raids/fixtures/fresh_lumbridge.ini
local role = (QD_PARTY and QD_PARTY.role) or 1
local P = "p" .. role .. " "
local LOBBY_X, LOBBY_Z = 3662, 3216

-- THE LEARNER/VOID KIT (wiki Strategies, the Learner/Void example): the void
-- melee set worn; the ranged and mage switches, the rune pouch of the barrage's
-- runes and Ancient Magicks on every seat (Maiden's freezer, the Nylocas'
-- barrage); Maiden's hammer and salve; seat 3 the serpentine helm for the
-- Athanatos (ROOM_SOLVERS 4.6: until the blowpipe's venom replaces it).
local WORN = {
    "game_pest_melee_helm", "elite_void_knight_top", "elite_void_knight_robes", "pest_void_knight_gloves",
    "abyssal_tentacle", "dragon_parryingdagger", "zenyte_amulet_enchanted", "tzhaar_cape_fire",
    "dragon_boots", "nzone_berzerker_ring",
}
local kit = { "::clearinv" }
for _, s in ipairs({ "attack", "strength", "defence", "hitpoints", "prayer", "magic", "ranged", "agility" }) do
    kit[#kit + 1] = "::setlevel " .. s .. " 99"
end
for _, line in ipairs({
    "::setvar varb3888_kr_quest ^kr_complete",
    "::setvar varb3909_kr_knightwaves_state 8",
    "::setvar varb5451_prayer_rigour_unlocked 1",
    "::setvar varb5452_prayer_augury_unlocked 1",
    "::setvar varb5453_prayer_preserve_unlocked 1",
    "::setvar varb16097_prayer_deadeye_unlocked 1",
    "::setvar varb16098_prayer_mystic_vigour_unlocked 1",
    "::setvar varb358_deserttreasure ^dt_complete",
    "::setvar varb4070_spellbook 1",
}) do kit[#kit + 1] = line end
for _, obj in ipairs(WORN) do
    kit[#kit + 1] = "::give " .. obj .. " 1"
    kit[#kit + 1] = "::wield " .. obj
end
for _, line in ipairs({
    "::~charge abyssal_tentacle 10000",
    "::clearinv",
    "::blowpipe dragon_dart 2000 2000",
    "::give game_pest_archer_helm 1", "::give zenyte_necklace_enchanted 1", "::give avas_assembler 1",
    "::give game_pest_mage_helm 1", "::give toxic_tots_charged 1", "::give occult_necklace 1",
    "::give ma2_saradomin_cape 1",
    "::~charge toxic_tots_charged 2500",
    "::give dragon_warhammer 1",
    "::give bh_rune_pouch 1",
    "::setvar varb29_rune_pouch_type_1 2", "::setvar varb1624_rune_pouch_quantity_1 600",
    "::setvar varb1622_rune_pouch_type_2 7", "::setvar varb1625_rune_pouch_quantity_2 400",
    "::setvar varb1623_rune_pouch_type_3 8", "::setvar varb1626_rune_pouch_quantity_3 200",
}) do kit[#kit + 1] = line end
-- SEAT 1 IS MAIDEN'S FREEZER: it needs room to take the defender and boots
-- off for its cast set (tob_maiden.lua's kit: "two supply slots fewer").
-- With the other seats' supplies its pack had one slot free, the barrage
-- swap failed, and her DPS seats took 209-402 against the room test's
-- 157-229 (8 relay seeds; the room test with this kit reproduced it to the
-- hit, and with these supplies matched the room test to the hit).
for _, line in ipairs((role == 1)
    and { "::give br_4dose2restore 4", "::give br_4dose2combat 1", "::give br_4dosepotionofsaradomin 3", "::give anglerfish 3" }
    or { "::give br_4dose2restore 5", "::give br_4dose2combat 2", "::give br_4dosepotionofsaradomin 4", "::give anglerfish 5" }) do
    kit[#kit + 1] = line
end
-- seat 1 ranges (Maiden's freezer, the Nylocas' ranger): the ranging
-- potion; seats 2 and 3 a second super combat, for Verzik
kit[#kit + 1] = (role == 1) and "::give br_4doserangerspotion 1" or "::give br_4dose2combat 1"
if role == 3 then kit[#kit + 1] = "::give serpentine_helm_charged 1" end

local ORDER = { "maiden", "bloat", "nylocas", "sotetseg", "xarpus", "verzik" }
local NEXT = { maiden = "bloat", bloat = "nylocas", nylocas = "sotetseg", sotetseg = "xarpus", xarpus = "verzik" }
local MAX = { maiden = 600, bloat = 600, nylocas = 700, sotetseg = 700, xarpus = 900 }

local function origin_of(t)
    local _, here = t.world.tile()
    return math.floor(here.x / 64) * 64, math.floor(here.z / 64) * 64, here
end

-- THE WAY OUT (tob_raid.rs2 ~tob_exit_walked: walking within reach of the
-- room's exit advances the raid, and ~tob_carry_party takes every raider in
-- the old room along). The gate first: that copy of the barrier, pinned to
-- the room's gate tile (tob.constant ^tob_<room>_gate_lx/lz), steps me
-- across. One packet each (t.raid.tob_loc_op, tob_move): the pointer
-- library's walk-then-click timed out on scriptrun (relay rl2).
local GATE = { maiden = { 49, 30 }, bloat = { 23, 31 }, nylocas = { 31, 31 }, sotetseg = { 15, 19 } }
local EXIT = { maiden = { 40, 6 }, bloat = { 5, 31 }, nylocas = { 39, 51 }, sotetseg = { 15, 5 }, xarpus = { 33, 48 } }

-- Which side of each gate is OUT of the arena (local tiles).
local OUT = {
    maiden = function(x, z) return x > 49 end, bloat = function(x, z) return x < 23 end,
    nylocas = function(x, z) return z > 31 end, sotetseg = function(x, z) return z < 19 end,
}
-- (a press before the room counts as cleared -- the watchdog misses the boss
-- two ticks after the solver sees it die -- asks to START the fight and steps
-- no one: relay rl3 stood at Maiden's gate. So: press, look, close any
-- question, press again, until I am on the far side.)
local function through_gate(t, name, ox, oz)
    local g = GATE[name]
    if g == nil then return true end
    local r, crossed = "none", false
    for _ = 1, 12 do
        local _, me = t.world.tile()
        if OUT[name](me.x - ox, me.z - oz) then crossed = true break end
        t.key("escape")
        r = t.raid.tob_loc_op("tob_arena_barrier", 1, { x = ox + g[1], z = oz + g[2] })
        t.ticks(5)
    end
    local _, me = t.world.tile()
    t.check(name .. ".gate", crossed, P .. "the gate at " .. g[1] .. "," .. g[2] .. ": " .. tostring(r) .. "; at "
        .. (me.x - ox) .. "," .. (me.z - oz))
    return crossed
end

-- Wait for my map square to leave `from` (the passage carried me).
local function await_square(t, from, budget)
    for _ = 1, budget do
        local sq = t.raid.tob_square()
        if sq ~= from then return true, sq end
        t.ticks(1)
    end
    return false, from
end

local function leave_room(t, name, ox, oz)
    local from = t.raid.tob_square()
    local e = EXIT[name]
    t.raid.tob_move(ox + e[1], oz + e[2])
    local ok, sq = await_square(t, from, 40)
    if not ok then
        local exit_loc = (name == "xarpus") and "tob_dungeon_xarpus_arena_door_exit" or "tob_dungeon_walkway_exit_clickbox"
        t.raid.tob_loc_op(exit_loc, 1)
        ok, sq = await_square(t, from, 40)
    end
    t.check(name .. ".passage", ok, P .. "from " .. from .. " to " .. tostring(sq))
    return ok
end

-- What a seat carries, in doses and fish (the relay's supply budget).
local function supplies_text(t)
    local function n(item)
        local r, c = t.inv.count(item)
        return (r == "ok" and tonumber(c)) or 0
    end
    local function doses(stem)
        local d = 0
        for k = 1, 4 do d = d + k * (n("br_" .. k .. stem) + n(k .. stem) + ((stem == "dose2restore") and n(k .. "doseprayerrestore") or 0)) end
        return d
    end
    return string.format("fish %d, brew doses %d, restore doses %d, combat doses %d", n("anglerfish") + n("shark"),
        doses("dosepotionofsaradomin"), doses("dose2restore"), doses("dose2combat"))
end

local function free_slots(t)
    local n = 0
    for i = 0, 27 do
        local r, sl = t.inv.slot(i)
        if r == "ok" and type(sl) == "table" and sl.count == 0 then n = n + 1 end
    end
    return n
end

local function drop_vials(t)
    for _, v in ipairs({ "vial_empty", "br_vial_empty" }) do
        for _ = 1, 28 do
            local cr, c = t.inv.count(v)
            if cr ~= "ok" or (tonumber(c) or 0) == 0 then break end
            t.player.drop(v)
        end
    end
end

-- THE SUPPLY CHEST (tob_chest.rs2: Normal's points store): restores to two
-- potions, then brews -- what every solver drinks (they eat anglerfish only).
-- One slot kept after Sotetseg for the Dawnbringer (the leader).
local CHEST_SLOT = { prayer = 1, brew = 2, restore = 3, shark = 5 }   -- enum_1952
local CHEST_COST = { prayer = 2, brew = 3, restore = 3, shark = 1 }   -- enum_1953
local function restock(t, name)
    drop_vials(t)
    local chr, chrow = t.world.loc_near("tob_midway_chest_closed", 40)
    t.check(name .. ".chest_found", chr == "ok" and chrow ~= nil, P .. tostring(chr))
    if chr ~= "ok" or chrow == nil then return end
    local or_ = "timeout"
    for _ = 1, 3 do
        if or_ == "ok" then break end
        t.raid.tob_loc_op("tob_midway_chest_closed", 1)
        or_ = t.ui.await_open("tob_midway_stores", 20)
    end
    t.check(name .. ".chest_open", or_ == "ok", P .. "store " .. tostring(or_))
    if or_ ~= "ok" then t.key("escape") return end
    local _, points = t.var.varbit("varb6460_tob_midwaychest_points")
    points = tonumber(points) or 0
    local p0, bought = points, {}
    -- (slots kept free: the Nylocas' and Verzik's gear switches need room to
    -- swap -- relay rl9's seat 1 left the Bloat chest full and the party
    -- wiped in the Nylocas -- and seat 1 takes the Dawnbringer at Xarpus)
    local keep = (name == "bloat") and 3 or ((role == 3) and 2 or 1)
    if name == "sotetseg" and role == 1 then keep = keep + 1 end   -- the Dawnbringer
    -- (a buy counts only when the chest's own points varbit fell: the server
    -- refuses one for a full pack, and counting down without reading left
    -- seat 1 of relay rb with 9 restore doses where it "bought" 24)
    local function buy(kind)
        if points < CHEST_COST[kind] or free_slots(t) <= keep then return false end
        local wr, cell = t.ui.widget("tob_midway_stores:items", CHEST_SLOT[kind])
        if wr ~= "ok" then return false end
        if t.ui.invoke(cell, 2) ~= "ok" then return false end
        t.ticks(1)
        local _, now = t.var.varbit("varb6460_tob_midwaychest_points")
        now = tonumber(now) or points
        if now >= points then return false end
        points = now
        bought[#bought + 1] = kind
        return true
    end
    -- PRAYER for the rooms ahead: a P3 under Piety and a protection prayer
    -- runs ~400 ticks, and seats that came in on 6-10 restore doses ran dry
    -- and took every hit (relay rl15). Prayer potions (2 points) toward 24
    -- doses of super restore and prayer potion held.
    local function prayer_doses()
        local d = 0
        for k = 1, 4 do
            for _, nm in ipairs({ "br_" .. k .. "dose2restore", k .. "dose2restore", k .. "doseprayerrestore" }) do
                local r, c = t.inv.count(nm)
                d = d + k * ((r == "ok" and tonumber(c)) or 0)
            end
        end
        return d
    end
    local function fish()
        local f = 0
        for _, nm in ipairs({ "anglerfish", "shark", "mantaray", "seaturtle" }) do
            local r, c = t.inv.count(nm)
            f = f + ((r == "ok" and tonumber(c)) or 0)
        end
        return f
    end
    -- (balanced: prayer to a floor, food to a floor, then prayer to the
    -- target, then food -- all-prayer left two relay seats no fish, rl16)
    -- (food to its floor FIRST: prayer first spent all 12 of a seat's points
    -- on prayer potions and sent Verzik's tank in with no fish, dead in P3,
    -- relay rl24 rp. The P3 prayer drought of rl21 was the top-up sipping
    -- prayer potions against a drained attack, not the chest: with that
    -- fixed every seat reaches Verzik on 21-25 restore doses, so 16)
    local floor_f, target_p = 6, 12
    if name == "sotetseg" then floor_f, target_p = 8, 16 end
    -- HEALING BY THE SLOT: the whole kit rides to the end (nothing dropped),
    -- so slots run out before points do. A shark is 20 hitpoints for 1 point
    -- and a slot; a brew is four sips, ~64, for 3 points and a slot. While a
    -- seat has more spendable points than free slots past `keep`, a slot is
    -- worth a brew; after that, sharks (rl38: 8 fish a seat at Verzik).
    -- Healing is counted in hitpoints, brews included, and the points the
    -- prayer target needs are held back first (rl39: brews bought until the
    -- points ran out, every seat at Verzik on 0-6 restore doses).
    local function brew_doses()
        local d = 0
        for k = 1, 4 do
            for _, nm in ipairs({ "br_" .. k .. "dosepotionofsaradomin", k .. "dosepotionofsaradomin" }) do
                local r, c = t.inv.count(nm)
                d = d + k * ((r == "ok" and tonumber(c)) or 0)
            end
        end
        return d
    end
    local function heal_hp() return 20 * fish() + 16 * brew_doses() end
    -- (SUPER restores for the prayer target, not prayer potions: brews drain
    -- Attack and Strength a sip, only a super restore puts them back, and a
    -- team that bought brews and prayer potions fought P3 drained - 8 a hit
    -- where it had 15, a P3 of 848 ticks where it had 350, rl40)
    local function prayer_reserve()
        local short = math.max(0, target_p - prayer_doses())
        return ((short + 3) // 4) * CHEST_COST.restore
    end
    local function heal(reserve)
        local room = free_slots(t) - keep
        local spend = points - reserve
        if room <= 0 or spend < CHEST_COST.shark then return false end
        if spend > room and spend >= CHEST_COST.brew then return buy("brew") end
        return buy("shark")
    end
    for _ = 1, 20 do if heal_hp() >= 20 * floor_f or not heal(prayer_reserve()) then break end end
    for _ = 1, 8 do if prayer_doses() >= target_p or not buy("restore") then break end end
    for _ = 1, 20 do if not heal(0) then break end end
    t.check(name .. ".chest_bought", true, P .. "points " .. p0 .. " -> " .. points .. "; bought "
        .. (#bought > 0 and table.concat(bought, " ") or "nothing") .. "; free slots " .. free_slots(t))
    t.key("escape")
    t.ticks(1)
end

-- What no later room uses, left on the floor so the chests can fill the
-- slots (relay rl12: six seeds of six reached Verzik P3 on 5-7 fish and ran
-- dry there, with ten slots a seat holding the Nylocas' switches)
local function prayers_off(t)
    local r, _, set = t.prayer.read()
    if r == "ok" and type(set) == "table" then
        -- in name order: one press a call, and pairs() order is the string
        -- hash's, not the game's (relay rc: the lanes turned prayers off in
        -- different orders)
        local lit = {}
        for prayer, on in pairs(set) do
            if on then lit[#lit + 1] = prayer end
        end
        table.sort(lit)
        for _, prayer in ipairs(lit) do t.prayer.set(prayer, false) end
    end
end

-- A room fight, every seat: the room test's own start sequence, the solver.
local function fight(t, name)
    t.expect(name .. ".barrier.ready", t.party.barrier(name .. "_ready", 600))
    t.cheat("::synctimers", false)
    if role == 1 and name ~= "maiden" then
        local sr, sd = t.cheat("::tobsyncroom")
        t.check(name .. ".sync", sr == "ok", tostring(sd))
    end
    t.expect(name .. ".barrier.synced", t.party.barrier(name .. "_synced", 300))
    local solve = t.raid[name .. "_solve"]
    local result, detail = solve({ max_ticks = MAX[name] })
    t.check(name .. ".solve", result == "ok", P .. string.sub(tostring(detail), 1, 900))
    return result == "ok"
end

local function verzik(t)
    local prep = t.raid.verzik_p1_prepare()
    t.check("verzik.prepare", prep ~= nil, P .. (prep and (prep.pillars .. " pillars searched") or "the room never came into view"))
    t.expect("verzik.barrier.ready", t.party.barrier("verzik_ready", 600))
    t.cheat("::synctimers", false)
    t.exec("verzik.prayer", t.prayer.set, "protectfrommagic", true)
    if role == 1 then
        t.check("verzik.talk", t.player.talk_to("verzik_initial", 1) == "ok", "")
        local cr, cd = t.chat.play({ "npc:So, you wish to entertain me", "options", "choose:Yes, begin the fight." })
        t.check("verzik.begin", cr == "ok", tostring(cd))
    end
    t.expect("verzik.barrier.started", t.party.barrier("verzik_started", 900))
    local r1, d1 = t.raid.verzik_p1_solve({ max_ticks = 400, prep = prep, weapon = "abyssal_tentacle" })
    t.check("verzik.p1", r1 == "ok", P .. string.sub(tostring(d1), 1, 600))
    if r1 ~= "ok" then return false end
    local r2, d2 = t.raid.verzik_p2_solve({ max_ticks = 900, base = prep.base, weapon = "abyssal_tentacle" })
    t.check("verzik.p2", r2 == "ok", P .. string.sub(tostring(d2), 1, 600))
    if r2 ~= "ok" then return false end
    local r3, d3 = t.raid.verzik_p3_solve({ max_ticks = 900, base = prep.base, weapon = "abyssal_tentacle" })
    t.check("verzik.p3", r3 == "ok", P .. string.sub(tostring(d3), 1, 600))
    return r3 == "ok"
end

-- Every raider: the trapdoor, the leader's chest, the teleport out.
local function reward_room(t)
    local dr, dd = "not_found", ""
    for _ = 1, 20 do
        dr, dd = t.player.click_loc("tob_dungeon_verzik_throne_door_opened", 1)
        if dr == "ok" then break end
        t.ticks(2)
    end
    t.ticks(4)
    local chest_sym = nil
    for k = 0, 4 do
        local sym = "tob_treasureroom_chest_loc" .. k
        local lr, row = t.world.loc_near(sym, 30)
        if lr == "ok" and row ~= nil then chest_sym = sym break end
    end
    t.check("raid.reward_room", dr == "ok" and chest_sym ~= nil, P .. "trapdoor " .. tostring(dr) .. " " .. string.sub(tostring(dd), 1, 100)
        .. "; chest " .. tostring(chest_sym))
    if role == 1 and chest_sym ~= nil then
        local cr, cd = t.player.click_loc(chest_sym, 1)
        t.ticks(3)
        t.check("raid.reward_chest", cr == "ok", tostring(cd))
    end
    t.expect("party.barrier.looted", t.party.barrier("looted", 600))
    local xr, xd = t.player.click_loc("tob_treasureroom_teleportout", 1)
    t.ticks(4)
    local _, here = t.world.tile()
    local home = math.abs(here.x - LOBBY_X) <= 20 and math.abs(here.z - LOBBY_Z) <= 20
    t.check("raid.lobby", home, P .. "teleport out " .. tostring(xr) .. " " .. string.sub(tostring(xd), 1, 100)
        .. "; at " .. here.x .. "," .. here.z)
end

local function run(t)
    local names = t.party.names()
    local leader = names[1]
    if role == 1 then t.check("relay.ticklog", t.ticklog.start() == "ok", "") end

    -- THE LOBBY: the board, the party, the door (the live client). Scriptrun
    -- has no clientscripts and the board is built of them: there the party
    -- enters at Maiden as the room tests do (QD.raid.tob_headless)
    if t.raid.tob_headless() then
        local r, d = t.raid.enter("tob", "maiden", { mode = "normal" })
        t.check("party.enter", r == "ok", P .. tostring(d))
    else
        t.exec("lobby.goto", t.player.goto_tile, LOBBY_X - 1 + role, LOBBY_Z, 0)
        t.expect("party.barrier.lobby", t.party.barrier("lobby", 200))
        if role == 1 then t.exec("party.form", t.party.form, "normal") end
        t.expect("party.barrier.formed", t.party.barrier("formed", 300))
        if role ~= 1 then t.exec("party.apply", t.party.apply, leader) end
        t.expect("party.barrier.applied", t.party.barrier("applied", 300))
        if role == 1 then
            for n = 2, #names do t.exec("party.accept." .. n, t.party.accept, names[n]) end
        end
        t.expect("party.barrier.accepted", t.party.barrier("accepted", 300))
        t.key("escape")
        t.ticks(2)
        t.expect("party.barrier.closed", t.party.barrier("closed", 300))
        if role == 1 then
            local rr, rd = t.party.ready()
            t.check("party.ready", rr == "ok", tostring(rd))
        end
        t.expect("party.barrier.leader_in", t.party.barrier("leader_in", 300))
        if role ~= 1 then
            t.msg.await("has entered the Theatre of Blood (Normal Mode)", 20)
            local fr, fd = t.party.follow_in()
            t.check("party.follow_in", fr == "ok", P .. tostring(fd))
        end
    end

    local cleared = 0
    local alive = true
    local from = nil
    for _, name in ipairs(ORDER) do
        -- THE ARRIVAL: in the room's own square (the passage carried us)
        if from ~= nil then await_square(t, from, 60) end
        t.expect(name .. ".barrier.arrived", t.party.barrier(name .. "_arrived", 3000))
        local ox, oz = origin_of(t)
        from = t.raid.tob_square()
        -- back up to full before the room (the solvers' own eating is for
        -- inside it)
        if alive then
            local hp, pr = t.raid.tob_top_up(30)
            t.check(name .. ".top_up", true, P .. "hitpoints " .. tostring(hp) .. ", prayer " .. tostring(pr)
                .. "; " .. supplies_text(t))
        end
        -- (a seat that failed a room still meets every barrier, fighting
        -- nothing: the party stays in lock step, relay rl5 hung 3000 ticks a
        -- barrier on a caged seat)
        -- the combat potions left after Sotetseg are Verzik's (holding them
        -- from the start lost Maiden and Sotetseg on four seeds of four, rl14)
        t.raid.tob_hold_boosts(name == "xarpus")
        local ok = false
        if alive then
            if name == "verzik" then ok = verzik(t) else ok = fight(t, name) end
        else
            for _, b in ipairs(name == "verzik" and { "_ready", "_started" } or { "_ready", "_synced" }) do
                t.party.barrier(name .. b, 9000)
            end
        end
        alive = alive and ok
        t.expect(name .. ".barrier.done", t.party.barrier(name .. "_done", 9000))
        if alive then
            prayers_off(t)
            -- empty vials only: no equipment is dropped after a boss (owner,
            -- 2026-10-10), every seat keeps its whole kit to the end
            drop_vials(t)
        end
        if name == "bloat" or name == "sotetseg" then
            -- every seat through the gate to the supply chest
            if alive then
                through_gate(t, name, ox, oz)
                restock(t, name)
            end
            t.expect(name .. ".barrier.chest", t.party.barrier(name .. "_chest", 3000))
        end
        if alive then cleared = cleared + 1 end
        if name ~= "verzik" and role == 1 then
            if name ~= "bloat" and name ~= "sotetseg" then through_gate(t, name, ox, oz) end
            if not leave_room(t, name, ox, oz) then break end
        end
    end
    if cleared == #ORDER then reward_room(t) end
    t.expect("party.barrier.end", t.party.barrier("end", 3000))
    if role == 1 then
        local _, dawn = t.inv.count("verzik_special_weapon")
        t.check("raid.complete", cleared == #ORDER, cleared .. " of " .. #ORDER .. " rooms cleared; Dawnbringer held " .. tostring(dawn))
    end
    t.finish(0)
end

return {
    id = "tob_relay",
    fixture = "fresh_lumbridge.ini",
    party = 3,
    max_frames = 300000,   -- a relay is ~4100 ticks, 30 frames a tick; the gate ceiling is 480000
    setup = kit,
    run = run,
}
