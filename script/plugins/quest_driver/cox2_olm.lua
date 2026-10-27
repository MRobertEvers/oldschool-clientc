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
-- does. The strategy runs his own rule (OLM_SPEC 3, the content's step) and
-- hands the planner a view bin for each read tick: the MELEE hand first, by
-- the 4:1 (CO.cycle); the MAGE hand only while the melee hand is clenched or
-- dead, by HEAD RUNNING (CO.headrun: the head turns on every step and never
-- attacks); P3 its own states (both hands down inside the regen); the head by
-- the step search (CO.search: every way of standing in the next four reads,
-- scored in hp).
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
        { 36, 36, 1, 1, 1 },     -- C  all three views, counted in each (cox_olm_most_dir_steered, RSProx 820)
        { 37, 41, 0, 1, 1 },     -- D  MIDDLE + NORTH
        { 42, 44, 0, 0, 1 },     -- E  NORTH only
    },
    -- the clock (OLM_SPEC 3); the rise's second beat (cox_olm_rise_done,
    -- the ghost clench's 2-tick poll grid) is the rise animation + 5 (its full length)
    -- (P1's first action: rise + 6, RECORDED -- ^cox_olm_first_act_p1 since
    -- OSRS-Content ff130e6540; it was 9)
    FIRST_P1 = 6, FIRST_LATER = 8, ACT = 4, RISE_DONE = 5,
    -- THE CLENCH (cox_olm_clench_check): one hit on the melee hand of 1/20
    -- of the solo hand's 600, in the window a null opens and the special
    -- after it judges (wiki Strategies: "the eight ticks between null and
    -- special"); 45 ticks (cox_olm.constant). The P3
    -- healing hand: 16 ticks (^cox_olm_protect_ticks), to the next special.
    HAND_HP = 600, CLENCH_HIT = 30, CLENCH_TICKS = 45, PROTECT_TICKS = 16,
    -- THE FIRE WALL: the row between burns this many ticks after the flames
    -- land (^cox_olm_wall_ticks)
    WALL_TICKS = 8,
    -- the clench's end: back to the melee hand this many ticks before it is
    -- hittable again (the gear switch and the 4:1's first read)
    MELEE_LEAD = 4,
    -- a seen auto in a flame phase (or P3 / the final stand): a third of the
    -- time a phase attack, one of the power's; a fire wall judges where I
    -- END the step tick (cox_olm_fire_wall: the walls land the tick after
    -- the cast), so standing still on a tile a wall fits costs this
    WALL_RISK = 6,
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
    -- head running: a read in view is an attack the method exists to deny
    -- (ol1 t138: a cast from the long line left the next read a tile short)
    RUN_REQ_COST = 30,
    MOVE_BIN = 8,           -- tiles a run covers between two reads (4 ticks x 2)
    -- P3: both hands down to this, then the kills; above a swing's max so a
    -- low hand is not killed by accident (ol1t: the mage hand at 60 outlived
    -- the 50-tick regen)
    LOW_HP = 40,
    REGEN = 50,             -- P3: the first hand down rises again this many ticks later
    CYCLE_PULL = 1.5,       -- 4:1: hp a tile off the guide's tile at a read
    CANON_VALUE = 8,        -- recovery: hp for a plan that ends back in the 4:1 loop
    -- BETWEEN PHASES: where the next phase starts (the guides' "P2/P3 mage
    -- start", chamber-local), by the wall Olm will rise on; the crystals rain
    -- this many ticks from the retreat (^cox_olm_transition_crystals), on
    -- past the rise
    START = { [0] = { 14, 40 }, [1] = { 19, 32 } },
    -- (RECORDED: the rain's beats run 6-34 ticks after the head's death, 3
    -- after the last claw's; the last crystal lands 4 after its throw)
    RAIN_TICKS = 41,
    -- THE HAND STEER: an attack of mine on a claw that went out for damage
    -- in the 8 ticks before a step (^cox_olm_steer_ticks, index 4392 155/155)
    STEER = 8,
    -- the solo teleport: the move this many ticks after the tile's colour
    -- is first seen (cast + 2 -> cast + 8, cox_olm_portal_tick)
    PORTAL_MOVE = 6,
    -- a graphic's birth stamp matches across reads within this many cycles
    -- (CO.observe: a watched client's tick is 30 cycles give or take a frame)
    SPOT_JITTER = 10,
    -- (no food in the kit: Xeric's aid (+) is the healing, ~19 a dose)
    EAT_BELOW = 55, BREW_BELOW = 65,
    RANGE = { melee = 1, magic = 7, ranged = 7 },
    SPEED = { melee = 4, magic = 4, ranged = 5 },
}

-- THE DUO (docs/minigames/cox/olm_duo_solver.md; Synq's "The EASIEST Mage
-- Skipping Guide 2026 (Duo Olm)", sources/transcripts/yt_8Ta9VkVaJ7M.md).
-- West wall tiles, chamber-local; the east mirrors x -> 33 - x, z -> 72 - z.
CO.DUO = {
    -- PLAYER 2: "the far safe spot … never go past this tile. Going past this
    -- tile will cause the head to face the center": the melee claw's end, the
    -- last row of its own view (N, z 36..44) the trident's Long range (9)
    -- still reaches the mage claw (z 29..33) from -- z 42; beside the melee
    -- claw (x 12), so its special and its melee go out from there too
    P2 = { 12, 42 },
    -- THE SKIPPER: the mage hand's side ("left") for the basics' reads, the
    -- melee hand's ("right", out of the mage side's view) for the special's;
    -- each in the trident's reach of the mage claw
    LEFT = { 13, 35 }, RIGHT = { 13, 37 },
    -- the thumb: the skipper's melee-hand tile, and where the pair meet for
    -- the teleport ("the mage skipper usually sits at the thumb, so you guys
    -- can meet up for the portal swap")
    THUMB = { 12, 39 },
    -- Long range (both: "you absolutely need to make sure you are on the
    -- long-range attack style"): reach 9 for the trident and the crossbow,
    -- the crossbow at its own 6 ticks
    RANGE = { melee = 1, magic = 9, ranged = 9, spec = 1 },
    SPEED = { melee = 4, magic = 4, ranged = 6, spec = 6 },
    -- the hands' "low" in P3: "around 10 to 15%" of 900
    LOW_HP = 120,
    SPEC_COST = 500,
    -- a read off player 2's view: what it costs (the head would face the centre)
    HOME_COST = 40,
    -- the swap reads where the pair stand at the end of the cast + 9 (the
    -- pair swap at +10, OLM_SPEC 4 "teleport (party)")
    SWAP_READ = 9,
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
        { "shadow", "spotanim", "gargboss_debris_shadow_120" },
        -- the kit: We Do Raids' minimum (test/raids/cox_olm_solo.lua)
        { "pipe", "obj", "toxic_blowpipe_loaded" }, { "assembler", "obj", QD.COX.ava_name() },
        { "trident", "obj", "toxic_tots_charged" },
        { "occult", "obj", "occult_necklace" }, { "imbued_cape", "obj", "ma2_saradomin_cape" },
        { "defender", "obj", "dragon_parryingdagger" }, { "tentacle", "obj", "abyssal_tentacle" },
        { "fury", "obj", "enchanted_onyx_amulet" }, { "fire_cape", "obj", "tzhaar_cape_fire" },
        { "xbow", "obj", "xbows_crossbow_dragon" }, { "dhide_body", "obj", "black_dragonhide_body" },
        { "torso", "obj", "barbassault_penance_fighter_torso" }, { "vial_empty", "obj", "raids_vial_empty" },
        { "chaps", "obj", "black_dragonhide_chaps" }, { "bgs", "obj", "bgs" },
        { "style_slot_3", "component", "combat_interface:style_slot_3" },
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
        -- (a duo: the BGS's special on the melee hand, two-handed, in the melee set)
        spec = { ids.bgs, ids.fury, ids.fire_cape, ids.torso, ids.chaps },
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
    ids.brews = QD.COX.supply_ids("heal")
    ids.restores = QD.COX.supply_ids("restore")
    ids.stat_restores = ids.restores
    ids.overloads = QD.COX.supply_ids("overload")
    ids.enhances = QD.COX.supply_ids("prayer")
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

-- THE ROTATION (cox_olm_act; Molgoatkirby's counting guide): the steps
-- alternate, an auto on every odd one; the even ones walk a list of their
-- own -- null, crystal burst, null, lightning, null, teleport (+ null, heal
-- in the penultimate) -- that does NOT advance on a clenched even step (a
-- "clenched null"). The list's item i -> its kind (cox_olm_even_kind):
-- 1 null, 2 burst, 3 lightning, 4 teleport, 5 heal.
function CO.even_kind(i)
    if i % 2 == 0 then return 1 end
    return 2 + i // 2
end

function CO.list_len(o)
    return o.glowing and 8 or 6
end

-- Is an even step at tick t a clenched null? The clench (its 45 ticks from
-- the even step it came on) or the ghost clench's one tick.
function CO.clenched_at(o, t)
    return t < (o.clench_until or -1) or o.ghost == t
end

-- Can the left claw special at tick t (cox_olm_claw_special: up, not
-- clenched, not healing, and neither hand on its P3 regen bar)?
function CO.can_special(o, t)
    if not o.left_alive or o.final then return false end
    -- (the healing hand from the tick after its sign: the sign's own step fired it)
    if t < (o.clench_until or -1) or (t > (o.protect_at or -1) and t < (o.protect_until or -1)) then return false end
    if o.glowing and not o.right_alive then return false end
    return true
end

-- The heaviest resolved hit among a hand's launches over ticks [t0, t1]
-- (0 if none), and whether one there is still in the air (its damage not
-- shown yet).
-- (no_kill: leave out a killing blow -- cox_olm_prepare_hit: it does not clench)
function CO.hit_in(list, t0, t1, no_kill)
    assert(list, "cox_olm: hit_in without a launch list")
    local best = 0
    for _, h in ipairs(list) do
        if h.t >= t0 and h.t <= t1 and h.dmg and h.dmg > best and not (no_kill and h.kill) then best = h.dmg end
    end
    return best
end

function CO.hit_open(list, t0, t1)
    assert(list, "cox_olm: hit_open without a launch list")
    for _, h in ipairs(list) do
        if h.t >= t0 and h.t <= t1 and h.dmg == nil then return true end
    end
    return false
end

-- The solo teleport's hit for a move of d tiles (cox_olm_portal_damage).
function CO.portal_damage(d)
    if d <= 0 then return 0 end
    return 1 + (43 * d) // 10
end

-- Does a hand steer step A? An attack of mine on it that WENT OUT in the STEER
-- ticks before A for damage (cox_olm_steers: by the attack's tick,
-- ~cox_olm_hand_attacked; a 0 or a splash does not steer). A launch whose
-- hitsplat is still to come counts (the plan assumes it hits).
function CO.launched_in(list, t0, t1)
    assert(list, "cox_olm: launched_in without a launch list")
    for _, h in ipairs(list) do
        if h.dmg and h.dmg > 0 and h.t >= t0 and h.t <= t1 then return true end
    end
    return false
end

function CO.steers(list, A)
    local V = CO.V
    return CO.launched_in(list, A - V.STEER, A - 1) or CO.hit_open(list, A - V.STEER, A - 1)
end

-- Will the next even step clench (cox_olm_clench_check)? Only a special
-- judges, and only the window the null before it opened (window_from, nil
-- when no window is open). (P1/P2, the mage hand up.)
function CO.clench_due(o, t_now)
    if o.glowing or not o.right_alive or not o.left_alive then return false end
    if t_now < (o.clench_until or -1) then return false end
    if o.window_from == nil or CO.even_kind(o.list % CO.list_len(o)) < 2 then return false end
    return CO.hit_in(o.lhits, o.window_from, t_now, true) >= (o.clench_hit or CO.V.CLENCH_HIT)
end

-- One step: state { dir, skipped }, the step's kind, my bin (an index into
-- CO.V.BINS; nil = I am not in the chamber), the hand flags, whether the
-- left claw can special, and the partner's bin (a duo; nil solo or not in
-- the chamber). -> new dir, new skipped, autos at me, special fired, autos
-- at the partner (cox_olm_act, step for step: an empty view -- neither of
-- us in it -- is ALWAYS a skip: an auto owed, a special lost, the head to
-- the most players; a seen null or special with nothing owed re-targets,
-- and a special it turns away from still goes off; otherwise the owed auto
-- first, then the step's own, at everyone in view.)
function CO.step(dir, skipped, kind, bin, side, extra_left, extra_right, can_special, mate)
    local b = bin and CO.V.BINS[bin]
    local mb = mate and CO.V.BINS[mate]
    -- (a bin is wholly in or out of every view by construction)
    local mine = b ~= nil and CO.in_view(dir, b[1])
    local theirs = mb ~= nil and CO.in_view(dir, mb[1])
    local inview = mine or theirs
    local function most()
        local s = (b and b[3] or 0) + (mb and mb[3] or 0)
        local m = (b and b[4] or 0) + (mb and mb[4] or 0)
        local n = (b and b[5] or 0) + (mb and mb[5] or 0)
        return CO.most_of(s, m, n, side, extra_left, extra_right)
    end
    if not inview then
        if kind == 0 then skipped = true end
        return most(), skipped, 0, false, 0
    end
    if kind > 0 and not skipped then
        local m = most()
        if m ~= dir then return m, skipped, 0, kind >= 2 and can_special, 0 end
        if kind == 1 then return dir, skipped, 0, false, 0 end
    end
    local debt = skipped
    local at = (kind == 0) and 1 or (debt and 1 or 0)
    local fired = kind >= 2 and can_special
    if kind == 0 then fired = false end
    return dir, false, mine and at or 0, fired, theirs and at or 0
end

-- The kind of the next step the count has not played yet.
function CO.next_kind(S, F)
    local o = S.o
    if o.parity == 0 then return 0 end
    local A = o.first_act + 4 * o.next_k
    if CO.clench_due(o, F.tick) or CO.clenched_at(o, A) then return 1 end
    return CO.even_kind(o.list % CO.list_len(o))
end

-- THE STEPS AHEAD from the model's state: every step whose read (A - 1) is
-- after now and at most tmax, up to n: { A, kind, slot, can }. A step whose
-- read is already behind me (A - 1 <= now < A) is played with the tile I am
-- on, and the state after it is the search's start. The 4:1's slot is the
-- step's place in its four: 0 the auto before a null, 1 the null, 2 the auto
-- before a special, 3 the special (a clenched null counts as a null).
-- -> steps, dir, skipped (after the pending step)
function CO.steps_ahead(S, F, n, tmax)
    local o = S.o
    local out = {}
    local dir, skipped = o.dir, o.skipped
    if o.first_act == nil then return out, dir, skipped end
    local parity, list, len = o.parity, o.list, CO.list_len(o)
    local cl_until = o.clench_until or -1
    local due = CO.clench_due(o, F.tick)
    local k = o.next_k
    local raw = {}
    while #out < n do
        local A = o.first_act + 4 * k
        if A - 1 > tmax then break end
        local kind
        if parity == 1 then
            if due then cl_until, due = A + CO.V.CLENCH_TICKS, false end
            if A < cl_until or o.ghost == A then
                kind = 1
            else
                kind = CO.even_kind(list % len)
                list = (list + 1) % len
            end
        else
            kind = 0
        end
        parity = 1 - parity
        local can = CO.can_special(o, A) and A >= cl_until
        local st = { A = A, kind = kind, can = can }
        raw[#raw + 1] = st
        if A - 1 > F.tick then
            out[#out + 1] = st
        elseif A > F.tick then
            -- (the pending step: my read tile is fixed; its steer is what
            -- has landed since the last step)
            local lx, lz = CO.local_of(S, F.me.x, F.me.z)
            local bin = (lx >= CO.V.CH.x0 and lx <= CO.V.CH.x1) and CO.bin_of(lz) or nil
            dir, skipped = CO.step(dir, skipped, kind, bin, o.side or 0, CO.steers(o.lhits, A), CO.steers(o.rhits, A), can, CO.mate_at(o, A - 1))
        end
        k = k + 1
    end
    -- the slots: an auto's from the even step after it
    for i, st in ipairs(raw) do
        if st.kind == 0 then
            local nx = raw[i + 1]
            st.slot = (nx and nx.kind >= 2) and 2 or 0
        else
            st.slot = (st.kind >= 2) and 3 or 1
        end
    end
    -- (a last auto with no even step after it in the list: from the list)
    local last = raw[#raw]
    if last and last.kind == 0 then
        local E = last.A + 4
        local nk = (E < cl_until or o.ghost == E) and 1 or CO.even_kind(list % len)
        last.slot = (nk >= 2) and 2 or 0
    end
    return out, dir, skipped
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
    -- + WALL_TICKS, read against the end of the tick before: cox_olm_wall_tick), and the two rows
    if #o.fire > 0 then
        if o.fire_first == nil then
            o.fire_first = F.tick
            -- THE CONTENT CHECK: the walls stand the tick after the cast -- a
            -- step the head attacked on (its cast projectile flies 15 cycles,
            -- too short to be seen alike on both lanes: not used)
            local cast = false
            for _, e in ipairs(o.events) do
                if e.tick == F.tick - 1 and e.kind == "attack" then cast = true end
            end
            local on_step = o.first_act ~= nil and (F.tick - 1 - o.first_act) % 4 == 0
            CO.mech(S, F, "wall", cast and on_step, string.format("flames seen %d: no head attack on a step at %d",
                F.tick, F.tick - 1))
        end
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
    -- MY ATTACKS ON THE HANDS, by their LAUNCH (cox_olm_prepare_hit runs as
    -- the attack goes out: the steer and the clench's heaviest hit count
    -- from the launch tick, in the player phase after the head acted -- so
    -- toward the first step AFTER it; the trident's hitsplat lands 3-4 ticks
    -- later, the tentacle's one): the swing the monitor saw this tick, on
    -- the hand I aimed at; each hitsplat resolves the oldest open launch
    if F.my_swing and (o.aim_hand == "left" or o.aim_hand == "right") then
        local list = (o.aim_hand == "left") and o.lhits or o.rhits
        list[#list + 1] = { t = F.my_swing_stamp or F.tick }
        o.launches = (o.launches or 0) + 1
    end
    for _, h in ipairs({ { "left_row", "hit_left", "left", "lhits" }, { "right_row", "hit_right", "right", "rhits" } }) do
        local row = o[h[1]]
        local list = o[h[4]]
        -- (a new hitsplat: its start cycle changes to another live one -- a
        -- splat running out reads cycle 0, damage -1, and is not a hit)
        if row and row.hit_cycle and row.hit_cycle > 0 and (row.hit_damage or -1) >= 0
            and row.hit_cycle ~= o[h[2] .. "_cycle"] then
            o[h[2] .. "_cycle"] = row.hit_cycle
            local acc = o.acc[h[3]]
            acc.n = acc.n + 1
            local dmg = row.hit_damage or 0
            if dmg > 0 then acc.h = acc.h + 1 else o[h[2] .. "_splash"] = F.tick end
            local open = nil
            for _, e in ipairs(list) do
                if e.dmg == nil and e.t < F.tick then open = e break end
            end
            if open == nil then
                -- (a hitsplat with no launch seen: taken as a swing the tick before)
                open = { t = F.tick - 1 }
                list[#list + 1] = open
                o.orphans = (o.orphans or 0) + 1
                -- (a duo: the partner's -- the hand it is casting at steers too)
                if dmg > 0 then o.mate_hit[h[3]] = F.tick end
            end
            open.dmg = dmg
            open.land = F.tick
            open.kill = dmg > 0 and (row.health_scale or 0) > 0 and (row.health_ratio or 1) == 0
        end
        -- (a launch with no hitsplat in 8 ticks did nothing)
        for _, e in ipairs(list) do
            if e.dmg == nil and e.t < F.tick - 8 then e.dmg = 0 end
        end
        while #list > 0 and list[1].t < F.tick - 30 do table.remove(list, 1) end
    end
    if o.ox == nil then return end
    -- THE PARTNER (a duo): the other raider in the chamber, its tile at the
    -- end of this tick -- the head counts it as it counts me
    if o.role then
        local pr, players = api_drive.players()
        o.mate_bin = nil
        if pr == "ok" then
            for _, p in ipairs(players) do
                if not p.me then
                    local lx, lz = p.x - o.ox, p.z - o.oz
                    if lx >= V.CH.x0 and lx <= V.CH.x1 and lz >= V.CH.z0 and lz <= V.CH.z1 then
                        o.mate_x, o.mate_z, o.mate_bin = p.x, p.z, CO.bin_of(lz)
                    end
                end
            end
        end
        o.mate_hist[F.tick] = o.mate_bin or false
        o.mate_hist[F.tick - 40] = nil
    end
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
                -- (cox_olm_falling_tick: the first aim 1 or 2 ticks after
                -- the cry, then one every two ticks for eleven; each lands 2
                -- ticks after its aim: the last aim is cry + 22 at most. The
                -- marks below are every tick's, a superset of either start.)
                if string.find(m.text or "", "chosen you as its target", 1, true) then
                    o.target_until = F.tick + 23
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
                -- THE PAIR (a duo's teleport: "You have been paired with <name>!
                -- The magical power will enact soon..."; the swap at +10)
                if string.find(m.text or "", "You have been paired with", 1, true) then
                    o.pair_first = F.tick
                    o.pairs = (o.pairs or 0) + 1
                end
                -- the retreat: between phases (cox_olm_switch_side)
                if string.find(m.text or "", "retreats into the depths", 1, true) then
                    CO.retreat(S, F, "chat")
                end
            end
        end
        if #msgs > 0 then o.msg_serial = math.max(o.msg_serial or 0, msgs[1].serial) end
    end
    -- the autos' style: the head's projectiles, the last seen
    local pr, projs = api_drive.projectiles(0)
    o.spheres, o.auto_inflight, o.spits = {}, false, {}
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
    -- graphics: lightning bolts, the portal, crystal shadows (first sight;
    -- the portal's tile and first sight are remembered across the ticks its
    -- graphic is not sent)
    o.bolts, o.shadows = {}, {}
    -- A FRESH SEND, lane neutral, by the graphic's BIRTH STAMP: my tick in
    -- cycles plus its cycles_left (both lanes list a floor graphic 40 ticks
    -- from its apply) is the same number at every read of one graphic, and
    -- a later send on the tile is at least a tick's 30 on. Matched within
    -- SPOT_JITTER, not exactly: a watched client runs on the wall clock, so
    -- two reads a tick apart are 29 or 31 cycles apart, not 30. (The old
    -- rule -- fresh when cycles_left fell by less than 30 -- took every
    -- 29-cycle read of an OLD shadow for a new one and put its landing 3
    -- ticks on again, each time: on a watched Play the crystals landed while
    -- the plan saw them still in the air, and it stood under them.)
    local sr, spots = api_drive.spotanims(0)
    local born = o.spot_born
    local fresh_rows = {}
    if sr == "ok" then
        for _, sp in ipairs(spots) do
            if sp.cycles_left > 0 then
                local key = sp.spotanim_id .. ":" .. sp.x .. ":" .. sp.z
                local b = F.tick * 30 + sp.cycles_left
                local list = born[key]
                if list == nil then
                    list = {}
                    born[key] = list
                end
                local known = nil
                for _, e in ipairs(list) do
                    if math.abs(e.b - b) <= V.SPOT_JITTER then known = e end
                end
                if known then
                    known.last = F.tick
                else
                    list[#list + 1] = { b = b, last = F.tick }
                    -- (two sends on one tile in one tick are one graphic: one landing)
                    fresh_rows[key] = sp
                end
            end
        end
    end
    -- (a graphic no longer listed has expired: forget it)
    for key, list in pairs(born) do
        local keep = {}
        for _, e in ipairs(list) do
            if e.last == F.tick then keep[#keep + 1] = e end
        end
        born[key] = (#keep > 0) and keep or nil
    end
    for key, sp in pairs(fresh_rows) do
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
            -- (the tile's colour at cast + 2, 4, 6, 8, the move at + 8
            -- (RECORDED, cox_olm_portal_tick): a send more than 4 ticks
            -- after the last is a new portal)
            if o.portal_last == nil or F.tick - o.portal_last > 4 then
                o.portal_tile, o.portal_first = { x = sp.x, z = sp.z }, F.tick
            end
            o.portal_last = F.tick
        elseif sp.spotanim_id == ids.shadow then
            -- (by send: an earlier shadow on the tile may not have landed yet)
            o.shadow_seen[key .. "@" .. F.tick] = { x = sp.x, z = sp.z, land = F.tick + 3 }
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
        -- (cox_olm_rise_done: parity, list, the clench, the ghost, all reset;
        -- no clench window until the first null)
        o.parity, o.list, o.window_from = 0, 0, nil
        o.ghost, o.clench_at, o.protect_signed = nil, nil, false
        o.lhits, o.rhits = {}, {}
        o.final = false
        o.left_alive, o.right_alive = true, true
        o.clench_until = -1
        o.protect_until, o.protect_at = -1, nil
        o.between = nil
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
                o.dir, o.skipped = u.dir, u.skipped
                o.parity, o.list, o.window_from = u.parity, u.list, u.window_from
                o.autos_at_me, o.specials_at_me = u.autos, u.specials
                o.next_k = u.k
                S.mon.rules.olm_step.checked = S.mon.rules.olm_step.checked - 1
            end
            o.first_act = o.first_act + d
            o.undo = nil
        end
    end
end

-- OLM RETREATS (cox_olm_switch_side: both hands down before the glowing
-- phase): BETWEEN PHASES until the next rise. The chat names the tick; both
-- claws seen down is the same moment, whichever is read first.
function CO.retreat(S, F, cause)
    local o = S.o
    if o.between or o.glowing then return end
    o.between = { since = F.tick, rain_until = F.tick + CO.V.RAIN_TICKS }
    QD.raid._tob_trace(S, F.tick, string.format("olm retreats (%s): between phases, crystals to %d",
        cause, o.between.rain_until))
end

function CO.claw_event(S, F, what)
    local o = S.o
    local sign = what:match("sign:(%d)")
    if sign then
        o.signs[#o.signs + 1] = { tick = F.tick, kind = tonumber(sign) }
        if #o.signs > 32 then table.remove(o.signs, 1) end
        -- (the healing hand: 16 ticks, ^cox_olm_protect_ticks; its register
        -- stays set for the phase, which ends the ghost's animation)
        if tonumber(sign) == 5 then
            o.protect_at = F.tick
            o.protect_until = F.tick + CO.V.PROTECT_TICKS
            o.protect_signed = true
        end
        return
    end
    if what == "clench" then
        o.clench_at = F.tick
        o.clench_until = F.tick + CO.V.CLENCH_TICKS
    elseif what == "unclench" then
        o.unclench_at = F.tick
        if F.tick < (o.clench_until or -1) then o.clench_until = F.tick end
    elseif what == "left_down" then o.left_alive = false
    elseif what == "right_down" then
        o.right_alive = false
        -- THE GHOST CLENCH (cox_olm_hand_down): Olm polls the mage hand every
        -- two ticks, on the rise's parity; on the poll after it dies Olm
        -- counts as clenched for that one tick (an even step on it is a
        -- clenched null), and a clench running ends there
        local rc = (o.rise_tick or 0) + CO.V.RISE_DONE
        local g = F.tick + 1
        if (g - rc) % 2 ~= 0 then g = g + 1 end
        o.ghost = g
        if F.tick < (o.clench_until or -1) then o.clench_until = g end
        QD.raid._tob_trace(S, F.tick, string.format("olm mage hand down: ghost clench at %d%s", g,
            (o.first_act and (g - o.first_act) % 4 == 0) and " (a step)" or ""))
    elseif what == "left_up" and o.phase_n > 0 and F.tick > (o.rise_tick or 0) + 5 then o.left_alive = true
    elseif what == "right_up" and o.phase_n > 0 and F.tick > (o.rise_tick or 0) + 5 then o.right_alive = true end
    if not o.left_alive and not o.right_alive and o.glowing then o.final = true end
    if not o.left_alive and not o.right_alive and not o.glowing and o.phase_n > 0 then CO.retreat(S, F, "claws") end
end

-- The bin a chamber-local z is in.
-- The partner's bin at the end of tick t (nil: solo, or not in the chamber).
function CO.mate_at(o, t)
    if not o.role then return nil end
    local b = o.mate_hist[t]
    if b == nil then return o.mate_bin end
    return b or nil
end

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
    local V = CO.V
    local len = CO.list_len(o)
    while true do
        local A = o.first_act + 4 * o.next_k
        if A > F.tick then break end
        o.undo = { k = o.next_k, A = A, dir = o.dir, skipped = o.skipped, parity = o.parity, list = o.list,
                   window_from = o.window_from, autos = o.autos_at_me, specials = o.specials_at_me }
        -- THE STEP'S KIND: an auto on the odd steps; on an even one a special
        -- first judges the clench window its null opened (the heaviest hit on
        -- the melee hand since that null), then a clenched null leaves the
        -- list where it is, and a null that plays opens the next window
        local kind, clenched = 0, false
        if o.parity == 1 then
            local seen_clench = (o.clench_at == A)
            local was = (not seen_clench) and A < (o.clench_until or -1)
            local able = (not o.glowing) and o.right_alive and o.left_alive and not was
            local t0 = o.window_from
            if t0 ~= nil and CO.even_kind(o.list % len) >= 2 then
                -- THE CONTENT CHECK: a clench exactly when a hit of 1/20 went
                -- out in the window (judged once every launch in it has shown
                -- its damage)
                local function judge(F2)
                    local hit = CO.hit_in(o.lhits, t0, A - 1, true)
                    CO.mech(S, F2, "clench", (able and hit >= (o.clench_hit or V.CLENCH_HIT)) == seen_clench, string.format(
                        "special step %d: heaviest hit %d since the null at %d, clench %s", A, hit, t0,
                        tostring(seen_clench)))
                end
                if CO.hit_open(o.lhits, t0, A - 1) then o.defer[#o.defer + 1] = { due = A + 6, run = judge }
                else judge(F) end
            else
                CO.mech(S, F, "clench", not seen_clench, string.format(
                    "even step %d is no null-to-special window, clench %s", A, tostring(seen_clench)))
            end
            o.window_from = nil
            if CO.clenched_at(o, A) then
                kind, clenched = 1, true
            else
                kind = CO.even_kind(o.list % len)
                o.list = (o.list + 1) % len
                if kind == 1 then o.window_from = A end
            end
        end
        o.parity = 1 - o.parity
        local me = QD.T2.tile_at(S, A - 1) or F.me
        local _, lz = CO.local_of(S, me.x, me.z)
        local lx = me.x - o.ox
        local bin = (lx >= CO.V.CH.x0 and lx <= CO.V.CH.x1) and CO.bin_of(lz) or nil
        local can = CO.can_special(o, A)
        local el, er = CO.steers(o.lhits, A), CO.steers(o.rhits, A)
        -- (the final stand's head down: the step after its death acts on nothing)
        local dying = o.final and (o.head_row == nil or ((o.head_row.health_scale or 0) > 0 and (o.head_row.health_ratio or 1) == 0))
        local mate = CO.mate_at(o, A - 1)
        local dir, skipped, autos, fired, mate_autos = CO.step(o.dir, o.skipped, kind, bin, o.side or 0, el, er, can, mate)
        -- what was seen at A: a turn names the new facing
        local seen, attacked = nil, false
        for _, e in ipairs(o.events) do
            if e.tick == A then
                seen = e
                if e.kind == "attack" then attacked = true end
            end
        end
        local sign = nil
        for _, sg in ipairs(o.signs) do
            if sg.tick == A then sign = sg.kind end
        end
        -- THE CONTENT CHECK: the special the list and the view say fires, and
        -- only it (the even list, the clenched null, the ghost clench)
        if o.left_alive and not o.final and not dying then
            local want = fired and kind or nil
            CO.mech(S, F, "special", want == sign, string.format("k%d at %d kind %d%s bin %s dir %d: model %s, sign %s",
                o.next_k, A, kind, clenched and " (clenched null)" or "", tostring(bin), o.dir,
                tostring(want), tostring(sign)))
        end
        -- THE STEP CHECK (olm_step): the facing the model turns to against
        -- the one seen; judged once every launch steering it has shown its
        -- damage (a trident cast 1-3 ticks before the step is still in the
        -- air). The live state takes the seen facing.
        local rec = { A = A, k = o.next_k, kind = kind, bin = bin, dir = o.dir, skipped = o.skipped, can = can,
                      side = o.side or 0, got = (seen and seen.kind == "turn") and seen.dir or o.dir, mate = mate }
        if dying then
            -- (no check: the head is down)
        elseif CO.hit_open(o.lhits, A - V.STEER, A - 1) or CO.hit_open(o.rhits, A - V.STEER, A - 1) then
            o.defer[#o.defer + 1] = { due = A + 6, run = function(F2) CO.judge_step(S, F2, rec) end }
        else
            CO.judge_step(S, F, rec)
        end
        dir = rec.got
        -- THE CONTENT CHECK: the head attacks exactly when the model's step
        -- owes me (or the partner) an auto (the caught-up one included)
        CO.mech(S, F, "auto", dying or ((autos + mate_autos > 0) == attacked), string.format("k%d at %d kind %d bin %s mate %s dir %d owed %s: model %d+%d autos, attack anim %s",
            o.next_k, A, kind, tostring(bin), tostring(mate), o.dir, tostring(o.skipped), autos, mate_autos, tostring(attacked)))
        o.mate_autos = (o.mate_autos or 0) + mate_autos
        -- THE TELLS, per state: autos owed and fired at me, specials due and
        -- signed, clenched nulls, and HEAD RUNS (a step the head turned on
        -- without attacking: nothing fired)
        if o.m and o.m.state then
            local tl = o.spec_tally[o.m.state] or { steps = 0, autos = 0, autos_hit = 0, due = 0, fired = 0, clenched = 0, runs = 0 }
            o.spec_tally[o.m.state] = tl
            tl.steps = tl.steps + 1
            if kind == 0 then tl.autos = tl.autos + 1 end
            tl.autos_hit = tl.autos_hit + autos
            if clenched then tl.clenched = tl.clenched + 1 end
            if kind >= 2 and not o.final then
                tl.due = tl.due + 1
                if sign then tl.fired = tl.fired + 1 end
            end
            if seen and seen.kind == "turn" and not attacked and sign == nil then tl.runs = tl.runs + 1 end
        end
        o.dir, o.skipped = dir, skipped
        o.autos_at_me = o.autos_at_me + autos
        if fired then o.specials_at_me = o.specials_at_me + 1 end
        -- (the step's 4:1 slot, as CO.steps_ahead names it)
        if kind == 0 then
            local nk = CO.clenched_at(o, A + 4) and 1 or CO.even_kind(o.list % len)
            o.last_slot = (nk >= 2) and 2 or 0
        else
            o.last_slot = (kind >= 2) and 3 or 1
        end
        o.next_k = o.next_k + 1
    end
end

-- One step judged: the model's turn, from the launches' resolved damage,
-- against the facing seen.
function CO.judge_step(S, F, r)
    local o = S.o
    local V = CO.V
    local el = CO.launched_in(o.lhits, r.A - V.STEER, r.A - 1)
    local er = CO.launched_in(o.rhits, r.A - V.STEER, r.A - 1)
    local dir = CO.step(r.dir, r.skipped, r.kind, r.bin, r.side, el, er, r.can, r.mate)
    S.mon.rules.olm_step.checked = S.mon.rules.olm_step.checked + 1
    if dir ~= r.got then
        QD.T2.monitor.note(S, "olm_step", r.A, string.format("k%d kind %d bin %s mate %s dir %d%s steer %s/%s: model %d, seen %d",
            r.k, r.kind, tostring(r.bin), tostring(r.mate), r.dir, r.skipped and " owed" or "", tostring(el), tostring(er), dir, r.got))
    end
end

-- ================================================================ THE SEARCH
--
-- The next DEPTH actions with a read tick inside the horizon; every bin
-- sequence; the best by hp. -> { reads = { {t, bin}, ... }, score }
function CO.search(S, F, target, B, canon)
    local o, V = S.o, CO.V
    if o.first_act == nil or o.side == nil then return nil end
    local steps, dir0, skipped0 = CO.steps_ahead(S, F, 4, F.tick + S.t2.plan_opts.h)
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
    -- (a duo: the claw the partner has hit in the last 8 ticks)
    local mate_steer = nil
    if o.role then
        if o.mate_hit.right and F.tick - o.mate_hit.right <= 8 then mate_steer = "right"
        elseif o.mate_hit.left and F.tick - o.mate_hit.left <= 8 then mate_steer = "left" end
    end
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
                local d, sk
                if type(canon) == "function" then d, sk = canon(steps[#steps].slot)
                else d, sk = CO.canon(o.side, steps[#steps].slot) end
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
                local base_l = (i == 1) and CO.steers(o.lhits, st.A) or false
                local base_r = (i == 1) and CO.steers(o.rhits, st.A) or false
                -- (a duo: the hand the partner is casting at steers every step)
                if mate_steer == "left" then base_l = true elseif mate_steer == "right" then base_r = true end
                local e, path, likely = 0, nil, nil
                for _, out in ipairs({ { true, p_any }, { false, 1 - p_any } }) do
                    local w = out[2]
                    if w > 0 then
                        local el = base_l or (out[1] and steer == "left")
                        local er = base_r or (out[1] and steer == "right")
                        local d2, s2, autos, fired, mautos = CO.step(dir, skipped, st.kind, b, o.side, el, er, st.can, o.mate_bin)
                        -- (a duo prices the partner's autos as mine: the
                        -- team's hitpoints)
                        local sc = (autos + mautos) * V.AUTO_COST + (fired and (V.SPECIAL_COST[st.kind] or 10) or 0)
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
            local d2, s2 = CO.step(dir, skipped, st.kind, b, o.side, false, false, st.can, o.mate_bin)
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
    local score, path = walk(1, dir0, skipped0, CO.bin_of(my_lz) or 3, F.tick, ready0 or F.tick + 1)
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
    -- the checks waiting on a launch's damage
    if #o.defer > 0 then
        local keep = {}
        for _, d in ipairs(o.defer) do
            if F.tick >= d.due then d.run(F) else keep[#keep + 1] = d end
        end
        o.defer = keep
    end
    -- THE GHOST CLENCH (cox_olm_hand_down): the melee hand's unclench plays
    -- on the poll after the mage hand dies (not under the P3 healing hand)
    if o.ghost == F.tick and o.left_alive and not o.protect_signed then
        CO.mech(S, F, "ghost", o.unclench_at == F.tick, string.format("ghost clench at %d: unclench seen %s",
            F.tick, tostring(o.unclench_at)))
    end
    -- the clench's own end: 45 ticks from the even step it came on
    if o.clench_at and F.tick == o.clench_at + CO.V.CLENCH_TICKS and o.left_alive and o.ghost == nil then
        CO.mech(S, F, "unclench", o.unclench_at == F.tick, string.format("clench at %d: unclench seen %s",
            o.clench_at, tostring(o.unclench_at)))
    end
    -- the solo portal: the move at first sight + 6 reads my tile at the end
    -- of + 5; off it, a teleport onto it and 1 + 4.3 a tile the tick after
    -- (cox_olm_portal_damage; I may run up to 2 tiles in the tick after it)
    if o.portal_first and F.tick == o.portal_first + CO.V.PORTAL_MOVE + 1 and o.portal_tile then
        local at = QD.T2.tile_at(S, o.portal_first + CO.V.PORTAL_MOVE - 1)
        if at then
            local px, pz = o.portal_tile.x, o.portal_tile.z
            local d = math.max(math.abs(at.x - px), math.abs(at.z - pz))
            local want = CO.portal_damage(d)
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
        -- (solo: every hitsplat on Olm is mine; a duo's are not)
        if not o.role and o.dealt > 0 and F.tick - o.dealt_last >= 2 and o.points_judged ~= o.dealt then
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
    -- THE PAIR (a duo): the swap reads where the two of us stand at the end
    -- of the pairing + 9 and hits each for the distance between: on one tile,
    -- nothing. Both meet on the thumb ("the mage skipper usually sits at the
    -- thumb, so you guys can meet up for the portal swap"); player 2 leaves
    -- its tile late ("wait for one basic attack before grouping up").
    if o.role and o.pair_first and o.side ~= nil then
        local t = o.pair_first + CO.DUO.SWAP_READ
        if t >= now + 1 then
            local th = CO.DUO.THUMB
            local mx, mz = CO.abs_of(S, CO.duo_lx(o.side, th[1]), CO.duo_lz(o.side, th[2]))
            B.zone({ name = "pair", x = mx, z = mz, size = 1, lo = 0, hi = 0, t0 = t, t1 = t,
                     tier = "lethal", require = true })
        end
    end
    -- the portal: be on it at the end of first-sight + 5 (1 + 4.3 a tile otherwise)
    if o.portal_tile and o.portal_first and not o.role then
        local t = o.portal_first + CO.V.PORTAL_MOVE - 1
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
            -- (every ring to the chamber's far corner: no cap -- RSProx 3282
            -- dealt 56 for 13 tiles)
            for d = 1, 20 do
                local c = CO.portal_damage(d)
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
            -- (60 on it, 15 less a ring, nothing past the 7x7 -- the content's
            -- ~cox_olm_bomb_damage; each nested box adds its ring's 15)
            for d = 3, 0, -1 do
                B.hit({ name = "bomb", x0 = bm.x - d, z0 = bm.z - d, x1 = bm.x + d, z1 = bm.z + d, t0 = t, t1 = t,
                        dmg_exp = 15, dmg_max = 15, forbid = (d <= 2) })
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
    if o.fire_first and o.fire_z1 and o.fire_z1 - o.fire_z0 == 2 and o.fire_first + V.WALL_TICKS - 1 >= now + 1 then
        local x0, x1 = nil, nil
        for _, fw in ipairs(o.fire) do
            x0 = (x0 == nil or fw.x < x0) and fw.x or x0
            x1 = (x1 == nil or fw.x > x1) and fw.x or x1
        end
        B.hit({ name = "burn", x0 = x0, z0 = o.fire_z0 + 1, x1 = x1, z1 = o.fire_z0 + 1,
                t0 = o.fire_first + V.WALL_TICKS - 1, t1 = o.fire_first + V.WALL_TICKS - 1, dmg_exp = 57, dmg_max = 65, forbid = true })
    end
    for _, fw in ipairs(o.fire) do
        B.hit({ name = "flame", x = fw.x, z = fw.z, t0 = now + 1, t1 = now + h, sample = "pass", dmg_exp = 50, dmg_max = 65,
                forbid = true })
    end
    -- (the wall before its flames: cast at a step C, the leap judged on where
    -- I ENDED C -- the dodge is the step tick's own run, CO.wall_risk; the
    -- flames stand and block from C + 1, the terms above)
    -- the falling crystals on me: one aimed every other tick A (marked every
    -- tick: the cadence's start is 1 or 2 after the cry) at the tile I ended
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
--   wait          nothing to hit: before the first rise, or a hand's row
--                 not in view yet
--   melee         P1/P2, the melee hand up and not clenched: the tentacle
--                 4:1 (CO.cycle: its own sub-machine, cycle / recover:<cause>)
--   mage          P1/P2, the melee hand clenched (or dead), the mage hand
--                 up: the trident, HEAD RUNNING (CO.headrun: 3:0)
--   between       BETWEEN PHASES: from the retreat (both hands down before
--                 the glowing phase, CO.retreat) to the next rise. Nothing
--                 to hit and no head to read: no reads, no swings; the
--                 crystal rain (RAIN_TICKS, every shadow a forbidden 3x3 at
--                 its landing, CO.predict) is the whole job, on the way to
--                 the start tile of the wall he rises on next
--   p3-low-melee  the penultimate (head glowing): the melee hand to LOW_HP
--   p3-low-mage   ... then the mage hand to LOW_HP ("leave both hands
--                 around 75": the first one down regenerates in REGEN ticks)
--   p3-kill-melee the melee hand dead first (its infinity heal: hold off it
--                 while the sign shows, on the mage hand if it is high)
--   p3-kill-mage  the mage hand dead inside the regen
--                 (the p3 melee states 4:1, the p3 mage states head-run)
--   head          the final stand: the crossbow on the head (the search)
function CO.machine(S, F)
    if S.o.role then return CO.machine_duo(S, F) end
    local o, V = S.o, CO.V
    local m = o.m
    local state, tgt = "wait", nil
    local L = o.left_alive and o.left_row or nil
    local R = o.right_alive and o.right_row or nil
    local melee_t = L and { row = L, style = "melee", hand = "left" } or nil
    local mage_t = R and { row = R, style = "magic", hand = "right" } or nil
    if o.ox == nil then
        state = "wait"
    elseif o.between then
        state = "between"
    elseif o.final then
        state, tgt = "head", o.head_row and { row = o.head_row, style = "ranged" } or nil
    elseif o.first_act == nil then
        state = "wait"
    elseif not o.glowing then
        -- THE MELEE HAND FIRST, by the 4:1; the mage hand only while the melee
        -- hand is dead or CLENCHED (invulnerable: its 45 ticks go to head
        -- running the mage hand), back to the 4:1 MELEE_LEAD ticks before
        -- it can be hit again
        -- (and from the tick the coming even step is KNOWN to clench -- a hit
        -- of 1/20 since the last one, its hitsplat up -- when it is 3 ticks
        -- off or less: from the ring finger the mid skip is 7 tiles, and the
        -- clench's own anim leaves 3 ticks to the next read; ol1..48: 293 of
        -- the mage state's 305 autos were that first read)
        local clenched = L ~= nil and F.tick < (o.clench_until or -1) - V.MELEE_LEAD
        if L ~= nil and not clenched and o.first_act and CO.clench_due(o, F.tick) then
            local E = o.first_act + 4 * (o.next_k + ((o.parity == 1) and 0 or 1))
            if E - F.tick <= 3 then clenched = true end
        end
        if melee_t and not (clenched and mage_t) then state, tgt = "melee", melee_t
        elseif mage_t then state, tgt = "mage", mage_t
        else state = "wait" end
    else
        -- (a hand never hit shows no health bar: unknown is FULL -- ol1i
        -- killed the melee hand with the mage hand's health unread and at 585,
        -- and the regen undid it)
        local lh, rh = CO.hp_of(L) or o.hand_hp, CO.hp_of(R) or o.hand_hp
        local healing = (o.protect_until or -1) >= F.tick
        if L and R then
            if lh and lh > o.low_hp and not healing then state, tgt = "p3-low-melee", melee_t
            elseif rh and rh > o.low_hp then state, tgt = "p3-low-mage", mage_t
            elseif healing then state, tgt = "p3-low-mage", mage_t
            else state, tgt = "p3-kill-melee", melee_t end
        elseif R then
            state, tgt = "p3-kill-mage", mage_t
        elseif L then
            -- (the mage hand went first: the melee hand inside its regen)
            state, tgt = "p3-kill-melee", (not healing) and melee_t or nil
        else
            state = "wait"
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

-- ================================================================ THE DUO
--
-- Synq's duo (CO.DUO): both raiders on the MAGE hand first -- the skipper
-- skipping every special, player 2 at the far end -- the melee hand after it,
-- the penultimate phase's hands in the solo order, the head last. Each seat
-- runs this with its role; the head counts both (CO.step's partner).

-- A west-wall tile on this wall.
function CO.duo_lx(side, x) return (side == 0) and x or (33 - x) end
function CO.duo_lz(side, z) return (side == 0) and z or (72 - z) end

-- May the BGS's special go out (in the pack or in hand, the energy for it)?
function CO.duo_spec_ok(S, F)
    local id = S.ids.bgs
    if id == nil or (F.energy or 0) < CO.DUO.SPEC_COST then return false end
    return QD.raid._tob_count(S, id) > 0 or QD.raid._tob_worn(S, id)
end

function CO.machine_duo(S, F)
    local o = S.o
    local m = o.m
    local state, tgt = "wait", nil
    local L = o.left_alive and o.left_row or nil
    local R = o.right_alive and o.right_row or nil
    local melee_t = L and { row = L, style = "melee", hand = "left" } or nil
    local mage_t = R and { row = R, style = "magic", hand = "right" } or nil
    local spec_t = L and { row = L, style = "spec", hand = "left" } or nil
    local spec_ok = L ~= nil and CO.duo_spec_ok(S, F)
    -- (the special: once a phase as it opens -- "the first thing you'll be
    -- doing when you enter the room is specking the hand" -- until it fires
    -- or SPEC_GIVE ticks go by)
    if o.spec_phase ~= o.phase_n and m.state == "spec" and F.tick - m.since > 20 then o.spec_phase = o.phase_n end
    if o.ox == nil then
        state = "wait"
    elseif o.between then
        state = "between"
    elseif o.final then
        state, tgt = "head", o.head_row and { row = o.head_row, style = "ranged" } or nil
    elseif not o.glowing then
        if spec_ok and o.spec_phase ~= o.phase_n then state, tgt = "spec", spec_t
        elseif o.first_act == nil then state = "wait"
        elseif mage_t then state, tgt = "mage", mage_t
        elseif melee_t then state, tgt = "melee", melee_t
        else state = "wait" end
    else
        -- THE PENULTIMATE (Synq): "spec as many times as you can and stay on
        -- the melee hand. Your goal is to get the hand down to around 10 to
        -- 15%" -- then the mage hand low -- "once both hands are low enough
        -- HP, I recommend killing the melee hand first. This is because if
        -- you kill the mage hand first, you run the risk of the melee hand
        -- healing"
        local lh, rh = CO.hp_of(L) or o.hand_hp, CO.hp_of(R) or o.hand_hp
        local healing = (o.protect_until or -1) >= F.tick
        if L and R then
            if lh > o.low_hp and not healing then
                if spec_ok then state, tgt = "p3-spec", spec_t else state, tgt = "p3-low-melee", melee_t end
            elseif rh > o.low_hp or healing then state, tgt = "p3-low-mage", mage_t
            else state, tgt = "p3-kill-melee", melee_t end
        elseif R then
            state, tgt = "p3-kill-mage", mage_t
        elseif L then
            state, tgt = "p3-kill-melee", (not healing) and melee_t or nil
        end
    end
    if m.state ~= state then
        QD.raid._tob_trace(S, F.tick, string.format("olm %s state %s -> %s (t+%d; hp %d, hands L %s R %s, partner bin %s)",
            o.role, m.state, state, F.tick - m.since, F.hp or -1,
            tostring(L and math.floor(CO.hp_of(L) or -1)), tostring(R and math.floor(CO.hp_of(R) or -1)), tostring(o.mate_bin)))
        m.state, m.since = state, F.tick
    end
    return state, tgt
end

-- SYNQ'S CYCLE, the model's state after each step of it (dir, an auto owed),
-- by the step's slot (0 the auto before a null, 1 the null, 2 the auto
-- before a special, 3 the special). G the mage hand's view (S on the west
-- wall), H the melee hand's, where player 2 stands:
--   special   nobody in G -> skipped; the head turns to H (both of us there)
--   basic 1   at player 2 in H                 ("each person tanks a basic")
--   empty     the mage side (me + the steer) outnumbers player 2 -> G
--   basic 2   at me in G                       ("always tank basic two")
function CO.skip_canon(side, slot)
    local G = (side == 0) and 0 or 2
    local H = 2 - G
    if slot == 3 or slot == 0 then return H, false end
    return G, false
end

-- The skipper on the mage hand: CYCLE on Synq's tiles while every step lands
-- in the cycle's state; RECOVER (the step search with both raiders, scored for
-- ending in it) the moment one does not -- an early run, a missed steer, a
-- teleport -- as the guide does it: "pay attention to the next special that
-- happens. After that, count the basic attacks. Your goal is to tank basic
-- two and run the head to set it right back up."
function CO.mageskip(S, F, B, target)
    local o, D = S.o, CO.DUO
    local c = o.mskip
    if c == nil or c.phase ~= o.phase_n or c.entry ~= o.m.since then
        c = { state = "recover", cause = "enter", since = F.tick, k = o.next_k - 1, phase = o.phase_n,
              n = (c and c.n) or 0, entry = o.m.since }
        o.mskip = c
    end
    local last_k = o.next_k - 1
    if last_k ~= c.k and last_k >= 0 then
        c.k = last_k
        local d, sk = CO.skip_canon(o.side, o.last_slot or 0)
        local ok = (o.dir == d and o.skipped == sk)
        local was = c.state
        if c.state == "cycle" and not ok then
            c.state, c.cause, c.since = "recover", "turn", F.tick
        elseif c.state == "recover" and ok then
            c.state, c.since = "cycle", F.tick
            c.n = c.n + 1
        end
        if c.state ~= was then
            QD.raid._tob_trace(S, F.tick, string.format("olm mage skip %s -> %s (%s) at k%d slot %d: dir %d owed %s, partner bin %s",
                was, c.state, c.cause, last_k, o.last_slot or 0, o.dir, tostring(o.skipped), tostring(o.mate_bin)))
        end
    end
    if c.state == "recover" then
        return CO.search(S, F, target, B, function(slot) return CO.skip_canon(o.side, slot) end), "recover-" .. c.cause
    end
    local reads = {}
    for _, st in ipairs(CO.steps_ahead(S, F, 8, F.tick + B.h)) do
        local at = (st.slot == 3) and D.RIGHT or D.LEFT
        local lx, lz = CO.duo_lx(o.side, at[1]), CO.duo_lz(o.side, at[2])
        reads[#reads + 1] = { t = st.A - 1, bin = CO.bin_of(lz), kind = st.kind, slot = st.slot, lz = lz }
        local x, z = CO.abs_of(S, lx, lz)
        B.pull({ name = "skip", x = x, z = z, size = 1, weight = CO.V.CYCLE_PULL, t0 = st.A - 1, t1 = st.A - 1 })
    end
    return { reads = reads, score = 0 }, "cycle"
end

-- One tile to stand on and every read in its bin (player 2's home; the
-- skipper's thumb on the melee hand): a pull to the tile, the reads required.
function CO.duo_hold(S, F, B, at, cost, name)
    local o = S.o
    local lx, lz = CO.duo_lx(o.side, at[1]), CO.duo_lz(o.side, at[2])
    local x, z = CO.abs_of(S, lx, lz)
    B.pull({ name = name, x = x, z = z, size = 1, weight = CO.V.CYCLE_PULL, t0 = F.tick + 1, t1 = F.tick + B.h })
    if cost == nil then return nil end
    local reads = {}
    for _, st in ipairs(CO.steps_ahead(S, F, 8, F.tick + B.h)) do
        reads[#reads + 1] = { t = st.A - 1, bin = CO.bin_of(lz), kind = st.kind, cost = cost }
    end
    return { reads = reads, score = 0 }
end

-- THE DUO'S READS by role and state.
function CO.duo_reads(S, F, B, mode, target)
    local o, D = S.o, CO.DUO
    if mode == "between" or o.side == nil then return nil, "between" end
    if o.role == "p2" then
        -- "attack the mage hand and make sure you never go past this tile";
        -- "for the head phase … attack the head and make sure you stay in
        -- your area no matter what"
        return CO.duo_hold(S, F, B, D.P2, D.HOME_COST, "home"), "home"
    end
    if mode == "spec" or mode == "p3-spec" or mode == "melee" or mode == "p3-low-melee" or mode == "p3-kill-melee" then
        return CO.duo_hold(S, F, B, D.THUMB, nil, "thumb"), "thumb"
    end
    if (mode == "mage" or mode == "p3-low-mage" or mode == "p3-kill-mage") and target then
        return CO.mageskip(S, F, B, target)
    end
    -- the head: "turn the head so each player tanks one basic attack" -- the
    -- step search with both of us in the count
    return CO.search(S, F, target, B), "search"
end

-- THE 4:1 (the melee hand), the guides' loop (sources/olm_4to1_research.md
-- 1.4; tiles from the Kaoz / ThePerfectG tile packs), chamber-local on the
-- west wall, by the slot of the step a read is for (k mod 4: 0 auto, 1 null,
-- 2 auto, 3 special). Swings one tick after each step: hit 2 at the ring
-- finger (z 42), hit 3 there, hit 4 at the thumb (39), touch the mid skip
-- (35) for the second auto's read, hit 1 at the thumb, back to the ring
-- finger for the special's read.
--   The NULL's read is the guides' tile 3 (z 40) once hit 3 is out (landed
--   with damage, or still in the air): tile 3 is in both the middle and the north views, and the damaged
--   melee hand's steer counts the north once more, so the head stays; from
--   there the thumb is one tile, hit 4 goes out on it a tick after the null,
--   and the mid skip is two running ticks off for the second auto's read.
--   On a 0 (or before hit 3 shows) it is the ring finger (z 42, north only):
--   at tile 3 the head would "noodle" middle and the second auto then sees
--   the mid skip; from the ring finger hit 4 cannot reach the mid skip in
--   time (an attack in reach swings where I stand), so that loop is a 3:1.
-- The east wall mirrors it: z -> 72 - z.
CO.CYCLE_LZ = { [3] = 42, [0] = 42, [1] = 42, [2] = 35 }
-- (the null's read once hit 3 is out)
CO.CYCLE_TILE3_LZ = 40

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
    -- (a new loop on every entry to a melee state: a phase, the clench's end)
    if c == nil or c.phase ~= o.phase_n or c.entry ~= o.m.since then
        c = { state = "recover", cause = "enter", since = F.tick, k = o.next_k - 1, phase = o.phase_n,
              autos = o.autos_at_me, specials = o.specials_at_me, n = (c and c.n) or 0, entry = o.m.since }
        o.cyc = c
    end
    local last_k = o.next_k - 1
    if last_k ~= c.k and last_k >= 0 then
        c.k = last_k
        local slot = o.last_slot or 0
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
    local reads = {}
    local steps = CO.steps_ahead(S, F, 8, F.tick + B.h)
    for _, st in ipairs(steps) do
        local A = st.A
        do
            local slot = st.slot
            local lz = CO.cycle_lz(o.side, slot)
            -- (hit 3 in the air counts, as in the step model: its splat can show
            -- after the click that must reach tile 3 in time -- ol1 t1190)
            if slot == 1 and CO.steers(o.lhits, A) then
                lz = (o.side == 0) and CO.CYCLE_TILE3_LZ or (72 - CO.CYCLE_TILE3_LZ)
            end
            -- (the null's read, once: on the last tick a click still reaches it)
            if slot == 1 and F.tick == A - 2 then
                local _, my_lz = CO.local_of(S, F.me.x, F.me.z)
                local near = {}
                for _, h in ipairs(o.lhits) do
                    if h.t >= A - 8 then near[#near + 1] = tostring(h.t) .. ":" .. tostring(h.dmg) end
                end
                QD.raid._tob_trace(S, F.tick, string.format("olm 4:1 null A%d read lz %d (hit 3 %s), at lz %d; launches %s",
                    A, lz, CO.steers(o.lhits, A) and "out" or "none", my_lz, table.concat(near, " ")))
            end
            -- THE FLAME SKIP (guides: "after hit 2, step one more tile toward
            -- the wall, so Olm cannot fit a wall when it autos you"): a wall
            -- needs a row either side inside z 27..44 (cox_olm_fire_wall), so
            -- the auto's read on z 43 (west, still beside the claw) or 28
            -- (east) leaves it nowhere to go
            if slot == 0 and (o.power == "flame" or o.glowing) then
                lz = (o.side == 0) and 43 or 28
            end
            reads[#reads + 1] = { t = A - 1, bin = CO.bin_of(lz), kind = st.kind, slot = slot, lz = lz }
            local x, z = CO.abs_of(S, target.col, lz)
            B.pull({ name = "cycle", x = x, z = z, size = 1, weight = V.CYCLE_PULL, t0 = A - 1, t1 = A - 1 })
        end
    end
    return { reads = reads, score = 0 }, "cycle"
end

-- HEAD RUNNING, the mage hand (Synq 2:18:34, "4-tick mage running";
-- OLM_OSRS_RULES 4.3): unseen at EVERY read, so every step is a skip and a
-- turn and Olm never attacks. The read's tile is named by the facing it
-- finds (H the melee hand's view: north on the west wall; G the mage hand's;
-- M the middle), so that the turn it makes sends the head away from the
-- NEXT read's tile (cox_olm_act: nobody in view -> skip, turn to the most):
--   H -> the mid skip (z 35: S + M). The cast since the last step steers it
--        (R6: the damaged hand's side counts one more) -> G; a splash -> M.
--   G -> between (z 38: M + N) -> M (the middle wins the tie).
--   M -> the ring finger (z 42: N only) -> H (the west wall's N/S tie: N).
-- Three casts a 12-tick loop, one on each step tick: from the ring finger
-- dragged two tiles into reach, at the mid skip, between. The east wall
-- mirrors z -> 72 - z (its H is the south). A splash reads as M at the
-- next step and the table goes on from there: no hit taken.
-- (by the facing, the out-of-view tiles in preference: the second is the
-- one in reach when the first is not -- after a splash turned the head to
-- the middle from the mid skip, the ring finger is 7 tiles off and three
-- ticks away; the mage side's long line, z 30, is 5, and turns it to G)
CO.RUN_LZ = { H = { 35, 30 }, G = { 38, 42 }, M = { 42, 30 }, H_SPLASH = { 30, 35 } }

function CO.run_cands(side, dir, splash)
    local H = (side == 0) and 2 or 0
    local key = (dir == H) and (splash and "H_SPLASH" or "H") or ((dir == 1) and "M" or "G")
    local out = {}
    for i, z in ipairs(CO.RUN_LZ[key]) do out[i] = (side == 0) and z or (72 - z) end
    return out
end

function CO.headrun(S, F, B, target)
    local o, V = S.o, CO.V
    if o.first_act == nil or o.side == nil then return nil end
    local steps, dir, skipped = CO.steps_ahead(S, F, 4, F.tick + B.h)
    local reads, windows = {}, {}
    local _, from_z = CO.local_of(S, F.me.x, F.me.z)
    local t_from = F.tick
    for i, st in ipairs(steps) do
        -- the first window's cast is known once its hitsplat is up: a splash
        -- since the last step and no damage is the turn to the middle
        local er = true
        if i == 1 then
            local hit = CO.steers(o.rhits, st.A)
            local splash = (o.hit_right_splash or -1) >= st.A - 3
            if splash and not hit then er = false end
        end
        local cands = CO.run_cands(o.side, dir, not er)
        local reach = 2 * ((st.A - 1) - t_from)
        local lz = cands[1]
        for _, z in ipairs(cands) do
            if math.abs(z - from_z) <= reach then lz = z break end
        end
        local bin = CO.bin_of(lz)
        reads[#reads + 1] = { t = st.A - 1, bin = bin, kind = st.kind, lz = lz, cost = V.RUN_REQ_COST }
        -- (a REQUIRED zone over the bin's rows: the plan's late-hazard
        -- lookahead keeps a run that pays off only at the read -- a beam
        -- ranked on the cost so far cast instead and fell a tile short, ol1
        -- t138; the portal's lesson)
        local b = V.BINS[bin]
        local zc, zh = (b[1] + b[2]) // 2, (b[2] - b[1]) // 2
        local zx, zz = CO.abs_of(S, target.col, zc)
        B.zone({ name = "headrun", x = zx, z = zz, size = 1, lo = 0, hi = zh, t0 = st.A - 1, t1 = st.A - 1,
                 require = true, tier = "damage", cost = V.RUN_REQ_COST })
        local x, z = CO.abs_of(S, target.col, lz)
        B.pull({ name = "headrun", x = x, z = z, size = 1, weight = V.CYCLE_PULL, t0 = st.A - 1, t1 = st.A - 1 })
        -- THE CAST WINDOW before this read: a cast stands the tick it goes
        -- out (out of reach, it drags me two tiles toward the hand first:
        -- the ring finger's), so it fits where the run to the read still
        -- does after it; else only once I am there
        local t_read = st.A - 1
        local d = math.abs(lz - from_z)
        local need = (d + 1) // 2
        local toward = (o.side == 0 and lz < from_z) or (o.side == 1 and lz > from_z)
        local far = target.lz and math.max(0, target.lz - from_z, from_z - (target.lz + 4)) > target.range
        local w0, w1
        if 1 + need <= t_read - t_from then
            w0, w1 = t_from + 1, t_read
        elseif toward and far and 1 + (math.max(0, d - 2) + 1) // 2 <= t_read - t_from then
            w0, w1 = t_from + 1, t_from + 1
        else
            w0, w1 = t_from + need + 1, t_read
        end
        if w0 <= w1 then windows[#windows + 1] = { t0 = w0, t1 = w1 } end
        dir, skipped = CO.step(dir, skipped, st.kind, bin, o.side, false, er, st.can, o.mate_bin)
        from_z, t_from = lz, t_read
    end
    return { reads = reads, score = 0, windows = windows }
end

-- THE FIRE WALL'S DODGE, before it is cast (OLM_OSRS_RULES 6, "click one
-- tick before"): the walls judge where I END the step tick a seen auto lands
-- on (cox_olm_fire_wall: they stand the tick after, the leap on z +- 1, the
-- trap on z), so in a flame phase -- and P3 and the final stand, which have
-- every power -- a seen auto on a tile a wall fits (both its rows in the
-- head's view, z 29..42) prices staying within a tile of the read tile on
-- the step tick. The 4:1's flame skip (z 43 west, 28 east) fits none.
function CO.wall_risk(S, F, B, res)
    local o, V = S.o, CO.V
    if o.first_act == nil or o.side == nil then return end
    if not (o.power == "flame" or o.glowing) then return end
    local function fits(dir, lz)
        return lz > 28 and lz < 43 and CO.in_view(dir, lz + 1) and CO.in_view(dir, lz - 1)
    end
    local function bin_fits(dir, bin, lz)
        if lz then return fits(dir, lz) end
        local b = V.BINS[bin]
        for z = b[1], b[2] do
            if fits(dir, z) then return true end
        end
        return false
    end
    local function mark(A)
        if #B.spec.marks < QD.T2.V.MARKS_MAX then
            B.mark({ name = "wallrisk", t_mark = A - 1, r = 1, t0 = A, t1 = A, dmg_exp = V.WALL_RISK, dmg_max = 10 })
        end
    end
    -- the pending step: its read is the tile I am on now
    local A = o.first_act + 4 * o.next_k
    if A - 1 == F.tick then
        local lx, lz = CO.local_of(S, F.me.x, F.me.z)
        local bin = (lx >= V.CH.x0 and lx <= V.CH.x1) and CO.bin_of(lz) or nil
        if bin then
            local _, _, autos = CO.step(o.dir, o.skipped, CO.next_kind(S, F), bin, o.side, false, false, false, o.mate_bin)
            if autos > 0 and fits(o.dir, lz) then mark(A) end
        end
    end
    if res == nil then return end
    local steps, d, sk = CO.steps_ahead(S, F, 4, F.tick + B.h)
    for i, st in ipairs(steps) do
        local r = res.reads[i]
        if r == nil or r.t ~= st.A - 1 or r.bin == nil then break end
        local d2, s2, autos = CO.step(d, sk, st.kind, r.bin, o.side, false, false, st.can, o.mate_bin)
        if autos > 0 and bin_fits(d, r.bin, r.lz) then mark(st.A) end
        d, sk = d2, s2
    end
end

function CO.gear(S, style)
    local out = {}
    for _, obj in ipairs(S.ids.sets[style]) do
        if not QD.raid._tob_worn(S, obj) and QD.raid._tob_count(S, obj) > 0 then
            out[#out + 1] = { obj = obj, two_hand = (obj == S.ids.bgs) }
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
            local kind = CO.next_kind(S, F)
            local lx, lz = CO.local_of(S, F.me.x, F.me.z)
            local bins = { (lx >= CO.V.CH.x0 and lx <= CO.V.CH.x1) and CO.bin_of(lz) or nil }
            if res and res.reads and res.reads[1] and res.reads[1].t == A - 1 then bins[2] = res.reads[1].bin end
            local el = CO.steers(o.lhits, A)
            local er = CO.steers(o.rhits, A)
            for _, b in pairs(bins) do
                local _, _, autos = CO.step(o.dir, o.skipped, kind, b, o.side or 0, el, er, false, o.mate_bin)
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
--   overload (+)       five minutes: boosts every combat stat the tick
--                      after the sip and each 25 ticks after, and at + 501
--                      ends and gives back the 50 it cost (five hits of 10
--                      from + 1 as it goes down). Drunk again THAT tick --
--                      overloaded the whole fight; a sip while it runs is
--                      refused -- at 85+ hp (the supplies top up to it), no auto in flight
--   revitalisation (+) a fighting stat under its base (Xeric's aid takes 9%
--                      + 2 a sip, deep burn 2 a hit; the overload's re-boost
--                      only ADDS its boost, min(level + d, base + d)): at once
--   prayer enhance (+) a point a prayer tick for 458 ticks: under 85 prayer
--   stamina            under 30% run energy (the 4:1 runs 7 tiles in 4 ticks)
-- A drink is known by its dose count falling (my own inventory).
function CO.doses(S, list)
    local n = 0
    for i, obj in ipairs(list) do n = n + ((i - 1) % 4 + 1) * QD.raid._tob_count(S, obj) end
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
    -- no overload running to lift a drain back (Tekton, a room before the
    -- kit's overloads): the shared rule, 20 under (QD.COX.restore_due) -- a
    -- restore whenever a stat was under base drank one per sip
    if o.ovl_until == nil or o.ovl_until < F.tick then
        return (QD.COX.restore_due(F, CO.fight_stats(o.fight_style)))
    end
    local deep = false
    for _, st in ipairs(CO.fight_stats(o.fight_style)) do
        local lv = F.levels and F.levels[st]
        if lv and lv.level < lv.base - 5 then deep = true end
    end
    if deep then return true end
    if not CO.drained(S, F) then return false end
    if o.ovl_until == nil or o.ovl_until < F.tick then return true end
    local drunk = o.ovl_until - 500   -- the first boost, the tick after the sip
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
    for _, d in ipairs({ { "overloads", "ovl_until", 501 }, { "enhances", "penh_until", 458 },
                         { "staminas", "stam_until", 200 } }) do
        local n = CO.doses(S, ids[d[1]])
        local key = d[1] .. "_n"
        if o[key] ~= nil and n < o[key] then
            o[d[2]] = F.tick + d[3]
            -- THE OVERLOAD'S COST (cox_potion.rs2 overload_start_t3 /
            -- cox_overload_damage): five hits of 10 two ticks apart, the
            -- first the tick after the sip -- the tick after its dose is seen
            -- gone (ol1k before the delay: seen 70, hits 70..78; now 71..79);
            -- the 50 comes back as it ends (+501)
            if d[1] == "overloads" then
                o.ovl_hits = {}
                for j = 0, 4 do o.ovl_hits[#o.ovl_hits + 1] = F.tick + 1 + 2 * j end
            end
        end
        o[key] = n
    end
    if intents.drink or intents.eat or F.tick - (S.last_drink or -10) < 3 then return end
    local pick = nil
    -- (again the tick it ends, not before: a sip while it runs is refused
    -- (cox_potion.rs2 ~overload_still_active); at 90+ it waited forever --
    -- ol1r drank twice, and all of P3 went without)
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
    -- (the hand a swing seen next tick went at: observe's launches)
    o.aim_hand = tgt and tgt.hand or nil
    local intents = { mode = mode, eat_below = V.EAT_BELOW, brew_below = V.BREW_BELOW }
    if o.ox == nil then
        return intents
    end
    -- THE TARGET: worn gear first (never a target in a style whose weapon is
    -- not worn now: a wield and an attack in one tick swing the old weapon)
    local target = nil
    if tgt and tgt.row then
        intents.gear = CO.gear(S, tgt.style)
        local weapon = (tgt.style == "melee") and ids.tentacle or ((tgt.style == "magic") and ids.trident
            or ((tgt.style == "spec") and ids.bgs or ids.xbow))
        local lx, lz = CO.local_of(S, tgt.row.x, tgt.row.z)
        -- (melee: the column beside the 5-wide claw, toward the chamber)
        local col = (o.side == 0) and (lx + 5) or (lx - 1)
        local acc = o.acc[tgt.hand or "head"]
        local RG, SP = V.RANGE, V.SPEED
        if o.role then RG, SP = CO.DUO.RANGE, CO.DUO.SPEED end
        target = { lz = lz, col = col, range = RG[tgt.style], speed = SP[tgt.style], hand = tgt.hand,
                   p_hit = (acc.h + 2) / (acc.n + 5) }
        if tgt.style == "spec" then
            -- THE SPECIAL (a duo, Synq): the BGS on the melee hand; fired when
            -- the energy drops by its cost
            o.spec_e0 = o.spec_e0 or F.energy
            if (F.energy or 0) <= (o.spec_e0 or 0) - CO.DUO.SPEC_COST + 10 then
                o.spec_phase, o.spec_e0 = o.phase_n, nil
                o.specs = (o.specs or 0) + 1
                QD.raid._tob_trace(S, F.tick, "olm special fired on the melee hand, energy " .. tostring(F.energy))
            elseif QD.raid._tob_worn(S, weapon) then
                intents.spec, intents.spec_cost = true, CO.DUO.SPEC_COST
                B.target({ name = mode, npc = tgt.row, size = 5, range = 1, speed = target.speed, value = V.SWING_VALUE })
            end
        else
            o.spec_e0 = nil
        end
        if tgt.style == "spec" then
            -- (above)
        elseif QD.raid._tob_worn(S, weapon) and tgt.style == "melee" and o.first_act and not o.role then
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
                -- (not while it is clenched: a hit then is a 0 and a message)
                if sw > F.tick and sw >= (o.clench_until or -1) then
                    B.target({ name = mode .. "@" .. sw, npc = tgt.row, size = 5, range = target.range,
                               speed = target.speed, value = V.SWING_VALUE, t_ok0 = sw, t_ok1 = sw })
                    n = n + 1
                end
                k = k + 1
            end
        elseif QD.raid._tob_worn(S, weapon) and tgt.style == "magic" and o.first_act and not o.role then
            -- (head running: the casts only in the windows the reads leave,
            -- CO.headrun)
            o.run_res = CO.headrun(S, F, B, target)
            for _, w in ipairs((o.run_res and o.run_res.windows) or {}) do
                if w.t1 > F.tick and #B.spec.targets < 5 then
                    B.target({ name = mode .. "@" .. w.t0, npc = tgt.row, size = 5, range = target.range,
                               speed = target.speed, value = V.SWING_VALUE, t_ok0 = math.max(w.t0, F.tick + 1), t_ok1 = w.t1 })
                end
            end
        elseif QD.raid._tob_worn(S, weapon) then
            B.target({ name = mode, npc = tgt.row, size = 5, range = target.range, speed = target.speed,
                       value = V.SWING_VALUE })
        end
        if F.prayer >= 30 then
            intents.pray_boost = ids.boost[(tgt.style == "spec") and "melee" or tgt.style]
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
    -- THE READS: the 4:1 machine on the melee hand, head running on the
    -- mage hand, the step search for the head -> the read ticks' bins
    local res
    if o.role then
        local sub
        res, sub = CO.duo_reads(S, F, B, mode, target)
        intents.mode = mode .. ":" .. tostring(sub)
    elseif target and tgt.style == "melee" and QD.raid._tob_worn(S, ids.tentacle) then
        local sub
        res, sub = CO.cycle(S, F, B, target)
        intents.mode = mode .. ":" .. sub
    elseif target and tgt.style == "magic" then
        -- (the mage hand: head running, from the gear switch on -- the
        -- reads do not wait for the trident)
        res = o.run_res or CO.headrun(S, F, B, target)
        o.run_res = nil
        intents.mode = mode .. ":headrun"
    elseif mode ~= "between" then
        res = CO.search(S, F, target, B)
    end
    o.plan = res
    -- (between phases the head does not act: no step to read, no wall cast)
    if mode ~= "between" then CO.wall_risk(S, F, B, res) end
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
            CO.require_bin(S, B, r.t, r.bin, r.cost or V.REQ_COST, "read" .. i)
        end
    end
    -- in the chamber, out of the hazards' way when there is nothing to hit;
    -- between phases, toward the start tile of the wall he rises on next (a
    -- pull, not a zone: every crystal's forbid outranks it)
    if mode == "between" and o.side ~= nil then
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
        S.who = "cox_olm_solve"
        S.t2.plan_opts = { h = opts.h or CO.V.H, beam = opts.beam or CO.V.BEAM }
        S.o = {
            seen = {}, seen_n = 0, events = {}, signs = {}, phase_n = 0, glowing = false,
            dir = 1, skipped = false, next_k = 0, side = nil,
            left_alive = false, right_alive = false, final = false,
            m = { state = "init", since = 0 }, mech = {}, spec_tally = {},
            dealt = 0, dealt_seen = {}, dealt_last = -10,
            parity = 0, list = 0, lhits = {}, rhits = {}, defer = {},
            role = opts.role, mate_hist = {}, mate_hit = {},
            -- the hands' hitpoints and the clench's hit scale with the party
            -- (cox_olm_hand_hp: 600 + 300 a raider past the first; 1/20 clenches)
            hand_hp = 600 + 300 * ((opts.party or 1) - 1),
            clench_hit = (600 + 300 * ((opts.party or 1) - 1)) // 20,
            -- (P3's "low": Synq's "around 10 to 15%" in a duo; solo the 4:1's 40)
            low_hp = opts.role and CO.DUO.LOW_HP or CO.V.LOW_HP,
            autos_at_me = 0, specials_at_me = 0,
            seed_first = {}, bomb_first = {}, bolt_col = {}, shadow_seen = {}, spot_born = {},
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
            -- (a duo: Long range, mode 3 -- 2 once a swing clamps it)
            if (S.o.role == nil and mode == 1) or (S.o.role ~= nil and mode >= 2) then break end
            assert(press <= 3, S.who .. ": three style presses and varp43_com_mode reads " .. tostring(mode))
            QD.raid._tob_tab(S, ids.combat_tab)
            local pr, pd = api_drive.if_click((S.o.role ~= nil) and ids.style_slot_3 or ids.style_slot_1, 1)
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
        -- THE TELLS per state: steps; autos skipped / owed; specials skipped /
        -- due; head runs (turned, nothing fired) / steps; clenched nulls
        local tally = {}
        for st, sp in pairs(o.spec_tally) do
            tally[#tally + 1] = string.format("%s: steps %d autos skipped %d/%d specials skipped %d/%d runs %d clenched %d",
                st, sp.steps, sp.autos - sp.autos_hit, sp.autos, sp.due - sp.fired, sp.due, sp.runs, sp.clenched)
        end
        table.sort(tally)
        local c = o.cyc
        return string.format("olm: points %s (dealt %d), phases %d, state %s, autos at me %d, specials at me %d, step mismatches %d/%d, "
            .. "mechanic mismatches {%s}, 4:1 entries %d, stats bad %d/%d ticks, launches %d (hitsplats with none %d), tells {%s}",
            tostring(o.points), o.dealt, o.phase_n, o.m.state, o.autos_at_me, o.specials_at_me, S.mon.rules.olm_step.n,
            S.mon.rules.olm_step.checked, table.concat(mech, ", "), c and c.n or 0,
            o.stat_bad or 0, S.mon.rules.olm_stats.checked, o.launches or 0, o.orphans or 0, table.concat(tally, "; "))
            .. (o.role and string.format("; duo %s: skip cycles %d, specs %d, autos at the partner %d, pairings %d",
                o.role, (o.mskip and o.mskip.n) or 0, o.specs or 0, o.mate_autos or 0, o.pairs or 0) or "")
    end,
})
