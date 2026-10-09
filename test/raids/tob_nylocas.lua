-- tob_nylocas: the Nylocas, Normal trio, played by t.raid.nylocas_solve
-- (script/plugins/quest_driver/tob_nylocas.lua) in the wiki's Learner/Void kit,
-- every seat carrying the three sets (docs/minigames/theater_of_blood/
-- ROOM_SOLVERS.md 2, 4.3, 4.3.1). Seat 1 ranges, seat 2 melees, seat 3 mages
-- the waves; all three wear Vasilias' colour. The leader reports from its tick
-- log: the supports lost, the wrong-style hits (the shielded graphic), the
-- damage each raider took and from what, the room's length.
--
--   ./src/build_rooms_opt/torirsserver --scriptrun test/raids/tob_nylocas.lua --bots 3 \
--       --name sa --session /tmp/nylo --fixture tests/raids/fixtures/fresh_lumbridge.ini
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
    "::give br_4dose2restore 5", "::give br_4dose2combat 2", "::give br_4doserangerspotion 2",
    "::give br_4dosepotionofsaradomin 4", "::give anglerfish 4",
}) do kit[#kit + 1] = line end

local function run(t)
    if role == 1 then t.check("nylo.ticklog", t.ticklog.start() == "ok", "") end
    local r, d = t.raid.enter("tob", "nylocas", { mode = "normal" })
    t.check("nylo.enter", r == "ok", "p" .. role .. " " .. tostring(d))
    t.expect("party.barrier.ready", t.party.barrier("ready", 300))
    -- every seat's login-tick clocks restarted on one tick (lesson 35), and
    -- the room's by the leader alone (ROOM_SOLVERS.md 4.2.2)
    t.cheat("::synctimers", false)
    if role == 1 then
        local sr, sd = t.cheat("::tobsyncroom")
        t.check("nylo.sync", sr == "ok", tostring(sd))
    end
    t.expect("party.barrier.synced", t.party.barrier("synced", 300))

    local result, detail, record = t.raid.nylocas_solve({ max_ticks = 700 })
    t.check("nylo.solve", result == "ok", tostring(detail))
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
    local ids = t.raid.nylocas_symbols()
    local boss = { [ids.boss_spawning] = true, [ids.boss_melee] = true, [ids.boss_magic] = true,
                   [ids.boss_ranged] = true }
    local start, dead_at, supports, lost = nil, nil, 0, 0
    for _, s in ipairs(rows("npc_spawn")) do
        if s.type == ids.support then
            supports = supports + 1
            if start == nil then start = s.tick end
        end
    end
    for _, f in ipairs(rows("npc_free")) do
        if f.type == ids.support and (dead_at == nil or f.tick < dead_at) then lost = lost + 1 end
        if boss[f.type] and dead_at == nil then dead_at = f.tick end
    end
    -- the supports freed at the clear are the room's teardown, not a collapse
    if dead_at then
        lost = 0
        for _, f in ipairs(rows("npc_free")) do
            if f.type == ids.support and f.tick < dead_at then lost = lost + 1 end
        end
    end
    local shielded = 0
    for _, g in ipairs(rows("map_spotanim")) do
        if g.spotanim == ids.shielded then shielded = shielded + 1 end
    end
    local taken, from, worst = {}, {}, 0
    for _, h in ipairs(rows("hit_player")) do
        if h.damage > 0 then
            taken[h.pid] = (taken[h.pid] or 0) + h.damage
            local src = boss[h.npc_type] and "boss" or (ids.nylo[h.npc_type] and "nylo") or
                (h.npc_type == ids.support and "collapse" or tostring(h.npc_type))
            from[src] = (from[src] or 0) + h.damage
            worst = math.max(worst, taken[h.pid])
        end
    end
    local tk, fr = {}, {}
    for pid = 0, 3 do if taken[pid] then tk[#tk + 1] = "pid" .. pid .. " " .. taken[pid] end end
    for k, v in pairs(from) do fr[#fr + 1] = k .. " " .. v end
    table.sort(fr)
    local room = (start and dead_at) and (dead_at - start) or nil
    t.check("nylo.measure", dead_at ~= nil and lost == 0 and shielded == 0 and worst <= 120
        and room ~= nil and room <= 472, string.format(
        "room %s ticks (supports t%s, her death t%s); supports lost %d of %d; shielded %d; taken %s; by %s",
        tostring(room), tostring(start), tostring(dead_at), lost, supports, shielded,
        #tk > 0 and table.concat(tk, ", ") or "none", #fr > 0 and table.concat(fr, ", ") or "nothing"))
    t.expect("party.barrier.done", t.party.barrier("done", 900))
    t.finish(0)
end

return {
    id = "tob_nylocas",
    fixture = "fresh_lumbridge.ini",
    party = 3,
    max_frames = 160000,
    setup = kit,
    run = run,
}
