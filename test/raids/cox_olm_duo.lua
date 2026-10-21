-- cox_olm_duo: the Great Olm, a party of two (--bots 2), played by the v2
-- solver (script/plugins/quest_driver/cox2_olm.lua) on Synq's duo method:
-- "The EASIEST Mage Skipping Guide 2026 (Duo Olm)" (plan
-- docs/minigames/cox/olm_duo_solver.md; transcript
-- docs/minigames/cox/sources/transcripts/yt_8Ta9VkVaJ7M.md). Seat 1 is the
-- mage skipper ("the more experienced player"), seat 2 player 2. Each seat in
-- the solo test's kit (cox_olm_solo.lua: We Do Raids' minimum, the wiki's
-- head set, the BGS); no thralls -- the content has none.
--
-- GREEN: the head dead, both raiders alive, on every seat. Reported: the
-- specials skipped and fired in the mage-skip states, the autos at each seat,
-- the teleports.
--
--   ./src/build_cox_opt/torirsserver --scriptrun test/raids/cox_olm_duo.lua --bots 2 \
--       --name olmduo0 --ticks 6000 --session /tmp/olmduo --fixture test/raids/fixtures/fresh_lumbridge.ini

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
    "::give mystic_robe_top 1", "::give mystic_robe_bottom 1",
    -- (Synq: "the first thing you'll be doing when you enter the room is
    -- specking the hand")
    "::give bgs 1",
    "::give raids_vial_overload_strong_4 2", "::give raids_vial_xericaid_strong_4 6",
    "::give raids_vial_revitalisation_strong_4 4", "::give raids_vial_prayer_strong_4 4",
    "::give 4dosestamina 1",
    -- (a rune pouch of Water Strike: the douse; Synq's watering can is the
    -- same tool)
    "::give bh_rune_pouch 1",
    "::setvar varb29_rune_pouch_type_1 1", "::setvar varb1624_rune_pouch_quantity_1 500",
    "::setvar varb1622_rune_pouch_type_2 2", "::setvar varb1625_rune_pouch_quantity_2 500",
    "::setvar varb1623_rune_pouch_type_3 5", "::setvar varb1626_rune_pouch_quantity_3 500",
}) do kit[#kit + 1] = line end

local function run(t)
    local role, size = t.party.role(), t.party.size()
    local function barrier(name, wait)
        local r, d = t.party.barrier(name, wait or 600)
        t.check("olm.barrier." .. name, r == "ok", tostring(d))
        if r ~= "ok" then t.finish(1) end
        return r == "ok"
    end
    t.check("olm.party", size == 2, "raiders " .. tostring(size))
    -- (the tick log is the host's: a live party's member has no embedded server)
    if role == 1 then
        t.check("olm.ticklog", t.ticklog.start() == "ok", "")
        t.cheat("::coxolmrush", false)
        t.ticks(4)
    end
    if not barrier("built") then return end
    if role ~= 1 then
        local jr, jd = t.raid.cox_test_join()
        t.check("olm.join", jr == "ok", tostring(jd))
        if jr ~= "ok" then t.finish(1) return end
    end
    if not barrier("in") then return end
    -- both step through the barrier (the first one through wakes Olm)
    local hr, hd = t.player.click_loc("raids_olm_barrier", 1)
    t.check("olm.through", hr == "ok" or hr == "timeout", tostring(hr) .. " " .. tostring(hd))
    local cr, cd = t.chat.play({ "options", "choose:Step through the mystical barrier." })
    t.check("olm.confirm", cr == "ok", tostring(cd))
    local result, detail = t.raid.cox_olm_solve({ max_ticks = 3000, role = (role == 1) and "skip" or "p2", party = 2 })
    t.check("olm.solve", result == "ok", tostring(detail))
    t.check("olm.alive", result ~= "died", tostring(result))
    t.finish(result == "ok" and 0 or 1)
end

return {
    id = "cox_olm_duo",
    fixture = "fresh_lumbridge.ini",
    party = 2,
    max_frames = 400000,
    setup = kit,
    run = run,
}
