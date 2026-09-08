-- Runs against the actual shipped plugin, with an independently controlled
-- definition-availability response and a stable ground stack.
return { id='loot-beam-behavior', on_start=function(host)
    local ready,cost,present=false,1000,true
    local reads,queries,creates,positions=0,0,0,0
    local live={}
    local api={
        config={tier='low',style='modern',value_mode='alch',spin=0,
            low_value=100,medium_value=10000,high_value=100000,insane_value=1000000,
            low_color=0x66b2ff,medium_color=0x99ff99,high_color=0xff9600,insane_color=0xff66b2},
        core={log=function() end},
        game={item_info=function(id)
            assert(id==123);queries=queries+1
            return ready and {obj_id=id,cost=cost} or nil
        end},
        world={item_next=function(cursor)
            if cursor~=-1 or not present then return nil end
            reads=reads+1
            return 1,{obj_id=123,count=1,cost=ready and cost or 0,tile_x=3200,tile_z=3200,level=0}
        end},
        assets={request=function() return true end,model=function() return 1 end},
        draw={hsl_from_rgb=function() return 30000 end},
        scene={
            instance_create=function() creates=creates+1;live[creates]=true;return creates end,
            instance_destroy=function(id) assert(live[id]);live[id]=nil end,
            instance_position=function() positions=positions+1 end,
            instance_active=function() end,instance_clear_recolors=function() end,
            instance_model=function() end,instance_recolor=function() end,instance_light=function() end,
        },
    }
    local function ticks(count)
        for i=1,count do product.on_logic_tick(api,{logic_cycle=i}) end
    end
    product.on_start(api)
    ticks(751)
    assert(creates==0,'pending definition must not fabricate value or a beam')
    assert(reads==1 and queries>10 and queries<40,
        'late readiness queries continue beyond the former deadline without rescanning the floor')
    ready=true
    ticks(25)
    assert(creates==1 and next(live),'definition arriving after fifteen seconds creates its beam without a new drop event')
    local positioned=positions
    for i=1,100 do product.on_frame_start(api,{now_ms=i*20}) end
    assert(positions==positioned,'spin zero produces no repeated instance writes')
    api.config.spin=90
    for i=1,10 do product.on_frame_start(api,{now_ms=i*20}) end
    assert(positions>positioned,'moving control actually updates beam positions')
    product.on_stop(api)
    assert(next(live)==nil,'stop releases the beam')

    ready,cost,present=true,0,true
    reads,queries,creates=0,0,0
    product.on_start(api);ticks(1000)
    assert(reads==1 and queries==1 and creates==0,
        'resident zero-cost item ends polling immediately')
    product.on_stop(api)

    ready,cost,present=false,1000,true
    reads,queries=0,0
    product.on_start(api);ticks(1)
    present=false;product.on_item_despawn(api,{});ticks(1)
    local stopped_queries=queries
    ticks(1000)
    assert(queries==stopped_queries,'departed pending item leaves no readiness polling behind')
    product.on_stop(api)
    host.core.log('loot beam readiness behavior passed')
end }
