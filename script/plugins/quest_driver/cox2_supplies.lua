-- quest-driver / cox2_supplies: the Chambers of Xeric PREP (COX_FINISH_PLAN.md round 2).
-- Owned by the supplies unit. Spec and plan: docs/minigames/cox/rooms/supplies_spec.md,
-- docs/minigames/cox/rooms/supplies_solver.md.
--
--   t.raid.cox_scavengers_solve(opts)  kill a scavenger room's beasts for the
--       secondaries and planks the plan needs; in the large room, take the
--       shortcut first when the tool and level allow.
--   t.raid.cox_resource_solve(opts)    the resource room: tools, plant, gourds,
--       geyser, pick, clean, brew the plan's potions (and, opts.food, a fish or a
--       bat as the room is).
--   t.raid.cox_weeds_solve(opts)       a floor start/end room's weeds: rake until
--       the seeds the plan needs are carried.
--
-- NOT A QD.T2 ROOM. The T2 loop plans walks and swings against a threat field;
-- prep is a sequence of loc, ground-item and backpack ops with nothing to dodge
-- (a scavenger hits 13 at most). It is a reactive task machine over one-packet
-- ops (api_drive.world_op, the ToB idiom: the server walks me) and the
-- backpack verbs, deciding every step from what the client is sent: the
-- backpack, ground items, the room's locs (patch stage, shortcut), npcs and
-- chat lines. No server state, no cheats.

QD.SUP = QD.SUP or {}

-- The plan's solo defaults (supplies_solver.md section 1).
QD.SUP.SCAV_WANT = { raids_endarkened_juice = 8, raids_stinkhorn_mushroom = 3, raids_cicely = 1, raids_plank = 2 }
QD.SUP.MAKE = { aid = 8, revit = 3, enhance = 1, overload = 0 }

-- Potion families: the secondary, the herb, the three tiers' four-dose objs.
QD.SUP.FAMILY = {
    aid = { herb = "raids_buchuleaf", sec = "raids_endarkened_juice",
            pots = { "raids_vial_xericaid_strong_4", "raids_vial_xericaid_4", "raids_vial_xericaid_weak_4" } },
    revit = { herb = "raids_buchuleaf", sec = "raids_stinkhorn_mushroom",
              pots = { "raids_vial_revitalisation_strong_4", "raids_vial_revitalisation_4", "raids_vial_revitalisation_weak_4" } },
    enhance = { herb = "raids_buchuleaf", sec = "raids_cicely",
                pots = { "raids_vial_prayer_strong_4", "raids_vial_prayer_4", "raids_vial_prayer_weak_4" } },
    elder = { herb = "raids_golpar", sec = "raids_stinkhorn_mushroom",
              pots = { "raids_vial_elder_strong_4", "raids_vial_elder_4", "raids_vial_elder_weak_4" } },
    kodai = { herb = "raids_golpar", sec = "raids_endarkened_juice",
              pots = { "raids_vial_kodai_strong_4", "raids_vial_kodai_4", "raids_vial_kodai_weak_4" } },
    twisted = { herb = "raids_golpar", sec = "raids_cicely",
                pots = { "raids_vial_twisted_strong_4", "raids_vial_twisted_4", "raids_vial_twisted_weak_4" } },
}
QD.SUP.OVERLOADS = { "raids_vial_overload_strong_4", "raids_vial_overload_4", "raids_vial_overload_weak_4" }
-- The make menu's cells are the carried secondaries in this order (cox_herblore.rs2
-- ~cox_brew_from_herb: stinkhorn, endarkened juice, cicely; nulls compacted).
QD.SUP.SEC_ORDER = { "raids_stinkhorn_mushroom", "raids_endarkened_juice", "raids_cicely" }
QD.SUP.HERBS = {
    { seed = "raids_seed_buchuleaf", grimy = "raids_grimy_buchuleaf", clean = "raids_buchuleaf", grown = "raids_patch_buchu_fullygrown" },
    { seed = "raids_seed_golpar", grimy = "raids_grimy_golpar", clean = "raids_golpar", grown = "raids_patch_golpar_fullygrown" },
    { seed = "raids_seed_noxifer", grimy = "raids_grimy_noxifer", clean = "raids_noxifer", grown = "raids_patch_noxifer_fullygrown" },
}
QD.SUP.SCAVS = { "raids_scavenger_beast_a", "raids_scavenger_beast_b" }
QD.SUP.PICKAXES = { "bronze_pickaxe", "iron_pickaxe", "steel_pickaxe", "black_pickaxe", "mithril_pickaxe",
                    "adamant_pickaxe", "rune_pickaxe", "dragon_pickaxe", "crystal_pickaxe", "infernal_pickaxe", "3a_pickaxe" }
QD.SUP.AXES = { "bronze_axe", "iron_axe", "steel_axe", "black_axe", "mithril_axe", "adamant_axe", "rune_axe",
                "dragon_axe", "crystal_axe" }
QD.SUP.AIDS = { "raids_vial_xericaid_strong_4", "raids_vial_xericaid_strong_3", "raids_vial_xericaid_strong_2",
                "raids_vial_xericaid_strong_1", "raids_vial_xericaid_4", "raids_vial_xericaid_3" }
QD.SUP.INV, QD.SUP.WORN = 93, 94
-- Old tools: op2 Take Rake, op3 Take Spade, op4 Take Seed dibber (cox_resource.rs2).
QD.SUP.TOOL_OP = { rake = 2, spade = 3, dibber = 4 }

-- ------------------------------------------------------------------ reads

function QD.SUP.id(kind, name)
    QD.SUP._ids = QD.SUP._ids or {}
    local key = kind .. ":" .. name
    local v = QD.SUP._ids[key]
    if v == nil then
        local r, id = api_drive.symbol(kind, name)
        v = (r == "ok") and id or false
        QD.SUP._ids[key] = v
    end
    return v or nil
end

-- (QD.COX.TRACE, t.raid.cox_trace(true): prep's choices to the log)
function QD.SUP.trace(fmt, ...)
    if QD.COX and QD.COX.TRACE then
        api_core.log(string.format("[cox sup t%d] ", api_drive.tick()) .. string.format(fmt, ...))
    end
end

function QD.SUP.count(name, inv)
    local id = QD.SUP.id("obj", name)
    if id == nil then return 0 end
    local r, n = api_drive.inv_count(inv or QD.SUP.INV, id)
    return (r == "ok" and n) or 0
end

-- Carried plus what the private storage held when last looked in.
function QD.SUP.have(name)
    return QD.SUP.count(name) + ((QD.SUP._stored_cache or {})[name] or 0)
end

function QD.SUP.count_any(names)
    local n = 0
    for _, name in ipairs(names) do n = n + QD.SUP.count(name) end
    return n
end

function QD.SUP.holds_any(names)
    for _, name in ipairs(names) do
        if QD.SUP.count(name) > 0 or QD.SUP.count(name, QD.SUP.WORN) > 0 then return true end
    end
    return false
end

function QD.SUP.free()
    local n = 0
    for s = 0, 27 do
        local r, cell = api_drive.inv_slot(QD.SUP.INV, s)
        if r == "ok" and cell.obj_id < 0 then n = n + 1 end
    end
    return n
end

function QD.SUP.hp()
    QD.SUP._stat_ids = QD.SUP._stat_ids or QD.raid._tob_common_ids("cox_supplies", {})
    local r, s = api_drive.skill(QD.SUP._stat_ids.hitpoints)
    if r ~= "ok" then return 99 end
    return s.level
end

function QD.SUP.me()
    local _, t = api_drive.player_tile()
    return t
end

function QD.SUP.in_room(room, x, z, level)
    return x >= room.ox and x < room.ox + 32 and z >= room.oz and z < room.oz + 32 and level == room.level
end

-- The rows of the named locs in `room` (its own floor and 32x32), by name.
function QD.SUP.locs(room, names)
    local want, out = {}, {}
    for _, name in ipairs(names) do
        local id = QD.SUP.id("loc", name)
        if id then want[id] = name end
        out[name] = {}
    end
    local r, rows = api_drive.locs(48)
    if r ~= "ok" then return out end
    for _, row in ipairs(rows) do
        local name = want[row.loc_id] or want[row.resolved_loc_id or -1]
        if name and QD.SUP.in_room(room, row.x, row.z, row.level) then
            local list = out[name]
            list[#list + 1] = row
        end
    end
    return out
end

function QD.SUP.first(room, name)
    return QD.SUP.locs(room, { name })[name][1]
end

function QD.SUP.cheb(ax, az, bx, bz)
    return math.max(math.abs(ax - bx), math.abs(az - bz))
end

function QD.SUP.serial()
    local _, s = api_drive.message_serial()
    return s or 0
end

-- A chat line containing `needle` after serial `since`.
function QD.SUP.said(since, needle)
    local r, rows = api_drive.messages(20)
    if r ~= "ok" then return nil end
    for i = 1, #rows do
        if (rows[i].serial or 0) > since and string.find(rows[i].text or "", needle, 1, true) then
            return rows[i].text
        end
    end
    return nil
end

function QD.SUP.menu_open()
    local id = QD.SUP.id("interface", "skillmulti")
    if id == nil then return false end
    local r, present = api_drive.group_present(id)
    return r == "ok" and present and true or false
end

function QD.SUP.wait(fn, ticks)
    for _ = 1, ticks do
        if fn() then return true end
        QD.ticks(1)
    end
    return fn() and true or false
end

-- ------------------------------------------------------------------ acts

-- One loc op, one packet; the server walks me there.
function QD.SUP.loc_op(row, op)
    return api_drive.world_op("loc", row.loc_id, op, row.element_id)
end

-- A backpack item used on a loc copy (arm, then the one "Use" row).
function QD.SUP.use_on_loc(item, name, row)
    local target = QD.player.by_symbol("loc", name)
    if target == nil then return "not_found" end
    return QD.player.use_on(item, target, { at = { row.x, row.z, row.level } })
end

-- Drink a Xeric's aid when low: prep never needs more (scavengers hit 13 at most).
function QD.SUP.heal(below)
    if QD.SUP.hp() >= (below or 45) then return false end
    for _, name in ipairs(QD.SUP.AIDS) do
        if QD.SUP.count(name) > 0 then
            QD.player.inv_op(name, 1)
            return true
        end
    end
    return false
end

-- Take one ground copy of `name` near me (op 3, the Take row); waits until
-- the backpack count rises.
-- A copy that could not be taken (out of reach: a scavenger can die where no
-- route goes) is not tried again this solve.
function QD.SUP.take(row, name)
    local before = QD.SUP.count(name)
    local key = row.obj_id .. "@" .. row.x .. "," .. row.z
    QD.SUP._banned = QD.SUP._banned or {}
    local r = api_drive.world_op("obj", row.obj_id, 3, row.element_id)
    if r ~= "ok" then
        QD.SUP._banned[key] = true
        QD.ticks(1)
        return r
    end
    if not QD.SUP.wait(function() return QD.SUP.count(name) > before end, 12) then
        QD.SUP._banned[key] = true
        return "unreached"
    end
    return "ok"
end

-- The nearest ground copy, in `room`, of any obj in `names` (name -> true).
function QD.SUP.ground(room, names)
    local ids = {}
    for name in pairs(names) do
        local id = QD.SUP.id("obj", name)
        if id then ids[id] = name end
    end
    local r, rows = api_drive.objs(24)
    if r ~= "ok" then return nil end
    local me = QD.SUP.me()
    local best, bd, bname = nil, nil, nil
    for _, row in ipairs(rows) do
        local name = ids[row.obj_id]
        -- in the room, or close by (a scavenger can chase out of it and die
        -- in the next room)
        if name and (QD.SUP.in_room(room, row.x, row.z, row.level)
                or (row.level == room.level and QD.SUP.cheb(me.x, me.z, row.x, row.z) <= 8))
            and not (QD.SUP._banned or {})[row.obj_id .. "@" .. row.x .. "," .. row.z] then
            local d = QD.SUP.cheb(me.x, me.z, row.x, row.z)
            if bd == nil or d < bd then best, bd, bname = row, d, name end
        end
    end
    return best, bname
end

-- Find and walk into my room (the whole-raid contract: called from where the
-- room before left me -- past its Passage or on its way-out edge -- with no
-- coordinates): the room I stand in, else the room ahead of it; opts.cx/cz
-- (a room test's ::coxtest centre) only as a last resort.
function QD.SUP.enter(names, opts)
    local function is(r)
        if r == nil then return false end
        for _, n in ipairs(names) do if r.name == n then return true end end
        return false
    end
    local room = QD.COX.here()
    if is(room) then return room end
    local targets = {}
    if room ~= nil then
        local _, _, nx, nz = QD.COX.next_origin(room)
        targets[#targets + 1] = { nx, nz }
    end
    if opts.cx then targets[#targets + 1] = { opts.cx, opts.cz } end
    for _, tg in ipairs(targets) do
        local _, me = api_drive.player_tile()
        local ahead = me and QD.COX.room_at(tg[1], tg[2], me.level)
        if ahead == nil or is(ahead) then
            QD.raid.cox_walk_near(tg[1], tg[2], 60)
            room = QD.COX.here()
            if is(room) then return room end
        end
    end
    return nil
end

function QD.SUP.summary(parts)
    local out = {}
    for _, p in ipairs(parts) do out[#out + 1] = p[1] .. "=" .. tostring(QD.SUP.count(p[2])) end
    return table.concat(out, " ")
end

-- ------------------------------------------------------------------ scavengers

-- The large room's shortcut: rocks need a pickaxe, the sapling an axe, the
-- boulder only Strength. -> "ok" cleared | "none" | "no_tool" | "refused" (text)
function QD.SUP.shortcut(room)
    local rows = QD.SUP.locs(room, { "raids_corridor_rocks", "raids_corridor_roots", "raids_corridor_boulder" })
    local name, row
    for _, n in ipairs({ "raids_corridor_rocks", "raids_corridor_roots", "raids_corridor_boulder" }) do
        if rows[n][1] then name, row = n, rows[n][1] end
    end
    if row == nil then return "none", "no blocking obstacle in this room" end
    if name == "raids_corridor_rocks" and not QD.SUP.holds_any(QD.SUP.PICKAXES) then return "no_tool", name end
    if name == "raids_corridor_roots" and not QD.SUP.holds_any(QD.SUP.AXES) then return "no_tool", name end
    local since = QD.SUP.serial()
    QD.SUP.loc_op(row, 1)
    local cleared = QD.SUP.wait(function()
        return QD.SUP.first(room, name) == nil or QD.SUP.said(since, "You need a") ~= nil
    end, 30)
    local refused = QD.SUP.said(since, "You need a")
    if refused then return "refused", refused end
    if cleared then return "ok", name end
    return "timeout", name
end

function QD.SUP.scav_done(want)
    for name, n in pairs(want) do
        if QD.SUP.have(name) < n then return false end
    end
    return true
end

-- A row whose health bar reads 0 is dead (its death seq playing); no bar
-- yet (-1) or a bar above 0 is alive.
function QD.SUP.row_alive(row)
    local ratio, scale = row.health_ratio, row.health_scale
    if ratio == nil or scale == nil or scale <= 0 or ratio < 0 then return true end
    return ratio > 0
end

-- The nearest live scavenger in the room (npc row), or nil.
function QD.SUP.scavenger(room)
    local ids = {}
    for _, s in ipairs(QD.SUP.SCAVS) do
        local id = QD.SUP.id("npc", s)
        if id then ids[id] = true end
    end
    local r, rows = api_drive.npcs(32)
    if r ~= "ok" then return nil end
    local me = QD.SUP.me()
    local best, bd = nil, nil
    for _, row in ipairs(rows) do
        local x, z = row.server_x or row.x, row.server_z or row.z
        if (ids[row.npc_id] or ids[row.base_npc_id or -1]) and QD.SUP.in_room(room, x, z, row.level or room.level)
            and QD.SUP.row_alive(row) then
            local d = QD.SUP.cheb(me.x, me.z, x, z)
            if bd == nil or d < bd then best, bd = row, d end
        end
    end
    return best
end

function QD.SUP.npc_alive(element)
    local r, rows = api_drive.npcs(32)
    if r ~= "ok" then return false end
    for _, row in ipairs(rows) do
        if row.element_id == element then
            return QD.SUP.row_alive(row)
        end
    end
    return false
end

-- Kill one scavenger: Attack (op 2) as one packet, re-pressed every 8 ticks
-- while it lives, until it dies or `ticks` pass.
function QD.SUP.kill(row, ticks)
    local deadline = api_drive.tick() + ticks
    local pressed = -100
    while api_drive.tick() < deadline do
        if not QD.SUP.npc_alive(row.element_id) then return true end
        QD.SUP.heal(45)
        if api_drive.tick() - pressed >= 8 then
            api_drive.world_op("npc", row.npc_id, 2, row.element_id)
            pressed = api_drive.tick()
        end
        QD.ticks(1)
    end
    return not QD.SUP.npc_alive(row.element_id)
end

-- Room for one more stack of `name`: a stackable already carried needs none;
-- else a free slot, made by dropping a spent empty vial or gourd (never kit).
function QD.SUP.make_room(name)
    if QD.SUP.count(name) > 0 or QD.SUP.free() > 0 then return true end
    for _, spent in ipairs({ "raids_vial_empty", "vial_empty" }) do
        if QD.SUP.count(spent) > 0 then
            QD.player.drop(spent)
            QD.ticks(1)
            if QD.SUP.free() > 0 then return true end
        end
    end
    return false
end

function QD.SUP.scavengers(opts)
    opts = opts or {}
    -- THE NEED: opts.want (a room test), else the plan's potions minus what I
    -- carry, as secondaries; planks only when asked
    local want = opts.want or QD.SUP.secondaries_wanted(opts.make)
    local room = QD.SUP.enter({ "scavenger_small", "scavenger_large" }, opts)
    if room == nil then return "not_found", "not in a scavenger room" end
    QD.SUP._banned = {}
    local start = api_drive.tick()
    local deadline = start + (opts.max_ticks or 1200)
    local max_kills = opts.max_kills or 30
    local kills, takes, final = 0, 0, 0
    local dry = 0        -- kills since a wanted count last rose
    local full = {}      -- wanted names there is no room for
    local why = ""
    local shortcut, shortcut_detail = "none", ""
    if room.name == "scavenger_large" and opts.shortcut ~= false then
        shortcut, shortcut_detail = QD.SUP.shortcut(room)
    end
    local function progress()
        local n = 0
        for name in pairs(want) do n = n + QD.SUP.count(name) end
        return n
    end
    local last = progress()
    local clearing = false
    while api_drive.tick() < deadline do
        local short = {}
        for name, n in pairs(want) do
            if QD.SUP.have(name) < n and not full[name] then short[name] = true end
        end
        if not clearing then
            if next(short) == nil then
                clearing = true
                why = QD.SUP.scav_done(want) and "needs met" or "no room for the rest"
            elseif kills >= max_kills then
                clearing, why = true, "kill cap"
            elseif dry >= 12 then
                clearing, why = true, "12 kills without a needed drop"
            end
        end
        if clearing then
            -- THE ROOM LEFT CLEAR: every beast standing now dies before I go
            -- (an aggressive beast left alive follows the raider out); at most
            -- three more, as each kill respawns one 2 ticks later
            local scav = QD.SUP.scavenger(room)
            if scav == nil or final >= 3 then break end
            if QD.SUP.kill(scav, 40) then kills, final = kills + 1, final + 1 end
            QD.ticks(1)
            goto continue
        end
        QD.SUP.heal(45)
        -- a tool the shortcut was waiting on
        if shortcut == "no_tool" then
            local tool = (shortcut_detail == "raids_corridor_rocks") and "iron_pickaxe" or "iron_axe"
            if not full[tool] then short[tool] = true end
        end
        do
            local drop, dname = QD.SUP.ground(room, short)
            if drop then
                if QD.SUP.make_room(dname) then
                    QD.SUP.take(drop, dname)
                    takes = takes + 1
                    if shortcut == "no_tool" and (dname == "iron_pickaxe" or dname == "iron_axe") then
                        shortcut, shortcut_detail = QD.SUP.shortcut(room)
                    end
                else
                    full[dname] = true
                    QD.ticks(1)
                end
            else
                local scav = QD.SUP.scavenger(room)
                if scav then
                    if QD.SUP.kill(scav, 40) then
                        kills = kills + 1
                        dry = dry + 1
                    end
                end
                QD.ticks(1)
            end
        end
        if progress() > last then last, dry = progress(), 0 end
        ::continue::
    end
    local wants = {}
    for name, n in pairs(want) do wants[#wants + 1] = string.gsub(name, "raids_", "") .. ">=" .. n end
    local detail = string.format("%s kills %d takes %d shortcut %s(%s) ticks %d; %s; want [%s] %s", room.name, kills, takes,
        shortcut, tostring(shortcut_detail), api_drive.tick() - start,
        QD.SUP.summary({ { "juice", "raids_endarkened_juice" }, { "stinkhorn", "raids_stinkhorn_mushroom" },
                         { "cicely", "raids_cicely" }, { "planks", "raids_plank" } }), table.concat(wants, " "), why)
    if clearing then return "ok", detail end
    return "timeout", detail
end

-- ------------------------------------------------------------------ resource room

function QD.SUP.made(family)
    if family == "overload" then return QD.SUP.count_any(QD.SUP.OVERLOADS) end
    return QD.SUP.count_any(QD.SUP.FAMILY[family].pots)
end

-- Every potion of a family carried, any tier, any dose (one item = one).
-- What a raider brings in from outside (QD.COX.SUPPLY's "#" stems), by family.
QD.SUP.OUTSIDE = { brew = "potionofsaradomin", superrestore = "2restore", prayerpot = "prayerrestore" }

function QD.SUP.stock(family)
    if QD.SUP.OUTSIDE[family] then
        local n = 0
        for d = 1, 4 do n = n + QD.SUP.count(d .. "dose" .. QD.SUP.OUTSIDE[family]) end
        return n
    end
    local pots = family == "overload" and QD.SUP.OVERLOADS or QD.SUP.FAMILY[family].pots
    local n = 0
    for _, p4 in ipairs(pots) do
        local stem = string.sub(p4, 1, -2)
        for d = 1, 4 do n = n + QD.SUP.count(stem .. d) end
    end
    return n
end

-- What is still to brew: the plan's target minus what I carry (the relay's
-- potions count), { family = n }; an overload needs 4-dose elder/kodai/twisted.
function QD.SUP.todo(make)
    local todo = {}
    for _, fam in ipairs({ "aid", "revit", "enhance" }) do
        local n = (make[fam] or 0) - QD.SUP.stock(fam)
        if n > 0 then todo[fam] = n end
    end
    local ov = (make.overload or 0) - QD.SUP.stock("overload")
    if ov > 0 then
        todo.overload = ov
        for _, fam in ipairs({ "elder", "kodai", "twisted" }) do
            local n = ov - QD.SUP.made(fam)
            if n > 0 then todo[fam] = n end
        end
    end
    return todo
end

-- The secondaries the plan still needs, net of the potions carried:
-- { secondary name = n } (what a scavenger room should loot).
function QD.SUP.secondaries_wanted(make)
    local todo = QD.SUP.todo(make or QD.SUP.MAKE)
    local want = {}
    local function add(name, n)
        if n and n > 0 then want[name] = (want[name] or 0) + n end
    end
    add("raids_endarkened_juice", todo.aid)
    add("raids_stinkhorn_mushroom", todo.revit)
    add("raids_cicely", todo.enhance)
    add("raids_stinkhorn_mushroom", todo.elder)
    add("raids_endarkened_juice", todo.kodai)
    add("raids_cicely", todo.twisted)
    return want
end

-- Herbs a family still needs, by clean-herb name.
function QD.SUP.herbs_needed(todo)
    local need = {}
    for fam, n in pairs(todo) do
        if fam == "overload" then
            need.raids_noxifer = (need.raids_noxifer or 0) + n
        elseif QD.SUP.FAMILY[fam] then
            local h = QD.SUP.FAMILY[fam].herb
            need[h] = (need[h] or 0) + n
        end
    end
    return need
end

-- Which seed to plant next: the herb most short (after what is carried and growing).
function QD.SUP.seed_to_plant(need, growing)
    for _, h in ipairs(QD.SUP.HERBS) do
        local have = QD.SUP.count(h.clean) + QD.SUP.count(h.grimy) + (growing[h.clean] or 0)
        if (need[h.clean] or 0) > have and QD.SUP.count(h.seed) > 0 then return h end
    end
    return nil
end

-- Brew one family's potion: herb on a water vial, the make menu answered.
function QD.SUP.brew(fam)
    local F = QD.SUP.FAMILY[fam]
    local before = QD.SUP.made(fam)
    local since = QD.SUP.serial()
    QD.player.use_item_on_item(F.herb, "raids_vial_water")
    local menu = false
    QD.SUP.wait(function()
        if QD.SUP.made(fam) > before then return true end
        menu = QD.SUP.menu_open()
        return menu
    end, 4)
    if menu and QD.SUP.made(fam) == before then
        local index = 0
        for _, sec in ipairs(QD.SUP.SEC_ORDER) do
            local present = QD.SUP.count(sec) > 0
            if F.herb == "raids_noxifer" then present = (sec == "raids_cicely") and present end
            if present then index = index + 1 end
            if sec == F.sec then break end
        end
        local letter = string.sub("abc", index, index)
        local cr, cell = QD.ui.widget("skillmulti:" .. letter)
        if cr == "ok" then QD.ui.invoke(cell, 1) end
        QD.SUP.wait(function() return QD.SUP.made(fam) > before end, 4)
    end
    return QD.SUP.made(fam) > before, QD.SUP.said(since, "You need")
end

-- The overload: noxifer used on the elder (all three 4-dose components carried).
function QD.SUP.brew_overload()
    local before = QD.SUP.made("overload")
    local elder
    for _, p in ipairs(QD.SUP.FAMILY.elder.pots) do if QD.SUP.count(p) > 0 then elder = p break end end
    if elder == nil then return false end
    QD.player.use_item_on_item("raids_noxifer", elder)
    QD.SUP.wait(function() return QD.SUP.made("overload") > before or QD.SUP.menu_open() end, 4)
    if QD.SUP.made("overload") == before and QD.SUP.menu_open() then
        local cr, cell = QD.ui.widget("skillmulti:a")
        if cr == "ok" then QD.ui.invoke(cell, 1) end
        QD.SUP.wait(function() return QD.SUP.made("overload") > before end, 4)
    end
    return QD.SUP.made("overload") > before
end

-- The fishing spot to try next (the snake moves; a refused spot is skipped
-- for 50 ticks), or nil.
function QD.SUP.fish_spot(room, banned)
    local rows = QD.SUP.locs(room, { "raids_floor_water_edge1_fishing" })["raids_floor_water_edge1_fishing"]
    local now = api_drive.tick()
    for _, row in ipairs(rows) do
        local key = row.x .. "," .. row.z
        if (banned[key] or -1000) < now - 50 then return row, key end
    end
    return nil
end

function QD.SUP.resource(opts)
    opts = opts or {}
    local make = {}
    for k, v in pairs(opts.make or QD.SUP.MAKE) do make[k] = v end
    local banked, bank_full = 0, false
    local room = QD.SUP.enter({ "resource", "resource_b" }, opts)
    if room == nil then return "not_found", "not in a resource room" end
    QD.SUP._banned = {}
    local start = api_drive.tick()
    local deadline = start + (opts.max_ticks or 1500)
    local planted, picks, brews, foods, steps, cleared = 0, 0, 0, 0, 0, 0
    local banned, why = {}, ""
    local food_want = opts.food or 0
    local food_got = 0
    local food_tries = 0
    local last_sig, last_change = nil, api_drive.tick()
    QD.SUP._growing = {}
    while api_drive.tick() < deadline do
        steps = steps + 1
        QD.SUP.heal(45)
        local need = QD.SUP.herbs_needed(QD.SUP.todo(make))
        local L = QD.SUP.locs(room, { "raids_farming_tools", "raids_patch_empty", "raids_gourd_tree", "raids_geyser",
            "raids_patch_buchu_fullygrown", "raids_patch_golpar_fullygrown", "raids_patch_noxifer_fullygrown",
            "raids_patch_buchu_seed", "raids_patch_golpar_seed", "raids_patch_noxifer_seed",
            "raids_patch_buchu_growth1", "raids_patch_buchu_growth2", "raids_patch_buchu_growth3",
            "raids_patch_golpar_growth1", "raids_patch_golpar_growth2", "raids_patch_golpar_growth3",
            "raids_patch_noxifer_growth1", "raids_patch_noxifer_growth2", "raids_patch_noxifer_growth3" })
        -- herbs growing, by clean name (a patch is counted as 8: it gave 8-20 in the sweeps, so a need above 8 plants both, as Beleti does)
        local growing = {}
        for _, h in ipairs(QD.SUP.HERBS) do
            local stem = string.match(h.grown, "raids_patch_(%a+)_")
            local n = 0
            for _, stage in ipairs({ "_seed", "_growth1", "_growth2", "_growth3", "_fullygrown" }) do
                n = n + #(L["raids_patch_" .. stem .. stage] or {})
            end
            growing[h.clean] = n * 8
        end
        QD.SUP._growing = {}
        for clean, n in pairs(growing) do if n > 0 then QD.SUP._growing[clean] = true end end
        local todo = QD.SUP.doable(QD.SUP.todo(make), room)
        local food_left = food_got < food_want and food_tries < 12
        -- the tools the relay carries on (a rake for the floor's weeds), taken
        -- whatever this room can make
        local tool_left = nil
        for _, tool in ipairs(opts.take_tools or {}) do
            if tool_left == nil and QD.SUP.count(tool) == 0 and QD.SUP.free() > 0 and L.raids_farming_tools[1] then tool_left = tool end
        end
        if tool_left then
            QD.SUP.loc_op(L.raids_farming_tools[1], QD.SUP.TOOL_OP[tool_left])
            QD.SUP.wait(function() return QD.SUP.count(tool_left) > 0 end, 20)
            goto next_step
        end
        if next(todo) == nil and not food_left then
            why = why .. " nothing (more) to make"
            break
        end
        -- NO PROGRESS: the backpack and the room unchanged for STALL ticks
        -- (a full pack, a herb that will not come) -- the prep is as done as
        -- it can be here
        local sig = QD.SUP.signature()
        if sig ~= last_sig then last_sig, last_change = sig, api_drive.tick() end
        if api_drive.tick() - last_change > (opts.stall or 150) then
            why = why .. " stalled (pack " .. QD.SUP.free() .. " free)"
            break
        end
        -- 0. a full pack mid-prep (opts.bank: gear parked): finished
        --    potions into private storage, and the plan net of them
        local bank_fams = { aid = true, revit = true, enhance = true, overload = true }
        local bank_carried = 0
        for fam in pairs(bank_fams) do bank_carried = bank_carried + QD.SUP.stock(fam) end
        if opts.bank and not bank_full and QD.SUP.free() <= 2 and bank_carried > 0 then
            local fams = bank_fams
            QD.SUP.trace("bank: carried %d", bank_carried)
            if QD.SUP.store_open(room) then
                local before = {}
                for fam in pairs(fams) do before[fam] = QD.SUP.stock(fam) end
                local n = QD.SUP.store_put(function(name) return fams[QD.SUP.potion_family(name) or ""] == true end, 28)
                for fam in pairs(fams) do
                    local moved = before[fam] - QD.SUP.stock(fam)
                    if moved > 0 and make[fam] then make[fam] = make[fam] - moved end
                    banked = banked + math.max(0, moved)
                end
                if n == 0 then bank_full = true end
                QD.SUP.store_close()
            else
                bank_full = true
            end
            goto next_step
        end
        local water = QD.SUP.count("raids_vial_water")
        local empty = QD.SUP.count("raids_vial_empty")
        local free = QD.SUP.free()
        local acted = false
        if steps % 10 == 1 then
            local parts = {}
            for fam, n in pairs(todo) do parts[#parts + 1] = fam .. ":" .. n end
            local np = {}
            for h, n in pairs(need) do np[#np + 1] = h .. ":" .. n end
            QD.SUP.trace("step %d free %d water %d empty %d todo[%s] need[%s] sig %s", steps, free, water, empty,
                table.concat(parts, " "), table.concat(np, " "), tostring(sig))
        end
        -- 1. brew what can be brewed now (overload components first)
        -- (the Olm kit's order: overload components, enhance, aid, revit)
        for _, fam in ipairs({ "elder", "kodai", "twisted", "enhance", "aid", "revit" }) do
            if not acted and todo[fam] and water > 0 and QD.SUP.count(QD.SUP.FAMILY[fam].herb) > 0
                and QD.SUP.count(QD.SUP.FAMILY[fam].sec) > 0 then
                local ok, refused = QD.SUP.brew(fam)
                if ok then brews = brews + 1 end
                if refused then why = refused end
                acted = true
            end
        end
        if not acted and todo.overload and QD.SUP.count("raids_noxifer") > 0 and QD.SUP.made("elder") > 0
            and QD.SUP.made("kodai") > 0 and QD.SUP.made("twisted") > 0 then
            if QD.SUP.brew_overload() then brews = brews + 1 end
            acted = true
        end
        -- 2. clean a grimy herb that is needed
        if not acted then
            for _, h in ipairs(QD.SUP.HERBS) do
                if not acted and QD.SUP.count(h.grimy) > 0 and (need[h.clean] or 0) > QD.SUP.count(h.clean) then
                    QD.player.inv_op(h.grimy, 1)
                    acted = true
                end
            end
        end
        -- 3. tools: a seed dibber to plant, a spade to pick
        if not acted and L.raids_farming_tools[1] then
            local want_dibber = QD.SUP.count("dibber") == 0 and #L.raids_patch_empty > 0 and QD.SUP.seed_to_plant(need, growing) ~= nil
            local want_spade = QD.SUP.count("spade") == 0
            if (want_dibber or want_spade) and free > 0 then
                QD.SUP.loc_op(L.raids_farming_tools[1], want_dibber and 4 or 3)
                QD.SUP.wait(function() return (not want_dibber or QD.SUP.count("dibber") > 0) and (want_dibber or QD.SUP.count("spade") > 0) end, 20)
                acted = true
            end
        end
        -- 4. plant the most needed seed in an empty patch
        if not acted and QD.SUP.count("dibber") > 0 and L.raids_patch_empty[1] then
            local h = QD.SUP.seed_to_plant(need, growing)
            if h then
                QD.SUP.use_on_loc(h.seed, "raids_patch_empty", L.raids_patch_empty[1])
                QD.ticks(1)
                planted = planted + 1
                acted = true
            end
        end
        -- 5. water: fill the empties
        local potions_left = 0
        for fam, n in pairs(todo) do if fam ~= "overload" then potions_left = potions_left + n end end
        if not acted and empty > 0 and L.raids_geyser[1] then
            QD.SUP.loc_op(L.raids_geyser[1], 1)
            QD.SUP.wait(function() return QD.SUP.count("raids_vial_empty") == 0 end, 20)
            acted = true
        end
        -- 6. gourds, up to what is left to brew, keeping four slots for herbs
        -- (a potion takes a vial AND a herb: half the free slots, less two)
        if not acted and water + empty < potions_left and free > 4 and L.raids_gourd_tree[1] then
            local target = math.min(potions_left - water - empty, (free - 2) // 2)
            local got0 = QD.SUP.count("raids_vial_empty")
            QD.SUP.loc_op(L.raids_gourd_tree[1], 1)
            QD.SUP.wait(function() return QD.SUP.count("raids_vial_empty") >= got0 + target end, 4 + 2 * target + 20)
            -- Pick goes on until the pack is full: a walk order stops it
            local me = QD.SUP.me()
            api_drive.move_to(me.x, me.z)
            QD.ticks(1)
            acted = true
        end
        -- 7. pick a grown patch whose herb is still needed (a spade; the server
        --    keeps picking until the patch is spent or the pack is full)
        if not acted and QD.SUP.count("spade") > 0 and free > 0 then
            for _, h in ipairs(QD.SUP.HERBS) do
                local row = L[h.grown] and L[h.grown][1]
                if not acted and row and (need[h.clean] or 0) > QD.SUP.count(h.clean) + QD.SUP.count(h.grimy) then
                    QD.SUP.loc_op(row, 1)
                    -- the server picks on until the patch is spent or the pack
                    -- is full: a walk order stops it once the need is carried
                    -- (the herbs past it would crowd out the gourds)
                    local enough = QD.SUP.wait(function()
                        return QD.SUP.free() == 0 or QD.SUP.first(room, h.grown) == nil
                            or QD.SUP.count(h.clean) + QD.SUP.count(h.grimy) >= (need[h.clean] or 0)
                    end, 120)
                    if enough and QD.SUP.first(room, h.grown) ~= nil then
                        local me = QD.SUP.me()
                        api_drive.move_to(me.x, me.z)
                        QD.ticks(1)
                    end
                    picks = picks + 1
                    acted = true
                end
            end
        end
        -- 7b. a grown patch whose herb is no longer needed, when another herb
        --     is short and its seed carried and no patch is empty: clear it
        --     (op3, a spade) and plant that one
        if not acted and QD.SUP.count("spade") > 0 and L.raids_patch_empty[1] == nil then
            local wanted = QD.SUP.seed_to_plant(need, growing)
            if wanted then
                for _, h in ipairs(QD.SUP.HERBS) do
                    local row = L[h.grown] and L[h.grown][1]
                    if not acted and row and h ~= wanted and (need[h.clean] or 0) <= QD.SUP.count(h.clean) + QD.SUP.count(h.grimy) then
                        QD.SUP.loc_op(row, 3)
                        QD.SUP.wait(function() return QD.SUP.first(room, "raids_patch_empty") ~= nil end, 20)
                        cleared = cleared + 1
                        acted = true
                    end
                end
            end
        end
        -- 8. a needed herb on the floor (picked with a full pack)
        if not acted and free > 0 then
            local short = {}
            for _, h in ipairs(QD.SUP.HERBS) do
                if (need[h.clean] or 0) > QD.SUP.count(h.clean) + QD.SUP.count(h.grimy) then short[h.grimy] = true end
            end
            local drop, dname = QD.SUP.ground(room, short)
            if drop then
                QD.SUP.take(drop, dname)
                acted = true
            end
        end
        -- 9. food while waiting, as the room is (A: fish, B: bats)
        if not acted and food_got < food_want and food_tries < 12 and free > 1 then
            if room.name == "resource" and QD.SUP.count("fishing_rod") + QD.SUP.count("fishing_rod", QD.SUP.WORN) > 0
                and QD.SUP.count("raids_fishingbait") > 0 then
                local spot, key = QD.SUP.fish_spot(room, banned)
                if spot then
                    local raw0 = QD.SUP.count_any({ "raids_fish0_raw", "raids_fish1_raw", "raids_fish2_raw", "raids_fish3_raw",
                        "raids_fish4_raw", "raids_fish5_raw", "raids_fish6_raw" })
                    local since = QD.SUP.serial()
                    QD.SUP.loc_op(spot, 1)
                    QD.SUP.wait(function()
                        return QD.SUP.said(since, "very bad moment") ~= nil or QD.SUP.count_any({ "raids_fish0_raw", "raids_fish1_raw",
                            "raids_fish2_raw", "raids_fish3_raw", "raids_fish4_raw", "raids_fish5_raw", "raids_fish6_raw" }) > raw0
                    end, 30)
                    food_tries = food_tries + 1
                    if QD.SUP.said(since, "very bad moment") then
                        banned[key] = api_drive.tick()
                        -- step away: the snake bites whoever stands by the spot
                        local me = QD.SUP.me()
                        api_drive.move_to(me.x + (me.x - spot.x), me.z + (me.z - spot.z))
                        QD.ticks(2)
                    elseif QD.SUP.count_any({ "raids_fish0_raw", "raids_fish1_raw", "raids_fish2_raw", "raids_fish3_raw",
                            "raids_fish4_raw", "raids_fish5_raw", "raids_fish6_raw" }) > raw0 then
                        -- (a try whose wait ran out caught nothing: it is a try, not food)
                        food_got = food_got + 1
                        foods = foods + 1
                    end
                    acted = true
                end
            elseif room.name == "resource_b" and QD.SUP.holds_any({ "hunting_butterfly_net" }) then
                local bat = nil
                local bats = {}
                for i = 0, 6 do
                    local id = QD.SUP.id("npc", "raids_bat_" .. i)
                    if id then bats[id] = true end
                end
                local r, rows = api_drive.npcs(32)
                if r == "ok" then
                    for _, row in ipairs(rows) do
                        local x, z = row.server_x or row.x, row.server_z or row.z
                        if bats[row.npc_id] and QD.SUP.in_room(room, x, z, row.level or room.level) then bat = row break end
                    end
                end
                if bat then
                    -- a net must be WIELDED (wiki Food: "a butterfly net ... is
                    -- required"; cox_bats.rs2 ~hunter_has_butterfly_net); the
                    -- weapon goes back on after
                    local _, cell = api_drive.inv_slot(QD.SUP.WORN, 3)
                    local weapon = nil
                    if cell and cell.obj_id >= 0 then
                        local _, nm = api_drive.symbol_name("obj", cell.obj_id)
                        weapon = nm
                    end
                    if weapon ~= "hunting_butterfly_net" and QD.SUP.count("hunting_butterfly_net") > 0 then
                        QD.player.equip("hunting_butterfly_net")
                    end
                    local raw0 = QD.SUP.free()
                    api_drive.world_op("npc", bat.npc_id, 1, bat.element_id)
                    if QD.SUP.wait(function() return QD.SUP.free() < raw0 end, 16) then
                        food_got = food_got + 1
                        foods = foods + 1
                    end
                    food_tries = food_tries + 1
                    if weapon and weapon ~= "hunting_butterfly_net" and QD.SUP.count(weapon) > 0 then
                        QD.player.equip(weapon)
                    end
                    acted = true
                end
            end
        end
        if not acted then QD.ticks(1) end
        ::next_step::
    end
    local todo = QD.SUP.todo(make)
    local doable_left = QD.SUP.doable(todo, room)
    local left = {}
    for fam, n in pairs(todo) do left[#left + 1] = fam .. ":" .. n end
    local detail = string.format("%s planted %d cleared %d picks %d brews %d banked %d food %d ticks %d left [%s] %s; %s", room.name,
        planted, cleared, picks, brews, banked, foods, api_drive.tick() - start, table.concat(left, " "), why,
        QD.SUP.summary({ { "aid", "raids_vial_xericaid_strong_4" }, { "revit", "raids_vial_revitalisation_strong_4" },
                         { "enhance", "raids_vial_prayer_strong_4" }, { "overload", "raids_vial_overload_strong_4" } }))
    if next(doable_left) == nil or string.find(why, "stalled", 1, true) then return "ok", detail end
    return "timeout", detail
end

-- The families that can still be made here: a secondary carried, and a herb
-- carried, growing, on the floor or as a seed (an overload: its components'
-- secondaries, or the components themselves, and a noxifer or its seed).
function QD.SUP.doable(todo, room)
    local out = {}
    local function herb_ok(clean)
        for _, h in ipairs(QD.SUP.HERBS) do
            if h.clean == clean then
                if QD.SUP.count(h.clean) + QD.SUP.count(h.grimy) + QD.SUP.count(h.seed) > 0
                    or (QD.SUP._growing or {})[h.clean] then return true end
                -- picked with a full pack: on the floor
                return room ~= nil and QD.SUP.ground(room, { [h.grimy] = true }) ~= nil
            end
        end
        return false
    end
    for fam, n in pairs(todo) do
        local F = QD.SUP.FAMILY[fam]
        if F then
            if QD.SUP.count(F.sec) > 0 and herb_ok(F.herb) then out[fam] = n end
        elseif fam == "overload" then
            if herb_ok("raids_noxifer") then out[fam] = n end
        end
    end
    -- an overload with no way to its components is not doable either
    if out.overload and not (out.elder or QD.SUP.made("elder") > 0) then out.overload = nil end
    return out
end

-- A digest of everything prep changes: potions, herbs, seeds, vials, the free slots.
function QD.SUP.signature()
    local parts = { QD.SUP.free(), QD.SUP.count("raids_vial_water"), QD.SUP.count("raids_vial_empty") }
    for _, h in ipairs(QD.SUP.HERBS) do
        parts[#parts + 1] = QD.SUP.count(h.clean) .. "/" .. QD.SUP.count(h.grimy) .. "/" .. QD.SUP.count(h.seed)
    end
    for _, fam in ipairs({ "aid", "revit", "enhance", "elder", "kodai", "twisted", "overload" }) do
        parts[#parts + 1] = QD.SUP.stock(fam)
    end
    local g = 0
    for _ in pairs(QD.SUP._growing or {}) do g = g + 1 end
    parts[#parts + 1] = g
    return table.concat(parts, ",")
end

-- ------------------------------------------------------------------ the raid's own kit (round 3b)
--
-- A raider brings no raid potions in: the Olm kit (test/raids/cox_olm_solo.lua:
-- overload(+) x2, Xeric's aid(+) x6, revitalisation(+) x4, prayer enhance(+) x4)
-- is made in the raid. Seeds: the demi-bosses' drops and the floors' weeds
-- (a rake from a resource room's old tools); secondaries: the scavengers (all
-- three stack); herbs and vials: a resource room. THE SLOTS: a solo raider's
-- pack holds his gear switches, so prep PARKS them in his private storage
-- (any storage unit, built or not: 25 items unbuilt) while he brews, and
-- takes them back after; potions past what the pack holds wait there for the
-- next unit (the floor's end room has one, beside the hole to Olm).
QD.SUP.OLM_KIT = { aid = 6, revit = 4, enhance = 4, overload = 2 }
QD.SUP.STORES = { "raids_storage_hotspot", "raids_storage_1", "raids_storage_2", "raids_storage_3", "raids_storage_4" }
QD.SUP.SEEDS = { "raids_seed_buchuleaf", "raids_seed_golpar", "raids_seed_noxifer" }
QD.SUP.SECONDARIES = { "raids_endarkened_juice", "raids_stinkhorn_mushroom", "raids_cicely" }
QD.SUP.TOOLS = { "rake", "spade", "dibber" }
-- What prep handles that is not gear (never parked; a final prep drops it).
QD.SUP.PREP_JUNK = { "rake", "spade", "dibber", "raids_seed_buchuleaf", "raids_seed_golpar", "raids_seed_noxifer",
    "raids_endarkened_juice", "raids_stinkhorn_mushroom", "raids_cicely", "raids_grimy_buchuleaf", "raids_grimy_golpar",
    "raids_grimy_noxifer", "raids_buchuleaf", "raids_golpar", "raids_noxifer", "raids_vial_water", "raids_vial_empty",
    "raids_fishingbait", "bones", "raids_plank" }

function QD.SUP.name_of(obj_id)
    QD.SUP._names = QD.SUP._names or {}
    local v = QD.SUP._names[obj_id]
    if v == nil then
        local r, nm = api_drive.symbol_name("obj", obj_id)
        v = (r == "ok" and nm) or ("obj" .. obj_id)
        QD.SUP._names[obj_id] = v
    end
    return v
end

-- A container's rows { slot, name, count } (the backpack by default).
function QD.SUP.rows(inv, size)
    local out = {}
    for s = 0, (size or 28) - 1 do
        local r, c = api_drive.inv_slot(inv or QD.SUP.INV, s)
        if r == "ok" and c.obj_id >= 0 then
            out[#out + 1] = { slot = s, name = QD.SUP.name_of(c.obj_id), count = c.count or 1 }
        end
    end
    return out
end

-- Any raid potion (a family's or an overload, any tier, any dose): its family.
function QD.SUP.potion_family(name)
    if string.find(name, "^raids_vial_overload") then return "overload" end
    local stem = string.match(name, "^%ddose(.+)$")
    if stem then
        for fam, st in pairs(QD.SUP.OUTSIDE) do if st == stem then return fam end end
    end
    for fam, F in pairs(QD.SUP.FAMILY) do
        for _, p4 in ipairs(F.pots) do
            if string.sub(name, 1, -2) == string.sub(p4, 1, -2) then return fam end
        end
    end
    return nil
end

function QD.SUP.is_junk(name)
    for _, j in ipairs(QD.SUP.PREP_JUNK) do if j == name then return true end end
    return false
end

-- Gear: what is neither a raid potion nor prep's own stuff.
function QD.SUP.is_gear(name)
    return QD.SUP.potion_family(name) == nil and not QD.SUP.is_junk(name)
end

function QD.SUP.store_inv()
    return QD.SUP.id("inv", "raids_privatestorage")
end

function QD.SUP.store_is_open()
    local id = QD.SUP.id("interface", "raids_storage_private")
    if id == nil then return false end
    local r, present = api_drive.group_present(id)
    return r == "ok" and present and true or false
end

-- Open my private storage at a storage unit in `room` (op2 Private: the
-- hotspot or a built unit). -> true when open
function QD.SUP.store_open(room)
    if QD.SUP.store_is_open() then return true end
    local L = QD.SUP.locs(room, QD.SUP.STORES)
    local row = nil
    for _, n in ipairs(QD.SUP.STORES) do row = row or L[n][1] end
    if row == nil then
        QD.SUP.trace("store_open: no unit in %s (%d,%d)", room.name, room.ox, room.oz)
        return false
    end
    QD.SUP.loc_op(row, 2)
    local ok = QD.SUP.wait(QD.SUP.store_is_open, 30)
    if not ok then QD.SUP.trace("store_open: unit at %d,%d did not open", row.x, row.z) end
    return ok
end

function QD.SUP.store_close()
    if QD.SUP.store_is_open() then
        api_drive.close_modal()
        QD.SUP.wait(function() return not QD.SUP.store_is_open() end, 4)
    end
end

-- What the private storage holds (as last sent), { name = n }.
function QD.SUP.stored()
    local out = {}
    local inv = QD.SUP.store_inv()
    if inv == nil then return out end
    for _, row in ipairs(QD.SUP.rows(inv, 120)) do out[row.name] = (out[row.name] or 0) + row.count end
    return out
end

-- Store every backpack copy of the first item `pick(name)` accepts (Store-All
-- on the side panel), one press a tick, until `pick` takes nothing or
-- `limit` presses. -> presses
function QD.SUP.store_put(pick, limit, one)
    local n = 0
    for _ = 1, limit or 28 do
        local target = nil
        for _, row in ipairs(QD.SUP.rows()) do
            if target == nil and pick(row.name) then target = row end
        end
        if target == nil then break end
        local before = QD.SUP.count(target.name)
        local wr, w = QD.ui.widget("raids_storage_side:items", target.slot)
        if wr ~= "ok" then break end
        -- (one: Store-1, a single item a press -- making room must not put
        -- a whole stack of brews away for one slot)
        QD.ui.invoke(w, one and 1 or 4)
        if not QD.SUP.wait(function() return QD.SUP.count(target.name) < before end, 4) then break end
        n = n + 1
    end
    return n
end

-- Withdraw `want` of `name` from the private storage (op1 one at a time, op4
-- all). -> withdrawn
function QD.SUP.store_take(name, want)
    local inv = QD.SUP.store_inv()
    local got = 0
    while got < want do
        local slot, have = nil, 0
        for _, row in ipairs(QD.SUP.rows(inv, 120)) do
            if row.name == name then
                slot = slot or row.slot
                have = have + row.count
            end
        end
        if slot == nil or QD.SUP.free() == 0 then break end
        local before = QD.SUP.count(name)
        local wr, w = QD.ui.widget("raids_storage_private:items", slot)
        if wr ~= "ok" then break end
        -- all of it when all is wanted, else one at a time
        QD.ui.invoke(w, (want - got >= have) and 4 or 1)
        if not QD.SUP.wait(function() return QD.SUP.count(name) > before end, 4) then break end
        got = got + (QD.SUP.count(name) - before)
    end
    return got
end

-- Potions to keep in the pack, most wanted first (the rest wait in storage).
-- The first share of each, in this order (a restore to about three heal sips).
-- (round 5: restores and prayer run out first, not heals -- raid1..7 drank
-- 0-16 heal doses a room and 1-6 restore doses, the restores mostly for
-- PRAYER; raid3 met Muttadiles with 27 heal doses, no restore, prayer 0)
QD.SUP.KEEP_QUOTA = { { "revit", 3 }, { "enhance", 2 }, { "superrestore", 1 }, { "aid", 4 }, { "brew", 2 } }
QD.SUP.KEEP_ORDER = { "revit", "aid", "enhance", "superrestore", "prayerpot", "brew", "overload", "elder", "kodai", "twisted" }

-- Bring the backpack to: every stored gear item back (but `skip`), then potions by
-- `order` while more than `keep_free` slots are free. Potions that do not fit
-- go INTO storage, the least wanted first. -> detail
function QD.SUP.rebalance(order, keep_free, skip, stash, quota)
    skip = skip or {}
    -- THE GEAR BACK, one item at a time: a slot over keep_free takes the next
    -- gear item; else the least wanted potion goes in (a swap: the item
    -- just taken freed its storage slot); with the store full too, one goes
    -- down on the floor (raid3: a full store kept the pickaxe, the blowpipe
    -- and the fire cape, and the Guardians found no pickaxe)
    local dropped = 0
    for _ = 1, 80 do
        local gear_name = nil
        for name in pairs(QD.SUP.stored()) do
            if gear_name == nil and QD.SUP.is_gear(name) and not skip[name] then gear_name = name end
        end
        if gear_name == nil then break end
        if QD.SUP.free() > keep_free then
            if QD.SUP.store_take(gear_name, 1) == 0 then break end
        else
            local moved = 0
            for i = #order, 1, -1 do
                if moved == 0 then
                    local fam = order[i]
                    moved = QD.SUP.store_put(function(name) return QD.SUP.potion_family(name) == fam end, 1, true)
                end
            end
            if moved == 0 then
                local victim = nil
                for i = #order, 1, -1 do
                    for _, row in ipairs(QD.SUP.rows()) do
                        if victim == nil and QD.SUP.potion_family(row.name) == order[i] then victim = row.name end
                    end
                end
                if victim == nil then break end
                local room = QD.COX.here()
                QD.SUP.store_close()
                QD.player.drop(victim)
                dropped = dropped + 1
                if room == nil or not QD.SUP.store_open(room) then break end
            end
        end
    end
    -- what waits in storage for a later prep (`stash(name)`), now the gear
    -- has left room for it
    if stash then QD.SUP.store_put(stash, 8) end
    -- first a share of each (heals with their restores: one restore to about
    -- three heal sips -- raid3 carried 11 heals and no restore into
    -- Vespula), then the rest by `order`
    for _, q in ipairs(#order > 0 and (quota or QD.SUP.KEEP_QUOTA) or {}) do
        local fam, want = q[1], q[2]
        do
            for name, n in pairs(QD.SUP.stored()) do
                local short = want - QD.SUP.stock(fam)
                if short > 0 and QD.SUP.potion_family(name) == fam and QD.SUP.free() > keep_free then
                    QD.SUP.store_take(name, math.min(n, short, QD.SUP.free() - keep_free))
                end
            end
        end
    end
    for _, fam in ipairs(order) do
        for name, n in pairs(QD.SUP.stored()) do
            if QD.SUP.potion_family(name) == fam and QD.SUP.free() > keep_free then
                QD.SUP.store_take(name, math.min(n, QD.SUP.free() - keep_free))
            end
        end
    end
    local parts = {}
    for name, n in pairs(QD.SUP.stored()) do parts[#parts + 1] = string.gsub(name, "raids_vial_", "") .. "=" .. n end
    table.sort(parts)
    return "stored [" .. table.concat(parts, " ") .. "] free " .. QD.SUP.free() .. (dropped > 0 and (" dropped " .. dropped) or "")
end

-- At least `n` free slots, made at a storage unit in `room` by storing the
-- least wanted potions (overloads first: Olm's, collected at the floor's end).
-- -> free slots after
function QD.SUP.make_slots(room, n, order)
    if QD.SUP.free() >= n then return QD.SUP.free() end
    if room == nil or not QD.SUP.store_open(room) then return QD.SUP.free() end
    order = order or QD.SUP.KEEP_ORDER
    for i = #order, 1, -1 do
        if QD.SUP.free() >= n then break end
        local fam = order[i]
        QD.SUP.store_put(function(name) return QD.SUP.potion_family(name) == fam end, n - QD.SUP.free(), true)
    end
    QD.SUP._stored_cache = QD.SUP.stored()
    QD.SUP.store_close()
    QD.SUP.trace("make_slots %d -> %d free", n, QD.SUP.free())
    return QD.SUP.free()
end

-- The kit counts as the relay reports them: carried + stored, by family.
function QD.SUP.kit_text()
    local stored = QD.SUP.stored()
    local out = {}
    for _, fam in ipairs({ "overload", "aid", "revit", "enhance", "elder", "kodai", "twisted", "brew", "superrestore", "prayerpot" }) do
        local s = 0
        for name, n in pairs(stored) do if QD.SUP.potion_family(name) == fam then s = s + n end end
        local c = QD.SUP.stock(fam)
        if c + s > 0 then out[#out + 1] = fam .. "=" .. c .. (s > 0 and ("+" .. s .. "s") or "") end
    end
    out[#out + 1] = QD.SUP.summary({ { "juice", "raids_endarkened_juice" }, { "stink", "raids_stinkhorn_mushroom" },
        { "cicely", "raids_cicely" }, { "buchu_s", "raids_seed_buchuleaf" }, { "golpar_s", "raids_seed_golpar" },
        { "nox_s", "raids_seed_noxifer" }, { "rake", "rake" } })
    out[#out + 1] = "free=" .. QD.SUP.free()
    local d = QD.SUP.dose_groups()
    out[#out + 1] = string.format("doses heal=%d restore=%d prayer=%d overload=%d", d.heal, d.restore, d.prayer, d.overload)
    QD.SUP._stat_ids = QD.SUP._stat_ids or QD.raid._tob_common_ids("cox_supplies", {})
    local parts = {}
    for _, st in ipairs({ "hitpoints", "prayer", "ranged", "attack", "strength", "magic" }) do
        local id = QD.SUP._stat_ids[st]
        local r, k = nil, nil
        if id then r, k = api_drive.skill(id) end
        if r == "ok" and k then parts[#parts + 1] = st .. "=" .. tostring(k.level) .. "/" .. tostring(k.base_level or k.base or "?") end
    end
    out[#out + 1] = table.concat(parts, " ")
    return table.concat(out, " ")
end

-- Doses CARRIED by need: heal (aid, brew), restore (revitalisation, super
-- restore), prayer (enhance, prayer potion), overload.
QD.SUP.DOSE_GROUP = { aid = "heal", brew = "heal", revit = "restore", superrestore = "restore",
    enhance = "prayer", prayerpot = "prayer", overload = "overload" }
function QD.SUP.dose_groups()
    local out = { heal = 0, restore = 0, prayer = 0, overload = 0 }
    for _, row in ipairs(QD.SUP.rows()) do
        local g = QD.SUP.DOSE_GROUP[QD.SUP.potion_family(row.name) or ""]
        if g then
            local dose = tonumber(string.match(row.name, "_(%d)$") or string.match(row.name, "^(%d)dose") or "0") or 0
            out[g] = out[g] + dose * (row.count or 1)
        end
    end
    return out
end

-- The plan net of the potions carried AND stored.
function QD.SUP.todo_all(make)
    local stored = QD.SUP._stored_cache or {}
    local net = {}
    for fam, n in pairs(make) do
        local s = 0
        for name, c in pairs(stored) do if QD.SUP.potion_family(name) == fam then s = s + c end end
        net[fam] = math.max(0, n - s)
    end
    return net
end

-- The seeds a plan still needs (net of herbs it no longer needs): buchu two
-- (both patches, Beleti), golpar two (a spare), noxifer one.
function QD.SUP.seeds_wanted(make)
    local need = QD.SUP.herbs_needed(QD.SUP.todo(QD.SUP.todo_all(make)))
    local want = {}
    if (need.raids_buchuleaf or 0) > 0 then want.raids_seed_buchuleaf = 2 end
    if (need.raids_golpar or 0) > 0 then want.raids_seed_golpar = 2 end
    if (need.raids_noxifer or 0) > 0 then want.raids_seed_noxifer = 1 end
    return want
end

-- After a room: what its boss or minions left that the raid's kit uses --
-- seeds and secondaries (stacks), raid potions while more than `keep_free`
-- slots are free. -> detail
function QD.SUP.loot(opts)
    opts = opts or {}
    local room = QD.COX.here()
    if room == nil then return "not_found", "not in a raid room" end
    QD.SUP._banned = {}
    local keep_free = opts.keep_free or 1
    local names = {}
    for _, n in ipairs(QD.SUP.SEEDS) do names[n] = true end
    for _, n in ipairs(QD.SUP.SECONDARIES) do names[n] = true end
    for fam, F in pairs(QD.SUP.FAMILY) do for _, p in ipairs(F.pots) do names[p] = true end end
    for _, p in ipairs(QD.SUP.OVERLOADS) do names[p] = true end
    local took = {}
    local deadline = api_drive.tick() + (opts.max_ticks or 120)
    -- (most wanted first, so a pack's last slots go to it: a buchu seed makes
    -- aid, revitalisation and prayer enhance; raid3 filled its one slot with
    -- noxifer and the floor's prep had no buchu to plant)
    local PRIORITY = { "raids_seed_buchuleaf", "raids_stinkhorn_mushroom", "raids_cicely", "raids_endarkened_juice",
        "raids_seed_golpar", "raids_seed_noxifer" }
    while api_drive.tick() < deadline do
        local drop, dname = nil, nil
        for _, want in ipairs(PRIORITY) do
            if drop == nil and names[want] then drop, dname = QD.SUP.ground(room, { [want] = true }) end
        end
        if drop == nil then drop, dname = QD.SUP.ground(room, names) end
        if drop == nil then break end
        local potion = QD.SUP.potion_family(dname) ~= nil
        local stack = QD.SUP.count(dname) > 0 and not potion
        -- (a seed or secondary may take the slot kept free: it is the kit's)
        if not stack and QD.SUP.free() <= (potion and keep_free or 0) then
            names[dname] = nil
        else
            if QD.SUP.take(drop, dname) == "ok" then took[#took + 1] = string.gsub(dname, "raids_", "") end
        end
    end
    return "ok", (#took > 0 and table.concat(took, ",") or "nothing") .. "; " .. QD.SUP.kit_text()
end

-- A resource room's prep with the slots planned: park the gear switches in
-- private storage, run the resource solver on opts.make (net of what storage
-- holds), then tidy -- opts.final (the raid's last resource room) drops the
-- tools, seeds, secondaries and spare herbs; otherwise the spade, dibber and
-- spare vials and herbs go and the rake, seeds and secondaries stay -- and
-- take the gear back, potions past the pack waiting in storage.
function QD.SUP.prep(opts)
    opts = opts or {}
    local room = QD.SUP.enter({ "resource", "resource_b" }, opts)
    if room == nil then return "not_found", "not in a resource room" end
    local start = api_drive.tick()
    local parked = 0
    -- the tools the relay carries on need a slot each
    local tools_missing = 0
    for _, tool in ipairs(opts.take_tools or {}) do
        if QD.SUP.count(tool) == 0 then tools_missing = tools_missing + 1 end
    end
    if tools_missing > 0 then QD.SUP.make_slots(room, tools_missing) end
    -- the seeds and secondaries an earlier prep stored, back for this one
    local stacks = {}
    for _, n in ipairs(QD.SUP.SEEDS) do stacks[n] = true end
    for _, n in ipairs(QD.SUP.SECONDARIES) do stacks[n] = true end
    local stored0 = QD.SUP._stored_cache or {}
    local any = false
    for n in pairs(stacks) do if (stored0[n] or 0) > 0 then any = true end end
    if any and QD.SUP.store_open(room) then
        for name, c in pairs(QD.SUP.stored()) do
            if stacks[name] and QD.SUP.free() > 0 then QD.SUP.store_take(name, c) end
        end
        QD.SUP._stored_cache = QD.SUP.stored()
        QD.SUP.store_close()
    end
    -- park only when this room can make something (a seed, a herb or a
    -- patch growing, and its secondary): else the pack is left as it is
    QD.SUP._growing = {}
    local can = next(QD.SUP.doable(QD.SUP.todo(QD.SUP.todo_all(opts.make or QD.SUP.OLM_KIT)), room)) ~= nil
    if can and QD.SUP.store_open(room) then
        -- STORE ROOM FOR THE GEAR (25 items unbuilt): Saradomin brews an
        -- earlier room left in it come out and go down on the floor, as many
        -- as the gear lacks (heals are the raid's surplus, round 5)
        local gear = 0
        for _, r in ipairs(QD.SUP.rows()) do if QD.SUP.is_gear(r.name) then gear = gear + 1 end end
        for _ = 1, 4 do
            local short = gear - (25 - #QD.SUP.rows(QD.SUP.store_inv(), 120))
            if short <= 0 then break end
            local took = {}
            for name, c in pairs(QD.SUP.stored()) do
                if QD.SUP.potion_family(name) == "brew" and short > 0 and QD.SUP.free() > 0 then
                    local g = QD.SUP.store_take(name, math.min(c, short, QD.SUP.free()))
                    if g > 0 then
                        short = short - g
                        took[#took + 1] = name
                    end
                end
            end
            if #took == 0 then break end
            QD.SUP.store_close()
            for _, name in ipairs(took) do
                for _ = 1, 28 do
                    if QD.SUP.count(name) == 0 then break end
                    QD.player.drop(name)
                end
            end
            if not QD.SUP.store_open(room) then break end
        end
        QD.SUP._stored_cache = QD.SUP.stored()
        -- (the gear switches, and what was brought in that the raid is short
        -- of -- super restores and prayer potions; a brew stays in the pack,
        -- where ROOM TO BREW below may put it down)
        parked = QD.SUP.store_put(function(name)
            local fam = QD.SUP.potion_family(name)
            return QD.SUP.is_gear(name) or fam == "superrestore" or fam == "prayerpot"
        end, 28)
        QD.SUP._stored_cache = QD.SUP.stored()
        QD.SUP.store_close()
    end
    -- ROOM TO BREW: the store holds 25 items, and what earlier rooms left in
    -- it can stop the parking short (raid3's floor-2 prep parked 6, stalled
    -- with the pack full and brewed nothing; Olm then had 1 revitalisation).
    -- Heals are what the raid has too many of (round 5: 0-16 heal doses a
    -- room against restores and prayer run dry), so Saradomin brews go down
    -- on the floor, the lowest dose first, until opts.min_free (12) are free.
    if can then
        for _ = 1, 12 do
            if QD.SUP.free() >= (opts.min_free or 12) then break end
            local victim = nil
            for d = 1, 4 do
                if victim == nil and QD.SUP.count(d .. "dosepotionofsaradomin") > 0 then victim = d .. "dosepotionofsaradomin" end
            end
            if victim == nil then break end
            QD.player.drop(victim)
        end
    end
    local sub = {}
    for k, v in pairs(opts) do sub[k] = v end
    sub.make = QD.SUP.todo_all(opts.make or QD.SUP.OLM_KIT)
    sub.bank = parked > 0
    local rr, rd = QD.SUP.resource(sub)
    -- tidy
    local drop = {}
    if opts.final then
        for _, j in ipairs(QD.SUP.PREP_JUNK) do drop[#drop + 1] = j end
    else
        for _, j in ipairs({ "spade", "dibber", "raids_vial_water", "raids_vial_empty", "raids_grimy_buchuleaf",
            "raids_grimy_golpar", "raids_grimy_noxifer", "raids_buchuleaf", "raids_golpar", "raids_noxifer", "bones" }) do
            drop[#drop + 1] = j
        end
    end
    local dropped = 0
    for _, j in ipairs(drop) do
        local guard = 0
        while QD.SUP.count(j) > 0 and guard < 28 do
            QD.player.drop(j)
            dropped = dropped + 1
            guard = guard + 1
        end
    end
    local back = "nothing parked"
    -- (not the last prep: the seeds and secondaries wait in storage for the
    -- next one -- six slots the floor's fights need for potions; raid3 kept
    -- 11 heals in storage and died in Vespula with 6)
    local keep_stacks = not opts.final and (QD.SUP.count("raids_seed_buchuleaf") + QD.SUP.count("raids_seed_golpar")
        + QD.SUP.count("raids_seed_noxifer") + QD.SUP.count("raids_endarkened_juice") + QD.SUP.count("raids_stinkhorn_mushroom")
        + QD.SUP.count("raids_cicely")) > 0
    if (parked > 0 or keep_stacks) and QD.SUP.store_open(room) then
        back = QD.SUP.rebalance(opts.keep_order or QD.SUP.KEEP_ORDER, opts.keep_free or 1, nil,
            keep_stacks and function(name) return stacks[name] == true end or nil)
        QD.SUP._stored_cache = QD.SUP.stored()
        QD.SUP.store_close()
    end
    local detail = string.format("parked %d dropped %d ticks %d; %s; %s; %s", parked, dropped, api_drive.tick() - start,
        tostring(rd), back, QD.SUP.kit_text())
    if rr == "ok" then return "ok", detail end
    return rr, detail
end

-- The last floor's end room, before the hole to Olm: what Olm does not use
-- into storage (opts.leave: names), then the stored potions back, the Olm
-- kit's first. -> "ok", detail
function QD.SUP.olm_pack(opts)
    opts = opts or {}
    local room = QD.COX.here()
    if room == nil then return "not_found", "not in a raid room" end
    if not QD.SUP.store_open(room) then return "ok", "no storage unit here; " .. QD.SUP.kit_text() end
    local leave = {}
    for _, n in ipairs(opts.leave or {}) do leave[n] = true end
    for _, j in ipairs(QD.SUP.PREP_JUNK) do leave[j] = true end
    QD.SUP.store_put(function(name) return leave[name] == true end, 28)
    -- every potion in, the gear back, then the Olm kit's quota of each family
    -- (strong, fullest first), then the rest by `order` while a slot is free
    QD.SUP.store_put(function(name) return QD.SUP.potion_family(name) ~= nil end, 28)
    QD.SUP.rebalance({}, 0, leave)
    local quota = opts.kit or QD.SUP.OLM_KIT
    local order = opts.keep_order or { "overload", "enhance", "aid", "revit" }
    local function rank(name)
        local tier = string.find(name, "_strong_") and 0 or (string.find(name, "_weak_") and 2 or 1)
        return tier * 10 + (4 - (tonumber(string.sub(name, -1)) or 0))
    end
    local function take_family(fam, n)
        local names = {}
        for name in pairs(QD.SUP.stored()) do
            if QD.SUP.potion_family(name) == fam then names[#names + 1] = name end
        end
        table.sort(names, function(x, y) return rank(x) < rank(y) end)
        for _, name in ipairs(names) do
            local have = QD.SUP.stock(fam)
            if have >= n or QD.SUP.free() <= (opts.keep_free or 0) then break end
            QD.SUP.store_take(name, math.min(n - have, QD.SUP.stored()[name] or 0, QD.SUP.free() - (opts.keep_free or 0)))
        end
    end
    for _, fam in ipairs(order) do take_family(fam, quota[fam] or 0) end
    -- the slots past the kit: Xeric's aid, Olm's only healing (raids 4/1
    -- ran out of aid first, with prayer and overloads left over)
    for _, fam in ipairs(opts.fill_order or { "aid", "brew", "enhance", "revit", "superrestore", "prayerpot" }) do take_family(fam, 99) end
    local parts = {}
    for name, n in pairs(QD.SUP.stored()) do parts[#parts + 1] = string.gsub(name, "raids_vial_", "") .. "=" .. n end
    table.sort(parts)
    QD.SUP._stored_cache = QD.SUP.stored()
    QD.SUP.store_close()
    return "ok", "stored [" .. table.concat(parts, " ") .. "] free " .. QD.SUP.free() .. "; " .. QD.SUP.kit_text()
end

-- ------------------------------------------------------------------ weeds

-- A floor room's weeds (template locs): a rake (the resource room's old tools),
-- rake until every seed in opts.want (seed name -> n) is carried.
function QD.SUP.weeds(opts)
    opts = opts or {}
    -- opts.plan: the seeds that plan still needs (QD.SUP.seeds_wanted)
    local want = opts.want or (opts.plan and QD.SUP.seeds_wanted(opts.plan)) or { raids_seed_buchuleaf = 1 }
    if next(want) == nil then return "ok", "no seed needed" end
    local room = QD.COX.here()
    if room == nil then return "not_found", "not in a raid room" end
    local row = QD.SUP.first(room, "raids_weeds")
    if row == nil then return "not_found", "no weeds in " .. room.name end
    if QD.SUP.count("rake") == 0 then return "no_tool", "no rake" end
    local function done()
        for name, n in pairs(want) do if QD.SUP.have(name) < n then return false end end
        return true
    end
    if done() then return "ok", "seeds carried or stored" end
    -- a new seed needs a slot of its own (a floor's end room has a storage unit)
    local stacks = 0
    -- (the rake rotates through all three seeds: each one not carried needs
    -- its slot, wanted or not, or the raking stops)
    for _, name in ipairs(QD.SUP.SEEDS) do if QD.SUP.count(name) == 0 then stacks = stacks + 1 end end
    local wl = {}
    for name, n in pairs(want) do wl[#wl + 1] = name .. ">=" .. n end
    QD.SUP.trace("weeds in %s: want [%s], %d new stacks, %d free", room.name, table.concat(wl, " "), stacks, QD.SUP.free())
    if QD.SUP.free() < stacks then QD.SUP.make_slots(room, stacks) end
    QD.SUP.loc_op(row, 1)
    local ok = QD.SUP.wait(done, opts.max_ticks or 150)
    -- stop raking: a walk order to where I stand
    local me = QD.SUP.me()
    api_drive.move_to(me.x, me.z)
    QD.ticks(1)
    return ok and "ok" or "timeout", QD.SUP.summary({ { "buchu", "raids_seed_buchuleaf" }, { "golpar", "raids_seed_golpar" },
        { "noxifer", "raids_seed_noxifer" } })
end

QD.raid.cox_scavengers_solve = function(opts) return QD.SUP.scavengers(opts) end
QD.raid.cox_resource_solve = function(opts) return QD.SUP.resource(opts) end
QD.raid.cox_weeds_solve = function(opts) return QD.SUP.weeds(opts) end
QD.raid.cox_prep_solve = function(opts) return QD.SUP.prep(opts) end
QD.raid.cox_supplies_loot = function(opts) return QD.SUP.loot(opts) end
QD.raid.cox_olm_pack = function(opts) return QD.SUP.olm_pack(opts) end
QD.raid.cox_supplies_text = function() return QD.SUP.kit_text() end
QD.raid.cox_seeds_wanted = function(make) return QD.SUP.seeds_wanted(make) end
-- The run's player name (a relay's variant switch: cox_raid_solo_prep's "raida").
QD.raid.cox_run_name = function()
    local pr, players = api_drive.players()
    if pr ~= "ok" then return "" end
    for _, p in ipairs(players) do if p.me then return tostring(p.name) end end
    return ""
end

-- Probe readout: the npcs within 32 as "name@x,z(level) hp ratio/scale".
QD.raid.cox_supplies_npcs = function()
    local r, rows = api_drive.npcs(32)
    if r ~= "ok" then return tostring(r) end
    local out = {}
    for _, row in ipairs(rows) do
        local _, nm = api_drive.symbol_name("npc", row.npc_id)
        out[#out + 1] = string.format("%s@%s,%s(%s) %s/%s", tostring(nm), tostring(row.server_x or row.x),
            tostring(row.server_z or row.z), tostring(row.level), tostring(row.health_ratio), tostring(row.health_scale))
    end
    return table.concat(out, " ")
end
