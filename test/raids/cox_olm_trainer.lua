-- cox_olm_trainer: the Olm 4:1 trainer (::olmtrainer, OSRS-Content
-- minigame_cox/scripts/cox_olm_trainer.rs2), proved by the v2 solver
-- (script/plugins/quest_driver/cox2_olm.lua), whose melee hand is Synq's 4:1.
-- The trainer fells the mage hand early and keeps the melee hand up, so the
-- solver 4:1s the melee hand loop after loop, and the trainer judges it. The
-- solver keeps its own tally of the same fight (the specials and autos that
-- reached it): every special that fired at it in a judged loop breaks that
-- loop, so the trainer can never blame more loops on the special than the
-- solver saw specials, and the solver's 4:1 holds most loops.
--
-- GREEN: at least 20 loops judged, two thirds of them set, and the
-- special-broken loops no more than the specials the solver took.
--
--   ./src/build_olmtr_opt/torirsserver --scriptrun test/raids/cox_olm_trainer.lua --bots 1 \
--       --name olt1 --ticks 4000 --session /tmp/olmtr --fixture test/raids/fixtures/fresh_lumbridge.ini
--
-- The kit is cox_olm_solo.lua's.

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
    "::setvar varp8023_barbarian_vial_smash 1",
}) do kit[#kit + 1] = line end
for _, obj in ipairs(WORN) do
    kit[#kit + 1] = "::give " .. obj .. " 1"
    kit[#kit + 1] = "::wield " .. obj
end
for _, line in ipairs({
    "::~charge abyssal_tentacle 10000",
    "::give dragon_bolts_enchanted_ruby 300", "::wield dragon_bolts_enchanted_ruby",
    "::clearinv",
    "::blowpipe dragon_dart 2000 2000",
    "::give avas_assembler 1",
    "::give toxic_tots_charged 1", "::~charge toxic_tots_charged 2500",
    "::give occult_necklace 1", "::give ma2_saradomin_cape 1",
    "::give xbows_crossbow_dragon 1", "::give black_dragonhide_body 1",
    -- (the wiki minimum's mage switch: the fighter torso is -40 magic attack)
    "::give mystic_robe_top 1", "::give mystic_robe_bottom 1",
    "::give bgs 1",
    -- the supplies: the raid's own potions, (+) tier (cox_potion.rs2) --
    -- overload, Xeric's aid (the only healing: no food), revitalisation,
    -- prayer enhance -- and one stamina
    "::give raids_vial_overload_strong_4 2", "::give raids_vial_xericaid_strong_4 6",
    "::give raids_vial_revitalisation_strong_4 4", "::give raids_vial_prayer_strong_4 4",
    "::give 4dosestamina 1",
    -- a rune pouch of Water Strike (air, water, mind: enum_982 indices 1, 2, 5)
    -- to douse a fire-wall segment ("the lowest water spell in the rune
    -- pouch", Noodely's budget solo guide)
    "::give bh_rune_pouch 1",
    "::setvar varb29_rune_pouch_type_1 1", "::setvar varb1624_rune_pouch_quantity_1 500",
    "::setvar varb1622_rune_pouch_type_2 2", "::setvar varb1625_rune_pouch_quantity_2 500",
    "::setvar varb1623_rune_pouch_type_3 5", "::setvar varb1626_rune_pouch_quantity_3 500",
}) do kit[#kit + 1] = line end

local function run(t)
    -- (nokit: the solver plays in this test's own kit)
    t.cheat("::olmtrainer nokit", false)
    t.ticks(4)
    -- (the fight never ends under the trainer: the solve runs out its ticks)
    local result, detail = t.raid.cox_olm_solve({ max_ticks = 900 })
    local solve = tostring(result) .. " " .. tostring(detail)
    t.cheat("::olmtrainer stats", false)
    t.ticks(2)
    local judged, set, steps, broken = nil, nil, 0, {}
    local by_special = 0
    for _, line in ipairs(t.raid.cox_messages(200)) do
        local j, s = string.match(line, "Loops judged (%d+), set (%d+)")
        if j then judged, set = tonumber(j), tonumber(s) end
        if string.find(line, "[4:1] Basic 1:", 1, true) or string.find(line, "[4:1] Special (", 1, true) then
            steps = steps + 1
        end
        local bs = string.match(line, "Broken by: the special (%d+)")
        if bs then by_special = tonumber(bs) end
        if string.find(line, "Loop broken", 1, true) and #broken < 4 then broken[#broken + 1] = line end
    end
    t.check("olmtr.stats", judged ~= nil, "no stats line")
    t.check("olmtr.steps", steps > 0, "no step lines")
    judged, set = judged or 0, set or 0
    t.check("olmtr.loops", judged >= 20, string.format("%d loops judged", judged))
    t.check("olmtr.set", set * 3 >= judged * 2, string.format("%d of %d loops set; %s",
        set, judged, table.concat(broken, " | ")))
    local took = tonumber(string.match(solve, "specials at me (%d+)") or "")
    t.check("olmtr.special_tally", took ~= nil and by_special <= took,
        string.format("%d loops broken by the special, the solver took %s", by_special, tostring(took)))
    t.finish(0)
end

return {
    id = "cox_olm_trainer",
    fixture = "fresh_lumbridge.ini",
    max_frames = 400000,
    setup = kit,
    run = run,
}
