-- tob_nylocas_probe: the Normal Nylocas trio's CONTENT under observation, no
-- solver. Every seat wears the Learner/Void kit, turns on ::god (the damage
-- funnel's measurement switch), crosses after the leader starts the room and
-- stands mid-room; the tick log records every nylo's spawn, tile, type change
-- and death, the waves, the supports and Vasilias, for the calibration against
-- Blert (ROOM_SOLVERS.md 4.7; scratchpad nylo_cal.py). Nobody kills a nylo,
-- so the room cap stalls waves Blert's teams do not stall: the calibration
-- compares only what players do not drive (paths, swaps, flickers,
-- detonations, splits, the boss's clock), and wave timing against the cap.
local role = (QD_PARTY and QD_PARTY.role) or 1

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
}) do kit[#kit + 1] = line end
for _, obj in ipairs(WORN) do
    kit[#kit + 1] = "::give " .. obj .. " 1"
    kit[#kit + 1] = "::wield " .. obj
end
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
        "::give waterrune 600", "::give deathrune 400", "::give bloodrune 200",
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
    if role == 1 then t.check("nylo.ticklog", t.ticklog.start() == "ok", "") end
    local r, d = t.raid.enter("tob", "nylocas", { mode = "normal" })
    t.check("nylo.enter", r == "ok", "p" .. role .. " " .. tostring(d))
    t.expect("party.barrier.ready", t.party.barrier("ready", 300))
    t.cheat("::synctimers", false)
    if role == 1 then
        t.check("nylo.start", t.player.click_loc("tob_arena_barrier", 1) == "ok", "")
        t.chat.play({ "options", "choose:Yes, begin the fight." })
    end
    t.expect("party.barrier.started", t.party.barrier("started", 300))
    if role ~= 1 then t.player.click_loc("tob_arena_barrier", 1) end
    -- stand mid-room (local (31,24) of the room's square)
    t.raid.tob_probe_stand(30 + role, 24)
    t.ticks(520)
    t.expect("party.barrier.done", t.party.barrier("done", 900))
    t.finish(0)
end

return {
    id = "tob_nylocas_probe",
    fixture = "fresh_lumbridge.ini",
    party = 3,
    max_frames = 120000,
    setup = kit,
    run = run,
}
