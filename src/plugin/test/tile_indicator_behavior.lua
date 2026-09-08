return { id='tile-behavior', on_start=function(host)
    local me={true_x=3200,true_z=3201,level=0,dest_x=3204,dest_z=3205}
    local config={show_hover=true,show_dest=true,true_color=0x00ffff,true_fill_color=0x00ffff,
        true_fill_alpha=40,dest_color=0xffff00,dest_fill_color=0xffff00,dest_fill_alpha=0,
        hover_color=0xffffff,hover_fill_color=0xffffff,hover_fill_alpha=0}
    local logs={}
    local api={config=config,world={local_player=function() return me end},
        input={hover_tile=function() return 3200,3201,2 end},
        core={log=function(...) logs[#logs+1]=table.concat({...},'') end}}
    local calls={}
    -- The client answers every push: true when the quad went into the overlay
    -- pool, false with a reason when it did not.
    local pushed=true
    local graphics={world_tile=function(...) calls[#calls+1]={...}
        if pushed then return true,'ok' end
        return false,'budget' end}
    product.on_draw_world(api,graphics)
    assert(#calls==3, 'hover, true and destination markers must all draw')
    assert(calls[1][3]==2 and calls[1][5]==0xffffff, 'hover uses picked level and draws first')
    assert(calls[2][1]==3200 and calls[2][3]==0 and calls[2][5]==0x00ffff,
        'true marker wins hover overlap')
    assert(calls[3][1]==3204 and calls[3][2]==3205, 'destination uses routed flag')
    assert(#logs==0, 'a marker the client accepted is not reported')
    calls={};me.dest_x=me.true_x;me.dest_z=me.true_z
    product.on_draw_world(api,graphics)
    assert(#calls==2, 'arrival removes destination marker')
    calls={};me=nil
    product.on_draw_world(api,graphics)
    assert(#calls==1 and calls[1][3]==2, 'hover works without a resident player')
    calls={};config.show_hover=false
    product.on_draw_world(api,graphics)
    assert(#calls==0, 'hover setting takes effect immediately')
    -- The overlay pool is full: the client drops every quad, the markers
    -- vanish, and without a line in the log that is indistinguishable from the
    -- plugin having been switched off. Once, not once per marker or per frame.
    calls={};config.show_hover=true;pushed=false
    me={true_x=3200,true_z=3201,level=0,dest_x=3204,dest_z=3205}
    product.on_draw_world(api,graphics)
    assert(#calls==3, 'the markers are still attempted')
    assert(#logs==1, 'a dropped marker is reported once, not once per dropped marker')
    assert(logs[1]:find('TILEIND_MARKER_DROPPED',1,true), 'the report names the drop')
    assert(logs[1]:find('budget',1,true), "the report carries the client's reason")
    product.on_draw_world(api,graphics)
    assert(#logs==1, 'the report does not repeat every frame')
    host.core.log('tile behavior passed')
end }
