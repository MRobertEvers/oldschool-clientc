-- Appended to the actual shipped source by plugin_lua_test.c. These checks
-- control time and paint counts; native widget ownership is tested separately.
return { id = 'performance-behavior', on_start = function(host)
    local labels, watch, layout_calls = {}, nil, 0
    local work, memory = 4000, 128 * 1024 * 1024
    local api = {
        config = { show_fps=true, show_frame_time=true, show_effective_fps=true,
            show_memory=true, x=10, y=25, text_color=0xffffff, refresh_ms=1000 },
        core = { frame_work_us=function() return work end },
        client = { memory_bytes=function() return memory end },
        widgets = { watch=function(role, callback)
            assert(role=='viewport'); watch=callback; return true
        end },
    }
    local viewport = {}
    function viewport:create_text(key)
        assert(not labels[key], 'duplicate metric widget')
        local label = {}
        function label:set_text(value) self.text=value end
        function label:set_position(x,y) self.x=x; self.y=y; layout_calls=layout_calls+1 end
        function label:set_size(w,h) self.w=w; self.h=h end
        function label:set_text_align(x,y) self.ax=x; self.ay=y end
        function label:set_text_color(value) self.color=value end
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
    for i=1,50 do
        product.on_frame_start(api,{now_ms=i*20,drawn_frames=10+math.floor(i*15/50)})
    end
    assert(text('fps')=='FPS: 15.0', 'FPS must count rendered frames')
    assert(text('frame')=='Frame: 4.00 ms', 'frame time must exclude pacing sleep')
    assert(text('effective')=='Effective FPS: 250.0', 'effective rate uses work time')
    assert(text('memory')=='Memory: 128.0 MiB')
    assert(layout_calls==4, 'frame callbacks must not repair layout')
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
    assert(frame.color==0xff00ff and frame.ax==1 and frame.ay==0, 'style survives port')
    memory=2*1024*1024*1024
    product.on_frame_start(api,{now_ms=2000,drawn_frames=50})
    assert(text('memory')=='Memory: 2.00 GiB')
    watch(viewport,{kind='unbound'})
    assert(next(labels)==nil, 'unbinding removes all metric widgets')
    watch(viewport,{kind='bound'})
    assert(text('frame')=='Frame: 20.00 ms' and labels.performance_memory.y==118,
        'remount preserves current samples and settings')
    product.on_stop(api)
    labels={} -- The production host revokes owned widgets on stop.
    product.on_start(api)
    watch(viewport,{kind='bound'})
    assert(text('frame')=='Frame: 0.00 ms', 'restart clears measurements')
    host.core.log('performance behavior passed')
end }
