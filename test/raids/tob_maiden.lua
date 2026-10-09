-- tob_maiden: the Normal Maiden trio, played by the from-scratch
-- solver t.raid.maiden_solve (tob_maiden.lua) in the wiki's
-- Learner/Void kit (docs/minigames/theater_of_blood/ROOM_SOLVERS.md section
-- 2). Every seat is called at the room's entry; the solver starts the room
-- (the leader) or crosses after it (a member). The leader reports the ground
-- truth from its tick log: the damage every raider took by source, the
-- room's length, the crabs and her heals.
--
--   ./src/build_rooms_opt/torirsserver --scriptrun test/raids/tob_maiden.lua --bots 3 \
--       --name sa --session /tmp/maiden --fixture tests/raids/fixtures/fresh_lumbridge.ini
local role = (QD_PARTY and QD_PARTY.role) or 1

-- THE LEARNER/VOID KIT (wiki_Theatre_of_Blood_Strategies.wikitext :93-126,
-- :294-330): the void melee set worn, the ranged and mage switches and the
-- supplies in the pack, one slot free for a two-handed switch.
local WORN = {
    "game_pest_melee_helm", "elite_void_knight_top", "elite_void_knight_robes", "pest_void_knight_gloves",
    "abyssal_tentacle", "dragon_parryingdagger", "zenyte_amulet_enchanted", "tzhaar_cape_fire",
    "dragon_boots", "nzone_berzerker_ring",
}
local kit = { "::clearinv" }
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
-- THE FREEZER (seat 1, the leader: ROOM_SOLVERS.md 4.1): Ancient Magicks and
-- the Ice Barrage runes, loose (no pouch-loading cheat); two supply slots
-- fewer so the defender and the boots can come off for the cast set.
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
    if role == 1 then t.check("maiden.ticklog", t.ticklog.start() == "ok", "") end
    local r, d = t.raid.enter("tob", "maiden", { mode = "normal" })
    t.check("maiden.enter", r == "ok", "p" .. role .. " " .. tostring(d))
    t.expect("party.barrier.ready", t.party.barrier("ready", 300))
    -- every seat's login-tick clocks restarted on one tick (lesson 35)
    t.cheat("::synctimers", false)

    local result, detail = t.raid.maiden_solve({ max_ticks = 600 })
    t.check("maiden.solve", result == "ok", tostring(detail))
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
    local ids = t.raid.maiden_symbols()
    local anims, retype, hitp, spawns, heals = rows("npc_anim"), rows("npc_retype"), rows("hit_player"), rows("npc_spawn"), rows("npc_heal")
    local first_attack, dead_at = nil, nil
    for _, a in ipairs(anims) do
        if (a.seq == ids.seq_storm or a.seq == ids.seq_blood) and first_attack == nil then first_attack = a.tick end
    end
    for _, rw in ipairs(retype) do
        if rw.to_type == ids.dying_a and dead_at == nil then dead_at = rw.tick end
    end
    -- her dying form too: a blackstorm thrown before the killing blow lands
    -- after it, logged under tob_maiden_dying_a (seeds m5s so, sj)
    local forms = { [ids.m100] = true, [ids.m70] = true, [ids.m50] = true, [ids.m30] = true, [ids.dying_a] = true }
    local taken, by_src, other = {}, {}, 0
    for _, h in ipairs(hitp) do
        if h.damage > 0 and (dead_at == nil or h.tick <= dead_at + 6) then
            taken[h.pid] = (taken[h.pid] or 0) + h.damage
            local src = forms[h.npc_type] and "storm" or tostring(h.npc_type) .. "/" .. tostring(h.hitsplat)
            by_src[src] = (by_src[src] or 0) + h.damage
            if not forms[h.npc_type] then other = other + h.damage end
        end
    end
    local crabs, slugs = 0, 0
    for _, s in ipairs(spawns) do
        if s.type == ids.crab then crabs = crabs + 1 end
        if s.type == ids.slug then slugs = slugs + 1 end
    end
    local healed, heal_src = 0, {}
    for _, h in ipairs(heals) do
        if forms[h.type] then
            healed = healed + (h.amount or 0)
            local k = tostring(h.label)
            heal_src[k] = (heal_src[k] or 0) + (h.amount or 0)
        end
    end
    local tk, src, hs = {}, {}, {}
    for pid = 0, 3 do if taken[pid] then tk[#tk + 1] = "pid" .. pid .. " " .. taken[pid] end end
    for k, v in pairs(by_src) do src[#src + 1] = k .. ":" .. v end
    for k, v in pairs(heal_src) do hs[#hs + 1] = k .. ":" .. v end
    table.sort(src)
    table.sort(hs)
    -- THE PROBES (ROOM_SOLVERS.md 4.1): (a) every storm on a raider, by
    -- amount (prayed <= 18 + 2c); (b) each pool's landing graphic tick, to
    -- set beside the solver's predicted landings (its summary's "pools");
    -- (e) the barrage's impacts on crabs: a freeze graphic or a splash.
    local storm_hits, pool_lands, freezes, splashes = {}, {}, 0, 0
    for _, h in ipairs(hitp) do
        if forms[h.npc_type] and h.damage >= 0 and #storm_hits < 40 then storm_hits[#storm_hits + 1] = "t" .. h.tick .. ":" .. h.damage end
    end
    local _, mapanims = t.ticklog.rows({ kind = "map_spotanim" })
    t.ticks(1)
    for _, m in ipairs(mapanims or {}) do
        if m.spotanim == ids.pool_gfx and #pool_lands < 60 then
            pool_lands[#pool_lands + 1] = "t" .. m.tick .. "@" .. ((m.coord >> 14) & 0x3FFF) .. "," .. (m.coord & 0x3FFF)
        end
    end
    local _, nspot = t.ticklog.rows({ kind = "npc_spotanim" })
    t.ticks(1)
    for _, n in ipairs(nspot or {}) do
        if n.type == ids.crab and n.spotanim == ids.freeze_gfx then freezes = freezes + 1 end
        if n.type == ids.crab and n.spotanim == ids.splash_gfx then splashes = splashes + 1 end
    end
    t.check("maiden.probe", true, string.format("storm hits %s; pool landings %s; barrage on crabs: %d frozen, %d splashed",
        table.concat(storm_hits, " "), table.concat(pool_lands, " "), freezes, splashes))
    local room = (first_attack and dead_at) and (dead_at - (first_attack - 9)) or nil
    t.check("maiden.measure", dead_at ~= nil and other == 0, string.format(
        "room %s ticks (first attack t%s, her death t%s); damage taken %s (by source %s); crabs %d, blood spawns %d; she healed %d (%s)",
        tostring(room), tostring(first_attack), tostring(dead_at), #tk > 0 and table.concat(tk, ", ") or "none",
        table.concat(src, " "), crabs, slugs, healed, table.concat(hs, " ")))
    t.expect("party.barrier.done", t.party.barrier("done", 900))
    t.finish(0)
end

return {
    id = "tob_maiden",
    fixture = "fresh_lumbridge.ini",
    party = 3,
    max_frames = 120000,
    setup = kit,
    run = run,
}
