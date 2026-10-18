-- cox_scavengers: a Chambers of Xeric scavenger room, solo, played by
-- script/plugins/quest_driver/cox2_supplies.lua (t.raid.cox_scavengers_solve;
-- plan docs/minigames/cox/rooms/supplies_solver.md).
--
-- GREEN: the plan's secondaries carried -- endarkened juice >= 8, stinkhorn
-- mushroom >= 3, cicely >= 1, mallignum root planks >= 2 -- the player alive,
-- within 1200 ticks, no scavenger standing when it hands back; in the large room the shortcut cleared (the kit carries a
-- pickaxe and an axe; the boulder needs Strength only).
--
--   ./src/build_cox_supplies_opt/torirsserver --scriptrun test/raids/cox_scavengers.lua --bots 1 \
--       --name sc0 --ticks 3000 --session /tmp/sc --fixture test/raids/fixtures/fresh_lumbridge.ini
--
-- The run name's number n picks the room (even: small, odd: large), the layout
-- (::coxtest <room> n/2) and the kit: n/2 odd is the whole raid's kit and call
-- (with its given potions when n/4 is even, none when odd; GREEN then: "ok",
-- alive, the room clear -- the needs are the plan minus what is carried, and
-- what does not fit is left).

local function kit()
    local lines = { "::clearinv" }
    for _, s in ipairs({ "attack", "strength", "defence", "hitpoints", "prayer", "magic", "ranged", "agility",
            "mining", "woodcutting" }) do
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
    -- the shortcut's tools, and two brews for the scavengers' 13s
    for _, g in ipairs({ "rune_pickaxe 1", "rune_axe 1", "raids_vial_xericaid_strong_4 2" }) do
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
            "::give mystic_robe_top 1", "::give mystic_robe_bottom 1", "::give 4dosestamina 1", "::give bh_rune_pouch 1" }) do
        t.cheat(g, false)
    end
    if potions then
        for _, g in ipairs({ "raids_vial_overload_strong_4 3", "raids_vial_xericaid_strong_4 6",
                "raids_vial_revitalisation_strong_4 4", "raids_vial_prayer_strong_4 3" }) do
            t.cheat("::give " .. g, false)
        end
    end
    t.ticks(2)
end

local WANT = { raids_endarkened_juice = 8, raids_stinkhorn_mushroom = 3, raids_cicely = 1, raids_plank = 2 }

local function run(t)
    t.check("sc.ticklog", t.ticklog.start() == "ok", "")
    local n = t.raid.cox_run_n()
    local name = (n % 2 == 0) and "scavenger_small" or "scavenger_large"
    -- n // 2 odd: the whole raid's kit and call ({ max_ticks } only); its
    -- potions when n // 4 is even, none when odd (prep from zero)
    local raid = (n // 2) % 2 == 1
    local potions = (n // 4) % 2 == 0
    if raid then raid_kit(t, potions) end
    local er, ed = t.raid.cox_test_enter_long(name, n // 2, 80)
    t.check("sc.enter", er == "ok", name .. " " .. tostring(er) .. " " .. tostring(ed and ed.seed))
    if er ~= "ok" then t.finish(1) return end
    t.ticks(2)
    -- (no walk in: the solver finds its room from where ::coxtest left me,
    -- as the whole raid calls it)
    local opts = { want = WANT, max_ticks = 1200 }
    if raid then opts = { max_ticks = 3000 } end
    local result, detail = t.raid.cox_scavengers_solve(opts)
    t.check("sc.mode", true, (raid and ("raid kit, potions " .. tostring(potions)) or "light kit"))
    t.check("sc.solve", result == "ok", tostring(detail))
    t.check("sc.alive", t.player.alive(), "")
    -- the room left clear: no beast standing when the solver hands back
    -- (a row whose bar reads 0 is the last kill's corpse, still falling)
    local left = t.raid.cox_supplies_npcs()
    local standing = 0
    for ratio in string.gmatch(left, "scavenger_beast_[ab]@[%d,]+%(%d+%) (%-?%d+)/") do
        if tonumber(ratio) ~= 0 then standing = standing + 1 end
    end
    t.check("sc.room_clear", standing == 0, left)
    if name == "scavenger_large" and not raid then
        t.check("sc.shortcut", string.find(tostring(detail), "shortcut ok", 1, true) ~= nil, tostring(detail))
    end
    t.finish(result == "ok" and 0 or 1)
end

return {
    id = "cox_scavengers",
    fixture = "fresh_lumbridge.ini",
    max_frames = 400000,
    setup = kit(),
    run = run,
}
