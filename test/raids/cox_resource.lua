-- cox_resource: a Chambers of Xeric resource room, solo, played by
-- script/plugins/quest_driver/cox2_supplies.lua (t.raid.cox_resource_solve;
-- plan docs/minigames/cox/rooms/supplies_solver.md).
--
-- The kit carries what a scavenger room and the bosses before would have
-- given (two buchu seeds, 8 endarkened juice, 3 stinkhorn, 1 cicely) plus a
-- fishing rod with cave worms and a butterfly net (scavenger drops).
--
-- GREEN: brewed 8 Xeric's aid (+), 3 revitalisation (+), 1 prayer enhance (+)
-- (the plan's solo Olm stock), one raw raid fish or bat caught (A: fishing, B:
-- bats), the player alive, within 1500 ticks.
--
--   ./src/build_cox_supplies_opt/torirsserver --scriptrun test/raids/cox_resource.lua --bots 1 \
--       --name rs0 --ticks 4000 --session /tmp/rs --fixture test/raids/fixtures/fresh_lumbridge.ini
--
-- n even: Resources A (fishing), odd: Resources B (bats); the layout is
-- ::coxtest <room> n/2. n/2 odd: the whole raid's kit and call, meeting the room
-- first (no seeds or secondaries; its potions when n/4 is even): GREEN then is
-- "ok" within 200 ticks, alive. n/2 even and n/4 odd: THE OLM KIT -- the raid
-- kit with no potions, a rake, the seeds (2 buchu, 2 golpar, 1 noxifer) and
-- the secondaries a scavenger room loots, the last floor's prep
-- (t.raid.cox_prep_solve: gear parked in private storage, brewed, gear back)
-- and the floor end's restock (t.raid.cox_olm_pack, here at this room's unit):
-- GREEN then is the Olm kit CARRIED (overload 2, aid 6, revit 4, enhance 4)
-- with every gear switch, within 1500 ticks, alive.

local function kit()
    local lines = { "::clearinv" }
    for _, s in ipairs({ "attack", "strength", "defence", "hitpoints", "prayer", "magic", "ranged", "agility",
            "farming", "herblore", "fishing", "hunter", "cooking", "construction" }) do
        lines[#lines + 1] = "::setlevel " .. s .. " 99"
    end
    for _, obj in ipairs({ "fris_kingly_helm", "barbassault_penance_fighter_torso", "black_dragonhide_chaps",
            "hundred_gauntlets_level_10", "abyssal_tentacle", "dragon_parryingdagger", "enchanted_onyx_amulet",
            "tzhaar_cape_fire", "dragon_boots", "nzone_berzerker_ring" }) do
        lines[#lines + 1] = "::give " .. obj .. " 1"
        lines[#lines + 1] = "::wield " .. obj
    end
    lines[#lines + 1] = "::~charge abyssal_tentacle 10000"
    lines[#lines + 1] = "::clearinv"
    for _, g in ipairs({ "raids_seed_buchuleaf 2", "raids_endarkened_juice 8", "raids_stinkhorn_mushroom 3",
            "raids_cicely 1", "fishing_rod 1", "raids_fishingbait 30", "hunting_butterfly_net 1" }) do
        lines[#lines + 1] = "::give " .. g
    end
    return lines
end


-- The whole raid's carried kit (test/raids/cox_raid_solo.lua kit()), given
-- before ::coxtest in the "raid kit" variants; its potions only when `potions`.
local function raid_kit(t, potions)
    for _, g in ipairs({ "::clearinv", "::blowpipe dragon_dart 2000 2000", "::give avas_assembler 1",
            "::give toxic_tots_charged 1", "::~charge toxic_tots_charged 2500", "::give occult_necklace 1",
            "::give ma2_saradomin_cape 1", "::give xbows_crossbow_dragon 1", "::give black_dragonhide_body 1",
            "::give mystic_robe_top 1", "::give mystic_robe_bottom 1", "::give dragon_shortsword 1",
            "::give dragon_pickaxe 1", "::give antidote++4 1", "::give nzone_salve_amulet_e 1", "::give bh_rune_pouch 1" }) do
        t.cheat(g, false)
    end
    if potions then
        for _, g in ipairs({ "raids_vial_overload_strong_4 2", "raids_vial_xericaid_strong_4 5",
                "raids_vial_revitalisation_strong_4 3", "raids_vial_prayer_strong_4 3" }) do
            t.cheat("::give " .. g, false)
        end
    end
    t.ticks(2)
end

local function run(t)
    t.raid.cox_trace(true)
    t.check("rs.ticklog", t.ticklog.start() == "ok", "")
    local n = t.raid.cox_run_n()
    local name = (n % 2 == 0) and "resource" or "resource_b"
    -- n // 2 odd: the whole raid's kit and call, as the raid meets this room
    -- FIRST (no seeds, no secondaries): it must hand back "ok" promptly
    local raid = (n // 2) % 2 == 1
    local potions = (n // 4) % 2 == 0
    local olm = not raid and not potions
    if raid or olm then raid_kit(t, potions and raid) end
    if olm then
        for _, g in ipairs({ "rake 1", "raids_seed_buchuleaf 2", "raids_seed_golpar 2", "raids_seed_noxifer 1",
                "raids_endarkened_juice 12", "raids_stinkhorn_mushroom 7", "raids_cicely 7" }) do
            t.cheat("::give " .. g, false)
        end
        t.ticks(1)
    end
    local er, ed = t.raid.cox_test_enter_long(name, n // 2, 80)
    t.check("rs.enter", er == "ok", name .. " " .. tostring(er) .. " " .. tostring(ed and ed.seed))
    if er ~= "ok" then t.finish(1) return end
    t.ticks(2)
    -- (no walk in: the solver finds its room from where ::coxtest left me,
    -- as the whole raid calls it)
    if raid then
        local result, detail = t.raid.cox_resource_solve({ max_ticks = 3000 })
        t.check("rs.mode", true, "raid kit, potions " .. tostring(potions))
        t.check("rs.solve", result == "ok", tostring(detail))
        local ticks = tonumber(string.match(tostring(detail), "ticks (%d+)") or "9999")
        t.check("rs.prompt", ticks <= 200, "ticks " .. tostring(ticks))
        t.check("rs.alive", t.player.alive(), "")
        t.finish((result == "ok" and ticks <= 200) and 0 or 1)
        return
    end
    if olm then
        local result, detail = t.raid.cox_prep_solve({ make = { aid = 10, revit = 5, enhance = 5, overload = 2 },
            final = true, max_ticks = 1500 })
        t.check("rs.mode", true, "olm kit")
        t.check("rs.prep", result == "ok", tostring(detail))
        local pr, pd = t.raid.cox_olm_pack({ leave = { "dragon_shortsword", "dragon_pickaxe", "nzone_salve_amulet_e", "antidote++4" } })
        t.check("rs.olm_pack", pr == "ok", tostring(pd))
        local want = { raids_vial_overload_strong_4 = 2, raids_vial_xericaid_strong_4 = 6,
            raids_vial_revitalisation_strong_4 = 4, raids_vial_prayer_strong_4 = 4,
            blowpipe = 0, toxic_tots_charged = 1, occult_necklace = 1, xbows_crossbow_dragon = 1, bh_rune_pouch = 1,
            mystic_robe_top = 1, black_dragonhide_body = 1, avas_assembler = 1 }
        local short = {}
        for nm, k in pairs(want) do
            local _, c = t.inv.count(nm)
            if (tonumber(c) or 0) < k then short[#short + 1] = nm .. "=" .. tostring(c) end
        end
        t.check("rs.kit_carried", #short == 0, table.concat(short, " "))
        local ticks = tonumber(string.match(tostring(detail), "parked %d+ dropped %d+ ticks (%d+)") or "9999")
        t.check("rs.in_time", ticks <= 1500, "ticks " .. tostring(ticks))
        t.check("rs.alive", t.player.alive(), "")
        t.finish((result == "ok" and #short == 0 and ticks <= 1500) and 0 or 1)
        return
    end
    local result, detail = t.raid.cox_resource_solve({ make = { aid = 8, revit = 3, enhance = 1 }, food = 1,
        max_ticks = 1500 })
    t.check("rs.solve", result == "ok", tostring(detail))
    t.check("rs.alive", t.player.alive(), "")
    local food = tonumber(string.match(tostring(detail), "food (%d+)") or "0")
    t.check("rs.food", food >= 1, "raw food caught " .. tostring(food))
    if food < 1 then t.finish(1) return end
    local made = tonumber(string.match(tostring(detail), "aid=(%d+)") or "0")
    t.check("rs.made", made >= 8 and string.find(tostring(detail), "left []", 1, true) ~= nil, tostring(detail))
    t.finish(result == "ok" and 0 or 1)
end

return {
    id = "cox_resource",
    fixture = "fresh_lumbridge.ini",
    max_frames = 400000,
    setup = kit(),
    run = run,
}
