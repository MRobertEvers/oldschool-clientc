return { id='roleprobe-behavior', on_start=function(host)
    local watches, hidden, calls = {}, false, 0
    local token = {}
    local widget = {}
    function widget:actions() return {{label='Public chat: Friends',ref=token}} end
    function widget:set_hidden(value) hidden=value;return true end
    function widget:reset() hidden=false;return true end
    function widget:visible() return not hidden end
    function widget:bounds() return {x=1,y=2,width=100,height=30} end
    local api={
        config={public_friends=true,press_report=false,verify_hide=true},
        core={log=function() end},
        widgets={watch=function(role,callback) watches[role]=callback;return true end,
            invoke=function(ref)
                assert(ref==token)
                if hidden then return false,'native_blocked' end
                calls=calls+1;return true,'ok'
            end},
    }
    product.on_start(api)
    for tick=1,200 do product.on_logic_tick(api) end
    assert(calls==0,'unbound probe must not dispatch')
    watches.public_chat_button(widget,{kind='bound'})
    assert(calls==1,'late native binding must execute the pending probe action')
    for tick=1,200 do product.on_logic_tick(api) end
    watches.public_chat_button(widget,{kind='unbound'})
    watches.public_chat_button(widget,{kind='bound'})
    assert(calls==1,'remount must not replay a completed startup action')
    product.on_stop(api)
    assert(watches.public_chat_button==nil,'stop revokes native binding subscription')
    product.on_start(api)
    watches.public_chat_button(widget,{kind='bound'})
    assert(calls==2,'reenable starts a fresh pending action')
    product.on_stop(api)
    host.core.log('role probe behavior passed')
end }
