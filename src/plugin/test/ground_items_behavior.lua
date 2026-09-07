return {id='ground-behavior',on_start=function(host)
    local config={}
    for _,item in ipairs(product.config) do
        local value=item.default
        if item.type=='int' then value=tonumber(value)
        elseif item.type=='color' then value=tonumber(value:sub(2),16)
        elseif item.type=='bool' then value=value==true or value=='1' or value=='true' end
        config[item.key]=value
    end
    local origin={3184,3392}
    local me={true_x=3210,true_z=3424,level=0,fine_x=3331,fine_z=4100,dest_x=3215,dest_z=3425}
    local obj={obj_id=1127,name='Rune platebody',count=1,tile_x=3210,tile_z=3424,level=0,cost=39000}
    local labels,projections={},{}
    local tree_listener,hidden_count=nil,0
    local api={config=config,core={log=function() end},assets={request=function() end},
        scripts={invalidate=function() return false end},
        widgets={watch_tree=function(callback) tree_listener=callback;return true end,
            find_all=function(role)
                assert(role=='ground_item_labels')
                return {{set_hidden=function(self,value) assert(value);hidden_count=hidden_count+1;return true end}}
            end},
        input={key_held=function() return false end},
        world={local_player=function() return me end,
            scene_origin=function() if origin then return origin[1],origin[2] end end,
            item_next=function(cursor) if cursor==-1 then return 0,obj end end},
        draw={project=function(x,z,height)
            projections[#projections+1]={x,z};return 100,100
        end}}
    local graphics={text=function(x,y,text,color) labels[#labels+1]=text end,world_tile=function() end}
    -- Startup after world loading, while moving. No world-loaded or server-tick event.
    product.on_start(api)
    tree_listener()
    assert(hidden_count==1,'native label widgets are hidden at publication')
    api.widgets.find_all=function() return nil end
    tree_listener() -- No native overlay capability on the older adapter.
    product.on_draw_world(api,graphics)
    assert(#labels>0 and labels[#labels]:find('Rune platebody',1,true),
        'mid-session enable while moving must immediately draw ground labels')
    assert(projections[1][1]==3392 and projections[1][2]==4160,
        'ground label projection must use the authoritative scene origin')
    origin={3192,3400};labels={};projections={}
    product.on_draw_world(api,graphics)
    assert(projections[1][1]==2368 and projections[1][2]==3136,
        'scene changes must not retain an old origin')
    origin=nil;labels={};projections={}
    product.on_draw_world(api,graphics)
    assert(#labels==0 and #projections==0,'no world means no projection')
    local added={}
    api.menu={add=function(text,tag) added[#added+1]={text=text,tag=tag};return true end}
    api.game={item_info=function(id) assert(id==1127);return {name='Rune platebody'} end}
    api.input.key_held=function() return true end
    api.config.set=function(key,value) config[key]=value;product.on_config_changed(api,key);return true end
    local menu={hover_pass=false,rows={{pick_kind=6,target_id=1127}}}
    product.on_menu_build(api,menu)
    assert(added[1].text=='Highlight @yel@Rune platebody')
    local select={owned=true,tag=added[1].tag}
    api.world.item_next=function() end -- Stack disappears while menu is open.
    product.on_menu_select(api,select)
    product.on_menu_select(api,select)
    assert(config.highlighted_items=='Rune platebody',
        'retained Highlight survives despawn and remains idempotent')
    api.world.item_next=function(cursor) if cursor==-1 then return 0,obj end end
    api.config.set('hidden_items','Rune *, Bones')
    added={};product.on_menu_build(api,menu)
    assert(added[2].text=='Unhide @yel@Rune platebody')
    product.on_menu_select(api,{owned=true,tag=added[2].tag})
    assert(config.hidden_items=='Rune *, Bones' and config.hide_exceptions=='Rune platebody',
        'Unhide one item must preserve wildcard rules and unrelated data')
    added={};product.on_menu_build(api,menu)
    assert(added[2].text=='Hide @yel@Rune platebody','exception changes the current menu action')
    product.on_menu_select(api,{owned=true,tag=added[2].tag})
    assert(config.hidden_items=='Rune *, Bones' and config.hide_exceptions=='',
        'Hide resumes the original wildcard rule without duplicate entries')
    -- Native formatter gets a declared argument window. Native edit controls
    -- must still expose ignored items and reflect native highlight/ignore state.
    local values={[0]=0,0,0,1,0,1,0,39000,1,1127,(3210<<14)|3424}
    local caption,color,invalidations,offset=nil,nil,0,nil
    api.scripts.counts=function() return 11,1 end
    api.scripts.get_int=function(ref,index) return values[index] end
    api.scripts.set_int=function(ref,index,value) if index==6 then color=value else assert(index==2);offset=value end;return true end
    api.scripts.set_string=function(ref,index,value) assert(index==0);caption=value;return true end
    api.scripts.invalidate=function(name) assert(name=='groundItemCaption');invalidations=invalidations+1;return true end
    api.game.item_info=function() return {name='Rune platebody',cost=65000} end
    api.config.set('hidden_items','')
    api.config.set('highlighted_items','')
    product.on_script_callback(api,{name='groundItemCaption',ref={}})
    assert(caption=='Rune platebody (EX: 65K gp) (HA: 39K gp)', 'native formatter preserves plugin price fields')
    values[0]=1
    product.on_script_callback(api,{name='groundItemCaption',ref={}})
    assert(color==config.hidden_color, 'native Ignore remains visible in edit mode with hidden styling')
    values[1]=1
    product.on_script_callback(api,{name='groundItemCaption',ref={}})
    assert(color==config.highlighted_color, 'native Highlight keeps priority over native Ignore')
    local width,height,lift,outlined
    local parent={position=function() return {width=458,height=34} end,
        set_size=function(self,w,h) width=w;height=h;return true end,
        set_projection_height=function(self,h) lift=h;return true end,revalidate=function() return true end}
    local widget={parent=function() return parent end,
        set_text_outline=function(self,value) outlined=value;return true end}
    config.height=160;config.line_gap=30;config.text_outline=true;values[3]=2;values[4]=0
    product.on_script_callback(api,{name='groundItemCaption',ref={},widget=widget})
    assert(width==458 and height==60 and lift==160 and outlined and offset==30,
        'native callback applies projection height, gap and outline through live widgets')
    -- The reveal key is live: a transition asks for one rebuild, a held key none.
    api.input.key_held=function() return true end
    product.on_frame_start(api,{})
    product.on_frame_start(api,{})
    assert(invalidations==1, 'a reveal key transition refreshes the native captions once')
    api.input.key_held=function() return false end
    product.on_frame_start(api,{})
    assert(invalidations==2, 'releasing the reveal key refreshes the native captions again')
    product.on_config_changed(api,'price_mode')
    product.on_stop(api)
    assert(invalidations==4, 'configuration and disable refresh current native caption results')
    host.core.log('ground behavior passed')
end}
