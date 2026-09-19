-- Appended to the actual shipped source by plugin_lua_test.c. These checks
-- control time and paint counts; native widget ownership is tested separately.
--
-- The api handed to the product is fabricated, and its `porcelain` member is a
-- stand-in for the real reconciler: it runs the describe callback only when an
-- input has moved, and it COMPARES every property before it counts a write.
-- Both halves matter. Without the compare, a plugin that re-stated an
-- unchanged box every frame would still read as five layout calls in the
-- engine; without the run-only-when-moved, "the steady state costs nothing"
-- could not be asserted at all. What is pinned here is therefore the pair: the
-- description the plugin states, and how often it states it.
return { id = 'performance-behavior', on_start = function(host)
    local P = {
        bound = false, last_bound = false, dirty = false, describe_fn = nil,
        items = {}, creates = 0, removes = 0,
        positions = 0, sizes = 0, texts = 0, colors = 0, aligns = 0,
        describe_runs = 0, revalidates = 0, uncommitted = 0,
    }
    -- The viewport's own parent-local origin. Zero so a row's resolved box is
    -- the offset the plugin asked for, which is what the old owned-child
    -- version wrote straight into set_position.
    local VIEWPORT = { x = 0, y = 0 }
    local scratch = {}
    local builder = { text = function(item)
        assert(item.key, 'a porcelain item needs a key')
        for _, seen in ipairs(scratch) do
            assert(seen.key ~= item.key, 'one key, one item')
        end
        assert(item.w and item.w > 0 and item.h and item.h > 0,
            'a text item must state its box: nothing measures a string')
        assert(item.place and item.place.kind == 'inside' and item.place.on == 'viewport'
            and item.place.corner == 'top_left', 'rows sit inside the viewport, top left')
        scratch[#scratch + 1] = item
    end }
    local function setters()
        return P.positions + P.sizes + P.texts + P.colors + P.aligns
    end
    local function reconcile()
        local described = {}
        for _, want in ipairs(scratch) do
            described[want.key] = true
            local applied = P.items[want.key]
            if not P.bound then
                -- A control whose target no longer binds is removed, never
                -- left floating: the layer's rule, not the plugin's.
                if applied then P.items[want.key] = nil; P.removes = P.removes + 1 end
            else
                if not applied then
                    applied = {}; P.items[want.key] = applied; P.creates = P.creates + 1
                    applied.text = want.text; P.texts = P.texts + 1
                    applied.rgb = want.rgb; P.colors = P.colors + 1
                    applied.align = want.align; P.aligns = P.aligns + 1
                else
                    if applied.text ~= want.text then applied.text = want.text; P.texts = P.texts + 1 end
                    if applied.rgb ~= want.rgb then applied.rgb = want.rgb; P.colors = P.colors + 1 end
                    if applied.align ~= want.align then applied.align = want.align; P.aligns = P.aligns + 1 end
                end
                local x = VIEWPORT.x + want.place.dx
                local y = VIEWPORT.y + want.place.dy
                if applied.x ~= x or applied.y ~= y then
                    applied.x = x; applied.y = y; P.positions = P.positions + 1
                end
                if applied.w ~= want.w or applied.h ~= want.h then
                    applied.w = want.w; applied.h = want.h; P.sizes = P.sizes + 1
                end
            end
        end
        for key in pairs(P.items) do
            if not described[key] then P.items[key] = nil; P.removes = P.removes + 1 end
        end
    end
    local porcelain = {
        open = function() return true end,
        close = function()
            for key in pairs(P.items) do P.items[key] = nil; P.removes = P.removes + 1 end
            P.describe_fn = nil; P.dirty = false
        end,
        describe = function(fn) P.describe_fn = fn; P.dirty = true end,
        invalidate = function() P.dirty = true end,
        note = function(kind)
            assert(kind == 'config', 'a settings change is a config input')
            P.dirty = true
        end,
        fence = function()
            -- Binding and unbinding move the element stamp, which is why the
            -- plugin no longer watches anything itself.
            if P.bound ~= P.last_bound then P.dirty = true; P.last_bound = P.bound end
            if P.dirty and P.describe_fn then
                P.describe_runs = P.describe_runs + 1
                local before = setters()
                scratch = {}
                P.describe_fn(builder)
                reconcile()
                P.dirty = false
                if setters() ~= before then P.uncommitted = P.uncommitted + 1 end
            end
        end,
        commit = function()
            if P.uncommitted > 0 then P.revalidates = P.revalidates + 1 end
            P.uncommitted = 0
        end,
    }
    -- TORIRS_DISPLAY_RENDERER_ACTIVE's answer, as a TORIRS_RENDERER_* kind.
    local work, memory, renderer = 4000, 128 * 1024 * 1024, 0
    local api = {
        config = { show_renderer=true, show_fps=true, show_frame_time=true, show_effective_fps=true,
            show_memory=true, x=10, y=25, text_color=0xffffff, refresh_ms=1000 },
        core = {
            frame_work_us=function() return work end,
            log=function() assert(false, 'the layer is present: nothing to report') end,
        },
        client = { memory_bytes=function() return memory end,
            display = { renderer_active = 19 },
            renderer_label = { [0] = 'Software', [1] = 'OpenGL' },
            display_get=function(setting)
                assert(setting == 19, 'the readout asks for the renderer drawing and nothing else')
                return renderer, 0, 6
            end },
        widgets = { watch=function()
            assert(false, 'a ported plugin owns no watch')
        end },
        porcelain = porcelain,
    }
    -- A disabled metric keeps its row and empties its string; it is never
    -- removed. Reading through here is what makes that a named failure rather
    -- than an index of nil three assertions later.
    local function text(key)
        local row = P.items['performance_'..key]
        assert(row, 'performance_'..key..' must exist whether or not it is shown')
        return row.text
    end

    -- The host's pump, modelled. `api.porcelain.open()` installs the real one
    -- in the runtime -- fence, then the plugin's handler, then commit -- and
    -- the second half of this file proves that against the real layer's own
    -- counters. Here it only has to be the same shape, so that what these
    -- cases pin is the plugin and not its boilerplate.
    local function frame(ev)
        porcelain.fence()
        product.on_frame_start(api, ev)
        porcelain.commit()
    end
    local function config_changed()
        porcelain.note('config')
        product.on_config_changed(api)
    end
    -- The pump's one visible consequence. The fence runs BEFORE the plugin's
    -- handler, so a string the handler composes and invalidates for is applied
    -- by the NEXT frame's fence -- a player sees it one frame later, and a
    -- case that asserts on a string the frame it just sampled produced has to
    -- let that fence happen. This is that fence and nothing else: no handler
    -- between, which is exactly what the next frame's pump does first.
    local function next_fence()
        porcelain.fence()
        porcelain.commit()
    end

    product.on_start(api)
    P.bound = true
    frame({now_ms=0,drawn_frames=10})
    -- Exactly five rows, exactly those keys, all five alive even before a
    -- single one of them has a number worth reading.
    local keys = {}
    for key in pairs(P.items) do keys[#keys+1] = key end
    table.sort(keys)
    assert(#keys == 5 and keys[1]=='performance_effective' and keys[2]=='performance_fps'
        and keys[3]=='performance_frame' and keys[4]=='performance_memory'
        and keys[5]=='performance_renderer',
        'five owned rows under exactly those keys')
    assert(P.creates == 5, 'one create per row')
    next_fence()
    assert(text('renderer')=='Renderer: Software', 'the renderer is named from the first frame')
    -- A live renderer switch shows at the next refresh, not at once: it
    -- latches with FPS, whose window straddles the switch anyway.
    renderer=1
    frame({now_ms=10,drawn_frames=10})
    next_fence()
    assert(text('renderer')=='Renderer: Software', 'the renderer waits for the refresh')
    -- Fifty callbacks, but only fifteen rendered frames in one second.
    for i=1,50 do
        frame({now_ms=i*20,drawn_frames=10+math.floor(i*15/50)})
    end
    next_fence()
    assert(text('fps')=='FPS: 15.0', 'FPS must count rendered frames')
    assert(text('frame')=='Frame: 4.00 ms', 'frame time must exclude pacing sleep')
    assert(text('effective')=='Effective FPS: 250.0', 'effective rate uses work time')
    assert(text('memory')=='Memory: 128.0 MiB', 'memory latches on the refresh interval')
    assert(text('renderer')=='Renderer: OpenGL', 'a renderer switched live is named at the refresh')
    assert(P.positions==5, 'frame callbacks must not repair layout')
    assert(P.sizes==5 and P.aligns==5 and P.colors==5, 'style is written once per row')
    assert(P.creates==5 and P.removes==0, 'a frame neither creates nor removes a row')
    work=20000
    for i=1,10 do frame({now_ms=1000+i*20,drawn_frames=25+i}) end
    next_fence()
    assert(text('frame')=='Frame: 20.00 ms', 'work window evicts old samples')
    work=0
    frame({now_ms=1240,drawn_frames=36})
    next_fence()
    assert(text('frame')=='Frame: 20.00 ms', 'unmeasured frames do not dilute work')

    -- Steady state: the ring is full of one value, the refresh window has not
    -- closed, and nothing the plugin would say has changed. It must therefore
    -- say nothing -- no describe run, no setter, no revalidate.
    work=20000
    frame({now_ms=1260,drawn_frames=37})
    next_fence()
    local runs, writes, revalidates = P.describe_runs, setters(), P.revalidates
    for i=1,20 do frame({now_ms=1280+i*20,drawn_frames=37+i}) end
    assert(P.describe_runs==runs, 'an unchanged readout must not re-describe')
    assert(setters()==writes, 'steady state costs zero engine setters')
    assert(P.revalidates==revalidates, 'steady state costs zero revalidates')

    api.config.show_fps=false; api.config.show_effective_fps=false
    api.config.x=160; api.config.y=100; api.config.text_color=0xff00ff
    runs = P.describe_runs
    config_changed()
    frame({now_ms=1700,drawn_frames=58})
    assert(P.describe_runs==runs+1, 'a config change re-describes exactly once')
    assert(text('fps')=='' and text('effective')=='', 'disabled metrics must disappear')
    local frame_row,mem=P.items.performance_frame,P.items.performance_memory
    assert(frame_row.x==160 and frame_row.y==118 and mem.y==133, 'visible lines close gaps')
    assert(frame_row.w==200 and frame_row.h==15, 'the row box is stated, not measured')
    assert(frame_row.rgb==0xff00ff and frame_row.align==1, 'style survives port')
    memory=2*1024*1024*1024
    frame({now_ms=2000,drawn_frames=60})
    next_fence()
    assert(text('memory')=='Memory: 2.00 GiB', 'a gibibyte figure carries two decimals')

    P.bound=false
    frame({now_ms=2020,drawn_frames=61})
    assert(next(P.items)==nil, 'unbinding removes all metric widgets')
    P.bound=true
    local positions = P.positions
    frame({now_ms=2040,drawn_frames=62})
    assert(text('frame')=='Frame: 20.00 ms' and P.items.performance_memory.y==133,
        'remount preserves current samples and settings')
    assert(P.positions==positions+5, 'a remount lays out each row once and stops')

    product.on_stop(api)
    assert(next(P.items)==nil, 'stopping drops the rows with the handle')
    product.on_start(api)
    -- An unmeasured frame records nothing, so what this row reads is the ring
    -- and the window as the restart left them -- not a sample taken since.
    work=0
    frame({now_ms=3000,drawn_frames=70})
    assert(text('frame')=='Frame: 0.00 ms', 'restart clears measurements')
    host.core.log('performance behavior passed')
end }
