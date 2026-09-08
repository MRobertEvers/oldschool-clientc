return {id='entity-behavior',on_start=function(host)
    local npc={slot=7,base_npc_id=42,npc_id=100,name='Guard',element_id=123}
    local added,drawn,logged={},{},{}
    local capabilities,shift={},true
    local api={config={tags='',color=0xff00ff,fill=48,shape='mesh'},
        core={log=function(message) logged[#logged+1]=message end,
            capability=function(name) return capabilities[name]==true end},
        input={key_held=function() return shift end},
        menu={add=function(text,tag) added[#added+1]={text=text,tag=tag};return true end},
        world={npc_by_slot=function() return npc end,npc_next=function(cursor)
            if cursor==-1 then return 0,npc end
        end}}
    api.config.set=function(key,value)
        api.config[key]=value;product.on_config_changed(api,key)
    end
    local graphics={world_hull=function(element,color,fill,shape)
        -- draw.world_hull raises on a shape it does not know (lua_world_hull's
        -- "unknown hull shape '%s'"), and a raise out of on_draw_world is what
        -- disables a plugin for the rest of the session.
        assert(shape=='bounds' or shape=='mesh',"unknown hull shape '"..tostring(shape).."'")
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

    -- A `shape` outside the published choices -- one word wrong in a
    -- hand-edited prefs file, which the host stores without checking it --
    -- must not reach draw.world_hull: a raise from on_draw_world disables the
    -- plugin for the rest of the session.
    api.config.set('tags','42')
    logged={};drawn={}
    api.config.set('shape','box')
    product.on_draw_world(api,graphics)
    assert(#drawn==1 and drawn[1][4]=='mesh',
        'an off-schema hull shape falls back to the published default')
    assert(#logged==1 and logged[1]:find("'box'",1,true),
        'and the refused value is named')
    drawn={}
    api.config.set('shape','bounds')
    product.on_draw_world(api,graphics)
    assert(#drawn==1 and drawn[1][4]=='bounds', 'a published choice is still drawn')

    -- api.config.set() TRUNCATES at TORIRS_PLUGIN_CONFIG_VALUE_MAX and reports
    -- success, so a csv that would not survive the write is never handed over:
    -- the saved list stays exactly as the user left it.
    local ids={}
    for id=10001,10032 do ids[#ids+1]=id end
    local full=table.concat(ids,',')
    assert(#full==191, 'the fixture is one id short of the 192-byte value limit')
    api.config.set('tags',full)
    npc={slot=9,base_npc_id=99,npc_id=99,name='Goblin',element_id=126}
    added={};logged={}
    product.on_menu_build(api,menu)
    assert(added[1].text=='Tag @yel@Goblin')
    product.on_menu_select(api,{owned=true,tag=added[1].tag})
    assert(api.config.tags==full,
        'a tag that would not survive the write is refused, never truncated')
    assert(#logged==1, 'and the refusal is reported instead of losing a tag silently')
    drawn={}
    product.on_draw_world(api,graphics)
    assert(#drawn==0, 'a refused tag leaves no hull the saved list cannot carry')
    -- Removing always shortens the list, so an untag from a full list stands.
    npc={slot=9,base_npc_id=10001,npc_id=10001,name='Man',element_id=127}
    added={};product.on_menu_build(api,menu)
    assert(added[1].text=='Untag @yel@Man')
    product.on_menu_select(api,{owned=true,tag=added[1].tag})
    table.remove(ids,1)
    assert(api.config.tags==table.concat(ids,','), 'Untag still writes the shorter list')

    -- The modifier gate is a keyboard's. The mobile preferences ship this
    -- plugin enabled with tags already in them, and a touch client has no
    -- shift to hold, so the rows are the only way back out of a hull.
    shift=false
    added={};product.on_menu_build(api,menu)
    assert(#added==0, 'the modifier still gates a client that has one')
    capabilities.touch=true
    added={};product.on_menu_build(api,menu)
    assert(#added==1 and added[1].text=='Tag @yel@Man',
        'a touch client reaches Tag without a key it does not have')
    host.core.log('entity behavior passed')
end}
