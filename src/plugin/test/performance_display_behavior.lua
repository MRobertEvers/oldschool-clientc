-- Appended to the actual shipped source by plugin_lua_test.c. These checks
-- control time and paint counts; native widget ownership is tested separately.
return { id = 'performance-behavior', on_start = function(host)
    local labels, watch, layout_calls = {}, nil, 0
    local work, memory = 4000, 128 * 1024 * 1024
    -- The bound surface, and whatever the frame paints over it. Both are what
    -- the plugin asks the tree for, so both are what the layout is measured
    -- against: 512x334 inset at 4,4 is the fixed root's viewport.
    local view = { x = 4, y = 4, width = 512, height = 334 }
    local chat = nil
    local chat_widget = {}
    function chat_widget:visible() return true end
    function chat_widget:bounds() return chat end
    local api = {
        config = { show_fps=true, show_frame_time=true, show_effective_fps=true,
            show_memory=true, x=10, y=25, text_color=0xffffff, refresh_ms=1000 },
        core = { frame_work_us=function() return work end },
        client = { memory_bytes=function() return memory end },
        widgets = { watch=function(role, callback)
            assert(role=='viewport'); watch=callback; return true
        end, find=function(role)
            if chat and (role=='chat' or role=='frame_chat') then return chat_widget end
            return nil
        end },
    }
    local viewport = {}
    function viewport:bounds() return view end
    function viewport:create_text(key)
        assert(not labels[key], 'duplicate metric widget')
        local label = {}
        function label:set_text(value) self.text=value end
        function label:set_position(x,y) self.x=x; self.y=y; layout_calls=layout_calls+1 end
        function label:set_size(w,h) self.w=w; self.h=h end
        function label:set_text_align(x,y) self.ax=x; self.ay=y end
        function label:set_text_color(value) self.color=value end
        function label:set_hidden(value) self.hidden=value end
        function label:revalidate() end
        function label:remove() labels[key]=nil end
        labels[key]=label
        return label
    end
    local function text(key) return labels['performance_'..key].text end
    product.on_start(api)
    watch(viewport,{kind='bound'})
    product.on_frame_start(api,{now_ms=0,drawn_frames=10})
    -- Fifty callbacks, but only fifteen rendered frames in one second.
    local previous_drawn=10
    for i=1,50 do
        local drawn=10+math.floor(i*15/50)
        work=drawn>previous_drawn and 4000 or 100
        product.on_frame_start(api,{now_ms=i*20,drawn_frames=drawn})
        previous_drawn=drawn
    end
    assert(text('fps')=='FPS: 15.0', 'FPS must count rendered frames')
    assert(text('frame')=='Frame: 4.00 ms', 'frame time excludes pacing sleep and logic-only iterations')
    assert(text('effective')=='Effective FPS: 250.0', 'effective rate uses work time')
    assert(text('memory')=='Memory: 128.0 MiB')
    assert(layout_calls==4, 'frame callbacks must not repair layout')
    -- `x` is labelled "X position", so it is where the text starts: a centred
    -- line in a fixed column moves as its digit count changes and no two rows
    -- share a left edge.
    assert(labels.performance_fps.ax==0 and labels.performance_fps.x==10,
        'the rows are aligned to the x they were given')
    work=20000
    for i=1,10 do product.on_frame_start(api,{now_ms=1000+i*20,drawn_frames=25+i}) end
    assert(text('frame')=='Frame: 20.00 ms', 'work window evicts old samples')
    work=0
    product.on_frame_start(api,{now_ms=1240,drawn_frames=36})
    assert(text('frame')=='Frame: 20.00 ms', 'unmeasured frames do not dilute work')
    api.config.show_fps=false; api.config.show_effective_fps=false
    api.config.x=160; api.config.y=100; api.config.text_color=0xff00ff
    product.on_config_changed(api)
    assert(text('fps')=='' and text('effective')=='', 'disabled metrics must disappear')
    local frame,mem=labels.performance_frame,labels.performance_memory
    assert(frame.x==160 and frame.y==103 and mem.y==118, 'visible lines close gaps')
    assert(frame.color==0xff00ff and frame.ax==0 and frame.ay==0, 'style survives port')
    -- A metric that is off is a HIDDEN node that stays where it was, not a
    -- live empty one laid out on top of the row below it.
    assert(labels.performance_fps.hidden==true
        and labels.performance_effective.hidden==true, 'a metric that is off is hidden')
    assert(frame.hidden==false, 'a metric that is on is not hidden')
    assert(labels.performance_fps.y~=frame.y and labels.performance_effective.y~=mem.y,
        'a hidden metric is not laid out over a visible row')
    memory=2*1024*1024*1024
    product.on_frame_start(api,{now_ms=2000,drawn_frames=50})
    assert(text('memory')=='Memory: 2.00 GiB')
    -- A refresh window the settings panel refuses but a hand-edited
    -- plugin_prefs.ini does not. Two callbacks in the same millisecond would
    -- divide a frame count by a zero-length window.
    api.config.refresh_ms=0
    api.config.show_fps=true
    product.on_frame_start(api,{now_ms=2000,drawn_frames=51})
    product.on_frame_start(api,{now_ms=2000,drawn_frames=51})
    assert(text('fps')=='FPS: 25.0', 'a zero refresh window is held to the declared minimum')
    api.config.show_fps=false
    api.config.refresh_ms=1000
    watch(viewport,{kind='unbound'})
    assert(next(labels)==nil, 'unbinding removes all metric widgets')
    watch(viewport,{kind='bound'})
    assert(text('frame')=='Frame: 20.00 ms' and labels.performance_memory.y==118,
        'remount preserves current samples and settings')
    -- The readout is a child of a surface that does not clip: an x past the
    -- right edge used to put every row off screen while the tree still
    -- reported four unhidden nodes with text in them.
    api.config.show_fps=true; api.config.show_effective_fps=true
    api.config.x=600; api.config.y=25
    product.on_config_changed(api)
    assert(labels.performance_fps.x==380 and labels.performance_fps.y==28,
        'a position past the surface parks the block against its edge')
    -- The mobile root: the surface is the whole canvas and the frame paints
    -- its chat across the top of it, over the default position.
    view.x, view.y, view.width, view.height = 0, 0, 765, 503
    chat = { x=11, y=0, width=519, height=145 }
    api.config.x=10; api.config.y=25
    product.on_config_changed(api)
    assert(labels.performance_fps.y==148 and labels.performance_memory.y==193,
        'the readout starts below a chat region that covers it')
    chat = nil
    product.on_config_changed(api)
    assert(labels.performance_fps.y==28, 'with nothing over it the readout keeps its place')
    product.on_stop(api)
    labels={} -- The production host revokes owned widgets on stop.
    product.on_start(api)
    watch(viewport,{kind='bound'})
    assert(text('frame')=='Frame: 0.00 ms', 'restart clears measurements')
    host.core.log('performance behavior passed')
end }
