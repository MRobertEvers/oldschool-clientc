-- quest-driver / tob_relay: the whole Theatre of Blood, a trio, lobby to lobby,
-- in ONE mode. test/raids/tob_relay.lua (Normal) and tob_relay_hard.lua
-- (Hard) are this, by mode: QD_ROOT.raid.tob_relay_test(mode) -> the test.
--
-- tob_relay: the whole Theatre of Blood, Normal, a trio, lobby to lobby, every
-- room played by its solver (script/plugins/quest_driver/tob_*.lua and
-- raid_solve_verzik_p1/p2/p3.lua) in ONE Learner/Void kit
-- (docs/minigames/theater_of_blood/ROOM_SOLVERS.md 6, 6.1):
--
--   the party in at Maiden through t.raid.enter, on both lanes (the board's
--     way in is test/raids/tob_lobby.lua, live only);
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

-- -> { id, fixture, party, max_frames, setup, run } for `mode` ("normal" or
-- "hard"); called while the test file loads, so QD_PARTY is already set.
-- `opts.from` / `opts.to` run a stretch of the raid (a room probe: the party
-- lands at `from` through t.raid.enter and stops after `to`); the kept tests
-- run the whole of it.
-- `opts.fixture` / `opts.party` / `opts.max_frames` are a kept test file's own
-- declaration: the Scripts tab reads a test file's TEXT, not this table
-- (tools/raid_gate/prepare_scripts.py), so the file spells them out, and each
-- one given is asserted to be this relay's.
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
    -- barrage); Maiden's hammer and salve. The Athanatos is whipped: the
    -- tentacle's hit is poisonous.
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
    -- P3'S SPECIAL WEAPON (owner, 2026-10-12: "is there room in the
    -- inventories for a spec weapon that we can use to speed up P3?"): one
    -- slot on every Hard seat, the P3 solver switching to it for a special
    -- and back (QD.raid._vzp3_special). opts.spec_weapon names another
    -- (raid_solve_verzik_p3.lua QD.VZP3.SPEC_WEAPONS), false carries none.
    local SPEC_WEAPON = nil
    if mode == "hard" and opts.spec_weapon ~= false then SPEC_WEAPON = opts.spec_weapon or "dragon_claws" end
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
    -- THE BOOKS (owner, 2026-10-12: "3 seats on ancients and 2 on arceus and
    -- the 2 on arceus use thralls"; "the other roles also bring magic and
    -- switch to blood barrage after the freezers have frozen the crabs"; "You
    -- need an imbued rune pouch for that"): seats 1 and 2 freeze (Ice
    -- Barrage: water, death, blood), seat 3 freezes in the Nylocas and blood
    -- barrages Maiden's frozen crabs (a divine pouch's fourth slot: soul),
    -- seats 4 and 5 are on Arceuus for greater thralls (fire, blood, cosmic).
    -- Normal keeps every seat on Ancients with the three-slot pouch.
    local book, pouch = 1, {
        "::give bh_rune_pouch 1",
        "::setvar varb29_rune_pouch_type_1 2", "::setvar varb1624_rune_pouch_quantity_1 600",
        "::setvar varb1622_rune_pouch_type_2 7", "::setvar varb1625_rune_pouch_quantity_2 400",
        "::setvar varb1623_rune_pouch_type_3 8", "::setvar varb1626_rune_pouch_quantity_3 200",
    }
    if mode == "hard" and role == 3 then
        pouch = {
            "::give divine_rune_pouch 1",
            "::setvar varb29_rune_pouch_type_1 2", "::setvar varb1624_rune_pouch_quantity_1 400",
            "::setvar varb1622_rune_pouch_type_2 7", "::setvar varb1625_rune_pouch_quantity_2 600",
            "::setvar varb1623_rune_pouch_type_3 8", "::setvar varb1626_rune_pouch_quantity_3 600",
            "::setvar varb14285_rune_pouch_type_4 13", "::setvar varb14286_rune_pouch_quantity_4 150",
        }
    elseif mode == "hard" and (role == 4 or role == 5) then
        book = 3
        pouch = {
            "::give bh_rune_pouch 1",
            "::setvar varb29_rune_pouch_type_1 4", "::setvar varb1624_rune_pouch_quantity_1 1000",
            "::setvar varb1622_rune_pouch_type_2 8", "::setvar varb1625_rune_pouch_quantity_2 500",
            "::setvar varb1623_rune_pouch_type_3 9", "::setvar varb1626_rune_pouch_quantity_3 100",
        }
    end
    kit[#kit + 1] = "::~charge toxic_tots_charged 2500"
    kit[#kit + 1] = "::give dragon_warhammer 1"
    for _, line in ipairs(pouch) do kit[#kit + 1] = line end
    kit[#kit + 1] = "::setvar varb4070_spellbook " .. book
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
    -- (no serpentine helm on any seat, owner 2026-10-12: the tentacle's hit
    -- is a poisonous one, so a whip pops the Athanatos - tob_damage.rs2
    -- ~tob_hit_is_poisonous)

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
    local COMBAT_AFTER = { maiden = 7, bloat = 6, nylocas = 5, sotetseg = 3, xarpus = 2, verzik = 2 }
    local NEXT = { maiden = "bloat", bloat = "nylocas", nylocas = "sotetseg", sotetseg = "xarpus", xarpus = "verzik" }
    local MAX = { maiden = 600, bloat = 600, nylocas = 700, sotetseg = 700, xarpus = 900 }
    if mode == "hard" then
        -- (Hard Mode's rooms are longer: Bloat 2400 at five, Xarpus 6000)
        MAX = { maiden = 900, bloat = 1400, nylocas = 1000, sotetseg = 1000, xarpus = 1400 }
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
        -- (HALF the points at most: short of both, the reserve took all 12 of
        -- a seat's points at Sotetseg's chest and it bought four restores with
        -- no brew or fish left - Verzik on no healing at all, relay hmt_live2
        -- seats 2 and 3)
        local function prayer_reserve()
            local short = math.max(0, target_p - prayer_doses())
            local want = ((short + 3) // 4) * CHEST_COST.restore
            local half = ((p0 // 2) // CHEST_COST.restore) * CHEST_COST.restore
            return math.min(want, half)
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
        -- v2 rooms (TOB2_DESIGN.md): `opts.v2 = { maiden = true, ... }` plays
        -- those rooms with QD.raid.<room>_v2_solve; every other room stays v1
        local suffix = (opts.v2 and opts.v2[name]) and "_v2" or SUFFIX
        local solve = t.raid[name .. suffix .. "_solve"]
        assert(solve, "tob_relay: no solver " .. name .. suffix .. "_solve")
        local result, detail = solve({ max_ticks = MAX[name], spec_weapon = SPEC_WEAPON })
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
    local SHARE_HP = 300
    local SHARE_LOW = {}
    local SHARE_CALL = "need heals"
    -- (slots the exchange never fills: P1's Dawnbringer is dropped by one seat
    -- for the next, and a seat the exchange had filled to the last slot could not
    -- take it - P1 ran 380 ticks and raiders died to it, rl83 rm, tc)
    local SHARE_KEEP_SLOTS = 2
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
    -- THE SHARE, AT EVERY ROOM'S DOOR (SHARE_AT: owner, 2026-10-12, "You need
    -- to get those to 100%"). The team carries enough and the wrong seat
    -- carries it: of 128 relays, 26 lost a seat in Sotetseg that walked in on
    -- 2-6 brew doses beside a seat on 29-33 (all128 r003, r006, r017); the room
    -- alone, every seat on a full kit, cleared 127 of 128 (so128). So before
    -- the rooms that cost most, each seat above the room's target drops its
    -- extra full brews and fish, and the seats below it take them back ONE ITEM
    -- A SEAT A PASS in role order (role order whole, seat 1 took all of them).
    -- What is left goes back up the same way. A caged seat meets the barriers.
    local function share_healing(t, room, target, alive)
        if alive then drop_vials(t) end
        local before, gave, took = alive and healing_hp(t) or 0, 0, 0
        -- THE TRIGGER, SAID IN CHAT (owner, 2026-10-12: "Any seat with no
        -- healing or too little healing should trigger the split"; "You should
        -- have them communicate with chat"): a seat under the room's line says
        -- so in public chat, as a raider would; two ticks after the barrier
        -- every seat has the line in its chatbox (MESSAGE_PUBLIC, back as each
        -- raider's PLAYER_INFO chat block), so all of them split or none does,
        -- the same on both lanes
        local low = SHARE_LOW[room] or 0
        -- (every seat's read cursor is taken BEFORE a barrier, and nobody
        -- speaks until it is passed: a seat a tick ahead said its line before
        -- three others took their cursors, they read "no split" and never met
        -- the shared barrier - all128e r023, Sotetseg)
        local since = select(2, api_drive.message_serial()) or 0
        t.expect(room .. ".barrier.presay", t.party.barrier(room .. "_presay", 300))
        if alive and before < low then api_drive.say(SHARE_CALL) end
        t.expect(room .. ".barrier.lowcheck", t.party.barrier(room .. "_lowcheck", 300))
        t.ticks(2)
        local anyone = false
        local mr, rows = api_drive.messages()
        if mr == "ok" then
            for _, m in ipairs(rows) do
                if m.serial > since and m.type == 2 and m.text and m.text:lower() == SHARE_CALL:lower() then anyone = true end
            end
        end
        if not anyone then
            if alive then t.check(room .. ".share", true, P .. string.format("healing %d hp: no seat under %d, no split", before, low)) end
            return
        end
        -- give: a full brew (64) while that leaves the target, then fish (20)
        -- (target 0: EVERY full brew and fish, then the rounds below share
        -- them out evenly - with the team under the old 300 line nobody gave:
        -- all21 sa reached P3 on 760 hp of healing, two seats on none)
        for _ = 1, 28 do
            if not alive then break end
            local dropped = false
            for _, list in ipairs({ { BREW_FULL, 64 }, { FISH, 20 } }) do
                if not dropped and healing_hp(t) - list[2] >= target then
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
        local function take_one()
            for _, nm in ipairs({ BREW_FULL[1], BREW_FULL[2], FISH[1], FISH[2], FISH[3], FISH[4] }) do
                if t.world.obj_near(nm, 15) == "ok" and t.player.click_obj(nm, 3) == "ok" then
                    took = took + 1
                    return true
                end
            end
            return false
        end
        for pass = 1, 6 do
            for r = 1, SEATS do
                if alive and role == r and target > 0 and healing_hp(t) < target and free_slots(t) > SHARE_KEEP_SLOTS then take_one() end
                t.expect(room .. ".barrier.took_" .. pass .. "_" .. r, t.party.barrier(room .. "_took_" .. pass .. "_" .. r, 300))
            end
        end
        -- and nothing is left on the floor: what nobody needed goes back up,
        -- one item a seat a pass, so a surplus spreads instead of landing on
        -- seat 1 (tl dropped two brews no seat needed, lost; then a role-order
        -- sweep put seat 1 on 584 and 544 with the others on ~310)
        for pass = 1, (target > 0) and 4 or 12 do
            for r = 1, SEATS do
                if alive and role == r and free_slots(t) > SHARE_KEEP_SLOTS then take_one() end
                t.expect(room .. ".barrier.left_" .. pass .. "_" .. r, t.party.barrier(room .. "_left_" .. pass .. "_" .. r, 300))
            end
        end
        if alive then
            t.check(room .. ".share", true, P .. string.format("healing %d -> %d hp (gave %d, took %d)",
                before, healing_hp(t), gave, took))
        end
    end
    local SHARE_AT = { nylocas = 0, sotetseg = 0 }
    -- (the lines: what a seat spends in the rooms ahead, measured over 128
    -- relays - Nylocas 130 a seat, Sotetseg 205, Verzik 311, all128)
    SHARE_LOW = { nylocas = 150, sotetseg = 200, verzik = 300 }

    local function verzik(t)
        share_healing(t, "verzik", 0, true)
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
        local r3, d3 = t.raid["verzik_p3" .. p3_suffix .. "_solve"]({ max_ticks = 900, base = prep.base, weapon = "abyssal_tentacle",
                                                                 spec_weapon = SPEC_WEAPON })
        t.check("verzik.supplies_left", true, P .. supplies_text(t))
        t.check("verzik.p3", r3 == "ok", P .. string.sub(tostring(d3), 1, opts.detail or 600))
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
        local chest_sym, chest_at = nil, nil
        for _ = 1, 40 do
            for k = 0, 4 do
                local sym = "tob_treasureroom_chest_loc" .. k
                local lr, row = t.world.loc_near(sym, 30)
                if lr == "ok" and row ~= nil then chest_sym = sym break end
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
            if SHARE_AT[name] then share_healing(t, name, SHARE_AT[name], alive) end
            -- (a seat that failed a room still meets every barrier, fighting
            -- nothing: the party stays in lock step, relay rl5 hung 3000 ticks a
            -- barrier on a caged seat)
            -- (every room drinks its super combats, owner 2026-10-12: the
            -- reserve above is what keeps Verzik's two doses)
            -- HARD MODE BUDGETS THE EIGHT DOSES BY ROOM (owner, 2026-10-12:
            -- "Are they also using supercombat?"): the early rooms drank them
            -- down to Verzik's two before Sotetseg, 0-0.6 sips a seat there
            -- (all128c). The reserve before each room is what the rooms after
            -- it are owed - Maiden, Bloat, the Nylocas and Xarpus a dose,
            -- Sotetseg two, Verzik two - so a dose a room leaves rolls on.
            if mode == "hard" and COMBAT_AFTER[name] then t.raid.tob_reserve_combats(COMBAT_AFTER[name]) end
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
            if name ~= "verzik" and role == 1 and name ~= ORDER[#ORDER] then
                if name ~= "bloat" and name ~= "sotetseg" then through_gate(t, name, ox, oz) end
                if not leave_room(t, name, ox, oz) then break end
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
    local test = {
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
    for _, field in ipairs({ "fixture", "party", "max_frames" }) do
        assert(opts[field] == nil or opts[field] == test[field], "tob_relay_test(" .. mode .. "): the test file declares "
            .. field .. " = " .. tostring(opts[field]) .. ", the relay is " .. tostring(test[field]))
    end
    return test
end
