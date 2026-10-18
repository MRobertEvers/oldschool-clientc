-- cox_vasa: Vasa Nistirio, solo, played by the v2 solver
-- (script/plugins/quest_driver/cox2_vasa.lua; plan
-- docs/minigames/cox/rooms/vasa_solver.md, mechanics vasa_spec.md) in the We
-- Do Raids minimum kit (QD.COX.KIT) plus what Vasa needs beyond the Olm's:
-- the dragon sword (stab, on the We Do Raids list: the crystal's stab defence
-- is -5 against +180 slash/crush), the toxic blowpipe for Vasa (light-ranged
-- defence +40), and the black d'hide body / Ava's assembler for the ranged
-- switch. Supplies: the raid's (+) potions only (overload, Xeric's aid,
-- revitalisation, prayer enhance).
--
-- GREEN (decided before the sweep): Vasa dead, the exit crystals gone and the
-- exit Passage taken into the next room, the player alive, NO special taken
-- (every one dodged out past the fire), NO boulder damage
-- (fully dodgeable: two tiles, Synq 0:58:03) and NO stomp damage (never under
-- him), counted off the server's tick log.
--
--   ./src/build_cox_vasa_opt/torirsserver --scriptrun test/raids/cox_vasa.lua --bots 1 \
--       --name vasa0 --ticks 3000 --session /tmp/v/vasa0 --fixture test/raids/fixtures/fresh_lumbridge.ini
-- The raid is the n-th holding the room, n = the run name's trailing number.

-- QD.COX.KIT (cox2_raid.lua), spelled out: the test is loaded before the
-- quest driver's chunk hands it `t`.
local WORN = {
    "fris_kingly_helm", "barbassault_penance_fighter_torso", "black_dragonhide_chaps", "hundred_gauntlets_level_10",
    "abyssal_tentacle", "dragon_parryingdagger", "enchanted_onyx_amulet", "tzhaar_cape_fire",
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
    "::setvar varp7348_barbarian_vial_smash 1",
}) do kit[#kit + 1] = line end
for _, obj in ipairs(WORN) do
    kit[#kit + 1] = "::give " .. obj .. " 1"
    kit[#kit + 1] = "::wield " .. obj
end
for _, line in ipairs({
    "::~charge abyssal_tentacle 10000",
    "::clearinv",
    "::blowpipe dragon_dart 2000 2000",
    "::give dragon_shortsword 1",
    "::give avas_assembler 1", "::give black_dragonhide_body 1",
    "::give raids_vial_overload_strong_4 2", "::give raids_vial_xericaid_strong_4 8",
    "::give raids_vial_revitalisation_strong_4 3", "::give raids_vial_prayer_strong_4 3",
}) do kit[#kit + 1] = line end

local VASA = { [7565] = true, [7566] = true, [7567] = true }

local function run(t)
    t.check("vasa.ticklog", t.ticklog.start() == "ok", "")
    -- (the live lane's user is the run name with "_p1" on it, scriptrun's
    -- may be either: the room number is the run name's own)
    local user = string.gsub(t.session._user() or "", "_p%d+$", "")
    local n = tonumber(string.match(user, "(%d+)$") or "0") or 0
    local er, at = t.raid.cox_test_enter("vasa", n)
    t.check("vasa.enter", er == "ok", tostring(er) .. " " .. tostring(at and at.seed))
    if er ~= "ok" then t.finish(1) return end
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
    -- THE SPECIAL, CHEAP: dodged out past the fire while I am high (the
    -- "Vasa skip"), else taken low -- no explosion over 35 (hp - 5 at the
    -- solver's DODGE_HP of 40). And what the room cost, in drinks.
    local dodged, tried = string.match(tostring(detail), "specials dodged (%d+)/(%d+)")
    local drinks = string.match(tostring(detail), "drinks (%d+)")
    t.check("vasa.special_cheap", special_max <= 35 and dodged ~= nil and dodged == tried,
        string.format("explosions taken %d (largest %d); specials dodged %s/%s; drinks %s", specials, special_max,
            tostring(dodged), tostring(tried), tostring(drinks)))
    -- (a fresh tick: the two passes above can spend most of one resume's
    -- Lua budget on a long fight -- d8l vasa7)
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
    t.finish((gone and boulders == 0 and stomps == 0 and special_max <= 35) and 0 or 1)
end

return {
    id = "cox_vasa",
    fixture = "fresh_lumbridge.ini",
    max_frames = 480000,
    setup = kit,
    run = run,
}
