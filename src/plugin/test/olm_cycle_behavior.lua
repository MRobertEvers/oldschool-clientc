-- Appended to the actual shipped source by plugin_lua_test.c.
--
-- What is pinned is the CYCLE the boxes show, from the head's and the left
-- claw's LOC_ANIMs alone: a rise, the four steps, a special's sign, a special
-- skipped by a head turn, a clench (the list held) and one ended by the mage
-- hand's death (held through that step), a long absence; then the boxes.
-- (The rules: docs/rsprox-analysis/cox/olm.md; the plugin replays index 4392
-- and rsprox.net 820/843/3282/3285 at 321 of 324 specials named.)
return { id = 'olm-cycle-behavior', on_start = function(host)
    local config = {
        box_size = 40, corner = 'top_left', offset_x = 8, offset_y = 8,
        highlight = 'next', highlight_color = 0xFFD700, show_countdown = true, log_steps = false,
    }
    local ring, cycle = {}, 0
    local api = {
        config = config,
        core = { log = function() end },
        world = { loc_anim_next = function(cur)
            local i = (cur < 0) and 1 or cur
            if i > #ring then return nil end
            return i + 1, ring[i]
        end },
    }
    local t = 0
    -- one server tick, with these seqs sent on it
    local function tick(...)
        t = t + 1
        for _, seq in ipairs({ ... }) do
            cycle = cycle + 1
            ring[#ring + 1] = { x = 3238, z = 5738, level = 0, shape = 10, seq_id = seq, cycle = cycle }
        end
        product.on_server_tick(api)
    end
    local function quiet(n) for _ = 1, n do tick() end end
    local function want(slot, special, wait, what)
        local s, sp, w, synced = product.cycle_state()
        assert(s == slot and sp == special and (wait == nil or w == wait),
            what .. ': next ' .. tostring(s) .. ' ' .. tostring(sp) .. ' in ' .. tostring(w))
        return synced
    end
    local ATTACK, TURN, IDLE = 7345, 7341, 7336
    local RISE, BURST, LIGHTNING, PORTAL = 7335, 7356, 7358, 7359
    local CLENCH, GRIP = 7360, 7364

    product.on_start(api)
    -- the rise: the first action 6 ticks on, Basic 1
    tick(RISE)
    assert(want(1, 'burst', 6, 'after the rise'), 'a rise places the cycle')
    quiet(5)
    tick(ATTACK)                                   -- Basic 1 tanked
    want(2, 'burst', 4, 'after Basic 1')
    quiet(3); tick()                               -- the empty: nothing sent
    want(3, 'burst', 4, 'after the empty')
    quiet(3); tick(ATTACK)                         -- Basic 2
    want(4, 'burst', 4, 'the special next')
    quiet(3); tick(BURST)                          -- the crystal burst's sign
    want(1, 'lightning', 4, 'after the burst')
    -- Basic 1, empty, Basic 2, then the head turns instead: the lightning is lost
    quiet(3); tick(ATTACK); quiet(3); tick(); quiet(3); tick(ATTACK)
    quiet(3); tick(TURN)
    want(1, 'portal', 4, 'a skipped special moves the list on')
    -- a clench on the special step holds the portal through every clenched step
    quiet(3); tick(ATTACK); quiet(3); tick(); quiet(3); tick(ATTACK)
    quiet(3); tick(CLENCH)
    want(3, 'portal', 4, 'a clenched null keeps the special')
    quiet(3); tick(ATTACK); quiet(3); tick()
    want(3, 'portal', 4, 'the clench holds the list')
    -- the mage hand dies: the claw grips on a step, which is still clenched
    quiet(3); tick(ATTACK); quiet(3); tick(GRIP)
    want(3, 'portal', 4, 'the step the claw grips on is clenched')
    quiet(3); tick(ATTACK); quiet(3); tick(PORTAL)
    want(1, 'burst', 4, 'the portal at last, then the burst again')
    -- an action off the grid moves the grid
    quiet(1); tick(IDLE); tick(ATTACK)
    local _, _, w = product.cycle_state()
    assert(w == 4, 'an action off the grid re-anchors it')
    -- out of sight: the cycle is not known again until a sign
    quiet(30); tick(ATTACK)
    local _, _, _, synced = product.cycle_state()
    assert(not synced, 'a long absence unsyncs it')
    tick(LIGHTNING)
    assert(want(1, 'portal', 4, 'a sign resyncs it'), 'synced again')

    -- the boxes: four, the next one highlighted, the special named
    local rects, texts, lines = {}, {}, {}
    local graphics = {
        context = function() return { bounds = { x = 0, y = 0, width = 765, height = 503 } } end,
        rect = function(x, y, w, h, c, a) rects[#rects + 1] = { x, y, w, h, c, a } end,
        line = function(x0, y0, x1, y1, c) lines[#lines + 1] = { x0, y0, x1, y1, c } end,
        text = function(x, y, s, c) texts[#texts + 1] = { x, y, s, c } end,
    }
    product.on_draw_world(api, graphics)
    local highlighted, names = 0, {}
    for _, r in ipairs(rects) do if r[5] == config.highlight_color then highlighted = highlighted + 1 end end
    for _, x in ipairs(texts) do names[x[3]] = x[4] end
    assert(highlighted == 1, 'one box highlighted')
    assert(names['Basic 1'] == config.highlight_color, 'the next step, Basic 1, is the highlighted one')
    assert(names['Portal'] ~= nil and names['Empty'] ~= nil and names['Basic 2'] ~= nil, 'four boxes, the special named')
    assert(#lines > 0, 'the special drawn as an icon')
    assert(names['4'] ~= nil, 'the ticks to the next step')
    host.core.log('olm cycle behavior passed')
end }
