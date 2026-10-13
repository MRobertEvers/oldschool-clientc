-- cox_olm_solo: the Great Olm, solo, played by the v2 solver
-- (script/plugins/quest_driver/cox2_olm.lua; plan docs/minigames/cox/olm_solver.md)
-- in the We Do Raids minimum CoX kit (wedoraids.com/raid-requirements: Barrows
-- gloves, salve (ei), imbued god cape, fire cape, dragon defender, dragon sword,
-- trident of the swamp, abyssal tentacle, toxic blowpipe, occult, berserker ring
-- (i), amulet of fury, bandos godsword; "strongly advised" Ava's assembler and
-- fighter torso). The slots the list leaves open, and the head phase, from the
-- wiki's "Minimum" CoX setup (Chambers_of_Xeric/Strategies): helm of
-- neitiznot, black d'hide chaps, dragon boots; a dragon crossbow with ruby
-- dragon bolts (e) and a black d'hide body for the head (5 ticks on rapid: the
-- head's 4:1, ThePerfectG / VideoGameBot). No void. Tentacle for the melee
-- hand, trident for the mage hand, the crossbow for the head; the dragon sword
-- and the salve stay out of an Olm inventory.
--
-- GREEN: the head dead, the player alive.
--
--   ./src/build_cox_opt/torirsserver --scriptrun test/raids/cox_olm_solo.lua --bots 1 \
--       --name ol1 --ticks 6000 --session /tmp/olm --fixture test/raids/fixtures/fresh_lumbridge.ini
--   python3 tools/raid_gate/run.py cox_olm_solo --name ol1 --no-publish      (the live lane)

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
    t.check("olm.ticklog", t.ticklog.start() == "ok", "")
    t.cheat("::coxolmrush", false)
    t.ticks(4)
    local hr, hd = t.player.click_loc("raids_olm_barrier", 1)
    t.check("olm.barrier", hr == "ok" or hr == "timeout", tostring(hr) .. " " .. tostring(hd))
    local cr, cd = t.chat.play({ "options", "choose:Step through the mystical barrier." })
    t.check("olm.confirm", cr == "ok", tostring(cd))
    local result, detail = t.raid.cox_olm_v2_solve({ max_ticks = 2400 })
    t.check("olm.solve", result == "ok", tostring(detail))
    if result ~= "ok" then
        t.finish(1)
        return
    end
    -- THE WAY OUT: the crystal blocking the exit shatters and is gone, the
    -- reward crystal beyond it is the Ancient chest
    -- (the exit tile is the chest's less (1, 2): chamber-local (16,45) and
    -- (17,47); other rooms' copies of the crystal stand within range)
    local cr0, chest = nil, nil
    for _ = 1, 4 do
        cr0, chest = t.world.loc_near("raids_reward_chest", 40)
        if chest ~= nil then break end
        t.ticks(1)
    end
    t.check("olm.chest", cr0 == "ok" and chest ~= nil, tostring(cr0))
    local gone = false
    local where = ""
    for _ = 1, 12 do
        local _, row = t.world.loc_near("raids_blockage_green", 40)
        if chest == nil or row == nil or row.tile_x ~= chest.tile_x - 1 or row.tile_z ~= chest.tile_z - 2 then
            gone = true
            break
        end
        where = row.tile_x .. "," .. row.tile_z
        t.ticks(1)
    end
    t.check("olm.exit_open", gone, "raids_blockage_green still at " .. where)
    -- THE LOOT: search the chest (one packet; the server walks me), the
    -- reward interface opens over what the roll put in it, Bank-all empties it
    -- and the interface closes
    local lr, at = t.raid.tob_loc_op("raids_reward_chest", 1)
    t.check("olm.chest_search", lr == "ok", tostring(lr))
    local or_ = t.ui.await_open("raids_rewards", 40)
    t.check("olm.rewards_open", or_ == "ok", tostring(or_) .. " at " .. tostring(at and (at.x .. "," .. at.z)))
    t.shot("ancient chest rewards")
    local _, bank_all = t.ui.widget("raids_rewards:bank")
    t.check("olm.bank_all", t.ui.invoke(bank_all, 6) == "ok", "")
    local cl = t.ui.await_close("raids_rewards", 10)
    t.check("olm.looted", cl == "ok", tostring(cl))
    t.finish(0)
end

return {
    id = "cox_olm_solo",
    fixture = "fresh_lumbridge.ini",
    max_frames = 400000,
    setup = kit,
    run = run,
}
