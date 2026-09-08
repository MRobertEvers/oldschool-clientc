-- Both actual shipped sources are loaded by the C harness. The default
-- performance block and the example controls must remain separately usable.
return { id='widgetprobe-composition', on_start=function(host)
    local labels, invoked = {}, 0
    local view={x=4,y=4,width=512,height=334}
    local viewport={}
    function viewport:bounds() return view end
    function viewport:create_text(key)
        assert(not labels[key], 'duplicate owned key')
        local label={x=0,y=0,w=180,h=16}
        function label:set_text(v) self.text=v;return true end
        function label:set_position(x,y) self.x=x;self.y=y;return true end
        function label:set_size(w,h) self.w=w;self.h=h;return true end
        function label:set_text_align() return true end
        function label:set_text_color() return true end
        function label:set_hidden(v) self.hidden=v;return true end
        function label:revalidate() return true end
        function label:remove() labels[key]=nil;return true end
        function label:set_on_op(name,callback) self.op=callback;return true end
        function label:bounds() return {x=view.x+self.x,y=view.y+self.y,width=self.w,height=self.h} end
        labels[key]=label;return label
    end
    local sidebar={x=553,y=205}
    function sidebar:position() return {x=self.x,y=self.y} end
    function sidebar:set_position(x,y) self.x=x;self.y=y;return true end
    function sidebar:revalidate() return true end
    local public={}
    function public:actions() return {{label='Public: Show friends',ref='friends'}} end
    local api={
        config={show_fps=true,show_frame_time=true,show_effective_fps=true,show_memory=true,
            x=10,y=25,text_color=0xffffff,refresh_ms=1000},
        core={frame_work_us=function() return 4000 end,log=function() end},
        client={memory_bytes=function() return 128*1024*1024 end},
        game={skill=function(id) assert(id==2);return {current_level=99} end},
        widgets={watch=function(role,callback)
            assert(role=='viewport' or role=='sidebar')
            callback(role=='viewport' and viewport or sidebar,{kind='bound'});return true
        end,find=function(role) if role=='public_chat_button' then return public end end,
        invoke=function(ref) assert(ref=='friends');invoked=invoked+1;return true end},
    }
    performance.on_start(api)
    performance.on_frame_start(api,{now_ms=0,drawn_frames=0})
    performance.on_frame_start(api,{now_ms=1000,drawn_frames=50})
    product.on_start(api)
    for _, probe in ipairs({'strength','public'}) do
        local a=assert(labels[probe]):bounds()
        assert(a.x>=view.x and a.y>=view.y and a.x+a.width<=view.x+view.width and a.y+a.height<=view.y+view.height,
            'example control stays in its native viewport')
        for _, metric in ipairs({'fps','frame','effective','memory'}) do
            local b=assert(labels['performance_'..metric]):bounds()
            local overlap=a.x<b.x+b.width and b.x<a.x+a.width and a.y<b.y+b.height and b.y<a.y+a.height
            assert(not overlap,'example text/control must not overlap the actual performance block')
        end
    end
    labels.public.op(labels.public,{kind='operation'})
    assert(invoked==1 and labels.public.text=='Public: set','composition retains the native control operation')
    assert(labels.strength.text=='Strength: 99','the live skill label remains visible')
    host.core.log('widgetprobe composition passed')
end }
