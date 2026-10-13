-- quest-driver / cox2_olm: THE GREAT OLM, SOLO, on the v2 layers.
--
-- docs/minigames/cox/olm_solver.md is the plan; docs/minigames/cox/OLM_SPEC.md
-- the mechanics (every number below cites it); docs/CONTENT_SOLVER_MANUAL.md
-- the method. A QD.T2 room (tob2_run.lua): PREDICT and STRATEGY are here,
-- the planner (api_drive.aplan), the executive, the monitor and the supplies
-- are the shared layers.
--
-- THE IDEA. Olm acts every 4 ticks and only at whoever stands in the view his
-- head faces; where I stand at the end of the tick before each action decides
-- whether it fires, skips, or turns his head -- and so what the NEXT action
-- does. The strategy runs his own rule (OLM_SPEC 3) over every way of
-- standing in the next four read ticks (five view bins, 5^4 sequences),
-- scores each in hp (an auto at me, a special fired, a swing landed), and
-- hands the planner the winning bin for each read tick. 4:1 on the melee hand
-- and 3:0 on the mage hand are what that search finds; nothing here scripts
-- them.
--
-- Perception is only what the client is sent: the head's and claws' loc
-- animations (api_drive.loc_anims), npc rows, projectiles, graphics, locs and
-- chat. The rotation is COUNTED from the rise, as a player counts it.
-- ==========================================================================

QD.T2 = QD.T2 or {}
QD.T2.rooms = QD.T2.rooms or {}
QD.COXO = QD.COXO or {}
local CO = QD.COXO

CO.V = {
    H = 14, BEAM = 48,
    -- the chamber (chamber-local; OLM_SPEC 2)
    CH = { x0 = 11, z0 = 26, x1 = 21, z1 = 44 },
    -- the fire wall's ten tiles a row (cox_olm.constant wall_x0..x1: the
    -- whole walkable width) and the bolts' two ends (bolt_lower_z / upper_z:
    -- a bolt walks 17 tiles, 27 -> 43 north or 44 -> 28 south)
    WALL_X0 = 12, WALL_X1 = 21, BOLT_LO = 27, BOLT_HI = 44, BOLT_TILES = 17,
    SEP_Z = 36, MID_Z0 = 31, MID_Z1 = 41,
    HEAD_LOC_Z = 34, WEST_LOC_X = 4, EAST_LOC_X = 22,
    UPPER_Z = 39, LOWER_Z = 29,
    -- the five view bins by z: { z0, z1, s, m, n } (player counts per bin)
    BINS = {
        { 26, 30, 1, 0, 0 },     -- A  SOUTH only
        { 31, 35, 1, 1, 0 },     -- B  SOUTH + MIDDLE
        { 36, 36, 0, 1, 0 },     -- C  all three views; the tie-break counts MIDDLE
        { 37, 41, 0, 1, 1 },     -- D  MIDDLE + NORTH
        { 42, 44, 0, 0, 1 },     -- E  NORTH only
    },
    -- the clock (OLM_SPEC 3)
    FIRST_P1 = 4, FIRST_LATER = 8, ACT = 4,
    -- THE CURRENCY (hp): an auto that reaches me (prayed 80% off, plus the
    -- 1/3 chance it is a phase attack), a special that fires, a swing landed
    -- (expected damage x HP_PER_BOSS_HP; PROBE: measure from the tick logs)
    -- (an auto is a third of the time the phase's power -- a fire wall, an
    -- acid drip, falling crystals: ol1p t812 took a wall for a cast; prayed,
    -- the rest is ~3)
    AUTO_COST = 9,
    SPECIAL_COST = { [2] = 10, [3] = 10, [4] = 14, [5] = 6 },
    SWING_VALUE = 2.5,
    REQ_COST = 12,          -- a read tick off the chosen bin (above a cast: ol1p t808 stayed to cast)
    MOVE_BIN = 8,           -- tiles a run covers between two reads (4 ticks x 2)
    -- P3: both hands down to this, then the kills; above a swing's max so a
    -- low hand is not killed by accident (ol1t: the mage hand at 60 outlived
    -- the 50-tick regen)
    LOW_HP = 40,
    REGEN = 50,             -- P3: the first hand down rises again this many ticks later
    CYCLE_PULL = 1.5,       -- 4:1: hp a tile off the guide's tile at a read
    CANON_VALUE = 8,        -- recovery: hp for a plan that ends back in the 4:1 loop
    -- the interim: where the next phase starts (the guides' "P2/P3 mage start",
    -- chamber-local), by the wall Olm will rise on
    START = { [0] = { 14, 40 }, [1] = { 19, 32 } },
    -- (no food in the kit: Xeric's aid (+) is the healing, ~19 a dose)
    EAT_BELOW = 55, BREW_BELOW = 65,
    RANGE = { melee = 1, magic = 7, ranged = 7 },
    SPEED = { melee = 4, magic = 4, ranged = 5 },
}

-- ======================================================================== IDS

function CO.ids(opts)
    local ids = QD.raid._tob_common_ids("cox_olm", opts)
    local room = QD.raid._tob_symbols("cox_olm", {
        { "head", "npc", "olm_head" }, { "head_rising", "npc", "olm_head_spawning" },
        { "left", "npc", "olm_hand_left" }, { "right", "npc", "olm_hand_right" },
        { "firewall", "npc", "olm_firewall_npc" },
        { "loc_head", "loc", "olm_head" }, { "loc_head_rising", "loc", "olm_head_spawning" },
        { "loc_head_cave", "loc", "olm_head_cave" },
        { "loc_left", "loc", "olm_hand_left" }, { "loc_right", "loc", "olm_hand_right" },
        { "pool", "loc", "olm_acid_pool" }, { "seedling", "loc", "olm_crystal_attack_small" },
        { "burst", "loc", "olm_crystal_attack_large" },
        { "bomb", "loc", "olm_crystal_bomb" },
        { "proj_magic", "spotanim", "olm_firebreath_travel" }, { "proj_ranged", "spotanim", "olm_generic_range_proj" },
        { "sph_melee", "spotanim", "olm_weak_melee_proj" }, { "sph_ranged", "spotanim", "olm_weak_range_proj" },
        { "sph_magic", "spotanim", "olm_weak_mage_proj" },
        { "fw_travel", "spotanim", "olm_firewall_travel" },
        { "bolt", "spotanim", "olm_shockwave" }, { "acid_spit", "spotanim", "olm_acid_spit" }, { "portal", "spotanim", "olm_playerswap_0" },
        { "siphon_spit", "spotanim", "olm_healme_spit" },
        { "shadow", "spotanim", "gargboss_debris_shadow_90" },
        -- the kit: We Do Raids' minimum (test/raids/cox_olm_solo.lua)
        { "pipe", "obj", "toxic_blowpipe_loaded" }, { "assembler", "obj", "avas_assembler" },
        { "trident", "obj", "toxic_tots_charged" },
        { "occult", "obj", "occult_necklace" }, { "imbued_cape", "obj", "ma2_saradomin_cape" },
        { "defender", "obj", "dragon_parryingdagger" }, { "tentacle", "obj", "abyssal_tentacle" },
        { "fury", "obj", "enchanted_onyx_amulet" }, { "fire_cape", "obj", "tzhaar_cape_fire" },
        { "xbow", "obj", "xbows_crossbow_dragon" }, { "dhide_body", "obj", "black_dragonhide_body" },
        { "torso", "obj", "barbassault_penance_fighter_torso" }, { "vial_empty", "obj", "raids_vial_empty" },
        { "chaps", "obj", "black_dragonhide_chaps" },
        { "mystic_top", "obj", "mystic_robe_top" }, { "mystic_bottom", "obj", "mystic_robe_bottom" },
        { "style_slot_1", "component", "combat_interface:style_slot_1" },
        { "com_mode", "varp", "varp43_com_mode" },
        { "water_strike", "component", "magic_spellbook:water_strike" },
        { "playerscore", "varp", "varp4609_raids_playerscore" },
    })
    for k, v in pairs(room) do ids[k] = v end
    local tr, combat_tab = api_drive.tab_by_name("combat")
    assert(tr == "ok", "cox_olm: no combat tab")
    ids.combat_tab = combat_tab
    ids.sets = {
        -- (the head: the dragon crossbow, one-handed, on rapid -- 5 ticks)
        -- (the mage set's robes: the fighter torso is -40 magic attack, the
        -- chaps -10 -- ol1t splashed 53% of its casts in them)
        ranged = { ids.xbow, ids.fury, ids.assembler, ids.dhide_body, ids.chaps, ids.defender },
        melee = { ids.tentacle, ids.fury, ids.fire_cape, ids.defender, ids.torso, ids.chaps },
        magic = { ids.trident, ids.occult, ids.imbued_cape, ids.defender, ids.mystic_top, ids.mystic_bottom },
    }
    ids.boost = { ranged = "rigour", melee = "piety", magic = "augury" }
    -- THE RAID'S POTIONS, (+) tier, one dose to four (the supplies drink the
    -- first listed held: the lowest dose first): Xeric's aid is the brew,
    -- revitalisation the restore (stats and prayer); the room drinks the
    -- overload, prayer enhance and stamina itself (CO.drinks); no combat or
    -- ranging potion (the overload is both)
    local function vials(stem)
        local out = {}
        for n = 1, 4 do
            local r, id = api_drive.symbol("obj", stem .. n)
            assert(r == "ok", "cox_olm: no obj " .. stem .. n)
            out[#out + 1] = id
        end
        return out
    end
    ids.brews = vials("raids_vial_xericaid_strong_")
    ids.restores = vials("raids_vial_revitalisation_strong_")
    ids.stat_restores = ids.restores
    ids.overloads = vials("raids_vial_overload_strong_")
    ids.enhances = vials("raids_vial_prayer_strong_")
    ids.staminas = {}
    for n = 1, 4 do
        local r, id = api_drive.symbol("obj", n .. "dosestamina")
        assert(r == "ok", "cox_olm: no obj " .. n .. "dosestamina")
        ids.staminas[n] = id
    end
    ids.combats, ids.rangings = {}, {}
    -- every head and claw animation, by what it tells (OLM_SPEC 1)
    local function seqs(list)
        local out = {}
        for _, e in ipairs(list) do
            for i = 2, #e do
                local r, id = api_drive.symbol("seq", e[i])
                assert(r == "ok", "cox_olm: no seq named " .. e[i])
                out[id] = e[1]
            end
        end
        return out
    end
    -- head: "idle:<side>", "turn:<side>", "attack:<side>", "rise", "fall"
    -- with <side> the head's own: 0 front, 1 left, 2 right
    ids.head_seq = seqs({
        { "rise", "olm_head_spawn" }, { "rise_glow", "olm_head_spawn_enraged" },
        { "idle:0", "olm_head_idle_front", "olm_head_idle_front_enraged" },
        { "idle:1", "olm_head_idle_left", "olm_head_idle_left_enraged" },
        { "idle:2", "olm_head_idle_right", "olm_head_idle_right_enraged" },
        { "turn:0", "olm_head_turn_left_reverse", "olm_head_turn_right_reverse",
                    "olm_head_rotate_left_reversed_enraged", "olm_head_rotate_right_reversed_enraged" },
        { "turn:1", "olm_head_turn_left", "olm_head_turn_right_left",
                    "olm_head_rotate_left_enraged", "olm_head_rotate_right_left_enraged" },
        { "turn:2", "olm_head_turn_right", "olm_head_turn_left_right",
                    "olm_head_rotate_right_enraged", "olm_head_rotate_left_right_enraged" },
        { "attack:0", "olm_head_attack_acid_front", "olm_head_attack_front_enraged" },
        { "attack:1", "olm_head_attack_acid_left", "olm_head_attack_left_enraged" },
        { "attack:2", "olm_head_attack_acid_right", "olm_head_attack_right_enraged" },
        { "fall", "olm_head_death_front" },
    })
    ids.claw_seq = seqs({
        { "sign:2", "olm_hand_left_cast_stun", "olm_hand_left_gripping_attack_stun" },
        { "sign:3", "olm_hand_left_cast_earthshock", "olm_hand_left_gripping_attack_earthshock" },
        { "sign:4", "olm_hand_left_cast_earthquake", "olm_hand_left_gripping_attack_earthquake" },
        { "sign:5", "olm_hand_left_cast_heal" },
        { "clench", "olm_hand_left_stunned" },
        { "unclench", "olm_hand_left_unstunned" },
        { "left_down", "olm_hand_left_death" }, { "right_down", "olm_hand_right_death" },
        { "left_up", "olm_hand_left_spawn" }, { "right_up", "olm_hand_right_spawn" },
    })
    return ids
end

-- ================================================================ THE MODEL
--
-- The content's own rule (OLM_SPEC 3), pure: the views, the turn target, a
-- step. Game knowledge, as a player knows it; the model is checked against
-- every action the client shows (monitor rule olm_step).

function CO.in_view(dir, lz)
    local V = CO.V
    if lz < V.CH.z0 or lz > V.CH.z1 then return false end
    if dir == 0 then return lz <= V.SEP_Z end
    if dir == 2 then return lz >= V.SEP_Z end
    return lz >= V.MID_Z0 and lz <= V.MID_Z1
end

-- s / m / n counts -> the view the head turns to (side 0 west, 1 east).
function CO.most_of(s, m, n, side, extra_left, extra_right)
    if side == 0 then
        if extra_left then n = n + 1 end
        if extra_right then s = s + 1 end
        if m >= s and m >= n then return 1 end
        if n >= s then return 2 end
        return 0
    end
    if extra_left then s = s + 1 end
    if extra_right then n = n + 1 end
    if m >= s and m >= n then return 1 end
    if s >= n then return 0 end
    return 2
end

function CO.step_kind(c)
    local slot = c % 4
    if slot == 0 or slot == 2 then return 0 end
    if slot == 1 then return 1 end
    return 2 + c // 4
end

-- One step: state { dir, skipped }, the step's kind, my bin (an index into
-- CO.V.BINS; nil = I am not in the chamber), the hand flags, whether the
-- left claw can special. -> new dir, new skipped, autos at me, special fired
function CO.step(dir, skipped, kind, bin, side, extra_left, extra_right, can_special)
    local b = bin and CO.V.BINS[bin]
    local lz = b and b[1]
    local inview = b ~= nil and (CO.in_view(dir, b[1]) or CO.in_view(dir, b[2]))
    -- (a bin is wholly in or out of every view by construction)
    if not inview or (kind > 0 and not skipped) then
        if kind == 0 then skipped = true end
        local most = CO.most_of(b and b[3] or 0, b and b[4] or 0, b and b[5] or 0, side, extra_left, extra_right)
        if most ~= dir then
            local autos, fired = 0, false
            if kind >= 2 and inview then
                if skipped then autos = 1 end
                skipped = false
                fired = can_special
            end
            return most, skipped, autos, fired
        end
        if kind == 1 then return dir, skipped, 0, false end
    end
    local debt = skipped
    skipped = false
    if kind == 0 then return dir, skipped, inview and 1 or 0, false end
    local autos = (debt and inview) and 1 or 0
    if kind == 1 then return dir, skipped, autos, false end
    return dir, skipped, autos, can_special
    -- (lz kept for the trace)
end

-- ================================================================= OBSERVE

function CO.local_of(S, x, z)
    local o = S.o
    return x - o.ox, z - o.oz
end

function CO.abs_of(S, lx, lz)
    local o = S.o
    return o.ox + lx, o.oz + lz
end

-- The chamber's origin and the wall in use, from the head-tile locs (the
-- active wall's head loc is the risen/rising one; the idle wall shows a cave).
function CO.find_body(S, F)
    local o, ids = S.o, S.ids
    local lr, rows = api_drive.locs(40)
    if lr ~= "ok" then return end
    local xs, z, active = {}, nil, nil
    o.pools, o.seedlings, o.bombs, o.bursts = {}, {}, {}, {}
    for _, row in ipairs(rows) do
        local id = row.loc_id
        if id == ids.loc_head or id == ids.loc_head_rising or id == ids.loc_head_cave then
            xs[#xs + 1] = row.x
            z = row.z
            if id ~= ids.loc_head_cave then active = row.x end
        elseif id == ids.pool then
            o.pools[#o.pools + 1] = { x = row.x, z = row.z }
        elseif id == ids.seedling then
            o.seedlings[#o.seedlings + 1] = { x = row.x, z = row.z }
        elseif id == ids.bomb then
            o.bombs[#o.bombs + 1] = { x = row.x, z = row.z }
        elseif id == ids.burst then
            o.bursts[row.x .. ":" .. row.z] = true
        end
    end
    -- A SEEDLING'S first sight is kept to its burst (first + 3) even when its
    -- loc goes: an acid pool dripped onto its tile replaces it (ol1e t185,
    -- the burst still came at 187). A new one on the same tile after the
    -- burst is a new seedling (ol1d: four on 6412,169 read the first one's
    -- time and three hit).
    for _, l in ipairs(o.seedlings) do
        local key = l.x .. ":" .. l.z
        if o.seed_first[key] == nil then o.seed_first[key] = { x = l.x, z = l.z, first = F.tick } end
    end
    for key, sd in pairs(o.seed_first) do
        if F.tick == sd.first + 3 then
            -- THE CONTENT CHECK: the burst at first sight + 3 (OLM_SPEC 4)
            CO.mech(S, F, "burst", o.bursts[key] == true,
                string.format("seedling %s seen %d: no burst loc at %d", key, sd.first, F.tick))
        end
        if F.tick >= sd.first + 3 then o.seed_first[key] = nil end
    end
    -- a bomb's first sight lives while its loc stands
    local here = {}
    for _, l in ipairs(o.bombs) do here[l.x .. ":" .. l.z] = true end
    for key in pairs(o.bomb_first) do
        if not here[key] then o.bomb_first[key] = nil end
    end
    if o.ox == nil and #xs >= 2 and z ~= nil then
        local minx = xs[1]
        for _, x in ipairs(xs) do if x < minx then minx = x end end
        o.ox, o.oz = minx - CO.V.WEST_LOC_X, z - CO.V.HEAD_LOC_Z
        local list = {}
        for _, x in ipairs(xs) do list[#list + 1] = tostring(x) end
        QD.raid._tob_trace(S, F.tick, string.format("olm body: head locs x {%s} z %d -> origin %d,%d (me %d,%d)",
            table.concat(list, ","), z, o.ox, o.oz, F.me.x, F.me.z))
    end
    if active ~= nil and o.ox ~= nil then
        o.side = (active - o.ox == CO.V.WEST_LOC_X) and 0 or 1
    end
end

-- The head-relative side of an animation -> the view (the west wall looks
-- east: its left is NORTH; the east wall's left is SOUTH).
function CO.dir_of(side, hs)
    if hs == 0 then return 1 end
    if side == 0 then return (hs == 1) and 2 or 0 end
    return (hs == 1) and 0 or 2
end

function CO.observe(S, F)
    local o, ids, V = S.o, S.ids, CO.V
    CO.find_body(S, F)
    -- the npcs: the hands and the head
    o.left_row, o.right_row, o.head_row, o.fire = nil, nil, nil, {}
    local heads, rising = 0, nil
    for _, row in ipairs(F.npcs) do
        local id = row.base_npc_id or row.npc_id
        if row.npc_id == ids.left then o.left_row = row
        elseif row.npc_id == ids.right then o.right_row = row
        elseif row.npc_id == ids.head then
            heads = heads + 1
            o.head_row = row
        elseif row.npc_id == ids.head_rising then
            heads = heads + 1
            rising = rising or row
        elseif row.npc_id == ids.firewall then o.fire[#o.fire + 1] = row end
        local _ = id
    end
    -- THE POINTS' INPUT: every new hitsplat on the head and the claws (solo,
    -- every one is mine), summed
    for _, h in ipairs({ "left_row", "right_row", "head_row" }) do
        local row = o[h]
        if row and row.hit_cycle and row.hit_cycle ~= o.dealt_seen[h] then
            o.dealt_seen[h] = row.hit_cycle
            if (row.hit_damage or 0) > 0 then
                o.dealt = o.dealt + row.hit_damage
                o.dealt_last = F.tick
            end
        end
    end
    -- (the attackable head first; the rising form only while it is the one)
    o.head_row = o.head_row or rising
    -- THE CONTENT CHECK: one head (ol1u: the dying claw was retyped into a
    -- second rising head at every phase's end)
    if heads > 1 and o.heads_noted ~= F.tick // 50 then
        o.heads_noted = F.tick // 50
        CO.mech(S, F, "one_head", false, heads .. " head npcs in the chamber")
    end
    -- THE FIRE WALL: its flames' first sight (they burn the row between at
    -- + 10, read against the end of + 9: cox_olm_wall_tick), and the two rows
    if #o.fire > 0 then
        if o.fire_first == nil then o.fire_first = F.tick end
        local z0, z1 = nil, nil
        for _, fw in ipairs(o.fire) do
            z0 = (z0 == nil or fw.z < z0) and fw.z or z0
            z1 = (z1 == nil or fw.z > z1) and fw.z or z1
        end
        o.fire_z0, o.fire_z1 = z0, z1
    else
        o.fire_first, o.fire_z0, o.fire_z1 = nil, nil, nil
    end
    -- the hands' last hitsplats: a landed hit since the last action steers
    -- (and my swings' landing rate on each, for the search: solo, every
    -- hitsplat on a claw is mine)
    for _, h in ipairs({ { "left_row", "hit_left", "left" }, { "right_row", "hit_right", "right" } }) do
        local row = o[h[1]]
        if row and row.hit_cycle and row.hit_cycle ~= o[h[2] .. "_cycle"] then
            o[h[2] .. "_cycle"] = row.hit_cycle
            local acc = o.acc[h[3]]
            acc.n = acc.n + 1
            if (row.hit_damage or 0) > 0 then
                o[h[2]] = true
                acc.h = acc.h + 1
            end
        end
    end
    if o.ox == nil then return end
    -- the loc animations: the head's tells and the left claw's signs
    local ar, anims = api_drive.loc_anims(0)
    if ar ~= "ok" then return end
    local hx = o.ox + ((o.side == 1) and V.EAST_LOC_X or V.WEST_LOC_X)
    local hz = o.oz + V.HEAD_LOC_Z
    for _, a in ipairs(anims) do
        local key = a.tick .. ":" .. a.x .. ":" .. a.z .. ":" .. a.seq_id
        if not o.seen[key] then
            o.seen[key] = F.tick
            o.seen_n = o.seen_n + 1
            local what = ids.head_seq[a.seq_id]
            if what and a.x == hx and a.z == hz then
                CO.head_event(S, F, what)
            else
                local cl = ids.claw_seq[a.seq_id]
                if cl then CO.claw_event(S, F, cl) end
            end
        end
    end
    -- (forget the seen keys older than the loc_anims window)
    if o.seen_n > 400 then
        local keep = {}
        for k, t in pairs(o.seen) do if t >= F.tick - 45 then keep[k] = t end end
        o.seen, o.seen_n = keep, 0
        for _ in pairs(keep) do o.seen_n = o.seen_n + 1 end
    end
    -- the chat: "chosen you as its target" -- eleven crystals, one a tick, on
    -- the tile I ended the previous tick on (OLM_SPEC 4)
    local mr, msgs = api_drive.messages(10)
    if mr == "ok" then
        for _, m in ipairs(msgs) do
            if m.serial > (o.msg_serial or 0) then
                -- (cox_olm_falling_tick: the first aim the tick after the
                -- cry, then one a tick for eleven; each lands 2 ticks after
                -- its aim: the last read is the end of cry + 12, one spare)
                if string.find(m.text or "", "chosen you as its target", 1, true) then
                    o.target_until = F.tick + 13
                end
                -- the phase's power ("rises with the power of acid/flame/
                -- crystal"; the penultimate and the final stand have all three)
                for _, pw in ipairs({ "acid", "flame", "crystal" }) do
                    if string.find(m.text or "", "power of <col=%x+>" .. pw) or string.find(m.text or "", "power of " .. pw, 1, true) then
                        o.power = pw
                    end
                end
                -- the acid drip: for 21 ticks (+2) a pool on the tile I
                -- ended the previous tick on (cox_olm_acid_tick), 16 ticks a pool
                if string.find(m.text or "", "smothered you in acid", 1, true) then
                    o.drip_until = F.tick + 23
                end
            end
        end
        if #msgs > 0 then o.msg_serial = math.max(o.msg_serial or 0, msgs[1].serial) end
    end
    -- the autos' style: the head's projectiles, the last seen
    local pr, projs = api_drive.projectiles(0)
    o.spheres, o.auto_inflight, o.spits = {}, false, {}
    local fw_rows = {}
    if pr == "ok" then
        for _, p in ipairs(projs) do
            -- the life siphon's two spits (the final stand): the two tiles that
            -- are safe at its hit, 8 ticks on (cox_olm_siphon_tick)
            if p.spotanim_id == ids.siphon_spit and (p.target or -1) <= 0 then
                if o.siphon == nil or F.tick - o.siphon.first > 10 then
                    o.siphon = { first = F.tick, tiles = {} }
                end
                local key = p.dst_x .. ":" .. p.dst_z
                if not o.siphon.tiles[key] then o.siphon.tiles[key] = { x = p.dst_x, z = p.dst_z } end
            end
            -- an acid spray's spit, at a tile (the drip's goes at a player):
            -- its pool lands with it (cox_olm_acid_spray)
            if p.spotanim_id == ids.acid_spit and (p.target or -1) <= 0 and (p.cycles_left or 0) > 0 then
                o.spits[#o.spits + 1] = { x = p.dst_x, z = p.dst_z, land = F.tick + (p.cycles_left + 15) // 30 }
            end
            -- THE FIRE WALL'S CAST (cox_olm_fire_wall): two projectiles from
            -- the head to the rows either side of the player it chose, z +- 1;
            -- the flames stand two ticks later, and THEN block (ol20 t212,
            -- t244: the plan only saw the flames, a tick too late to run the
            -- two tiles out; trapped, and a seedling under me burst for 31)
            if p.spotanim_id == ids.fw_travel and (p.target or -1) <= 0 then
                fw_rows[p.dst_z] = true
            end
            if p.spotanim_id == ids.proj_magic or p.spotanim_id == ids.proj_ranged then
                o.auto_inflight = o.auto_inflight or (p.cycles_left or 0) > 0
            end
            if p.spotanim_id == ids.proj_magic then o.style = "magic"
            elseif p.spotanim_id == ids.proj_ranged then o.style = "ranged"
            elseif p.spotanim_id == ids.sph_magic then o.spheres[#o.spheres + 1] = "magic"
            elseif p.spotanim_id == ids.sph_ranged then o.spheres[#o.spheres + 1] = "ranged"
            elseif p.spotanim_id == ids.sph_melee then o.spheres[#o.spheres + 1] = "melee" end
        end
    end
    -- (first sight is the cast tick; the pair is in the air two ticks)
    local z0, z1 = nil, nil
    for z in pairs(fw_rows) do
        z0 = (z0 == nil or z < z0) and z or z0
        z1 = (z1 == nil or z > z1) and z or z1
    end
    if z0 and z1 - z0 == 2 and (o.fw_cast == nil or o.fw_cast.z0 ~= z0 or F.tick - o.fw_cast.tick > 4) then
        o.fw_cast = { tick = F.tick, z0 = z0, z1 = z1 }
    end
    -- graphics: lightning bolts, the portal, crystal shadows (first sight;
    -- the portal's tile and first sight are remembered across the ticks its
    -- graphic is not sent)
    o.bolts, o.shadows = {}, {}
    -- A FRESH SEND, lane neutral: the largest cycles_left on a tile rises
    -- (a new graphic) instead of falling by a tick's 30 (scriptrun retires a
    -- floor graphic 40 ticks after it lands; the client by its sequence)
    local sr, spots = api_drive.spotanims(0)
    local cur = {}
    if sr == "ok" then
        for _, sp in ipairs(spots) do
            local key = sp.spotanim_id .. ":" .. sp.x .. ":" .. sp.z
            local c = cur[key]
            if c == nil or sp.cycles_left > c.left then cur[key] = { sp = sp, left = sp.cycles_left } end
        end
    end
    local prev = o.spot_prev or {}
    o.spot_prev = {}
    for key, c in pairs(cur) do
        o.spot_prev[key] = c.left
        local fresh = prev[key] == nil or c.left > prev[key] - 30
        local sp = c.sp
        if fresh then
            if sp.spotanim_id == ids.bolt then
                -- THE BOLT'S DIRECTION, per column (one bolt a column): the
                -- way it moved since its last tile; at first sight, from the
                -- end it starts at (ol20 t227: the old rule guessed from the
                -- half the bolt was in and looked up the tile behind THAT
                -- guess, so every bolt past the middle was predicted walking
                -- back the way it came -- the plan stood in its path, 24 and
                -- a stun, and the auto it then could not pray landed 16)
                local _, lz = CO.local_of(S, sp.x, sp.z)
                local c = o.bolt_col[sp.x]
                local dz
                if c and F.tick - c.tick <= 2 and c.z ~= sp.z then
                    dz = (sp.z > c.z) and 1 or -1
                elseif c and F.tick - c.tick <= 2 then
                    dz = c.dz
                else
                    dz = (lz - V.BOLT_LO <= V.BOLT_HI - lz) and 1 or -1
                end
                o.bolt_col[sp.x] = { z = sp.z, tick = F.tick, dz = dz }
                -- (tiles still to walk: 17 in all from its end)
                local left = (dz > 0) and (V.BOLT_LO + V.BOLT_TILES - 1 - lz) or (lz - (V.BOLT_HI - V.BOLT_TILES + 1))
                o.bolts[#o.bolts + 1] = { x = sp.x, z = sp.z, dz = dz, left = math.max(0, left) }
            elseif sp.spotanim_id == ids.portal then
                -- (sent 10/8/6/4 ticks before the swap: a send more than 4
                -- ticks after the last is a new portal)
                if o.portal_last == nil or F.tick - o.portal_last > 4 then
                    o.portal_tile, o.portal_first = { x = sp.x, z = sp.z }, F.tick
                end
                o.portal_last = F.tick
            elseif sp.spotanim_id == ids.shadow then
                o.shadow_seen[key] = { x = sp.x, z = sp.z, land = F.tick + 3 }
            end
        end
    end
    for key, sh in pairs(o.shadow_seen) do
        if sh.land < F.tick then o.shadow_seen[key] = nil
        else o.shadows[#o.shadows + 1] = sh end
    end
end

-- A head animation seen (first sight = its server tick, lesson 31/67).
function CO.head_event(S, F, what)
    local o, V = S.o, CO.V
    if what == "rise" or what == "rise_glow" then
        o.phase_n = o.phase_n + 1
        o.glowing = (what == "rise_glow")
        o.rise_tick = F.tick
        o.first_act = F.tick + ((o.phase_n == 1) and V.FIRST_P1 or V.FIRST_LATER)
        o.next_k = 0
        o.dir, o.skipped = 1, false
        o.hit_left, o.hit_right = false, false
        o.final = false
        o.left_alive, o.right_alive = true, true
        o.clench_until = -1
        QD.raid._tob_trace(S, F.tick, "olm rise " .. o.phase_n .. (o.glowing and " (glowing)" or "") .. " side " .. tostring(o.side))
        return
    end
    if what == "fall" then
        o.fallen = F.tick
        o.first_act = nil
        QD.raid._tob_trace(S, F.tick, "olm falls")
        return
    end
    local kind, hs = what:match("(%a+):(%d)")
    hs = tonumber(hs)
    local dir = CO.dir_of(o.side or 0, hs)
    if kind == "idle" then
        o.dir_seen = dir
        return
    end
    o.dir_seen = dir
    o.events[#o.events + 1] = { tick = F.tick, kind = kind, dir = dir }
    if #o.events > 32 then table.remove(o.events, 1) end
    -- THE METRONOME: a turn or an attack is an action, on the count's tick.
    -- One off it re-anchors the count ("the tick Olm acts on stays fixed for
    -- the whole phase": sync on what the head does, as a player does) and
    -- is a content check -- a rise that acts a tick late is the content's
    -- (ol1h: rise + 9 in phase 2); the 4:1 machine then recovers.
    if o.first_act ~= nil then
        local off = (F.tick - o.first_act) % 4
        local d = (off == 3) and -1 or off
        CO.mech(S, F, "clock", d == 0, string.format("action at %d is %+d off the count (rise %d, first %d)",
            F.tick, d, o.rise_tick or -1, o.first_act))
        if d ~= 0 then
            local u = o.undo
            if d > 0 and u and u.A > F.tick - 4 then
                -- (the step counted on the wrong tick: undone, run again now)
                o.dir, o.skipped, o.hit_left, o.hit_right = u.dir, u.skipped, u.hit_left, u.hit_right
                o.autos_at_me, o.specials_at_me = u.autos, u.specials
                o.next_k = u.k
                S.mon.rules.olm_step.checked = S.mon.rules.olm_step.checked - 1
            end
            o.first_act = o.first_act + d
            o.undo = nil
        end
    end
end

function CO.claw_event(S, F, what)
    local o = S.o
    local sign = what:match("sign:(%d)")
    if sign then
        o.signs[#o.signs + 1] = { tick = F.tick, kind = tonumber(sign) }
        if tonumber(sign) == 5 then o.protect_until = F.tick + 16 end
        return
    end
    if what == "clench" then o.clench_until = F.tick + 45
    elseif what == "unclench" then o.clench_until = F.tick
    elseif what == "left_down" then o.left_alive = false
    elseif what == "right_down" then o.right_alive = false
    elseif what == "left_up" and o.phase_n > 0 and F.tick > (o.rise_tick or 0) + 5 then o.left_alive = true
    elseif what == "right_up" and o.phase_n > 0 and F.tick > (o.rise_tick or 0) + 5 then o.right_alive = true end
    if not o.left_alive and not o.right_alive and o.glowing then o.final = true end
end

-- The bin a chamber-local z is in.
function CO.bin_of(lz)
    for i, b in ipairs(CO.V.BINS) do
        if lz >= b[1] and lz <= b[2] then return i end
    end
    return nil
end

-- THE COUNT: every action the rotation owes up to now, simulated with the
-- tile I ended its read tick on; the facing seen corrects the model (and the
-- monitor counts the corrections).
function CO.advance(S, F)
    local o = S.o
    if o.first_act == nil then return end
    local len = o.glowing and 16 or 12
    while true do
        local A = o.first_act + 4 * o.next_k
        if A > F.tick then break end
        local kind = CO.step_kind(o.next_k % len)
        o.undo = { k = o.next_k, A = A, dir = o.dir, skipped = o.skipped, hit_left = o.hit_left,
                   hit_right = o.hit_right, autos = o.autos_at_me, specials = o.specials_at_me }
        local me = QD.T2.tile_at(S, A - 1) or F.me
        local _, lz = CO.local_of(S, me.x, me.z)
        local lx = me.x - o.ox
        local bin = (lx >= CO.V.CH.x0 and lx <= CO.V.CH.x1) and CO.bin_of(lz) or nil
        local can = o.left_alive and not o.final and (o.clench_until or -1) < A
        local dir, skipped, autos, fired = CO.step(o.dir, o.skipped, kind, bin, o.side or 0, o.hit_left, o.hit_right, can)
        -- what was seen at A: a turn names the new facing
        local seen = nil
        for _, e in ipairs(o.events) do
            if e.tick == A then seen = e end
        end
        if seen and seen.kind == "turn" and seen.dir ~= dir then
            QD.T2.monitor.note(S, "olm_step", A, string.format("k%d kind %d bin %s: model %d, turned %d",
                o.next_k, kind, tostring(bin), dir, seen.dir))
            dir = seen.dir
        elseif seen == nil and dir ~= o.dir then
            QD.T2.monitor.note(S, "olm_step", A, string.format("k%d kind %d bin %s: model turned %d, none seen",
                o.next_k, kind, tostring(bin), dir))
            dir = o.dir
        end
        S.mon.rules.olm_step.checked = S.mon.rules.olm_step.checked + 1
        -- THE 4:1'S TELL: a special step skipped (no claw sign on its tick),
        -- per state (the final stand has none to skip)
        if kind >= 2 and not o.final and o.m and o.m.state then
            local sp = o.spec_tally[o.m.state] or { due = 0, fired = 0 }
            o.spec_tally[o.m.state] = sp
            sp.due = sp.due + 1
            for _, sg in ipairs(o.signs) do
                if sg.tick == A then sp.fired = sp.fired + 1 break end
            end
        end
        o.dir, o.skipped = dir, skipped
        o.autos_at_me = o.autos_at_me + autos
        if fired then o.specials_at_me = o.specials_at_me + 1 end
        o.hit_left, o.hit_right = false, false
        o.next_k = o.next_k + 1
    end
end

-- ================================================================ THE SEARCH
--
-- The next DEPTH actions with a read tick inside the horizon; every bin
-- sequence; the best by hp. -> { reads = { {t, bin}, ... }, score }
function CO.search(S, F, target, B, canon)
    local o, V = S.o, CO.V
    if o.first_act == nil or o.side == nil then return nil end
    local len = o.glowing and 16 or 12
    local steps = {}
    local k = o.next_k
    while #steps < 4 do
        local A = o.first_act + 4 * k
        if A - 1 > F.tick + S.t2.plan_opts.h then break end
        if A - 1 > F.tick then
            steps[#steps + 1] = { A = A, kind = CO.step_kind(k % len), slot = k % 4,
                                  can = o.left_alive and not o.final and (o.clench_until or -1) < A }
        end
        k = k + 1
    end
    if #steps == 0 then return nil end
    -- THE REACH: the target's rows in z (grown by the range for magic and
    -- ranged) and, for melee, the one column beside the claw; the hand a
    -- landed hit steers; my weapon clock and how often a swing lands
    local reach0, reach1, col, steer, speed, p = nil, nil, nil, nil, nil, 0
    local ready0 = nil
    if target then
        local r = (target.range > 1) and target.range or 0
        reach0, reach1 = target.lz - r, target.lz + 4 + r
        if target.range == 1 then col = target.col end
        steer, speed, p = target.hand, target.speed, target.p_hit
        ready0 = QD.T2.exec.plan_state(S, F, B).swing_ready
    end
    local my_lx, my_lz = CO.local_of(S, F.me.x, F.me.z)
    local bins = V.BINS
    local nb = #bins
    local function zgap(z0, z1)
        return math.max(0, reach0 - z1, z0 - reach1)
    end
    local function run_ticks(d) return (d + 1) // 2 end
    local function bin_gap(a, b)
        local A, B2 = bins[a], bins[b]
        return math.max(0, B2[1] - A[2], A[1] - B2[2])
    end
    -- from me (window 1) or from a bin (later windows: on the column)
    local me_reach = target and math.max(zgap(my_lz, my_lz), col and math.abs(my_lx - col) or 0) or nil
    -- EXPECTIMAX over the reads: my bin at each read is chosen, whether a
    -- swing in the window before it lands is chance (a landed hit steers the
    -- head at that read's action: OLM_SPEC 3). -> expected hp, the bins on
    -- the likelier branch
    -- (a DP: the subtree depends only on these; ~700 states, not 10^4 leaves)
    local memo = {}
    local walk
    local function walk0(i, dir, skipped, prev, t_prev, wready)
        if i > #steps then
            -- (recovery: a plan that ends in the 4:1 loop's own state is
            -- worth the autos and specials the loop saves after it)
            if canon then
                local d, sk = CO.canon(o.side, steps[#steps].slot)
                if dir == d and skipped == sk then return -V.CANON_VALUE, {} end
            end
            return 0, {}
        end
        local st = steps[i]
        local r = st.A - 1
        local ticks = r - t_prev
        local best, best_path = nil, nil
        for b = 1, nb do
            local d_move
            if i == 1 then
                d_move = math.max(0, bins[b][1] - my_lz, my_lz - bins[b][2])
            else
                d_move = bin_gap(prev, b)
            end
            if d_move <= ticks * 2 then
                -- the swings this window holds: in reach after the run there
                -- and the weapon clock, out again in time for the read
                local swings, wnext = 0, wready
                if target then
                    local d1 = (i == 1) and me_reach or zgap(bins[prev][1], bins[prev][2])
                    local s0 = math.max(t_prev + run_ticks(d1), wready)
                    local last = r - run_ticks(zgap(bins[b][1], bins[b][2]))
                    if target.phase then
                        -- (phased: the one swing a window, on the previous
                        -- step + phase)
                        local sw = st.A - 4 + target.phase
                        if sw > t_prev and sw >= s0 and sw <= last then
                            swings, wnext = 1, sw + speed
                        end
                    elseif s0 <= last then
                        swings = (last - s0) // speed + 1
                        wnext = s0 + swings * speed
                    end
                end
                local p_any = (swings > 0) and (1 - (1 - p) ^ swings) or 0
                local base_l = (i == 1) and o.hit_left or false
                local base_r = (i == 1) and o.hit_right or false
                local e, path, likely = 0, nil, nil
                for _, out in ipairs({ { true, p_any }, { false, 1 - p_any } }) do
                    local w = out[2]
                    if w > 0 then
                        local el = base_l or (out[1] and steer == "left")
                        local er = base_r or (out[1] and steer == "right")
                        local d2, s2, autos, fired = CO.step(dir, skipped, st.kind, b, o.side, el, er, st.can)
                        local sc = autos * V.AUTO_COST + (fired and (V.SPECIAL_COST[st.kind] or 10) or 0)
                        local sub, sub_path = walk(i + 1, d2, s2, b, r, wnext)
                        e = e + w * (sc + sub)
                        if likely == nil or w > likely then likely, path = w, sub_path end
                    end
                end
                -- the swings are worth their value; a run costs run energy
                -- and clicks (a hair per tile)
                e = e - V.SWING_VALUE * swings + 0.02 * d_move
                if best == nil or e < best then
                    best = e
                    best_path = { b }
                    for j = 1, #path do best_path[j + 1] = path[j] end
                end
            end
        end
        if best == nil then
            -- (no bin reachable: stay where I am; the planner answers)
            local b = (i == 1) and (CO.bin_of(my_lz) or prev) or prev
            local d2, s2 = CO.step(dir, skipped, st.kind, b, o.side, false, false, st.can)
            local sub, sub_path = walk(i + 1, d2, s2, b, r, wready)
            best, best_path = sub, { b }
            for j = 1, #sub_path do best_path[j + 1] = sub_path[j] end
        end
        return best, best_path
    end
    walk = function(i, dir, skipped, prev, t_prev, wready)
        if i == 1 then return walk0(i, dir, skipped, prev, t_prev, wready) end
        local key = ((((i * 4 + dir) * 2 + (skipped and 1 or 0)) * 8 + prev) * 16
                     + math.min(15, math.max(0, wready - t_prev)))
        local hit = memo[key]
        if hit then return hit[1], hit[2] end
        local sc, path = walk0(i, dir, skipped, prev, t_prev, wready)
        memo[key] = { sc, path }
        return sc, path
    end
    local score, path = walk(1, o.dir, o.skipped, CO.bin_of(my_lz) or 3, F.tick, ready0 or F.tick + 1)
    local reads = {}
    for j, st in ipairs(steps) do reads[j] = { t = st.A - 1, bin = path[j], kind = st.kind } end
    return { reads = reads, score = score }
end

-- ======================================================== CONTENT CHECKS
--
-- The point of the solver is the content: every mechanic it meets is checked
-- against OLM_SPEC as it lands, and a mismatch is a monitor row (olm_mech)
-- naming the mechanic -- a content defect, not a play mistake.
function CO.mech(S, F, name, ok, text)
    local o = S.o
    local t = o.mech[name] or { n = 0, bad = 0 }
    o.mech[name] = t
    t.n = t.n + 1
    S.mon.rules.olm_mech.checked = S.mon.rules.olm_mech.checked + 1
    if not ok then
        t.bad = t.bad + 1
        QD.T2.monitor.note(S, "olm_mech", F.tick, name .. ": " .. text)
    end
end

function CO.check(S, F)
    local o = S.o
    -- the solo portal: the swap at first sight + 8 reads my tile at the end
    -- of + 7; off it, min(50, 5 a tile) and a teleport onto it (I may run up
    -- to 2 tiles in the same tick after it)
    if o.portal_first and F.tick == o.portal_first + 8 and o.portal_tile then
        local at = QD.T2.tile_at(S, o.portal_first + 7)
        if at then
            local px, pz = o.portal_tile.x, o.portal_tile.z
            local d = math.max(math.abs(at.x - px), math.abs(at.z - pz))
            local want = math.min(50, 5 * d)
            local lost = (o.prev_hp or F.hp) - F.hp
            local near = math.max(math.abs(F.me.x - px), math.abs(F.me.z - pz)) <= 2
            -- (a heal of mine landing this tick hides the damage: ol17 t759 sipped
            -- and read -4 for a 15; then only the teleport is checked)
            local E = S.expect
            local healed = E ~= nil and E.tick == F.tick and (E.heal or 0) > 0
            local ok = (d == 0) or (near and (healed or lost >= want))
            CO.mech(S, F, "portal", ok, string.format("off by %d: lost %d (want %d)%s, now %d,%d vs portal %d,%d",
                d, lost, want, healed and " under a heal" or "", F.me.x, F.me.z, px, pz))
        end
    end
    -- THE POINTS (cox_points.rs2: 5 a point of damage, ^cox_points_per_damage;
    -- shown on the raids_overlay, %varp4609_raids_playerscore): judged when
    -- no hit has landed for two ticks, so a points packet a tick behind its
    -- hitsplat is not a mismatch
    local pr, pts = api_drive.varp(S.ids.playerscore)
    if pr == "ok" then
        o.points = pts
        if o.points0 == nil then o.points0 = pts - 5 * o.dealt end
        if o.dealt > 0 and F.tick - o.dealt_last >= 2 and o.points_judged ~= o.dealt then
            o.points_judged = o.dealt
            local want = o.points0 + 5 * o.dealt
            CO.mech(S, F, "points", pts == want, string.format("points %d, want %d (dealt %d)", pts, want, o.dealt))
        end
    end
    o.prev_hp = F.hp
end

-- ================================================================ PREDICT

-- A hit on the chamber tiles OUTSIDE a bin at read tick t (the search's choice).
function CO.require_bin(S, B, t, bin, cost, name)
    local V, o = CO.V, S.o
    local b = V.BINS[bin]
    local x0, x1 = o.ox + V.CH.x0, o.ox + V.CH.x1
    if b[1] > V.CH.z0 then
        B.hit({ name = name .. "-s", x0 = x0, z0 = o.oz + V.CH.z0, x1 = x1, z1 = o.oz + b[1] - 1,
                t0 = t, t1 = t, dmg_exp = cost, dmg_max = 0 })
    end
    if b[2] < V.CH.z1 then
        B.hit({ name = name .. "-n", x0 = x0, z0 = o.oz + b[2] + 1, x1 = x1, z1 = o.oz + V.CH.z1,
                t0 = t, t1 = t, dmg_exp = cost, dmg_max = 0 })
    end
end

function CO.predict(S, F, B)
    local o, V = S.o, CO.V
    CO.observe(S, F)
    CO.advance(S, F)
    CO.check(S, F)
    if o.ox == nil then return end
    local now, h = F.tick, B.h
    -- my own overload: 10 a hit wherever I stand, unprayable (it lands at t,
    -- the tile-end sample t - 1)
    for _, t in ipairs(o.ovl_hits or {}) do
        if t >= now + 1 then
            B.hit({ name = "overload", x0 = F.me.x - 40, z0 = F.me.z - 40, x1 = F.me.x + 40, z1 = F.me.z + 40,
                    t0 = t - 1, t1 = t - 1, dmg_exp = 10, dmg_max = 10 })
        end
    end
    -- the life siphon: on one of its two tiles at the end of first sight + 7
    -- (1-18 off them, and the head heals five times it): the nearer, pulled
    if o.siphon and o.siphon.first + 7 >= now + 1 then
        local best, bd = nil, nil
        for _, tl in pairs(o.siphon.tiles) do
            local d = math.max(math.abs(tl.x - F.me.x), math.abs(tl.z - F.me.z))
            if bd == nil or d < bd then best, bd = tl, d end
        end
        if best then
            local t = o.siphon.first + 7
            B.pull({ name = "siphon", x = best.x, z = best.z, size = 1, weight = 8, t0 = t, t1 = t })
        end
    end
    -- acid pools: every tick they stand (forbid: 3-6 and poison); and the
    -- ones still in the air, from their spit's landing
    for _, sp in ipairs(o.spits) do
        B.hit({ name = "spit", x = sp.x, z = sp.z, t0 = math.max(now + 1, sp.land - 1), t1 = now + h,
                dmg_exp = 5, dmg_max = 6, forbid = true })
    end
    for _, p in ipairs(o.pools) do
        B.hit({ name = "pool", x = p.x, z = p.z, t0 = now + 1, t1 = now + h, dmg_exp = 5, dmg_max = 6, forbid = true })
    end
    -- seedlings: the burst reads the end of first-sight + 2
    for _, sd in pairs(o.seed_first) do
        local t = sd.first + 2
        if t >= now + 1 then
            B.hit({ name = "burst", x = sd.x, z = sd.z, t0 = t, t1 = t, dmg_exp = 25, dmg_max = 40, forbid = true })
        end
    end
    -- lightning: each bolt walks one tile a tick away from its end; the tile
    -- it reaches on tick k is read at the end of k-1
    for _, bt in ipairs(o.bolts) do
        local dz = bt.dz
        for k = 1, math.min(h, bt.left) do
            local z = bt.z + dz * k
            B.hit({ name = "bolt", x = bt.x, z = z, t0 = now + k - 1, t1 = now + k - 1, dmg_exp = 20, dmg_max = 25,
                    forbid = true })
        end
    end
    -- the portal: be on it at the end of first-sight + 7 (5 a tile otherwise)
    if o.portal_tile and o.portal_first then
        local t = o.portal_first + 7
        if t >= now + 1 then
            local px, pz = o.portal_tile.x, o.portal_tile.z
            -- ON IT, as a rule: the rings below are priced only at the swap,
            -- so a beam ranked on the cost so far dropped every run to the
            -- tile before it got there (ol20 t795-802 and t1297-1304: 6 and
            -- 9 tiles out with 7 ticks to spare, it stayed and took 25 and
            -- 40 -- and the first one put it 3 from a crystal bomb, 30 more).
            -- A required zone is in the plan's late-hazard lookahead.
            B.zone({ name = "portal", x = px, z = pz, size = 1, lo = 0, hi = 0, t0 = t, t1 = t,
                     tier = "lethal", require = true })
            -- (every ring to the chamber's far corner: off 10 or more is the
            -- 50 cap, not free -- ol1f t224 sat 11 off and took it)
            for d = 1, 20 do
                local c = math.min(50, 5 * d)
                B.hit({ name = "portal", x0 = px - d, z0 = pz - d, x1 = px + d, z1 = pz - d, t0 = t, t1 = t, dmg_exp = c, dmg_max = c })
                B.hit({ name = "portal", x0 = px - d, z0 = pz + d, x1 = px + d, z1 = pz + d, t0 = t, t1 = t, dmg_exp = c, dmg_max = c })
                B.hit({ name = "portal", x0 = px - d, z0 = pz - d + 1, x1 = px - d, z1 = pz + d - 1, t0 = t, t1 = t, dmg_exp = c, dmg_max = c })
                B.hit({ name = "portal", x0 = px + d, z0 = pz - d + 1, x1 = px + d, z1 = pz + d - 1, t0 = t, t1 = t, dmg_exp = c, dmg_max = c })
            end
        end
    end
    -- crystal bombs: 60 within 1, 45 at 2, 30 at 3, 15 at 4, read at first-sight + 5
    for _, bm in ipairs(o.bombs) do
        local key = bm.x .. ":" .. bm.z
        o.bomb_first[key] = o.bomb_first[key] or now
        local t = o.bomb_first[key] + 5
        if t >= now + 1 then
            for d = 4, 1, -1 do
                local c = math.min(60, 15 * (5 - d))
                B.hit({ name = "bomb", x0 = bm.x - d, z0 = bm.z - d, x1 = bm.x + d, z1 = bm.z + d, t0 = t, t1 = t,
                        dmg_exp = 15, dmg_max = 15, forbid = (d <= 2) })
                local _ = c
            end
        end
    end
    -- crystals from the ceiling: 3x3 at the landing read
    for _, sh in ipairs(o.shadows) do
        local t = sh.land
        if t >= now + 1 then
            -- (beside it is 10-16: forbid too -- ol20 t1628 priced it under
            -- a read's cost and stood beside one in the final stand, where
            -- nothing heals)
            B.hit({ name = "crystal", x0 = sh.x - 1, z0 = sh.z - 1, x1 = sh.x + 1, z1 = sh.z + 1, t0 = t, t1 = t,
                    dmg_exp = 13, dmg_max = 16, forbid = true })
            B.hit({ name = "crystal", x = sh.x, z = sh.z, t0 = t, t1 = t, dmg_exp = 20, dmg_max = 25, forbid = true })
        end
    end
    -- the fire wall: the row between its two at the burn (50-65), and its
    -- flames, which a player cannot walk through (blockwalk=all)
    if o.fire_first and o.fire_z1 and o.fire_z1 - o.fire_z0 == 2 and o.fire_first + 9 >= now + 1 then
        local x0, x1 = nil, nil
        for _, fw in ipairs(o.fire) do
            x0 = (x0 == nil or fw.x < x0) and fw.x or x0
            x1 = (x1 == nil or fw.x > x1) and fw.x or x1
        end
        B.hit({ name = "burn", x0 = x0, z0 = o.fire_z0 + 1, x1 = x1, z1 = o.fire_z0 + 1,
                t0 = o.fire_first + 9, t1 = o.fire_first + 9, dmg_exp = 57, dmg_max = 65, forbid = true })
    end
    for _, fw in ipairs(o.fire) do
        B.hit({ name = "flame", x = fw.x, z = fw.z, t0 = now + 1, t1 = now + h, sample = "pass", dmg_exp = 50, dmg_max = 65,
                forbid = true })
    end
    -- THE WALL BEFORE ITS FLAMES (its cast, seen at C: cox_olm_wall_tick
    -- stands the flames at C + 2, reading the end of C + 1 for the leap
    -- (8-10, out a row), and burns the row between at C + 12, reading the
    -- end of C + 11): off all three rows by the end of C + 1 -- two tiles
    -- out, one tick's run -- and nothing lands. Priced, not forbidden: a
    -- leap (9) is better than a trap, and a trap a Water Strike gets out of
    -- is better than a forbid elsewhere; the flames themselves block.
    local fc = o.fw_cast
    if fc and o.fire_first == nil and fc.tick + 2 > now then
        local C = fc.tick
        local x0, x1 = o.ox + V.WALL_X0, o.ox + V.WALL_X1
        local zm = fc.z0 + 1
        if C + 1 >= now + 1 then
            for _, z in ipairs({ fc.z0, fc.z1 }) do
                B.hit({ name = "leap", x0 = x0, z0 = z, x1 = x1, z1 = z, t0 = C + 1, t1 = C + 1, dmg_exp = 9, dmg_max = 10 })
            end
        end
        for _, z in ipairs({ fc.z0, fc.z1 }) do
            B.hit({ name = "flame", x0 = x0, z0 = z, x1 = x1, z1 = z, t0 = math.max(now + 1, C + 2), t1 = now + h,
                    sample = "pass", dmg_exp = 50, dmg_max = 65, forbid = true })
        end
        if C + 11 <= now + h then
            B.hit({ name = "burn", x0 = x0, z0 = zm, x1 = x1, z1 = zm, t0 = C + 11, t1 = C + 11, dmg_exp = 57, dmg_max = 65 })
        end
    end
    -- the falling crystals on me: one aimed each tick A at the tile I ended
    -- A - 1 on, landing at A + 2 on whoever ended A + 1 there -- never end a
    -- tick on the tile I ended two ticks before (ol20 t1651-1653: the rule
    -- was "one tick before", and the crystals ran past its window)
    if o.target_until and o.target_until >= now + 1 then
        for k = 0, math.min(h, o.target_until - now) - 1 do
            if #B.spec.marks < QD.T2.V.MARKS_MAX then
                B.mark({ name = "falling", t_mark = now + k - 1, r = 0, t0 = now + k + 1, t1 = now + k + 1,
                         dmg_exp = 17, dmg_max = 20, forbid = true })
            end
        end
    end
    -- the acid drip: the tile I end tick k-1 on holds a pool from k (read
    -- against the end of k) for its 16 ticks
    if o.drip_until and o.drip_until > now then
        for k = 1, math.min(h, o.drip_until - now) do
            if #B.spec.marks < QD.T2.V.MARKS_MAX then
                B.mark({ name = "drip", t_mark = now + k - 1, r = 0, t0 = now + k, t1 = math.min(now + h, now + k + 15),
                         dmg_exp = 5, dmg_max = 6, forbid = true })
            end
        end
    end
end

-- ================================================================ STRATEGY

function CO.hp_of(row)
    if row == nil or row.health_scale == nil or row.health_scale <= 0 then return nil end
    return 600 * row.health_ratio / row.health_scale
end

-- ============================================================ THE MACHINE
--
-- The fight as a state machine (TOB2_DESIGN: STRATEGY is the mode machine).
-- Each state names what to hit, in what gear; a change is traced with its
-- cause and every decision-log row carries the state (intents.mode).
--
--   wait          no head up: before the first rise
--   mage          P1/P2, the mage hand up: the trident 3:0 (the search)
--   melee         P1/P2, the mage hand down: the tentacle 4:1 (CO.cycle:
--                 its own sub-machine, cycle / recover:<cause>)
--   interim       the hands down, Olm fallen: 33 ticks of crystals; wait
--                 on the next wall's start tile (he rises on the other wall)
--   p3-low-melee  the penultimate (head glowing): the melee hand to LOW_HP
--   p3-low-mage   ... then the mage hand to LOW_HP ("leave both hands
--                 around 75": the first one down regenerates in REGEN ticks)
--   p3-kill-melee the melee hand dead first (its infinity heal: hold off it
--                 while the sign shows, on the mage hand if it is high)
--   p3-kill-mage  the mage hand dead inside the regen
--   head          the final stand: the blowpipe on the head (the search)
function CO.machine(S, F)
    local o, V = S.o, CO.V
    local m = o.m
    local state, tgt = "wait", nil
    local L = o.left_alive and o.left_row or nil
    local R = o.right_alive and o.right_row or nil
    local melee_t = L and { row = L, style = "melee", hand = "left" } or nil
    local mage_t = R and { row = R, style = "magic", hand = "right" } or nil
    if o.ox == nil then
        state = "wait"
    elseif o.final then
        state, tgt = "head", o.head_row and { row = o.head_row, style = "ranged" } or nil
    elseif o.first_act == nil then
        state = (o.phase_n > 0) and "interim" or "wait"
    elseif not o.glowing then
        if mage_t then state, tgt = "mage", mage_t
        elseif melee_t then state, tgt = "melee", melee_t
        else state = "interim" end
    else
        -- (a hand never hit shows no health bar: unknown is FULL -- ol1i
        -- killed the melee hand with the mage hand's health unread and at 585,
        -- and the regen undid it)
        local lh, rh = CO.hp_of(L) or 600, CO.hp_of(R) or 600
        local healing = (o.protect_until or -1) >= F.tick
        if L and R then
            if lh and lh > V.LOW_HP and not healing then state, tgt = "p3-low-melee", melee_t
            elseif rh and rh > V.LOW_HP then state, tgt = "p3-low-mage", mage_t
            elseif healing then state, tgt = "p3-low-mage", mage_t
            else state, tgt = "p3-kill-melee", melee_t end
        elseif R then
            state, tgt = "p3-kill-mage", mage_t
        elseif L then
            -- (the mage hand went first: the melee hand inside its regen)
            state, tgt = "p3-kill-melee", (not healing) and melee_t or nil
        else
            state = "interim"
        end
    end
    if m.state ~= state then
        QD.raid._tob_trace(S, F.tick, string.format("olm state %s -> %s (t+%d in %s; hp %d, hands L %s R %s)",
            m.state, state, F.tick - m.since, m.state, F.hp or -1,
            tostring(L and math.floor(CO.hp_of(L) or -1)), tostring(R and math.floor(CO.hp_of(R) or -1))))
        m.state, m.since = state, F.tick
    end
    return state, tgt
end

-- THE 4:1 (the melee hand), the guides' loop (sources/olm_4to1_research.md
-- 1.4; tiles from the Kaoz / ThePerfectG tile packs), chamber-local on the
-- west wall, by the slot of the step a read is for (k mod 4: 0 auto, 1 null,
-- 2 auto, 3 special). Swings one tick after each step: hit 2 at the ring
-- finger (z 42), hit 3 there, hit 4 at the thumb (39), touch the mid skip
-- (35) for the second auto's read, hit 1 at the thumb, back to the ring
-- finger for the special's read.
--   (one change from the guides: the NULL's read on the ring finger, not on
--   tile 3 (z 40). Tile 3 is in both the middle and the north views: on a 0
--   the head "noodles" middle there and the second auto then sees the mid
--   skip. The ring finger is north-only; the step model says the head stays.)
-- The east wall mirrors it: z -> 72 - z.
CO.CYCLE_LZ = { [3] = 42, [0] = 42, [1] = 42, [2] = 35 }

function CO.cycle_lz(side, slot)
    local z = CO.CYCLE_LZ[slot]
    return (side == 0) and z or (72 - z)
end

-- The model state (facing, owed auto) after a step of this slot in the loop:
-- the hand's view (north on the west wall, south on the east) but for the
-- skipped second auto, which turns the head to the middle and owes it.
function CO.canon(side, slot)
    local H = (side == 0) and 2 or 0
    if slot == 2 then return 1, true end
    if slot == 3 then return H, true end
    return H, false
end

-- The 4:1's sub-machine: CYCLE while the model's state after every step is
-- the loop's (the guide's tiles, as pulls and the reads' bins); RECOVER the
-- moment a step leaves it, with its cause (what the step did: a special, an
-- auto the loop skips, a turn the loop does not make -- a noodle, a steer --
-- or the portal's swap moving me), until a step lands back in it. RECOVER
-- plans with the step search, scored for ending in the loop's own state: the
-- guides' "four possibilities" (3:1 once, 5 or 6 hits, 1:0 after a teleport)
-- are what it finds.
function CO.cycle(S, F, B, target)
    local o, V = S.o, CO.V
    local c = o.cyc
    if c == nil or c.phase ~= o.phase_n then
        c = { state = "recover", cause = "enter", since = F.tick, k = o.next_k - 1, phase = o.phase_n,
              autos = o.autos_at_me, specials = o.specials_at_me, n = 0 }
        o.cyc = c
    end
    local last_k = o.next_k - 1
    if last_k ~= c.k and last_k >= 0 then
        c.k = last_k
        local slot = last_k % 4
        local d, sk = CO.canon(o.side, slot)
        local ok = (o.dir == d and o.skipped == sk)
        local was = c.state
        if c.state == "cycle" and not ok then
            local cause = "turn"
            if o.specials_at_me > c.specials then cause = "special"
            elseif o.autos_at_me > c.autos and slot ~= 0 then cause = "auto"
            elseif o.portal_first and F.tick - o.portal_first <= 10 then cause = "portal" end
            c.state, c.cause, c.since = "recover", cause, F.tick
        elseif c.state == "recover" and ok then
            c.state, c.since = "cycle", F.tick
            c.n = c.n + 1
        end
        if c.state ~= was then
            QD.raid._tob_trace(S, F.tick, string.format("olm 4:1 %s -> %s (%s) at k%d slot %d: dir %d owed %s",
                was, c.state, c.cause, last_k, slot, o.dir, tostring(o.skipped)))
        end
        c.autos, c.specials = o.autos_at_me, o.specials_at_me
    end
    if c.state == "recover" then
        return CO.search(S, F, target, B, true), "recover-" .. c.cause
    end
    -- CYCLE: the loop's reads, their bins and the guide's tiles
    local len = o.glowing and 16 or 12
    local reads = {}
    local k = o.next_k
    while true do
        local A = o.first_act + 4 * k
        if A - 1 > F.tick + B.h then break end
        if A - 1 > F.tick then
            local slot = k % 4
            local lz = CO.cycle_lz(o.side, slot)
            -- THE FLAME SKIP (guides: "after hit 2, step one more tile toward
            -- the wall, so Olm cannot fit a wall when it autos you"): a wall
            -- needs a row either side inside z 27..44 (cox_olm_fire_wall), so
            -- the auto's read on z 43 (west, still beside the claw) or 28
            -- (east) leaves it nowhere to go
            if slot == 0 and (o.power == "flame" or o.glowing) then
                lz = (o.side == 0) and 43 or 28
            end
            reads[#reads + 1] = { t = A - 1, bin = CO.bin_of(lz), kind = CO.step_kind(k % len), slot = slot }
            local x, z = CO.abs_of(S, target.col, lz)
            B.pull({ name = "cycle", x = x, z = z, size = 1, weight = V.CYCLE_PULL, t0 = A - 1, t1 = A - 1 })
        end
        k = k + 1
    end
    return { reads = reads, score = 0 }, "cycle"
end

function CO.gear(S, style)
    local out = {}
    for _, obj in ipairs(S.ids.sets[style]) do
        if not QD.raid._tob_worn(S, obj) and QD.raid._tob_count(S, obj) > 0 then
            out[#out + 1] = { obj = obj, two_hand = false }
        end
    end
    return out
end

-- TRAPPED: the flames on the rows either side of me, over my column -- a
-- Water Strike from the rune pouch puts out the segment toward where I am
-- going (the target's rows, else the chamber's middle) and the gap is the way
-- out (cox_olm_douse; the guides carry "the lowest water spell"). Once sent,
-- the cast's segment is gone the next tick; the burn term keeps the planner
-- off the row in the meantime.
function CO.douse(S, F, B, intents, target)
    local o, V, ids = S.o, CO.V, S.ids
    if not (o.fire_first and o.fire_z1 and o.fire_z1 - o.fire_z0 == 2) then return end
    if F.me.z ~= o.fire_z0 + 1 then return end
    local under, over = nil, nil
    for _, fw in ipairs(o.fire) do
        if fw.x == F.me.x then
            if fw.z == o.fire_z0 then under = fw else over = fw end
        end
    end
    if under == nil and over == nil then return end
    local _, my_lz = CO.local_of(S, F.me.x, F.me.z)
    local goal_lz = target and (target.lz + 2) or 35
    local fw = (goal_lz > my_lz and over) or under or over
    local i = B.target({ name = "douse", npc = fw, size = 1, range = 10, speed = 5, value = 30,
                         cast = ids.water_strike })
    -- (no tgt: the cast goes on the first tick the plan stands, not on the
    -- plan's swing flag; the target only keeps the plan standing)
    intents.cast = { component = ids.water_strike, npc = fw, ready = true, cooldown = 5 }
    local _ = i
    intents.mode = (intents.mode or "") .. ":douse"
    if o.douse_traced ~= o.fire_first then
        o.douse_traced = o.fire_first
        QD.raid._tob_trace(S, F.tick, string.format("olm trapped between z %d and %d at %d,%d: douse %d,%d (flames seen %d)",
            o.fire_z0, o.fire_z1, F.me.x, F.me.z, fw.x, fw.z, o.fire_first))
    end
end

-- THE OVERHEAD, lazily: the content reads the protection prayer as an auto
-- LANDS (cox_olm_auto_land), so it is up from the tick before an action the
-- step model says fires an auto at me -- from my tile now or the plan's at
-- that read -- until its projectile is down; a sphere in flight names its
-- own. Off otherwise: protect + a boost drains ~0.6 points a tick, and ol1h
-- had none left at t1190 (16 restore doses). A sphere launched with a
-- protect up saps it (1 in 20 autos); the guides keep it up the same way.
-- (Named only while unlit: one press a tick, and the boost -- the resource
-- channel's res.pray -- takes the slot when the room leaves it empty.)
function CO.prayers(S, F, intents, res)
    local o = S.o
    -- (a douse takes the panel first: the cast needs the spellbook, and a
    -- protect press there cost ol1m t764 its cast -- the burn is 50-65)
    if intents.cast and #o.spheres == 0 then return end
    local prot = nil
    if #o.spheres > 0 then
        prot = QD.T2.PROTECT[o.spheres[1]]
    elseif o.first_act ~= nil and o.ox ~= nil then
        local want = o.auto_inflight
        local A = o.first_act + 4 * o.next_k
        if not want and A - F.tick <= 3 then
            local kind = CO.step_kind(o.next_k % (o.glowing and 16 or 12))
            local lx, lz = CO.local_of(S, F.me.x, F.me.z)
            local bins = { (lx >= CO.V.CH.x0 and lx <= CO.V.CH.x1) and CO.bin_of(lz) or nil }
            if res and res.reads and res.reads[1] and res.reads[1].t == A - 1 then bins[2] = res.reads[1].bin end
            for _, b in pairs(bins) do
                local _, _, autos = CO.step(o.dir, o.skipped, kind, b, o.side or 0, o.hit_left, o.hit_right, false)
                if autos > 0 then want = true end
            end
        end
        -- (hysteresis: once up it stays two ticks past the last want --
        -- ol1j flipped it 800 times, a press a tick)
        if want then
            o.prot_want_t = F.tick
        elseif o.prot_want_t and F.tick - o.prot_want_t <= 2 then
            want = true
        end
        if want then prot = QD.T2.PROTECT[o.style or "magic"] end
    end
    if prot and not F.lit[prot] then intents.pray = prot end
    local off = {}
    -- (an overhead replaces the lit one by itself: pressing the old one "off"
    -- as well toggled it straight back on -- ol1s, two presses every two ticks
    -- and the overhead never moved, so every style switch landed unprayed)
    if prot == nil then
        for _, name in pairs(QD.T2.PROTECT) do
            if F.lit[name] then off[#off + 1] = name end
        end
    end
    -- the boosts: only while there is something to hit
    if intents.pray_boost == nil then
        for _, name in ipairs({ "piety", "rigour", "augury" }) do
            -- (not one the resource channel still presses from last tick's want)
            if F.lit[name] and S.t2.want.boost ~= name then off[#off + 1] = name end
        end
    end
    if #off > 0 then intents.pray_off = off end
end

-- THE ROOM'S OWN DRINKS (the supplies drink Xeric's aid and revitalisation):
--   overload (+)       five minutes: re-boosts every combat stat each 25
--                      ticks for 500, and at + 525 ends and gives back the
--                      50 it cost (five hits of 10 as it goes down). Drunk
--                      again THAT tick -- overloaded the whole fight, and a
--                      drink while it runs restarts the timer and loses the
--                      50 -- at 85+ hp (the supplies top up to it), no auto in flight
--   revitalisation (+) a fighting stat under its base (Xeric's aid takes 9%
--                      + 2 a sip, deep burn 2 a hit; the overload's re-boost
--                      only ADDS its boost, min(level + d, base + d)): at once
--   prayer enhance (+) a point a prayer tick for 458 ticks: under 85 prayer
--   stamina            under 30% run energy (the 4:1 runs 7 tiles in 4 ticks)
-- A drink is known by its dose count falling (my own inventory).
function CO.doses(S, list)
    local n = 0
    for i, obj in ipairs(list) do n = n + i * QD.raid._tob_count(S, obj) end
    return n
end

-- The overload damage still to land after now.
function CO.ovl_pending(S, F)
    local n = 0
    for _, t in ipairs(S.o.ovl_hits or {}) do
        if t > F.tick then n = n + 10 end
    end
    return n
end

-- THE SUPPLIES' MACHINE: the hp the brew floor judges is the hp the
-- overload's timeline will leave me (the supplies drink Xeric's aid under it):
--   steady    no overload damage owed: the floor (BREW_BELOW)
--   topping   the next dose is due: up to 88 first
--   draining  its 10s still to land: 25 plus what is owed (never under 25
--             when it is done); the steady floor brings it back after
--   (its end gives the 50 back, and the next dose takes it that tick: no
--   change to the floor)
-- -> the floor for this tick; the state is traced as it changes.
function CO.supplies(S, F)
    local o, V = S.o, CO.V
    local pending = CO.ovl_pending(S, F)
    local state, floor = "steady", V.BREW_BELOW
    local due = (o.ovl_until or -1) <= F.tick and CO.doses(S, S.ids.overloads) > 0
    if due then
        -- (its dose is due: up to 85 first, so the 50 it takes leaves 35 --
        -- drunk at 55, it left the final stand on 5, sweep4 ol20)
        state, floor = "topping", 88
    elseif pending > 0 then
        -- (only enough that the rest of the drain leaves 25: brewing back to
        -- 90 under each dose cost four sips an overload, the kit was dry by
        -- the head -- sweep4 ol20/ol37; the steady floor tops up after)
        state, floor = "draining", 25 + pending
    end
    if o.supply ~= state then
        QD.raid._tob_trace(S, F.tick, string.format("olm supplies %s -> %s (hp %d, owed %d, floor %d)",
            tostring(o.supply), state, F.hp or -1, pending, floor))
        o.supply = state
    end
    return floor
end

-- The stats the current fight swings with: the style's (melee: attack and
-- strength), from the room's target.
function CO.fight_stats(style)
    if style == "melee" then return { "attack", "strength" } end
    if style == "magic" then return { "magic" } end
    if style == "ranged" then return { "ranged" } end
    return {}
end

-- Is a fighting stat under its base? (strict: the olm_stats check)
function CO.drained(S, F)
    for _, st in ipairs(CO.fight_stats(S.o.fight_style)) do
        local lv = F.levels and F.levels[st]
        if lv and lv.level < lv.base then return true end
    end
    return false
end

-- Worth a revitalisation now? The overload re-boosts every 25 ticks from its
-- drink (it ADDS its boost, so one re-boost lifts a Xeric's-aid drain back
-- over the base): a drain is restored only when the next re-boost is more
-- than 8 ticks off, or it is deep (more than 5 under). A revitalisation for
-- every brew sip emptied the kit by the head phase (sweep4 ol20, ol37).
function CO.restore_due(S, F)
    local o = S.o
    local deep = false
    for _, st in ipairs(CO.fight_stats(o.fight_style)) do
        local lv = F.levels and F.levels[st]
        if lv and lv.level < lv.base - 5 then deep = true end
    end
    if deep then return true end
    if not CO.drained(S, F) then return false end
    if o.ovl_until == nil or o.ovl_until < F.tick then return true end
    local drunk = o.ovl_until - 525
    local next_boost = drunk + 25 * ((F.tick - drunk) // 25 + 1)
    return next_boost - F.tick > 8
end

-- THE CHECK (olm_stats): every tick with something to hit, overloaded, no
-- fighting stat more than 5 under its base, none under it 10+ ticks running. A tick that fails is the solver's fault.
function CO.check_stats(S, F)
    local o = S.o
    if o.fight_style == nil then return end
    local R = S.mon.rules.olm_stats
    R.checked = R.checked + 1
    local why = nil
    if CO.drained(S, F) then
        o.drained_since = o.drained_since or F.tick
    else
        o.drained_since = nil
    end
    local deep = false
    for _, st in ipairs(CO.fight_stats(o.fight_style)) do
        local lv = F.levels and F.levels[st]
        if lv and lv.level < lv.base - 5 then deep = true end
    end
    if (o.ovl_until or -1) < F.tick then why = "not overloaded"
    elseif deep then why = "drained deep"
    elseif o.drained_since and F.tick - o.drained_since > 10 then why = "drained 10+ ticks" end
    if why then
        o.stat_bad = (o.stat_bad or 0) + 1
        if o.stat_why ~= why then
            local parts = {}
            for _, st in ipairs(CO.fight_stats(o.fight_style)) do
                local lv = F.levels and F.levels[st]
                if lv then parts[#parts + 1] = string.format("%s %d/%d", st, lv.level, lv.base) end
            end
            QD.T2.monitor.note(S, "olm_stats", F.tick, why .. " (" .. table.concat(parts, ", ") .. ")")
        end
    end
    o.stat_why = why
end

function CO.drinks(S, F, intents)
    local o, ids = S.o, S.ids
    for _, d in ipairs({ { "overloads", "ovl_until", 525 }, { "enhances", "penh_until", 458 },
                         { "staminas", "stam_until", 200 } }) do
        local n = CO.doses(S, ids[d[1]])
        local key = d[1] .. "_n"
        if o[key] ~= nil and n < o[key] then
            o[d[2]] = F.tick + d[3]
            -- THE OVERLOAD'S COST (cox_potion.rs2 cox_overload_damage): five
            -- hits of 10 a second apart from the drink + 1 -- the first is in
            -- by the tick its dose is seen gone (ol1k: seen 70, hits 70..78);
            -- the 50 comes back as it ends (+525)
            if d[1] == "overloads" then
                o.ovl_hits = {}
                for j = 0, 4 do o.ovl_hits[#o.ovl_hits + 1] = F.tick + 2 * j end
            end
        end
        o[key] = n
    end
    if intents.drink or intents.eat or F.tick - (S.last_drink or -10) < 3 then return end
    local pick = nil
    -- (again the tick it ends, not before: a drink while it runs restarts its
    -- timer and the 50 never comes back; at 90+ it waited forever -- ol1r
    -- drank twice, and all of P3 went without)
    if (o.ovl_until or -1) <= F.tick and F.hp >= 85 and CO.ovl_pending(S, F) == 0
        and not o.auto_inflight and not intents.cast then
        pick = QD.raid._tob_first(S, ids.overloads)
    end
    if pick == nil and CO.restore_due(S, F) then
        pick = QD.raid._tob_first(S, ids.restores)
    end
    if pick == nil and (o.penh_until or -1) < F.tick and F.prayer < 85 then
        pick = QD.raid._tob_first(S, ids.enhances)
    end
    if pick == nil and (o.stam_until or -1) < F.tick and (F.run_energy or 0) < 3000 then
        pick = QD.raid._tob_first(S, ids.staminas)
    end
    if pick then
        intents.drink = pick
        S.last_drink = F.tick
    end
end

function CO.strategy(S, F, B)
    local o, V, ids = S.o, CO.V, S.ids
    local mode, tgt = CO.machine(S, F)
    local intents = { mode = mode, eat_below = V.EAT_BELOW, brew_below = V.BREW_BELOW }
    if o.ox == nil then
        return intents
    end
    -- THE TARGET: worn gear first (never a target in a style whose weapon is
    -- not worn now: a wield and an attack in one tick swing the old weapon)
    local target = nil
    if tgt and tgt.row then
        intents.gear = CO.gear(S, tgt.style)
        local weapon = (tgt.style == "melee") and ids.tentacle or ((tgt.style == "magic") and ids.trident or ids.xbow)
        local lx, lz = CO.local_of(S, tgt.row.x, tgt.row.z)
        -- (melee: the column beside the 5-wide claw, toward the chamber)
        local col = (o.side == 0) and (lx + 5) or (lx - 1)
        local acc = o.acc[tgt.hand or "head"]
        target = { lz = lz, col = col, range = V.RANGE[tgt.style], speed = V.SPEED[tgt.style], hand = tgt.hand,
                   p_hit = (acc.h + 2) / (acc.n + 5) }
        if QD.raid._tob_worn(S, weapon) and tgt.style == "melee" and o.first_act then
            -- THE MELEE PHASE: "attack one tick after Olm's attack" (the
            -- guides; OLM_SPEC 3): a swing goes out only on a step + 1, one
            -- window per step in the horizon (ol1g: a swing on the step
            -- itself put hit 4 at the ring finger and the mid skip out of
            -- reach, 26 loops in a row)
            target.phase = 1
            local k = math.max(0, o.next_k - 1)
            local n = 0
            while n < 5 do
                local sw = o.first_act + 4 * k + 1
                if sw > F.tick + B.h then break end
                if sw > F.tick then
                    B.target({ name = mode .. "@" .. sw, npc = tgt.row, size = 5, range = target.range,
                               speed = target.speed, value = V.SWING_VALUE, t_ok0 = sw, t_ok1 = sw })
                    n = n + 1
                end
                k = k + 1
            end
        elseif QD.raid._tob_worn(S, weapon) then
            B.target({ name = mode, npc = tgt.row, size = 5, range = target.range, speed = target.speed,
                       value = V.SWING_VALUE })
        end
        if F.prayer >= 30 then
            intents.pray_boost = ids.boost[tgt.style]
        end
    else
        intents.gear = CO.gear(S, "melee")
    end
    if mode == "head" and F.tick % 10 == 0 then
        local g = {}
        for _, e in ipairs(intents.gear or {}) do g[#g + 1] = tostring(e.obj) end
        local sent = {}
        for _, x in ipairs((S.x2 and S.x2.sent) or {}) do sent[#sent + 1] = tostring(x.ch) .. ":" .. tostring(x.what) .. "=" .. tostring(x.r) end
        local I = S.x2 and S.x2.interact or {}
        QD.raid._tob_trace(S, F.tick, string.format("olm head: gear due {%s}, target %s, xbow worn %s, head row %s at %s,%s; interact %s; sent %s",
            table.concat(g, ","), tostring(target ~= nil), tostring(QD.raid._tob_worn(S, ids.xbow)),
            tostring(o.head_row and o.head_row.npc_id), tostring(o.head_row and o.head_row.x), tostring(o.head_row and o.head_row.z),
            tostring(I.state), table.concat(sent, " ")))
    end
    -- THE READS: the 4:1 machine on the melee hand, the step search for
    -- everything else (the mage hand's 3:0, the head) -> the read ticks' bins
    local res
    if target and tgt.style == "melee" and QD.raid._tob_worn(S, ids.tentacle) then
        local sub
        res, sub = CO.cycle(S, F, B, target)
        intents.mode = mode .. ":" .. sub
    else
        res = CO.search(S, F, target, B)
    end
    o.plan = res
    o.fight_style = tgt and tgt.style or nil
    CO.check_stats(S, F)
    CO.douse(S, F, B, intents, target)
    CO.prayers(S, F, intents, res)
    CO.drinks(S, F, intents)
    -- (no food in the kit: the supplies brew only under eat_below, so the
    -- machine's floor is both)
    intents.brew_below = CO.supplies(S, F)
    intents.eat_below = intents.brew_below
    -- an empty gourd vial goes on the floor only when the backpack is full
    -- (a backpack op ends the attack)
    if not intents.drink and not intents.eat and QD.raid._tob_count(S, ids.vial_empty) > 0
        and QD.T2.exec.free_slots(S) == 0 then
        intents.drop = ids.vial_empty
    end
    if res then
        for i, r in ipairs(res.reads) do
            CO.require_bin(S, B, r.t, r.bin, V.REQ_COST, "read" .. i)
        end
    end
    -- in the chamber, out of the hazards' way when there is nothing to hit;
    -- in the interim, on the start tile of the wall he rises on next
    if mode == "interim" and o.side ~= nil then
        local st = V.START[1 - o.side]
        local sx, sz = CO.abs_of(S, st[1], st[2])
        B.pull({ name = "start", x = sx, z = sz, size = 1, weight = 0.3, t0 = F.tick, t1 = F.tick + B.h })
    elseif target == nil then
        local cx, cz = CO.abs_of(S, 16, 35)
        B.pull({ name = "centre", x = cx - 1, z = cz - 1, size = 3, weight = 0.05, t0 = F.tick, t1 = F.tick + B.h })
    end
    return intents
end

-- (test hook, test/raids/cox_olm_douse.lua: the room's own cast call on the
-- nearest flame -- the test environment has no api_drive)
function CO.cast_on_flame(spell)
    local cr, comp = api_drive.component("magic_spellbook:" .. spell)
    assert(cr == "ok", "cox_olm: no component magic_spellbook:" .. spell)
    local fr, fid = api_drive.symbol("npc", "olm_firewall_npc")
    assert(fr == "ok", "cox_olm: no npc olm_firewall_npc")
    local tr, tab = api_drive.tab_by_name("magic")
    assert(tr == "ok", "cox_olm: no magic tab")
    api_drive.tab(tab)
    local nr, rows = api_drive.npcs(6)
    assert(nr == "ok", "cox_olm: npcs answered " .. tostring(nr))
    for _, row in ipairs(rows) do
        if row.npc_id == fid then return api_drive.cast_npc(row.npc_id, comp, row.element_id) end
    end
    return "no flame"
end

-- ================================================================ THE ROOM

QD.T2.register("cox_olm", {
    ids = function(opts) return CO.ids(opts) end,

    init = function(S, opts)
        opts = opts or {}
        S.who = "cox_olm_v2_solve"
        S.t2.plan_opts = { h = opts.h or CO.V.H, beam = opts.beam or CO.V.BEAM }
        S.o = {
            seen = {}, seen_n = 0, events = {}, signs = {}, phase_n = 0, glowing = false,
            dir = 1, skipped = false, next_k = 0, side = nil,
            left_alive = false, right_alive = false, final = false,
            m = { state = "init", since = 0 }, mech = {}, spec_tally = {},
            dealt = 0, dealt_seen = {}, dealt_last = -10,
            hit_left = false, hit_right = false,
            autos_at_me = 0, specials_at_me = 0,
            seed_first = {}, bomb_first = {}, bolt_col = {}, shadow_seen = {},
            acc = { left = { n = 0, h = 0 }, right = { n = 0, h = 0 }, head = { n = 0, h = 0 } },
            pools = {}, seedlings = {}, bombs = {}, bolts = {}, shadows = {}, fire = {}, spheres = {},
        }
        S.mon.rules.olm_step = { n = 0, checked = 0, traced = 0 }
        S.mon.rules.olm_mech = { n = 0, checked = 0, traced = 0 }
        S.mon.rules.olm_stats = { n = 0, checked = 0, traced = 0 }
    end,

    -- Rapid / Accurate (style slot 1) before the fight: the blowpipe's Rapid,
    -- the trident's Accurate (never long range: the guides' 3:0 needs the
    -- drag), the tentacle's Controlled; it survives every wield in the kit.
    enter = function(S)
        local ids = S.ids
        for press = 1, 4 do
            local mr, mode = api_drive.varp(ids.com_mode)
            assert(mr == "ok", S.who .. ": varp43_com_mode answered " .. tostring(mr))
            if mode == 1 then break end
            assert(press <= 3, S.who .. ": three style presses and varp43_com_mode reads " .. tostring(mode))
            QD.raid._tob_tab(S, ids.combat_tab)
            local pr, pd = api_drive.if_click(ids.style_slot_1, 1)
            assert(pr == "ok", S.who .. ": the style press answered " .. tostring(pr) .. " " .. tostring(pd))
            for _ = 1, 2 do
                await({ event = "server_tick", match = function() return true end, note = S.who .. ": style" }, 3)
            end
        end
    end,

    predict = function(S, F, B) CO.predict(S, F, B) end,

    strategy = function(S, F, B)
        local intents = CO.strategy(S, F, B)
        -- the boost prayer: on while fighting, through the resource channel
        S.t2.want.boost = intents.pray_boost
        intents.pray_boost = nil
        return intents
    end,

    done = function(S, F)
        local o = S.o
        -- the head dead: its fall in the final stand
        if o.final and o.fallen and o.fallen > (o.rise_tick or 0) then return "ok" end
        return nil
    end,

    summary = function(S)
        local o = S.o
        local mech = {}
        for name, t in pairs(o.mech) do mech[#mech + 1] = string.format("%s %d/%d", name, t.bad, t.n) end
        table.sort(mech)
        local tally = {}
        for st, sp in pairs(o.spec_tally) do
            tally[#tally + 1] = string.format("%s %d/%d", st, sp.due - sp.fired, sp.due)
        end
        table.sort(tally)
        local c = o.cyc
        return string.format("olm: points %s (dealt %d), phases %d, state %s, autos at me %d, specials at me %d, step mismatches %d/%d, "
            .. "mechanic mismatches {%s}, 4:1 entries %d, stats bad %d/%d ticks, specials skipped {%s}",
            tostring(o.points), o.dealt, o.phase_n, o.m.state, o.autos_at_me, o.specials_at_me, S.mon.rules.olm_step.n,
            S.mon.rules.olm_step.checked, table.concat(mech, ", "), c and c.n or 0,
            o.stat_bad or 0, S.mon.rules.olm_stats.checked, table.concat(tally, ", "))
    end,
})
