--
-- Olm Cycle
--
-- The Great Olm's attack cycle as four boxes -- Basic 1, Empty, Basic 2 and
-- the Special -- the next step highlighted with the ticks until it, and the
-- special box showing the special that step will be: crystal burst,
-- lightning, portal swap, and in the glowing phase the healing hand.
-- The 4:1 trainer's chat (OSRS-Content cox_olm_trainer.rs2, ::olmtrainer)
-- names each step after it happens; this shows the next one coming.
--
-- Everything is read from what the client is sent, as a RuneLite plugin
-- would: the LOC_ANIMs on the head and the left claw (api.world.loc_anim_next).
-- The cycle is the one the content runs (cox_olm.rs2 ~cox_olm_act), checked
-- against the recordings (docs/rsprox-analysis/cox/olm.md):
--
--   * the head acts every 4 ticks, from 6 ticks after the rise animation in
--     the first phase and 8 in the later ones (^cox_olm_first_act_p1/_later);
--   * the steps alternate: an auto, then an "even" step that walks a list of
--     its own -- null, crystal burst, null, lightning, null, portal swap, and
--     in the glowing phase null, healing hand -- so the autos before a null
--     are Basic 1 and those before a special Basic 2;
--   * a CLENCHED even step (the claw clenched, or the mage hand's death
--     unclenching it) is a null that leaves the list where it is;
--   * a skipped special (the head turned instead) is lost: the list moves on.
--
-- The tells keep it honest when it starts mid-fight or misses a beat: any
-- head action (a turn or an attack) is on a step, so it pins the 4-tick grid,
-- and a claw sign names its special, so it pins the list itself.
--

---@type torirs.Plugin
local plugin = {
    id      = "olm-cycle",
    title   = "Olm Cycle",
    version = "1.0.0",
    config  = {
        { key = "box_size", type = "int", default = "40", min = 24, max = 128, label = "Box size" },
        {
            key = "corner",
            type = "enum",
            choices = "top_left|top_right|bottom_left|bottom_right",
            default = "top_left",
            label = "Position"
        },
        { key = "offset_x", type = "int", default = "8", min = 0, max = 4096, label = "X offset" },
        { key = "offset_y", type = "int", default = "8", min = 0, max = 4096, label = "Y offset" },
        {
            key = "highlight",
            type = "enum",
            choices = "next|last",
            default = "next",
            label = "Highlight"
        },
        { key = "highlight_color", type = "color", default = "#FFD700", label = "Highlight colour" },
        { key = "show_countdown", type = "bool", default = "1", label = "Ticks to the next step" },
        -- (each step it reads, to the log: checked against the trainer's chat)
        { key = "log_steps", type = "bool", default = "0", label = "Log each step" },
    },
}

-- ------------------------------------------------------------ the cache's ids
-- (OSRS-Content configs/all.seq, rev 241)
local HEAD_SPAWN, HEAD_SPAWN_GLOW = 7335, 7383
local HEAD_DEATH = 7348
-- turns and attacks, plain and glowing: an action, so a step
local HEAD_ACTION = {}
for _, id in ipairs({ 7339, 7340, 7341, 7342, 7343, 7344, 7345, 7346, 7347,
    7371, 7372, 7373, 7377, 7378, 7379, 7380, 7381, 7382 }) do HEAD_ACTION[id] = true end
local HEAD_ANY = { [HEAD_SPAWN] = true, [HEAD_SPAWN_GLOW] = true, [HEAD_DEATH] = true }
for id = 7334, 7348 do HEAD_ANY[id] = true end
for id = 7371, 7383 do HEAD_ANY[id] = true end
-- the left claw's signs, plain and gripping (the mage hand down): the special
local SIGN = {
    [7356] = "burst", [7366] = "burst",
    [7358] = "lightning", [7368] = "lightning",
    [7359] = "portal", [7369] = "portal",
    [7357] = "heal", [7367] = "heal",
}
local CLAW_STUNNED, CLAW_UNSTUNNED = 7360, 7362
-- a clench's end that is not the unclench: the claw goes to its grip (the mage
-- hand died during it: gripping_spawn1/2, transition_from_grip)
local CLAW_GRIP = { [7363] = true, [7364] = true, [7469] = true }
-- the clench runs out after this long if nothing ends it first (^cox_olm_clench_ticks)
local CLENCH_TICKS = 45

local FIRST_ACT_P1, FIRST_ACT_LATER, STEP = 6, 8, 4
-- the even list: null at the even indices, the specials at the odd ones
local SPECIALS = { "burst", "lightning", "portal", "heal" }
local SPECIAL_ITEM = { burst = 1, lightning = 3, portal = 5, heal = 7 }
local NAMES = { burst = "Crystal", lightning = "Lightning", portal = "Portal", heal = "Heal" }

-- ------------------------------------------------------------------- state
local tick = 0
local last_cycle = -1
local seen_at = -1000        -- the tick a head animation was last seen
local later_phase = false    -- a head death seen: the next rise is a later phase's
local glowing = false
local grid = nil             -- a tick the head acts on (nil: not known yet)
local parity = 0             -- 0: the next step is an auto
local list = 0               -- the even list's next item
local clenched = false
local clench_until = 0
local last_slot = nil        -- the step that just happened: 1..4
local last_special = nil     -- the special the last special step named
local synced = false         -- the list's place known (a rise or a sign seen)
local since_rise = nil       -- steps taken on the rise's prediction, unconfirmed

local function list_len() return glowing and 8 or 6 end

local function reset_cycle(api)
    grid, parity, list, clenched, last_slot, last_special = nil, 0, 0, false, nil, nil
    synced, since_rise = false, nil
end

-- The slot the NEXT step is in: 1 Basic 1, 2 Empty, 3 Basic 2, 4 the Special.
local function next_slot()
    if parity == 0 then
        return (list % 2 == 0) and 1 or 3
    end
    return (list % 2 == 0) and 2 or 4
end

-- The special the next special step will be (the list's next odd item).
local function next_special()
    local i = list
    if i % 2 == 0 then i = i + 1 end
    i = i % list_len()
    return SPECIALS[(i - 1) // 2 + 1]
end

-- One step of the head, at a step tick: what it was, then the list moves.
local function step(clench_here)
    last_slot = next_slot()
    if parity == 1 then
        if not (clenched or clench_here) then
            if last_slot == 4 then last_special = next_special() end
            list = (list + 1) % list_len()
        end
    end
    parity = 1 - parity
end

local function on_rise(seq)
    glowing = seq == HEAD_SPAWN_GLOW
    reset_cycle()
    grid = tick + (later_phase and FIRST_ACT_LATER or FIRST_ACT_P1)
    synced, since_rise = true, 0
end

-- A claw sign names the step it is on: a special step, its item the list's.
local function on_sign(kind)
    if kind == "heal" then glowing = true end
    list = (SPECIAL_ITEM[kind] + 1) % list_len()
    parity = 0
    last_slot, last_special = 4, kind
    grid = tick
    synced, since_rise = true, nil
end


local function is_step_tick()
    return grid ~= nil and tick >= grid and (tick - grid) % STEP == 0
end

function plugin.on_start(api)
    tick, last_cycle, seen_at, later_phase, glowing = 0, -1, -1000, false, false
    reset_cycle(api)
end

function plugin.on_server_tick(api)
    tick = tick + 1
    -- this tick's new LOC_ANIMs (the ring is the last ~40 ticks; cycles grow)
    local head_action, rise, sign, stunned, unstunned, death = false, nil, nil, false, false, false
    local grip = false
    local newest = last_cycle
    local quiet_since = seen_at
    local cursor = -1
    while true do
        local nxt, row = api.world.loc_anim_next(cursor)
        if not nxt then break end
        cursor = nxt
        if row.cycle > last_cycle then
            local s = row.seq_id
            if HEAD_ANY[s] then seen_at = tick end
            if s == HEAD_SPAWN or s == HEAD_SPAWN_GLOW then rise = s end
            if s == HEAD_DEATH then death = true end
            if HEAD_ACTION[s] then head_action = true end
            if s >= 7371 and s <= 7382 then glowing = true end
            if SIGN[s] then sign = SIGN[s] end
            if s == CLAW_STUNNED then stunned = true end
            if s == CLAW_UNSTUNNED then unstunned = true end
            if CLAW_GRIP[s] then grip = true end
            if row.cycle > newest then newest = row.cycle end
        end
    end
    last_cycle = newest
    -- (out of sight of the head for longer than its quietest stretch -- away
    -- from the chamber, a death: the steps went on unseen, and only the next
    -- sign can place the list again)
    if seen_at == tick and tick - quiet_since > 20 then synced = false end

    -- (a long absence: a new raid's Olm starts at phase 1)
    if tick - seen_at > 300 then later_phase = false end
    if rise then on_rise(rise) return end
    if death then reset_cycle(api); later_phase = true; return end
    -- (a clench that runs out ends before this tick's step; one the mage
    -- hand's death ends -- the claw gripping -- ends AFTER it: that step is
    -- still clenched, index 4392 [4550])
    if clenched and tick >= clench_until then clenched = false end
    if sign then
        clenched = false
        on_sign(sign)
        if api.config.log_steps then
            api.core.log("olm-cycle step 4 special " .. sign .. " next " .. next_slot() .. " " .. next_special())
        end
        return
    end
    -- an action off the grid: the grid was wrong (a late start, a missed rise);
    -- steps taken on the rise's prediction with no action to confirm them are
    -- undone, the rise's own place kept
    if head_action and not is_step_tick() then
        if since_rise ~= nil then
            parity, list, last_slot = 0, 0, nil
        end
        grid = tick
    end
    if head_action then since_rise = nil end
    if is_step_tick() then
        -- a clenched step: the claw clenching on it, or the mage hand's death
        -- unclenching it with no clench running (the ghost clench); a clench
        -- running out on a step leaves it an ordinary one
        step(stunned or (unstunned and not clenched))
        if since_rise ~= nil then since_rise = since_rise + 1 end
        if api.config.log_steps then
            api.core.log("olm-cycle step " .. tostring(last_slot) .. " special " .. tostring(last_special)
                .. " next " .. next_slot() .. " " .. next_special())
        end
    end
    if stunned then clenched, clench_until = true, tick + CLENCH_TICKS end
    if unstunned or grip then clenched = false end
end

-- ------------------------------------------------------------------ drawing

local function thick_line(draw, x0, y0, x1, y1, colour)
    draw.line(x0, y0, x1, y1, colour)
    draw.line(x0 + 1, y0, x1 + 1, y1, colour)
    draw.line(x0, y0 + 1, x1, y1 + 1, colour)
end

local function polygon(draw, pts, colour)
    for i = 1, #pts do
        local a, b = pts[i], pts[i % #pts + 1]
        thick_line(draw, a[1], a[2], b[1], b[2], colour)
    end
end

-- Each special's icon, drawn in a box's top three quarters.
local function draw_icon(draw, kind, x, y, s)
    local cx, cy = x + s // 2, y + s * 2 // 5
    local r = s // 4
    if kind == "burst" then
        -- a crystal: a diamond with a facet
        polygon(draw, { { cx, cy - r - 2 }, { cx + r - 2, cy }, { cx, cy + r + 2 }, { cx - r + 2, cy } }, 0xD070FF)
        thick_line(draw, cx - r + 2, cy, cx + r - 2, cy, 0xF0C0FF)
    elseif kind == "lightning" then
        -- a bolt
        polygon(draw, { { cx + 2, cy - r - 2 }, { cx - r // 2, cy + 1 }, { cx + 1, cy + 1 },
            { cx - 2, cy + r + 2 }, { cx + r // 2, cy - 2 }, { cx - 1, cy - 2 } }, 0xFFE040)
    elseif kind == "portal" then
        -- two rings
        for _, rr in ipairs({ r, r // 2 + 1 }) do
            local pts = {}
            for k = 0, 7 do
                local a = k * math.pi / 4
                pts[#pts + 1] = { cx + math.floor(rr * math.cos(a) + 0.5), cy + math.floor(rr * math.sin(a) + 0.5) }
            end
            polygon(draw, pts, 0x40E0D0)
        end
    else
        -- the healing hand: a cross
        local w = r // 2
        draw.rect(cx - w, cy - r, 2 * w, 2 * r, 0x40D040, 255)
        draw.rect(cx - r, cy - w, 2 * r, 2 * w, 0x40D040, 255)
    end
end

local function anchor_box(api, draw, total_w, total_h)
    local context = draw.context()
    if not context then return nil end
    local b = context.bounds
    local cfg = api.config
    local x = b.x + cfg.offset_x
    if cfg.corner == "top_right" or cfg.corner == "bottom_right" then
        x = b.x + b.width - total_w - cfg.offset_x
    end
    local y = b.y + cfg.offset_y
    if cfg.corner == "bottom_left" or cfg.corner == "bottom_right" then
        y = b.y + b.height - total_h - cfg.offset_y
    end
    return x, y
end

local LABELS = { "Basic 1", "Empty", "Basic 2" }

function plugin.on_draw_world(api, draw)
    if tick - seen_at > 40 then return end
    local cfg = api.config
    local s = cfg.box_size
    local gap = 4
    local total_w = 4 * s + 3 * gap
    local x, y = anchor_box(api, draw, total_w + 8, s + 26)
    if not x then return end

    draw.rect(x, y, total_w + 8, s + 26, 0x000000, 150)
    local title = glowing and "Great Olm (glowing)" or "Great Olm"
    if grid ~= nil and not synced then title = title .. " - syncing" end
    draw.text(x + 4 + total_w // 2, y + 13, title, synced and 0xFFFFFF or 0xFFB000)

    local known = grid ~= nil
    local hl = nil
    if known then
        if cfg.highlight == "next" then hl = next_slot() else hl = last_slot end
    end
    local wait = nil
    if known and tick >= grid then
        wait = STEP - (tick - grid) % STEP
    elseif known then
        wait = grid - tick
    end

    local by = y + 20
    for i = 1, 4 do
        local bx = x + 4 + (i - 1) * (s + gap)
        local on = (i == hl)
        draw.rect(bx, by, s, s, on and cfg.highlight_color or 0x303030, on and 200 or 180)
        draw.rect(bx + 2, by + 2, s - 4, s - 4, 0x181818, 230)
        if i < 4 then
            draw.text(bx + s // 2, by + s // 2 + 5, LABELS[i], on and cfg.highlight_color or 0xC0C0C0)
        else
            local kind = (cfg.highlight == "last" and last_slot == 4 and last_special) or next_special()
            draw_icon(draw, kind, bx, by, s)
            draw.text(bx + s // 2, by + s - 4, NAMES[kind], on and cfg.highlight_color or 0xC0C0C0)
        end
        if on and cfg.show_countdown and wait and cfg.highlight == "next" then
            draw.text(bx + s - 7, by + 12, tostring(wait), 0xFFFFFF)
        end
    end
    if not known then
        draw.text(x + 4 + total_w // 2, by + s // 2 + 5, "waiting for Olm to act", 0xFFFFFF)
    end
end

function plugin.on_stop(api)
    reset_cycle(api)
end

-- What the boxes show, for a test: the next slot (1..4), the next special,
-- the ticks to the next step, and whether the list's place is known.
function plugin.cycle_state()
    if grid == nil then return nil, next_special(), nil, synced end
    local wait = tick >= grid and (STEP - (tick - grid) % STEP) or (grid - tick)
    return next_slot(), next_special(), wait, synced
end

return plugin
