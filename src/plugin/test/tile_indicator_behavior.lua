return { id='tile-behavior', on_start=function(host)
    local me={true_x=3200,true_z=3201,level=0,dest_x=3204,dest_z=3205}
    local config={show_hover=true,show_dest=true,true_color=0x00ffff,true_fill_color=0x00ffff,
        true_fill_alpha=40,dest_color=0xffff00,dest_fill_color=0xffff00,dest_fill_alpha=0,
        hover_color=0xffffff,hover_fill_color=0xffffff,hover_fill_alpha=0}
    local api={config=config,world={local_player=function() return me end},
        input={hover_tile=function() return 3200,3201,2 end}}
    local calls={}
    local graphics={world_tile=function(...) calls[#calls+1]={...} end}
    product.on_draw_world(api,graphics)
    assert(#calls==3, 'hover, true and destination markers must all draw')
    assert(calls[1][3]==2 and calls[1][5]==0xffffff, 'hover uses picked level and draws first')
    assert(calls[2][1]==3200 and calls[2][3]==0 and calls[2][5]==0x00ffff,
        'true marker wins hover overlap')
    assert(calls[3][1]==3204 and calls[3][2]==3205, 'destination uses routed flag')
    calls={};me.dest_x=me.true_x;me.dest_z=me.true_z
    product.on_draw_world(api,graphics)
    assert(#calls==2, 'arrival removes destination marker')
    calls={};me=nil
    product.on_draw_world(api,graphics)
    assert(#calls==1 and calls[1][3]==2, 'hover works without a resident player')
    calls={};config.show_hover=false
    product.on_draw_world(api,graphics)
    assert(#calls==0, 'hover setting takes effect immediately')
    host.core.log('tile behavior passed')
end }
