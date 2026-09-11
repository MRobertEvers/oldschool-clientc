return {id='entity-behavior',on_start=function(host)
    local npc={slot=7,base_npc_id=42,npc_id=100,name='Guard',element_id=123}
    local added,drawn={},{}
    local api={config={tags='',color=0xff00ff,fill=48,shape='mesh'},
        input={key_held=function() return true end},
        menu={add=function(text,tag) added[#added+1]={text=text,tag=tag};return true end},
        world={npc_by_slot=function() return npc end,npc_next=function(cursor)
            if cursor==-1 then return 0,npc end
        end}}
    api.config.set=function(key,value)
        api.config[key]=value;product.on_config_changed(api,key)
    end
    local graphics={world_hull=function(element,color,fill,shape)
        drawn[#drawn+1]={element,color,fill,shape}
    end}
    product.on_start(api)
    local menu={hover_pass=false,rows={{npc_slot=7},{npc_slot=7}}}
    product.on_menu_build(api,menu)
    assert(#added==1 and added[1].text=='Tag @yel@Guard', 'one action per hovered NPC')
    local select={owned=true,tag=added[1].tag}
    -- The same server slot now holds a different species while the menu stays open.
    npc={slot=7,base_npc_id=99,npc_id=99,name='Goblin',element_id=124}
    product.on_menu_select(api,select)
    assert(api.config.tags=='42', 'retained Tag must not retarget a recycled NPC slot')
    product.on_menu_select(api,select)
    assert(api.config.tags=='42', 'retained Tag preserves its intended operation')
    product.on_draw_world(api,graphics)
    assert(#drawn==0, 'replacement species is not highlighted')
    npc={slot=8,base_npc_id=42,npc_id=101,name='Guard',element_id=125}
    product.on_draw_world(api,graphics)
    assert(#drawn==1 and drawn[1][1]==125 and drawn[1][4]=='mesh',
        'tag follows the shell across model transforms and slot changes')
    added={};product.on_menu_build(api,menu)
    assert(added[1].text=='Untag @yel@Guard')
    product.on_menu_select(api,{owned=true,tag=added[1].tag})
    assert(api.config.tags=='', 'Untag removes the saved species')
    product.on_menu_select(api,{owned=false,tag=select.tag})
    assert(api.config.tags=='', 'native menu rows do not alter tags')
    host.core.log('entity behavior passed')
end}
