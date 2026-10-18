-- quest-driver / raid_solve_verzik_p3: VERZIK PHASE 3, SOLVED FROM SCRATCH.
--
--   t.raid.verzik_p3_solve(opts) -> result, detail, record
--
-- THE SHAPE.  Each server tick: MEASURE builds facts that carry ticks; the
-- CLOCK keeps her rotation as a schedule (which special is next and when);
-- the CONTEXT that schedule names hands the planner a goal and two to four
-- constraints; the PLANNER (api_drive.plan: collision_plan in C, served by
-- the client and by scriptrun over the same collision map) runs a beam search
-- over (tile, tick) for twelve ticks, stay or one or two steps a tick, with
-- every chasing tornado walked along each path; EMIT sends one click, the
-- prayer and the backpack first.  Nothing waits in a state.  Floor helpers
-- (gap, beside, the crab blasts) are the P2 solver's.
-- docs/minigames/theater_of_blood/solver_lessons.md is the checklist.
--
-- THE SPEC, from the content (OSRS-Content .../minigame_tob/scripts/
-- tob_verzik.rs2 "Phase 3 - the hybrid", configs/tob.constant) and the tick
-- log (every figure below was read off a scriptrun run before it was relied
-- on).  Tick mapping: an npc decision on tick T reads where players stood at
-- the END of T-1; my click after seeing tick d moves me during d+1, so a
-- plan's step k is my tile at the end of tick d+k.
--
--   her body       7x7.  She FOLLOWS her tank one step a tick while the tank
--                  is 2+ from her footprint; beside or under her she stands
--                  and faces the tank.  Webs and yellows suspend the follow.
--   the clock      a fixed rotation from the tick F her P3 form appears:
--                  a slot at F+6, then every 7 (5 once enraged); four autos,
--                  then a special, in the order crabs, webs, yellows, ball,
--                  repeating.  Crabs ride an auto (next slot one cadence on);
--                  the webs slot T resumes at T+42 whenever the spin is seen;
--                  yellows at Y blast on the end-of-Y+13 positions and resume
--                  at Y+21; the ball at B resumes at B+12.  The enrage at E
--                  pulls a pending slot to E+5.  Measured (sa): crabs 655 =
--                  F+34, webs 690 -> 732, yellows 760 -> 781, ball 809 ->
--                  821, the enrage at 845 pulled 849 to 850.
--   autos          one style rolled, thrown at EVERY raider: magic (seq 8124,
--                  projectile 1594) or ranged (8125, 1593); up to 33 (34
--                  enraged), halved by the matching overhead read when it
--                  LANDS (+3).  -> switch on her animation or the projectile.
--   melee          at a slot, if her TANK is at npc_range 1 AFTER her follow
--                  step on that tick: 50% a melee, up to 63 on everyone at
--                  range 1, unprayable.  A tank 2 away at the end of T-1 is 1
--                  away after her step.  -> at the end of every slot-1,
--                  her TANK is under her (gap 0) or 3+ away and the rest
--                  swing on; with the tank not known yet, everyone does.
--   crabs          one per raider; walks at it and dies on contact or at 25
--                  ticks; the death blasts npc_range <= 3 of its 2x2 (63 / 26
--                  / 8 by band), reading the end of D+2 (the P2 model).
--   webs           she walks to the centre (corner = centre - 3,3),
--                  invulnerable; on arrival anyone within 3 of her middle is
--                  thrown 4 tiles and the spin (8127) starts; from spin+3 one
--                  throw a tick, 24 of them: each raider in turn, its tile and
--                  two more 1-3 away; projectile 1601 lands +4, the web (npc
--                  8376) explodes +12 for 0-40 on whoever stands on it, and
--                  LEAVING a web tile sticks you 10 ticks, the middle tile of
--                  a run too.  -> never on a web tile from a tick before it
--                  lands to its explosion, middle tiles included.
--   yellows        she charges 14 ticks, invulnerable; one pool (map graphic
--                  1595) per living raider, scattered 3+ from her and from
--                  each other; at the blast a raider ALONE on a pool is
--                  spared, everyone else takes up to 80.  -> the same
--                  assignment on every raider's view; mine at the end of
--                  Y+13.
--   ball           rides the ranged pose; projectile 1598 at a random raider,
--                  lands seen + floor(cycles/30).  Exactly ONE unvisited
--                  raider within 1 of the target's tile -> it hops there
--                  (projectile from the target, +6, plus one tick when the
--                  new target's pid is below the sender's); two or more ->
--                  explodes on all; none -> the target takes 75% of their
--                  Hitpoints level.  With three raiders the third landing
--                  dissipates it.  -> target on the anchor, the next raider
--                  (lowest unvisited pid) within 1 of it, the third 3+ away,
--                  from a tick before the landing to a tick after.
--   enrage         at 20%, latched: slots every 5, 34 max, one tornado per
--                  raider rising 4-8 from it and walking a tile a tick at
--                  it, through walls.  On tick T it touches if it stands on
--                  its raider's end-of-T-1 tile (half current hitpoints, and
--                  she heals three times that); else it steps sign(dx),
--                  sign(dz) toward that tile.  Its raider's tornado respawns
--                  16 ticks after a touch.  -> the planner walks every
--                  tornado that may be mine along each candidate path and
--                  never ends a tick on the tile one reaches.
--
-- WHAT REAL TEAMS DO (Blert, 20 Normal trio rooms): P3 median 134 ticks with
-- scythes; webs at +75 in all 20, yellows in 6, the ball in 2; 76 damage a
-- raider in 8 hits, 3 eats, 3 drinks; the right overhead on 83% of autos by
-- three ticks after; the tank beside her most ticks, stepping off for her
-- attacks; 24 swings a web special.  This solver runs in the wiki's learner
-- kit (void, an abyssal tentacle) so that P3 lasts long enough to show every
-- special; survival is the brief, speed is not.
-- ==========================================================================

QD.VZP3 = {
    SIZE = 7,
    CADENCE = 7, CADENCE_ENRAGED = 5,
    FIRST_AFTER_FORM = 6,       -- her first slot, from the tick the P3 form appears
    ATTACKS_PER_SPECIAL = 4,
    ROTATION = { "crabs", "webs", "yellows", "ball" },
    AFTER_BALL = 12,            -- the throw to the next slot
    YELLOW_CHARGE = 14, AFTER_YELLOWS = 21,    -- Y to the blast, Y to the next slot
    WEB_RESUME = 42,            -- the webs slot to the next slot
    WEB_LIFE = 12, WEB_FLIGHT = 4, WEB_KNOCK = 3,  -- WEB_FLIGHT: a web thrown 8-9 tiles; see web_flight
    BALL_FLIGHT = 8, BALL_HOP = 6,  -- C: 242 (start 62 + 180, RSProx) and 185 cycles over 30, the queue delays
    ENRAGE_PULL = 5,
    CENTRE = { x = 32, z = 24 },  -- her middle for the webs, local to the room's origin (Blert: her SW at 29,21; with ^tob_verzik_centre_lz 24)
    -- the P3 floor, local to the room's origin (the pools' scatter round her
    -- centre tile is 13 x 9; the spectator cage sits north of it, local z
    -- 37): a raider off it is out of the fight
    ARENA = { x0 = 18, x1 = 46, z0 = 14, z1 = 34 },
    EDGE = 3, EDGE_WEIGHT = 0.3,
    H = 12, BEAM = 32, MOVE_COST = 0.01,
    KITE_FAR = 4, KITE_SWING_BONUS = 25,   -- the enrage kite: a distance worth no more, a due swing's weight
    KITE_DEPTH = 7, KITE_BEAM = 16, KITE_THREATS = 3,        -- its lookahead in ticks and the paths kept each tick
    -- the web run: the stack's first tile (the south middle of her ring,
    -- QD.raid._vzp3_ring), and how long a tile stepped to must stay clear
    WEB_RUN_START = 4, WEB_RUN_HOLD = 4,
    -- her throws (^tob_verzik_p3_web_first_throw, _throws), and how many
    -- tick-ends a tile is mine before the web thrown at it lands under me
    WEB_FIRST_THROW = 3, WEB_THROWS = 24, WEB_STAY = 4,
    TANK_TRUST = 3,             -- Hard Mode: her face names the re-picked tank from the special + this
    -- and the ticks before her resume it hands back: the planner steps off
    -- for the resume's scan (held to it, the stack took her melee, xv30 sa
    -- t1337: 26, 50, 46)
    WEB_RUN_LEAVE = 3,
    -- my swings, read off my own animation so the kite's weapon clock counts
    -- the planner's swings too (all.seq: 1658 the whip's and the tentacle's)
    SWING_SEQS = { [1658] = true },
    -- THE WEAPON'S TICKS between swings (owner, 2026-10-12: "It should also
    -- handle 5 tick weapons. The solver should be configurable for weapon
    -- speed"): opts.weapon_ticks, else the first of these worn (the obj's
    -- `attackrate` param, skill_combat/combat.rs2), else DEFAULT_TICKS
    WEAPON_TICKS = { abyssal_tentacle = 4, abyssal_whip = 4, ghrazi_rapier = 4, blade_of_saeldor = 4,
                     abyssal_bludgeon = 4, inquisitors_mace = 4, dragon_claws = 4, dragon_hunter_lance = 4,
                     scythe_of_vitur = 5, osmumtens_fang = 5, soulreaper_axe = 5, dragon_warhammer = 6 },
    DEFAULT_TICKS = 4,
    GOAL_PULL = 1.0, OFF_SIDE = 0.2, UNDER = 0.4,
    POOL_PULL = 1.5, BALL_PULL = 1.5, NEXT_PULL = 0.4,
    CRAB_BLAST = 3, CRAB_SIZE = 2,
    HP_EAT = 60, HP_BREW = 45, HP_TOP = 90, PRAYER_SIP = 25, COMBAT_REDOSE = 5,
    STAT_DRAIN = 24,           -- a super restore for Attack this far below base: two brews' worth
    PIETY_FLOOR = 40,          -- Piety only with points to spare (the overheads come first)
    TORNADO_RING = { 2, 10 },  -- a spawn this far from a raider (end of T-1, seen a tick later) may be its
    OWNER_MISSES = 2,          -- steps not heading for a raider before it is ruled out as a tornado's owner
    TRACE_CAP = 400, RECENT = 400,
}

-- A web's landing, in ticks from its first sight (the throw tick): the content
-- sends it for 30 x floor(d / 2) cycles, d her centre to the web's tile
-- (RSProx, 539 webs), so floor(d / 2) ticks, at least one.
function QD.VZP3.web_flight(p)
    if p.src_x == nil or p.dst_x == nil then return QD.VZP3.WEB_FLIGHT end
    local d = math.max(math.abs(p.src_x - p.dst_x), math.abs(p.src_z - p.dst_z))
    return math.max(1, d // 2)
end

local function cheb(ax, az, bx, bz) return QD.raid._vzp2_cheb(ax, az, bx, bz) end
function QD.raid._vzp3_trace(S, d, text)
    if #S.trace < QD.VZP3.TRACE_CAP then S.trace[#S.trace + 1] = "t" .. d .. " " .. text end
end

function QD.raid._vzp3_ids(weapon)
    local function sym(kind, name)
        if kind == "npc" then name = QD.raid.tob_npc_name(name) end
        local r, id = api_drive.symbol(kind, name)
        assert(r == "ok", "verzik_p3_solve: no " .. kind .. " named " .. name)
        return id
    end
    local function comp(name)
        local r, id = api_drive.component(name)
        assert(r == "ok", "verzik_p3_solve: no component " .. name)
        return id
    end
    local _, inv_tab = api_drive.tab_by_name("inventory")
    local _, prayer_tab = api_drive.tab_by_name("prayer")
    local function doses(stem)
        local out = {}
        -- (then the plain doses the ToB supply chest sells, enum_1952)
        for _, pre in ipairs({ "br_", "" }) do
            for n = 1, 4 do
                local r, id = api_drive.symbol("obj", pre .. n .. stem)
                if r == "ok" then out[#out + 1] = id end
            end
        end
        return out
    end
    return {
        p3 = sym("npc", "verzik_phase3"),
        transition = sym("npc", "verzik_phase2_to3_transition"),
        bat = sym("npc", "verzik_death_bat"),
        web = sym("npc", "verzik_web_npc"),
        tornado = sym("npc", "tob_verzik_creeper"),
        -- (QD.raid._vzp2_crabs reads a crab's death off its animation too)
        death_seqs = { [sym("seq", "top_spider_melee_death")] = true, [sym("seq", "top_spider_ranged_death")] = true,
                       [sym("seq", "top_spider_magic_death")] = true },
        crabs = { [sym("npc", "verzik_nylocas_melee")] = true, [sym("npc", "verzik_nylocas_ranged")] = true,
                  [sym("npc", "verzik_nylocas_magic")] = true },
        seq_melee = sym("seq", "verzik_phase3_attack_melee"),
        seq_magic = sym("seq", "verzik_phase3_attack_magic"),
        seq_ranged = sym("seq", "verzik_phase3_attack_ranged"),
        seq_summon = sym("seq", "verzik_phase3_attack_summon"),
        seq_webspin = sym("seq", "verzik_phase3_attack_webspin"),
        seq_powerblast = sym("seq", "verzik_phase3_attack_powerblast"),
        proj_ranged = sym("spotanim", "verzik_phase3_rangeproj"),
        proj_magic = sym("spotanim", "verzik_phase3_mageproj"),
        pool = sym("spotanim", "verzik_powerblast_safezone"),
        ball = sym("spotanim", "verzik_acidbomb_projanim"),
        web_proj = sym("spotanim", "verzik_p3_web_proj"),
        inv = sym("inv", "inv"), worn = sym("inv", "worn"),
        hitpoints = sym("stat", "hitpoints"), prayer = sym("stat", "prayer"), attack = sym("stat", "attack"),
        -- the weapon P1 found in my hand (QD.raid._vzp1_weapon_from_hand)
        weapon = QD.raid.vz_weapon or sym("obj", weapon or "scythe_of_vitur"),
        food = sym("obj", "anglerfish"),
        -- (and what the ToB supply chest sells, enum_1952: the relay's seats
        -- reach Verzik on the chests' fish)
        foods = { sym("obj", "anglerfish"), sym("obj", "mantaray"), sym("obj", "seaturtle"), sym("obj", "shark") },
        restores = QD.raid._vzp3_restores(doses("dose2restore")),
        stat_restores = doses("dose2restore"),
        combats = doses("dose2combat"),
        brews = doses("dosepotionofsaradomin"),
        backpack = comp("inventory:items"),
        missiles = comp("prayerbook:prayer14"), magic = comp("prayerbook:prayer13"),
        piety = comp("prayerbook:prayer27"),
        missiles_lit = sym("varbit", "varb4117_prayer_protectfrommissiles"),
        magic_lit = sym("varbit", "varb4116_prayer_protectfrommagic"),
        piety_lit = sym("varbit", "varb4129_prayer_piety"),
        inv_tab = inv_tab, prayer_tab = prayer_tab,
    }
end

-- ==================================================================== MEASURE

-- ====================================================================== CLOCK
--
-- Her rotation as a schedule: S.next_slot is the tick of her next slot,
-- S.attacks_left how many autos come before the next special, S.special the
-- rotation index of that special.  Animations confirm and resync it (an auto
-- at an unexpected tick moves the slot), the specials' resumes are the
-- content's constants from the slot they ran at.
function QD.raid._vzp3_clock(S, F)
    local V, b, ids = QD.VZP3, F.boss, S.ids
    if b and S.entered == nil then
        S.entered = F.tick
        S.next_slot = F.tick + V.FIRST_AFTER_FORM
        S.attacks_left = V.ATTACKS_PER_SPECIAL
        S.special = 1
        QD.raid._vzp3_trace(S, F.tick, "her P3 form at " .. b.x .. "," .. b.z)
    end
    if S.entered == nil then return end
    local cad = F.enraged and V.CADENCE_ENRAGED or V.CADENCE
    -- the enrage SETS a pending slot to 5 ticks on -- later as well as
    -- sooner (C: ~tob_verzik_check_enrage, `clock > map_clock` ->
    -- `map_clock + 5`); a slot due this very tick goes out.  Not while a
    -- special holds her clock (the webs' and the yellows' resumes stand).
    -- Pulling only slots beyond E+5 left a slot due at E+4 "passed" on the
    -- schedule, and her melee at E+5 found raiders beside her (sc, se, sg,
    -- sp: one melee each, all on the enrage).
    if F.enrage_now then
        QD.raid._vzp3_trace(S, F.tick, "enraged (next slot was t" .. tostring(S.next_slot) .. ")")
        if not S.suspended and S.next_slot and S.next_slot > F.tick then
            S.next_slot = F.tick + V.ENRAGE_PULL
        end
    end
    local function slot_done(T, special)
        -- a slot went out at T: an auto, or the special it names
        S.last_slot = T
        if special then
            S.special = S.special % #V.ROTATION + 1
            S.attacks_left = V.ATTACKS_PER_SPECIAL
            S.seen[special] = (S.seen[special] or 0) + 1
            -- Hard Mode re-picks her tank on every special
            -- (`~tob_verzik_special`): not known again until she faces
            if QD.raid.tob_mode == "hard" then
                S.tank_ok, S.tank_prev, S.face_cleared = false, S.tank_pid, false
                -- ...and her face is the truth from T+3, and ONLY then: the
                -- next tick's `~tob_verzik_pick_tank` names the new tank and
                -- its `~tob_verzik_p3_follow` faces it, beside her or walking
                -- at it (she keeps the face while she walks, the PLAYERFACE
                -- walk, 2026-10-13), and the client reads that a tick on.
                -- Read at T+1 the face was the OLD tank's, a pid
                -- other than the last one named, and that seat was taken for
                -- the tank while the real one stood beside her (xv33 sh
                -- t1318 -> the melee at t1344). The webs and the yellows
                -- suspend the follow: from their resume.
                S.tank_trust_at = (special == "webs" or special == "yellows") and math.huge or (T + V.TANK_TRUST)
            end
        else
            S.attacks_left = math.max(S.attacks_left - 1, 0)
            -- the first auto after the webs or the yellows: her follow runs again
            if S.tank_trust_at == math.huge then S.tank_trust_at = T + V.TANK_TRUST end
        end
        S.suspended = nil
    end
    local seq_now = b and b.seq_tick ~= nil and b.seq_tick ~= S.last_seq_tick and b.seq_id ~= nil and b.seq_id >= 0
    local seq = seq_now and b.seq_id or nil
    if seq_now then S.last_seq_tick = b.seq_tick end
    -- the ball shows the ranged pose: its projectile is the tell
    if seq == ids.seq_ranged and F.ball_thrown then seq = "ball" end
    if seq == ids.seq_melee or seq == ids.seq_magic or seq == ids.seq_ranged then
        S.autos = S.autos + 1
        if seq == ids.seq_magic then S.prot = "magic" end
        if seq == ids.seq_ranged then S.prot = "missiles" end
        if seq == ids.seq_melee then
            S.melees = S.melees + 1
            QD.raid._vzp3_trace(S, F.tick, "MELEE")
        end
        if S.next_slot ~= F.tick then
            QD.raid._vzp3_trace(S, F.tick, "auto off schedule (expected t" .. tostring(S.next_slot) .. ")")
        end
        slot_done(F.tick, nil)
        S.next_slot = F.tick + cad
    elseif seq == ids.seq_summon then
        S.autos = S.autos + 1
        slot_done(F.tick, "crabs")
        S.next_slot = F.tick + cad
        QD.raid._vzp3_trace(S, F.tick, "special: crabs")
    elseif seq == "ball" then
        slot_done(F.tick, "ball")
        S.next_slot = F.tick + V.AFTER_BALL
        QD.raid._vzp3_trace(S, F.tick, "special: ball, next slot t" .. S.next_slot)
    elseif seq == ids.seq_powerblast then
        slot_done(F.tick, "yellows")
        S.blast = F.tick + V.YELLOW_CHARGE
        S.next_slot = F.tick + V.AFTER_YELLOWS
        S.suspended = true
        S.pool_mine, S.pools = nil, nil
        QD.raid._vzp3_trace(S, F.tick, "special: yellows, blast t" .. S.blast)
    elseif seq == ids.seq_webspin then
        S.spin_at = F.tick
        S.seen.webs = (S.seen.webs or 0) + 1
        if S.webs_at == nil then
            -- the walk was not seen (she stood on the centre already)
            S.webs_at = F.tick
            slot_done(F.tick, "webs")
            S.seen.webs = S.seen.webs - 1
            S.next_slot = F.tick + V.WEB_RESUME
            S.suspended = true
        end
        QD.raid._vzp3_trace(S, F.tick, "special: webs (spin), next slot t" .. S.next_slot)
    end
    -- the webs slot shows no animation: the slot came, no auto went out, and
    -- the rotation names the webs
    if S.webs_at == nil and S.next_slot ~= nil and F.tick >= S.next_slot and not seq_now
        and S.attacks_left == 0 and V.ROTATION[S.special] == "webs" then
        S.webs_at = S.next_slot
        slot_done(S.webs_at, "webs")
        S.seen.webs = S.seen.webs - 1
        S.next_slot = S.webs_at + V.WEB_RESUME
        S.suspended = true
        QD.raid._vzp3_trace(S, F.tick, "special: webs from t" .. S.webs_at .. ", next slot t" .. S.next_slot)
    end
    -- a slot that passed with nothing seen (a melee is an animation too):
    -- assume it went out and keep the cadence
    if S.next_slot and F.tick > S.next_slot + 1 then
        QD.raid._vzp3_trace(S, F.tick, "slot t" .. S.next_slot .. " unseen, assumed")
        slot_done(S.next_slot, nil)
        S.next_slot = S.next_slot + cad
    end
    -- the web special is over once her clock resumes
    if S.webs_at and F.tick >= S.webs_at + V.WEB_RESUME then
        S.webs_at, S.spin_at = nil, nil
    end
    F.charging = S.blast ~= nil and F.tick < S.blast
    if S.blast and F.tick >= S.blast then S.blast = nil end
    -- whom she faces during the webs: each throw's target (a probe of the
    -- server's FACE_ENTITY; the trace only)
    if b and S.webs_at and b.facing ~= S.web_face_traced then
        S.web_face_traced = b.facing
        QD.raid._vzp3_trace(S, F.tick, "webs: she faces " .. tostring(b.facing))
    end
    -- HER TANK: whom she faces (`~tob_verzik_p3_follow`: `~tob_verzik_face_uid`),
    -- beside her or walking at it - her walk keeps the face (the PLAYERFACE
    -- walk, 2026-10-13; before it a walk cleared the face and a Hard Mode
    -- re-pick was learned only when she reached the new tank). So the face
    -- names the new tank from the T+3 trust tick: the tank is known once she
    -- faces a different raider, or anyone after the face was cleared.
    if b and S.webs_at == nil then
        if b.facing ~= nil and b.facing >= 32768 then
            local pid = b.facing - 32768
            if not S.tank_ok then
                if S.tank_trust_at then
                    -- Hard Mode's re-pick: the face from its trusted tick only
                    if F.tick >= S.tank_trust_at then S.tank_ok = true end
                elseif pid ~= S.tank_prev or S.face_cleared or S.tank_prev == nil then
                    S.tank_ok = true
                end
            end
            if pid ~= S.tank_pid then
                S.tank_pid = pid
                QD.raid._vzp3_trace(S, F.tick, "her tank: pid " .. pid)
            end
        else
            S.face_cleared = true
        end
    end
    -- the scan ticks in the horizon: the end of the tick before every slot.
    -- A resume after a suspended special is the content's constant from a
    -- slot I inferred, so it gets a tick of margin either side.
    F.scans = {}
    if S.next_slot then
        local t = S.next_slot
        local margin = S.suspended and 1 or 0
        for i = 0, 2 do
            F.scans[#F.scans + 1] = { t0 = t - 1 - margin, t1 = t - 1 + margin }
            t = t + cad
            margin = 0
        end
    end
end

-- Projectiles: the autos' styles, the webs in flight, the ball's chain.
function QD.raid._vzp3_projectiles(S, F)
    local V, ids = QD.VZP3, S.ids
    local r, projs = api_drive.projectiles(0)
    if r ~= "ok" then projs = {} end
    local ball_now = nil
    F.ball_thrown = false
    for _, p in ipairs(projs) do
        local cycles = math.max(p.cycles_left or 0, 0)
        if p.spotanim_id == ids.web_proj then
            -- keyed by tile and landing: a map projectile's element id is
            -- the same for all of them, and keyed on it only the first web
            -- of a special was ever remembered (sa: 184 web damage)
            -- A projectile is read a tick into its flight (the tick's packets
            -- applied, the world stepped 30 cycles), so the content's landing,
            -- throw tick + floor(flight / 30), is floor(cycles_left / 30) + 1
            -- from here: webs 120 -> +4, the ball 227 -> +7, its hop 185 ->
            -- +6 (every one checked against the tick log). ceil put the web a
            -- tick early (harmless) and floor put the ball a tick early, which
            -- the three-tick hold hid until the hold became one tick (sa t817:
            -- a 74 on a raider holding the right tile at the wrong tick).
            -- FROM FIRST SIGHT, BY THE CONTENT'S FLIGHT, not from cycles_left:
            -- the live client reads a projectile further into its flight than
            -- scriptrun does (a throw of 227 cycles read as 197 on scriptrun,
            -- fewer live), and a landing a tick late put the "clear" raider
            -- at gap 2 on the read: three 74s, two deaths (live, 2026-10-09).
            -- A web is 120 cycles: +4.
            -- The web's flight is the recorded 30 x floor(d / 2) cycles from her
            -- centre (docs/rsprox-analysis/tob/verzik.md), so a near web lands
            -- sooner than a far one: floor(d / 2) ticks, at least one.
            local land = F.tick + QD.VZP3.web_flight(p)
            local key = p.dst_x .. "," .. p.dst_z .. "@" .. land
            if not S.web_seen[key] then
                S.web_seen[key] = true
                S.web_hazards[#S.web_hazards + 1] = { x = p.dst_x, z = p.dst_z, from = land - 1, to = land + V.WEB_LIFE }
                if cheb(p.dst_x, p.dst_z, F.me.x, F.me.z) <= 1 then
                    QD.raid._vzp3_trace(S, F.tick, "web at " .. p.dst_x .. "," .. p.dst_z .. " lands t" .. land .. " (me " .. F.me.x .. "," .. F.me.z .. ")")
                end
            end
        elseif p.spotanim_id == ids.ball and p.target ~= nil and p.target < 0 then
            -- the throw's 227 cycles land +7 from the throw tick, a hop's 185
            -- land +6 from the landing it left (the pid rule adds one below);
            -- first sight is the throw tick (it rides her ranged pose) and the
            -- previous landing
            ball_now = { pid = -p.target - 1, first = F.tick }
        elseif (p.spotanim_id == ids.proj_ranged or p.spotanim_id == ids.proj_magic)
            and F.pid ~= nil and p.target == -F.pid - 1 then
            S.prot = (p.spotanim_id == ids.proj_magic) and "magic" or "missiles"
        end
    end
    local keep = {}
    for _, h in ipairs(S.web_hazards) do if h.to >= F.tick then keep[#keep + 1] = h end end
    S.web_hazards = keep
    -- webs standing on the floor that no projectile announced (seen late)
    for _, w in ipairs(F.webs) do
        local known = false
        for _, h in ipairs(S.web_hazards) do
            if h.x == w.x and h.z == w.z then known = true break end
        end
        if not known then S.web_hazards[#S.web_hazards + 1] = { x = w.x, z = w.z, from = F.tick, to = F.tick + V.WEB_LIFE } end
    end
    if ball_now then
        if S.ball == nil then
            S.ball = { order = {}, visited = {} }
            S.seen.ball = (S.seen.ball or 0) + 1
            F.ball_thrown = true
            QD.raid._vzp3_trace(S, F.tick, "special: ball at pid " .. ball_now.pid)
        end
        local B = S.ball
        if B.cur ~= ball_now.pid then
            -- a hop's landing queue counts one tick more when the new
            -- target's pid is below the sender's (its queues ran already)
            ball_now.land = ball_now.first + (B.cur == nil and V.BALL_FLIGHT or V.BALL_HOP)
            if B.cur ~= nil and ball_now.pid < B.cur then ball_now.land = ball_now.land + 1 end
            B.cur = ball_now.pid
            B.visited[ball_now.pid] = true
            B.order[#B.order + 1] = ball_now.pid
            -- the meeting point: the target's tile as the ball (or its hop)
            -- is first seen, the same on every raider's view
            B.anchor = nil
            for _, rd in ipairs(F.raiders) do
                if rd.pid == ball_now.pid then B.anchor = { x = rd.x, z = rd.z } end
            end
            B.land = ball_now.land
            QD.raid._vzp3_trace(S, F.tick, "ball on pid " .. ball_now.pid .. ", lands t" .. B.land
                .. (B.anchor and (" at " .. B.anchor.x .. "," .. B.anchor.z) or " (no anchor)"))
        end
        B.last_seen = F.tick
    elseif S.ball and F.tick - S.ball.last_seen > 2 then
        QD.raid._vzp3_trace(S, F.tick, "ball over: " .. table.concat(S.ball.order, ">"))
        S.ball = nil
    end
end

-- Whose tornado is whose.  Each walks a step a tick at its own raider's
-- end-of-T-1 tile (sign(dx), sign(dz)) and touches only its own.  BY
-- ELIMINATION: a raider is ruled out by OWNER_MISSES steps that do not head
-- for its last tile, or at once by a spawn outside the 4-8 ring (seen a tick
-- late: 2-10).  A tornado is MINE while I am not ruled out.  (Votes for the
-- raiders a step heads at never settled a tornado among five raiders round
-- her - every step headed at three or four of them - so each seat dodged all
-- five and swung once in 25 ticks after the enrage, xv21.)
function QD.raid._vzp3_tornadoes(S, F)
    local V = QD.VZP3
    local seen = {}
    local last = S.last_raiders or {}
    for _, tn in ipairs(F.tornadoes) do
        seen[tn.slot] = true
        local rec = S.torn[tn.slot]
        if rec == nil then
            rec = { miss = {}, born = F.tick }
            S.torn[tn.slot] = rec
            S.tornado_spawns = (S.tornado_spawns or 0) + 1
            for _, rd in ipairs(last) do
                local d = cheb(rd.x, rd.z, tn.x, tn.z)
                if d < V.TORNADO_RING[1] or d > V.TORNADO_RING[2] then rec.miss[rd.pid] = V.OWNER_MISSES end
            end
        elseif rec.x and (rec.x ~= tn.x or rec.z ~= tn.z) then
            -- (a miss only against BOTH of the raider's tiles I hold - last
            -- tick's and this one's: which of them the step read depends on
            -- the lane's tick, and a one-tick misread flipped owners, xv22)
            local sx, sz = tn.x - rec.x, tn.z - rec.z
            local now = {}
            for _, rd in ipairs(F.raiders) do now[rd.pid] = rd end
            local function heads(rd)
                if rd == nil then return false end
                local dx = (rd.x > rec.x and 1) or (rd.x < rec.x and -1) or 0
                local dz = (rd.z > rec.z and 1) or (rd.z < rec.z and -1) or 0
                return dx == sx and dz == sz
            end
            for _, rd in ipairs(last) do
                if not heads(rd) and not heads(now[rd.pid]) then rec.miss[rd.pid] = (rec.miss[rd.pid] or 0) + 1 end
            end
        end
        rec.x, rec.z = tn.x, tn.z
        -- an owner once settled stays, unless its own misses reach twice
        -- the bar; all ruled out (a misread) starts over
        if rec.owner ~= nil and (rec.miss[rec.owner] or 0) >= 2 * V.OWNER_MISSES then rec.owner = nil end
        local left = 0
        for _, rd in ipairs(F.raiders) do
            if (rec.miss[rd.pid] or 0) < V.OWNER_MISSES then left = left + 1 end
        end
        if left == 0 then rec.miss = {} end
    end
    -- ONE TORNADO A RAIDER (owner, 2026-10-12: "each seat should only care
    -- about their own tornado"; `~tob_verzik_tornado_spawn` gives each its
    -- own): a settled owner is ruled out for every other tornado, and with as
    -- many tornadoes as raiders a raider possible for one tornado only owns
    -- it - repeated until nothing changes. Five raiders round her made each
    -- step head at three or four of them, so one tornado's own steps settled
    -- it slowly and the seats dodged two or three each.
    local cand = {}
    for _, tn in ipairs(F.tornadoes) do
        local rec = S.torn[tn.slot]
        local c = {}
        if rec.owner ~= nil then
            c[rec.owner] = true
        else
            for _, rd in ipairs(F.raiders) do
                if (rec.miss[rd.pid] or 0) < V.OWNER_MISSES then c[rd.pid] = true end
            end
        end
        cand[tn.slot] = c
    end
    local changed = true
    while changed do
        changed = false
        for _, tn in ipairs(F.tornadoes) do
            local c = cand[tn.slot]
            local n, only = 0, nil
            for pid in pairs(c) do n, only = n + 1, pid end
            if n == 1 then
                for _, other in ipairs(F.tornadoes) do
                    if other.slot ~= tn.slot and cand[other.slot][only] then
                        local oc = cand[other.slot]
                        local on = 0
                        for _ in pairs(oc) do on = on + 1 end
                        if on > 1 then oc[only] = nil changed = true end
                    end
                end
            end
        end
        if #F.tornadoes == #F.raiders then
            for _, rd in ipairs(F.raiders) do
                local where, count = nil, 0
                for _, tn in ipairs(F.tornadoes) do
                    if cand[tn.slot][rd.pid] then where, count = tn.slot, count + 1 end
                end
                if count == 1 then
                    local c = cand[where]
                    local n = 0
                    for _ in pairs(c) do n = n + 1 end
                    if n > 1 then cand[where] = { [rd.pid] = true } changed = true end
                end
            end
        end
    end
    for _, tn in ipairs(F.tornadoes) do
        local rec = S.torn[tn.slot]
        local c = cand[tn.slot]
        local n, only = 0, nil
        for pid in pairs(c) do n, only = n + 1, pid end
        if rec.owner == nil and n == 1 then rec.owner = only end
        tn.owner = rec.owner
        if rec.owner ~= nil then
            tn.mine = (rec.owner == F.pid)
        else
            tn.mine = F.pid ~= nil and c[F.pid] == true
        end
        if tn.owner ~= rec.owner_traced then
            rec.owner_traced = tn.owner
            QD.raid._vzp3_trace(S, F.tick, "tornado slot " .. tn.slot .. (tn.owner and (" is pid " .. tn.owner .. "'s") or " may be mine"))
        end
    end
    for slot in pairs(S.torn) do if not seen[slot] then S.torn[slot] = nil end end
    S.last_raiders = F.raiders
end

-- The yellow pools: the same assignment on every raider (pid order, pools in
-- tile order, the permutation with the cheapest worst walk, then the
-- cheapest total, then the first).
function QD.raid._vzp3_pools(S, F)
    if not F.charging then
        S.pools, S.pool_mine = nil, nil
        return
    end
    if S.pool_mine then return end
    local r, rows = api_drive.spotanims(0)
    if r ~= "ok" then return end
    local seen, pools = {}, {}
    for _, s in ipairs(rows) do
        if s.spotanim_id == S.ids.pool then
            local k = s.x .. "," .. s.z
            if not seen[k] then
                seen[k] = true
                pools[#pools + 1] = { x = s.x, z = s.z }
            end
        end
    end
    local n = #F.raiders
    if #pools < n then return end                    -- not all drawn yet
    table.sort(pools, function(a, b) return a.x < b.x or (a.x == b.x and a.z < b.z) end)
    local best, best_worst, best_sum = nil, nil, nil
    local used, pick = {}, {}
    local function walk(i)
        if i > n then
            local worst, sum = 0, 0
            for j = 1, n do
                local d = cheb(F.raiders[j].x, F.raiders[j].z, pools[pick[j]].x, pools[pick[j]].z)
                if d > worst then worst = d end
                sum = sum + d
            end
            if best == nil or worst < best_worst or (worst == best_worst and sum < best_sum) then
                best, best_worst, best_sum = { table.unpack(pick) }, worst, sum
            end
            return
        end
        for p = 1, #pools do
            if not used[p] then
                used[p] = true
                pick[i] = p
                walk(i + 1)
                used[p] = false
            end
        end
    end
    walk(1)
    S.pools = pools
    for j = 1, n do
        if F.raiders[j].pid == F.pid then S.pool_mine = pools[best[j]] end
    end
    QD.raid._vzp3_trace(S, F.tick, string.format("my pool %d,%d of %d (worst walk %d tiles)",
        S.pool_mine.x, S.pool_mine.z, #pools, best_worst))
end

-- What I hit: her, unless she is invulnerable (the yellows' charge, the
-- webs' walk) or gone.
function QD.raid._vzp3_target(S, F)
    if F.boss == nil then return nil end
    if F.charging then return nil end
    if S.webs_at and S.spin_at == nil then return nil end
    if S.attacks_left == 0 and QD.VZP3.ROTATION[S.special] == "webs" and S.next_slot and F.tick >= S.next_slot - 1 then return nil end
    return F.boss
end

-- ================================================================== DECIDE

-- The ticks between my swings: the option, else the weapon I wear.
function QD.raid._vzp3_weapon_ticks(S)
    local V = QD.VZP3
    if S.weapon_ticks_opt then return S.weapon_ticks_opt end
    if S.weapon_table == nil then
        S.weapon_table = {}
        for name, ticks in pairs(V.WEAPON_TICKS) do
            local r, id = api_drive.symbol("obj", name)
            if r == "ok" then S.weapon_table[#S.weapon_table + 1] = { id = id, ticks = ticks, name = name } end
        end
        table.sort(S.weapon_table, function(a, b) return a.name < b.name end)
    end
    for _, w in ipairs(S.weapon_table) do
        local _, n = api_drive.inv_count(S.ids.worn, w.id)
        if (n or 0) > 0 then return w.ticks end
    end
    return V.DEFAULT_TICKS
end

-- Her edge's melee ring round a footprint at (x, z), in order round her:
-- the south edge west to east, the east edge south to north, the north edge
-- east to west, the west edge north to south (so CLOCKWISE is down the list).
function QD.raid._vzp3_ring(x, z)
    local n = QD.VZP3.SIZE
    local ring = {}
    for i = x, x + n - 1 do ring[#ring + 1] = { x = i, z = z - 1 } end
    for i = z, z + n - 1 do ring[#ring + 1] = { x = x + n, z = i } end
    for i = x + n - 1, x, -1 do ring[#ring + 1] = { x = i, z = z + n } end
    for i = z + n - 1, z, -1 do ring[#ring + 1] = { x = x - 1, z = i } end
    return ring
end

-- THE WEB RUN (S.hm.web_run; owner, 2026-10-12: "For hard mode, the solver
-- should implement the web running state"; "The solver should be able to
-- avoid ALL WEBS. Implement a 5 man web run"; "Web running should be
-- implemented as a sub-state machine"). The wiki's Sticky webs: the team on
-- ONE tile beside her, attacking on the same tick, a step CLOCKWISE from the
-- south after each tile's attacks. Her webs land on the tile a raider stood
-- on and two more 1-3 tiles round it (`~tob_verzik_web_throw`), four ticks
-- after the throw, so the stack's tile is safe until the first web thrown at
-- it is a tick from landing, and a step reads the scattered ones from their
-- projectiles (`webbed`). Every seat reads the same throws, so every seat makes the same
-- transitions on the same tick and the stack stays one tile.
--
--   gather  her walk to the centre (invulnerable): to the start tile, the
--           south middle of her ring there (clear of the knockback under
--           her), and hold it
--   hold    on the stack's tile: swing (attackable from the spin)
--   join    off the stack's tile (a late seat, a crab's fuse that moved me):
--           run to it, or the planner pulls me there from further than a run
--   step    the stack's tile turns hazardous next tick: one ring tile
--           clockwise, two if a lagging web holds the next and not the one
--           between; the tile moves, so everyone is off it at once (join)
--   lost    no clear tile clockwise: the planner, until a tile clears
--
-- Not with tornadoes (enraged): the kite runs the spin (QD.raid._vzp3_kite).
-- -> a decision, or nil for the planner (S.wr.tile pulls it to the stack).
function QD.raid._vzp3_web_run(S, F)
    local V = QD.VZP3
    if not (S.hm and S.hm.web_run) or S.webs_at == nil or F.boss == nil
        or F.enraged or F.tick >= S.webs_at + V.WEB_RESUME - V.WEB_RUN_LEAVE then
        if S.wr and S.wr.state ~= "done" then
            QD.raid._vzp3_trace(S, F.tick, "web run: " .. S.wr.state .. " -> done")
            S.wr.state, S.wr.tile = "done", nil
        end
        return nil
    end
    local base = S.base
    local ring = QD.raid._vzp3_ring(base.x + V.CENTRE.x - 3, base.z + V.CENTRE.z - 3)
    local L = #ring
    local function at(idx) return ring[((idx - 1) % L) + 1] end
    local function webbed(p, t0, t1)
        for _, h in ipairs(S.web_hazards) do
            if h.x == p.x and h.z == p.z and h.from <= t1 and h.to >= t0 then return true end
        end
        return false
    end
    local W = S.wr
    if W == nil or W.webs_at ~= S.webs_at then
        W = { webs_at = S.webs_at, idx = V.WEB_RUN_START, state = "gather" }
        S.wr = W
        QD.raid._vzp3_trace(S, F.tick, "web run: gather at " .. at(W.idx).x .. "," .. at(W.idx).z)
    end
    local function enter(state, why)
        if W.state ~= state then
            QD.raid._vzp3_trace(S, F.tick, "web run: " .. W.state .. " -> " .. state .. " (" .. why .. ")")
            W.state = state
        end
    end
    -- the stack's tile first: it moves when it turns hazardous, whatever my state
    if webbed(at(W.idx), F.tick + 1, F.tick + 1) then
        local to = nil
        for step = 1, 2 do
            local mid_ok = step == 1 or not webbed(at(W.idx - 1), F.tick + 1, F.tick + 1)
            if mid_ok and not webbed(at(W.idx - step), F.tick + 1, F.tick + V.WEB_RUN_HOLD) then
                to = W.idx - step
                break
            end
        end
        if to == nil then
            enter("lost", "no clear tile clockwise of " .. at(W.idx).x .. "," .. at(W.idx).z)
            W.tile = nil
            return nil
        end
        W.idx = ((to - 1) % L) + 1
        enter("step", "to " .. at(W.idx).x .. "," .. at(W.idx).z)
    elseif W.state == "lost" then
        enter("join", "the stack's tile is clear")
    end
    local here = at(W.idx)
    W.tile = here
    local on = F.me.x == here.x and F.me.z == here.z
    if S.spin_at == nil then
        enter("gather", "her walk")
    elseif on then
        enter("hold", "on the stack")
    elseif W.state ~= "step" then
        enter("join", "off the stack")
    end
    -- a crab's fuse, or a walk longer than a run's two tiles: the planner
    if #S.blasts > 0 or cheb(F.me.x, F.me.z, here.x, here.z) > 2 then return nil end
    local decision = { x = here.x, z = here.z, soft = 0, lethal = 0 }
    if not on then
        decision.order = { mode = "walk", x = here.x, z = here.z }
    elseif W.state == "hold" and S.target == F.boss then
        decision.order = { mode = "attack" }
    else
        decision.order = { mode = "walk", x = here.x, z = here.z }
    end
    return decision
end

-- ===================================================================== EMIT

-- ===================================================================== LOOP

-- The super restores, then the prayer potions the ToB supply chest sells
-- (enum_1952, 2 points): the relay's seats bring those into P3.
-- ====================================================== THE PRAYER BUDGET
--
-- Verzik is the last room and the last chest is Sotetseg's, so what a seat
-- carries in is all the prayer it has. Every death in the relay sweeps after
-- the zap was fixed (rl58 rn, rl59 ta tk tg) was a seat out of restores or
-- brews in P3: prayer 0, her autos unprayed, 29s. Two things spent it:
--   Piety, kept lit to 0 points (tk: 42 points in 105 ticks with the
--     overhead, one a tick and a half; the overhead alone is one in 7.5 at
--     this kit's prayer bonus, Piety alone one in 3.75), and
--   a super restore for every brew's Attack drain in P3.
-- So: the points I have (prayer + a dose's 31 per restore or prayer dose
-- carried) less the overhead's drain over what is left of the fight is the
-- SPARE. IN P3, Piety stays on while the spare is positive -- the overhead is
-- funded to the end -- and goes OFF only when it is not: with void and whips
-- the fight is long (P3 392 ticks against Blert's 139), 94% of P3 damage is
-- her autos landing on every raider at a prayed 8.5 a hit, so damage is paid
-- by the tick, and Piety's quarter off the kill time is ~110 hp a raider for
-- ~104 prayer points. Rationing Piety to "the spare covers Piety to the end"
-- made P3 30 ticks longer (rl58 361 -> rl64 392) and seats died out of brews
-- with prayer to spare (rl64 se, ri). In P2 Piety still has to leave P3's
-- (and P2 cannot go without it: the reds outlive their window, rl71). (Drained stats are restored
-- at two brews' drain, QD.VZP3.STAT_DRAIN, not gated on the spare: skipped,
-- the hits on her fell to 1 and the fight never ended.)
QD.VZ_PRAYER = {
    PER_DOSE = 31,
    PROTECT_TICKS = 6,       -- a point per this many ticks: the overhead alone (7.5 measured, kept short)
    PIETY_TICKS = 3.75,      -- and Piety's own share
    P2_TICKS = 520,          -- P2's length, p90 of 32 relays (rl56/rl58: median 468)
    P3_TICKS = 400,          -- P3's (p90 384, median 354)
}

function QD.raid.vz_prayer_doses(inv)
    local P = QD.VZ_PRAYER
    if P.ids == nil then
        P.ids = {}
        for _, stem in ipairs({ "dose2restore", "doseprayerrestore" }) do
            for _, pre in ipairs({ "br_", "" }) do
                for n = 1, 4 do
                    local r, id = api_drive.symbol("obj", pre .. n .. stem)
                    if r == "ok" then P.ids[#P.ids + 1] = { id = id, doses = n } end
                end
            end
        end
    end
    local total = 0
    for _, e in ipairs(P.ids) do
        local _, c = api_drive.inv_count(inv, e.id)
        total = total + e.doses * (c or 0)
    end
    return total
end

-- -> the spare points, and whether Piety is affordable, `remaining` ticks out
function QD.raid.vz_prayer_spare(inv, prayer, remaining)
    local P = QD.VZ_PRAYER
    local spare = prayer + QD.raid.vz_prayer_doses(inv) * P.PER_DOSE - remaining / P.PROTECT_TICKS
    return spare, spare >= remaining / P.PIETY_TICKS
end

function QD.raid._vzp3_restores(list)
    for n = 1, 4 do
        local r, id = api_drive.symbol("obj", n .. "doseprayerrestore")
        if r == "ok" then list[#list + 1] = id end
    end
    return list
end
