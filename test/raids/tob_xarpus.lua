-- tob_xarpus: Xarpus, Normal trio, played by t.raid.xarpus_solve
-- (script/plugins/quest_driver/tob_xarpus.lua) in the wiki's Learner/Void kit,
-- every seat on the tentacle (docs/minigames/theater_of_blood/ROOM_SOLVERS.md
-- 2, 4.5, 4.5.1). The leader reports from its tick log: the heal orbs, the
-- spits, the damage each raider took, the room's length; and the Dawnbringer
-- must be in its pack.
--
--   ./src/build_rooms_opt/torirsserver --scriptrun test/raids/tob_xarpus.lua --bots 3 \
--       --name sa --session /tmp/xarp --fixture tests/raids/fixtures/fresh_lumbridge.ini
local role = (QD_PARTY and QD_PARTY.role) or 1

-- THE LEARNER/VOID KIT: the void melee set worn, the ranged and mage
-- switches and the supplies in the pack.
local WORN = {
    "game_pest_melee_helm", "elite_void_knight_top", "elite_void_knight_robes", "pest_void_knight_gloves",
    "abyssal_tentacle", "dragon_parryingdagger", "zenyte_amulet_enchanted", "tzhaar_cape_fire",
    "dragon_boots", "nzone_berzerker_ring",
}
local kit = { "::clearinv" }
for _, s in ipairs({ "attack", "strength", "defence", "hitpoints", "prayer", "magic", "ranged", "agility" }) do
    kit[#kit + 1] = "::setlevel " .. s .. " 99"
end
-- THE PRAYER UNLOCKS (Piety: King's Ransom and the Knight Waves; Rigour,
-- Augury and Preserve their scrolls; Deadeye and Mystic Vigour theirs)
for _, line in ipairs({
    "::setvar varb3888_kr_quest ^kr_complete",
    "::setvar varb3909_kr_knightwaves_state 8",
    "::setvar varb5451_prayer_rigour_unlocked 1",
    "::setvar varb5452_prayer_augury_unlocked 1",
    "::setvar varb5453_prayer_preserve_unlocked 1",
    "::setvar varb16097_prayer_deadeye_unlocked 1",
    "::setvar varb16098_prayer_mystic_vigour_unlocked 1",
    -- Desert Treasure done: the Ancient Magicks (the mage seat's barrage)
    "::setvar varb358_deserttreasure ^dt_complete",
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
}) do kit[#kit + 1] = line end
do
    -- the Learner/Void inventory's supplies (wiki Strategies example); a
    -- free slot for the Dawnbringer
    for _, line in ipairs({
        "::give br_4dosepotionofsaradomin 8", "::give br_4dose2restore 4", "::give br_4dose2combat 3",
        "::give anglerfish 2",
    }) do kit[#kit + 1] = line end
end

local function run(t)
    if role == 1 then t.check("xarp.ticklog", t.ticklog.start() == "ok", "") end
    local r, d = t.raid.enter("tob", "xarpus", { mode = "normal" })
    t.check("xarp.enter", r == "ok", "p" .. role .. " " .. tostring(d))
    t.expect("party.barrier.ready", t.party.barrier("ready", 300))
    t.cheat("::synctimers", false)
    if role == 1 then
        local sr, sd = t.cheat("::tobsyncroom")
        t.check("xarp.sync", sr == "ok", tostring(sd))
    end
    t.expect("party.barrier.synced", t.party.barrier("synced", 300))

    local result, detail, record = t.raid.xarpus_solve({ max_ticks = 900 })
    t.check("xarp.solve", result == "ok", tostring(detail))
    t.expect("party.barrier.solved", t.party.barrier("solved", 900))
    if role ~= 1 then
        t.expect("party.barrier.done", t.party.barrier("done", 900))
        t.finish(0)
        return
    end

    -- THE LEADER'S MEASURES, from the tick log (lesson 12)
    local function rows(kind)
        local _, out = t.ticklog.rows({ kind = kind })
        t.ticks(1)
        return out or {}
    end
    local ids = t.raid.xarpus_symbols()
    local start, stand, dead_at = nil, nil, nil
    for _, s in ipairs(rows("npc_spawn")) do
        if (s.type == ids.static or s.type == ids.feeding) and start == nil then start = s.tick end
    end
    for _, rt in ipairs(rows("npc_retype")) do
        if rt.to_type == ids.boss and stand == nil then stand = rt.tick end
        if rt.to_type == ids.dead and dead_at == nil then dead_at = rt.tick end
    end
    local orbs, spits, splats, healed = 0, 0, 0, 0
    for _, p in ipairs(rows("projectile")) do
        if p.spotanim == ids.orb then orbs = orbs + 1 end
        if p.spotanim == ids.spit then splats = splats + 1 end
    end
    for _, a in ipairs(rows("npc_anim")) do
        if a.seq == ids.spit_seq then spits = spits + 1 end
    end
    for _, h in ipairs(rows("npc_heal")) do
        -- the orbs' heals only: his death form heals to base
        if h.type ~= ids.dead then healed = healed + h.amount end
    end
    local taken = {}
    for _, h in ipairs(rows("hit_player")) do
        if h.damage > 0 then taken[h.pid] = (taken[h.pid] or 0) + h.damage end
    end
    local tk = {}
    for pid = 0, 3 do if taken[pid] then tk[#tk + 1] = "pid" .. pid .. " " .. taken[pid] end end
    local _, dawn = t.inv.count("verzik_special_weapon")
    dawn = dawn or 0
    local room = (start and dead_at) and (dead_at - start) or nil
    -- GREEN (the owner, 2026-10-09): the room passes with no raider dead, and
    -- the Dawnbringer leaves with the party. Heal orbs, damage and the room's
    -- length are reported.
    t.check("xarp.measure", dead_at ~= nil and dawn > 0, string.format(
        "room %s ticks (stand-up +%s); heal orbs %d (healed %d); spits %d, projectiles %d; taken %s; dawnbringer %d",
        tostring(room), tostring(stand and start and (stand - start)), orbs, healed, spits, splats,
        #tk > 0 and table.concat(tk, ", ") or "none", dawn))
    t.expect("party.barrier.done", t.party.barrier("done", 900))
    t.finish(0)
end

return {
    id = "tob_xarpus",
    fixture = "fresh_lumbridge.ini",
    party = 3,
    max_frames = 200000,
    setup = kit,
    run = run,
}
