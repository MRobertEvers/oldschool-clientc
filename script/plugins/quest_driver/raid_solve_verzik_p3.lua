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
    WEB_LIFE = 12, WEB_FLIGHT = 4, WEB_KNOCK = 3,
    BALL_FLIGHT = 7, BALL_HOP = 6,  -- C: 227 and 185 cycles over 30, the queue delays
    ENRAGE_PULL = 5,
    CENTRE = { x = 32, z = 25 },  -- her middle for the webs, local to the room's origin
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

local function cheb(ax, az, bx, bz) return QD.raid._vzp2_cheb(ax, az, bx, bz) end
local function beside(x, z, fx, fz, n) return QD.raid._vzp2_beside(x, z, fx, fz, n) end

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

function QD.raid._vzp3_measure(S)
    local ids = S.ids
    local F = { crabs = {}, webs = {}, tornadoes = {}, raiders = {}, mates = {} }
    -- THE TICK IS THE CLIENT'S OWN: api_drive.tick(), the world cycle over
    -- 30 on the live client and the bot's applied tick on scriptrun. The
    -- live loop wakes about twice per server tick (today's run: 376 wakes
    -- over 188 ticks on every seat), so a counted tick ran ahead 2x and every
    -- absolute-tick hold -- the pool at Y+13, the ball at L-1, the scans --
    -- landed on the wrong tick; the server's own counter (srv->tick, or a
    -- member's lockstep stamp) was right but repeated. The cycle clock is
    -- what the client's projectiles and anims are stepped on, so deadlines
    -- in it agree with the observations. A wake in a tick already decided
    -- is not decided again (the loop below).
    F.tick = api_drive.tick()
    assert(math.type(F.tick) == "integer", "verzik_p3_solve: api_drive.tick answered no tick")
    local tr, tile = api_drive.player_tile()
    assert(tr == "ok", "verzik_p3_solve: no tile")
    F.me = { x = tile.x, z = tile.z }
    local nr, rows = api_drive.npcs(0)
    if nr ~= "ok" then rows = {} end
    for _, row in ipairs(rows) do
        -- THE SERVER'S TILE. The live client's `x`/`z` is the tile the figure
        -- is drawn on, a tile behind the server's while it walks; scriptrun's
        -- is the server's. Both carry it as server_x/server_z. Read off `x`,
        -- every tornado on the live lane was a tile short and touched 21
        -- times in 22 spawns (2026-10-08).
        if row.server_x ~= nil then row.x, row.z = row.server_x, row.server_z end
        local id = row.npc_id
        if id == ids.p3 then F.boss = row
        elseif id == ids.transition then F.transition = row
        elseif id == ids.bat then F.bat = row
        elseif id == ids.web then F.webs[#F.webs + 1] = row
        elseif id == ids.tornado then F.tornadoes[#F.tornadoes + 1] = row
        elseif ids.crabs[id] then F.crabs[#F.crabs + 1] = row
        end
    end
    local pr, players = api_drive.players()
    if pr == "ok" then
        for _, p in ipairs(players) do
            local px, pz = p.server_x or p.x, p.server_z or p.z
            F.raiders[#F.raiders + 1] = { pid = p.pid, x = px, z = pz, me = p.me }
            -- (the server tile, as F.raiders: live's x is the drawn one, and
            -- the crab look-ahead reads mates - QD.raid._vzp2_crab_stop)
            if p.me then
                F.pid = p.pid
                -- my own swing, the tick my screen sees it start (both lanes'
                -- `me` row counts the starts)
                F.seq_starts, F.seq = p.seq_starts, p.seq
            else
                F.mates[#F.mates + 1] = { pid = p.pid, x = px, z = pz }
            end
        end
    end
    table.sort(F.raiders, function(a, b) return a.pid < b.pid end)
    local hr, hp = api_drive.skill(ids.hitpoints)
    F.hp = (hr == "ok" and hp.level) or 0
    local qr, pp = api_drive.skill(ids.prayer)
    F.prayer = (qr == "ok" and pp.level) or 0
    local ar, at = api_drive.skill(ids.attack)
    F.attack, F.attack_base = (ar == "ok" and at.level) or 0, (ar == "ok" and at.base_level) or 0
    local _, ml = api_drive.varbit(ids.missiles_lit)
    local _, gl = api_drive.varbit(ids.magic_lit)
    local _, pl = api_drive.varbit(ids.piety_lit)
    F.lit = { missiles = ml == 1, magic = gl == 1, piety = pl == 1 }
    local _, ww = api_drive.inv_count(ids.worn, ids.weapon)
    local _, wp = api_drive.inv_count(ids.inv, ids.weapon)
    F.weapon_held = (ww or 0) == 0 and (wp or 0) > 0
    F.enraged = S.enraged or #F.tornadoes > 0
    F.enrage_now = F.enraged and not S.enraged
    S.enraged = F.enraged
    if F.seq_starts and F.seq_starts ~= S.seq_starts then
        S.seq_starts = F.seq_starts
        if QD.VZP3.SWING_SEQS[F.seq] then S.kite_swing = F.tick end
    end
    return F
end

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
            local land = F.tick + V.WEB_FLIGHT
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

-- The ball's chain, from my seat: "target", "next" (beside the target at its
-- landing), "clear" (3+ from it), or nil (visited, or no ball).
function QD.raid._vzp3_ball_role(S, F)
    local B = S.ball
    if B == nil or B.land == nil or B.anchor == nil or F.pid == nil then return nil end
    local count = 0
    for _ in pairs(B.visited) do count = count + 1 end
    if count >= #F.raiders then return nil end      -- the last landing dissipates
    local nxt = nil
    for _, rd in ipairs(F.raiders) do
        if not B.visited[rd.pid] and rd.pid ~= B.cur then nxt = rd break end
    end
    if B.cur == F.pid then return "target", B.anchor, nxt end
    if B.visited[F.pid] or nxt == nil then return nil end
    return (nxt.pid == F.pid) and "next" or "clear", B.anchor, nxt
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

-- ================================================================= CONTEXT
--
-- The planner's spec for this tick: the goal and the constraints the current
-- context names, the enrage overlay on top.  Every tick window is absolute.
function QD.raid._vzp3_spec(S, F)
    local V, b = QD.VZP3, F.boss
    local base = S.base
    local spec = {
        -- from MY tile as measured (player_tile, the server's), never the
        -- lane's default: the live plugin's was the drawn tile
        from = { x = F.me.x, z = F.me.z },
        now = F.tick, h = V.H, beam = V.BEAM, run = true, move_cost = V.MOVE_COST,
        chasers = {}, forbid = {}, zones = {}, pulls = {},
        edge = { x0 = base.x + V.ARENA.x0, z0 = base.z + V.ARENA.z0, x1 = base.x + V.ARENA.x1, z1 = base.z + V.ARENA.z1,
                 margin = V.EDGE, weight = V.EDGE_WEIGHT },
    }
    local names = { zones = {}, forbid = {}, chasers = {} }
    local function zone(name, zn)
        spec.zones[#spec.zones + 1] = zn
        names.zones[#spec.zones] = name
    end
    -- her melee: at the end of the tick before a slot, under her or 3+ away.
    -- `npc_walk` takes her follow step inside her own timer, before the
    -- attack's check, so the check reads her POST-step footprint: a raider 2
    -- away is 1 away by then (a "gap 2" relaxation cost one melee a seed,
    -- r8). And she walks off a raider standing under her now, one random
    -- cardinal tile (`~tob_verzik_p3_follow`), so under her means the INNER
    -- 5x5, which is under her after any one-tile step; her outer ring is a
    -- gap-1 tile half the time.
    -- ONLY HER TANK STEPS OFF (owner, 2026-10-12: "The tank's whole job is
    -- to step under and prevent her melee attack"): her melee needs the TANK
    -- at range 1 after her follow step and then hits everyone beside her, so
    -- with the tank under her or 3+ away the rest stay beside her and swing.
    -- Until her tank is known (QD.raid._vzp3_clock) everybody steps off.
    local exempt = S.tank_ok and S.tank_pid ~= nil and F.pid ~= nil and S.tank_pid ~= F.pid
    for _, sc in ipairs(F.scans) do
        if b and not exempt then
            zone("scan", { x = b.x, z = b.z, size = V.SIZE, lo = 1, hi = 2, t0 = sc.t0, t1 = sc.t1, tier = "lethal" })
            zone("scan-ring", { x = b.x + 1, z = b.z + 1, size = V.SIZE - 2, lo = 1, hi = 1, t0 = sc.t0, t1 = sc.t1, tier = "lethal" })
        end
    end
    -- crab blasts (the P2 model, QD.raid._vzp2_crabs): within 2 of its 2x2
    -- is out (63, 26), 3 is a flat 7 and only damage
    for _, bl in ipairs(S.blasts) do
        zone("blast", { x = bl.x, z = bl.z, size = V.CRAB_SIZE, lo = 0, hi = V.CRAB_BLAST - 1, t0 = bl.from, t1 = bl.to, tier = "lethal" })
        zone("blast-far", { x = bl.x, z = bl.z, size = V.CRAB_SIZE, lo = V.CRAB_BLAST, hi = V.CRAB_BLAST, t0 = bl.from, t1 = bl.to, tier = "damage", cost = 40 })
    end
    -- webs: never on one from a tick before it lands to its explosion; the
    -- planner charges the middle tile of a run too
    for _, h in ipairs(S.web_hazards) do
        spec.forbid[#spec.forbid + 1] = { x = h.x, z = h.z, t0 = h.from, t1 = h.to, tier = "lethal" }
        names.forbid[#spec.forbid] = "web"
    end
    -- the webs' arrival: nobody under her at the centre from the slot
    -- until the spin.  From the PREDICTED slot: the slot is only confirmed
    -- on its own tick, and a raider walked under the centre a tick before
    -- that was thrown four tiles, held four ticks by the throw, and touched
    -- (sk t791-796).  The arrival reads the end of T+1 at the earliest.
    local webs_due = S.webs_at
    if webs_due == nil and S.attacks_left == 0 and V.ROTATION[S.special] == "webs" and S.next_slot then
        webs_due = S.next_slot
    end
    if webs_due and S.spin_at == nil then
        local cx, cz = base.x + V.CENTRE.x - 3, base.z + V.CENTRE.z - 3
        zone("knockback", { x = cx, z = cz, size = V.SIZE, lo = 0, hi = 0, t0 = webs_due, t1 = webs_due + 12, tier = "lethal" })
    end
    -- the yellows: alone on my pool at the end of Y+13, toward it before
    if S.pool_mine and S.blast then
        local mine = S.pool_mine
        zone("pool", { x = mine.x, z = mine.z, size = 1, lo = 0, hi = 0, t0 = S.blast - 1, t1 = S.blast - 1, require = true, tier = "lethal" })
        zone("pool-early", { x = mine.x, z = mine.z, size = 1, lo = 0, hi = 0, t0 = S.blast - 2, t1 = S.blast - 2, require = true, tier = "damage", cost = 40 })
        for _, p in ipairs(S.pools) do
            if p.x ~= mine.x or p.z ~= mine.z then
                spec.forbid[#spec.forbid + 1] = { x = p.x, z = p.z, t0 = S.blast - 2, t1 = S.blast - 1, tier = "lethal" }
                names.forbid[#spec.forbid] = "other-pool"
            end
        end
        spec.pulls[#spec.pulls + 1] = { x = mine.x, z = mine.z, size = 1, weight = V.POOL_PULL, t0 = F.tick, t1 = S.blast - 3 }
    end
    -- the ball's chain: target on the anchor, next within 1, the third 3+
    -- away, from a tick before the landing to a tick after
    local role, anchor, nxt = QD.raid._vzp3_ball_role(S, F)
    if role then
        -- The landing runs in the target's own queue on tick L, before the
        -- target moves: it reads the target at the end of L-1, a lower pid
        -- (moved already) at the end of L, a higher pid at the end of L-1.
        -- One tick end each, so with a tornado at everyone's heels the hold
        -- is a step onto the tile and a step off (sd t925: a three-tick hold
        -- against three tornadoes took the touch).  The ticks either side
        -- are priced, not forbidden.
        local land = S.ball.land
        if role == "target" then
            zone("ball-hold", { x = anchor.x, z = anchor.z, size = 1, lo = 0, hi = 0, t0 = land - 1, t1 = land - 1, require = true, tier = "lethal" })
            zone("ball-hold-margin", { x = anchor.x, z = anchor.z, size = 1, lo = 0, hi = 0, t0 = land - 2, t1 = land, require = true, tier = "damage", cost = 30 })
            spec.pulls[#spec.pulls + 1] = { x = anchor.x, z = anchor.z, size = 1, weight = V.BALL_PULL, t0 = F.tick, t1 = land - 3 }
        elseif role == "next" then
            local read = (F.pid < S.ball.cur) and land or (land - 1)
            zone("ball-next", { x = anchor.x, z = anchor.z, size = 1, lo = 0, hi = 1, t0 = read, t1 = read, require = true, tier = "lethal" })
            zone("ball-next-margin", { x = anchor.x, z = anchor.z, size = 1, lo = 0, hi = 1, t0 = read - 1, t1 = read + 1, require = true, tier = "damage", cost = 30 })
            spec.pulls[#spec.pulls + 1] = { x = anchor.x, z = anchor.z, size = 1, weight = V.BALL_PULL, t0 = F.tick, t1 = read - 2 }
        else
            -- 3+ from the target for the WHOLE flight, not just round the
            -- landing: kept off only for land-1..land+1, the third raider
            -- walked up to the target mid-flight (pulled at her and at the
            -- next raider), meant to step out, was boxed in, and all three
            -- stood together as it landed - it exploded on every one (rl76 un
            -- t3859-t3866, two dead)
            zone("ball-clear", { x = anchor.x, z = anchor.z, size = 1, lo = 0, hi = 2, t0 = F.tick + 1, t1 = land + 1, tier = "lethal" })
            if nxt then
                spec.pulls[#spec.pulls + 1] = { x = nxt.x, z = nxt.z, size = 1, weight = V.NEXT_PULL, t0 = F.tick, t1 = land + 1 }
            end
        end
    end
    -- the enrage: every tornado that may be mine, walked along each path
    local near_trace = {}
    for _, tn in ipairs(F.tornadoes) do
        if tn.mine and #spec.chasers < 4 then
            spec.chasers[#spec.chasers + 1] = { x = tn.x, z = tn.z, tier = "lethal" }
            names.chasers[#spec.chasers] = "tornado " .. tn.slot
        end
        if cheb(tn.x, tn.z, F.me.x, F.me.z) <= 3 then
            near_trace[#near_trace + 1] = tn.slot .. "@" .. tn.x .. "," .. tn.z .. (tn.mine and "!" or "-")
        end
    end
    -- the seat's own view of a tornado at its heels (the live lane's touches
    -- are read against this; TRACE_CAP bounds it)
    if #near_trace > 0 then
        QD.raid._vzp3_trace(S, F.tick, "me " .. F.me.x .. "," .. F.me.z .. " tornadoes " .. table.concat(near_trace, " "))
    end
    -- damage: beside her, on my role's side (so the three do not stack)
    local target = S.target
    if S.wr and S.wr.state ~= "done" and S.wr.tile then
        -- the web run's stack (QD.raid._vzp3_web_run), from too far to run to
        spec.pulls[#spec.pulls + 1] = { x = S.wr.tile.x, z = S.wr.tile.z, size = 1, weight = V.GOAL_PULL, t0 = F.tick, t1 = F.tick + V.H }
    elseif target and not role then
        spec.goal = { x = target.x, z = target.z, size = V.SIZE, side = ({ 0, 3, 2 })[S.role] or -1,
                      under_ok = true, off_side = V.OFF_SIDE, under = V.UNDER, pull = V.GOAL_PULL }
    elseif target then
        -- the chain outranks her, but a weak pull keeps the team near her
        spec.pulls[#spec.pulls + 1] = { x = target.x, z = target.z, size = V.SIZE, weight = 0.1, t0 = F.tick, t1 = F.tick + V.H }
    elseif S.webs_at and b then
        -- she walks to the centre: wait beside where she will stand
        local cx, cz = base.x + V.CENTRE.x - 3, base.z + V.CENTRE.z - 3
        spec.goal = { x = cx, z = cz, size = V.SIZE, side = ({ 0, 3, 2 })[S.role] or -1,
                      under_ok = false, off_side = V.OFF_SIDE, pull = 0.3 }
    end
    return spec, names
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
-- south after each tile's attacks. Her webs land on the tiles the raiders
-- stood on (`~tob_verzik_web_throw`), four ticks after the throw, so the
-- stack's tile is safe until the first web thrown at it is a tick from
-- landing. Every seat reads the same throws, so every seat makes the same
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

-- THE ENRAGE KITE (S.hm.kite: owner, 2026-10-12: "Just kite the tornado and
-- stay within attacking distance of verzik's edge"; "the solver should be
-- able to get pretty close to 4 ticks per swing"). On the ring of tiles
-- beside her edge (never under her), each tick: stand, or run one or two
-- ring tiles; the one whose distance from where my tornado steps next is
-- greatest, never the tile it stands on, standing to swing whenever my
-- weapon's ticks have passed since my last (QD.raid._vzp3_weapon_ticks). A tornado steps at my last tile and
-- touches only if it already stands there (`~tob_verzik_tornado_tick`), so
-- running along her edge at two a tick outpaces it and a standing tick in
-- four costs one tile: a grid model of exactly this swung once in 4.2 ticks
-- and was never touched (kite_sim2). The planner keeps everything else: the
-- specials, a crab's blast, and the tank's step off for her slot.
-- -> a decision, or nil for the planner.
function QD.raid._vzp3_kite(S, F)
    local V, b = QD.VZP3, F.boss
    if not (S.hm and S.hm.kite) or not F.enraged or b == nil then return nil end
    -- her walk to the centre (invulnerable, the knockback) and the yellows'
    -- charge are the planner's; the web SPIN, the ball and a crab's fuse are
    -- the kite's too, as tiles it may not end a tick on (owner, 2026-10-12:
    -- "the only time the seats should not be landing their attacks is when
    -- there are specials and they have to prioritize avoiding lethal
    -- damage"): with the planner there, a seat chased by its tornado walked
    -- every tick of an enraged spin and swung none (xv28 sh t1384-1409)
    if (S.webs_at and S.spin_at == nil) or F.charging then return nil end
    local ball_anchor, ball_until = nil, nil
    if S.ball then
        local role, anchor = QD.raid._vzp3_ball_role(S, F)
        if role == "target" or role == "next" then return nil end
        if role == "clear" then ball_anchor, ball_until = anchor, S.ball.land + 1 end
    end
    -- HER MELEE IS THE KITE'S TOO (owner, 2026-10-12: "The solver should be
    -- able to prevent ALL melees"): a seat that must be off her edge at a
    -- scan (her tank, or everyone while the tank is not known) ends that tick
    -- UNDER her, on the inner 5x5 (QD.raid._vzp3_spec's scan zones). Handed
    -- to the planner for the scan, the tank with a tornado at its heels found
    -- no safe plan and stayed: every melee of xv31 was enraged.
    local exempt = S.tank_ok and S.tank_pid ~= nil and F.pid ~= nil and S.tank_pid ~= F.pid
    -- (the ones that can reach me inside the horizon, the nearest
    -- KITE_THREATS: on the enrage tick all five are unowned, and five
    -- stepped through the beam spent the instruction budget, xv36 sc t1555)
    local threats = {}
    for _, tn in ipairs(F.tornadoes) do
        local d = cheb(tn.x, tn.z, F.me.x, F.me.z)
        if tn.mine and d <= V.KITE_DEPTH + 2 then threats[#threats + 1] = { x = tn.x, z = tn.z, d = d } end
    end
    table.sort(threats, function(x, y) return x.d < y.d end)
    while #threats > V.KITE_THREATS do table.remove(threats) end
    -- the tiles: her edge's ring (swings), and under her (no swing; not in
    -- the webs, where she is a hard npc)
    local function scan_at(t)
        for _, sc in ipairs(F.scans) do
            if t >= sc.t0 and t <= sc.t1 then return true end
        end
        return false
    end
    -- (under her only when I have a scan to dodge in the horizon: the
    -- search over both every tick spent the instruction budget, xv32)
    local dodge = false
    if not exempt then
        for d = 1, V.KITE_DEPTH do if scan_at(F.tick + d) then dodge = true end end
    end
    local tiles = {}
    for _, t in ipairs(QD.raid._vzp3_ring(b.x, b.z)) do tiles[#tiles + 1] = { x = t.x, z = t.z, ring = true } end
    if S.webs_at == nil and dodge then
        for x = b.x + 1, b.x + V.SIZE - 2 do
            for z = b.z + 1, b.z + V.SIZE - 2 do
                if x == b.x + 1 or x == b.x + V.SIZE - 2 or z == b.z + 1 or z == b.z + V.SIZE - 2 then
                    tiles[#tiles + 1] = { x = x, z = z, ring = false }
                end
            end
        end
    end
    local N = #tiles
    local index = {}
    for i, t in ipairs(tiles) do index[t.x * 65536 + t.z] = i end
    local mine = index[F.me.x * 65536 + F.me.z]
    if mine == nil then return nil end
    -- a run's reach (two tiles), per footprint position and tile set
    local rkey = b.x .. "," .. b.z .. "," .. N
    if S.kite_reach_key ~= rkey then
        local reach = {}
        for i = 1, N do
            reach[i] = {}
            for k = 1, N do
                local dx, dz = math.abs(tiles[i].x - tiles[k].x), math.abs(tiles[i].z - tiles[k].z)
                if dx <= 2 and dz <= 2 then reach[i][#reach[i] + 1] = k end
            end
        end
        S.kite_reach_key, S.kite_reach = rkey, reach
    end
    local reach = S.kite_reach
    local function sgn(v) return (v > 0 and 1) or (v < 0 and -1) or 0 end
    -- the webs by tile
    local haz = {}
    for _, h in ipairs(S.web_hazards) do
        local key = h.x * 65536 + h.z
        local l = haz[key]
        if l == nil then l = {} haz[key] = l end
        l[#l + 1] = h
    end
    -- a tile I may not END tick `t` on (the planner's terms for the same
    -- things: a web from a tick before it lands, a crab's blast within 2 of
    -- its 2x2, the ball's flight within 2 of its target), and what a tile
    -- costs (a crab's blast at 3, a flat 7)
    local function outside(p, x, z, size)
        local dx = math.max(x - p.x, 0, p.x - (x + size - 1))
        local dz = math.max(z - p.z, 0, p.z - (z + size - 1))
        return math.max(dx, dz)
    end
    local function webbed(p, t)
        local l = haz[p.x * 65536 + p.z]
        if l then
            for _, h in ipairs(l) do
                if t >= h.from and t <= h.to then return true end
            end
        end
        return false
    end
    local function blocked(p, t)
        if webbed(p, t) then return true end
        -- (the blast's flat 7 at 3 too: "There should be NO avoidable
        -- damage", owner 2026-10-12)
        for _, bl in ipairs(S.blasts) do
            if t >= bl.from and t <= bl.to and outside(p, bl.x, bl.z, V.CRAB_SIZE) <= V.CRAB_BLAST then return true end
        end
        if ball_anchor and t <= ball_until and cheb(p.x, p.z, ball_anchor.x, ball_anchor.z) <= 2 then return true end
        if p.ring and not exempt and scan_at(t) then return true end
        return false
    end
    local function cost(p, t)
        for _, bl in ipairs(S.blasts) do
            if t >= bl.from and t <= bl.to and outside(p, bl.x, bl.z, V.CRAB_SIZE) == V.CRAB_BLAST then return V.KITE_SWING_BONUS end
        end
        return 0
    end
    -- HER WEBS AT MY OWN TILES, before they are thrown: from the spin every
    -- tick-end I stand on draws a web four ticks on (`~tob_verzik_web_throw`
    -- reads my tile; QD.raid._vzp3_web_run steps on that clock), so a tile I
    -- ended a tick on in the throws' window is mine for WEB_STAY tick-ends,
    -- then a web's until it snaps. The tiles before now are the history.
    local throw0, throw1 = nil, nil
    if S.spin_at then throw0, throw1 = S.spin_at + V.WEB_FIRST_THROW - 1, S.spin_at + V.WEB_FIRST_THROW + V.WEB_THROWS - 2 end
    local function own_web(p, e, e0)
        -- standing on p at end e, having ended a tick on it at e0
        return throw0 ~= nil and e0 >= throw0 and e0 <= throw1 and e - e0 >= V.WEB_STAY and e - e0 <= V.WEB_STAY + V.WEB_LIFE + 1
    end
    S.kite_hist = S.kite_hist or {}
    local hist = {}
    for _, h in ipairs(S.kite_hist) do
        if F.tick - h.e <= V.WEB_STAY + V.WEB_LIFE + 1 then hist[#hist + 1] = h end
    end
    hist[#hist + 1] = { x = F.me.x, z = F.me.z, e = F.tick - 1 }
    S.kite_hist = hist
    -- SEVEN TICKS AHEAD (owner, 2026-10-12: "The tornado kiting should look
    -- up to 7 ahead. The planner should be able to plan a weapon attack on
    -- each of its weapon ticks"): a beam over my moves (stand, or run to a
    -- tile within two), the tornadoes stepped at each tile I end on, a path
    -- dropped the tick it ends on one; scored by the swings it stands for on
    -- my weapon's ticks, then by how far the tornadoes are at its end.
    -- (one tick ahead walked seats into a sandwich between two, all9 a4)
    local wt = QD.raid._vzp3_weapon_ticks(S)
    local since = F.tick - (S.kite_swing or -10)
    -- THE TICK, in the content's order (`~tob_verzik_tornado_tick` runs in
    -- the npc phase, before my step): a tornado standing on the tile I ended
    -- the last tick on touches; else it steps at that tile; then I step. So
    -- from a node (the tile I end on, the tornadoes after their step) the
    -- next tick kills the path if a tornado stands on my tile, steps them at
    -- my tile, and then takes my move.
    -- The tables once per tile per depth, not per beam child (the spin's
    -- webs are dozens of hazards: read per child, the kite spent the seat's
    -- whole instruction budget, xv29).
    local mine_at = {}
    for _, h in ipairs(hist) do
        local key = h.x * 65536 + h.z
        local l = mine_at[key]
        if l == nil then l = {} mine_at[key] = l end
        l[#l + 1] = h.e
    end
    local bad, toll, wet = {}, {}, {}
    for depth = 1, V.KITE_DEPTH do
        bad[depth], toll[depth], wet[depth] = {}, {}, {}
        for i, p in ipairs(tiles) do
            local t = F.tick + depth
            local own = false
            local l = throw0 and mine_at[p.x * 65536 + p.z]
            if l then
                for _, e0 in ipairs(l) do
                    if own_web(p, t, e0) then own = true break end
                end
            end
            bad[depth][i] = own or blocked(p, t)
            toll[depth][i] = cost(p, t)
            wet[depth][i] = webbed(p, t)
        end
    end
    -- THE TILES A MOVE MAY PASS: a run's two steps can go through any tile
    -- beside both ends, and in the webs her footprint is a hard npc's, so a
    -- run past her corner goes round it and a diagonal past it through the
    -- open side. Reading only the straight line's middle (inside her), a run
    -- went round through a web and stuck there for ten ticks, its tornado on
    -- it (xv34 sa t1490, varp 5754).
    local hard = S.webs_at ~= nil
    local function under(x, z) return x >= b.x and x < b.x + V.SIZE and z >= b.z and z < b.z + V.SIZE end
    local passes = {}
    local function passes_of(a, c)
        local key = a * 256 + c
        local l = passes[key]
        if l then return l end
        l = {}
        local pa, pc = tiles[a], tiles[c]
        local dx, dz = pc.x - pa.x, pc.z - pa.z
        if math.max(math.abs(dx), math.abs(dz)) == 2 then
            for x = pa.x - 1, pa.x + 1 do
                for z = pa.z - 1, pa.z + 1 do
                    if (x ~= pa.x or z ~= pa.z) and math.abs(pc.x - x) <= 1 and math.abs(pc.z - z) <= 1
                        and not (hard and under(x, z)) then
                        l[#l + 1] = { x = x, z = z }
                    end
                end
            end
        elseif dx ~= 0 and dz ~= 0 and hard then
            if under(pa.x + dx, pa.z) then l[#l + 1] = { x = pa.x, z = pa.z + dz } end
            if under(pa.x, pa.z + dz) then l[#l + 1] = { x = pa.x + dx, z = pa.z } end
        end
        passes[key] = l
        return l
    end
    local function wet_at(depth, x, z)
        local i = index[x * 65536 + z]
        if i then return wet[depth][i] end
        -- off the kite's tiles: the hazards themselves
        return webbed({ x = x, z = z }, F.tick + depth)
    end
    local beam = { { i = mine, ts = {}, since = since, swings = 0, depth = 0 } }
    for _, tn in ipairs(threats) do beam[1].ts[#beam[1].ts + 1] = { x = tn.x, z = tn.z } end
    for depth = 1, V.KITE_DEPTH do
        local nxt = {}
        for _, node in ipairs(beam) do
            local here = tiles[node.i]
            local dead = false
            local ts = {}
            for _, tn in ipairs(node.ts) do
                if depth > 1 and tn.x == here.x and tn.z == here.z then dead = true end
                ts[#ts + 1] = { x = tn.x + sgn(here.x - tn.x), z = tn.z + sgn(here.z - tn.z) }
            end
            if not dead then
                for _, k in ipairs(reach[node.i]) do
                    local p = tiles[k]
                    local ok = not bad[depth][k]
                    -- the tile a run passes: a web there sticks me (the
                    -- step that leaves it, `[walktrigger,tob_verzik_web_stuck]`)
                    if ok then
                        for _, m in ipairs(passes_of(node.i, k)) do
                            if wet_at(depth, m.x, m.z) then ok = false break end
                        end
                    end
                    -- a tile this path ended a tick on, in the throws' window
                    if ok and throw0 then
                        local a = node
                        while a and a.depth > 0 do
                            local q = tiles[a.i]
                            if q.x == p.x and q.z == p.z and own_web(p, F.tick + depth, F.tick + a.depth) then ok = false break end
                            a = a.parent
                        end
                    end
                    if ok then
                        local near = 99
                        for _, tn in ipairs(ts) do near = math.min(near, cheb(tn.x, tn.z, p.x, p.z)) end
                        if near >= 1 then
                            local stay = k == node.i
                            local swing = stay and p.ring and node.since + depth - 1 >= wt
                            local child = { i = k, ts = ts, swings = node.swings + (swing and 1 or 0),
                                            since = swing and (1 - depth) or node.since, parent = node, depth = depth,
                                            first = node.first or p, firststay = (depth == 1) and stay or node.firststay,
                                            firstswing = (depth == 1) and swing or node.firstswing,
                                            moved = (node.moved or 0) + (stay and 0 or 1),
                                            paid = (node.paid or 0) + toll[depth][k] }
                            child.score = child.swings * V.KITE_SWING_BONUS + math.min(near, V.KITE_FAR) * 10
                                - child.moved * 0.1 - child.paid
                            nxt[#nxt + 1] = child
                        end
                    end
                end
            end
        end
        if #nxt == 0 then break end
        table.sort(nxt, function(x, y) return x.score > y.score end)
        beam = {}
        for n = 1, math.min(#nxt, V.KITE_BEAM) do beam[n] = nxt[n] end
    end
    local top = beam[1]
    if top == nil or top.first == nil then return nil end
    -- a path that dies before the horizon is no plan: the planner's terms
    if top.depth < math.min(V.KITE_DEPTH, 2) then return nil end
    local best = top.first
    local decision = { x = best.x, z = best.z, soft = 0, lethal = 0 }
    if top.firststay and top.firstswing and S.target == b then
        decision.order = { mode = "attack" }
        S.kite_swing = F.tick
    else
        decision.order = { mode = "walk", x = best.x, z = best.z }
    end
    return decision
end

function QD.raid._vzp3_decide(S, F)
    local V = QD.VZP3
    local run = QD.raid._vzp3_web_run(S, F)
    if run then run.src = "r" return run end
    local kite = QD.raid._vzp3_kite(S, F)
    if kite then kite.src = "k" return kite end
    local spec, names = QD.raid._vzp3_spec(S, F)
    -- a mode's own constraints (raid_solve_verzik_hm.lua)
    if S.hm and S.hm.spec then S.hm.spec(S, F, spec, names) end
    local r, plan = api_drive.plan(spec)
    assert(r == "ok", "verzik_p3_solve: api_drive.plan answered " .. tostring(r))
    S.expanded = (S.expanded or 0) + (plan.expanded or 0)
    local p1 = plan.path[1]
    local why = plan.why
    if plan.lethal > 0 and (S.last_inf or -10) < F.tick - 5 then
        S.last_inf = F.tick
        -- name the term
        local kind, idx, k = tostring(why):match("(%a+)%[(%d+)%] k(%d+)")
        local nm = kind and names[kind == "zone" and "zones" or (kind == "forbid" and "forbid" or "chasers")][tonumber(idx)]
        QD.raid._vzp3_trace(S, F.tick, "no safe plan at " .. F.me.x .. "," .. F.me.z .. " (" .. tostring(nm or why) .. " k" .. tostring(k) .. ")")
    end
    local b, target = F.boss, S.target
    local decision = { x = p1.x, z = p1.z, mid = p1.mid, soft = plan.soft, lethal = plan.lethal }
    local stays = p1.x == F.me.x and p1.z == F.me.z
    local reach = target ~= nil and beside(p1.x, p1.z, b.x, b.z, V.SIZE)
    -- A BALL HOLD IS A WALK, never an attack. An attack order follows her:
    -- on the tick she steps, the server walks the attacker after her, off
    -- the tile the landing reads (se t996: the "next" raider stood beside
    -- the anchor with an attack order, she stepped, the server moved it two
    -- away for the read, and the target ate the 74). Inside the hold window
    -- the order that keeps the tile is the tile itself.
    local holding = false
    do
        local role = QD.raid._vzp3_ball_role(S, F)
        if role == "target" or role == "next" then
            local land = S.ball.land
            local read = (role == "target") and (land - 1) or ((F.pid < S.ball.cur) and land or (land - 1))
            holding = F.tick >= read - 2 and F.tick <= read + 1
        end
    end
    if stays then
        if reach and not holding then
            decision.order = { mode = "attack" }
        elseif holding or (S.order and S.order.mode == "attack") then
            -- an attack order under way would walk me: hold here
            decision.order = { mode = "walk", x = F.me.x, z = F.me.z }
        end
        return decision
    end
    local chased = false
    for _, tn in ipairs(F.tornadoes) do
        if tn.mine and cheb(tn.x, tn.z, F.me.x, F.me.z) <= 3 then chased = true end
    end
    if reach and not chased and not holding then
        -- the attack's own route takes this very step: press her instead of
        -- the tile, and the swing goes out on arrival (lesson 8).  Not with
        -- a tornado at my heels: a press the server refuses (she is
        -- invulnerable from the webs slot) stands me still, and the tornado
        -- stepped onto that stillness (sa t872)
        local rr, rt = api_drive.route(b.x, b.z, { run = true, size = V.SIZE })
        if rr == "ok" and rt.ticks and rt.ticks[1] and rt.ticks[1].x == p1.x and rt.ticks[1].z == p1.z then
            local mid_ok = true
            if p1.mid and rt.tiles and rt.tiles[1] and (rt.tiles[1].x ~= p1.mid.x or rt.tiles[1].z ~= p1.mid.z) then
                mid_ok = not QD.raid._vzp3_webbed(S, F, rt.tiles[1].x, rt.tiles[1].z)
            end
            if mid_ok then
                decision.order = { mode = "attack" }
                return decision
            end
        end
    end
    -- a two-step run: the server's own middle tile must be as safe as the
    -- planner's (a web there sticks)
    if p1.mid and #S.web_hazards > 0 then
        local rr, rt = api_drive.route(p1.x, p1.z, { run = true })
        if rr == "ok" and rt.tiles and rt.tiles[1] and (rt.tiles[1].x ~= p1.mid.x or rt.tiles[1].z ~= p1.mid.z)
            and QD.raid._vzp3_webbed(S, F, rt.tiles[1].x, rt.tiles[1].z) then
            decision.order = { mode = "walk", x = p1.mid.x, z = p1.mid.z }
            return decision
        end
    end
    decision.order = { mode = "walk", x = p1.x, z = p1.z }
    return decision
end

function QD.raid._vzp3_webbed(S, F, x, z)
    for _, h in ipairs(S.web_hazards) do
        if h.x == x and h.z == z and F.tick + 1 >= h.from and F.tick + 1 <= h.to then return true end
    end
    return false
end

-- ===================================================================== EMIT

function QD.raid._vzp3_held(S, obj, op)
    for slot = 0, 27 do
        local r, cell = api_drive.inv_slot(S.ids.inv, slot)
        if r == "ok" and cell.obj_id == obj then
            return api_drive.inv_op(S.ids.backpack, slot, obj, cell.count, op)
        end
    end
    return "not_found"
end

function QD.raid._vzp3_first(S, list)
    for _, obj in ipairs(list) do
        local _, n = api_drive.inv_count(S.ids.inv, obj)
        if (n or 0) > 0 then return obj end
    end
    return nil
end

function QD.raid._vzp3_same(a, b)
    if a == nil or b == nil then return a == b end
    if a.mode ~= b.mode then return false end
    if a.mode == "attack" then return true end
    return a.x == b.x and a.z == b.z
end

function QD.raid._vzp3_emit(S, F, order, intent)
    -- Each channel in one tick, in order: the prayer (its tab, then the press),
    -- the backpack (its tab, then the held op), then the interaction.  A tab is
    -- the client's own and instant, like an F-key: inputs queue in call order
    -- and the server takes them together at the next tick (the owner,
    -- 2026-10-08: "Prayer should be a free action").
    -- ONE PANEL CHANNEL A TICK, THE PRESS IN THE TICK ITS TAB IS SWITCHED:
    -- a tab is instant (the owner, 2026-10-09: "Interface tab changes do not
    -- need to wait for a tick boundary"). The live plugin lays the panel out
    -- inside the switch now (app_plugin_tab_select), so a tab-and-press in
    -- one resume lands on both lanes; before, live refused the press on a
    -- panel it had not laid out and the overhead lit a tick late, racing the
    -- landing. The overhead wins the channel; a bite or a sip waits a tick.
    local held = intent.eat or intent.drink or intent.gear
    local switched = false
    if intent.pray and (S.pray_trace or 0) < 12 then
        S.pray_trace = (S.pray_trace or 0) + 1
        QD.raid._vzp3_trace(S, F.tick, string.format("pray %s wanted (tab %s, last press t%s, lit m%s r%s)", intent.pray,
            tostring(S.tab == S.ids.prayer_tab and "prayer" or S.tab), tostring(S.pray_clicked[intent.pray]), tostring(F.lit.magic), tostring(F.lit.missiles)))
    end
    if intent.pray and F.tick - (S.pray_clicked[intent.pray] or -10) >= 2 then
        if S.tab ~= S.ids.prayer_tab then
            api_drive.tab(S.ids.prayer_tab)
            S.tab = S.ids.prayer_tab
        end
        -- a press the client refuses is not a press: it is retried next
        -- tick, and counted
        local pr = api_drive.if_click(S.ids[intent.pray], 1)
        if (S.pray_trace or 0) <= 12 then QD.raid._vzp3_trace(S, F.tick, "press " .. intent.pray .. " -> " .. tostring(pr)) end
        if pr == "ok" then
            S.pray_clicked[intent.pray] = F.tick
            S.presses = S.presses + 1
        else
            S.press_refused = (S.press_refused or 0) + 1
            if S.press_refused <= 5 then QD.raid._vzp3_trace(S, F.tick, "prayer press refused: " .. tostring(pr)) end
        end
    elseif held and S.tab ~= S.ids.inv_tab then
        api_drive.tab(S.ids.inv_tab)
        S.tab = S.ids.inv_tab
    end
    if held and S.tab == S.ids.inv_tab and not switched and not intent.pray then
        if intent.eat then
            local er = QD.raid._vzp3_held(S, intent.eat, 1)
            if er == "ok" then S.eats = S.eats + 1 else S.op_refused = (S.op_refused or 0) + 1; S.last_eat = nil
                if S.op_refused <= 5 then QD.raid._vzp3_trace(S, F.tick, "eat refused: " .. tostring(er)) end end
        end
        if intent.drink then
            local dr = QD.raid._vzp3_held(S, intent.drink, 1)
            if dr == "ok" then S.drinks = S.drinks + 1 else S.op_refused = (S.op_refused or 0) + 1; S.last_drink = nil
                if S.op_refused <= 5 then QD.raid._vzp3_trace(S, F.tick, "drink refused: " .. tostring(dr)) end end
        end
        if intent.gear then QD.raid._vzp3_held(S, intent.gear, 2) end
        -- a held op ends the interaction (the walk goes on): an attack under
        -- way is pressed again below
        if S.order and S.order.mode == "attack" then
            S.order = nil
            if order == nil then order = { mode = "attack" } end
        end
    else
        -- the intent waits: its clocks must not count this tick as done
        if intent.eat then S.last_eat = nil end
        if intent.drink then S.last_drink = nil end
    end
    if order and not QD.raid._vzp3_same(order, S.order) then
        if order.mode == "walk" then
            api_drive.move_to(order.x, order.z)
        else
            local b = F.boss
            local r = api_drive.world_op("npc", b.npc_id, 2, b.element_id)
            if r == "no_row" then api_drive.world_op("npc", b.npc_id, 1, b.element_id) end
        end
        S.order = order
        S.clicks = S.clicks + 1
    end
end

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

function QD.raid.verzik_p3_solve(opts)
    opts = opts or {}
    assert(opts.base, "verzik_p3_solve: opts.base (the room origin, verzik_p1_prepare's) is required")
    local V = QD.VZP3
    local S = {
        ids = QD.raid._vzp3_ids(opts.weapon),
        weapon_ticks_opt = opts.weapon_ticks,
        base = opts.base,
        role = (QD_PARTY and QD_PARTY.role) or 1,
        seen = {}, web_seen = {}, web_hazards = {}, torn = {}, crab_seen = {}, blasts = {},
        trace = {}, hits = {}, recent = {}, pray_clicked = {},
        autos = 0, melees = 0, eats = 0, drinks = 0, clicks = 0, presses = 0,
        prot = "magic", hm = opts.hm,
    }
    local start = api_drive.tick()
    local last_hp = nil
    while true do
        local F = QD.raid._vzp3_measure(S)
        -- the wake trace (the first forty wakes after her form): the client
        -- tick, the server's counter, the await's answer and whether this
        -- wake decides -- the live lane wakes about twice a tick and this
        -- says which wake is which
        -- ... and WHAT THE WAKE SEES: her server tile and my hitpoints,
        -- which the tick log also carries (npc_tile, raider), so a wake can
        -- be matched to the tick-log tick it perceives (2026-10-09: the live
        -- seat perceived every tick one late)
        if S.entered and (S.wakes or 0) < 120 then
            S.wakes = (S.wakes or 0) + 1
            local _, srv = api_drive.server_tick()
            QD.raid._vzp3_trace(S, F.tick, string.format("wake srv=%s await=%s %s @%d,%d her %s,%s hp %s", tostring(srv), tostring(S.last_await),
                F.tick == S.decided_tick and "repeat" or "decide", F.me.x, F.me.z,
                tostring(F.boss and F.boss.x), tostring(F.boss and F.boss.z), tostring(F.hp)))
        end
        if F.tick == S.decided_tick then
            -- a second wake in a tick already decided (the live client): the
            -- clicks are out, the next tick's packets are not in
            S.last_await = await({ event = "server_tick", match = function() return true end,
                note = "verzik_p3_solve: a repeated wake" }, 3)
            goto continue
        end
        S.decided_tick = F.tick
        if F.tick - start > (opts.max_ticks or 900) then return "timeout", QD.raid._vzp3_summary(S), S end
        if F.hp <= 0 then return "died", QD.raid._vzp3_summary(S), S end
        if F.bat or (S.entered and F.boss == nil and F.transition == nil) then
            return "ok", QD.raid._vzp3_summary(S), S
        end
        if last_hp and F.hp < last_hp then
            S.hits[#S.hits + 1] = "t" .. F.tick .. " -" .. (last_hp - F.hp)
            S.taken = (S.taken or 0) + (last_hp - F.hp)
        end
        last_hp = F.hp
        local intent = {}
        -- The panel channel's priorities: prayer points first (with none, no
        -- overhead lights and every press is wasted), then food, then the
        -- overhead against the style she last showed, then Piety only with
        -- points to spare.
        -- THE TRANSITION TOPS UP. Nothing attacks while she rises, so a seat
        -- eats (or brews, with no fish) back to HP_TOP before her first P3
        -- auto. The zap's hops chip 3-7 off every raider that passes the ball
        -- on, and seats came into P3 at 52-73 and died there (relay rl43 rd,
        -- rn).
        local topping = F.transition ~= nil and F.boss == nil
        local P = QD.VZ_PRAYER
        local remaining = math.max(60, P.P3_TICKS - (S.entered and (F.tick - S.entered) or 0))
        local spare = QD.raid.vz_prayer_spare(S.ids.inv, F.prayer, remaining)
        S.piety_ok = spare > 0
        local restore_due = F.prayer < V.PRAYER_SIP and F.tick - (S.last_drink or -10) >= 3
        -- (a mode's own floors: raid_solve_verzik_hm.lua, Hard Mode's autos)
        local hp_eat = (S.hm and S.hm.hp_eat) or V.HP_EAT
        local hp_brew = (S.hm and S.hm.hp_brew) or V.HP_BREW
        local eat_due = F.hp < (topping and V.HP_TOP or hp_eat) and F.tick - (S.last_eat or -10) >= 3
        if restore_due then
            intent.drink = QD.raid._vzp3_first(S, S.ids.restores)
            if intent.drink then S.last_drink = F.tick end
        end
        if not intent.drink and eat_due then
            local food = QD.raid._vzp3_first(S, S.ids.foods)
            if food then
                intent.eat = food
                S.last_eat = F.tick
            elseif topping or F.hp < hp_brew then
                intent.drink = QD.raid._vzp3_first(S, S.ids.brews)
                if intent.drink then S.last_drink = F.tick end
            end
        end
        if F.prayer > 0 and not F.lit[S.prot] then intent.pray = S.prot end
        if not intent.drink and not intent.eat and F.tick - (S.last_drink or -10) >= 3 then
            -- (a drain takes a super restore only: a prayer potion restores
            -- no stat, and every brew drains attack, so a seat out of super
            -- restores sipped its prayer potions dry, relay rl21)
            -- TWO BREWS' DRAIN, NOT ONE: a brew dose drains about 12 levels and
            -- a super restore gives back about 32, so a restore after every
            -- brew threw away 60% of it (rl60 ta: a restore three ticks after
            -- each of six brews). Never skipped, though: with the stats
            -- drained the raiders' hits on her fell from 19 to 1 and P3 never
            -- ended (rl60 tk, dead of attrition 700 ticks in).
            if F.attack <= F.attack_base - V.STAT_DRAIN then
                intent.drink = QD.raid._vzp3_first(S, S.ids.stat_restores)
            elseif F.attack <= F.attack_base + V.COMBAT_REDOSE then
                intent.drink = QD.raid._vzp3_first(S, S.ids.combats)
            end
            if intent.drink then S.last_drink = F.tick end
        end
        -- PIETY ON THE BUDGET (QD.VZ_PRAYER): on while the spare covers it to
        -- the end of the fight, OFF when it stops covering it
        if not intent.pray and (S.pray_clicked.piety == nil or F.tick - S.pray_clicked.piety > 6) then
            if F.lit.piety and not S.piety_ok then
                intent.pray = "piety"
            elseif not F.lit.piety and S.piety_ok and F.prayer >= V.PIETY_FLOOR then
                intent.pray = "piety"
            end
        end
        if F.weapon_held and F.tick - (S.weapon_tried or -10) >= 3 then
            intent.gear = S.ids.weapon
            S.weapon_tried = F.tick
        end
        if F.boss then
            -- the raiders still in the fight: a dead raider's seat is in the
            -- spectator cage, and counting it broke the ball's chain
            local A, base = V.ARENA, opts.base
            local alive = {}
            for _, rd in ipairs(F.raiders) do
                local lx, lz = rd.x - base.x, rd.z - base.z
                if lx >= A.x0 and lx <= A.x1 and lz >= A.z0 and lz <= A.z1 then alive[#alive + 1] = rd end
            end
            F.raiders = alive
            QD.raid._vzp3_projectiles(S, F)
            QD.raid._vzp3_clock(S, F)
            QD.raid._vzp2_crabs(S, F)
            QD.raid._vzp3_tornadoes(S, F)
            -- (a mode's own pools: raid_solve_verzik_hm.lua, Hard Mode's three a raider)
            if S.hm and S.hm.pools then S.hm.pools(S, F) else QD.raid._vzp3_pools(S, F) end
            S.target = QD.raid._vzp3_target(S, F)
            local d = QD.raid._vzp3_decide(S, F)
            QD.raid._vzp3_emit(S, F, d.order, intent)
            S.recent[#S.recent + 1] = string.format("t%d @%d,%d>%d,%d %s L%d c%.1f %s", F.tick, F.me.x, F.me.z,
                d.x, d.z, d.order and (d.order.mode:sub(1, 1) .. (d.order.x and (d.order.x .. "," .. d.order.z) or "")) or "-", d.lethal, d.soft,
                d.src or "p")
            if #S.recent > V.RECENT then table.remove(S.recent, 1) end
        else
            -- the six-tick change of form: nothing to dodge; supplies and the
            -- prayer book only
            QD.raid._vzp3_emit(S, F, nil, intent)
        end
        S.last_await = await({ event = "server_tick", match = function() return true end,
            note = "verzik_p3_solve: the tick's packets applied" }, 3)
        ::continue::
    end
end

-- The ids the test's tick-log measures read (a test file has no api_drive).
function QD.raid.verzik_p3_symbols()
    local function sym(kind, name)
        if kind == "npc" then name = QD.raid.tob_npc_name(name) end
        local r, id = api_drive.symbol(kind, name)
        assert(r == "ok", "verzik_p3_symbols: no " .. kind .. " named " .. name)
        return id
    end
    return {
        p1 = sym("npc", "verzik_phase1"), p2 = sym("npc", "verzik_phase2"), p3 = sym("npc", "verzik_phase3"),
        bat = sym("npc", "verzik_death_bat"), tornado = sym("npc", "tob_verzik_creeper"),
        crabs = sym("seq", "verzik_phase3_attack_summon"), webs = sym("seq", "verzik_phase3_attack_webspin"),
        yellows = sym("seq", "verzik_phase3_attack_powerblast"), ball = sym("spotanim", "verzik_acidbomb_projanim"),
    }
end

-- The planner against the client's own pathfinder: for every two-step run
-- from here, the middle tile the planner assumes (the straight step along
-- the dominant axis first) must be the route's first step.  Returns the
-- number of offsets compared and a list of disagreements.
function QD.raid.vzp3_plan_check()
    local tr, me = api_drive.player_tile()
    assert(tr == "ok", "vzp3_plan_check: no tile")
    local _, now = api_drive.server_tick()
    local n, bad = 0, {}
    for dx = -2, 2 do
        for dz = -2, 2 do
            if math.max(math.abs(dx), math.abs(dz)) == 2 then
                local gx, gz = me.x + dx, me.z + dz
                local rr, rt = api_drive.route(gx, gz, { run = true })
                local pr, plan = api_drive.plan({ now = now or 0, h = 1, beam = 4,
                    pulls = { { x = gx, z = gz, size = 1, weight = 10 } } })
                if rr == "ok" and pr == "ok" and rt.tiles and #rt.tiles == 2 and plan.path[1].x == gx and plan.path[1].z == gz then
                    n = n + 1
                    local mid = plan.path[1].mid
                    if mid == nil or mid.x ~= rt.tiles[1].x or mid.z ~= rt.tiles[1].z then
                        bad[#bad + 1] = string.format("%d,%d: route via %d,%d, plan via %s", dx, dz, rt.tiles[1].x, rt.tiles[1].z,
                            mid and (mid.x .. "," .. mid.z) or "nil")
                    end
                end
            end
        end
    end
    return n, bad
end

-- A planner probe for a test file: the spec as given, the answer as given.
function QD.raid.vzp3_plan_probe(spec)
    local r, plan = api_drive.plan(spec)
    return r, plan
end

function QD.raid._vzp3_summary(S)
    local seen = {}
    for _, k in ipairs({ "crabs", "webs", "yellows", "ball" }) do seen[#seen + 1] = k .. " " .. tostring(S.seen[k] or 0) end
    return string.format("p%d: taken %d in %d hits; specials %s; autos %d (melee %d); tornado spawns %d; eats %d, drinks %d, clicks %d, presses %d (refused %d, ops refused %d); plan nodes %d; trace %s",
        S.role, S.taken or 0, #S.hits, table.concat(seen, ", "), S.autos, S.melees, S.tornado_spawns or 0, S.eats, S.drinks, S.clicks,
        S.presses, S.press_refused or 0, S.op_refused or 0, S.expanded or 0, table.concat(S.trace, " | ")) .. "; recent " .. table.concat(S.recent, " ")
end
