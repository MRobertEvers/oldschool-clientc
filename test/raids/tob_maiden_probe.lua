-- tob_maiden_probe: the Normal Maiden trio's CONTENT under observation. Every
-- seat wears the Learner/Void kit, turns on ::god and melees her and nothing
-- else (t.raid.maiden_solve's ignore_crabs), so every crab of the 70/50/30
-- waves walks its whole path untouched to its leak; the tick log records each
-- crab's spawn, tile and leak and her heals, for the calibration against Blert
-- (ROOM_SOLVERS.md 4.7; scratchpad maiden_crab_cal.py). The frozen crabs'
-- side of the calibration comes from tob_maiden runs.
local role = (QD_PARTY and QD_PARTY.role) or 1

-- THE LEARNER/VOID KIT (wiki_Theatre_of_Blood_Strategies.wikitext :93-126,
-- :294-330): the void melee set worn, the ranged and mage switches and the
-- supplies in the pack, one slot free for a two-handed switch.
local WORN = {
    "game_pest_melee_helm", "elite_void_knight_top", "elite_void_knight_robes", "pest_void_knight_gloves",
    "abyssal_tentacle", "dragon_parryingdagger", "zenyte_amulet_enchanted", "tzhaar_cape_fire",
    "dragon_boots", "nzone_berzerker_ring",
}
local kit = { "::clearinv", "::god 1" }
for _, s in ipairs({ "attack", "strength", "defence", "hitpoints", "prayer", "magic", "ranged", "agility" }) do
    kit[#kit + 1] = "::setlevel " .. s .. " 99"
end
-- THE PRAYER UNLOCKS, as the game keeps them on the player: Piety and
-- Chivalry need King's Ransom and the Knight Waves training grounds
-- (varb3909, 8 = completed); Rigour, Augury and Preserve their scrolls
-- (varb5451-5453, raids_perm_transmit); Deadeye and Mystic Vigour theirs.
for _, line in ipairs({
    "::setvar varb3888_kr_quest ^kr_complete",
    "::setvar varb3909_kr_knightwaves_state 8",
    "::setvar varb5451_prayer_rigour_unlocked 1",
    "::setvar varb5452_prayer_augury_unlocked 1",
    "::setvar varb5453_prayer_preserve_unlocked 1",
    "::setvar varb16097_prayer_deadeye_unlocked 1",
    "::setvar varb16098_prayer_mystic_vigour_unlocked 1",
    -- Desert Treasure done: the Ancient Magicks (the client's spellbook and
    -- skill guide read varb358 >= 15)
    "::setvar varb358_deserttreasure ^dt_complete",
}) do kit[#kit + 1] = line end
for _, obj in ipairs(WORN) do
    kit[#kit + 1] = "::give " .. obj .. " 1"
    kit[#kit + 1] = "::wield " .. obj
end
-- THE FREEZER (seat 1, the leader: ROOM_SOLVERS.md 4.1): Ancient Magicks and
-- the Ice Barrage runes in a rune pouch; two supply slots fewer so the
-- defender and the boots can come off for the cast set.
if role == 1 then kit[#kit + 1] = "::setvar varb4070_spellbook 1" end
for _, line in ipairs({
    "::~charge abyssal_tentacle 10000",
    "::clearinv",
    -- three free slots for the load (test/raids/README.md "A loaded toxic
    -- blowpipe is one kit line")
    "::blowpipe dragon_dart 2000 2000",
    "::give game_pest_archer_helm 1", "::give zenyte_necklace_enchanted 1", "::give avas_assembler 1",
    "::give game_pest_mage_helm 1", "::give toxic_tots_charged 1", "::give occult_necklace 1",
    "::give ma2_saradomin_cape 1",
    "::~charge toxic_tots_charged 2500",
    "::give dragon_warhammer 1", "::give nzone_salve_amulet_e 1",
}) do kit[#kit + 1] = line end
if role == 1 then
    for _, line in ipairs({
        -- the barrage's runes in a rune pouch, as a raider carries them: the
        -- pouch's three slots are vars (a rune index into
        -- enum982_rune_pouch_rune and a count), read by the client's
        -- magic_runecount.cs2 and the server's ~rune_pouch_total alike
        "::give bh_rune_pouch 1",
        "::setvar varb29_rune_pouch_type_1 2", "::setvar varb1624_rune_pouch_quantity_1 600",   -- water
        "::setvar varb1622_rune_pouch_type_2 7", "::setvar varb1625_rune_pouch_quantity_2 400", -- death
        "::setvar varb1623_rune_pouch_type_3 8", "::setvar varb1626_rune_pouch_quantity_3 200", -- blood
        "::give br_4dose2restore 4", "::give br_4doserangerspotion 1",
        "::give br_4dosepotionofsaradomin 3", "::give anglerfish 3",
    }) do kit[#kit + 1] = line end
else
    for _, line in ipairs({
        "::give br_4dose2restore 5", "::give br_4dose2combat 3", "::give br_4doserangerspotion 1",
        "::give br_4dosepotionofsaradomin 4", "::give anglerfish 4",
    }) do kit[#kit + 1] = line end
end

local function run(t)
    if role == 1 then t.check("maiden.ticklog", t.ticklog.start() == "ok", "") end
    local r, d = t.raid.enter("tob", "maiden", { mode = "normal" })
    t.check("maiden.enter", r == "ok", "p" .. role .. " " .. tostring(d))
    t.expect("party.barrier.ready", t.party.barrier("ready", 300))
    t.cheat("::synctimers", false)
    local result, detail = t.raid.maiden_solve({ max_ticks = 2400, ignore_crabs = true })
    t.check("maiden.probe_solve", result == "ok", tostring(detail))
    t.expect("party.barrier.done", t.party.barrier("done", 900))
    t.finish(0)
end

return {
    id = "tob_maiden_probe",
    fixture = "fresh_lumbridge.ini",
    party = 3,
    max_frames = 240000,
    setup = kit,
    run = run,
}
