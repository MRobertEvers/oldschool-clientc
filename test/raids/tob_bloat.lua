-- tob_bloat: the Pestilent Bloat, Normal trio, played by t.raid.bloat_solve
-- (script/plugins/quest_driver/tob_bloat.lua) in the wiki's Learner/Void kit
-- (docs/minigames/theater_of_blood/ROOM_SOLVERS.md 2, 4.2, 4.2.1). Every seat
-- is called at the room's entry; it learns his lap, then the leader starts the
-- room and the members cross, each when a plan from the crossing tile is
-- unseen. The leader reports from its tick log: the damage he dealt each
-- raider (flies, hands, stomps: the pass is none), his downs, the room's length.
--
--   ./src/build_rooms_opt/torirsserver --scriptrun test/raids/tob_bloat.lua --bots 3 \
--       --name sa --session /tmp/bloat --fixture tests/raids/fixtures/fresh_lumbridge.ini
local role = (QD_PARTY and QD_PARTY.role) or 1

-- THE LEARNER/VOID KIT: the void melee set worn, the supplies in the pack.
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
    "::give br_4dose2restore 5", "::give br_4dose2combat 3",
    "::give br_4dosepotionofsaradomin 4", "::give anglerfish 10",
}) do kit[#kit + 1] = line end

local function run(t)
    if role == 1 then t.check("bloat.ticklog", t.ticklog.start() == "ok", "") end
    local r, d = t.raid.enter("tob", "bloat", { mode = "normal" })
    t.check("bloat.enter", r == "ok", "p" .. role .. " " .. tostring(d))
    t.expect("party.barrier.ready", t.party.barrier("ready", 300))
    -- every seat's login-tick clocks restarted on one tick (lesson 35)
    t.cheat("::synctimers", false)
    -- and the room's: Bloat respawned at his spawn tile with a fresh life, by
    -- the leader alone, once every seat is in -- so the fight starts from the
    -- same state however long the gathering took on this lane
    if role == 1 then
        local sr, sd = t.cheat("::tobsyncroom")
        t.check("bloat.sync", sr == "ok", tostring(sd))
    end
    t.expect("party.barrier.synced", t.party.barrier("synced", 300))

    local result, detail, record = t.raid.bloat_solve({ max_ticks = 600 })
    t.check("bloat.solve", result == "ok", tostring(detail))
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
    local ids = t.raid.bloat_symbols()
    local anims, hitp = rows("npc_anim"), rows("hit_player")
    local downs, dead_at = {}, nil
    for _, a in ipairs(anims) do
        if a.seq == ids.sleep then downs[#downs + 1] = "t" .. a.tick end
        if a.seq == ids.death and dead_at == nil then dead_at = a.tick end
    end
    local taken, total = {}, 0
    for _, h in ipairs(hitp) do
        if h.damage > 0 and h.npc_type == ids.boss then
            taken[h.pid] = (taken[h.pid] or 0) + h.damage
            total = total + h.damage
        end
    end
    local tk = {}
    for pid = 0, 3 do if taken[pid] then tk[#tk + 1] = "pid" .. pid .. " " .. taken[pid] end end
    local start = record and record.fight_start
    t.check("bloat.measure", dead_at ~= nil and total == 0, string.format(
        "room %s ticks (start t%s, his death t%s); %d downs (%s); his damage on the raiders %s",
        (start and dead_at) and tostring(dead_at - start) or "?", tostring(start), tostring(dead_at),
        #downs, table.concat(downs, " "), #tk > 0 and table.concat(tk, ", ") or "none"))
    t.expect("party.barrier.done", t.party.barrier("done", 900))
    t.finish(0)
end

return {
    id = "tob_bloat",
    fixture = "fresh_lumbridge.ini",
    party = 3,
    max_frames = 120000,
    setup = kit,
    run = run,
}
