-- quest-driver / raid_solve_verzik_p1: VERZIK PHASE 1, SOLVED ON THE PLAN'S LOOP.
--
--   t.raid.verzik_p1_prepare([opts]) -> prep          (before the fight)
--   t.raid.verzik_p1_solve(opts)     -> result, detail, record
--
-- The loop of the "Verzik Solver" design note, as P2 and P3 run it: each tick
-- MEASURE turns rows into facts that carry ticks; BEHAVIORS turn facts into
-- cost terms on my tile at t+k and proposals on the other channels (a
-- behavior with a sequence keeps a small machine inside itself -- the
-- Dawnbringer's custody, the shield break -- but proposes, never walks); the
-- ARBITER takes the argmin over multi-tick plans for movement, the plan under
-- way kept unless a challenger beats it by a margin; EMIT sends the channels
-- in order (mouth, gear, special, interaction) and only what changed.  Every
-- click moves me on the NEXT tick, so a plan's tile_at[k] is my tile at the
-- end of tick d+k.  docs/minigames/theater_of_blood/solver_lessons.md is the
-- checklist.
--
-- THE SPEC (OSRS-Content .../minigame_tob: scripts/tob_verzik.rs2 "C:",
-- scripts/tob_damage.rs2, configs/tob.constant; skill_combat for energy):
--
--   shot        her wind-up (seq verzik_phase1_attack_magic) seen on tick k;
--               the bolt launches at k+3 and reads where raiders stood at the
--               end of k+2 (R), then every 14 (C: ^tob_verzik_p1_attack_ticks).
--               A raider in a STANDING pillar's cover box (C:
--               ~tob_verzik_hiding_pillar) costs that pillar a hit and takes
--               nothing; anyone else takes up to 137 (68 prayed).
--   pillar      185 hitpoints, 40-60 a hit, landing 3 ticks after the launch;
--               at 0 it collapses on everyone within 3 of its centre (foot+1),
--               reading the end of R+3: 32-65, a one-tile shove and a 5-tick
--               stun (C: ~tob_verzik_collapse_pillar, [queue,
--               tob_verzik_pillar_shove]).  A stunned raider misses the next
--               shot's box.
--   shield      1500 in a trio (C: ^tob_verzik_p1_hp_3).  Melee caps at 10 a
--               hitsplat (C: ~tob_verzik_p1_cap); the Dawnbringer is uncapped
--               and never misses, on every attack, from range 10 with line of
--               sight (all.obj weapon_attackrange); its special (35% energy)
--               does 75-150.  Energy returns 10% every 50 ticks.
--   break       her death animation on tick D: every pillar collapses,
--               reading the end of D+2; a shot she wound up still fires.
--
-- THE TEAM'S RULES (the owner, 2026-10-08):
--   * stay behind a pillar until the shot that could bring it down is in
--     flight, then leave its fall and attack;
--   * everyone strikes in every window, the Dawnbringer holder too.
-- ==========================================================================

QD.VZP1 = {
    CYCLE = 14,               -- C: ^tob_verzik_p1_attack_ticks
    FIRST_WINDUP = 18,        -- C: ^tob_verzik_p1_first_attack
    WINDUP_TO_READ = 2,       -- wind-up seen on k, the bolt reads the end of k+2
    HIT_TO_FALL_READ = 3,     -- the pillar's hit lands R+4; its fall reads R+3
    BREAK_TO_FALL_READ = 2,   -- death animation on D, every fall reads D+2
    PILLAR_HP = 185, PILLAR_HIT_MAX = 60,
    COLLAPSE_RANGE = 3,
    DAWN_RANGE = 10,          -- all.obj verzik_special_weapon weapon_attackrange
    SPEC_COST = 350,
    WEAPON_SLOT = 3,          -- wearpos 3
    H = 7,                    -- horizon: a run back from her side is up to 6 ticks
    WAITS = 3, MARGIN = 0.75, INF = 1e9,
    HP_EAT = 60,
    -- the take's worth against striking: a special (75-150, uncapped) is
    -- worth more than a whole horizon of capped swings (3 a tick unattacking)
    TAKE_WORTH = -25,
    OP_EAT = 1, OP_WIELD = 2, OP_DROP = 5,
    TRACE_CAP = 160,
}

-- The server tick, where the CLIENT ends it (a member reads the lockstep tick).
function QD.raid._vzp1_now()
    local r, tick = api_drive.server_tick()
    if r == "ok" then return tick end
    local session = api_drive.session()
    assert(type(session) == "table" and math.type(session.lockstep_tick) == "integer",
        "verzik_p1_solve: no server tick and no lockstep tick")
    return session.lockstep_tick
end

-- One server tick, ended where the client ends it: the `server_tick` event
-- (never the server's counter, which moves before the client has the tick).
function QD.raid._vzp1_wait()
    return await({ event = "server_tick", match = function() return true end,
        note = "verzik_p1_solve: the tick's packets applied" }, 3)
end

function QD.raid._vzp1_trace(S, d, text)
    if #S.trace < QD.VZP1.TRACE_CAP then S.trace[#S.trace + 1] = "t" .. d .. " " .. text end
end

function QD.raid._vzp1_component(name)
    local r, id = api_drive.component(name)
    assert(r == "ok", "verzik_p1_solve: no component " .. name)
    return id
end

function QD.raid._vzp1_ids(weapon)
    local function sym(kind, name)
        if kind == "npc" then name = QD.raid.tob_npc_name(name) end
        local r, id = api_drive.symbol(kind, name)
        assert(r == "ok", "verzik_p1_solve: no " .. kind .. " named " .. name)
        return id
    end
    return {
        p1 = sym("npc", "verzik_phase1"),
        initial = sym("npc", "verzik_initial"),
        transition = sym("npc", "verzik_phase1_to2_transition"),
        pillar = sym("npc", "verzik_pillar_npc"),
        windup = sym("seq", "verzik_phase1_attack_magic"),
        death = sym("seq", "verzik_phase1_death"),
        dawn = sym("obj", "verzik_special_weapon"),
        weapon = sym("obj", weapon or "scythe_of_vitur"),
        inv = sym("inv", "inv"), worn = sym("inv", "worn"),
        hitpoints = sym("stat", "hitpoints"),
        energy = sym("varp", "varp300_sa_energy"),
        food = sym("obj", "anglerfish"),
        backpack = QD.raid._vzp1_component("inventory:items"),
        spec_orb = QD.raid._vzp1_component("orbs:specbutton"),
    }
end

-- The weapon in my hand as the fight starts IS my weapon: a raider does not
-- need its name.  The symbol is only a fallback, and it can name another obj
-- than the one the kit gave (::give matches the display name).  Kept for P2
-- and P3, whose start can find the hand empty (the Dawnbringer leaves with
-- her P1 form).
function QD.raid._vzp1_weapon_from_hand(ids)
    local r, cell = api_drive.inv_slot(ids.worn, QD.VZP1.WEAPON_SLOT)
    if r == "ok" and cell and cell.obj_id and cell.obj_id > 0 and cell.obj_id ~= ids.dawn then
        ids.weapon = cell.obj_id
    end
    QD.raid.vz_weapon = ids.weapon
    return ids
end

-- ==================================================================== MEASURE

function QD.raid._vzp1_measure(S)
    local ids = S.ids
    local F = { pillar_rows = {}, ground_dawn = {} }
    F.tick = QD.raid._vzp1_now()
    local tr, tile = api_drive.player_tile()
    assert(tr == "ok", "verzik_p1_solve: no tile")
    F.me = { x = tile.x, z = tile.z }
    local nr, rows = api_drive.npcs(0)
    if nr ~= "ok" then rows = {} end
    for _, row in ipairs(rows) do
        local id, base = row.npc_id, row.base_npc_id
        -- the server tile on both lanes (live x/z is the drawn tile; see
        -- the P2 and P3 measures)
        if row.server_x ~= nil then row.x, row.z = row.server_x, row.server_z end
        if id == ids.p1 or base == ids.p1 then F.boss = row
        elseif id == ids.initial or base == ids.initial then F.initial = row
        elseif id == ids.transition or base == ids.transition then F.after = row
        elseif id == ids.pillar then F.pillar_rows[#F.pillar_rows + 1] = row
        end
    end
    local hr, hp = api_drive.skill(ids.hitpoints)
    F.hp = (hr == "ok" and hp.level) or 0
    local er, energy = api_drive.varp(ids.energy)
    F.energy = (er == "ok" and energy) or 0
    local _, pack = api_drive.inv_count(ids.inv, ids.dawn)
    local _, worn = api_drive.inv_count(ids.worn, ids.dawn)
    F.dawn_pack, F.dawn_worn = (pack or 0) > 0, (worn or 0) > 0
    local orr, objs = api_drive.objs(0)
    if orr == "ok" then
        for _, o in ipairs(objs) do
            if o.obj_id == ids.dawn then F.ground_dawn[#F.ground_dawn + 1] = { x = o.x, z = o.z } end
        end
    end
    return F
end

-- The pillars, keyed by their south-west tile, with the hits everyone saw
-- (a pillar's own hitsplats: one shared count, so the team agrees), in the
-- wiki's order: the near row first, west before east, then the second row.
function QD.raid._vzp1_pillars(S, F)
    local V = QD.VZP1
    local out = {}
    local b = F.boss or F.initial
    if b then S.mid_x = b.x + math.floor(((b.size or 1) - 1) / 2) end
    for _, row in ipairs(F.pillar_rows) do
        local key = row.x .. "," .. row.z
        if row.hit_cycle and row.hit_cycle > (S.hit_cycle_seen[key] or 0) and (row.hit_damage or -1) > 0 then
            S.hit_cycle_seen[key] = row.hit_cycle
            S.hits[key] = (S.hits[key] or 0) + 1
        end
        local hits = S.hits[key] or 0
        out[#out + 1] = {
            key = key, x = row.x, z = row.z, size = row.size or 3,
            west = S.mid_x ~= nil and row.x < S.mid_x,
            cx = row.x + 1, cz = row.z + 1,
            hits = hits,
            -- the next hit could bring it down: at most 60 may be left
            could_fall = V.PILLAR_HP - V.PILLAR_HIT_MAX * hits <= V.PILLAR_HIT_MAX,
        }
    end
    table.sort(out, function(a, c)
        if a.z ~= c.z then return a.z > c.z end
        return a.x < c.x
    end)
    return out
end

-- C: ~tob_verzik_hiding_pillar, the cover box off a pillar's south-west tile.
function QD.raid._vzp1_box(p)
    local t = {}
    if p.west then
        for dx = -3, 0 do for dz = -3, 0 do t[#t + 1] = { x = p.x + dx, z = p.z + dz } end end
        t[#t + 1] = { x = p.x + 1, z = p.z - 1 }
        t[#t + 1] = { x = p.x - 1, z = p.z + 1 }
    else
        for dx = 2, 4 do for dz = -2, 0 do t[#t + 1] = { x = p.x + dx, z = p.z + dz } end end
        t[#t + 1] = { x = p.x + 1, z = p.z - 1 }
        t[#t + 1] = { x = p.x + 3, z = p.z + 1 }
    end
    return t
end

-- Her clock: the shot's read tick, from her wind-up.  Before one is seen it
-- is a guess, and a guess only keeps raiders in (every tick counts as a read).
function QD.raid._vzp1_clock(S, F)
    local V, b = QD.VZP1, F.boss
    if b and b.seq_id == S.ids.windup and b.seq_tick ~= S.windup_seq_tick then
        S.windup_seq_tick = b.seq_tick
        S.read = F.tick + V.WINDUP_TO_READ
        S.synced = true
        S.windups[#S.windups + 1] = F.tick
    elseif S.read and F.tick > S.read and S.synced then
        S.read = S.read + V.CYCLE
    end
    F.read = S.read
    -- the guess (no wind-up seen yet) only keeps raiders in: once its tick has
    -- passed without one, every tick is a read until she winds up
    F.hold = not S.synced and S.read ~= nil and F.tick >= S.read
end

-- The shield break, a small machine: FIGHT -> BREAK (every pillar falls,
-- reading D+2) -> DONE.
function QD.raid._vzp1_break(S, F, pillars)
    local V = QD.VZP1
    if S.phase == "FIGHT" and ((F.boss and F.boss.seq_id == S.ids.death) or (F.boss == nil and F.after ~= nil)) then
        S.phase = "BREAK"
        S.break_read = F.tick + V.BREAK_TO_FALL_READ
        for _, p in ipairs(pillars) do
            S.falls[#S.falls + 1] = { cx = p.cx, cz = p.cz, read = S.break_read, key = p.key }
        end
        QD.raid._vzp1_trace(S, F.tick, "her shield broke: every pillar falls, read t" .. S.break_read)
    end
    if S.phase == "BREAK" and F.tick > S.break_read + 1 then S.phase = "DONE" end
end

-- ================================================================ BEHAVIORS

-- Whose turn the Dawnbringer is: seat 1 carries it in, and each time it lands
-- on the floor the next seat in orb order takes it (W:885).
function QD.raid._vzp1_dawn_turn(S)
    return (S.dawn_drops % S.size) + 1
end

-- ================================================================== ARBITER

-- ===================================================================== EMIT

-- ===================================================================== LOOP

-- Before the fight: the backpack open (a held op on a hidden panel is
-- refused) and the room's origin for the later phases (C: west pillars at
-- local x 25, the near row at local z 30).
function QD.raid.verzik_p1_prepare(opts)
    opts = opts or {}
    local S = { ids = QD.raid._vzp1_ids() }
    local tr, tab = api_drive.tab_by_name("inventory")
    assert(tr == "ok", "verzik_p1_prepare: no inventory tab")
    api_drive.tab(tab)
    local deadline = QD.raid._vzp1_now() + (opts.max_ticks or 30)
    while QD.raid._vzp1_now() <= deadline do
        local F = QD.raid._vzp1_measure(S)
        if (F.boss or F.initial) and #F.pillar_rows > 0 then
            local bx, bz = nil, nil
            for _, p in ipairs(F.pillar_rows) do
                bx = bx and math.min(bx, p.x) or p.x
                bz = bz and math.max(bz, p.z) or p.z
            end
            return { pillars = #F.pillar_rows, base = { x = bx - 25, z = bz - 30 } }
        end
        QD.raid._vzp1_wait()
    end
    return nil
end
