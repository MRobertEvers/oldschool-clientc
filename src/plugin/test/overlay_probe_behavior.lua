return {id='overlay-probe-behavior-'..product.id,on_start=function(host)
    local me={true_x=3210,true_z=3424,level=0,fine_x=3392,fine_z=4160}
    local texts,tiles,instances,meshes,destroyed,mesh_removed=0,{},0,0,0,0
    local vertices={}
    local api={config={height=512,colour=0xff9600,spin=90},core={log=function() end},
        world={local_player=function() return me end,scene_origin=function() return 3184,3392 end,
            item_next=function(cursor) if cursor==-1 then return 0,{name='Rune platebody',tile_x=3210,tile_z=3424,level=0} end end},
        draw={project=function() return 100,100 end,hsl_from_rgb=function(rgb) return rgb end},
        scene={mesh_create=function() meshes=meshes+1;return meshes end,
            mesh_vertex=function(m,x,y,z) vertices[#vertices+1]=y end,
            mesh_face=function() end,mesh_destroy=function() mesh_removed=mesh_removed+1 end,
            instance_create=function() instances=instances+1;return instances end,
            instance_destroy=function() destroyed=destroyed+1 end,
            instance_mesh=function() end,instance_recolor=function() end,
            instance_light=function() end,instance_active=function() end,
            instance_position=function() end,instance_ready=function() return true end}}
    local graphics={text=function() texts=texts+1 end,rect=function() end,line=function() end,
        world_tile=function(x,z,level) tiles[#tiles+1]={x,z,level} end}
    product.on_start(api)
    if product.id=='beam-probe' then
        assert(product.on_logic_tick,'beam creation must use the common logic tick')
        product.on_logic_tick(api)
        product.on_logic_tick(api)
        assert(instances==1 and meshes==1,'quiet ticks must reuse the same beam and mesh')
        api.config.height=256;product.on_config_changed(api,'height')
        assert(destroyed==1 and mesh_removed==1,'height changes release old geometry')
        vertices={};product.on_logic_tick(api)
        assert(instances==2 and meshes==2 and vertices[2]==-256,'height changes rebuild the beam')
        product.on_stop(api)
        assert(destroyed==2 and mesh_removed==2,'stop releases probe instances and meshes')
    else
        product.on_draw_world(api,graphics)
        product.on_draw_world(api,graphics)
        if product.id=='gi-count' then
            assert(texts==4,'ground-count probe labels must render between log intervals')
        else
            assert(tiles[1][1]==3210 and tiles[2][1]==3212 and tiles[1][2]==3424,
                'draw probe must mark the current world instead of hardcoded offline tiles')
        end
    end
    host.core.log('overlay probe behavior passed')
end}
