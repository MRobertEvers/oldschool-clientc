-- raid_agent / world: MEASURE.  The fight as a table, folded from the
-- server's ticklog rows (raid seam55 bot_runner, 2026-10-07).
--
-- One input for every runner: the bot runner streams the rows straight from
-- the server (src/torirsserver/torirs_server_botrun.c), the quest driver
-- reads the same rows from the embedded server, and a recorded ticklog.tsv
-- replays them.  So what the agent knows does not depend on where it runs.
--
-- Pure: no QD, no io.  `names` resolves ids to the content's names
-- (configs/all.*.compack); the runner hands it in.

local World = {}
World.__index = World

local CYCLES_PER_TICK = 30

local function unpack_coord(c)
    c = tonumber(c)
    return (c >> 14) & 0x3fff, c & 0x3fff, (c >> 28) & 0x3
end
World.unpack_coord = unpack_coord

function World.new(names)
    assert(names, "World.new: names")
    return setmetatable({
        names = names,
        tick = 0,
        players = {},     -- pid -> { x, z, hp, hpmax, prayer, prmax, prayers, weapon, style, spec, target, energy, run, inv, worn, anim }
        npcs = {},        -- slot -> { slot, type, name, x, z, size, alive, hp, hpmax, anim, anim_tick, spawn_tick }
        projectiles = {}, -- live: { spotanim, name, sx, sz, dx, dz, target, launch, land }
        spotanims = {},   -- map spotanims this tick and earlier: { spotanim, name, x, z, tick }
        hits = {},        -- this tick: { pid, npc_slot, damage, npc_type }
        npc_hits = {},    -- this tick: { slot, type, damage }
        locs = {},        -- coord -> { loc, name, shape, angle, kind, tick }
        ground = {},      -- dropped objs: { obj, name, x, z, count, tick } (obj_add; gone when held)
        events = {},      -- this tick: npc_anim / npc_spawn / npc_retype / npc_death rows, for the policy
        messages = {},    -- this tick: { pid, text }
    }, World)
end

local function name_of(self, space, id)
    local t = self.names[space]
    return t and t[id] or tostring(id)
end

-- Ticks start with `tick <n>`: the per-tick lists reset, the standing state stays.
function World:begin_tick(n)
    self.tick = n
    self.hits = {}
    self.npc_hits = {}
    self.events = {}
    self.messages = {}
    local live = {}
    for _, p in ipairs(self.projectiles) do
        if p.land >= n then live[#live + 1] = p end
    end
    self.projectiles = live
    local spots = {}
    for _, s in ipairs(self.spotanims) do
        if n - s.tick <= 30 then spots[#spots + 1] = s end
    end
    self.spotanims = spots
end

function World:player(pid)
    local p = self.players[pid]
    if p == nil then
        p = { pid = pid, inv = {}, worn = {} }
        self.players[pid] = p
    end
    return p
end

function World:npc(slot)
    local n = self.npcs[slot]
    if n == nil then
        n = { slot = slot, alive = false }
        self.npcs[slot] = n
    end
    return n
end

-- One `row` line's fields after the word `row`: serial tick kind a b c d e f label g.
function World:row(kind, sa, sb, sc, sd, se, sf, label, sg)
    local a, b, c, d, e, f, g = tonumber(sa), tonumber(sb), tonumber(sc), tonumber(sd), tonumber(se), tonumber(sf), tonumber(sg)
    if kind == "player_tile" then
        local p = self:player(a)
        p.x, p.z, p.level = b, c, d
    elseif kind == "raider" then
        local p = self:player(a)
        p.hp, p.prayer, p.prayers, p.weapon, p.style, p.spec = b, c, d, e, f, g
        p.hpmax = tonumber(label:match("hpmax (%-?%d+)"))
        p.prmax = tonumber(label:match("prmax (%-?%d+)"))
        p.target = tonumber(label:match("tgt (%-?%d+)"))
        p.energy = tonumber(label:match("energy (%-?%d+)"))
        p.run = tonumber(label:match("run (%-?%d+)"))
        p.dead = p.hp ~= nil and p.hp <= 0
    elseif kind == "npc_spawn" then
        local n = self:npc(a)
        n.type, n.name = b, name_of(self, "npc", b)
        n.x, n.z = unpack_coord(c)
        n.alive, n.spawn_tick, n.anim = true, self.tick, nil
        n.hp, n.hpmax = nil, nil
        self.events[#self.events + 1] = { kind = kind, npc = n }
    elseif kind == "npc_tile" then
        local n = self:npc(a)
        n.x, n.z, n.level, n.size = b, c, d, f
        if e ~= n.type then n.type, n.name = e, name_of(self, "npc", e) end
        n.alive = true
    elseif kind == "npc_retype" then
        local n = self:npc(a)
        n.type, n.name = c, name_of(self, "npc", c)
        self.events[#self.events + 1] = { kind = kind, npc = n, from = name_of(self, "npc", b) }
    elseif kind == "npc_anim" then
        local n = self:npc(a)
        n.anim, n.anim_name, n.anim_tick = c, name_of(self, "seq", c), self.tick
        self.events[#self.events + 1] = { kind = kind, npc = n, anim = c }
    elseif kind == "npc_death" or kind == "npc_free" then
        local n = self:npc(a)
        if kind == "npc_free" then n.alive = false end
        n.dying = true
        self.events[#self.events + 1] = { kind = kind, npc = n }
    elseif kind == "hit_npc" then
        self.npc_hits[#self.npc_hits + 1] = { slot = a, type = b, damage = c }
    elseif kind == "hudbar" then
        local n = self:npc(a)
        n.hp, n.hpmax = f, g
    elseif kind == "projectile" then
        local sx, sz = unpack_coord(a)
        local dx, dz = unpack_coord(b)
        local p = { spotanim = d, name = name_of(self, "spotanim", d), sx = sx, sz = sz, dx = dx, dz = dz,
            target = c, launch = self.tick, land = self.tick + (f // CYCLES_PER_TICK) }
        -- target: a player is -(pid + 1) or pid + 32768; an npc is slot + 1
        if c < 0 then p.target_pid = -c - 1 elseif c >= 32768 then p.target_pid = c - 32768
        elseif c > 0 then p.target_npc = c - 1 end
        self.projectiles[#self.projectiles + 1] = p
    elseif kind == "map_spotanim" then
        local x, z = unpack_coord(a)
        self.spotanims[#self.spotanims + 1] = { spotanim = b, name = name_of(self, "spotanim", b), x = x, z = z, tick = self.tick }
    elseif kind == "hit_player" then
        self.hits[#self.hits + 1] = { pid = a, npc_slot = b, damage = c, npc_type = f }
    elseif kind == "obj_add" then
        local x, z = unpack_coord(a)
        self.ground[#self.ground + 1] = { obj = b, name = name_of(self, "obj", b), x = x, z = z, count = c, tick = self.tick }
    elseif kind == "loc_set" then
        self.locs[a] = { loc = b, name = name_of(self, "loc", b), shape = c, angle = d, kind = e, tick = self.tick }
    end
end

-- A game message to a bot.  Content's death line is the death signal: the
-- raider row need not read 0 (the slam of t164 left 1, the death came at 171).
function World:message(pid, text)
    pid = tonumber(pid)
    self.messages[#self.messages + 1] = { pid = pid, text = text }
    local p = self:player(pid)
    -- "You have died. Death count: N." is the Theatre's (a raider is caged,
    -- not respawned); "Oh dear, you are dead!" the world's.
    if text:find("You have died. Death count", 1, true) or text:find("Oh dear, you are dead", 1, true) then
        p.died_tick = self.tick
        p.deaths = (p.deaths or 0) + 1
    end
end

-- The runner's `self` line: inventory and gear, which no row carries.
function World:self_line(pid, x, z, level, inv, worn, mainmodal, chatmodal)
    local p = self:player(tonumber(pid))
    p.x, p.z, p.level = tonumber(x), tonumber(z), tonumber(level)
    p.mainmodal, p.chatmodal = tonumber(mainmodal) or -1, tonumber(chatmodal) or -1
    p.inv, p.worn = {}, {}
    for s, o, count in (inv or ""):gmatch("(%d+):(%d+):(%d+),") do
        local obj = tonumber(o)
        p.inv[tonumber(s)] = { obj = obj, name = name_of(self, "obj", obj), count = tonumber(count) }
    end
    for s, o in (worn or ""):gmatch("(%d+):(%d+),") do
        local obj = tonumber(o)
        p.worn[tonumber(s)] = { obj = obj, name = name_of(self, "obj", obj) }
    end
    p.mine = true
end

-- Is an obj held by anybody (an inventory or a weapon this runner can see)?
function World:held(obj)
    for _, p in pairs(self.players) do
        if p.weapon == obj then return p end
        for _, it in pairs(p.inv or {}) do if it.obj == obj then return p end end
    end
    return nil
end

-- The ground obj named `name` nobody holds, newest first, or nil.
function World:on_ground(name)
    for i = #self.ground, 1, -1 do
        local g = self.ground[i]
        if g.name == name then
            if self:held(g.obj) == nil then return g end
            table.remove(self.ground, i)
        end
    end
    return nil
end

-- The live npcs whose name is in `set` (a name -> true table), nearest first to (x, z).
function World:find(set, x, z)
    local out = {}
    for _, n in pairs(self.npcs) do
        if n.alive and set[n.name] then out[#out + 1] = n end
    end
    if x ~= nil then
        table.sort(out, function(p, q)
            local dp = math.max(math.abs(p.x - x), math.abs(p.z - z))
            local dq = math.max(math.abs(q.x - x), math.abs(q.z - z))
            if dp ~= dq then return dp < dq end
            return p.slot < q.slot
        end)
    end
    return out
end

-- The first inventory slot holding an obj named `name`, or nil.
function World.inv_slot(p, name)
    for slot = 0, 27 do
        local it = p.inv[slot]
        if it ~= nil and it.name == name then return slot, it end
    end
    return nil
end

return World
