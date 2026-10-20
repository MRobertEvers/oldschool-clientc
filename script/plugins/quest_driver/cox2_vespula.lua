-- quest-driver / cox2_vespula: VESPULA, solo or a party, on the v2 layers.
--
-- docs/minigames/cox/rooms/vespula_solver.md is the plan, vespula_spec.md the
-- mechanics (rows Vn); docs/CONTENT_SOLVER_MANUAL.md the method. A QD.T2 room
-- (tob2_run.lua): PREDICT and STRATEGY are here; the planner (api_drive.aplan),
-- the executive, the monitor and the supplies are the shared layers.
--
-- THE METHOD (the Redemption method: wiki Strategies, Synq 1:25:34, The
-- Academy): through the septic tendrils, attack the abyssal portal at once so
-- Vespula hovers enraged in front of it and stops stinging her grubs; from a
-- SAFE tile two off her footprint on her rows/columns, step under her to swing
-- at the portal (the stomp, 2-8 a tick, V13) and back; Redemption (lit, with
-- prayer) turns the stomps into heals; a restore after every Redemption. Never
-- on her edge ring (the sting, 5-20, V14), and steps off a portal drain's tile
-- before it lands (the drain comes at me, its target). A grub she tagged
-- before the enrage hatches ~69 ticks on (V8, RSProx 820): when the
-- portal will not die first, a root run in an enrage window feeds it full.
--
-- THE INTENDED SOLVE (opts.method = "intended", a party: test/raids/
-- cox_vespula_duo.lua; Synq 1:36:20, "how players were initially intended to
-- attack the portal without Vespula being enraged"): shoot HER, from out of
-- her reach, to 20%, where each hit may land her (V5); grounded 33 ticks (V6)
-- every seat shoots the portal. Enraged she tags no grub, so the ENRAGE seat
-- (opts.role "enrage") opens on the portal and re-hits it inside each renewal
-- window, shooting her between; she re-enrages as she takes off. (The kit's
-- crossbow reaches the portal, 9 on Longrange, only from under her or her
-- reach: the enrage seat pays a stomp or two a refresh, never her ring.) The FEED
-- seat (opts.role "feed") feeds every grub she tagged back to full from the
-- roots -- full stops its drain, and enraged or grounded she cannot tag it
-- again -- and shoots her between. Protect from Missiles against her ranged
-- (grounded, she shoots).
--
-- Perception: npc rows (types, tiles, health bars, splats), locs (tendrils,
-- boils, roots), my stats and prayers -- what a client is sent.
-- ==========================================================================

QD.T2 = QD.T2 or {}
QD.T2.rooms = QD.T2.rooms or {}
QD.COXV = QD.COXV or {}
local CV = QD.COXV

CV.V = {
    H = 10, BEAM = 32,
    -- template room-local tables (vespula_spec.md section 2), variant 0 ccw,
    -- 1 thru, 2 cw
    HOME = { [0] = { 15, 8 }, [1] = { 10, 10 }, [2] = { 11, 8 } },
    PORTAL = { [0] = { 17, 20 }, [1] = { 22, 11 }, [2] = { 11, 20 } },
    HIT = { [0] = { { 17, 13 }, { 18, 13 } }, [1] = { { 15, 13 }, { 15, 12 } }, [2] = { { 12, 13 }, { 13, 13 } } },
    -- SYNQ'S TILES (his tile markers, pastebin JugTh0FG linked from the
    -- guide's description: RuneLite region tiles 13138 (13,44) ccw, (46,40)
    -- thru, 13394 (17,44) cw, template room-local here): two tiles off her
    -- hover on her edge row (column) nearest the portal. The run in goes
    -- along that row under her -- her ring skipped, two tiles a tick -- to
    -- the tiles in line with the cove, the only ones under her that reach the
    -- portal (probe 2026-10-18: from the corner the server walks on, from the
    -- cove column it fires in place), and back: a stomp or three a swing,
    -- never her sting ("Vespula will only damage you up to eight", SQ
    -- 1:29:35; W Strategies "6-24 damage for every attack").
    SYNQ = { [0] = { 13, 12 }, [1] = { 14, 8 }, [2] = { 17, 12 } },
    GRUBS = {
        [0] = { { 22, 8 }, { 20, 13 }, { 12, 5 }, { 13, 13 } },
        [1] = { { 15, 8 }, { 7, 10 }, { 15, 16 }, { 7, 16 } },
        [2] = { { 15, 13 }, { 8, 5 }, { 8, 13 }, { 5, 9 } },
    },
    TENDRIL_LZ = 1,
    -- the mechanics (spec V12-V18)
    -- RECORDED (RSProx 820, 843, 3389; docs/rsprox-analysis/cox/vespula.md):
    -- enraged the tick after an attack on the portal, held 20 ticks a cycle and
    -- renewed (after one flying tick) while the portal was attacked within 9;
    -- a sting takes a grub to 56% (70 hp) and it drains 1 a tick to a hatch;
    -- a portal drain lands 2 ticks after its throw on the tile thrown at.
    ENRAGE = 20, DRAIN_WINDOW = 9, STOMP_EXP = 5, STOMP_MAX = 8, STING_EXP = 7, STING_MAX = 20,
    HATCH = 69, STUNG_HP = 70, BLOSSOM = 35, GRUB_HP = 125, PORTAL_HP = 250, FLIGHT = 3,
    -- her renewal window: an attack in the RENEW ticks up to her cycle's end
    -- (the 9-tick "under attack" read on the tick after it) keeps her enraged
    RENEW = 8, RUN_MIN = 8, RUN_HP = 40,
    -- her ranged (V3): every 3 ticks while she flies unenraged or stands grounded
    RANGED_EXP = 4, RANGED_MAX = 14, CALM_BREW = 40, FEED_STALL = 8,
    -- the pre-pot: hp brought to this before the overload (it takes 50)
    PREPOT_HP = 75, POISON_PERIOD = 30,
    -- the currency: a swing at the portal in hp (it must outweigh two ticks of
    -- stomp); the pull to the safe tile a tick away from it
    SWING_VALUE = 40, SAFE_PULL = 2, SAFE_NEAR = 3,
    -- THE GROUND METHOD (the solo "no supplies" way, yt_3HMsjFlJ7aA; V5/V6):
    -- from the safe tile shoot HER while she hovers enraged; at <= 20% each
    -- hit lands her (1 in 2) for 33 ticks, when she stomps no one and shoots
    -- only raiders not under or beside her: the portal from under her then,
    -- free. The portal is re-hit (one trip under her) only to keep the
    -- enrage (REFRESH ticks before it lapses), so she tags no grubs.
    -- (NOT the default: tried round 4 on the prep kit, 1 of 8 room seeds
    -- green, 128-650 taken -- she lands rarely before the soldiers' heals,
    -- and every lapse of the enrage tags grubs. opts.method = "ground".)
    METHOD = "redemption", HER_VALUE = 30, HER_LOW_VALUE = 45, GROUND_PCT = 20, GROUND_TICKS = 33,
    REFRESH = 10, REFRESH_VALUE = 120, HEAL_FLOOR = 20, PREPOT_OVL_HP = 85,
    -- the intended solve: hp kept over a ranged hit and a stomp; a flying
    -- soldier (it heals the portal, V11) shot before her
    DUO_BREW = 45, SOLDIER_VALUE = 60,
    -- the feeder: while she can sting, every tagged grub kept over KEEP_HP
    -- (a blossom a grub every ~35 ticks); while she cannot (grounded with
    -- CALM_LEFT ticks to go, or enraged), fed to full, which stops its drain;
    -- TRIP_DRAIN ticks of drain counted on top for the walk
    KEEP_HP = 45, CALM_LEFT = 8, TRIP_DRAIN = 6, FEED_RETRY = 20, NO_TILE_RETRY = 3,
    -- the weapon (both the kit's, both on Longrange: +2, 9 tiles): the dragon
    -- crossbow (6 ticks, Rigour) or the trident of the swamp (4 ticks, Augury;
    -- the portal's magic defence bonus is its lowest, wiki)
    WEAPON = "xbow",
    WEAPONS = {
        -- (rate: its damage a tick on the portal in Synq's loop, for the
        -- fallback's predicted death -- MEASURED on the budget kit, probe p1
        -- 2026-10-19, seeds 0/5/10 (cw, ccw, thru): the crossbow + ruby bolts
        -- (e) 2.98-3.45 a tick, 20% zeros, kill 70-94 ticks from the first
        -- hit; the trident 0.36-1.20, 50-85% zeros, and the raider died on
        -- the stomps before the portal (section 13). stat: the one its
        -- pre-pot and restores keep up)
        xbow = { set = "ranged_set", obj = "xbow", range = 9, speed = 6, boost = "rigour", slot = "style_slot_2",
                 rate = 3.0, stat = "ranged" },
        trident = { set = "magic_set", obj = "trident", range = 9, speed = 4, boost = "augury", slot = "style_slot_3",
                    rate = 1.0, stat = "magic" },
        -- Tumeken's shadow, Synq's "best weapon to use on the portal": attack
        -- rate 5, range 8 in our data, which reaches the portal from the lane
        -- "at any setting" (W Abyssal portal) -- no style to set; the solo
        -- test wears it with the max magic set (::maxmage)
        -- (rate: its expected damage a tick on the portal -- 82% to hit at
        -- Augury, 27 an average hit (max ~54), a swing every 5 ticks;
        -- measured median kill 50 ticks for 250, sweep vsh4)
        shadow = { set = "shadow_set", obj = "shadow", range = 8, speed = 5, boost = "augury", rate = 4.4,
                   stat = "magic" },
    },
    -- Redemption ("over 10 HP before we click the portal", The Academy): a
    -- stomp's max is 8, so at 9 or more a stomp cannot kill and Redemption
    -- fires on it; Xeric's aid only under this
    REDEMPTION_HP = 9, REDEEM_PRAYER = 6, BREW_BELOW = 8, EAT_BELOW = 0, ROOT_FINISH = 15,
    -- (Redemption alone drains a point in ~10 ticks, not ~2: with the boost
    -- dark, REDEEM_BARE points outlast a trip under her. At 0-5 points --
    -- an enhance's trickle after a fire -- the plan had priced every stomp
    -- lethal and rescued with a brew each ~23 ticks, 30 brews draining
    -- Ranged to 74% zeros, raid45 r27; section 16 E5)
    REDEEM_BARE = 2,
    -- prayer: an enhance (1 point a ~9 ticks for ~450 ticks, cox_potion.rs2)
    -- keeps Redemption relit after it fires; Rigour drains faster than that,
    -- so it is lit only over BOOST_ON points and let go under BOOST_OFF
    ENHANCE_TICKS = 450, ENHANCE_BELOW = 60, BOOST_ON = 25, BOOST_OFF = 10, FEED_PULL = 6,
    -- a root run: when a tagged grub's hatch comes before the portal's death
    -- (a portal dying within ROOT_MARGIN of the hatch is finished instead: one
    -- soldier's heal costs less than the run, and the feed tile may be her edge)
    -- (a tagged grub hatches ~69 ticks after the sting: the run may start at
    -- once, a blossom fed as soon as it is held -- each one is 35 ticks)
    ROOT_LEAD = 70, ROOT_MARGIN = 20, ROOT_EARLY = 40, ROOT_RUN = 25, ROOT_MAX = 6,
    -- the fallback root run under the Redemption method: the way back
    -- (his tile, the lane, the swing), a pick, the margin a held blossom
    -- keeps over its grub's hp, and the brew floor once no prayer is left
    LANE_TICKS = 3, PICK_TICKS = 3, HATCH_MARGIN = 5, FEED_MARGIN = 6, YIELD_MARGIN = 3, DRY_BREW = 30,
    -- the budget kit's run (section 13): margin a fed grub keeps over the
    -- portal's predicted death, and the portal ticks a blossom's trip costs
    OUTLAST_MARGIN = 15, RUN_SLOW = 15,
    -- the root walk (section 14 T5): ticks beside a root without a pick, or
    -- past the route's ticks on the way, before it is skipped for ROOT_BAD
    ROOT_STALL = 5, ROOT_BAD = 40,
    -- (section 16: a root trip that does not fit before the renewal window
    -- closes waits for the renewal swing, unless its grub hatches within this)
    TRIP_FORCE = 30,
    -- (section 16 E6: a held feed lets the loop shoot until this many ticks,
    -- plus the walk to the feed tile, before her cycle's end - 2)
    FEED_LOOP = 6,
    -- (section 16 E7: ticks to a hatch under which no trip can feed it)
    TRIP_MIN = 12,
    FAR = 9,
    -- the room's end: hp brewed back to this before the solver returns
    POST_HP = 50,
    -- run energy (hundredths) the Redemption method waits for outside the
    -- tendrils (section 15: the relay arrived on 0-34% and lost)
    ENTER_ENERGY = 9500,
}

-- ======================================================================== IDS

function CV.ids(opts)
    local o2 = {}
    for k, v in pairs(opts or {}) do o2[k] = v end
    o2.prayers = { "protectfrommagic", "protectfrommissiles", "protectfrommelee", "piety", "rigour", "augury",
                   "redemption" }
    local ids = QD.raid._tob_common_ids("cox_vespula", o2)
    local room = QD.raid._tob_symbols("cox_vespula", {
        { "flying", "npc", "raids_vespula_flying" }, { "enraged", "npc", "raids_vespula_enraged" },
        { "walking", "npc", "raids_vespula_walking" }, { "portal", "npc", "raids_vespula_portal" },
        { "grub_healthy", "npc", "raids_vespula_caterpillar_healthy" },
        { "grub_sickly", "npc", "raids_vespula_caterpillar_sickly" },
        { "grub_infected", "npc", "raids_vespula_caterpillar_infected" },
        { "grub_dead", "npc", "raids_vespula_caterpillar_dead" },
        { "soldier", "npc", "raids_vespula_vespine_flying" }, { "soldier_down", "npc", "raids_vespula_vespine_walking" },
        { "tendril", "loc", "raids_vespula_tendrils_entrance" },
        { "drain", "spotanim", "raids_vespula_portal_attack" },
        { "boil", "loc", "raids_vespula_boil_blocking" }, { "boil_burst", "loc", "raids_vespula_boil_burst" },
        { "root", "loc", "raids_vespula_herb" }, { "root_empty", "loc", "raids_vespula_herb_empty" },
        { "blossom", "obj", "raids_vespula_herb" }, { "vial_empty", "obj", "raids_vial_empty" },
        { "xbow", "obj", "xbows_crossbow_dragon" }, { "assembler", "obj", QD.COX.ava_name() },
        { "dhide_body", "obj", "black_dragonhide_body" }, { "chaps", "obj", "black_dragonhide_chaps" },
        { "fury", "obj", "enchanted_onyx_amulet" }, { "defender", "obj", "dragon_parryingdagger" },
        { "com_mode", "varp", "varp43_com_mode" },
        { "style_slot_2", "component", "combat_interface:style_slot_2" },
        { "style_slot_3", "component", "combat_interface:style_slot_3" },
        { "poison", "varp", "varp102_poison" },
        { "trident", "obj", "toxic_tots_charged" }, { "shadow", "obj", "tumekens_shadow" }, { "occult", "obj", "occult_necklace" },
        { "imbued_cape", "obj", "ma2_saradomin_cape" }, { "mystic_top", "obj", "mystic_robe_top" },
        { "mystic_bottom", "obj", "mystic_robe_bottom" },
    })
    for k, v in pairs(room) do ids[k] = v end
    local tr, combat_tab = api_drive.tab_by_name("combat")
    assert(tr == "ok", "cox_vespula: no combat tab")
    ids.combat_tab = combat_tab
    ids.ranged_set = { ids.xbow, ids.fury, ids.assembler, ids.dhide_body, ids.chaps, ids.defender }
    ids.magic_set = { ids.trident, ids.occult, ids.imbued_cape, ids.defender, ids.mystic_top, ids.mystic_bottom }
    ids.shadow_set = { ids.shadow }
    local function vials(stem)
        local out = {}
        for n = 1, 4 do
            local r, id = api_drive.symbol("obj", stem .. n)
            assert(r == "ok", "cox_vespula: no obj " .. stem .. n)
            out[#out + 1] = id
        end
        return out
    end
    -- the raid's (+) potions: Xeric's aid the brew, revitalisation the restore
    -- (prayer after a Redemption), prayer enhance the room's own top-up
    ids.brews = QD.COX.supply_ids("heal")
    ids.restores = QD.COX.supply_ids("restore")
    ids.stat_restores = ids.restores
    -- (the prayer family's first twelve: the raid's enhances, a point a
    -- prayer tick for ~450 ticks; a prayer potion or restore is not one)
    ids.enhances = {}
    for i, id in ipairs(QD.COX.supply_ids("prayer")) do
        if i <= 12 then ids.enhances[#ids.enhances + 1] = id end
    end
    ids.overloads = QD.COX.supply_ids("overload")
    -- (the last dose of each, by family: drunk, its vial smashes -- the
    -- root run's slot when the pack has none, section 16 E4. supply_ids
    -- lists each potion's doses 1-4, so dose 1 is every fourth from the first)
    local function ones(list)
        local out = {}
        for i, id in ipairs(list) do if i % 4 == 1 then out[#out + 1] = id end end
        return out
    end
    ids.one_restores, ids.one_enhances, ids.one_brews = ones(ids.restores), ones(ids.enhances), ones(ids.brews)
    ids.combats, ids.rangings = {}, {}
    -- the kit's ranging potion (wiki Minimum): the crossbow's pre-pot
    for d = 1, 4 do
        local r, id = api_drive.symbol("obj", d .. "doserangerspotion")
        if r == "ok" then ids.rangings[#ids.rangings + 1] = id end
    end
    -- the cure carried (the relay's antidote++, a sanfew, an antipoison):
    -- her ranged poisons 1 in 3, and a splat Redemption cannot save
    ids.antidotes = {}
    for _, stem in ipairs({ "antidote++%d", "antidote+%d", "sanfew_salve_%d_dose", "%ddose2antipoison", "%ddoseantipoison" }) do
        for d = 1, 4 do
            local r, id = api_drive.symbol("obj", string.format(stem, d))
            if r == "ok" then ids.antidotes[#ids.antidotes + 1] = id end
        end
    end
    ids.food = {}
    -- the kit's anglerfish (wiki Minimum): eaten before the tendrils, the
    -- wiki's "prepot ... health" for Vespula -- the overheal is the loop's
    -- buffer, and its slot the root run's (the full budget kit has none)
    ids.prepot_food = {}
    local fr, fish = api_drive.symbol("obj", "anglerfish")
    if fr == "ok" then ids.prepot_food[1] = fish end
    return ids
end

-- ===================================================================== GEOMETRY

-- The room's world tiles from the template tables, once its room is known.
function CV.geometry(S, room)
    local v, V = S.v, CV.V
    v.room = room
    local h = V.HOME[room.variant]
    v.home = {}
    v.home.x, v.home.z = QD.COX.world_sized(room, h[1], h[2], 5, 5)
    local p = V.PORTAL[room.variant]
    v.portal_sw = {}
    v.portal_sw.x, v.portal_sw.z = QD.COX.world_sized(room, p[1], p[2], 4, 4)
    v.hit = {}
    for i, t in ipairs(V.HIT[room.variant]) do
        local x, z = QD.COX.world(room, t[1], t[2])
        v.hit[i] = { x = x, z = z }
    end
    v.grub_sw = {}
    for i, t in ipairs(V.GRUBS[room.variant]) do
        local x, z = QD.COX.world_sized(room, t[1], t[2], 3, 3)
        v.grub_sw[i] = { x = x, z = z }
    end
    QD.raid._tob_trace(S, api_drive.tick(), string.format("vespula room v%d t%d at %d,%d: home %d,%d portal %d,%d",
        room.variant, room.turn, room.ox, room.oz, v.home.x, v.home.z, v.portal_sw.x, v.portal_sw.z))
end

-- THE SAFE TILE (once I am in the room: the client routes only from here)
function CV.safe_tile(S)
    local v = S.v
    local room = v.room
    if v.method == "redemption" then
        -- Synq's tile, and the corner of her hover one run in from it: the
        -- loop's lane under her is her edge row from that corner on
        local t = CV.V.SYNQ[room.variant]
        local x, z = QD.COX.world(room, t[1], t[2])
        local ax = math.max(v.home.x, math.min(v.home.x + 4, x))
        local az = math.max(v.home.z, math.min(v.home.z + 4, z))
        assert(math.abs(ax - x) + math.abs(az - z) == 2 and (ax == x or az == z),
            S.who .. ": Synq's tile " .. x .. "," .. z .. " is not two off her hover on a row or column")
        v.safe = { x = x, z = z }
        v.attack_tile = { x = ax, z = az }
        -- (the lane's direction: from the safe tile toward the corner)
        v.lane = { dx = (ax > x and 1) or (ax < x and -1) or 0, dz = (az > z and 1) or (az < z and -1) or 0 }
        -- (the lane's two tiles in line with the cove, where a swing fires
        -- in place: the corner + 2 and + 3 along it, section 11's probe)
        v.fire = { { x = ax + 2 * v.lane.dx, z = az + 2 * v.lane.dz }, { x = ax + 3 * v.lane.dx, z = az + 3 * v.lane.dz } }
        QD.raid._tob_trace(S, api_drive.tick(), string.format("vespula Synq tile %d,%d, attack corner %d,%d",
            x, z, ax, az))
        return
    end
    -- THE SAFE TILE: two tiles off her hover on one of her rows or columns
    -- (wiki Strategies, The Academy), the one nearest the portal that the
    -- client's route reaches exactly
    local hx, hz = v.home.x, v.home.z
    local cands = {}
    for k = 0, 4 do
        cands[#cands + 1] = { hx - 2, hz + k }
        cands[#cands + 1] = { hx + 6, hz + k }
        cands[#cands + 1] = { hx + k, hz - 2 }
        cands[#cands + 1] = { hx + k, hz + 6 }
    end
    local best, bd = nil, nil
    for _, c in ipairs(cands) do
        if QD.COX.room_at(c[1], c[2], room.level) ~= nil and CV.in_room(v, c[1], c[2]) then
            local rr, route = api_drive.route(c[1], c[2])
            local exact = rr == "ok" and route and (route.nearest == nil or route.nearest == false or route.nearest == 0)
            local d = CV.gap(c[1], c[2], v.portal_sw.x, v.portal_sw.z, 4)
            if exact and d > CV.V.FAR then
                if bd == nil or d < bd then best, bd = c, d end
            end
        end
    end
    assert(best, S.who .. ": no safe tile reachable around " .. hx .. "," .. hz)
    v.safe = { x = best[1], z = best[2] }
    QD.raid._tob_trace(S, api_drive.tick(), string.format("vespula safe tile %d,%d", v.safe.x, v.safe.z))
end

function CV.in_room(v, x, z)
    local r = v.room
    return x >= r.ox and x < r.ox + 32 and z >= r.oz and z < r.oz + 32
end

-- Inside: past the tendrils (their row, z 1, and the door before it are the
-- room's cell too, and ccw's nook beside its one tendril is open).
function CV.inside(v, x, z)
    if not CV.in_room(v, x, z) then return false end
    local _, lz = QD.COX.template(v.room, x, z)
    return lz > CV.V.TENDRIL_LZ
end

-- Chebyshev gap from (x, z) to a size-square footprint at (sx, sz): 0 under it.
function CV.gap(x, z, sx, sz, size)
    local dx, dz = 0, 0
    if x < sx then dx = sx - x elseif x > sx + size - 1 then dx = x - (sx + size - 1) end
    if z < sz then dz = sz - z elseif z > sz + size - 1 then dz = z - (sz + size - 1) end
    return math.max(dx, dz)
end

-- ===================================================================== OBSERVE

function CV.observe(S, F)
    local v, ids = S.v, S.ids
    local now = F.tick
    v.ves, v.portal, v.grubs, v.soldiers = nil, nil, {}, {}
    for _, row in ipairs(F.npcs) do
        local id = row.npc_id
        if id == ids.flying or id == ids.enraged or id == ids.walking then
            v.ves = row
        elseif id == ids.portal then
            v.portal = row
        elseif id == ids.grub_healthy or id == ids.grub_sickly or id == ids.grub_infected or id == ids.grub_dead then
            v.grubs[#v.grubs + 1] = row
        elseif id == ids.soldier or id == ids.soldier_down then
            v.soldiers[#v.soldiers + 1] = row
            local key = QD.T2.npc_key(row)
            if not v.soldier_seen[key] then
                v.soldier_seen[key] = now
                v.soldiers_n = v.soldiers_n + 1
            end
            if id == ids.soldier_down and v.soldier_down[key] == nil then v.soldier_down[key] = now end
        end
    end
    -- her landing (V5/V6): the tick she took the walking form; her hp (bar)
    if v.ves and v.ves.npc_id == ids.walking then
        if v.land_t == nil then v.land_t = now; v.landings = (v.landings or 0) + 1 end
    else
        v.land_t = nil
    end
    v.ves_pct = nil
    if v.ves and v.ves.health_scale and v.ves.health_scale > 0 and v.ves.health_ratio and v.ves.health_ratio >= 0 then
        v.ves_pct = 100 * v.ves.health_ratio / v.ves.health_scale
    end
    -- the room, from the tendrils (the room's door) the first time they show
    if v.room == nil then
        local _, me = api_drive.player_tile()
        local lr, rows = api_drive.locs(40)
        if lr == "ok" then
            for _, row in ipairs(rows) do
                if row.loc_id == ids.tendril then
                    local room = QD.COX.room_at(row.x, row.z, me.level)
                    if room ~= nil and room.name == "vespula" then
                        v.tendrils = v.tendrils or {}
                        v.tendrils[#v.tendrils + 1] = { x = row.x, z = row.z }
                        if v.room == nil then CV.geometry(S, room) end
                    end
                end
            end
        end
    end
    -- the grubs she tagged: a grub's first INFECTED row is the sting's landing
    -- (every unstung grub turns sickly 10 ticks into the room: not a tag)
    for _, g in ipairs(v.grubs) do
        local key = g.x .. ":" .. g.z
        local st = v.grub[key]
        if st == nil then st = { x = g.x, z = g.z }; v.grub[key] = st end
        if g.npc_id == ids.grub_healthy or g.npc_id == ids.grub_dead or g.npc_id == ids.grub_sickly then
            -- (a stung grub fed back to full is sickly again: not draining)
            st.stung = nil
        elseif g.npc_id == ids.grub_infected and st.stung == nil then
            st.hp_base, st.hp_t = CV.V.STUNG_HP, now
            st.stung = now
            v.tags = v.tags + 1
        end
        st.row = g
        st.id = g.npc_id
        -- its hp from the bar when the client has one (a feed shows there)
        if st.stung and g.health_scale and g.health_scale > 0 and g.health_ratio and g.health_ratio >= 0 then
            st.hp = math.floor(CV.V.GRUB_HP * g.health_ratio / g.health_scale)
            st.hp_tick = now
        end
    end
    -- her enrage cycle: the tick she took the enraged form (held 20 ticks)
    v.enraged_prev = v.was_enraged
    if v.ves and v.ves.npc_id == ids.enraged then
        if not v.was_enraged then v.enr_start = now end
        v.was_enraged = true
    else
        v.was_enraged = false
    end
    -- my last attack on the portal: my swing on the server's clock (the
    -- executive's), the tick the content stamps -- a swing AT THE PORTAL
    -- (the intended solve shoots her too: a shot at her renews nothing, duo
    -- seed 10 t209)
    if S.x2 and S.x2.swing_last and S.x2.swing_last_npc == ids.portal then v.attack_t = S.x2.swing_last end
    -- the portal's drains in the air (each lands 2 ticks after its throw on
    -- the tile it was thrown at; a row read on its arrival tick counts 30
    -- cycles gone)
    v.drains = {}
    local pr, projs = api_drive.projectiles(0)
    if pr == "ok" then
        for _, p in ipairs(projs) do
            if p.spotanim_id == ids.drain and (p.cycles_left or 0) > 0 then
                v.drains[#v.drains + 1] = { x = p.dst_x, z = p.dst_z, land = now + (p.cycles_left + 45) // 30 }
            end
        end
    end
    -- the portal hit: a splat on it I had not seen (spec V20)
    if v.portal and v.portal.splats then
        for _, sp in ipairs(v.portal.splats) do
            local key = tostring(sp.start) .. ":" .. tostring(sp.damage) .. ":" .. tostring(sp.type)
            if not v.splat_seen[key] then
                v.splat_seen[key] = now
                v.portal_hit = now
                if v.first_hit == nil then v.first_hit = now end
                v.dealt = v.dealt + (sp.damage or 0)
            end
        end
    end
    if v.portal then
        v.portal_seen = true
        if v.portal.health_ratio and v.portal.health_scale and v.portal.health_scale > 0 and v.portal.health_ratio >= 0 then
            v.portal_frac = v.portal.health_ratio / v.portal.health_scale
        end
    end
    -- the enraged sting: an hp loss over a stomp's max in a tick that ended
    -- on her edge ring while she was enraged (the PASS zero)
    -- (her sting is decided a tick before its splat, on my tile and hers
    -- before her step -- the one-tick check missed seed 4 r3 t100's 16,
    -- stung on 6473,81 at t98 with her on 6474)
    v.ves_at = v.ves_at or {}
    if v.ves then v.ves_at[now] = { x = v.ves.x, z = v.ves.z, enr = v.ves.npc_id == ids.enraged } end
    v.ves_at[now - 4] = nil
    if v.last_hp and F.hp < v.last_hp then
        local lost = v.last_hp - F.hp
        if lost > CV.V.STOMP_MAX and v.ves and v.enraged_prev
            and (v.method == "redemption" or v.ves.npc_id == ids.enraged) then
            local hit = nil
            -- (the duo keeps its one-tick check: its seats' counts are
            -- the ones its sweep was judged on)
            -- (Synq's loop: exactly the content's -- her enraged timer at D
            -- reads my tile and hers at the end of D-1, the splat at D+1:
            -- both two ticks back, her enraged then. Relay seed 11 r5 t102
            -- counted a stomp of 8 and a point of drain against the tile I
            -- stood on as she took the enraged form)
            if v.method == "redemption" then
                local me, h = QD.T2.tile_at(S, now - 2), v.ves_at[now - 2]
                if me and h and h.enr and CV.ring(h.x, h.z, me.x, me.z) then hit = me end
            else
                local me = QD.T2.tile_at(S, now - 1)
                if me and CV.ring(v.ves.x, v.ves.z, me.x, me.z) then hit = me end
            end
            if hit then
                v.sting_hits = v.sting_hits + 1
                QD.raid._tob_trace(S, now, string.format("STUNG %d at %d,%d", lost, hit.x, hit.z))
            end
        end
    end
    v.last_hp = F.hp
    -- POISON (the orb's varp102): the next poison splat's size, (sev + 4) / 5
    local pr, sev = api_drive.varp(ids.poison)
    sev = (pr == "ok" and sev) or 0
    v.poison_dmg = (sev > 0) and ((sev + 4) // 5) or 0
    -- the poison timer fires every 30 ticks, one severity a fire
    -- (poison.rs2 set_poison_timer, [timer,poison]): a fall in the varp is
    -- seen the tick before its splat; the next fall 30 ticks on, its splat
    -- the tick after (raid8 t388 varp, t389 splat; t418, t419; t448, t449).
    -- poison_next = the next fall's tick = the plan's hit tick of the splat
    if v.poison_sev and sev > 0 and sev < v.poison_sev then v.poison_next = now + CV.V.POISON_PERIOD end
    if sev <= 0 then v.poison_next = nil end
    v.poison_sev = sev
    -- Redemption fired: prayer gone (and its graphic); counted for the summary
    if v.last_prayer and v.last_prayer > 0 and F.prayer == 0 then v.redemptions = v.redemptions + 1 end
    v.last_prayer = F.prayer
end

-- Is (x, z) on her sting reach (her 5x5 edge ring, corners excluded; V14)?
function CV.ring(sx, sz, x, z)
    local dx, dz = 0, 0
    if x < sx then dx = sx - x elseif x > sx + 4 then dx = x - (sx + 4) end
    if z < sz then dz = sz - z elseif z > sz + 4 then dz = z - (sz + 4) end
    if dx > 1 or dz > 1 then return false end
    if dx == 1 and dz == 1 then return false end
    return dx + dz > 0
end

-- The tick the enrage holds to, or nil. My attack is the splat less the
-- flight; her cycle ends ENRAGE ticks after it began, and is renewed (one
-- flying tick later) when the attack falls within DRAIN_WINDOW of that.
function CV.enrage_until(S)
    local v, V = S.v, CV.V
    local attack = v.attack_t
    if attack == nil and v.portal_hit then attack = v.portal_hit - V.FLIGHT end
    if attack == nil then return nil end
    if v.enr_start == nil then return attack + 1 + V.ENRAGE end
    local finish = v.enr_start + V.ENRAGE
    while attack >= finish - V.RENEW + 1 do finish = finish + 1 + V.ENRAGE end
    return finish
end

-- The tick by which my next attack must go out to keep her enraged, and
-- whether its window [deadline - RENEW + 1, deadline] is open now. nil when
-- she is not enraged.
function CV.renew_deadline(S, F)
    local v, V = S.v, CV.V
    if not v.was_enraged or v.enr_start == nil then return nil end
    return CV.enrage_until(S)
end

-- ===================================================================== PREDICT

function CV.predict(S, F, B)
    local v, V, ids = S.v, CV.V, S.ids
    if v.room == nil then return end
    local now, h = F.tick, B.h
    local ves = v.ves
    local enraged = ves and ves.npc_id == ids.enraged
    local until_t = CV.enrage_until(S)
    local hx, hz
    if enraged then hx, hz = ves.x, ves.z else hx, hz = v.home.x, v.home.z end
    local last = now + h
    if not enraged then
        if until_t == nil or until_t < now + 1 then last = nil else last = math.min(last, until_t) end
    end
    -- (grounded: no stomp until she takes off, GROUND_TICKS from the landing)
    local first = now + 1
    if ves and ves.npc_id == ids.walking and v.land_t then first = math.max(first, v.land_t + V.GROUND_TICKS + 1) end
    -- (the intended solve's enrage seat: its own portal swing enrages her IN
    -- PLACE the tick after (RSProx 843, 3389), and between two cycles she
    -- flies one tick and is enraged again at once (820) -- her reach stands
    -- where she hovers now, from the next tick: duo seed 0 t119 stepped on
    -- her ring in that one flying tick)
    if ves and ves.npc_id == ids.flying and v.method == "intended" and v.role == "enrage" then
        hx, hz = ves.x, ves.z
        last = now + h
    end
    if last and first > last then last = nil end
    local away = ves and (ves.x ~= v.home.x or ves.z ~= v.home.z)
    local homing = last and v.method == "intended" and enraged and away
    -- (Synq's loop too, section 14 T2: the first swing finds her flying to
    -- a grub; she enrages THERE and flies home a tile a tick, and her sting
    -- reads my tile against hers, both before her step -- seeds 10/19/25/42
    -- stepped back to his tile onto the ring of a tile she flew through.
    -- Enraged away from home, or flying with my swing's enrage due: her
    -- footprint k-1, k and k+1 tiles flown home, and the ring of EACH --
    -- a ring tile of one is under another, seed 4 r3 t98 6473,81)
    local reach3 = last and v.method == "redemption" and away
        and (enraged or (ves.npc_id == ids.flying and until_t ~= nil))
    if reach3 then homing = true end
    if homing then
        -- (the intended solve: enraged away from home -- in place, where a
        -- portal swing found her -- she flies home a tile a tick (RSProx
        -- 820, 843), and her reach goes with her: each tick both the tile
        -- she leaves and the one she takes. Duo seed 4 t89: a seat backing
        -- off a tile a tick from a still hazard was stung)
        local function her_at(k)
            return ves.x + math.max(-k, math.min(k, v.home.x - ves.x)), ves.z + math.max(-k, math.min(k, v.home.z - ves.z))
        end
        local redeem3 = reach3 and F.lit.redemption and F.prayer >= CV.redeem_min(S, F) and F.hp >= V.REDEMPTION_HP
            and (v.poison_dmg or 0) <= 1
        for t = first, last do
            local ax, az = her_at(t - now - 1)
            local bx, bz = her_at(reach3 and (t - now + 1) or (t - now))
            local x0, z0, x1, z1 = math.min(ax, bx), math.min(az, bz), math.max(ax, bx) + 4, math.max(az, bz) + 4
            B.hit({ name = "stomp", x0 = x0, z0 = z0, x1 = x1, z1 = z1, t0 = t, t1 = t,
                    dmg_exp = V.STOMP_EXP, dmg_max = redeem3 and 0 or V.STOMP_MAX })
            local rings = { { x0, z0, x1, z1 } }
            if reach3 then
                rings = {}
                for dk = -1, 1 do
                    local px, pz = her_at(math.max(0, t - now + dk))
                    rings[#rings + 1] = { px, pz, px + 4, pz + 4 }
                end
            end
            for _, q in ipairs(rings) do
                local qx0, qz0, qx1, qz1 = q[1], q[2], q[3], q[4]
                for _, r in ipairs({ { qx0, qz1 + 1, qx1, qz1 + 1 }, { qx0, qz0 - 1, qx1, qz0 - 1 },
                                     { qx0 - 1, qz0, qx0 - 1, qz1 }, { qx1 + 1, qz0, qx1 + 1, qz1 } }) do
                    B.hit({ name = "sting", x0 = r[1], z0 = r[2], x1 = r[3], z1 = r[4], t0 = t, t1 = t,
                            dmg_exp = V.STING_EXP, dmg_max = V.STING_MAX, forbid = true })
                end
            end
        end
    elseif last then
        -- THE STOMP (V13): her 25 tiles every enraged tick. With Redemption lit
        -- and prayer up, at REDEMPTION_HP or more a stomp cannot kill (it
        -- fires Redemption instead): its max is no lethal risk then
        -- (poisoned, a poison splat at <= its size kills where Redemption
        -- cannot: no stomp is free then -- the kit has no anti-poison; the
        -- room before may have poisoned me, or her own ranged)
        -- (prayer must outlast the trip: 1 point drains under her before a
        -- stomp lands, and the stomps are real -- raid1 prep3 t1483-1487)
        local redeem = F.lit.redemption and F.prayer >= CV.redeem_min(S, F) and F.hp >= V.REDEMPTION_HP
        local pd = v.poison_dmg or 0
        if pd > 1 and v.poison_next == nil then redeem = false end
        if pd > 1 and redeem then
            -- the poison splat's tick (and the two before it) the stomps are
            -- real: hp must not stand at or under the splat when it lands
            local pn = v.poison_next
            while pn < now + 1 do pn = pn + V.POISON_PERIOD end
            for t = first, last do
                local real = (t >= pn - 2 and t <= pn) or (t >= pn + V.POISON_PERIOD - 2)
                B.hit({ name = "stomp", x0 = hx, z0 = hz, x1 = hx + 4, z1 = hz + 4, t0 = t, t1 = t,
                        dmg_exp = V.STOMP_EXP, dmg_max = real and V.STOMP_MAX or 0 })
            end
        else
            B.hit({ name = "stomp", x0 = hx, z0 = hz, x1 = hx + 4, z1 = hz + 4, t0 = first, t1 = last,
                    dmg_exp = V.STOMP_EXP, dmg_max = redeem and 0 or V.STOMP_MAX })
        end
        -- (Synq's loop: hovering at home, the tiles under her it uses are
        -- her edge row (column) from the corner his tile faces -- the rest
        -- of her footprint is forbidden, so the run in and out stays on
        -- that lane)
        -- (lifted while a run feeds from under her: the thru room's grub
        -- beside her hover is fed only from there, section 14 T4)
        local A, ln = v.attack_tile, v.lane
        if A and v.method == "redemption" and hx == v.home.x and hz == v.home.z
            and not (v.run and v.feed_under) then
            for x = hx, hx + 4 do
                for z = hz, hz + 4 do
                    local on_lane = (ln.dx ~= 0 and z == A.z) or (ln.dz ~= 0 and x == A.x)
                    if not on_lane then
                        B.hit({ name = "under", x = x, z = z, t0 = first, t1 = last, dmg_exp = V.STOMP_EXP,
                                dmg_max = V.STOMP_MAX, forbid = true })
                    end
                end
            end
        end
        -- THE STING (V14): her edge ring, corners out, every other tick: forbid
        local ring = {
            { hx, hz + 5, hx + 4, hz + 5 }, { hx, hz - 1, hx + 4, hz - 1 },
            { hx - 1, hz, hx - 1, hz + 4 }, { hx + 5, hz, hx + 5, hz + 4 },
        }
        for _, r in ipairs(ring) do
            B.hit({ name = "sting", x0 = r[1], z0 = r[2], x1 = r[3], z1 = r[4], t0 = first, t1 = last,
                    dmg_exp = V.STING_EXP, dmg_max = V.STING_MAX, forbid = true })
        end
    end
    -- THE DRAIN (V18, recorded): each one in the air takes 3 prayer if I am
    -- still on its tile at the end of the tick before it lands
    -- (3 prayer points -- Redemption's fuel -- or, at none, 3 hitpoints:
    -- priced at 3, so a wait on Synq's tile steps off the drained tile;
    -- duo/solo seeds 10 and 23 died to drains standing still at 0 prayer)
    local dry = F.prayer < 3
    for _, d in ipairs(v.drains or {}) do
        local t = d.land - 1
        if t >= now + 1 then
            B.hit({ name = "drain", x = d.x, z = d.z, t0 = t, t1 = t, dmg_exp = 3, dmg_max = dry and 3 or 0 })
        end
    end
    if CV.in_room(v, F.me.x, F.me.z) then CV.void(S, F, B) end
    -- THE POISON SPLAT: everywhere, at its tick
    if (v.poison_dmg or 0) > 0 and v.poison_next then
        local pn = v.poison_next
        while pn < now + 1 do pn = pn + V.POISON_PERIOD end
        if pn <= now + h then
            B.hit({ name = "poison", x0 = F.me.x - 40, z0 = F.me.z - 40, x1 = F.me.x + 40, z1 = F.me.z + 40,
                    t0 = pn, t1 = pn, dmg_exp = v.poison_dmg, dmg_max = v.poison_dmg })
        end
    end
    -- HER RANGED (V3): not enraged, she shoots wherever I stand in the room
    -- (grounded she shoots only raiders not under or beside her, V3/V6:
    -- the room less her footprint and its ring, as four bands)
    -- (not in the one flying tick between two renewed cycles of Synq's
    -- loop -- she is enraged again the next tick and shoots nothing: priced
    -- there, the plan rescued with a brew every cycle, raid45 r27 a4 22
    -- Saradomin brews 21 ticks apart and Ranged drained to 0-hit swings,
    -- raid44 the same on Xeric's aid; section 16 E5)
    local between = v.method == "redemption" and v.enr_start ~= nil and until_t ~= nil and until_t >= now
        and ves and ves.npc_id == ids.flying
    if ves and not enraged and not between and CV.in_room(v, F.me.x, F.me.z) then
        local r = v.room
        -- (Protect from Missiles halves it, read at the landing: NR 0.5)
        local prot = F.lit.protectfrommissiles and 2 or 1
        local rects = { { r.ox, r.oz, r.ox + 31, r.oz + 31 } }
        if ves.npc_id == ids.walking then
            local x0, z0, x1, z1 = ves.x - 1, ves.z - 1, ves.x + 5, ves.z + 5
            rects = { { r.ox, r.oz, r.ox + 31, z0 - 1 }, { r.ox, z1 + 1, r.ox + 31, r.oz + 31 },
                      { r.ox, z0, x0 - 1, z1 }, { x1 + 1, z0, r.ox + 31, z1 } }
        end
        for t = now + 1, now + h do
            if (t - now) % 3 == 0 then
                for _, q in ipairs(rects) do
                    if q[3] >= q[1] and q[4] >= q[2] then
                        B.hit({ name = "ranged", x0 = q[1], z0 = q[2], x1 = q[3], z1 = q[4], t0 = t, t1 = t,
                                dmg_exp = V.RANGED_EXP // prot, dmg_max = V.RANGED_MAX // prot })
                    end
                end
            end
        end
    end
    -- SOLDIERS (V11): their reach every tick; a dropped one's blast at its 33rd tick
    for _, s in ipairs(v.soldiers) do
        B.hit({ name = "soldier", x0 = s.x - 1, z0 = s.z - 1, x1 = s.x + 3, z1 = s.z + 3, t0 = now + 1, t1 = now + h,
                dmg_exp = 3, dmg_max = 14 })
        local down = v.soldier_down[QD.T2.npc_key(s)]
        if down then
            local t = down + 33
            if t >= now + 1 and t <= now + h then
                B.hit({ name = "blast", x0 = s.x - 2, z0 = s.z - 2, x1 = s.x + 4, z1 = s.z + 4, t0 = t - 1, t1 = t,
                        dmg_exp = 22, dmg_max = 30, forbid = true })
            end
        end
    end
end

-- THE VOID (vespula_spec.md A28): the abyss floor and the cove's back wall
-- carry `breakroutefinding` -- the client's route finder (OSRS's too) walks
-- through them and the server stops you on their edge. A raider sees the void
-- and does not plan into it: their tiles (the loc rows the client is sent),
-- read once, as row runs, forbidden.
function CV.void(S, F, B)
    local v = S.v
    if v.void_runs == nil then
        local lr, rows = api_drive.locs(40)
        if lr ~= "ok" then return end
        local names, set = {}, {}
        for _, row in ipairs(rows) do
            if CV.in_room(v, row.x, row.z) then
                local nm = names[row.loc_id]
                if nm == nil then
                    local _, n = api_drive.symbol_name("loc", row.loc_id)
                    nm = n or ""
                    names[row.loc_id] = nm
                end
                if string.find(nm, "raids_floor_abyss", 1, true) == 1 or nm == "inviswall_serverside" then
                    set[row.x .. ":" .. row.z] = true
                    v.void_set = set
                end
            end
        end
        v.void_runs = {}
        local r = v.room
        for z = r.oz, r.oz + 31 do
            local run0 = nil
            for x = r.ox, r.ox + 32 do
                local bad = x <= r.ox + 31 and set[x .. ":" .. z] == true
                if bad and run0 == nil then run0 = x end
                if not bad and run0 ~= nil then
                    v.void_runs[#v.void_runs + 1] = { run0, z, x - 1 }
                    run0 = nil
                end
            end
        end
    end
    for _, rr in ipairs(v.void_runs) do
        B.hit({ name = "void", x0 = rr[1], z0 = rr[2], x1 = rr[3], z1 = rr[2], t0 = F.tick + 1, t1 = F.tick + B.h,
                dmg_exp = 50, dmg_max = 0, forbid = true })
    end
end

-- ==================================================================== STRATEGY

-- Ticks until the portal dies at my measured rate (nil when unknown).
function CV.portal_eta(S, F)
    local v = S.v
    if v.first_swing == nil then return nil end
    local spent = F.tick - v.first_swing
    if spent < 20 or v.dealt <= 0 then return nil end
    local rate = v.dealt / spent
    -- (what is left from its bar when shown -- a soldier's heal and the
    -- portal's regen are not in my dealt -- and the rate taken 20% slower:
    -- the estimate swings, and a grub fed for too short a time hatches)
    local left = CV.V.PORTAL_HP - v.dealt
    if v.portal_frac then left = math.max(left, CV.V.PORTAL_HP * v.portal_frac) end
    return math.max(0, left) / (rate * 0.8)
end

-- A tagged grub's hatch tick: from the tag (~69 ticks of drain), or from
-- its bar's hp when the client shows one (a feed moves it).
function CV.hatch_t(g)
    local t = (g.hp_t or g.stung) + (g.hp_base or CV.V.STUNG_HP)
    if g.hp_tick and g.hp_tick > (g.hp_t or g.stung) then t = g.hp_tick + g.hp end
    return t
end

-- A tagged grub's hp now: the bar when the client has one, else counted.
function CV.grub_hp(S, F, g)
    local row = g.row
    if row and row.health_scale and row.health_scale > 0 and row.health_ratio and row.health_ratio >= 0 then
        return math.floor(CV.V.GRUB_HP * row.health_ratio / row.health_scale)
    end
    -- (counted: STUNG_HP at the sting, -1 a tick, +BLOSSOM a feed -- the
    -- grub's bar does not reach the client)
    return (g.hp_base or CV.V.STUNG_HP) - (F.tick - (g.hp_t or g.stung))
end

-- The tagged grub hatching first, and its hatch tick.
function CV.next_hatch(S)
    local best = nil
    for _, g in pairs(S.v.grub) do
        if g.stung and not g.unfeedable and g.id == S.ids.grub_infected then
            local t = CV.hatch_t(g)
            if best == nil or t < best.t then best = { t = t, g = g } end
        end
    end
    return best
end

-- The tile a grub is fed from: edge-adjacent to its 3x3 (the op runs without
-- a step), two or more off her hover (out of the stomp and the sting reach),
-- not the void, and one the route reaches exactly; nearest me. nil = none.
function CV.feed_tile(S, F, g)
    local v = S.v
    -- (sent from a side's end tile, the server's op route stepped to that
    -- side's middle -- onto her ring, r4 seed 2, 6419,81 -> 6419,80: a tile
    -- counts only when its side's middle is out of her reach too; 3+ off her
    -- first, 2 the fallback. Middles alone left two seeds no tile, r6.)
    local cands = {}
    for k = 0, 2 do
        cands[#cands + 1] = { g.x + k, g.z - 1, g.x + 1, g.z - 1 }
        cands[#cands + 1] = { g.x + k, g.z + 3, g.x + 1, g.z + 3 }
        cands[#cands + 1] = { g.x - 1, g.z + k, g.x - 1, g.z + 1 }
        cands[#cands + 1] = { g.x + 3, g.z + k, g.x + 3, g.z + 1 }
    end
    local best, bd = nil, nil
    -- (enraged she is in reach where she hovers now, which is not her home
    -- while she flies there: enraged in place beside the grub she just
    -- tagged, duo seed 0 t104 -- both are measured)
    local cur = v.ves and v.ves.npc_id == S.ids.enraged and v.ves or nil
    local function off(x, z)
        local d = CV.gap(x, z, v.home.x, v.home.z, 5)
        if cur then d = math.min(d, CV.gap(x, z, cur.x, cur.z, 5)) end
        return d
    end
    -- (last, a tile whose side's middle IS in her reach: a sting risked
    -- per blossom beats a soldier -- r7 seeds 0 and 2 hatched one, 381 and
    -- 278 taken, against 282 and 270 with the feed and one sting)
    -- (grounded with CALM_LEFT ticks to go she reaches no one: any side
    -- will do -- the thru room's grub beside her hover is fed only then,
    -- duo seed 10; the takeoff's predicted reach walks me off before it)
    -- (then "reach": the tile and its side's middle off her reach exactly --
    -- under her or her edge ring, corners out -- where she hovers and at
    -- home: the thru room's grub beside her hover has its corner tile, duo
    -- seed 10)
    -- (last, "under": off her ring, under her allowed -- a stomp or two,
    -- never the sting: the thru room's grub beside her hover has every
    -- other side in the void, duo seed 10)
    local tiers = { { 3, 2 }, { 2, 2 }, "reach", { 2, -1 }, "under" }
    if v.ves and v.ves.npc_id == S.ids.walking and v.land_t
        and v.land_t + CV.V.GROUND_TICKS - F.tick >= CV.V.CALM_LEFT then
        tiers = { { -9, -9 } }
    end
    local function in_reach(x, z, under_ok)
        for _, h in ipairs({ v.home, cur }) do
            if h and ((not under_ok and x >= h.x and x <= h.x + 4 and z >= h.z and z <= h.z + 4) or CV.ring(h.x, h.z, x, z)) then
                return true
            end
        end
        return false
    end
    for _, tier in ipairs(tiers) do
    if best == nil then
    for _, c in ipairs(cands) do
        local ok
        if tier == "reach" or tier == "under" then
            ok = not in_reach(c[1], c[2], tier == "under") and not in_reach(c[3], c[4], tier == "under")
        else
            ok = off(c[1], c[2]) >= tier[1] and off(c[3], c[4]) >= tier[2]
        end
        if CV.in_room(v, c[1], c[2]) and ok
            and not (v.void_set and v.void_set[c[1] .. ":" .. c[2]]) then
            local rr, route = api_drive.route(c[1], c[2])
            local exact = rr == "ok" and route and (route.nearest == nil or route.nearest == false or route.nearest == 0)
            local d = QD.raid._tob_cheb(F.me.x, F.me.z, c[1], c[2])
            if exact and (bd == nil or d < bd) then best, bd = { x = c[1], z = c[2] }, d end
        end
    end
    end
    end
    return best
end

-- A feed op from tile ft onto grub g's 3x3 walks to that side's middle
-- first (the engine's npc reach: seed 4 r1 t153 6472,81 -> 6473,81, seed 44
-- r3 t195; r4 seed 2). When that middle is her ring at home (the thru room's
-- grub G1, whose only open side is Synq's column), the op goes only at her
-- cycle's end: at T = enrage start + ENRAGE her enraged timer turns her
-- flying and returns, at T+1 the flying timer re-enrages her and returns
-- (cox_vespula.rs2) -- no sting reads my tile at the end of T-1 or T. An op
-- decided at T-2 or T-1 lands me there then. True = hold the op.
function CV.feed_hold(S, F, g, ft)
    local v, V = S.v, CV.V
    if v.method ~= "redemption" then return false end
    local mx, mz
    if ft.x == g.x - 1 or ft.x == g.x + 3 then mx, mz = ft.x, g.z + 1 else mx, mz = g.x + 1, ft.z end
    if not CV.ring(v.home.x, v.home.z, mx, mz) then return false end
    if v.ves and v.ves.npc_id == S.ids.enraged and v.enr_start then
        local T = v.enr_start + V.ENRAGE
        return not (F.tick == T - 2 or F.tick == T - 1)
    end
    -- (flying between two cycles, her re-enrage due: an op now lands me on
    -- the ring as she stings again -- seed 4 r4 t201, the second blossom)
    local u = CV.enrage_until(S)
    return v.ves ~= nil and v.ves.npc_id == S.ids.flying and u ~= nil and u >= F.tick
end

-- May the portal attack be pressed from (x, z)? Synq's loop, her enraged at
-- home: only from his tile, the lane's corner or a fire tile. Pressed from
-- the corner + 1, or from behind his tile, the server's route to range ran
-- on to the cove or her ring row at the tick's end (section 14 T3; seeds 19
-- r4 t296 6475,81 -> 6476,80, 24 r4 t341 and t349).
function CV.press_ok(S, x, z)
    local v = S.v
    if v.method ~= "redemption" or v.fire == nil then return true end
    if not (v.ves and v.ves.npc_id == S.ids.enraged and v.ves.x == v.home.x and v.ves.z == v.home.z) then return true end
    for _, t in ipairs({ v.safe, v.attack_tile, v.fire[1], v.fire[2] }) do
        if t.x == x and t.z == z then return true end
    end
    return false
end

-- THE FEEDER (the intended solve, opts.role "feed"): the blossoms its tagged
-- grubs want now, picked from the roots and fed, lowest grub first. True when
-- it took the tick (intents filled), false to let the seat shoot.
function CV.feeder(S, F, B, intents, blossoms)
    local v, V, ids = S.v, CV.V, S.ids
    local now = F.tick
    -- (a fed blossom: the count fell while feeding that grub -- counted on
    -- it, the bar does not reach the client)
    if v.feed_g and v.feed_bl and blossoms < v.feed_bl then
        local g0 = v.feed_g
        g0.hp_base = math.min(V.GRUB_HP, CV.grub_hp(S, F, g0) + (v.feed_bl - blossoms) * V.BLOSSOM)
        g0.hp_t = now
        v.feed_since = now
    end
    v.feed_bl = blossoms
    local grounded = v.ves and v.ves.npc_id == ids.walking
    local calm = (v.ves and v.ves.npc_id == ids.enraged)
        or (grounded and v.land_t and v.land_t + V.GROUND_TICKS - now >= V.CALM_LEFT)
    local want, best, bhp = 0, nil, nil
    local held_g, held_hp = nil, nil
    for _, g in pairs(v.grub) do
        if g.stung and g.id == ids.grub_infected and (g.retry_t or -1) <= now then
            local hp = CV.grub_hp(S, F, g) - V.TRIP_DRAIN
            local n = 0
            if calm then
                n = math.max(0, math.ceil((V.GRUB_HP - hp) / V.BLOSSOM))
            elseif hp < V.KEEP_HP then
                n = math.ceil((V.KEEP_HP + V.BLOSSOM - hp) / V.BLOSSOM)
            end
            if n > 0 then
                want = want + n
                if bhp == nil or hp < bhp then best, bhp = g, hp end
                if g == v.feed_g then held_g, held_hp = g, hp end
            end
        end
    end
    -- (calm, the grub in hand is fed to full before the next: full stops its
    -- drain for good while she cannot tag it again -- a blossom a grub,
    -- lowest first, kept three draining as fast as one seat feeds, duo seed
    -- 10 -- unless the lowest is down to its walk)
    if calm and held_g and best and held_g ~= best
        and bhp > QD.raid._tob_cheb(F.me.x, F.me.z, best.x + 1, best.z + 1) + 2 + V.TRIP_DRAIN * 2 then
        best, bhp = held_g, held_hp
    end
    if best == nil then
        v.feed_g, v.feed_since = nil, nil
        return false
    end
    -- the blossoms first, while a slot is free and some grub still wants more
    -- than is held -- but a held blossom goes to the lowest grub once its hp
    -- left is the walk there (a tile a tick) and a margin: duo seed 10 picked
    -- for all four for 17 ticks across the room while one hatched
    local walk = QD.raid._tob_cheb(F.me.x, F.me.z, best.x + 1, best.z + 1) + 2
    local urgent = blossoms > 0 and bhp <= walk + V.TRIP_DRAIN * 2
    if blossoms < want and CV.free_slots() > 0 and not urgent then
        local root = CV.nearest_loc(S, F, ids.root)
        if root then
            intents.mode = "roots"
            intents.world_op = { kind = "loc", id = ids.root, op = 1, x = root.x, z = root.z }
            return true
        end
    end
    if blossoms == 0 then
        if v.feed_why ~= now // 10 then
            v.feed_why = now // 10
            QD.raid._tob_trace(S, now, string.format("feeder idle: want %d, free %d, no root in bloom", want, CV.free_slots()))
        end
        return false
    end
    local g = best
    if v.feed_g ~= g then v.feed_g, v.feed_since = g, now end
    intents.mode = "feed"
    local ft = CV.feed_tile(S, F, g)
    if ft == nil or now - v.feed_since > V.FEED_STALL * 3 then
        -- (no tile out of her reach, or the route never reaches: another
        -- grub for a while, or the seat shoots)
        -- (no tile is mostly her, enraged beside it, on her way home)
        QD.raid._tob_trace(S, now, (ft and "feed stalled at " or "no feed tile at ") .. g.x .. "," .. g.z)
        g.retry_t = now + (ft and V.FEED_RETRY or V.NO_TILE_RETRY)
        v.feed_g, v.feed_since = nil, nil
        return false
    end
    if F.me.x == ft.x and F.me.z == ft.z then
        intents.world_op = { kind = "npc", id = g.id, op = 1, x = g.row.x, z = g.row.z }
    else
        B.pull({ name = "feed", x = ft.x, z = ft.z, size = 1, weight = V.FEED_PULL, t0 = now + 1, t1 = now + B.h })
    end
    return true
end

-- Doses held of a potion family (4 for a full vial).
function CV.doses(S, list)
    local n = 0
    for i, obj in ipairs(list) do n = n + ((i - 1) % 4 + 1) * QD.raid._tob_count(S, obj) end
    return n
end

function CV.strategy(S, F, B)
    local v, V, ids = S.v, CV.V, S.ids
    local intents = { mode = "-", eat_below = V.EAT_BELOW, brew_below = V.BREW_BELOW }
    -- (the Redemption method lets hp fall to Redemption's 9 and shoots from
    -- under her; out of it she shoots, so hp is kept up. The ground method
    -- keeps one low floor -- the plan's rescue sips for a predicted lethal
    -- hit -- dropped further once no restore is held, the shared rule)
    if v.method == "redemption" and F.prayer < CV.redeem_min(S, F)
        and not QD.raid._tob_first(S, ids.restores) and not QD.raid._tob_first(S, ids.enhances) then
        -- (no prayer left to light Redemption, none in the pack: the loop's
        -- stomps and the portal's drain come out of hp -- seed 23 went
        -- under her at 8 and died; three stomps and a drain held instead)
        intents.brew_below = V.DRY_BREW
    elseif v.method == "ground" then
        intents.brew_below = QD.COX.brew_floor(S, V.HEAL_FLOOR)
    elseif v.method == "intended" then
        intents.brew_below = QD.COX.brew_floor(S, V.DUO_BREW)
    elseif not (v.ves and v.ves.npc_id == ids.enraged) then
        -- (calm she shoots: hp kept over her ranged. Not in the one flying
        -- tick between two renewed cycles -- the loop's hp sits under 40 by
        -- design, and that tick drank a brew every cycle: raid45 r27 a3, 22
        -- Saradomin brews 21 ticks apart, Ranged drained to 0-hit swings;
        -- raid44 the same on Xeric's aid. Section 16 E5)
        local u = v.method == "redemption" and v.enr_start and CV.enrage_until(S)
        if not (u and u >= F.tick) then intents.brew_below = V.CALM_BREW end
    end
    -- poisoned: the splat Redemption cannot save must find hp over it (and a
    -- stomp in the same tick) -- always while its tick is unknown, else in
    -- the few ticks before it
    if (v.poison_dmg or 0) > 1 then
        local pn = v.poison_next
        if pn then while pn < F.tick do pn = pn + V.POISON_PERIOD end end
        if pn == nil then
            intents.brew_below = math.max(intents.brew_below, v.poison_dmg + 12)
        elseif pn - F.tick <= 4 then
            intents.brew_below = math.max(intents.brew_below, v.poison_dmg + V.STOMP_MAX + 1)
        end
    end
    -- (the shared supplies brew only under eat_below -- the kit has no food --
    -- and EAT_BELOW is 0 for Synq's loop, which lets hp fall to Redemption on
    -- purpose. The intended solve keeps hp up, so its floor is both: without
    -- it DUO_BREW never fired, and duo seed 18's enrage seat sat at 23 for 75
    -- ticks, too low to pay the stomps of a renewal, until she tagged)
    if v.method == "intended" then intents.eat_below = intents.brew_below end
    if v.room == nil then
        intents.mode = "find"
        return intents
    end
    -- the ranged set worn, Longrange (enter), Redemption + Rigour
    local W = V.WEAPONS[v.weapon]
    intents.gear = ids[W.set]
    -- the quick prayers (Synq 1:29:03): Redemption, then Rigour beside it --
    -- pressed here, not through the resource channel's boost (which waits for
    -- 40 points, and a restore after a Redemption gives fewer)
    -- (Rigour lit over BOOST_ON points, let go under BOOST_OFF: what the
    -- enhance gives back keeps Redemption, not Rigour, alive)
    -- (the intended solve: Protect from Missiles in Redemption's place --
    -- no stomp is taken on purpose, and her ranged is the room's damage)
    local first_pray = v.method == "intended" and "protectfrommissiles" or "redemption"
    -- (Synq, 1:30:08: a restore -- or the enhance -- "once your prayer gets
    -- depleted", i.e. after Redemption fired; the shared sip at 25 drank the
    -- restores dry by tick ~205 of a long room, seed 23)
    if v.method == "redemption" then intents.prayer_sip = V.REDEEM_PRAYER end
    if F.prayer > 0 and not F.lit[first_pray] then
        intents.pray = first_pray
    elseif F.prayer >= V.BOOST_ON and not F.lit[W.boost] then
        intents.pray = W.boost
    elseif F.prayer < V.BOOST_OFF and F.lit[W.boost] then
        intents.pray_off = { W.boost }
    end
    -- my enhance running: a dose count falling (its ~450 ticks start)
    local en = CV.doses(S, ids.enhances)
    if v.enh_n ~= nil and en < v.enh_n then v.penh_until = F.tick + V.ENHANCE_TICKS end
    v.enh_n = en
    local enh = QD.raid._tob_first(S, ids.enhances)
    local enh_due = enh and (v.penh_until or -1) < F.tick and F.prayer < V.ENHANCE_BELOW
        and F.tick - (S.last_drink or -10) >= 3
    S.t2.want.boost = nil
    -- a Ranged drained by Xeric's aid (no overload running) gets a revitalisation
    -- once 20 under (QD.COX.restore_due; the shared boost_stat did at 10)
    S.t2.want.boost_stat = nil
    QD.COX.restore_intent(S, F, intents, { W.stat })
    -- THE PRE-POT ("prepot", the wiki's raid notes; The Academy): an overload
    -- before the tendrils, once, at full health (its five 10s are paid before
    -- the loop needs the hp)
    -- (from the room before, hp is what it left: Xeric's aid up to PREPOT_HP
    -- first -- the overload's five 10s come out of it -- and the overload then
    -- also undoes the aid's drain of Ranged, a.k.a. the whole raid's -25)
    if not v.overloaded and not CV.inside(v, F.me.x, F.me.z) then
        local ovl = QD.raid._tob_first(S, ids.overloads)
        if ovl and F.tick - (S.last_drink or -10) < 3 then
            -- (the potion timer: wait for it outside, the pre-pot is not done)
            intents.mode = "prepot"
            return intents
        end
        local brew = QD.raid._tob_first(S, ids.brews)
        if v.method == "ground" and ovl and F.hp < V.PREPOT_OVL_HP then ovl = nil end
        -- (not under the budget Redemption loop, RAID_SOLO_RELAY.md round 4:
        -- in the relay r21-r23 every Vespula that drank one lost -- 28 aid
        -- sips in raid1 r23 -- and every pass carried none; the loop was
        -- tuned on the kit's ranging potion, and Olm wants the overload)
        if v.method == "redemption" then ovl = nil end
        if ovl and F.hp < V.PREPOT_HP and brew and (v.prepot_brews or 0) < 3 then
            intents.drink = brew
            S.last_drink = F.tick
            v.prepot_brews = (v.prepot_brews or 0) + 1
            intents.mode = "prepot"
            return intents
        end
        -- (Ranged drained by those aids, or by the rooms before: a
        -- revitalisation first, so the fight opens at full Ranged)
        local lv = F.levels.ranged
        local revit = QD.raid._tob_first(S, ids.restores)
        if ovl and not v.prepot_revit and QD.COX.restore_due(F, { W.stat }) and revit then
            intents.drink = revit
            S.last_drink = F.tick
            v.prepot_revit = true
            intents.mode = "prepot"
            return intents
        end
        if ovl and enh_due and not v.prepot_enh then
            intents.drink = enh
            S.last_drink = F.tick
            v.prepot_enh = true
            intents.mode = "prepot"
            return intents
        end
        if ovl and F.hp > 50 then
            intents.drink = ovl
            S.last_drink = F.tick
            v.overloaded = F.tick
            intents.mode = "prepot"
            return intents
        end
        -- (no overload -- the budget kit, wiki Minimum -- the crossbow's own
        -- ranging potion, once, when Ranged is not already boosted)
        local rng = W.stat == "ranged" and QD.raid._tob_first(S, ids.rangings)
        local lvr = F.levels and F.levels.ranged
        if not ovl and rng and not v.prepot_rng and lvr and lvr.level <= lvr.base then
            if F.tick - (S.last_drink or -10) >= 3 then
                intents.drink = rng
                S.last_drink = F.tick
                v.prepot_rng = true
            end
            intents.mode = "prepot"
            return intents
        end
        v.overloaded = -1
    end
    -- (the anglerfish, once, after any overload: overheal and a free slot)
    local fish = not CV.inside(v, F.me.x, F.me.z) and QD.raid._tob_first(S, ids.prepot_food)
    if fish and (v.prepot_fish or 0) < 3 and v.method == "redemption" then
        if F.tick - (v.prepot_fish_t or -10) >= 2 and F.tick - (S.last_drink or -10) >= 1 then
            intents.eat = fish
            v.prepot_fish = (v.prepot_fish or 0) + 1
            v.prepot_fish_t = F.tick
        end
        intents.mode = "prepot"
        return intents
    end
    -- ENTER: through the tendrils (the Pass hop, V22)
    if not CV.inside(v, F.me.x, F.me.z) then
        intents.mode = "enter"
        -- (the loop runs on run energy: the lane is a 2-tile run step over
        -- her ring, and below 3% the plan walks and keeps off it. The relay
        -- meets her on what the rooms before left -- r25 raid21 0%, raid44
        -- 15%: no swing in her renewal window, and every grub tagged.
        -- Rested outside first, prayers dark -- standing regains 0.24% a
        -- tick, nothing is spent; section 15)
        if v.method == "redemption" and not v.rested and (F.run_energy or 0) < V.ENTER_ENERGY then
            intents.mode = "rest"
            intents.pray = nil
            local off = {}
            for _, name in ipairs({ "redemption", W.boost }) do
                if F.lit[name] then off[#off + 1] = name end
            end
            if #off > 0 then intents.pray_off = off end
            return intents
        end
        if v.method == "redemption" and not v.rested then
            v.rested = true
            QD.raid._tob_trace(S, F.tick, "vespula rested: run energy " .. tostring(F.run_energy))
        end
        -- (Synq's solo enters with run and the quick prayers on -- "with our
        -- run energy and Redemption prayer turned on" (The Academy): the
        -- encounter starts on entry, and her first sting is S+4 (RSProx 820);
        -- the run orb clicked with the pass took effect a step late and the
        -- first swing missed the race by a tick, seed 10)
        -- (both quick prayers, Redemption + the boost (SQ 1:29:03): a prayer
        -- press inside the room takes the panel tick the first attack needs,
        -- seed 10 t22-23)
        if v.method == "redemption" and (not F.run_on or not F.lit.redemption or not F.lit[W.boost]) then
            return intents
        end
        local t = nil
        for _, td in ipairs(v.tendrils or {}) do
            if t == nil or QD.raid._tob_cheb(F.me.x, F.me.z, td.x, td.z) < QD.raid._tob_cheb(F.me.x, F.me.z, t.x, t.z) then t = td end
        end
        if t then intents.world_op = { kind = "loc", id = ids.tendril, op = 1, x = t.x, z = t.z } end
        return intents
    end
    if v.portal == nil then
        intents.mode = "clear"
        if v.portal_seen then
            -- THE ROOM LEFT USABLE (the solo relay walks straight on): the
            -- loop's prayers off -- Redemption, the boost -- and hp brewed
            -- back over POST_HP (Synq's loop ends near Redemption's 9)
            intents.mode = "settle"
            local off = {}
            for _, name in ipairs({ "redemption", W.boost }) do
                if F.lit[name] then off[#off + 1] = name end
            end
            if #off > 0 then intents.pray_off = off end
            local brew = QD.raid._tob_first(S, ids.brews)
            if brew and F.hp < V.POST_HP and not intents.drink and F.tick - (S.last_drink or -10) >= 3 then
                intents.drink = brew
                S.last_drink = F.tick
            end
        end
        return intents
    end
    if v.safe == nil then CV.safe_tile(S) end
    local anti = QD.raid._tob_first(S, ids.antidotes)
    if anti and (v.poison_sev or 0) > 0 and not intents.drink and F.tick - (S.last_drink or -10) >= 3 then
        intents.drink = anti
        S.last_drink = F.tick
    elseif enh_due then
        intents.drink = enh
        S.last_drink = F.tick
    end
    -- THE ROOT RUN (plan section 4): tagged grubs hatching before the portal
    -- dies: blossoms, then the feeds. One run serves every grub that would
    -- hatch before the portal's death (+ the run and a margin), each fed only
    -- the blossoms that carry it past then (35 ticks a blossom, V8), earliest
    -- hatch first.
    local nh = CV.next_hatch(S)
    local eta = CV.portal_eta(S, F)
    local blossoms = QD.raid._tob_count(S, ids.blossom)
    -- (ROOT_LEAD before the first hatch: a run longer than her enrage lets
    -- her fly and tag more grubs -- an earlier start for a bigger run was
    -- tried and lost more seeds, trident probe fp3)
    local list, total = nil, 0
    -- (a portal the estimate kills up to ROOT_EARLY before the hatch is
    -- left to die; one dying after it gets the run -- the estimate swings
    -- by more than a few ticks: r2 seed 0, eta inside the margin, soldier
    -- out 2 ticks before the portal fell)
    -- (and a portal within ROOT_FINISH ticks of dying is finished: raid1
    -- prep3 left for the roots at eta 6)
    -- (only with her enrage renewed for a run's length: a lapse lets her fly
    -- and tag more grubs -- RSProx: she renews only on an attack in the RENEW
    -- ticks before her 20-tick cycle ends)
    local deadline = CV.renew_deadline(S, F)
    local free = deadline and (deadline - V.RENEW + 1 - F.tick) or 0
    if v.method == "intended" then
        -- (the intended solve feeds through CV.feeder, below)
    elseif v.method == "redemption" then
        -- SYNQ'S FALLBACK (1:27:47-1:28:21): with decent gear the portal dies
        -- before a tagged grub hatches; with "terrible luck" -- a string of
        -- misses, seed 15 -- roots, "around three to four per grub that was
        -- tagged ... to full HP", run in "the exact window": her re-enrage.
        -- Decided there from what the client shows: the portal's bar at the
        -- weapon's rate against the grub's hatch.
        if v.run == nil and nh and v.enr_start == F.tick and v.was_enraged then
            local left = V.PORTAL_HP * (v.portal_frac or 1)
            local death = F.tick + left / V.WEAPONS[v.weapon].rate
            -- (or the measured rate when it is slower -- trips, zeros, a
            -- heal: the thru room's second run was decided on the weapon's
            -- 3.0 with the room going at ~2 and started 8-33 ticks before
            -- the hatch, seeds 16/25/31/36 r2)
            if eta then death = math.max(death, F.tick + eta) end
            list = {}
            local room = CV.free_slots() + blossoms
            for _, g in pairs(v.grub) do
                -- (section 16 E7: a grub hatching before a trip could feed
                -- it is past saving -- the portal first; raid46 r27 a2 ran
                -- for a grub 4 ticks from its hatch with the portal 8 off)
                if g.stung and not g.unfeedable and g.id == ids.grub_infected
                    and CV.hatch_t(g) < death + V.HATCH_MARGIN and CV.hatch_t(g) - F.tick >= V.TRIP_MIN then
                    -- (the budget kit's blossom a trip: fed to OUTLAST the
                    -- portal, not to full -- each trip costs the portal
                    -- RUN_SLOW ticks of its death, so a blossom nets
                    -- BLOSSOM - RUN_SLOW of margin; full is the cap. A
                    -- short estimate is caught at the next re-enrage)
                    local full = math.max(1, math.ceil((V.GRUB_HP - CV.grub_hp(S, F, g)) / V.BLOSSOM))
                    local outlast = math.ceil((death + V.OUTLAST_MARGIN - CV.hatch_t(g)) / (V.BLOSSOM - V.RUN_SLOW))
                    g.plan_n = math.max(1, math.min(full, outlast))
                    g.fed_n = 0
                    g.need = g.plan_n
                    g.ht = CV.hatch_t(g)
                    list[#list + 1] = g
                end
            end
            table.sort(list, function(x, y) return x.ht < y.ht end)
            -- (the free slots bound the blossoms HELD, not the run: the budget
            -- kit has one, so the run picks and feeds a blossom a trip until
            -- the grub is full -- a filter on the run's total kept no grub
            -- needing two or more, and every tag hatched, probe p1 seed 4)
            if room == 0 and not QD.raid._tob_first(S, { ids.vial_empty }) and CV.slot_dose(S, F) == nil then list = {} end
            -- (and a portal the measure kills within ROOT_MARGIN is finished:
            -- a trip costs it ~20 ticks, a soldier's heal is 17 an 8)
            if eta and eta <= V.ROOT_MARGIN then list = {} end
            for _, g in ipairs(list) do total = total + g.need end
        end
    elseif v.run == nil and nh and free >= V.RUN_MIN and F.hp >= V.RUN_HP and (eta == nil or (eta > V.ROOT_FINISH and F.tick + eta > nh.t - V.ROOT_EARLY)) then
        local target = F.tick + (eta or V.HATCH) + V.ROOT_RUN + V.ROOT_MARGIN
        list = {}
        for _, g in pairs(v.grub) do
            if g.stung and not g.unfeedable and (g.id == ids.grub_sickly or g.id == ids.grub_infected) then
                local ht = CV.hatch_t(g)
                -- (a grub fed only to outlast the portal: a one-slot kit nets
                -- ~10 hp a trip against the drain, so feeding to full never
                -- ends -- r4 seed 0 ran laps until it died)
                if ht < target then
                    local hp = CV.grub_hp(S, F, g)
                    g.need = math.min(math.ceil((target - ht) / V.BLOSSOM), math.max(1, math.ceil((V.GRUB_HP - hp) / V.BLOSSOM)))
                    g.ht = ht
                    list[#list + 1] = g
                end
            end
        end
        table.sort(list, function(x, y) return x.ht < y.ht end)
        -- (at most ROOT_MAX blossoms a run, earliest hatch first)
        local keep = {}
        for _, g in ipairs(list) do
            if total + g.need <= V.ROOT_MAX then keep[#keep + 1] = g; total = total + g.need end
        end
        list = keep
        if nh.t - F.tick > V.ROOT_LEAD + V.BLOSSOM * 2 then list = nil end
    end
    if list then
        if #list > 0 then
            v.run = { list = list }
            v.picking, v.feed_g, v.feed_since = nil, nil, nil
            local parts = {}
            for _, g in ipairs(list) do parts[#parts + 1] = string.format("%d,%d t%d x%d", g.x, g.z, g.ht, g.need) end
            QD.raid._tob_trace(S, F.tick, string.format("root run: %d blossoms for %s, portal eta %s",
                total, table.concat(parts, " "), tostring(eta and math.floor(eta))))
        end
    end
    if v.method == "intended" and v.role == "feed" and CV.feeder(S, F, B, intents, blossoms) then return intents end
    -- a run in progress YIELDS to her renewal window: the portal first (the
    -- run, its blossoms and its grub kept), then on with the run
    -- (soon enough to walk back into range: two tiles a tick, and a swing)
    local travel = 2
    if v.portal_sw then
        local far = CV.gap(F.me.x, F.me.z, v.portal_sw.x, v.portal_sw.z, 4)
        travel = math.max(0, far - V.WEAPONS[v.weapon or V.WEAPON].range) // 2 + 3
    end
    local yield = v.run and ((deadline and F.tick + travel >= deadline - V.RENEW + 1
        and (v.attack_t or -1) < deadline - V.RENEW + 1) or (F.hp < V.RUN_HP / 2 and v.method ~= "redemption"))
    if v.run and v.method == "redemption" then
        -- (Synq's run turns back for the window's END, not its start: the
        -- swing renews her anywhere in it -- back to his tile, the lane in,
        -- the swing; the start threw away 7 ticks a run, the thru room's
        -- roots are 8 ticks out, seed 10)
        local back = (QD.raid._tob_cheb(F.me.x, F.me.z, v.safe.x, v.safe.z) + 1) // 2 + V.LANE_TICKS
        -- (or the route to the nearer fire tile and the swing: from the thru
        -- room's north side, his tile is past it -- section 14 T6)
        if v.run and v.fire then
            for _, ft in ipairs(v.fire) do
                local rr, route = api_drive.route(ft.x, ft.z, { run = F.run_on == true })
                if rr == "ok" and route and route.ticks then back = math.min(back, #route.ticks + 2) end
            end
        end
        yield = deadline ~= nil and F.tick + back >= deadline - V.YIELD_MARGIN
            and (v.attack_t or -1) < deadline - V.RENEW + 1
        -- (latched until the renewal swing: the estimate shrinks as I walk
        -- back, and the run took the tick again -- seeds 24/42 r1 flipped
        -- feed/back for 6 ticks)
        if deadline ~= nil and v.yield_at == deadline and (v.attack_t or -1) < deadline - V.RENEW + 1 then yield = true end
    end
    if yield and v.yield_at ~= deadline then
        v.yield_at = deadline
        QD.raid._tob_trace(S, F.tick, "root run yields (renewal window " .. tostring(deadline) .. ", hp " .. F.hp .. ")")
    end
    -- (the root walked to survives only a tick spent walking to it)
    v.root_tgt, v.root_keep = v.root_keep, nil
    v.feed_under = nil
    if v.run and not yield then
        local list = v.run.list
        -- (a fed blossom: the count fell while feeding that grub)
        if v.feed_g and v.feed_bl and blossoms < v.feed_bl then
            local fed = v.feed_bl - blossoms
            local g0 = v.feed_g
            g0.hp_base = math.min(V.GRUB_HP, CV.grub_hp(S, F, g0) + fed * V.BLOSSOM)
            g0.hp_t = F.tick
            g0.need = g0.need - fed
            g0.fed_n = (g0.fed_n or 0) + fed
        end
        v.feed_bl = blossoms
        -- (Synq's run feeds a grub to FULL: its need is what its hp lacks now,
        -- not what the run's start guessed -- seed 4's three blossoms, fed 60
        -- ticks after the guess, left it at 108 and draining)
        if v.method == "redemption" then
            for _, g in ipairs(list) do
                if g.id == ids.grub_infected and not g.unfeedable then
                    g.need = math.max(1, math.ceil((V.GRUB_HP - CV.grub_hp(S, F, g)) / V.BLOSSOM))
                    if g.plan_n then g.need = math.min(g.need, g.plan_n - (g.fed_n or 0)) end
                else
                    g.need = 0
                end
            end
        end
        while #list > 0 and (list[1].need <= 0 or (list[1].id ~= ids.grub_sickly and list[1].id ~= ids.grub_infected)
                or list[1].unfeedable) do
            table.remove(list, 1)
            v.feed_g, v.feed_since = nil, nil
        end
        local need = 0
        for _, g in ipairs(list) do need = need + g.need end
        local g = list[1]
        if g ~= nil and v.method == "redemption" and v.picking == false and blossoms == 0 and need > 0 then
            -- (a trip's blossoms fed, the grub still short: the next trip,
            -- its feed clock new -- the stall rule had counted from the
            -- first trip's feed and abandoned the grub, seeds 0/4/13 r2)
            v.picking, v.feed_g, v.feed_since = nil, nil, nil
        end
        if g == nil or (v.picking == false and blossoms == 0) then
            v.run, v.picking, v.feed_g, v.feed_since = nil, nil, nil, nil
        elseif v.picking ~= false and blossoms < need and CV.free_slots() == 0
            and QD.raid._tob_first(S, { ids.vial_empty }) and not intents.drink then
            -- (a spent gourd vial makes the slot: never kit)
            intents.mode = "roots"
            intents.drop = ids.vial_empty
            return intents
        elseif v.method == "redemption" and v.picking ~= false and blossoms < need and CV.free_slots() == 0
            and CV.slot_dose(S, F) ~= nil then
            -- (section 16 E4: no slot and no empty vial -- a potion's last
            -- dose drunk, its vial smashes; the loop goes on meanwhile)
            if not intents.drink and F.tick - (S.last_drink or -10) >= 3 then
                intents.drink = CV.slot_dose(S, F)
                S.last_drink = F.tick
                QD.raid._tob_trace(S, F.tick, "slot: a last dose drunk for the run")
            end
        elseif v.picking ~= false and blossoms < need and CV.free_slots() > 0 then
            intents.mode = "roots"
            v.picking = true
            local root, app = nil, nil
            if v.method == "redemption" then
                root, app = CV.root_choice(S, F)
            else
                root = CV.nearest_loc(S, F, ids.root)
            end
            -- (blossoms in hand go to the grub once another root would cost
            -- its life: its hp left against the walk to the root, the pick,
            -- and the walk on to it -- seed 4 went back for a fourth with
            -- three held and the grub at 10)
            if root and blossoms > 0 and v.method == "redemption" then
                local trip = (QD.raid._tob_cheb(F.me.x, F.me.z, root.x, root.z)
                    + QD.raid._tob_cheb(root.x, root.z, g.x + 1, g.z + 1)) // 2 + V.PICK_TICKS + V.FEED_MARGIN
                if CV.grub_hp(S, F, g) <= trip then
                    root = nil
                    v.picking = false
                end
            end
            -- (section 16 E2: a trip sets off only when it fits before her
            -- renewal window closes -- the walk there, one pick at least,
            -- the walk back and the swing -- else the loop goes on and the
            -- renewal swing makes the room; a grub near its hatch goes)
            if root and app and v.method == "redemption" and blossoms == 0 and deadline
                and v.trip_dl ~= deadline then
                local fits = F.tick + 2 * app.ticks + V.PICK_TICKS + 2 <= deadline - V.YIELD_MARGIN
                if fits or CV.hatch_t(g) - F.tick <= V.TRIP_FORCE then
                    v.trip_dl = deadline
                else
                    if v.trip_wait ~= deadline then
                        v.trip_wait = deadline
                        QD.raid._tob_trace(S, F.tick, string.format("root trip waits: %d ticks there, window ends %d",
                            app.ticks, deadline))
                    end
                    root, app = nil, nil
                end
            end
            if root and app then
                -- (section 14 T5: the walk is the plan's, around her reach;
                -- the op only from beside the root, and a root that does
                -- not pick is skipped a while)
                local key = root.x .. ":" .. root.z
                local rt = v.root_tgt
                if rt == nil or rt.key ~= key then
                    rt = { key = key, t0 = F.tick, ticks = app.ticks }
                    QD.raid._tob_trace(S, F.tick, string.format("root %s via %d,%d (%d ticks)", key, app.x, app.z, app.ticks))
                end
                v.root_keep = rt
                local bad = false
                if CV.gap(F.me.x, F.me.z, root.x, root.z, 2) <= 1 then
                    rt.beside = rt.beside or F.tick
                    bad = F.tick - rt.beside > V.ROOT_STALL
                    intents.world_op = { kind = "loc", id = ids.root, op = 1, x = root.x, z = root.z }
                else
                    bad = F.tick - rt.t0 > rt.ticks + 2 * V.ROOT_STALL
                    B.pull({ name = "root", x = app.x, z = app.z, size = 1, weight = V.FEED_PULL, t0 = F.tick + 1, t1 = F.tick + B.h })
                end
                if bad then
                    v.root_bad[key] = F.tick + V.ROOT_BAD
                    v.root_keep = nil
                    QD.raid._tob_trace(S, F.tick, "root " .. key .. " stalled: skipped")
                end
                return intents
            elseif root then
                intents.world_op = { kind = "loc", id = ids.root, op = 1, x = root.x, z = root.z }
                return intents
            end
            -- (Synq's run with nothing yet in flower -- they open 10-15
            -- ticks in, a picked one regrows in 8 -- waits on: the loop)
            if v.method ~= "redemption" or blossoms > 0 then v.picking = false end
        elseif v.picking ~= false and blossoms < need then
            v.picking = false   -- (no slot to pick into: feed what is held)
        end
        if v.run and blossoms > 0 then
            v.picking = false
            if v.feed_g ~= g then v.feed_g, v.feed_since, v.feed_n = g, F.tick, 0 end
            -- (the stall counts the ticks spent feeding, not those the run
            -- yielded to her renewal: a yield mid-feed had abandoned a
            -- grub that was being fed, seed 31 r3)
            v.feed_n = (v.feed_n or 0) + 1
            intents.mode = "feed"
            local ft = CV.feed_tile(S, F, g)
            if ft and v.method == "redemption" then
                v.feed_under = CV.gap(ft.x, ft.z, v.home.x, v.home.z, 5) == 0
                local fk = g.x .. ":" .. g.z .. ">" .. ft.x .. ":" .. ft.z
                if v.ft_trace ~= fk then
                    v.ft_trace = fk
                    QD.raid._tob_trace(S, F.tick, string.format("feed %d,%d from %d,%d%s", g.x, g.z, ft.x, ft.z,
                        v.feed_under and " (under her)" or ""))
                end
            end
            -- (section 16 E6: a feed held for her cycle's end is not a
            -- reason to stop shooting -- the loop takes the ticks until a
            -- swing's trip would end past T - 2, then the feed tile; raid21
            -- r27 a1 stood on it holding blossoms, one portal hit a cycle)
            local hold_loop = false
            if ft and v.method == "redemption" and v.enr_start and v.ves and v.ves.npc_id == ids.enraged
                and CV.feed_hold(S, F, g, ft) then
                local T = v.enr_start + V.ENRAGE
                hold_loop = T - 2 - F.tick > V.FEED_LOOP + QD.raid._tob_cheb(F.me.x, F.me.z, ft.x, ft.z) // 2
            end
            if hold_loop then
                v.feed_n = v.feed_n - 1
            elseif ft == nil or v.feed_n > V.FEED_STALL * 3 then
                -- (a grub no tile out of her reach feeds, or the route never
                -- reaches: the next one, or back to the portal)
                QD.raid._tob_trace(S, F.tick, (ft and "feed abandoned at " or "no feed tile at ") .. g.x .. "," .. g.z)
                g.unfeedable = true
            elseif F.me.x == ft.x and F.me.z == ft.z and CV.feed_hold(S, F, g, ft) then
                -- (section 14 T7: the op would step me onto her ring -- wait
                -- on the tile for her cycle's end; the wait is not a stall)
                intents.mode = "feed-wait"
                v.feed_n = v.feed_n - 1
                return intents
            elseif F.me.x == ft.x and F.me.z == ft.z then
                intents.world_op = { kind = "npc", id = g.id, op = 1, x = g.row.x, z = g.row.z }
                -- (the second blossom of a trip: the executive re-sends a
                -- world op only when its key changes, and a feed's key is
                -- the grub's -- the same op named by the grub's element is
                -- a new key; seed 18 r4 stood 14 ticks holding a blossom)
                if (g.fed_n or 0) % 2 == 1 and g.row.element_id then
                    intents.world_op = { kind = "npc", id = g.id, op = 1, element = g.row.element_id }
                end
                return intents
            else
                -- (the walk is the plan's, around her reach; the op only
                -- from the tile, so the server's route never picks her ring)
                B.pull({ name = "feed", x = ft.x, z = ft.z, size = 1, weight = V.FEED_PULL, t0 = F.tick + 1, t1 = F.tick + B.h })
                return intents
            end
        end
    end
    -- (a run that needs more blossoms than the free slots hold: the kit's
    -- ranging potion finished between trips -- it lifts the Ranged the brews
    -- drained, and its vial's shatter is a second slot, two blossoms a trip)
    if v.run and v.method == "redemption" and not intents.drink and F.tick - (S.last_drink or -10) >= 3 then
        local want = 0
        for _, g in ipairs(v.run.list) do want = want + math.max(0, g.need or 0) end
        local free = CV.free_slots()
        local rng = QD.raid._tob_first(S, ids.rangings)
        if rng and free < 2 and want > free + blossoms then
            intents.drink = rng
            S.last_drink = F.tick
        end
    end
    -- THE LOOP: the portal from under her, back to the safe tile (and out of
    -- the loop -- she shoots then -- hp kept up with Xeric's aid)
    intents.mode = (v.ves and v.ves.npc_id == ids.enraged) and "loop" or "open"
    -- Longrange, once the weapon is worn (varp43_com_mode 2; a powered staff's
    -- button writes 3, clamped to 2 at its first swing)
    if W.slot == nil then v.style_ok = true end
    if QD.raid._tob_worn(S, ids[W.obj]) and not v.style_ok then
        local mr, mode = api_drive.varp(ids.com_mode)
        if mr == "ok" and mode >= 2 then
            v.style_ok = true
        elseif F.tick - (v.style_pressed or -10) >= 3 then
            QD.raid._tob_tab(S, ids.combat_tab)
            api_drive.if_click(ids[W.slot], 1)
            v.style_pressed = F.tick
        end
    end
    local grounded = v.ves and v.ves.npc_id == ids.walking
    if QD.raid._tob_worn(S, ids[W.obj]) then
        local far = QD.raid._tob_cheb(F.me.x, F.me.z, v.safe.x, v.safe.z) > V.SAFE_NEAR
            and CV.gap(F.me.x, F.me.z, v.home.x, v.home.z, 5) > 0
        if v.method == "redemption" and far and v.ves and v.ves.npc_id == ids.enraged then
            -- (away from the safe tile -- a feed, a root -- the way back is
            -- walked first: an attack pressed from there is the server's
            -- route to range, which can end on her sting ring; r3 seed 2)
            intents.mode = "back"
        elseif v.method == "intended" then
            -- THE INTENDED SOLVE: grounded, the portal; flying, a soldier
            -- (it heals the portal), the enrage seat's portal hit when her
            -- renewal window is open (or she is not enraged), else her
            local enraged = v.ves and v.ves.npc_id == ids.enraged
            local deadline = CV.renew_deadline(S, F)
            local refresh = v.role == "enrage" and (not enraged
                or (deadline and F.tick >= deadline - V.RENEW + 1 and (v.attack_t or -1) < deadline - V.RENEW + 1))
            local sol = nil
            for _, s in ipairs(v.soldiers) do
                if s.npc_id == ids.soldier and (sol == nil
                    or QD.raid._tob_cheb(F.me.x, F.me.z, s.x, s.z) < QD.raid._tob_cheb(F.me.x, F.me.z, sol.x, sol.z)) then
                    sol = s
                end
            end
            if grounded then
                intents.mode = "ground"
                B.target({ name = "portal", npc = v.portal, size = 4, range = W.range, speed = W.speed, value = V.SWING_VALUE })
            elseif sol then
                intents.mode = "soldier"
                B.target({ name = "soldier", npc = sol, size = 3, range = W.range, speed = W.speed, value = V.SOLDIER_VALUE })
            elseif refresh then
                intents.mode = "refresh"
                B.target({ name = "portal", npc = v.portal, size = 4, range = W.range, speed = W.speed, value = V.REFRESH_VALUE })
            elseif v.ves then
                intents.mode = "her"
                local low = v.ves_pct and v.ves_pct <= V.GROUND_PCT
                B.target({ name = "vespula", npc = v.ves, size = 5, range = W.range, speed = W.speed,
                           value = low and V.HER_LOW_VALUE or V.HER_VALUE })
            end
        elseif v.method ~= "ground" then
            if CV.press_ok(S, F.me.x, F.me.z) then
                B.target({ name = "portal", npc = v.portal, size = 4, range = W.range, speed = W.speed, value = V.SWING_VALUE })
            else
                -- (on the lane off a press tile: on along it to the nearer
                -- fire tile, where the swing fires in place)
                local f1, f2 = v.fire[1], v.fire[2]
                local ft = (QD.raid._tob_cheb(F.me.x, F.me.z, f1.x, f1.z) <= QD.raid._tob_cheb(F.me.x, F.me.z, f2.x, f2.z)) and f1 or f2
                -- (only with the weapon due: cooling down, the safe pull
                -- takes me off her)
                local due = ((S.x2 and S.x2.swing_last) or -100) + W.speed <= F.tick + 2
                if due and CV.gap(F.me.x, F.me.z, v.home.x, v.home.z, 5) == 0 then
                    B.pull({ name = "fire", x = ft.x, z = ft.z, size = 1, weight = V.FEED_PULL, t0 = F.tick + 1, t1 = F.tick + B.h })
                end
            end
        else
            -- THE GROUND METHOD: grounded, the portal from under her; else
            -- her, and the portal only to hold the enrage (or to start it)
            local until_t = CV.enrage_until(S)
            local enraged = v.ves and v.ves.npc_id == ids.enraged
            local refresh = until_t == nil or until_t - F.tick <= V.REFRESH or not enraged
            if grounded then
                intents.mode = "ground"
                B.target({ name = "portal", npc = v.portal, size = 4, range = W.range, speed = W.speed, value = V.SWING_VALUE })
            elseif refresh then
                -- (the refresh owns the swing: a shot at her then lets the
                -- enrage lapse, and calm she tags grubs and shoots poison)
                intents.mode = "refresh"
                B.target({ name = "portal", npc = v.portal, size = 4, range = W.range, speed = W.speed, value = V.REFRESH_VALUE })
            else
                if v.ves then
                    local low = v.ves_pct and v.ves_pct <= V.GROUND_PCT
                    B.target({ name = "vespula", npc = v.ves, size = 5, range = W.range, speed = W.speed,
                               value = low and V.HER_LOW_VALUE or V.HER_VALUE })
                end
            end
        end
        if v.first_swing == nil then v.first_swing = F.tick end
    end
    if not (grounded and v.method == "ground") and v.method ~= "intended" then
        B.pull({ name = "safe", x = v.safe.x, z = v.safe.z, size = 1, weight = V.SAFE_PULL, t0 = F.tick + 1, t1 = F.tick + B.h })
    elseif v.method == "intended" and (intents.mode == "refresh" or intents.mode == "ground") then
        -- (the portal from across the room is past the plan's horizon: the
        -- way there is the safe tile's, beside her hover)
        B.pull({ name = "safe", x = v.safe.x, z = v.safe.z, size = 1, weight = V.SAFE_PULL, t0 = F.tick + 1, t1 = F.tick + B.h })
    end
    if S.debug and F.tick % 5 == 0 then
        local last = S.t2.last
        QD.raid._tob_trace(S, F.tick, string.format("dbg %s tg %d hits %d run %s energy %s last why %s risk %s",
            intents.mode, #B.spec.targets, #B.spec.hits, tostring(B.spec.run), tostring(F.run_energy),
            tostring(last and last.why_name), tostring(last and last.risk)))
    end
    return intents
end

-- Empty tiles between two square footprints (0 = touching).
function CV.rect_gap(ax, az, sa, bx, bz, sb)
    local dx = math.max(0, bx - (ax + sa), ax - (bx + sb))
    local dz = math.max(0, bz - (az + sa), az - (bz + sb))
    return math.max(dx, dz)
end

-- Free backpack slots (the raid kit leaves one: a blossom at a time).
-- The prayer that keeps Redemption sure to fire through a trip under her:
-- REDEEM_PRAYER with the boost lit (both drain a point in ~2 ticks),
-- REDEEM_BARE with Redemption alone.
function CV.redeem_min(S, F)
    local W = CV.V.WEAPONS[S.v.weapon or CV.V.WEAPON]
    if W and W.boost and F.lit[W.boost] then return CV.V.REDEEM_PRAYER end
    return CV.V.REDEEM_BARE
end

-- A held potion's last dose to drink for a slot (section 16 E4), the one
-- the raider can best use now: a restore under 70 prayer, an aid under
-- 85 hp, else an enhance, a restore, an aid; else a restore's or an
-- enhance's lowest dose (a sip toward its last). nil = none held.
function CV.slot_dose(S, F)
    local ids = S.ids
    local r = QD.raid._tob_first(S, ids.one_restores)
    local e = QD.raid._tob_first(S, ids.one_enhances)
    local b = QD.raid._tob_first(S, ids.one_brews)
    if r and F.prayer < 70 then return r end
    if b and F.hp < 85 then return b end
    if e or r or b then return e or r or b end
    -- (none at its last dose: a restore's or an enhance's next sip, the
    -- lowest dose held -- two or three sips make the slot; raid19 r27 met
    -- the room with 0 free and its tag hatched for want of one)
    return QD.raid._tob_first(S, ids.restores) or QD.raid._tob_first(S, ids.enhances)
end

function CV.free_slots()
    local r, free = QD._supply_free_slots()
    if r ~= "ok" then return 0 end
    return free
end

-- The flowered root to walk to (section 14 T5): the one whose approach --
-- the client route's arrival beside it -- is the fewest route ticks away,
-- not the void, and not skipped for a stalled op. -> row, approach {x, z,
-- ticks}, or nil.
function CV.root_choice(S, F)
    local v = S.v
    local lr, rows = api_drive.locs(40)
    if lr ~= "ok" then return nil end
    local best, app = nil, nil
    for _, row in ipairs(rows) do
        if row.loc_id == S.ids.root and CV.in_room(v, row.x, row.z)
            and (v.root_bad[row.x .. ":" .. row.z] or -1) < F.tick then
            -- (the route to the 2x2 loc's approach, run: its ticks are the
            -- trip's; the current target keeps 2 ticks of preference, so the
            -- walk does not flip between two roots as I move)
            local rr, route = api_drive.route(row.x, row.z, { size = 2, run = F.run_on == true })
            if rr == "ok" and route and route.arrive and route.arrive.x then
                local ax, az = route.arrive.x, route.arrive.z
                local t = #(route.ticks or {})
                if v.root_tgt and v.root_tgt.key == row.x .. ":" .. row.z then t = t - 2 end
                if CV.gap(ax, az, row.x, row.z, 2) <= 1 and not (v.void_set and v.void_set[ax .. ":" .. az])
                    and (app == nil or t < app.ticks) then
                    best, app = row, { x = ax, z = az, ticks = math.max(0, t) }
                end
            end
        end
    end
    return best, app
end

function CV.nearest_loc(S, F, id)
    local lr, rows = api_drive.locs(40)
    if lr ~= "ok" then return nil end
    local best, bd = nil, nil
    for _, row in ipairs(rows) do
        if row.loc_id == id and CV.in_room(S.v, row.x, row.z) then
            local d = QD.raid._tob_cheb(F.me.x, F.me.z, row.x, row.z)
            if bd == nil or d < bd then best, bd = row, d end
        end
    end
    return best
end

-- ======================================================================= ROOM

QD.T2.register("cox_vespula", {
    ids = function(opts) return CV.ids(opts) end,

    init = function(S, opts)
        opts = opts or {}
        S.who = "cox_vespula_solve"
        S.debug = opts.debug
        S.t2.plan_opts = { h = opts.h or CV.V.H, beam = opts.beam or CV.V.BEAM }
        -- NOT MY SWING (section 16 E3): a block (a soldier's hit, her
        -- ranged), a root pick and a feed start animations the monitor
        -- counted as swings -- each pushed the weapon clock 6 ticks, and a
        -- soldier hitting every 4 ticks kept the plan from ever swinging
        -- (raid19 r27 t3319-3464). The pick and feed scripts set no delay.
        for _, name in ipairs({ "human_unarmedblock", "human_pickuptable", "human_pickupfloor" }) do
            local sr, sq = api_drive.symbol("seq", name)
            assert(sr == "ok", "cox_vespula: no seq " .. name)
            S.mon.not_swing[sq] = true
        end
        S.v = { weapon = opts.weapon or CV.V.WEAPON, method = opts.method or CV.V.METHOD, role = opts.role, grub = {}, soldier_seen = {}, soldier_down = {}, soldiers_n = 0, splat_seen = {}, tags = 0, root_bad = {},
                dealt = 0, sting_hits = 0, redemptions = 0 }
    end,

    -- (the style is set in the strategy, once the weapon is worn)
    enter = function(S) end,

    predict = function(S, F, B)
        CV.observe(S, F)
        CV.predict(S, F, B)
    end,

    strategy = function(S, F, B) return CV.strategy(S, F, B) end,

    -- done: the portal gone and the boils burst (the room's exit open, V23)
    done = function(S, F)
        local v = S.v
        if v.portal_seen and v.portal == nil and v.room then
            local lr, rows = api_drive.locs(40)
            if lr == "ok" then
                for _, row in ipairs(rows) do
                    if row.loc_id == S.ids.boil_burst and CV.in_room(v, row.x, row.z) then
                        v.exit_open = true
                        v.kill_t = v.kill_t or F.tick
                        -- (settled: the loop's prayers off, hp brewed back)
                        local lit = F.lit.redemption or F.lit[CV.V.WEAPONS[v.weapon].boost]
                        local low = F.hp < CV.V.POST_HP and QD.raid._tob_first(S, S.ids.brews)
                        if lit or low then return nil end
                        return "ok"
                    end
                end
            end
        end
        return nil
    end,

    summary = function(S)
        local v = S.v
        return string.format("vespula: exit %s, %s %s%s, kill %s ticks from the first hit, portal dealt %d, tags %d, soldiers %d, sting hits %d, redemptions %d, landings %d, room %s",
            tostring(v.exit_open == true), v.method, tostring(v.weapon),
            v.role and (" (" .. v.role .. ")") or "", tostring(v.kill_t and v.first_hit and (v.kill_t - v.first_hit)), v.dealt, v.tags, v.soldiers_n, v.sting_hits, v.redemptions, v.landings or 0,
            v.room and (v.room.variant .. "/" .. v.room.turn) or "?")
    end,
})
