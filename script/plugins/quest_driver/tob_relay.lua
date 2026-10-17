-- quest-driver / tob_relay: the whole Theatre of Blood, a trio, lobby to lobby,
-- in ONE mode. test/raids/tob_relay.lua (Normal) and tob_relay_hard_v2.lua
-- (Hard) are this, by mode: QD_ROOT.raid.tob_relay_test(mode, opts) -> the test.
--
-- tob_relay: the whole Theatre of Blood, Normal, a trio, lobby to lobby, every
-- room played by its solver (script/plugins/quest_driver/tob_*.lua and
-- raid_solve_verzik_p1/p2/p3.lua) in ONE Learner/Void kit
-- (docs/minigames/theater_of_blood/ROOM_SOLVERS.md 6, 6.1):
--
--   the party in at Maiden through t.raid.enter, on both lanes (the board's
--     way in is test/raids/tob_lobby.lua, live only);
--   per room: arrive, every seat WALKS to the room's door (the corridor side
--     of its barrier), the room's own start sequence (the room test's), the
--     solver on every seat, prayers off, and EVERY SEAT'S OWN way out (the
--     gate, the supply chest after Bloat and after Sotetseg, the passage: each
--     raider's walk into it is its own transition, tob_raid.rs2
--     [queue,tob_transition_build]; nobody is carried);
--   the supply chest after Bloat and after Sotetseg (restores, then brews);
--   the Dawnbringer: the Xarpus solver's leader takes it from the skeleton;
--   Verzik P1, P2, P3; the trapdoor, the reward chest, the teleport out.
--
-- GREEN: every room cleared with no raider dead, the party back in the lobby.
--
--   ./src/build_tob2_opt/torirsserver --scriptrun test/raids/tob_relay_hard_v2.lua --bots 5 \
--       --name f1 --ticks 12000 --session /tmp/relay --fixture tests/raids/fixtures/tob_normal_done.ini

-- -> { id, fixture, party, max_frames, setup, run } for `mode` ("normal" or
-- "hard"); called while the test file loads, so QD_PARTY is already set.
-- `opts.from` / `opts.to` run a stretch of the raid (a room probe: the party
-- lands at `from` through t.raid.enter and stops after `to`); the kept tests
-- run the whole of it.
function QD.raid.tob_relay_test(mode, opts)
    assert(mode == "normal" or mode == "hard", "tob_relay_test: mode " .. tostring(mode))
    opts = opts or {}
    local role = (QD_PARTY and QD_PARTY.role) or 1
    -- (Normal is a trio; Hard Mode a five-man, owner 2026-10-12: "Perhaps you
    -- can up HMT runner to use 5 players")
    local SEATS = (mode == "hard") and 5 or 3
    -- Hard Mode plays every room with its own solver, tob_<room>_hm.lua
    -- (owner 2026-10-12: "the HMT solvers should be separate files")
    local SUFFIX = (mode == "hard") and "_hm" or ""
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
        -- barbarian vial smashing: a last dose leaves no vial (owner 2026-10-12)
        "::setvar varp7348_barbarian_vial_smash 1",
    }) do kit[#kit + 1] = line end
    for _, obj in ipairs(WORN) do
        kit[#kit + 1] = "::give " .. obj .. " 1"
        kit[#kit + 1] = "::wield " .. obj
    end
    -- HARD MODE'S OFF-STYLE BOOSTERS STAY HOME. Every seat keeps all three
    -- weapons (Vasilias is fought in her colour by everyone), but a seat that
    -- never ranges or casts in its own role carries that style's helm, amulet
    -- and cape only for her few swaps, and the slots are worth more as
    -- healing: the relay reached Verzik on 0-17 brew doses a seat against the
    -- 25-48 one Verzik drank from a full kit (all4, xv16). Seat 5 (the
    -- Nylocas' second ranger) leaves the mage three. Seats 1 and 2 freeze at
    -- Maiden and seats 3 and 4 pipe her crabs and cast in the Nylocas, so
    -- they use both (without the ranged three her room ran 707 ticks to 435,
    -- md1 m1).
    local RANGED_EXTRA = { "game_pest_archer_helm", "zenyte_necklace_enchanted", "avas_assembler" }
    local MAGIC_EXTRA = { "game_pest_mage_helm", "occult_necklace", "ma2_saradomin_cape" }
    local left_home = {}
    if mode == "hard" and not opts.keep_boosters then
        if role == 5 then for _, o in ipairs(MAGIC_EXTRA) do left_home[o] = true end end
    end
    local swaps = {}
    for _, o in ipairs({ RANGED_EXTRA[1], RANGED_EXTRA[2], RANGED_EXTRA[3], MAGIC_EXTRA[1], "toxic_tots_charged",
                         MAGIC_EXTRA[2], MAGIC_EXTRA[3] }) do
        if not left_home[o] then swaps[#swaps + 1] = "::give " .. o .. " 1" end
    end
    local extra_brews = 0
    for _ in pairs(left_home) do extra_brews = extra_brews + 1 end
    for _, line in ipairs({
        "::~charge abyssal_tentacle 10000",
        "::clearinv",
        "::blowpipe dragon_dart 2000 2000",
    }) do kit[#kit + 1] = line end
    for _, line in ipairs(swaps) do kit[#kit + 1] = line end
    for _, line in ipairs({
        "::~charge toxic_tots_charged 2500",
        "::give dragon_warhammer 1",
        "::give bh_rune_pouch 1",
        "::setvar varb29_rune_pouch_type_1 2", "::setvar varb1624_rune_pouch_quantity_1 600",
        "::setvar varb1622_rune_pouch_type_2 7", "::setvar varb1625_rune_pouch_quantity_2 400",
        "::setvar varb1623_rune_pouch_type_3 8", "::setvar varb1626_rune_pouch_quantity_3 200",
    }) do kit[#kit + 1] = line end
    -- VERZIK P3'S SPECIAL WEAPON (owner, 2026-10-12: "p3 should be using dragon
    -- dagger or dragon claws or crystal halberd as the spec weapons, not dragon
    -- warhammer"; then: "use burning claws as an option. In fact - prefer that
    -- option"): Hard Mode carries one, burning claws (`bone_claws`, this cache's
    -- name for them: configs/all.obj, special_attack.obj sa_kind 47, 350 energy,
    -- two-handed) unless opts.spec_weapon names another (false carries none); the
    -- warhammer stays for Maiden's opener
    local SPEC_WEAPON = nil
    if mode == "hard" and opts.spec_weapon ~= false then SPEC_WEAPON = opts.spec_weapon or "bone_claws" end
    assert(SPEC_WEAPON == nil or SPEC_WEAPON == "bone_claws" or SPEC_WEAPON == "dragon_claws" or SPEC_WEAPON == "dragon_dagger"
        or SPEC_WEAPON == "crystal_halberd", "tob_relay_test: spec_weapon " .. tostring(SPEC_WEAPON))
    if SPEC_WEAPON then kit[#kit + 1] = "::give " .. SPEC_WEAPON .. " 1" end
    -- SEAT 1 IS MAIDEN'S FREEZER: it needs room to take the defender and boots
    -- off for its cast set (tob_maiden.lua's kit: "two supply slots fewer").
    -- With the other seats' supplies its pack had one slot free, the barrage
    -- swap failed, and her DPS seats took 209-402 against the room test's
    -- 157-229 (8 relay seeds; the room test with this kit reproduced it to the
    -- hit, and with these supplies matched the room test to the hit).
    -- HEALING BY THE SLOT, from the start: an anglerfish is 22 hitpoints a slot,
    -- a four-dose brew ~64, and the void-and-whip team pays for Verzik's long P3
    -- by the tick -- in rl72 tl every seat finished it with nothing left at all.
    -- So most of the fish became brews, and a super restore beside them for the
    -- Attack and Strength those brews drain (same slot count: seat 1 keeps its two
    -- free slots for the freezer's swap).
    -- (Hard Mode's Maiden freezes with seats 1 AND 2, tob_maiden.lua
    -- HARD_WAVE: seat 2 carries the freezer's pack too)
    local freezer_kit = (role == 1) or (mode == "hard" and role == 2)
    -- (Hard Mode carries Normal's split: two restores a seat traded for brews
    -- - right for the Xarpus-to-Verzik stretch alone, xv15 - sent the relay
    -- into Sotetseg on 6-8 restore doses, brew-drained, its hits halved (6.6
    -- a hit to the room test's 12.3) and 215 brew doses drunk there, all3 a3)
    local supplies = freezer_kit
        and { "::give br_4dose2restore 5", "::give br_4dose2combat 2", "::give br_4dosepotionofsaradomin 4", "::give anglerfish 1" }
        or { "::give br_4dose2restore 6", "::give br_4dose2combat 2", "::give br_4dosepotionofsaradomin 6", "::give anglerfish 2" }
    for _, line in ipairs(supplies) do
        kit[#kit + 1] = line
    end
    if extra_brews > 0 then kit[#kit + 1] = "::give br_4dosepotionofsaradomin " .. extra_brews end
    -- TWO SUPER COMBATS A SEAT (owner, 2026-10-12: "Each raider should bring 2
    -- supercombats and use supercombats throughout every room"), the last two
    -- doses kept for P2's second part and P3 (QD.raid.tob_reserve_combats below).
    -- Seat 1 ranges (Maiden's freezer, the Nylocas' ranger) and carries the
    -- ranging potion besides.
    if freezer_kit then kit[#kit + 1] = "::give br_4doserangerspotion 1" end
    if role == 3 then kit[#kit + 1] = "::give serpentine_helm_charged 1" end

    local ORDER = {}
    do
        local all = { "maiden", "bloat", "nylocas", "sotetseg", "xarpus", "verzik" }
        local on = false
        for _, name in ipairs(all) do
            if name == (opts.from or "maiden") then on = true end
            if on then ORDER[#ORDER + 1] = name end
            if name == (opts.to or "verzik") then on = false end
        end
        assert(#ORDER > 0, "tob_relay_test: no rooms from " .. tostring(opts.from) .. " to " .. tostring(opts.to))
    end
    -- the v2 supply budget's rooms (tob2_budget.lua: the reserve is what the
    -- rooms after the current one need)
    if QD.T2 and QD.T2.budget and QD.T2.budget.set_order then QD.T2.budget.set_order(ORDER) end
    if QD.T2 and QD.T2.budget and QD.T2.budget.set_spec then QD.T2.budget.set_spec(SPEC_WEAPON) end
    local NEXT = { maiden = "bloat", bloat = "nylocas", nylocas = "sotetseg", sotetseg = "xarpus", xarpus = "verzik" }
    local MAX = { maiden = 600, bloat = 600, nylocas = 700, sotetseg = 700, xarpus = 900 }
    if mode == "hard" then
        -- (Hard Mode's rooms are longer: Bloat 2400 at five, Xarpus 6000)
        MAX = { maiden = 900, bloat = 1400, nylocas = 1000, sotetseg = 1000, xarpus = 1400 }
    end


    -- THE WAY OUT, EVERY SEAT ITS OWN (owner, 2026-10-12: "they need to run to
    -- the next room and click the door, or go to the chest then click the door.
    -- They should not just appear in the next room - that is not how real tob
    -- works"). tob_raid.rs2 ~tob_exit_walked: walking within reach of the
    -- room's exit is MY transition; the first raider through builds the next
    -- room and every later one follows into it (^tob_var_next_handle). The gate
    -- first: that copy of the barrier, pinned to the room's gate tile
    -- (tob.constant ^tob_<room>_gate_lx/lz), steps me across. One packet each
    -- (t.raid.tob_loc_op, tob_move): the pointer library's walk-then-click timed
    -- out on scriptrun (relay rl2).
    -- (Xarpus' north gate too: the leader crosses it for the Dawnbringer inside
    -- the solver, and every other seat has to cross it to reach the door out)
    local GATE = { maiden = { 49, 30 }, bloat = { 23, 31 }, nylocas = { 31, 31 }, sotetseg = { 15, 19 }, xarpus = { 34, 43 } }
    local EXIT = { maiden = { 40, 6 }, bloat = { 5, 31 }, nylocas = { 39, 51 }, sotetseg = { 15, 5 }, xarpus = { 33, 48 } }

    -- Which side of each gate is OUT of the arena (local tiles).
    local OUT = {
        maiden = function(x, z) return x > 49 end, bloat = function(x, z) return x < 23 end,
        nylocas = function(x, z) return z > 31 end, sotetseg = function(x, z) return z < 19 end,
        xarpus = function(x, z) return z > 43 end,
    }
    -- THE DOOR: the room's entry tile, on the corridor side of its barrier
    -- (tob.constant ^tob_<room>_entry_lx/lz, tob_raid.rs2 ~tob_room_entry).
    -- Every seat walks there from the far end of the corridor it arrived in
    -- before the room's ready barrier, so the room's start-state sync is behind
    -- a barrier every seat reached on foot. Verzik has none: her chamber is
    -- walked into on arrival (~tob_room_arrive) and started by talking to her.
    local DOOR = { maiden = { 52, 30 }, bloat = { 42, 31 }, nylocas = { 31, 33 }, sotetseg = { 15, 17 }, xarpus = { 34, 24 } }
    -- The room's own square. The Maiden is the exception: the lobby lands the
    -- party at the DOOR end of her walkway, (79,31) in her two-square instance
    -- (tob.constant ^tob_maiden_arrive_lx), so the arrival square is the second
    -- one and her room is the square west of it (relay h1: every gate and exit
    -- 64 tiles east, "the gate at 49,30: not_found; at -29,32").
    local function origin_of(t, name)
        local _, here = t.world.tile()
        local ox = math.floor(here.x / 64) * 64
        if name == "maiden" and here.x - ox < GATE.maiden[1] then ox = ox - 64 end
        return ox, math.floor(here.z / 64) * 64, here
    end

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

    -- Walk to the room's door (DOOR), one walk order, the server pathing.
    local function walk_to_door(t, name, ox, oz)
        local d = DOOR[name]
        if d == nil then return true end
        local tx, tz = ox + d[1], oz + d[2]
        local at, sent, me = false, nil, nil
        for i = 1, 90 do
            local _tr
            _tr, me = t.world.tile()
            if me.x == tx and me.z == tz then at = true break end
            if sent == nil or i - sent >= 30 then
                t.raid.tob_move(tx, tz)
                sent = i
            end
            t.ticks(1)
        end
        t.check(name .. ".door", at, P .. "walked to the door at " .. d[1] .. "," .. d[2] .. "; at "
            .. (me.x - ox) .. "," .. (me.z - oz))
        return at
    end

    -- A seat let out of the cage when the room was won stands at the room's
    -- entry, outside the arena (tob_spectate.rs2 ~tob_spectate_tick). Bloat is
    -- the one room whose entry barrier is not its way out (x40 in, x23 out):
    -- that seat crosses back in first, then walks out with the rest.
    local function into_arena(t, name, ox, oz)
        if name ~= "bloat" then return end
        local _tr, me = t.world.tile()
        if me.x - ox <= 40 then return end
        for _ = 1, 4 do
            t.key("escape")
            t.raid.tob_loc_op("tob_arena_barrier", 1)
            t.ticks(5)
            _tr, me = t.world.tile()
            if me.x - ox <= 40 then return end
        end
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
        -- BUDGET-DRIVEN (tob2_budget.lua NEED, this seat, the rooms after this
        -- chest; 2026-10-12, relay_bud_128: 4 of the first 9 failures were
        -- seat 1 starving in P3 -- 18 restore doses and 108-212 hp of healing,
        -- its slots filled with restores to the old 16-dose target): restores
        -- only up to the later rooms' need, then brews (~64 hp a slot) while
        -- the healing carried is under the later rooms' need, sharks after;
        -- then the rest as before. The slot reserves (`keep`) stand. Without
        -- the budget, the old targets.
        local Bu = QD.T2 and QD.T2.budget
        if Bu and Bu.later_need then
            target_p = Bu.later_need(role, name, "restore")
            local heal_need = 16 * Bu.later_need(role, name, "brew") + 20 * Bu.later_need(role, name, "food")
            local function heal_to_need()
                for _ = 1, 20 do
                    if heal_hp() >= heal_need then break end
                    if not ((points >= CHEST_COST.brew and buy("brew")) or buy("shark")) then break end
                end
            end
            -- HEALING FIRST WHEN SHORT (relay_r13_128 f17: seat 4 tanked
            -- Maiden on all 24 brew doses, Bloat's chest bought it "restore
            -- restore shark" with 0 brews, and it died in Sotetseg): a seat
            -- under its later healing need buys brews, then sharks, to that
            -- need before any restore
            if heal_hp() < heal_need then heal_to_need() end
            for _ = 1, 8 do if prayer_doses() >= target_p or not buy("restore") then break end end
            heal_to_need()
            for _ = 1, 20 do if not heal(0) then break end end
        else
            for _ = 1, 20 do if heal_hp() >= 20 * floor_f or not heal(prayer_reserve()) then break end end
            for _ = 1, 8 do if prayer_doses() >= target_p or not buy("restore") then break end end
            for _ = 1, 20 do if not heal(0) then break end end
        end
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
        -- v2 rooms (TOB2_DESIGN.md): `opts.v2 = { maiden = true, ... }` plays
        -- those rooms with QD.raid.<room>_v2_solve; every other room stays v1
        local suffix = (opts.v2 and opts.v2[name]) and "_v2" or SUFFIX
        local solve = t.raid[name .. suffix .. "_solve"]
        assert(solve, "tob_relay: no solver " .. name .. suffix .. "_solve")
        local result, detail = solve({ max_ticks = MAX[name] })
        t.check(name .. ".solve", result == "ok", P .. string.sub(tostring(detail), 1, opts.detail or 900))
        return result == "ok"
    end

    -- THE HEALING IS SHARED AT VERZIK'S DOOR. Her P3 is paid for by the tick --
    -- her autos land on every raider -- and with void and whips it runs ~390
    -- ticks, so a seat's brews and fish are its life there. The team carried
    -- enough; the wrong seat carried it: seat 3 (no slots at Bloat's chest, all
    -- its points at Sotetseg's) came in on 24-36 brew doses and left 18-23 at her
    -- death, while every P3 death was seat 1 or 2 out of healing (rl66, 19
    -- relays; rl69 ri: seat 1 in on 0 brews and 6 fish, dead, the other two
    -- finishing with 14 and 16 brew doses). So, as teams do: a seat above
    -- SHARE_HP of healing (16 a brew dose, 20 a fish) drops its extra full brews
    -- and fish on its tile, keeping SHARE_HP; then, one seat at a time in role
    -- order, a seat below it takes them up to it. Holding Bloat's points for
    -- Sotetseg's chest instead starved the middle rooms (rl67 rn: dead in
    -- Sotetseg). The floor is the message: no seat needs another's inventory.
    -- Supplies only -- no equipment changes hands.
    -- 260, not 300 (2026-10-12, relay_bud_128): givers kept 300 while the team
    -- held ~270 a seat in the failing relays (f36: 120/140/352/332/408), so a
    -- low seat could never be filled; 260 levels toward the team instead
    local SHARE_HP = 260
    -- (slots the exchange never fills: P1's Dawnbringer is dropped by one seat
    -- for the next, and a seat the exchange had filled to the last slot could not
    -- take it - P1 ran 380 ticks and raiders died to it, rl83 rm, tc)
    -- ONE slot, not two (2026-10-12): the Dawnbringer is one-handed
    -- (all.obj verzik_special_weapon wearpos2 default), so its pickup needs one
    -- slot, and P3's two-handed spec swap needs one for the parrying dagger, never
    -- at the same time. The second slot was a full brew the freezer seat could
    -- not take (relay_bud_128 f36: seat 1 on 120 hp of healing took nothing)
    local SHARE_KEEP_SLOTS = 1
    local SHARE_SOTE = 200
    local BREW_FULL = { "br_4dosepotionofsaradomin", "4dosepotionofsaradomin" }
    local FISH = { "anglerfish", "shark", "mantaray", "seaturtle" }
    local function healing_hp(t)
        local d, f = 0, 0
        for k = 1, 4 do
            for _, nm in ipairs({ "br_" .. k .. "dosepotionofsaradomin", k .. "dosepotionofsaradomin" }) do
                local r, c = t.inv.count(nm)
                d = d + k * ((r == "ok" and tonumber(c)) or 0)
            end
        end
        for _, nm in ipairs(FISH) do
            local r, c = t.inv.count(nm)
            f = f + ((r == "ok" and tonumber(c)) or 0)
        end
        return 16 * d + 20 * f
    end
    -- (room, level): the exchange runs at a room's door; LEVEL is what a giver
    -- keeps and a taker fills to. Verzik's door at level; Sotetseg's at
    -- SHARE_SOTE (2026-10-12, relay_r10_128: 3 of 5 failures were a Sotetseg
    -- seat out of brews - f74 seats 1/2 came in on 7 and 8 brew doses with
    -- seat 5 on 31; four pairs at the 75 worst case need ~10-12 doses)
    -- THE EXCHANGE ON THE SUPPLY BOARD (owner 2026-10-12: "You can use the
    -- chat to coordinate which seats get what resources"; real public chat on
    -- both lanes, tob2_budget.lua THE BOARD). Every seat says its healing,
    -- givable full brews and fish,
    -- free slots past SHARE_KEEP_SLOTS and its later rooms' healing need;
    -- every seat computes the same allocation (QD.T2.budget.allocate: the
    -- team's healing split by need, largest deficit from largest surplus,
    -- whole items); a giver drops exactly its items, then each taker in role
    -- order takes exactly its own from its giver's tile. Supplies only.
    -- (relay_r13_128 f17: takers filled to the level in role order from a
    -- pool short of it, and seat 4, on 0, found the floor empty and died.)
    local BOARD_AFTER = { sotetseg = "nylocas", verzik = "xarpus" }
    local function share_by_board(t, room)
        drop_vials(t)
        local Bu = QD.T2.budget
        local t0 = api_drive.tick()
        local function count(nm) local r, c = t.inv.count(nm) return (r == "ok" and tonumber(c)) or 0 end
        local full, fish = 0, 0
        for _, nm in ipairs(BREW_FULL) do full = full + count(nm) end
        for _, nm in ipairs(FISH) do fish = fish + count(nm) end
        local after = BOARD_AFTER[room]
        local need = 16 * Bu.later_need(role, after, "brew") + 20 * Bu.later_need(role, after, "food")
        local before = healing_hp(t)
        local mine = { before, full, fish, math.max(0, free_slots(t) - SHARE_KEEP_SLOTS), need }
        -- (the exchange number is the same on every seat: a count every seat
        -- keeps alike, one per exchange this relay)
        -- (the read starts BEFORE the barrier: a seat released a tick later
        -- must not start reading after a line already arrived)
        local since = QD.T2.board.mark()
        t.expect(room .. ".barrier.board", t.party.barrier(room .. "_board", 300))
        Bu.board_seq = (Bu.board_seq or 0) + 1
        local seq = Bu.board_seq
        local sr = QD.T2.board.say(seq, role, mine)
        t.check(room .. ".board_said", sr == "ok", P .. "say answered " .. tostring(sr))
        local names = t.party.names()
        local got, deadline = {}, api_drive.tick() + 30
        while true do
            since = QD.T2.board.collect(seq, names, since, got)
            local all = true
            for r = 1, SEATS do if got[r] == nil then all = false end end
            if all then break end
            assert(api_drive.tick() <= deadline, "tob_relay: the supply board for " .. room .. " never filled")
            t.ticks(1)
        end
        local rows = {}
        for r = 1, SEATS do
            local v = got[r]
            rows[r] = { heal = v[1], full = v[2], fish = v[3], slots = v[4], need = v[5] }
        end
        local moves = Bu.allocate(rows)
        -- give: exactly my items
        local gave, took, missed = 0, 0, 0
        for _, m in ipairs(moves) do
            if m.from == role then
                local list = (m.kind == "brew") and BREW_FULL or FISH
                local dropped = false
                for _, nm in ipairs(list) do
                    if not dropped and count(nm) > 0 and t.player.drop(nm) == "ok" then
                        gave, dropped = gave + 1, true
                    end
                end
            end
        end
        t.expect(room .. ".barrier.given", t.party.barrier(room .. "_given", 300))
        -- take: each seat in role order, exactly its own, from its giver's tile
        local ids = {}
        for _, nm in ipairs(BREW_FULL) do local r, id = api_drive.symbol("obj", nm) if r == "ok" then ids[id] = "brew" end end
        for _, nm in ipairs(FISH) do local r, id = api_drive.symbol("obj", nm) if r == "ok" then ids[id] = "fish" end end
        for r = 1, SEATS do
            if role == r then
                for _, m in ipairs(moves) do
                    if m.to == role then
                        local giver = QD.party.name(m.from)
                        local gx, gz = nil, nil
                        local pr, players = api_drive.players()
                        if pr == "ok" then
                            for _, p in ipairs(players) do
                                if QD.party._same(p.name, giver) then gx, gz = p.server_x or p.x, p.server_z or p.z end
                            end
                        end
                        local row, bd = nil, nil
                        local orr, objs = api_drive.objs(15)
                        if orr == "ok" and gx then
                            for _, o in ipairs(objs) do
                                if ids[o.obj_id] == m.kind then
                                    local dd = math.max(math.abs(o.x - gx), math.abs(o.z - gz))
                                    if bd == nil or dd < bd then row, bd = o, dd end
                                end
                            end
                        end
                        local got = false
                        if row then
                            local h0 = healing_hp(t)
                            if api_drive.world_op("obj", row.obj_id, 3, row.element_id) == "ok" then
                                for _ = 1, 12 do
                                    t.ticks(1)
                                    if healing_hp(t) > h0 then got = true break end
                                end
                            end
                        end
                        if got then took = took + 1 else missed = missed + 1 end
                    end
                end
            end
            t.expect(room .. ".barrier.took_" .. r, t.party.barrier(room .. "_took_" .. r, 300))
        end
        t.check(room .. ".share", missed == 0, P .. string.format(
            "board: healing %d -> %d hp (need %d; gave %d, took %d, missed %d; %d moves; %d ticks)",
            before, healing_hp(t), need, gave, took, missed, #moves, api_drive.tick() - t0))
    end

    local function share_healing(t, room, level)
        if QD.T2 and QD.T2.budget and QD.T2.budget.allocate and QD.T2.board and BOARD_AFTER[room] then
            return share_by_board(t, room)
        end
        drop_vials(t)
        local before, gave, took = healing_hp(t), 0, 0
        -- give: a full brew (64) while that leaves level, then fish (20)
        for _ = 1, 12 do
            local dropped = false
            for _, list in ipairs({ { BREW_FULL, 64 }, { FISH, 20 } }) do
                if not dropped and healing_hp(t) - list[2] >= level then
                    for _, nm in ipairs(list[1]) do
                        local r, c = t.inv.count(nm)
                        if not dropped and r == "ok" and (tonumber(c) or 0) > 0 then
                            if t.player.drop(nm) == "ok" then gave = gave + 1 end
                            dropped = true
                        end
                    end
                end
            end
            if not dropped then break end
        end
        t.expect(room .. ".barrier.shared", t.party.barrier(room .. "_shared", 300))
        for r = 1, SEATS do
            if role == r then
                for _ = 1, 12 do
                    if healing_hp(t) >= level or free_slots(t) <= SHARE_KEEP_SLOTS then break end
                    local picked = false
                    for _, nm in ipairs({ BREW_FULL[1], BREW_FULL[2], FISH[1], FISH[2], FISH[3], FISH[4] }) do
                        if not picked and t.world.obj_near(nm, 15) == "ok" and t.player.click_obj(nm, 3) == "ok" then
                            took = took + 1
                            picked = true
                        end
                    end
                    if not picked then break end
                end
            end
            t.expect(room .. ".barrier.took_" .. r, t.party.barrier(room .. "_took_" .. r, 300))
        end
        -- and nothing is left on the floor: what nobody needed goes back up,
        -- ONE ITEM A SEAT A PASS in role order, so a surplus spreads instead of
        -- landing on seat 1 (tl dropped two brews no seat needed, lost; then a
        -- role-order sweep put seat 1 on 584 and 544 with the others on ~310)
        for pass = 1, 4 do
            for r = 1, SEATS do
                if role == r and free_slots(t) > SHARE_KEEP_SLOTS then
                    for _, nm in ipairs({ BREW_FULL[1], BREW_FULL[2], FISH[1], FISH[2], FISH[3], FISH[4] }) do
                        if t.world.obj_near(nm, 15) == "ok" and t.player.click_obj(nm, 3) == "ok" then
                            took = took + 1
                            break
                        end
                    end
                end
                t.expect(room .. ".barrier.left_" .. pass .. "_" .. r, t.party.barrier(room .. "_left_" .. pass .. "_" .. r, 300))
            end
        end
        t.check(room .. ".share", true, P .. string.format("healing %d -> %d hp (gave %d, took %d)",
            before, healing_hp(t), gave, took))
    end

    local function verzik(t)
        share_healing(t, "verzik", SHARE_HP)
        local prep = t.raid.verzik_p1_prepare()
        t.check("verzik.prepare", prep ~= nil, P .. (prep and (prep.pillars .. " pillars searched") or "the room never came into view"))
        t.expect("verzik.barrier.ready", t.party.barrier("verzik_ready", 600))
        t.cheat("::synctimers", false)
        t.exec("verzik.prayer", t.prayer.set, "protectfrommagic", true)
        if role == 1 then
            -- one packet, as the gates are: the pointer's camera framing cost
            -- live four ticks scriptrun did not spend (tob.lua tob_npc_op)
            local tr = t.raid.tob_npc_op("verzik_initial", 1)
            t.check("verzik.talk", tr == "ok", P .. tostring(tr))
            -- the server walks me to her first: her line opens on arrival
            for _ = 1, 30 do
                if t.chat.kind() ~= "none" then break end
                t.ticks(1)
            end
            local cr, cd = t.chat.play({ "npc:So, you wish to entertain me", "options", "choose:Yes, begin the fight." })
            t.check("verzik.begin", cr == "ok", tostring(cd))
        end
        t.expect("verzik.barrier.started", t.party.barrier("verzik_started", 900))
        t.check("verzik.supplies_in", true, P .. supplies_text(t))
        local r1, d1 = t.raid["verzik_p1" .. ((opts.v2 and opts.v2.verzik_p1) and "_v2" or SUFFIX) .. "_solve"]({ max_ticks = 400, prep = prep, weapon = "abyssal_tentacle" })
        t.check("verzik.p1", r1 == "ok", P .. string.sub(tostring(d1), 1, opts.detail or 600))
        if r1 ~= "ok" then return false end
        local r2, d2 = t.raid["verzik_p2" .. ((opts.v2 and opts.v2.verzik_p2) and "_v2" or SUFFIX) .. "_solve"]({ max_ticks = 900, base = prep.base, weapon = "abyssal_tentacle" })
        t.check("verzik.p2", r2 == "ok", P .. string.sub(tostring(d2), 1, opts.detail or 600))
        if r2 ~= "ok" then return false end
        -- (the P2 solver released the reserve at her reds; again here for a P2
        -- that ended before her bar got there)
        t.raid.tob_reserve_combats(0)
        t.check("verzik.p3_supplies", true, P .. supplies_text(t))
        local p3_suffix = (opts.v2 and opts.v2.verzik_p3) and "_v2" or SUFFIX
        local r3, d3 = t.raid["verzik_p3" .. p3_suffix .. "_solve"]({ max_ticks = 900, base = prep.base, weapon = "abyssal_tentacle" , spec_weapon = SPEC_WEAPON })
        t.check("verzik.supplies_left", true, P .. supplies_text(t))
        t.check("verzik.p3", r3 == "ok", P .. string.sub(tostring(d3), 1, opts.detail or 600))
        -- a P3 death respawns ~6 ticks later and stays in lock step, so no
        -- player.died row is ever written for it: name it from the solve's answer
        if r3 == "died" then t.check("verzik.p3_died", false, P .. "died in P3 (the solve answered died)") end
        return r3 == "ok"
    end

    -- Every raider: the trapdoor, the leader's chest, the teleport out.
    local function reward_room(t)
        -- ONE PACKET a press, the server walking me, as the gates and Verzik's
        -- talk are: click_loc frames the camera first and live spent ticks there
        -- that scriptrun did not (relay ra: the teleport out 4 ticks apart).
        local dr = "not_found"
        for _ = 1, 20 do
            dr = t.raid.tob_loc_op("tob_dungeon_verzik_throne_door_opened", 1)
            if dr == "ok" then break end
            t.ticks(2)
        end
        -- down the trapdoor: the server walks me there first, so the chest is
        -- looked for until it is in view
        -- (the five chest symbols in ONE C-filtered read, api_drive.locs_of:
        -- five world.loc_near calls a tick each walked the whole scenery pool
        -- twice, the relay's heaviest tick after P3, 128k instructions, c22m)
        local chest_sym, chest_at = nil, nil
        local chest_ids, chest_syms = {}, {}
        for k = 0, 4 do
            local sym = "tob_treasureroom_chest_loc" .. k
            local sr, id = api_drive.symbol("loc", sym)
            if sr == "ok" then chest_ids[#chest_ids + 1], chest_syms[#chest_syms + 1] = id, sym end
        end
        for _ = 1, 40 do
            local lr, rows = "not_found", nil
            if #chest_ids > 0 then lr, rows = api_drive.locs_of(30, chest_ids) end
            if lr == "ok" then
                for k, id in ipairs(chest_ids) do
                    for _, row in ipairs(rows) do
                        if row.loc_id == id or row.resolved_loc_id == id then chest_sym = chest_syms[k] break end
                    end
                    if chest_sym ~= nil then break end
                end
            end
            if chest_sym ~= nil then break end
            t.ticks(1)
        end
        t.check("raid.reward_room", dr == "ok" and chest_sym ~= nil, P .. "trapdoor " .. tostring(dr)
            .. "; chest " .. tostring(chest_sym))
        if role == 1 and chest_sym ~= nil then
            local cr
            cr, chest_at = t.raid.tob_loc_op(chest_sym, 1)
            -- walked to it, and opened
            for _ = 1, 20 do
                local _, me = t.world.tile()
                if chest_at and math.abs(me.x - chest_at.x) <= 2 and math.abs(me.z - chest_at.z) <= 2 then break end
                t.ticks(1)
            end
            t.ticks(3)
            t.check("raid.reward_chest", cr == "ok", tostring(cr))
        end
        t.expect("party.barrier.looted", t.party.barrier("looted", 600))
        local xr = t.raid.tob_loc_op("tob_treasureroom_teleportout", 1)
        local here, home = nil, false
        for _ = 1, 40 do
            local _tr
            _tr, here = t.world.tile()
            home = math.abs(here.x - LOBBY_X) <= 20 and math.abs(here.z - LOBBY_Z) <= 20
            if home then break end
            t.ticks(1)
        end
        t.check("raid.lobby", home, P .. "teleport out " .. tostring(xr) .. "; at " .. here.x .. "," .. here.z)
    end

    local function run(t)
        if role == 1 then t.check("relay.ticklog", t.ticklog.start() == "ok", "") end

        -- THE WAY IN: t.raid.enter on both lanes, so the live and scriptrun tick
        -- logs are the same log (the board's party and the door's ready check
        -- spend the RNG differently, and scriptrun cannot run the board: it is
        -- clientscripts). The board is test/raids/tob_lobby.lua, live only.
        local r, d = t.raid.enter("tob", ORDER[1], { mode = mode })
        t.check("party.enter", r == "ok", P .. tostring(d))
        -- two doses a seat are Verzik's P2 second part and P3: no room before
        -- drinks below this (owner 2026-10-12); the P2 solver releases them when
        -- her bar reaches the reds (QD.VZP2.REDS_NEAR)
        t.raid.tob_reserve_combats(2)

        local cleared = 0
        local alive = true
        local from = nil
        for _, name in ipairs(ORDER) do
            -- THE ARRIVAL: in the room's own square (the passage carried us)
            if from ~= nil then await_square(t, from, 60) end
            t.expect(name .. ".barrier.arrived", t.party.barrier(name .. "_arrived", 3000))
            local ox, oz = origin_of(t, name)
            from = t.raid.tob_square()
            -- back up to full before the room (the solvers' own eating is for
            -- inside it)
            if alive then
                local hp, pr = t.raid.tob_top_up(30)
                t.check(name .. ".top_up", true, P .. "hitpoints " .. tostring(hp) .. ", prayer " .. tostring(pr)
                    .. "; " .. supplies_text(t) .. "; free slots " .. free_slots(t))
                -- THE NYLOCAS' TWO-HANDED SWAP needs a free slot: every seat wears
                -- the defender, and the blowpipe cannot go on with nowhere to put
                -- it - the wield is refused, and seat 3 of relay h23 tg stood
                -- through all three of Vasilias' ranged windows re-sending it.
                -- A full pack drops its least-worth item here.
                if name == "nylocas" and free_slots(t) < 1 then
                    local dropped = nil
                    for _, nm in ipairs({ "br_1dosepotionofsaradomin", "br_1dose2restore", "anglerfish", "br_2dosepotionofsaradomin" }) do
                        local r, c = t.inv.count(nm)
                        if dropped == nil and r == "ok" and (tonumber(c) or 0) > 0 and t.player.drop(nm) == "ok" then dropped = nm end
                    end
                    t.check("nylocas.swap_slot", free_slots(t) >= 1, P .. "dropped " .. tostring(dropped)
                        .. " for the blowpipe's swap; free slots " .. free_slots(t))
                end
            end
            -- TO THE DOOR, every seat on foot, before the room's ready barrier
            walk_to_door(t, name, ox, oz)
            -- (a seat that failed a room still meets every barrier, fighting
            -- nothing: the party stays in lock step, relay rl5 hung 3000 ticks a
            -- barrier on a caged seat)
            -- (every room drinks its super combats, owner 2026-10-12: the
            -- reserve above is what keeps Verzik's two doses)
            local ok = false
            if alive then
                if name == "sotetseg" then share_healing(t, "sotetseg", SHARE_SOTE) end
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
            if alive then cleared = cleared + 1 end
            -- EVERY SEAT WALKS OUT ON ITS OWN: through the gate, to the supply
            -- chest after Bloat and after Sotetseg, then into the passage, which
            -- takes ME (and only me) to the next room. No barrier between: each
            -- seat goes when it is done, as raiders do; the next room's arrival
            -- barrier gathers them.
            if name ~= "verzik" then
                into_arena(t, name, ox, oz)
                through_gate(t, name, ox, oz)
                if alive and (name == "bloat" or name == "sotetseg") then restock(t, name) end
                if name ~= ORDER[#ORDER] and not leave_room(t, name, ox, oz) then break end
            end
        end
        if cleared == #ORDER and ORDER[#ORDER] == "verzik" then reward_room(t) end
        t.expect("party.barrier.end", t.party.barrier("end", 3000))
        if role == 1 then
            local _, dawn = t.inv.count("verzik_special_weapon")
            t.check("raid.complete", cleared == #ORDER, cleared .. " of " .. #ORDER .. " rooms cleared; Dawnbringer held " .. tostring(dawn))
        end
        t.finish(0)
    end
    return {
        id = ((mode == "normal") and "tob_relay" or ("tob_relay_" .. mode))
            .. ((opts.from or opts.to) and ("_" .. tostring(opts.from or "maiden") .. "_" .. tostring(opts.to or "verzik")) or ""),
        -- Hard Mode's door wants one completion (raid seam19); the debug landing
        -- t.raid.enter uses skips it, the fixture keeps the account honest
        fixture = (mode == "hard") and "tob_normal_done.ini" or "fresh_lumbridge.ini",
        party = SEATS,
        max_frames = 300000,   -- a relay is ~4100 ticks, 30 frames a tick; the gate ceiling is 480000
        setup = kit,
        run = run,
    }
end
