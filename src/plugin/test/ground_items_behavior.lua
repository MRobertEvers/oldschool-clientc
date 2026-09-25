-- Appended to the actual shipped source by plugin_lua_test.c. ground-items is
-- a Porcelain plugin now, so `api.porcelain` below is a stand-in for the real
-- layer WITH THE LAYER'S RULES IN IT rather than a pass-through. Every rule it
-- enforces is one the port depends on:
--
--   * native_overlay is a three-state latch. SUPPRESSING hides every label
--     widget at each fence; the FIRST caption callback hands them back and only
--     then does the plugin's formatting run; a lane with no hook SITE for that
--     callback never arms it at all and says so once. The gate is
--     `script_callback:<name>` and NOT `cs2_scripts` -- the hook is a patched
--     opcode in the cache, so a CS2 lane on a stock cache answers cs2_scripts
--     yes and then never calls back.
--   * menu_add refuses when the host-wide route table is full and RECORDS the
--     refusal, naming the label nobody will see.
--   * config_list_add/_remove parse, deduplicate, sort, MEASURE against the
--     192-byte config value ceiling and refuse rather than let the store
--     truncate -- leaving the stored list byte-for-byte as it was.
--   * key_edge answers false on a lane with no keyboard frame, with one
--     finding, at the moment it is asked; the edge itself arrives through
--     note_key and never through a fence poll.
--   * tier walks from the top, compares STRICTLY GREATER, and disables a tier
--     whose threshold is at or below zero.
--   * table requests, parses and releases once, and a missing file is one
--     finding and never a second request.
--   * notify coalesces on (kind, subject) per frame.
--   * expect_absent re-labels an absence already recorded; expect_unsupported
--     does NOT, so a declaration that arrives after its finding is too late.
--     The plugin's declaration ORDER is pinned on that difference.
--
-- What this file must NOT grow back: api.input.key_held, api.menu.add,
-- api.widgets.*, api.assets.*, a tag encoding written out by hand, a
-- CONFIG_VALUE_MAX literal, or a call to draw_context / hover / note_menu. The
-- fake refuses all of them.
return {id='ground-behavior',on_start=function(host)
    local CONFIG_VALUE_MAX = 192   -- TORIRS_PLUGIN_CONFIG_VALUE_MAX
    local TAG_OPS = 16             -- PORCELAIN_MENU_TAG_OPS
    local KEY_SHIFT, KEY_CTRL = 42, 43           -- TORIRS_KEY_SHIFT / _CTRL
    local KEY_CODE = { shift = KEY_SHIFT, ctrl = KEY_CTRL }

    local api                                     -- forward: the fake reaches it
    local config = {}
    for _,item in ipairs(product.config) do
        local value=item.default
        if item.type=='int' then value=tonumber(value)
        elseif item.type=='color' then value=tonumber(value:sub(2),16)
        elseif item.type=='bool' then value=value==true or value=='1' or value=='true' end
        config[item.key]=value
    end

    -- ---------------------------------------------------------- the schema
    -- Eleven of these are read on the hot path and four are the four names
    -- porcelain.tiers_from_config looks up BY NAME. A rename is a silent
    -- feature loss, so the names and their shipped defaults are pinned.
    assert(config.hidden_items=='Vial, Ashes, Coins, Bones, Bucket, Jug, Seaweed',
        "the shipped hide list is the reference's own")
    assert(config.highlighted_items=='' and config.highlight_exceptions=='' and
        config.hide_exceptions=='', 'the three other lists ship empty')
    assert(config.low_value==20000 and config.medium_value==100000 and
        config.high_value==1000000 and config.insane_value==10000000,
        'the four thresholds tiers_from_config reads by name')
    assert(config.value_mode=='alch' and config.price_mode=='both',
        'alch is the default measure; both prices are shown')
    assert(config.max_distance==19 and config.line_gap==15 and config.height==20,
        "MAX_DISTANCE 2500 local units, STRING_GAP and OFFSET_Z")
    assert(config.reveal_key=='shift' and config.text_outline==false and
        config.show_highlighted_only==false and config.hide_under_value==0 and
        config.tile_fill==50 and config.highlight_tiles==false)

    -- ------------------------------------------------------------ findings
    -- Coalesced on (verb, element, result), like the real table.
    local findings, absences, unsupporteds = {}, {}, {}
    local function finding(verb, element, result, detail)
        for _, seen in ipairs(findings) do
            if seen.verb == verb and seen.element == element and seen.result == result then
                seen.count = seen.count + 1
                return
            end
        end
        findings[#findings + 1] = { verb = verb, element = element, result = result,
            detail = detail, count = 1,
            -- An UNSUPPORTED is matched on its DETAIL, an ABSENT on its
            -- element -- which is the real table's rule, and the reason the
            -- declarations have to come first.
            expected = (result == 'absent' and absences[element] ~= nil)
                or (result == 'unsupported' and unsupporteds[detail] ~= nil) }
    end
    local function found(verb, result)
        for _, seen in ipairs(findings) do
            if seen.verb == verb and seen.result == result then return seen end
        end
    end
    local function unexpected()
        local total = 0
        for _, seen in ipairs(findings) do if not seen.expected then total = total + 1 end end
        return total
    end

    -- ------------------------------------------------------- the stored list
    local function parse_list(text)
        local out = {}
        for raw in string.gmatch(text or '', '[^,]+') do
            local entry = (string.gsub(raw, '^%s*(.-)%s*$', '%1'))
            local duplicate = false
            for _, seen in ipairs(out) do
                if string.lower(seen) == string.lower(entry) then duplicate = true end
            end
            if entry ~= '' and not duplicate then out[#out + 1] = entry end
        end
        return out
    end
    local function store_list(verb, key, entries)
        table.sort(entries, function(a, b) return string.lower(a) < string.lower(b) end)
        local joined = table.concat(entries, ',')
        -- MEASURE BEFORE STORING. The host's own setter snprintf-truncates and
        -- its validator then accepts the fragment, so a hide list cut mid-name
        -- stores a DIFFERENT rule and reads back as one.
        if #joined + 1 > CONFIG_VALUE_MAX then
            finding(verb, 'none', 'budget', key)
            return false
        end
        api.config.set(key, joined)
        return true
    end
    local function list_contains(key, item)
        for _, entry in ipairs(parse_list(config[key])) do
            if string.lower(entry) == string.lower(item) then return true end
        end
        return false
    end

    -- ------------------------------------------------------------ porcelain
    local edges, opened, closed = {}, 0, 0
    local routes_left = 24                        -- TORIRS_PLUGIN_MENU_ROUTES_MAX
    local added = {}
    -- This plugin owns no control, so these two must stay at zero for the life
    -- of the run. They are counted anyway: "nothing to reconcile" and "the
    -- reconciler happened to write nothing" are not the same claim.
    local setters, revalidates, staged = 0, 0, false
    -- TWO capabilities, and the second is the one the native half turns on.
    -- cs2_scripts says the client runs CS2; script_callback:groundItemCaption
    -- says this client has a hook site that can RAISE that callback, which is
    -- a fact about patched cache bytes and not about the ui logic. The four
    -- CS2 gate lanes answer yes to the first and no to the second.
    local capabilities = { cs2_scripts = true,
        ['script_callback:groundItemCaption'] = true }
    -- Keyed by NAME. `# Rune platebody = 1` is a commented-out row and
    -- `1127 = 65000` is an obj id: the first is the shape the old whole-file
    -- gmatch matched anyway, and the second is the shape that shipped -- an id
    -- from one cache, read on every cache. Neither may price anything.
    local assets = { ['prices.txt'] = '# a comment\nABYSSAL WHIP = 65000\n'
        .. 'not a row\n# Abyssal whip = 1\n1127 = 3\n' }
    local asset_requests = {}
    local table_failed = {}
    local notified = {}
    local frame = 0
    -- The label widgets the latch suppresses, and what it did to them.
    local labels, hidden_count, reset_count = {}, 0, 0
    local overlay = nil

    local porcelain
    porcelain = {
        open = function() opened = opened + 1; return true end,
        close = function()
            closed = closed + 1
            -- Close hands every suppressed native back, which is what makes
            -- "disabling the plugin leaves the cache's labels as the cache
            -- wants them" true without the plugin doing it.
            if overlay and overlay.state == 'suppressing' then
                reset_count = reset_count + #overlay.hidden
                overlay.hidden = {}
            end
            edges, overlay = {}, nil
        end,
        expect_absent = function(element, why)
            assert(why and why ~= '', 'an expected absence has to state its reason')
            absences[element] = why
            -- The real verb RE-LABELS an absence already in the table.
            for _, seen in ipairs(findings) do
                if seen.result == 'absent' and seen.element == element then
                    seen.expected = true
                end
            end
        end,
        expect_unsupported = function(feature, why)
            assert(why and why ~= '', 'a declared limitation has to state its reason')
            -- Deliberately does NOT relabel: the real table computes `expected`
            -- when the finding is recorded and an UNSUPPORTED raised before its
            -- declaration stays unexpected for the session.
            unsupporteds[feature] = why
        end,
        has = function(name) return capabilities[name] == true end,
        require = function(capability, feature)
            if capabilities[capability] then return true end
            finding('require', 'none', 'unsupported', feature)
            return false
        end,
        key_edge = function(config_key, fn)
            if capabilities.touch then
                finding('key_edge', 'role:' .. config_key, 'absent', 'touch lane has no key')
                return false
            end
            edges[#edges + 1] = { key = config_key, fn = fn, down = false, code = -1 }
            return true
        end,
        note_key = function(key, down)
            -- The EDGE, not a poll: a poll cannot see a press that opens and
            -- closes inside one frame, and this is the only way state moves.
            for _, edge in ipairs(edges) do
                if edge.code == key and down ~= edge.down then
                    edge.down = down
                    edge.fn(down)
                end
            end
        end,
        fence = function()
            frame = frame + 1
            for _, edge in ipairs(edges) do
                local name = config[edge.key]
                local code = KEY_CODE[name] or -1
                if code ~= edge.code and edge.down then
                    edge.down = false
                    edge.fn(false)
                end
                edge.code = code
                if code < 0 and not edge.absent then
                    edge.absent = true
                    finding('key_edge', 'role:' .. edge.key, 'absent', name or 'off')
                end
                if code >= 0 then edge.absent = false end
            end
            -- The latch hides whatever the labels role matches at every fence, while
            -- it is suppressing -- and not at all after the handoff.
            if overlay and overlay.state == 'suppressing' then
                overlay.hidden = {}
                for _, label in ipairs(labels) do
                    hidden_count = hidden_count + 1
                    label.hidden = true
                    overlay.hidden[#overlay.hidden + 1] = label
                end
            end
        end,
        commit = function()
            if staged then revalidates = revalidates + 1; staged = false end
        end,
        describe = function() error('this plugin owns no control: it must not describe one') end,
        set = function() error('this plugin owns no control: it must not move one') end,
        draw_context = function()
            error('every primitive here is scene-addressed: there is no rect to derive')
        end,
        hover = function()
            error('hover answers a container cell; a ground stack has no container')
        end,
        note_menu = function()
            error('note_menu stamps inventory cells only: it would stamp nothing here')
        end,
        tier = function(tiers, value)
            -- Walked from the top, STRICTLY greater, and a threshold at or
            -- below zero disables its tier instead of matching everything.
            if tiers.insane > 0 and value > tiers.insane then return 4 end
            if tiers.high > 0 and value > tiers.high then return 3 end
            if tiers.medium > 0 and value > tiers.medium then return 2 end
            if tiers.low > 0 and value > tiers.low then return 1 end
            return 0
        end,
        tiers_from_config = function()
            return { low = config.low_value, medium = config.medium_value,
                     high = config.high_value, insane = config.insane_value }
        end,
        menu_tag = function(subject, op)
            assert(subject >= 0, 'a menu tag names a subject')
            assert(op >= 0 and op < TAG_OPS, 'one of sixteen operations per subject')
            return subject * TAG_OPS + op
        end,
        menu_untag = function(tag) return tag // TAG_OPS, tag % TAG_OPS end,
        menu_add = function(text, action)
            if routes_left <= 0 then
                finding('menu_add', 'role:menu', 'refused', text)
                return false
            end
            routes_left = routes_left - 1
            added[#added + 1] = { text = text, tag = action }
            return true
        end,
        config_list_add = function(key, item)
            if list_contains(key, item) then return true end
            local entries = parse_list(config[key])
            entries[#entries + 1] = item
            return store_list('config_list_add', key, entries)
        end,
        config_list_remove = function(key, item)
            -- Absent is TRUE and costs no write: "this name is not on the
            -- list" is the state the caller asked for.
            if not list_contains(key, item) then return true end
            local kept = {}
            for _, entry in ipairs(parse_list(config[key])) do
                if string.lower(entry) ~= string.lower(item) then kept[#kept + 1] = entry end
            end
            return store_list('config_list_remove', key, kept)
        end,
        native_overlay = function(labels_role, callback, fn)
            assert(labels_role == 'ground_item_labels', 'the lane names the label role')
            assert(callback == 'groundItemCaption', 'the lane names the caption script')
            overlay = { state = 'suppressing', fn = fn, callback = callback, hidden = {} }
            -- Registration asks for one coalesced rebuild, so the plugin does
            -- not have to.
            api.scripts.invalidate(callback)
        end,
        note_script = function()
            if not overlay then return end
            if overlay.state == 'suppressing' then
                -- The handoff: from here the cache's own captions carry the
                -- plugin's fields, so the natives go back to their own
                -- visibility and nothing is hidden again.
                for _, label in ipairs(overlay.hidden) do
                    reset_count = reset_count + 1
                    label.hidden = false
                end
                overlay.hidden = {}
                overlay.state = 'formatting'
            end
            if not overlay.fn(overlay.callback) then
                finding('native_overlay', 'role:' .. overlay.callback, 'refused',
                    'the caption was refused')
            end
        end,
        table = function(asset, parse)
            -- A terminal state is remembered: the real slot answers `failed`
            -- before it reaches assets.request, so a plugin that keeps asking
            -- costs the host nothing. It still answers FALSE, which is why the
            -- plugin's retry never stops -- pinned below as one request, not
            -- as one call.
            if table_failed[asset] then return false end
            asset_requests[#asset_requests + 1] = asset
            local bytes = assets[asset]
            if not bytes then
                table_failed[asset] = true
                finding('table', 'role:' .. asset, 'asset_missing', asset)
                return false
            end
            -- The bytes are released the moment the parse returns; there is no
            -- resident copy for the plugin to hold.
            return parse(bytes) == true
        end,
        notify = function(kind, subject, text)
            for _, seen in ipairs(notified) do
                if seen.kind == kind and seen.subject == subject and seen.frame == frame then
                    return
                end
            end
            notified[#notified + 1] = { kind = kind, subject = subject, text = text,
                                        frame = frame }
        end,
    }

    -- ------------------------------------------------------------------ api
    local origin={3184,3392}
    local me={true_x=3210,true_z=3424,level=0,fine_x=3331,fine_z=4100,dest_x=3215,dest_z=3425}
    local obj={obj_id=1127,name='Rune platebody',count=1,tile_x=3210,tile_z=3424,level=0,cost=39000}
    local drawn,projections={},{}
    local invalidations=0
    config.set=function(key,value)
        assert(#tostring(value) < CONFIG_VALUE_MAX,
            'a value past the ceiling must never reach the store')
        if config[key]==value then return true end
        config[key]=value
        product.on_config_changed(api,key)
        return true
    end
    api={config=config,porcelain=porcelain,
        core={log=function() end,
              notify=function() error('notifications go through porcelain.notify') end},
        scripts={
            invalidate=function(name)
                assert(name=='groundItemCaption');invalidations=invalidations+1;return true
            end},
        -- The raw verbs the port replaced. A regression to any of them is a
        -- refusal going quiet again, so none of them is merely unused.
        widgets={
            watch_tree=function() error('the layer owns the label sweep now') end,
            find_all=function() error('the layer collects the label widgets now') end,
            get=function() error('this plugin holds no widget of its own') end},
        assets={
            request=function() error('prices.txt is porcelain.table\'s to request') end,
            bytes=function() error('porcelain.table hands the bytes to the parse') end,
            release=function() error('porcelain.table releases them itself') end},
        input={key_held=function()
            error('a Porcelain plugin does not poll the reveal key itself')
        end},
        menu={add=function()
            error('a Porcelain plugin adds rows through the layer, which reports a refusal')
        end},
        game={item_info=function(id)
            if id==1127 then return {name='Rune platebody',cost=39000} end
            return nil
        end},
        world={local_player=function() return me end,
            scene_origin=function() if origin then return origin[1],origin[2] end end,
            item_next=function(cursor) if cursor==-1 then return 0,obj end end},
        draw={project=function(x,z)
            projections[#projections+1]={x,z};return 100,100
        end}}
    local graphics={
        text=function(x,y,text,color) drawn[#drawn+1]={x=x,y=y,text=text,color=color} end,
        world_tile=function(x,z,level,fill,outline,alpha)
            drawn[#drawn+1]={tile=true,x=x,z=z,level=level,fill=fill,
                             outline=outline,alpha=alpha}
        end,
        context=function()
            error('a ground label is projected from the scene: there is no rect here')
        end}
    local function labels_of()
        local out={}
        for _,item in ipairs(drawn) do if not item.tile then out[#out+1]=item.text end end
        return out
    end
    -- The host's pump, modelled. The plugin spells no fence and no commit of
    -- its own any more: `porcelain.open()` installs them, and the plugin's own
    -- handler runs BETWEEN the two. The latch's suppression sweep happens at
    -- the fence, so a test that called only the handler would drive a plugin
    -- whose latch never swept -- which is exactly the failure this replaced.
    local function frame_step()
        api.porcelain.fence()
        product.on_frame_start(api, {})
        api.porcelain.commit()
    end
    local function press(key,down) product.on_key(api,{key=key,down=down}) end

    -- ------------------------------------------------------------ lifecycle
    labels={{hidden=false},{hidden=false}}
    product.on_start(api)
    assert(opened==1,'on_start opens the layer')
    assert(#edges==1 and edges[1].key=='reveal_key',
        'the reveal key is one edge, read from a config key')
    assert(absences['role:reveal_key'],
        'the lane that cannot answer the reveal key is declared before it is asked')
    assert(unsupporteds['native captions'] and unsupporteds['draw_refusal_readout'],
        'both lane limitations are declared BEFORE the calls that can record them')
    assert(overlay and overlay.state=='suppressing',
        'the latch arms suppressing: the natives must not double-label')
    assert(invalidations==1,
        'registering the latch is the one coalesced rebuild on_start asks for')
    assert(#asset_requests==1 and asset_requests[1]=='prices.txt',
        'the price table is requested, parsed and released in one call')
    assert(#findings==0,'a CS2 lane with a keyboard raises nothing at start')

    -- The suppression is a per-fence sweep, not a one-shot: the lane rebuilds
    -- its caption widgets and each new one has to be hidden as it arrives.
    frame_step()
    assert(hidden_count==2 and labels[1].hidden and labels[2].hidden,
        'every label widget is hidden while the latch is suppressing')
    labels[#labels+1]={hidden=false}
    frame_step()
    assert(hidden_count==5 and labels[3].hidden,
        'a label widget the lane built after the plugin started is hidden too')

    -- --------------------------------------------------------------- the pass
    product.on_draw_world(api,graphics)
    local shown=labels_of()
    assert(#shown>0 and shown[1]:find('Rune platebody',1,true),
        'mid-session enable while moving must immediately draw ground labels')
    assert(#projections>0 and projections[1][1]==3392 and projections[1][2]==4160,
        'ground label projection must use the authoritative scene origin')
    -- The shadow is drawn BEFORE the ink, one pixel down and right, and the
    -- ink carries the colour: two primitives a line, which is why the outline
    -- is not the default.
    assert(#drawn==2 and drawn[1].x==101 and drawn[1].y==101 and drawn[1].color==0x000000,
        'the default is one shadow, one pixel down-right')
    assert(drawn[2].x==100 and drawn[2].y==100 and drawn[2].color==config.low_color,
        'the ink is last and carries the colour')
    assert(drawn[2].text=='Rune platebody (EX: 39K gp) (HA: 23K gp)',
        'name, then the prices the mode asks for, each only when non-zero')

    -- The price table is the FILE's and not the cache's, and it is keyed on
    -- the item's NAME. It overrides `Abyssal whip` to 65000 where ObjType.cost
    -- says 39000, and the alch price is floor(unit * 3/5) per unit before the
    -- stack multiply. Matched case-insensitively (the row is spelled
    -- ABYSSAL WHIP), and neither the commented-out row nor the `1127 = 3` one
    -- is read -- the second is the shape that shipped, an obj id from one
    -- cache read on every cache, and it is now a name nothing is called.
    obj.name='Abyssal whip';drawn={}
    product.on_draw_world(api,graphics)
    assert(drawn[2].text=='Abyssal whip (EX: 65K gp) (HA: 39K gp)',
        'the NAME row prices the stack: '..drawn[2].text)
    obj.obj_id=1127;obj.name='Rune platebody';drawn={}
    product.on_draw_world(api,graphics)
    assert(drawn[2].text=='Rune platebody (EX: 39K gp) (HA: 23K gp)',
        'and an obj id the file lists prices nothing: '..drawn[2].text)

    origin={3192,3400};drawn={};projections={}
    product.on_draw_world(api,graphics)
    assert(projections[1][1]==2368 and projections[1][2]==3136,
        'scene changes must not retain an old origin')
    origin=nil;drawn={};projections={}
    product.on_draw_world(api,graphics)
    assert(#drawn==0 and #projections==0,'no world means no projection')
    origin={3184,3392}

    -- The stack size and the price mode. An item the price file does NOT name
    -- falls back to the cache's own OC_COST, which is also what makes obj.cost
    -- the lever for every value case below.
    obj.obj_id=4151;obj.cost=100;obj.count=5;drawn={}
    product.on_draw_world(api,graphics)
    assert(drawn[2].text=='Rune platebody (5) (EX: 500 gp) (HA: 300 gp)',
        'an item the price file does not name is priced from OC_COST, per unit then stacked')
    obj.count=65535;drawn={}
    product.on_draw_world(api,graphics)
    assert(drawn[2].text:find('(Lots!)',1,true),'65535 is where the reference stops counting')
    obj.count=12000;drawn={}
    product.on_draw_world(api,graphics)
    assert(drawn[2].text:find('(12K)',1,true),'QuantityFormatter.quantityToStackSize')
    obj.count=1
    config.set('price_mode','off');drawn={}
    product.on_draw_world(api,graphics)
    assert(drawn[2].text=='Rune platebody','price_mode off is the name alone')
    config.set('price_mode','value');drawn={}
    product.on_draw_world(api,graphics)
    assert(drawn[2].text=='Rune platebody (100 gp)','price_mode value is the exchange price')
    config.set('price_mode','alch');drawn={}
    product.on_draw_world(api,graphics)
    assert(drawn[2].text=='Rune platebody (60 gp)','price_mode alch is the high-alchemy one')
    config.set('price_mode','both')

    -- ------------------------------------------- the M-to-B roll, and coins
    -- QuantityFormatter.quantityToStackSize gives each suffix TEN THOUSAND of
    -- its own unit: plain below 10,000, K below 10,000,000, M below
    -- 10,000,000,000, B after that. The K rung was right and the M one rolled
    -- at 1e9 -- a thousand times early -- so a max cash stack printed "2B",
    -- and every figure between 1e9 and 1e10 lost three digits to a unit the
    -- reference does not reach for until ten times further up.
    obj.obj_id=4151;obj.count=1
    config.set('price_mode','value')
    local function priced(cost)
        obj.cost=cost;drawn={}
        product.on_draw_world(api,graphics)
        return drawn[2].text
    end
    local function prints(cost,want,why)
        local got=priced(cost)
        assert(got=='Rune platebody ('..want..' gp)',why..': '..got)
    end
    prints(2147483647,'2147M','a max cash stack is 2147M, not 2B')
    prints(9999999999,'9999M','M runs to one under ten thousand million')
    prints(10000000000,'10B','B begins at 1e10, where the reference begins it')
    -- The K rung, unchanged, so a fix to the one above cannot quietly move it.
    prints(1500000,'1500K','1500K')
    prints(9999999,'9999K','K runs to 9999K')
    prints(10000000,'10M','and M begins at 1e7')
    config.set('price_mode','both')

    -- COINS. A coin's cache cost is 1, floor(1 * 0.6) is 0, and the alch price
    -- is truncated per unit -- so under the shipped `alch` value_mode a pile of
    -- five thousand coins was worth NOTHING: no tier, and no "(HA: ...)" at
    -- all, because label_for prints a price only when it is non-zero.
    -- GroundItemsPlugin.buildGroundItem corrects exactly this after its price
    -- lookup -- setHaPrice(1), setGePrice(1) -- so a coin is one gp under both
    -- prices and a pile is worth its count.
    config.set('hidden_items','')           -- 'Coins' is on the shipped hide list
    obj.obj_id=995;obj.name='Coins';obj.cost=1;obj.count=5000;drawn={}
    product.on_draw_world(api,graphics)
    assert(drawn[2].text=='Coins (5000) (EX: 5000 gp) (HA: 5000 gp)',
        'five thousand coins is worth five thousand gp: '..drawn[2].text)
    -- And it is a number the thresholds can see, which is the whole defect:
    -- with the alch price at zero no coin pile could ever earn a tier.
    config.set('low_value',4999);drawn={}
    product.on_draw_world(api,graphics)
    assert(drawn[2].color==config.low_color,'5000 coins clears a threshold of 4999')
    config.set('low_value',5000);drawn={}
    product.on_draw_world(api,graphics)
    assert(drawn[2].color==config.default_color,
        'and is worth exactly 5000 -- the correction is one gp a coin, not 0.6 of one')
    config.set('low_value',20000)
    config.set('hidden_items','Vial, Ashes, Coins, Bones, Bucket, Jug, Seaweed')
    -- Back to what the block below expects: `Rune platebody` is a name
    -- prices.txt does NOT carry, so obj.cost is the lever again.
    obj.obj_id=4151;obj.name='Rune platebody';obj.cost=100;obj.count=1

    -- ---------------------------------------------------- the tier zero gate
    -- The reference DISABLES a tier whose threshold is at or below zero. This
    -- plugin used to read a zero low threshold as "everything is low value",
    -- so a stack worth ten coins came out blue.
    obj.cost=10;drawn={}
    config.set('low_value',0)
    product.on_draw_world(api,graphics)
    assert(drawn[2].color==config.default_color,
        'a threshold at or below zero disables its tier, it does not match everything')
    config.set('low_value',20000)
    -- And the comparison is STRICTLY greater: worth exactly the low threshold
    -- is not a low-value item.
    obj.cost=33334;drawn={}      -- alch 20000 exactly
    product.on_draw_world(api,graphics)
    assert(drawn[2].color==config.default_color,'exactly the threshold is not that tier')
    obj.cost=33335;drawn={}      -- alch 20001
    product.on_draw_world(api,graphics)
    assert(drawn[2].color==config.low_color,'one over the threshold is')
    -- value_mode chooses what the thresholds MEASURE, independently of what
    -- price_mode prints.
    config.set('value_mode','value');drawn={}
    product.on_draw_world(api,graphics)
    assert(drawn[2].color==config.low_color,'the exchange price is over the low threshold')
    config.set('medium_value',33334);drawn={}
    product.on_draw_world(api,graphics)
    assert(drawn[2].color==config.medium_color,'and over the medium one by a coin')
    config.set('medium_value',100000);config.set('value_mode','alch')

    -- ------------------------------------------------- hide under value
    -- BOTH prices have to be under the threshold: an item whose exchange price
    -- clears it is not a cheap item just because its alch price does not. The
    -- stack is worth 100/60 here, which earns no tier -- a VALUE-earned
    -- highlight would beat the hide and the case would prove nothing.
    obj.cost=100
    config.set('hide_under_value',80);drawn={}
    product.on_draw_world(api,graphics)
    assert(#drawn==2,'one price over the threshold is not "under value"')
    config.set('hide_under_value',150);drawn={}
    product.on_draw_world(api,graphics)
    assert(#drawn==0,'both prices under the threshold hides the stack')
    -- An explicit highlight beats a value-earned hide.
    config.set('highlighted_items','Rune platebody');drawn={}
    product.on_draw_world(api,graphics)
    assert(#drawn==2 and drawn[2].color==config.highlighted_color,
        'an explicit highlight beats a value-earned hide')
    config.set('highlighted_items','')
    -- And show_highlighted_only drops what earns no highlight at all.
    config.set('hide_under_value',0);config.set('show_highlighted_only',true);drawn={}
    product.on_draw_world(api,graphics)
    assert(#drawn==0,'show highlighted only drops a stack that earns no highlight')
    config.set('show_highlighted_only',false)
    obj.obj_id=1127;obj.cost=39000;drawn={}

    -- ------------------------------------------------------ highlight tiles
    config.set('highlight_tiles',true)
    drawn={}
    product.on_draw_world(api,graphics)
    -- Found rather than indexed, so WHERE it is in the list is the order
    -- assertion's business alone and this one only judges the wash itself.
    local function washes_and_ink(list)
        local washes,ink,last_tile,first_text={},{},0,nil
        for at,item in ipairs(list) do
            if item.tile then
                washes[#washes+1]=item; last_tile=at
            else
                if item.color~=0x000000 then ink[#ink+1]=item end
                if not first_text then first_text=at end
            end
        end
        return washes,ink,last_tile,first_text
    end
    local washes,ink,last_tile,first_text=washes_and_ink(drawn)
    assert(#washes==1 and washes[1].x==3210 and washes[1].z==3424 and
        washes[1].level==0 and washes[1].alpha==config.tile_fill and
        washes[1].fill==ink[1].color and washes[1].outline==ink[1].color,
        "the tile takes the top item's colour with the tile_fill wash")

    -- ORDER. The wash is a translucent quad over the tile and the caption is
    -- drawn at that same tile's projected centre, so the two overlap by
    -- construction. Drawn after, the wash paints out the middle of its own
    -- label -- the photographed defect was "Abyssal tentacle (EX: 90M gp)"
    -- with the price under a pink smear. A highlight goes UNDER the thing it
    -- highlights.
    --
    -- This was pinned the wrong way round here (`drawn[#drawn]`), which is why
    -- no test saw it: one stack on one tile makes "last" and "after its own
    -- label" the same sentence.
    assert(first_text and last_tile<first_text,
        'every tile wash goes down before any caption, or it paints the caption out')

    -- And ACROSS tiles, which the single-tile case cannot see: interleaving
    -- per tile let a stack lose its text to the NEXT tile's wash, so the
    -- damage depended on pool order.
    local neighbour={obj_id=4151,name='Abyssal whip',count=1,tile_x=3211,tile_z=3424,
                     level=0,cost=2000000}
    api.world.item_next=function(cursor)
        if cursor==-1 then return 0,obj end
        if cursor==0 then return 1,neighbour end
    end
    drawn={}
    product.on_draw_world(api,graphics)
    washes,ink,last_tile,first_text=washes_and_ink(drawn)
    assert(#washes==2,'two occupied tiles are two washes')
    assert(#ink==2,'and two captions')
    assert(first_text and last_tile<first_text,
        'BOTH washes precede BOTH captions: no tile may paint over its neighbour')
    api.world.item_next=function(cursor) if cursor==-1 then return 0,obj end end
    config.set('highlight_tiles',false)
    drawn={}
    product.on_draw_world(api,graphics)
    for _,item in ipairs(drawn) do
        assert(not item.tile,'highlight_tiles off draws no wash at all')
    end

    -- ------------------------------------------------ per-tile stacking
    -- Two stacks on one tile: the most valuable line is nearest the ground and
    -- the column climbs by line_gap. The reference stacks in ARRIVAL order,
    -- which reshuffles as stacks come and go; this does not.
    local second={obj_id=4152,name='Bronze dagger',count=1,tile_x=3210,tile_z=3424,
                  level=0,cost=10}
    api.world.item_next=function(cursor)
        if cursor==-1 then return 0,obj end
        if cursor==0 then return 1,second end
    end
    local function ink_of()
        local ink={}
        for _,item in ipairs(drawn) do
            if not item.tile and item.color~=0x000000 then ink[#ink+1]=item end
        end
        return ink
    end
    drawn={}
    product.on_draw_world(api,graphics)
    local ink=ink_of()
    assert(#ink==2 and ink[1].text:find('Rune platebody',1,true) and ink[1].y==100,
        'the most valuable line on a tile is the one nearest the ground')
    assert(ink[2].text:find('Bronze dagger',1,true) and ink[2].y==100-config.line_gap,
        'and the column above it climbs by line_gap')
    -- The per-tile line list is REUSED across frames, so a tile that loses a
    -- line has to drop it: a row left in the pool would be sorted back in and
    -- drawn as a stack that is not there any more.
    api.world.item_next=function(cursor) if cursor==-1 then return 0,obj end end
    drawn={}
    product.on_draw_world(api,graphics)
    ink=ink_of()
    assert(#ink==1 and ink[1].text:find('Rune platebody',1,true),
        'a tile that loses a line draws one line, not one left over from last frame')

    -- -------------------------------------------------- the reveal key edge
    -- The rows are behind the key and the key arrives as an EDGE. A fence poll
    -- is not what moves it: nothing here ever calls input.key_held, which
    -- errors.
    local menu={hover_pass=false,rows={{pick_kind=6,target_id=1127}}}
    added={};product.on_menu_build(api,menu)
    assert(#added==0,'the rows are not offered while the reveal key is up')
    press(KEY_CTRL,true)
    added={};product.on_menu_build(api,menu)
    assert(#added==0,'a key that is not the bound one moves nothing')
    press(KEY_SHIFT,true)
    product.on_menu_build(api,{hover_pass=true,rows={{pick_kind=6,target_id=1127}}})
    assert(#added==0,'the hover pass is left on the first statement')

    -- ------------------------------------------------------- tag and retain
    api.world.item_next=function() end   -- the row's name is the OBJTYPE's
    added={};product.on_menu_build(api,menu)
    assert(#added==2,'one Highlight row and one Hide row per distinct ground obj')
    assert(added[1].text=='Highlight @yel@Rune platebody' and
        added[2].text=='Hide @yel@Rune platebody',
        'the row text is resolved from the objtype, not from a walk of the scene')
    assert(added[1].tag==1127*TAG_OPS+1 and added[2].tag==1127*TAG_OPS+3,
        'subject and intended direction are frozen into the one shared encoding')
    menu.rows[#menu.rows+1]={pick_kind=6,target_id=1127}
    menu.rows[#menu.rows+1]={pick_kind=2,target_id=1127}
    added={};product.on_menu_build(api,menu)
    assert(#added==2,'rows are deduplicated by obj, and only pick kind 6 is a ground stack')
    menu.rows={{pick_kind=6,target_id=1127}}

    local retained={owned=true,tag=added[1].tag}
    product.on_menu_select(api,retained)
    product.on_menu_select(api,retained)
    assert(config.highlighted_items=='Rune platebody',
        'retained Highlight survives despawn and remains idempotent')
    product.on_menu_select(api,{owned=false,tag=retained.tag})
    assert(config.highlighted_items=='Rune platebody','native menu rows do not alter the lists')
    api.world.item_next=function(cursor) if cursor==-1 then return 0,obj end end

    -- ------------------------------------------- the exception, not a deletion
    config.set('hidden_items','Rune *, Bones')
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

    -- A list the verb actually WRITES comes back in the verb's own shape:
    -- sorted, case-insensitively deduplicated and comma-joined. The spacing a
    -- user typed is the store's, not one of the rules.
    config.set('highlighted_items','Zamorak *, Abyssal whip')
    added={};product.on_menu_build(api,menu)
    product.on_menu_select(api,{owned=true,tag=added[1].tag})
    assert(config.highlighted_items=='Abyssal whip,Rune platebody,Zamorak *',
        'a written list is stated whole: sorted, deduplicated, comma-joined')
    config.set('highlighted_items','')
    config.set('hidden_items','Vial, Ashes, Coins, Bones, Bucket, Jug, Seaweed')

    -- ---------------------------------------------- the ceiling, out loud
    -- Twenty eight-character rules join to 179 bytes; one more name is 194 and
    -- the store would have snprintf'd it to 191 and ACCEPTED the fragment,
    -- which is a rule cut mid-name that reads back as a different one.
    local filler={}
    for n=1,20 do filler[#filler+1]=string.format('Aa%06d',n) end
    config.set('highlighted_items',table.concat(filler,','))
    config.set('highlight_exceptions','Rune platebody')
    local stored_list, stored_exceptions = config.highlighted_items, config.highlight_exceptions
    assert(#stored_list==179 and #stored_list+#',Rune platebody'>CONFIG_VALUE_MAX,
        'the fixture list is one addition short of the ceiling')
    added={};product.on_menu_build(api,menu)
    assert(added[1].text=='Highlight @yel@Rune platebody',
        'the exception means the item is not highlighted, so the row turns it on')
    product.on_menu_select(api,{owned=true,tag=added[1].tag})
    assert(config.highlighted_items==stored_list,
        'a refused addition leaves the stored list unchanged')
    assert(config.highlight_exceptions==stored_exceptions,
        'and leaves the OTHER list it would have edited unchanged too')
    local refusal=found('config_list_add','budget')
    assert(refusal and refusal.detail=='highlighted_items',
        'the refused addition is one finding naming the list it would not store')
    assert(unexpected()==1,'and it is not an absence anybody declared')
    config.set('highlighted_items','');config.set('highlight_exceptions','')

    -- --------------------------------------------- the route table, out loud
    -- TORIRS_PLUGIN_MENU_ROUTES_MAX is 24 shared by every plugin in one build.
    -- Over it `add` answers false; the loop stops, and the refusal is a line
    -- naming the row nobody will see.
    routes_left=1;added={}
    product.on_menu_build(api,menu)
    assert(#added==1,'the first row fits and the second does not')
    local refused=found('menu_add','refused')
    assert(refused and refused.detail=='Hide @yel@Rune platebody',
        'a refused menu row is one finding naming the label nobody will see')
    routes_left=0;added={}
    product.on_menu_build(api,menu)
    assert(#added==0,'a full route table adds nothing')
    assert(unexpected()==2,'a refusal is never an expected absence')
    routes_left=24

    -- ------------------------------------------------------- notifications
    -- The log line nobody playing could see is a game line now, and twelve
    -- bones landing on one tile is one announcement.
    config.set('notify_highlighted',true)
    config.set('highlighted_items','Rune platebody')
    product.on_item_spawn(api,obj)
    product.on_item_spawn(api,obj)
    assert(#notified==1 and notified[1].kind=='highlight' and notified[1].subject==1127 and
        notified[1].text=='highlighted drop: Rune platebody (EX: 39K gp) (HA: 23K gp)',
        'a highlighted drop is one announcement per obj per frame')
    config.set('notify_highlighted',false);config.set('highlighted_items','')
    config.set('notify_tier','low')
    frame_step();notified={}
    product.on_item_spawn(api,obj)
    assert(#notified==1 and notified[1].kind=='tier',
        'a tier announcement is a different kind and stays a different line')
    config.set('notify_tier','insane');notified={}
    frame_step()
    product.on_item_spawn(api,obj)
    assert(#notified==0,'below the floor there is nothing to announce')
    config.set('notify_tier','off')

    -- ------------------------------------------------ the native handoff
    -- The FIRST caption callback is the handoff: the suppressed label widgets
    -- go back to their own visibility and only then does this plugin's
    -- formatting run. Nothing is hidden again afterwards.
    local values={[0]=0,0,0,1,0,1,0,39000,1,1127,(3210<<14)|3424}
    local caption,color,offset=nil,nil,nil
    api.scripts.counts=function() return 11,1 end
    api.scripts.get_int=function(ref,index) return values[index] end
    api.scripts.set_int=function(ref,index,value)
        if index==6 then color=value else assert(index==2);offset=value end;return true
    end
    api.scripts.set_string=function(ref,index,value)
        assert(index==0);caption=value;return true
    end
    api.game.item_info=function() return {name='Rune platebody',cost=65000} end
    local before_hidden=hidden_count
    product.on_script_callback(api,{name='somethingElse',ref={}})
    assert(caption==nil,"another script's callback is not this plugin's")
    product.on_script_callback(api,{name='groundItemCaption',ref={}})
    assert(overlay.state=='formatting' and reset_count==3,
        'the first caption callback hands every suppressed native back')
    assert(caption=='Rune platebody (EX: 65K gp) (HA: 39K gp)',
        'native formatter preserves plugin price fields')
    frame_step()
    assert(hidden_count==before_hidden,'nothing is suppressed after the handoff')
    drawn={}
    product.on_draw_world(api,graphics)
    assert(#drawn==0,'and the plugin stops drawing its own labels')

    -- Native Highlight and native Ignore, and their precedence.
    values[0]=1
    product.on_script_callback(api,{name='groundItemCaption',ref={}})
    assert(color==config.hidden_color,
        'native Ignore remains visible in edit mode with hidden styling')
    values[1]=1
    product.on_script_callback(api,{name='groundItemCaption',ref={}})
    assert(color==config.highlighted_color,'native Highlight keeps priority over native Ignore')

    -- The native visibility rule: the packed coord's own level and distance.
    values[0],values[1],values[5]=0,0,0
    values[10]=(1<<28)|(3210<<14)|3424
    product.on_script_callback(api,{name='groundItemCaption',ref={}})
    assert(caption=='','a stack on another plane is blanked, not drawn')
    values[10]=((3210+20)<<14)|3424
    product.on_script_callback(api,{name='groundItemCaption',ref={}})
    assert(caption=='','a stack past max_distance in either axis is blanked')
    values[10]=(3210<<14)|3424
    product.on_script_callback(api,{name='groundItemCaption',ref={}})
    assert(caption~='','and one on the plane and in range is not')

    -- The widget edits the native row measures before it places its buttons.
    local width,height,lift,outlined
    local parent={position=function() return {width=458,height=34} end,
        set_size=function(self,w,h) width=w;height=h;return true end,
        set_projection_height=function(self,h) lift=h;return true end,
        revalidate=function() return true end}
    local widget={parent=function() return parent end,
        set_text_outline=function(self,value) outlined=value;return true end}
    config.height=160;config.line_gap=30;config.text_outline=true;values[3]=2;values[4]=0
    product.on_script_callback(api,{name='groundItemCaption',ref={},widget=widget})
    assert(width==458 and height==60 and lift==160 and outlined and offset==30,
        'native callback applies projection height, gap and outline through live widgets')
    config.height=20;config.line_gap=15;config.text_outline=false;values[3]=1;values[4]=0

    -- ----------------------------------------- one invalidate per edge
    -- Start, config change, reveal transition and stop each ask for exactly
    -- one coalesced rebuild -- never two, never per row.
    invalidations=0
    press(KEY_SHIFT,false)
    frame_step()
    assert(invalidations==1,'a reveal key transition refreshes the native captions once')
    frame_step();frame_step()
    assert(invalidations==1,'a key that did not move asks for nothing')
    press(KEY_SHIFT,true);frame_step()
    assert(invalidations==2,'and the other direction refreshes them again')
    product.on_config_changed(api,'price_mode')
    assert(invalidations==3,'a config change refreshes the current results')

    -- ----------------------------------------------------------- the frame
    -- Nothing described and nothing to move: a settled frame is the fence, a
    -- commit that stages nothing, and the reveal comparison.
    local before_setters,before_revalidates,before_requests=setters,revalidates,#asset_requests
    for _=1,20 do frame_step() end
    assert(setters==before_setters,'steady state costs zero engine setters')
    assert(revalidates==before_revalidates,'steady state costs zero revalidates')
    assert(setters==0 and revalidates==0,
        'this plugin owns no control, so it never pays for one')
    assert(#asset_requests==before_requests,'a settled price table is never re-requested')
    assert(invalidations==3,'a settled frame asks for no native rebuild')

    -- --------------------------------------------------------------- stop
    product.on_stop(api)
    assert(invalidations==4,'disabling refreshes the current native caption results once')
    porcelain.close()
    assert(closed==1)

    -- ------------------------------------------ a lane with no keyboard
    capabilities.touch=true
    findings={};absences={};unsupporteds={};asset_requests={};invalidations=0
    labels={{hidden=false}}
    product.on_start(api)
    assert(#edges==0,'a touch lane registers no edge at all')
    local absent=found('key_edge','absent')
    assert(absent and absent.element=='role:reveal_key' and absent.expected,
        'a lane with no keyboard frame is one declared absence, not silence')
    added={};frame_step()
    product.on_menu_build(api,menu)
    assert(#added==0,'the Highlight and Hide rows report themselves off, not merely never held')
    assert(unexpected()==0,'and a frame there costs nothing and says nothing new')
    product.on_stop(api);porcelain.close()
    capabilities.touch=nil

    -- ---------------------------------------------------- a lane with no CS2
    -- rs289lc / rs254lc have no CS2, so there is no caption script and no
    -- label role. The drawn overlay is unchanged and the native half is one
    -- DECLARED finding rather than a latch hiding captions it will never
    -- replace.
    capabilities.cs2_scripts=nil
    capabilities['script_callback:groundItemCaption']=nil
    findings={};absences={};unsupporteds={};invalidations=0;overlay=nil
    labels={{hidden=false}}
    product.on_start(api)
    assert(overlay==nil,'no CS2 means no latch and nothing suppressed')
    assert(invalidations==0,'and no caption rebuild asked for on a lane with no captions')
    local off=found('require','unsupported')
    assert(off and off.detail=='native captions' and off.expected,
        'the CS1 lane limitation is one finding, declared before it was raised')
    assert(unexpected()==0,'declaring it is what makes the clean gate satisfiable there')
    frame_step()
    assert(labels[1].hidden==false,'the lane keeps its own labels')
    drawn={}
    product.on_draw_world(api,graphics)
    assert(#drawn==2 and drawn[2].text:find('Rune platebody',1,true),
        'the drawn overlay is bit-identical on a lane with no CS2')
    product.on_stop(api);porcelain.close()
    capabilities.cs2_scripts=true

    -- ------------------------------------------- a CS2 lane with NO HOOK SITE
    -- Every CS2 lane in this checkout. The cache's caption script is run by
    -- the lane's own overlay and raises nothing, because the callback is a
    -- RUNELITE_CALLBACK opcode that tools/plugin_engine_script_hooks.py
    -- patches in and no shipped cache carries. cs2_scripts answers YES here,
    -- so gating on it armed the latch and left it SUPPRESSING for the whole
    -- session: no formatting, no callback, no finding and no log line -- the
    -- pictures only looked right because the fallback draw ran underneath.
    --
    -- This is the SAME assertion set as the CS1 case above and that is the
    -- point: the two lanes differ in ui logic and not in what this half of
    -- the plugin can do, so they must report identically.
    capabilities['script_callback:groundItemCaption']=nil
    findings={};absences={};unsupporteds={};invalidations=0;overlay=nil
    labels={{hidden=false}}
    product.on_start(api)
    assert(overlay==nil,'CS2 alone is not a hook site: no latch and nothing suppressed')
    assert(invalidations==0,'and no rebuild asked for a caption that cannot be written')
    local nohook=found('require','unsupported')
    assert(nohook and nohook.detail=='native captions' and nohook.expected,
        'a CS2 lane with no hook says so ONCE -- it does not suppress in silence')
    assert(unexpected()==0,'and the declaration covers it, so the gate stays satisfiable')
    frame_step()
    assert(labels[1].hidden==false,
        'the lane keeps its own labels rather than losing them to a latch that never lifts')
    drawn={}
    product.on_draw_world(api,graphics)
    assert(#drawn==2 and drawn[2].text:find('Rune platebody',1,true),
        'and the drawn overlay is what a player sees, unchanged')
    product.on_stop(api);porcelain.close()
    capabilities['script_callback:groundItemCaption']=true

    -- ------------------------------------------- a client with no prices.txt
    -- Optional: the cache's own OC_COST is the fallback, the absence is one
    -- finding, and it is never asked for a second time.
    assets['prices.txt']=nil
    findings={};absences={};unsupporteds={};asset_requests={};table_failed={}
    product.on_start(api)
    local missing=found('table','asset_missing')
    assert(missing and missing.detail=='prices.txt',
        'a price table that is not shipped is one finding naming it')
    frame_step();frame_step()
    assert(#asset_requests==1,'and a terminal miss is never re-requested')
    drawn={}
    product.on_draw_world(api,graphics)
    assert(drawn[2].text=='Rune platebody (EX: 39K gp) (HA: 23K gp)',
        "without the file the cache's own OC_COST prices every stack")
    product.on_stop(api);porcelain.close()

    host.core.log('ground behavior passed')
end}
