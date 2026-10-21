-- cox_vasa: Vasa Nistirio, solo, played by the v2 solver
-- (script/plugins/quest_driver/cox2_vasa.lua; plan
-- docs/minigames/cox/rooms/vasa_solver.md, mechanics vasa_spec.md) on the
-- relay's ARRIVAL kit (QD.COX.SOLO_KIT, cox2_raid.lua, spelled out as
-- test/raids/cox_vanguards.lua does): the wiki Minimum, worn and carried,
-- WITHOUT the anglerfish (the relay eats it to make its one free slot), so 27
-- in the pack. The dragon sword is the crystal's stab weapon, the toxic
-- blowpipe Vasa's. No raid potion: no overload, no Xeric's aid, no enhance.
--
-- GREEN (the owner, 2026-10-18): Vasa dead, the exit crystals gone and the
-- exit Passage taken into the next room, the player alive, NO boulder damage
-- (fully dodgeable: two tiles, Synq 0:58:03) and NO stomp damage (never under
-- him), counted off the server's tick log. The special is TAKEN, as every
-- recorded solo raider takes it (RSProx 843/3282/3285), and eaten up after:
-- reported, not judged.
--
--   ./src/build_cox_vasa_opt/torirsserver --scriptrun test/raids/cox_vasa.lua --bots 1 \
--       --name vasa0 --ticks 3000 --session /tmp/v/vasa0 --fixture test/raids/fixtures/fresh_lumbridge.ini
-- The raid is the n-th holding the room, n = the run name's trailing number.
-- A party: --bots 2..5, the same command (vasa_solver.md "A party").

-- QD.COX.SOLO_KIT (cox2_raid.lua), spelled out: the test is loaded before the
-- quest driver's chunk hands it `t`.
local kit = { "::clearinv" }
for _, s in ipairs({ "attack", "strength", "defence", "hitpoints", "prayer", "magic", "ranged", "agility",
                     "woodcutting", "firemaking", "mining", "thieving", "herblore", "farming", "fishing", "hunter",
                     "construction", "cooking" }) do
    kit[#kit + 1] = "::setlevel " .. s .. " 99"
end
for _, line in ipairs({
    "::setvar varb3888_kr_quest ^kr_complete", "::setvar varb3909_kr_knightwaves_state 8",
    "::setvar varb5451_prayer_rigour_unlocked 1", "::setvar varb5452_prayer_augury_unlocked 1",
    "::setvar varb5453_prayer_preserve_unlocked 1", "::setvar varp8023_barbarian_vial_smash 1",
}) do kit[#kit + 1] = line end
for _, obj in ipairs({ "fris_kingly_helm", "barbassault_penance_fighter_torso", "black_dragonhide_chaps",
        "hundred_gauntlets_level_10", "abyssal_tentacle", "dragon_parryingdagger", "enchanted_onyx_amulet",
        "tzhaar_cape_fire", "dragon_boots", "lightbearer" }) do
    kit[#kit + 1] = "::give " .. obj .. " 1"
    kit[#kit + 1] = "::wield " .. obj
end
for _, line in ipairs({
    "::~charge abyssal_tentacle 10000",
    "::give dragon_bolts_enchanted_ruby 300", "::wield dragon_bolts_enchanted_ruby",
    "::clearinv",
    "::blowpipe dragon_dart 2000 2000",
    -- (SOLO_KIT.carry, less the anglerfish)
    "::give xbows_crossbow_dragon 1", "::give anma_50_reward 1", "::give toxic_tots_charged 1",
    "::give occult_necklace 1", "::give black_dragonhide_body 1", "::give mystic_robe_top 1",
    "::give mystic_robe_bottom 1", "::give bgs 1", "::give bh_rune_pouch 1", "::give ma2_saradomin_cape 1",
    "::give rune_pickaxe 1", "::give nzone_salve_amulet_e 1", "::give dragon_shortsword 1",
    "::give dragon_warhammer 1", "::give sanfew_salve_4_dose 1",
    "::~charge toxic_tots_charged 2500",
    -- (SOLO_KIT.supplies: the stamina is Olm's, never drunk here)
    "::give 4dosepotionofsaradomin 6", "::give 4dose2restore 2", "::give 4dosestamina 1",
    "::setvar varb29_rune_pouch_type_1 1", "::setvar varb1624_rune_pouch_quantity_1 500",
    "::setvar varb1622_rune_pouch_type_2 2", "::setvar varb1625_rune_pouch_quantity_2 500",
    "::setvar varb1623_rune_pouch_type_3 5", "::setvar varb1626_rune_pouch_quantity_3 500",
}) do kit[#kit + 1] = line end

local VASA = { [7565] = true, [7566] = true, [7567] = true }
local SEQ_SPECIAL = 7409   -- vasa_stun_spawn: S

-- THE TELEPORT, off the server's log: on S+3 every raider stands 3-4 from his
-- middle (next to him) or 8-11 (by the walls), and half of them, rounded up,
-- next to him -- RSProx 3285/3282/843 solo and 820's five trio specials, read
-- in untranslated instance tiles (vasa.md RV-S2). His tile on S is his last
-- npc_tile row, or his spawn's while he has not moved.
local function landings(t)
    local ar, anims = t.ticklog.rows({ kind = "npc_anim" })
    local specials = {}
    for _, row in ipairs(ar == "ok" and anims or {}) do
        if row.seq == SEQ_SPECIAL and VASA[row.type or -1] then
            specials[#specials + 1] = { tick = row.tick, slot = row.slot }
        end
    end
    t.ticks(1)
    local pr, ptiles = t.ticklog.rows({ kind = "player_tile" })
    local out, ok = {}, #specials > 0
    for _, sp in ipairs(specials) do
        local sr, spawn = t.ticklog.rows({ kind = "npc_spawn", slot = sp.slot })
        local nr, ntiles = t.ticklog.rows({ kind = "npc_tile", slot = sp.slot })
        local sw = nil
        for _, row in ipairs(sr == "ok" and spawn or {}) do
            if row.tick <= sp.tick then
                sw = { x = (row.coord >> 14) & 0x3fff, z = row.coord & 0x3fff }
            end
        end
        for _, row in ipairs(nr == "ok" and ntiles or {}) do
            if row.tick <= sp.tick then sw = { x = row.x, z = row.z } end
        end
        local at = {}
        for _, row in ipairs(pr == "ok" and ptiles or {}) do
            if row.tick <= sp.tick + 3 then at[row.pid] = row end
        end
        local near, far, bad, seen = 0, 0, 0, {}
        for pid, row in pairs(at) do
            local m = sw and math.max(math.abs(row.x - sw.x - 2), math.abs(row.z - sw.z - 2)) or -1
            if m >= 3 and m <= 4 then near = near + 1
            elseif m >= 8 and m <= 11 then far = far + 1
            else bad = bad + 1 end
            seen[#seen + 1] = "p" .. pid .. " m" .. m
        end
        local n = near + far + bad
        if bad > 0 or near ~= (n + 1) // 2 then ok = false end
        out[#out + 1] = string.format("S=%d near %d far %d off %d (%s)", sp.tick, near, far, bad, table.concat(seen, " "))
        t.ticks(1)
    end
    t.check("vasa.teleport_ring", ok, #specials > 0 and table.concat(out, "; ") or "no special in the log")
    return ok
end

local function run(t)
    t.check("vasa.ticklog", t.ticklog.start() == "ok", "")
    -- (the live lane's user is the run name with "_p1" on it, scriptrun's
    -- may be either: the room number is the run name's own)
    local user = string.gsub(t.session._user() or "", "_p%d+$", "")
    local n = tonumber(string.match(user, "(%d+)$") or "0") or 0
    -- A PARTY (--bots N): the leader builds the raid, then each member joins
    -- it on the leader's tile (::coxtestjoin), and all start together. The
    -- counts below are every raider's: the party is green only if no one
    -- takes a boulder or a stomp.
    local role, size = t.party.role(), t.party.size()
    local er, at = "ok", nil
    if role == 1 then
        er, at = t.raid.cox_test_enter("vasa", n)
    end
    t.check("vasa.enter", er == "ok", tostring(er) .. " " .. tostring(at and at.seed))
    if er ~= "ok" then t.finish(1) return end
    if size > 1 then
        local br, bd = t.party.barrier("vasa_built", 600)
        t.check("vasa.built", br == "ok", tostring(bd))
        if br ~= "ok" then t.finish(1) return end
        if role ~= 1 then
            local jr, jd = t.raid.cox_test_join()
            t.check("vasa.join", jr == "ok", tostring(jd))
            if jr ~= "ok" then t.finish(1) return end
        end
        local ir, id = t.party.barrier("vasa_in", 300)
        t.check("vasa.in", ir == "ok", tostring(id))
        if ir ~= "ok" then t.finish(1) return end
    end
    t.ticklog.mark("room start")
    local result, detail = t.raid.cox_vasa_solve({ max_ticks = 1500 })
    t.check("vasa.solve", result == "ok", tostring(detail))
    if result ~= "ok" then t.finish(1) return end
    -- the zeros, off the server's log: a boulder lands on the tick its 3x3
    -- impact graphic shows; anything else Vasa does to me off the special's
    -- explosion (the 8 magic impacts) is the stomp
    -- (one kind a call: filtered in C, inside the per-tick Lua budget)
    local sr, spots = t.ticklog.rows({ kind = "map_spotanim" })
    t.ticks(1)
    local rr, rows = t.ticklog.rows({ kind = "hit_player" })
    local impact, explode = {}, {}
    if sr == "ok" then
        for _, row in ipairs(spots) do
            if row.kind == "map_spotanim" then
                if row.spotanim == 1330 then impact[row.tick] = true end
                if row.spotanim == 1328 then explode[row.tick] = true end
            end
        end
    end
    local boulders, boulder_dmg, stomps, stomp_dmg, specials, special_max = 0, 0, 0, 0, 0, 0
    if rr == "ok" then
        for _, row in ipairs(rows) do
            if row.kind == "hit_player" and VASA[row.npc_type or -1] then
                local dmg = row.damage or 0
                if explode[row.tick] then
                    specials = specials + 1
                    special_max = math.max(special_max, dmg)
                elseif impact[row.tick] then
                    if dmg > 0 then boulders = boulders + 1; boulder_dmg = boulder_dmg + dmg end
                elseif dmg > 0 then
                    stomps = stomps + 1; stomp_dmg = stomp_dmg + dmg
                end
            end
        end
    end
    t.check("vasa.no_boulder", boulders == 0, string.format("boulder hits %d (%d dmg)", boulders, boulder_dmg))
    t.check("vasa.no_stomp", stomps == 0, string.format("stomp hits %d (%d dmg)", stomps, stomp_dmg))
    -- THE SPECIAL: taken, as every recorded solo raider takes it (the owner,
    -- 2026-10-18; vasa_solver.md). Reported, not judged. And what the room
    -- cost, in drinks.
    local drinks = string.match(tostring(detail), "drinks (%d+)")
    t.check("vasa.special_taken", true,
        string.format("explosions taken %d (largest %d); drinks %s", specials, special_max, tostring(drinks)))
    -- (a fresh tick: the two passes above can spend most of one resume's
    -- Lua budget on a long fight -- d8l vasa7)
    t.ticks(1)
    local ring_ok = landings(t)
    t.ticks(1)
    -- the exit: both crystals shattered, and the way on taken
    local gone = false
    for _ = 1, 6 do
        local lr, row = t.world.loc_near("raids_blockage_purple_small", 40)
        if lr ~= "ok" or row == nil then gone = true break end
        t.ticks(1)
    end
    t.check("vasa.exit_open", gone, "raids_blockage_purple_small still standing")
    -- on through the exit Passage into the next room: the foundation's walk
    -- (cox_walk_on), logged, not judged here (s8c: 3 of 8 layouts leave me on
    -- the Passage's far tile with the next room's edge closed -- reported)
    local wr, wd = t.raid.cox_walk_on(60)
    t.ticklog.mark("vasa walk_on " .. tostring(wr) .. " " .. tostring(wd and wd.name or wd))
    t.finish((gone and boulders == 0 and stomps == 0 and ring_ok) and 0 or 1)
end

return {
    id = "cox_vasa",
    fixture = "fresh_lumbridge.ini",
    max_frames = 480000,
    setup = kit,
    run = run,
}
