# Quest Helper port — contract and status

RuneLite's Quest Helper (`/Users/matthewevers/Documents/git_repos/quest-helper`,
694 Java files) running as a Lua plugin in this client. A FULL port: every
quest, diary, miniquest, skill/activity helper and player-made quest; the side
panel; every overlay (world, minimap, world map, widgets, top-left panel);
dialogue/option highlighting; item requirements over inventory, equipment and
bank; the bank's Quest Helper tab; hint arrows; chat messages; config.
Quests load ON DEMAND: nothing reads a quest module until it is needed.

This file is the contract every piece is written against. If reality forces a
change to a name below, change it HERE in the same edit, so the other side
reads the truth.

## Layout

| Path | What | Who writes it |
|---|---|---|
| `tools/questhelper_port/QhToLua.java`, `build.sh` | Java -> Lua transpiler (javac + Lombok attribution) | generated output only; edit the transpiler, never `gen/` |
| `script/plugins/quest_helper/gen/` | transpiler output: `fw/*` framework modules, `helpers/**` + `playerquests/*` lazy quest modules, `index.lua`, `enums.lua`, `inventory.tsv` | `build.sh` |
| `script/plugins/quest_helper/rt/j_*.lua` | Java runtime (object model, java.lang/util/stream/regex, Guava/Gson/...) | done |
| `script/plugins/quest_helper/rt/rl_*.lua` | the RuneLite API over the client's plugin API (`api.*`) | RuneLite-layer agents |
| `script/plugins/quest_helper/rt/awt.lua` | java.awt (Color, geometry, Graphics2D over `api.draw`/the draw callback) | rendering agent |
| `script/plugins/quest_helper/ui/*.lua` | the side panel (RuneLite's Swing `com.questhelper.panel.*`), hand-written on the plugin panel | panel agent |
| `script/plugins/quest_helper.lua` | the plugin entry: definition table, boot sequence, event fan-out | integration |
| `script/plugins/plugins.ini` `[plugin:quest-helper]` | `source=plugins/quest_helper.lua`, `enabled=0`, `budget=20000000`, `memory_mb=48` | integration |
| `tools/questhelper_port/harness/` | standalone tests under `/opt/homebrew/bin/lua` (5.5, same as the client) | everyone |

## How generated code calls out

Read `gen/inventory.tsv`: `kind  owner  lua-name  java  uses`. Every row whose
owner is not a `com.questhelper` class must be provided by `rt/`. Rules
(`tools/questhelper_port/README.md`):

- Classes are `J.def(key, {kind, super, ifaces, body = function(cls, super) ... end, clinit = ...})`
  in the global registry `C`; instance methods are `function cls.name(self, ...)`,
  statics `function cls.name(...)`.
- Overloaded names carry the erased signature: `getWidget__int_int`. A name is
  mangled exactly when the JDK/RuneLite TYPE declares (or inherits) more than one
  method of that name with different erased parameters. The inventory gives
  the exact spelling; match it.
- Constructors: define `cls.__init__<sig>(self, ...)`; `J` derives `__new__<sig>`.
  `class` rows with no member are class references (`C["..."]` / `J.classof`).
- RuneLite enums are already generated (`gen/enums.lua`, `J.ext_enum`) with
  their getters; do not redefine them.
- `@Inject` fields: each class's `__def.inject = {{field, typeKey}, ...}`
  (walk `__super` for inherited ones). `@Subscribe` methods:
  `__def.subscribe = {{method, eventKey, priority}, ...}`. Config:
  `QuestHelperConfig`'s `__def.config` lists every `@ConfigItem`.
- Lambdas are `J.fn(ifaceKey, samName, f)`; call a functional object with
  its SAM method (`r:run()`, `p:test(x)`).
- `null` is `nil`; arrays are `{n = len, [1..n]}`; strings are Lua strings
  carrying Java's String methods on their metatable.

## The plugin API the port adds (C side)

All of these are in `src/plugin/torirs_plugin_api.h` (C) AND
`src/plugin/torirs_plugin_lua.c` (Lua) AND `script/plugins/plugin_api.meta.lua`,
except `api.sandbox`, which is Lua-only. `src/plugin/test/plugin_lua_api_inventory_test.py`
pins all three together.

Done (2026-11-19): global `setmetatable` (refuses `__gc`), `api.sandbox.protect`,
`api.sandbox.module_request` / `module_load` / `module_unload` (live-sweep round:
a LOADED module's result leaves the module table and its slot goes back to
"none" -- the next `module_request` reads the file again into the SAME slot,
not counted again toward the 512 limit, and `module_load` runs it again; a
name that is not loaded raises), manifest `budget=`, 64 config items.
With `TORIRS_PLUGIN_LOG` set the host also reports, when a script is released
(quit, reload, disable), `plugin: '<id>' peak Lua heap K KB of CAP KB, largest
callback N instructions (<handler>) of BUDGET` (heap high-water mark including
garbage; instructions read back from the budget hook's own counter,
`torirs_lua_hookcount_left` in `plugin/lua_unity.c`), and once per script a
line when one callback first runs past half its budget.

To add — Lua spelling first; RuneLite meaning in brackets:

### Drawing (the draw callback's graphics object, `draw.*`, on_draw_world / on_draw_canvas) -- DONE 2026-11-19
All of these are on the DRAW OBJECT (C: `ToriRS_Graphics`), in that callback's pixels.
- `draw.polygon(points, fill_rgb, fill_alpha, outline_rgb?, outline_width?)` -> ok, reason -- `points` = `{x1, y1, x2, y2, ...}`, 1..256 points, floats truncated like Java's `(int)`. EVEN-ODD fill (java.awt.Polygon's; concave and self-crossing outlines are split into runs by the engine) when `fill_alpha` > 0; a border only when `outline_rgb` is given (`outline_width` default 1 = BasicStroke, 0 = none). Clipped to the callback's clip (the wash is cut to the box, the border's own edges are cut -- no frame along the cut). `false, 'budget'` when the frame's 512-item allotment is spent: a polygon costs n+2 items for a convex wash (more for a concave one) plus n for its border. Renders on soft3d, gl3/gl3zb, the GLES/WebGL lanes and (since this change) d3d9. [Graphics2D.fill/draw(Polygon)]
- `draw.text_width(text)` -> integer, `draw.text_height()` -> integer, for the font `draw.text` uses (p11); nil while that font is not resident (the same frames `draw.text` draws nothing). NOTE `draw.text` draws CENTRED on x with y as the BASELINE: left-aligned layout adds `text_width/2`. [FontMetrics]
- `draw.text(x, y, text, rgb, shadow?)` existing; keep markup-free. (There is no `shadow` argument in the C API today: draw the shadow as a second `text` at +1,+1.)
- `draw.minimap_project(fine_x, fine_z)` -> x, y or nil -- SCENE-LOCAL fine units (like `draw.project`). The client's own dot rule: offset from the local player (pushed out through a hull when aboard), 4 px/tile (this client has no minimap zoom), turned by the angle the map was blitted at; nil when the map is not drawn, past the dot ring (80 px = RuneLite's 6400), or outside the map box. [Perspective.localToMinimap]
- `draw.minimap_bounds()` -> {x, y, width, height, w, h, cx, cy, radius} or nil. box = the map widget box (the dot scissor), cx/cy = the player's pixel, radius = the dot ring (80), NOT the mask sprite's window. [the minimap widget + draw area]
- `draw.worldmap_project(tile_x, tile_z, level)` -> x, y of the tile's CENTRE, or nil when the world map is closed or the shown area does not cover the tile at that level. A covered tile scrolled out of the view STILL answers (as RuneLite's does), so clip lines to `worldmap_bounds`. [WorldMapOverlay mapWorldPointToGraphicsPoint]
- `draw.worldmap_bounds()` -> {x, y, width, height} or nil. [world map view rectangle]
- `draw.project(fine_x, fine_z, height?)` -> x, y or nil -- now ALSO on the draw object (it was only `api.draw.project`). [Perspective.localToCanvas]

### Cache / vars (`api.cache.*`) -- DONE 2026-11-19
Records are read RESIDENT-FIRST: a miss queues the cache load and answers `nil, 'pending'` (ask again on a later tick); once that load has ended without the record the answer is `nil, 'missing'`. Nothing blocks.
- `varc_int(id)` -> integer, -1 when never set (the client's own Varcs answer, what a script reads; NOT RuneLite's 0). `varc_str(id)` -> string ("" when never set), nil for an id outside 0..32767. [getVarcIntValue/getVarcStrValue]
- `set_varc_int(id, v)`, `set_varc_str(id, s)` -> ok, reason -- the store scripts write; `'invalid'` outside 0..32767 (TORIRS_VARC_ID_MAX), `'unsupported'` in a draw callback. [setVarcIntValue/setVarcStrValue]
- `enum_def(id)` -> {id, key_type, value_type, value_is_string, default, keys = {...}, values = {...}}, 'ready' | nil, 'pending'|'missing'. **Spelled `enum_def`, not `enum`** (`enum` is a C keyword and the inventory test pins Lua names to C member names). Types are one-char strings; `value_type` is only ever 's' or 'i' (the decoded record keeps no output type char, so an obj-valued enum reads 'i'). `default` is a string when value_is_string. [getEnum: getKeys/getIntVals/getStringVals/getIntValue/size]
- `npc_def(id)` -> {id, name, actions = {5}, size, combat_level, transform_varbit, transform_varp, impostor, transforms = {...}, models = {...}, recolor_from = {...}, recolor_to = {...}}, 'ready' | nil, state. `actions[i]` is `false` for an empty slot (RuneLite's null). `impostor` = the multinpc child the client's varps select now (-1 none), the world's own resolution. [getNpcDefinition, getTransformedComposition, getModels, getColorToReplace(With)]
- `loc_def(id)` -> {id, name, actions = {5}, size_x, size_z, transform_varbit, transform_varp, impostor, map_scene, map_icon, transforms = {...}}, 'ready' | nil, state. [getObjectDefinition, getImpostor, getImpostorIds]
- `sprite_image(id, frame?)` -> ImageRef, 'ready' | nil, 'pending'|'missing' -- frame (default 0) of a CACHE sprite as one of this plugin's images, composed under the name `cache-sprite-<id>-<frame>` (C: `ToriRS_CacheApi::sprite_image`; engine `ToriRS_PluginEngine::sprite_pixels`, bound through `UITreeSceneBridge_BindSprite` so a sprite not resident yet is 'pending' and fills in when it lands). Re-copies the pixels on every call: keep the ref once 'ready' (rl_client's sprite source does). Added 2026-11-19 (fix round). [SpriteManager.getSprite]
- existing: `varbit(id)`, `varp(id)`, `named_id(kind, name)`.

### Game (`api.game.*`) -- DONE 2026-11-19
- `chat_add(type, sender?, text)` -> ok, reason -- a CLIENT-SIDE chat line in the client's own message store, drawn by the cache's chatbox (markup kept). Never a packet. `type` = the wire chat type 0..127 (RuneLite's ChatMessageType value). It raises NO on_chat_message and never reaches the game-event recogniser (unlike RuneLite's addChatMessage, which fires ChatMessage): the RuneLite layer must fan a queued message out to its own subscribers itself if any need it. `'unsupported'` in a draw callback. [addChatMessage, ChatMessageManager.queue]
- `hint_arrow_tile(tile_x, tile_z, level?)`, `hint_arrow_npc(server_slot)`, `hint_arrow_clear()` -> ok, reason; `hint_arrow()` -> {kind = 'tile'|'npc'|'player', tile_x, tile_z (tile arrows), npc_slot, player_pid (each -1 unless that kind)} or nil. The HINT_ARROW packet's own arrow, so the server's and the plugin's replace each other. The client's arrow carries no level: `level` (default 0) is validated 0..3 and otherwise unused -- the arrow floats on the player's plane, as the packet's does. A tile arrow keeps the arrow's current height (RuneLite leaves it too). `hint_arrow_npc` is `'not_found'` unless a live npc holds the slot. Setters `'unsupported'` in a draw callback. [setHintArrow/clearHintArrow/getHintArrowNpc/getHintArrowPoint]
- `sound(id, loops?, delay?)` -> ok, reason -- the SYNTH_SOUND queue on this client, loops default 1 (once, 1..255), delay in client ticks. [playSoundEffect]
- `weight()` -> kg (UPDATE_RUNWEIGHT), `world_types()` -> the WORLDFLAGS mask (bit 0 members; this client is told no more than that, so only MEMBERS is ever set today), `tick()` -> server ticks executed (RS_ServerTick serial). [getWeight/getWorldType/getTickCount]
- existing: `skill(i)` (current_level = boosted, base_level = real), `inventory_size(inv)`, `inventory_slot(inv, slot)`, `item_info(obj)`, `item_image(obj, count, style)`.
- (fix round 2) `container_size(container_id)` -> slots (0 when the client holds no such container); `container_slot(container_id, slot)` -> obj id (-1 empty), count, or nil when it is not held / the slot is outside it. ANY container by the id on_inventory_changed reports (93, 516 a shop, 659 group storage, 660 its inventory, ...), from the client's InvManager; `inventory_size/slot` stay the way to name the client's own three without their numbers. Engine hooks `container_slot/container_size` (optional). [Client.getItemContainer(int), ItemContainerChanged.getItemContainer]
- (fix round 2) `world_entity_next(cursor?)` -> cursor, `{id, parent_view (0 = root), config_id, fine_x, fine_z (where the hull is now, ABSOLUTE fine), angle, level (parent's), deck_level, base_x, base_z, size_x, size_z (its own world's staging rectangle, absolute tiles)}` -- the live sailing boats (`struct ToriRS_WorldEntitySnapshot`); `world_entity_to_main(id, deck_fine_x, deck_fine_z)` -> absolute root fine x, z (a point of the boat's own world, fine from its staging base, through the hull's live deck box -- the descent that draws the deck; a boat carried by a boat goes through both), nil when the entity is gone. Engine hooks `world_entity_next/world_entity_to_main` (optional; absent = no boats). [WorldView.worldEntities, WorldEntity.getConfig/transformToMainWorld]

### Widgets (`widget:*` methods on api.widgets.get(component_id) results) -- DONE 2026-11-19
- `widget:detail()` -> {id, index, type, content_type, item_id, item_quantity, sprite_id, model_id, model_type, animation_id, text, text_color, font_id, name, actions = {...}, hidden, self_hidden, x, y, width, height, original_x, original_y, original_width, original_height, relative_x, relative_y, scroll_x, scroll_y, scroll_width, scroll_height, opacity, parent_id} or nil (widget gone). C: `ToriRS_WidgetApi::detail` + `struct ToriRS_WidgetDetail`. A READ: any widget, any context (paint included). RuneLite's identity: a cc_create child answers its PARENT's component id with its own `index`, a static component index -1. `type` is RuneLite's WidgetType (0 layer, 2 inventory, 3 rect, 4 text, 5 graphic, 6 model, 7 text inventory, 9 line), -1 for a client-built node (minimap, sidebar, a plugin's own control). `x, y, width, height` = the drawn CANVAS box; `relative_*` = laid-out position in the parent; `original_*` = the authored inputs. `opacity` = transparency (0 opaque). `hidden` = isHidden (self or an ancestor does not show), `self_hidden` = the node's own hide bit. `sprite_id` = the CACHE sprite shown (-1 for an obj icon / no art / non-cache art); `font_id` = cache font (-1 for a chrome font); `model_type` = WidgetModelType from what IF_SETNPCHEAD/PLAYERHEAD/OBJECT/MODEL bound (2 npc chathead with model_id = npc id, 3 local player, 4 item with model_id = obj, 1 model). `actions` = op labels up to the last set one, "" for an empty slot below it. `text` / `text_color` are the node's OWN (a set_native_text edit is drawn, not answered). [the Widget getters]
- `widget:dynamic_children()`, `widget:static_children()`, `widget:nested_children()` -> arrays of widgets ({} for none, nil when the widget is gone). Dynamic = cc_create children in CHILD-INDEX order (no holes: a sparse index list is compacted); static = the same interface's own components under this one; nested = roots of an interface mounted IN this component (a child whose group differs). A plugin's owned controls are in none. A walk of this node's children only. [getDynamicChildren/getStaticChildren/getNestedChildren]
- `widget:set_native_text(text)`, `widget:set_native_text_color(rgb)` -> ok, reason -- retained edits on a NATIVE text widget (UITree retained edit, owner-scoped exactly like set_hidden: newest owner wins per aspect, restating the winning value writes nothing, reset/teardown shows the game's text again; the node's own text and colour are never written). The GAME writing a new string/colour to the widget (IF_SETTEXT, cc_settext, cc_setcolour) or a cc_create rebuild of the child DROPS that edit -- RuneLite's order, where the script's write wins until the plugin writes again -- so a highlight is restated each tick, as Quest Helper does. The colour replaces the BASE colour: a cache hover/active colour still wins while it applies. Text <= 4096 bytes (`budget_exceeded`); an owned control or non-text widget is `native_blocked`; `wrong_context` in paint. [setText/setTextColor on game widgets: dialogue option highlighting]
- `widget:create_item(key)` + `widget:set_item(obj_id, quantity?)` -- owned item control (an owned GRAPHIC child carrying an obj; it shares create_image's key space). `quantity` defaults to 1; obj -1 clears. `'pending'` while the obj's model is not resident: the load is queued and the icon binds BY ITSELF when it lands (a bounded pump in the bridge, one test per frame when empty); the obj is on the control at once. [createChild(WidgetType.GRAPHIC) + setItemId] **On a NATIVE item cell** (a game RS_GRAPHIC / objbox: a bankmain slot) `set_item` is a retained RE-LAY (bank-tab round): owner-scoped like set_native_text, but WRITTEN THROUGH (`UITree_WidgetSetNativeObject`) so every reader of the cell -- the emit's icon and stack count, the minimenu's target name, `detail()` -- sees the plugin's obj; the game's value is kept beside the edit and `reset`/teardown shows the next owner's or the game's again. The GAME writing the cell itself (cc_setobject, IF_SETOBJECT, an icon landing for another obj) DROPS the edit, RuneLite's order. Any other native widget is `native_blocked`. The cell keeps its NATIVE ops (a native op cannot be taken away): see rl_widgets `menu_retarget` for how a click on a re-laid cell is handled.
- `widget:set_opacity(alpha)` on a NATIVE widget is the same write-through retained edit of its transparency (`UITree_WidgetSetNativeTransparency`; game `cc_settrans` drops it). [setOpacity: the bank tab's missing-item cells]
- `widget:set_native_hidden(hidden)` -> ok, reason -- a NATIVE widget's OWN hide, written through the same way (`UITree_WidgetSetNativeHidden`; game `cc_sethide`/IF_SETHIDE drops it): false SHOWS a node the game hid, which `set_hidden` (a presentation hide that only ever adds hiding) cannot. Owned controls: `native_blocked`. [setHidden(false) on a game widget the script hid: the bank tab's slots past the build's range]
- `widget:set_scroll_size(width, height)` -> ok, reason -- a NATIVE layer's scroll size, a plain write (as RuneLite's field write is): the game's next scroll-size write replaces it; nothing restores it on reset. [setScrollHeight: the bank tab's list is taller than the build made it]
- `widget:create_rect(key)` + `widget:set_rect(rgb, filled?)` -- an owned RECTANGLE control (keyed like create_image, sharing its keys; colour + fill, opacity through set_opacity). [createChild(WidgetType.RECTANGLE)]
- `widget:create_model(key)` + `widget:set_model(kind, id?)` / `widget:set_model_pose(x_angle, y_angle, z_angle, zoom?)` / `widget:set_model_anim(seq)` -- an owned MODEL control: kind `'npc'` (an npc's chathead, npc id), `'obj'` (an obj's inventory model), `'model'` (a cache model id), `'player'` (the local player's chathead, no id), `'none'`; angles are RuneLite's rotationX/Y/Z (2048ths), zoom its modelZoom (0 keeps). `'pending'` while not composited: the load is queued (the IF_SET* head task, `App_PluginModelScene`) and the control binds it by itself (the bridge's model pump). [createChild(WidgetType.MODEL) + setModelId/setModelType/setItemId/setRotation*/setModelZoom/setAnimationId: ChatBox / PlayerChatBox faces, QuestCompletedWidget's scroll, icon and ribbon]
- `widget:set_sprite(sprite_id)` -> ok, reason -- a cache sprite: on an owned image control (create_image) the control's art at its size; on a native sprite/graphic/compass a retained re-skin (the same art edit as set_image with size 0,0; reset drops it). A sprite still loading is bound and fills in when it lands; one the cache lacks is `'unavailable'`. [setSpriteId]
- `widget:listener(name)` -> `{script_id, arg1, ...}` or nil -- a NATIVE component's CS2 listener as RuneLite's Object[] (magic ScriptEvent ints kept as they are, string args as strings). Names in RuneLite's spelling: "onInvTransmit", "onKey", "onOp", "onTimer", "onVarTransmit", "onClick", "onHold", "onMouseOver", "onMouseLeave", "onMouseRepeat", "onRelease", "onDrag", "onDragComplete", "onScrollWheel", "onTargetEnter", "onTargetLeave", "onDialogAbort", "onLoad". An unknown name RAISES (C: INVALID_ARGUMENT); nil when nothing is armed -- ALWAYS for "onLoad" (this client runs a cache onload at the mount and keeps nothing). Read-only. [getOnInvTransmitListener for BankSearch.layoutBank, getOnKeyListener for the GE search box]
- existing: create_text/create_image/set_on_op/set_position/set_size/set_hidden/bounds/children/visible/text.

### Client scripts (`api.scripts.*`) -- DONE 2026-11-19
- `run(script_id, ...)` -> ints table, strings table (what the script left on the VM's stacks, bottom first: `ints[1]` is RuneLite's `getIntStack()[0]`) | nil, reason. Extra args are integers (int args) or strings (string args), in order; more of either than the script declares is `'invalid_argument'`, as is an id the cache has no script for. Also refused inside `on_script_pre` / `on_script_post`. Runs NOW on the client's own VM/host (a pooled VM, RS_CS2Host_Exec). Refused (`'wrong_context'`) in a draw callback, inside `on_script_callback`, during teardown, or while any script is executing. A synchronous run cannot wait: if the script or anything it reaches (gosub, enum, db row/table/index, struct, param, obj, npc, loc, sprite, font, model, world map...) is not resident the run is ABANDONED at that opcode, the load is queued, and the answer is `'pending'` -- call again next tick (QUEST_STATUS_GET reads db rows, so its first calls are pending). A load that ended without the record is answered as the lookup's miss on the next run, exactly as Task_CS2Run's resume does. `'native_blocked'` for a script that needs an interface mount (component pack bake, loc/npc-head model set, obj icon); `'failed'` for a script error; `'budget_exceeded'` past 64 ints / 16 strings of 511 bytes. RUNELITE_CALLBACK opcodes inside the run raise no plugin callback; long-stack results are not returned. Our cache's 4029 is `[proc,quest_status_get]` (int quest db row -> 0 in progress, 1 not started, 2 finished), the same id RuneLite's ScriptID.QUEST_STATUS_GET names. [client.runScript + getIntStack/getStringStack; QUEST_STATUS_GET (4029), UPDATE_SCROLLBAR]
- `watch(script_id, on)` -> true | false, reason (bank-tab round) -- raise `on_script_pre` / `on_script_post` (Events) for that clientscript id. At most 32 ids a plugin (`'budget_exceeded'`), `'invalid_argument'` outside 0..65535, restating is fine, a stopped plugin's watches go with it; the engine is told the UNION (`ToriRS_PluginEngine::script_watch` -> `CS2VM2_ScriptWatch`, counted per id). An unwatched id costs the VM one load of a global count at entry / gosub / return. [ScriptPreFired / ScriptPostFired subscriptions]

### Events (new callbacks in ToriRS_PluginCallbacks and the Lua handler list) -- DONE 2026-11-19
All three cost NOTHING while no RUNNING plugin has the handler: the host asks the engine for varp values, queues mounts and coalesces inventories only for an enabled, started subscriber (a disabled Quest Helper costs nothing), and the Lua runtime installs the C callback only for a script whose table defines the handler. The host copies a definition's callbacks at registration, so a hot reload that ADDS one of these handlers takes effect at the next client start.
- `on_var_changed(api, ev)` -- ev = {varp, value, old}; once per changed varp per SERVER TICK, before on_server_tick (diff of the int varp array against the values last reported). The first tick after a subscriber starts only records (no event for a change that predates it); a varp that changed and changed back within one tick raises nothing; long (64-bit) varps are not reported. VARBIT ids are not reported: the RuneLite layer posts VarbitChanged with varpId set and varbitId -1. [VarbitChanged]
- `on_interface(api, ev)` -- ev = {group, open = boolean}. From the engine's mount seam (`UITree::mount_listener`: every mount-table set/clear, the toplevel via IF_OPENTOP, every mount when the tree is cleared), never a scan. A sub re-opened into its slot is close + open (RuneLite's closeInterface + openInterface). Queued and delivered at the next frame start, before on_frame_start, in mount order (at most 128 per frame; past that the oldest are dropped, logged once). [WidgetLoaded / WidgetClosed]
- `on_inventory_changed(api, ev)` -- ev = {inv} (container id: 93 backpack, 94 worn, 95 bank). From the inventory store's change callback; coalesced per container per frame, delivered at the next frame start after that frame's on_interface events. [ItemContainerChanged]
- `on_menu_build` rows (and `on_menu_select`'s `row`) gain `option`, `target` (RuneLite's split of `text`: the option is what precedes the target's colour; the target keeps its markup with the client's `@yel@` tags spelt as `<col=ffff00>`; "" when the row names no target), `identifier`, `param0`, `param1`, `type` (C: `ToriRS_MenuRow::identifier/param0/param1/type`; option/target are Lua-side). `type` = RuneLite's MenuAction id (9..13 NPC ops, 1003 examine npc, 3..6/1001 loc ops, 1002 examine loc, 18..22 ground item ops, 1004, 44..48 player ops, 23 WALK, 1006 CANCEL, 57 CC_OP for an IF3 component op / 24 for IF1, 30 WIDGET_CONTINUE, 26 WIDGET_CLOSE, 28/29 toggle/select, 33..37 + 1005 item ops, 38 ITEM_USE, 1/7/14/16/31 use-on, 2/8/15/17/32/25 spell targets, 1500 RUNELITE for a plugin's own row), -1 where RuneLite has none. `identifier` = npc slot / player pid / loc or obj id / CC_OP op number (1-based) / the obj on an item row. `param0/param1` = the scene tile x/z of a loc, obj or walk row; for a widget row the dynamic child index (-1 static) or item slot, and the component id (a dynamic child named by its PARENT, as RuneLite does). on_menu_select's row now also carries `component_id`/`slot` (it used to leave them 0). [MenuEntryAdded / MenuOptionClicked]
- `on_loc_changed(api, ev)` -- ev = {tile_x, tile_z (absolute), level, shape, angle, loc_id (-1 a removal), old_loc_id (-1 none stood there), old_shape (-1 then), old_angle}: a ROOT-scene loc replaced, added or removed AFTER the scene was built (LOC_ADD_CHANGE, LOC_DEL, a zone reset's restore), raised where the loc lane lands it in the scenery (`app_world_spawn.c`, after `WorldBuilder_ApplyLocChange`), so the scenery already shows it. Queued and delivered at the next frame start after on_inventory_changed, at most 256 a frame (oldest dropped, logged once); nothing queued without a running subscriber. A scene build raises none (on_world_loaded). [GameObject/WallObject/DecorativeObject/GroundObject Despawned + Spawned] (fix round)
- `on_map_anim(api, ev)` -- ev = {tile_x, tile_z (absolute), level, spotanim, height, delay, cycle}: a MAP_ANIM graphic on a root-scene tile (`App_PluginMapAnim` from the packet's apply). Same queue rules, 128 a frame, after the loc changes. [GraphicsObjectCreated] (fix round)
- `on_item_search(api, ev) -> ids?` -- ev = {query, tradeable_only}: the GE item search (OC_FIND with the tradeable filter) offered to the plugins before the cache scan, SYNCHRONOUSLY from inside the running script (no scripts.run, no yield). Returning an array of obj ids (at most 256) answers the search with exactly those; nil lets the client scan. The first plugin that answers wins. C: `RS_CS2Host::item_search` -> `app_item_search` -> `PluginHost_ItemSearch`. [GrandExchangeSearched consumed + Client.setGeSearchResultIds] (fix round)
- `on_script_pre(api, ev)` / `on_script_post(api, ev)` (bank-tab round) -- ev = {script_id, invoked, ref, widget?}: a WATCHED clientscript (`scripts.watch`) is about to run / has returned, SYNCHRONOUSLY from inside the client VM (`CS2VM2_SetScriptHook` -> `app_script_hook` -> `PluginHost_ScriptHook`; only plugins watching that id hear it). `ev.ref` reads and writes through `api.scripts.counts/get_int/set_int/get_string/set_string` exactly like a script callback's ref, index 0 the TOP: at PRE the script's arguments (index 0 = its LAST int / string argument; for an `invoked` gosub callee they are still on the caller's stacks, for a top-level script in its locals -- a write changes what it receives), at POST what it returned (the values above the stack height its frame started from; a write changes what its caller sees). `invoked` is true for a gosub callee (RuneLite's null `getScriptEvent()`); `widget` is a top-level listener script's source (the VM's active component). PRE fires at a top-level script's first opcode (once, even across a yield) or when a gosub has pushed the callee; POST at its RETURN. An abandoned or failed script never posts. No `scripts.run` from either (`'wrong_context'`), no widget `invoke`. [ScriptPreFired / ScriptPostFired]
- NpcSnap and PlayerSnap carry `orientation` (drawn yaw 0..2047, RuneLite's units), `anim_id` (the action sequence, -1 none -- the engine's rule: 0 and 0xFFFF are none), `interacting` (-1, an npc server slot, or 32768 + a player pid, from the face-entity lock); PlayerSnap `gender`. [Actor.getOrientation/getAnimation/getInteracting, PlayerComposition.getGender] (fix round)
- existing: on_server_tick [GameTick], on_screen_changed [GameStateChanged], on_chat_message [ChatMessage], on_npc_spawn/despawn/retype, on_item_spawn/despawn, on_key, on_draw_*, on_ui_*.

### Panel (`api.panel.*`) -- fix round 2026-11-19
- `show()` -- open this plugin's registered page in the side panel at the NEXT frame start (never inside the asking callback); a page already up stays up (unlike a second rail click, it never collapses). Ignored without a registered page. [ClientToolbar.openPanel: autoOpenSidebar]

`TORIRS_API_V2_MODULE_RESERVED_SLOTS` is 16 (was 12): the cache module's thirteenth verb did not fit.

### Client-APIs round (2026-11-19) -- DONE
Every verb in `torirs_plugin_api.h` (C) AND `torirs_plugin_lua.c` (Lua) AND `plugin_api.meta.lua`; host in `torirs_plugin_host.c` (+ `torirs_plugin_runtime.inc`), engine hooks in `torirs_plugin_host.h` (all OPTIONAL: a client without one answers UNSUPPORTED / MISSING), bridge in `torirs_plugin_bridge.u.c`. Tests: `test-plugin-host` (`test_quest_helper_client_apis`), `test-plugin-lua` (`test_quest_helper_client_apis`). The cache module's reserved slots are down to 1 (`RESERVED_SLOTS - 15`), the client module's to 12.
- **Menu editing** (`api.menu.*`, only inside `on_menu_build`; C `ToriRS_MenuApi`): `set_order(rows)` -> ok, reason -- the build's WHOLE menu, bottom row first (RuneLite's `Menu.setMenuEntries`, `removeMenuEntry`, `createMenuEntry(idx)`'s position): each entry `{row = i}` (1-based into this build's `ev.rows`) or `{tag = t}` (this plugin's own `menu.add` tag of THIS build), `deprioritized = true` sinks it below every row listed without it (order kept within each half; `MenuEntry.setDeprioritized`). A payload row or own row left out is REMOVED. Rows the call cannot name keep their place apart from it: another plugin's and the entity holders' rows go BELOW the listed ones, a native row past the payload's 16 ABOVE them. Taken when the build ends (after every plugin and the entity holders, `plugin_menu_apply_plan`); the menu is then NOT re-sorted (`PluginHost_MenuBuild` returns true and `app_plugin_menu_build` skips `UIMinimenu_SortPriorityActions`), so the order stands as given. The last plugin to call it in a build wins. `'invalid'` for an index outside the payload, a tag this plugin did not add in this build, a row listed twice, more than 16 + 24 entries. C: `struct ToriRS_MenuRowRef {row (0-based, -1 = a tag), tag, deprioritized}`. `set_text(row, text)` -> ok, reason -- replace payload row `row`'s drawn text (option, a space, the target with the client's `@tag@` colours); the row keeps its action, so a click still does what the game row does (`MenuEntry.setOption/setTarget` on a native row: RuneliteObjectManager's replace-walk); `'budget'` past 127 bytes. `shown()` -> `{x, y, width, height}` or nil -- the right-click menu's box while it is up (`Client.isMenuOpen`, `Menu.getMenuX/Y/Width/Height`); a read, any callback. [Menu.setMenuEntries/removeMenuEntry, MenuEntry.setDeprioritized, isMenuOpen]
- **`api.cache.set_varbit(id, value)`** -> ok, reason -- a CLIENT-SIDE varbit write (RuneLite's `Client.setVarbit` / `setVarbitValue(getVarps(), ...)`): the same optimistic write a clientscript's own `%varbit = v` makes (`RS_CS2Host_ScriptWriteVarbit`: the bits land in the client's copy of the base varp and that varp's transmit listeners run), NEVER a packet; the server's next VARP for that varp replaces it. `'invalid'` for a varbit the client does not know, `'unsupported'` in a draw callback. [setVarbit: QuestBankTabInterface's BANK_CURRENTTAB reset, the player-made quests]
- **`api.cache.seq_def(id)`** -> `{id, frame_count, frame_lengths = {...}, duration, frame_step, skeletal}`, `'ready'` | nil, `'pending'`|`'missing'` -- `frame_lengths` in client cycles, `duration` their sum, `frame_step` the loop-back offset (-1 none), `skeletal` an Animaya seq (its frames each read 1). A seq not in the scene's animation registry queues `CreateTask_SequenceLoad` (`'pending'`); an id the cache has no record of is `'missing'` (the loader registers an empty animation for it). C: `struct ToriRS_SeqDef` (frame_lengths a caller's `ToriRS_DefList`). [Client.loadAnimation, Animation.getNumFrames/getFrameLengths/getDuration: RuneLiteObject animations]
- **`api.client.open_url(url)`** -> ok, reason -- the player's browser (`LinkBrowser.browse`): through the slot the server's URL_OPEN already uses (`host.client.pending_open_url` -> main.c `PlatformWindow_OpenUrl`: `SDL_OpenURL` on desktop and the browser lane's `window.open`, `ShellExecute` on win32, the Android activity's intent), at that frame's end. Only `http://` / `https://`, printable, at most 500 bytes (`'invalid'`); `'unsupported'` on a lane with no person to show it to (`App::plugin_open_url_ok`, set by main.c: false under `SDL_VIDEODRIVER=dummy|offscreen` and on a replay) and in a draw callback; `'conflict'` while an earlier url of the frame is still waiting. Logged `plugin: open_url <url>`.
- **`api.client.worldmap_jump(tile_x, tile_z, level?)`** -> ok, reason -- glide the open world map to an ABSOLUTE tile (`WorldMap.setWorldMapPositionTarget`; the map's own `RS_WorldMap_JumpToSourceCoord`, not the instant form). `'unsupported'` while the map is closed (and in a draw callback), `'not_found'` when the area shown does not cover the tile, `'invalid'` for a level outside 0..3.
- **`on_command(api, ev)`** -> `"consume"`? -- ev = `{name, args = {...}, text}`: a `"::"` line BEFORE it goes to the server as a client cheat -- typed in the chat line (`app_frame.c`), the chatbox script's `docheat` (`app_cs2_flush.c`, which is also the agent API's `chat`), `App_SendCommand` (harness) -- `name` its first word, `args` the rest split at spaces, `text` everything after `::`. Raised to the running plugins in event order until one CONSUMES it: then the server never sees it and no client cheat runs. C: `PluginHost_Command(host, text)` -> 1 consumed; `struct ToriRS_CommandEvent {text, name, args}` (args the raw rest). Costs one test while no running plugin has the handler. [CommandExecuted]
- **`api.sandbox.memory_limit()`** + manifest **`memory_mb=`**: above ("The entry's residency wants").

## The RuneLite layer (rt/rl_*.lua)

Split by file so agents never share one:

| File | Provides |
|---|---|
| `rl_core.lua` | EventBus (register/unregister/post; dispatch to `__def.subscribe` methods by event key and priority), the injector (`J.inject(obj)` fills `@Inject` fields from a singleton table; `Injector.injectMembers/getInstance/createChildInjector`; `Binder`), ClientThread (invoke/invokeLater/invokeAtTickEnd -> run now or queue to next frame), ConfigManager (getConfig(QuestHelperConfig) proxy reading `api.config` for declared items and falling back to the default methods; get/setConfiguration and RSProfile variants for undeclared keys, persisted via `api.assets.save` JSON), Plugin/PluginManager/ClientToolbar/NavigationButton, events classes (GameTick, VarbitChanged, ChatMessage, WidgetLoaded, ItemContainerChanged, NpcSpawned, MenuEntryAdded, ...), CommandExecuted, the clock (`J.now_ms`), logging (`J.log`) |
| `rl_client.lua` | `net.runelite.api.Client` (vars, varcs, skills, game state, tick, plane, world types, item containers, getWidget, runScript/getIntStack, menu, enums/defs, hint arrows, chat, sound, canvas sizes, camera), ItemContainer/Item, ItemComposition/NPCComposition/ObjectComposition/EnumComposition, QuestState lookups, ItemManager, SpriteManager, SkillIconManager, ChatMessageManager/ChatMessageBuilder/QueuedMessage, Text, QuantityFormatter, ColorUtil, JagexColors, ColorScheme |
| `rl_world.lua` | WorldPoint/WorldArea/LocalPoint/Point/Direction, WorldView/Scene/Tile, Actor/NPC/Player, TileObject/GameObject/WallObject/DecorativeObject/GroundObject, TileItem/ItemLayer, IndexedObjectSet, Perspective (canvas/minimap/tile poly), WorldEntity, instance templates (fromLocalInstance via `api.world.instance_zone`), RuneLiteObject/Model/ModelData (scene instances via `api.scene`) |
| `rl_widgets.lua` | `net.runelite.api.widgets.Widget` over `api.widgets` (read + every setter the plugin uses; owned children for createChild), JavaScriptCallback, ScriptEvent, WidgetType/position modes, ChatboxPanelManager/ChatboxInput, BankSearch, the GE search hooks. Exports `J.rl_widget(handle)` (what rl_client's getWidget calls) and `J.rlw`: `get(component_id)`, `get_child(group, child)`, `from_menu(component_id, slot)` (a CC_OP row's child -- MenuEntry.getWidget should use it), `script_event(fields)` (ScriptPreFired.getScriptEvent), and the entry's fan-in: `on_key(ev)` -> consumed (chatbox-panel KeyListeners + JavaScriptCallback onKey), `on_menu_select(component_id, slot, op)` -> consumed (JavaScriptCallback onOp/onClick put on a NATIVE widget), `on_interface(ev)`, `on_screen_changed(screen)`, `invalidate()` (after native scripts ran mid-frame), `on_stop()`; GE: `ge_searched(event_bus)` -> consumed, `{index, count, ids}` and `ge_set_index/ge_set_count/ge_set_ids` (Client.setGeSearchResult*); bank-tab round: `on_script_pre(script_id)` (drops this plugin's edits on the children of a container that script rebuilds), `menu_retarget(entry)` -> consumed, `owned_menu_target(option)` -> name. Owned kinds: TEXT -> text, GRAPHIC -> image (item when it carries an obj), RECTANGLE -> rect, MODEL -> model (an item model when it carries an obj, else by model type: 2 npc chathead, 3 local player, else a cache model); a native widget's setItemId/setItemQuantity/setOpacity/setHidden(false)/setScrollHeight reach the host's native verbs (set_item, set_opacity, set_native_hidden, set_scroll_size). Native dynamic children are read once a frame (invalidate() and every runScript drop the cache). The game's scroll size of a native layer is kept before the first setScrollHeight/Width and written back at `on_stop` (a plain write no reset undoes); BANKMAIN_FINISHBUILDING (505) writes its own, so a rebuild forgets the saved one. BankSearch's runs and rl_client's runScript retry a `'pending'` run (BankSearch: on the ClientThread, up to 120 frames; runScript: raises J.PendingMT so the asking task or dispatch runs again) |
| `awt.lua` + `rl_overlay.lua` | java.awt Color/Rectangle/Point/Polygon/Shape/geom/BasicStroke/Font/FontMetrics/BufferedImage/Graphics2D (a Graphics2D over the draw callback's `draw`), Overlay/OverlayManager/OverlayPanel/OverlayUtil/OverlayLayer/OverlayPosition/OverlayPriority, PanelComponent/LineComponent/TitleComponent, ModelOutlineRenderer, WorldMapPoint/WorldMapPointManager, TooltipManager, ImageUtil, ImageIcon |

### World shapes (rl_world.lua -> awt.lua / rl_overlay.lua)

Every `java.awt.Shape` rl_world hands out -- `Actor`/`TileObject`
`getConvexHull`, `getConvexHull2`, `getClickbox`, `getCanvasTilePoly`, and
`Perspective.getCanvasTilePoly*`/`getCanvasTileAreaPoly*`/`getClickbox` -- is
a plain **`java.awt.Polygon`** built with `C["java.awt.Polygon"].__new__()` +
`:addPoint(x, y)` from canvas projections taken when it is asked for, so
`getBounds`, `getBounds2D`, `contains__double_double`, `Area(Shape)` all
work in Lua. It ALSO carries one field, `poly.__world`, saying what it is in
the world so the renderer can draw it natively:

- `{kind = "hull", element_id = n}` -- the drawn model of an entity (npc,
  player, loc, ground item). Graphics2D `fill`/`draw` of it, and
  OverlayUtil.renderPolygon/renderHoverableArea, should call
  `draw.world_hull(element_id, rgb, alpha)` instead of filling the polygon.
  The polygon is the convex hull of the footprint corners at the ground and at
  `api.draw.element_height(element_id)` -- right for bounds and hover tests,
  coarser than the real mesh.
- `{kind = "tile", tile_x, tile_z, level, size_x, size_z}` -- ABSOLUTE
  south-west tile and footprint in tiles. A `fill` of it can be
  `draw.world_tile(tile_x + i, tile_z + j, level, fill, outline, alpha, width)`
  per tile; the polygon is the exact projected outline (at the actor's
  interpolated position for an Actor's tile poly, so it may sit between tiles
  while the true tile does not).
- no `__world` -- a plain polygon: `draw.polygon(points, ...)`.

`Area(Shape)` should keep the shapes it was built from (a list) so a fill of
an Area of hulls (QuestPerspective.addHull) still routes each one natively. A
Shape getter answers nil when the thing is not drawn (`element_id < 0`) or a
corner does not project, as RuneLite's do.

Heights: `Perspective.getTileHeight` answers 0 and every height in this port
is measured UP from the ground under the point (`api.draw.project`'s third
argument). `localToCanvas(client, x, y, z)` projects at `-z` above ground;
`localToCanvas(client, lp, plane, zOffset)` at `zOffset`. RuneLite code that
computes `getTileHeight(...) - offset` therefore lands where it should.

`QH.draw` is the draw callback's graphics object while `on_draw_world` /
`on_draw_canvas` runs (set by rl_overlay / the plugin entry around the overlay
pass, nil outside it). `Perspective.localToMinimap` uses
`QH.draw.minimap_project(fine_x, fine_z)` (scene-relative fine units, like
`api.draw.project`) and is nil outside a draw pass; canvas projection uses
`api.draw.project`, which is valid outside one.

### rl_world.lua factories (what rl_client / rl_core call)

Fixed names; the `client` argument RuneLite passes to statics is accepted and
ignored (there is one top-level world view):

- `C["net.runelite.api.WorldView"].__top()` -> the top-level WorldView
  (id -1); `__byId(id)` -> it for -1, nil for anything else. Client
  `getTopLevelWorldView()`, `getWorldView(id)`, `getScene()` (=
  `__top():getScene()`), `getPlane()` (= `__top():getPlane()`) delegate here.
- `C["net.runelite.api.Player"].__local()` -> the local Player (one object for
  the session) or nil without a world. Client `getLocalPlayer()`.
- `C["net.runelite.api.NPC"].__bySlot(slot)` -> the one NPC object for a
  server slot (identity stable while it lives, which NpcStep's lists need);
  `__fromSnap(snap)` registers/refreshes from an `on_npc_spawn`/retype event's
  NpcSnap and returns it; `__forget(slot)` after posting NpcDespawned.
- `C["net.runelite.api.Tile"].__at(world_x, world_y, plane)` -> the scene
  Tile (same object for the same tile while anyone holds it -- a weak cache --
  until the scene moves) or nil outside the scene. `Scene.getTiles()` holds
  nil for a tile with nothing on it; its index is built per PLANE per cycle
  and keeps each loc as the scenery cursor before it (its snapshot is fetched
  again only for a tile whose objects are asked for; ground items are read
  apart from locs), so a full scene costs a few MB, not one table per loc.
  The top-level WorldView's `worldViews()` is itself (ObjectStep.loadObjects
  and NpcStep walk it to reach the scene).
- `C["net.runelite.api.TileItem"].__fromSnap(item_snap)` -> TileItem (value
  equality: id, quantity, tile) for ItemSpawned/ItemDespawned.
- `C["net.runelite.api.TileObject"].__fromSnap(scenery_snap)` -> a
  GameObject / WallObject / DecorativeObject / GroundObject by `shape` (value
  equality: class, id, tile, level, shape), for the *Object(De)Spawned events.
- `C["net.runelite.api.TileObject"].__invalidate()` -- drop the per-cycle scene
  index (on a scene load / GameStateChanged LOADING).
- `C["net.runelite.api.RuneLiteObject"].__new(client)` -- the constructor
  (Client `createRuneLiteObject()`); `RuneLiteObject.__destroyAll()` frees
  every scene instance (plugin shutdown).
- `C["net.runelite.api.ModelData"].__load(id)` (Client `loadModelData`),
  `C["net.runelite.api.ModelData"].__merge(arr)` (Client
  `mergeModels__ModelDataArr`, returns a ModelData), and
  `C["net.runelite.api.Animation"].__load(id)` (Client `loadAnimation`).
- The same, as the hooks rl_client calls: `J.rl_world_view()` (= `__top()`),
  `J.rl_local_player()`, `J.rl_create_runelite_object()`,
  `J.rl_load_model_data(id)`, `J.rl_merge_models(arr)`,
  `J.rl_load_animation(id)`.

rl_world needs from rl_client: `C["net.runelite.api.Client"].npc_composition(id)`
-> NPCComposition or nil (for `NPC.getComposition` = the base/shell id and
`getTransformedComposition` = the drawn id).

### Sailing (rl_world.lua, fix round 2)

Boats come from `api.game.world_entity_next` (once per frame). Everything
aboard is addressed in the boat's OWN world -- a rider's true tile is in the
staging rectangle -- exactly as RuneLite's boat WorldView addresses it;
LocalPoints of actors stay the ROOT's (where they are drawn).

- `WorldView.__top():worldEntities()` -> an IndexedObjectSet of
  `WorldEntity` keyed by id (= its world view id); `worldViews()` -> the top
  level plus every boat's world. `WorldView.__byId(id)` / `Client.getWorldView(id)`
  answer a boat's world (`WorldView$Boat`: `isTopLevel()` false, `getId`,
  `getBaseX/Y` and `getSizeX/Y` its staging rectangle, `getPlane` its deck
  level, `npcs()`/`players()` empty -- everyone is in the top level's sets --
  and a Scene with no tiles).
- `WorldEntity`: `getConfig():getId()`, `getOrientation()`,
  `getLocalLocation()` (in its parent), `getWorldView()`,
  `transformToMainWorld(lp)` (a LocalPoint of its own world through
  `api.game.world_entity_to_main`; any other point is returned as it is).
- `Actor.getWorldView()` is the boat's world when the actor's true tile is in
  a boat's staging rectangle (RuneLite's), so `QuestPerspective.
  getWorldPointConsideringWorldView` and `Zone` carry an aboard point to the
  main world; the top level's `getPlane()` aboard is the level the boat
  floats on, not the deck's.
- Perspective (`localToCanvas`, `localToMinimap`, `getCanvasTileAreaPoly`)
  carries a boat-world LocalPoint to the main world first.
- NOT modelled: a boat's own scene (its deck locs reach a plugin through the
  top level's scene at their staging tiles), so `QuestPerspective.
  getAreaFromWorldView` finds nothing and WorldEntityStep / BoardShipStep draw
  their plain arrow at the defined point instead of outlining the hull; NpcStep
  with `allowMultipleHighlights` lists top-level npcs twice while the player is
  aboard (its third loop walks the top level again); nested boats only through
  `world_entity_to_main`.

### rl_world.lua wants from the C side (read as absent until they exist)

- DONE (fix round): NpcSnap/PlayerSnap `orientation`, `anim_id`,
  `interacting`, PlayerSnap `gender` (see Events above).
- DONE (client-APIs round): `api.cache.seq_def(id)` -> `{frame_count,
  frame_lengths = {...}, duration, frame_step, skeletal}`, state (above).
  `Animation.__load` reads it; a `'pending'` seq reads 0 frames and every
  getter asks again until it is in. [Animation.getNumFrames, RuneLiteObject frames]

### The entry's residency wants from the C side (all present since the live-sweep round)

- `api.sandbox.module_unload(name)` -> true (DONE, live-sweep round) -- forget a LOADED module: drop
  its result from the script's module table and return its slot to "never
  requested" (or reuse it), so the next `module_request(name)` reads the file
  again ('pending' -> 'ready') and `module_load(name)` compiles and RUNS the
  chunk again. Today `module_load` runs a module once per script lifetime and
  frees its source, so nothing can be read twice. A slot reused by the same
  name must not count again toward the 512-module limit (the pass loads ~250
  modules and may reload some). Raising for a name never loaded is fine (the
  entry only unloads what it loaded). Without it the residency pass keeps
  every module it loads and stops at the heap ceiling (the entry's
  "Residency"); the harness mocks it (`test_residency.lua`, `test_entry.lua`).
- `api.sandbox.memory_limit()` -> bytes -- this script's heap cap: 16 MB
  (16777216), or the manifest's `memory_mb=` (client-APIs round, below). The
  entry places its harvest ceiling (cap - 8 MB), eviction line (cap - 6 MB)
  and collector pause by it; 16 MB is assumed without it.
- **Manifest `memory_mb=`** (client-APIs round; contract fixed first, C in
  `src/plugin/task_plugin_io.c` + `torirs_plugin_lua.c`): a `[plugin:<id>]`
  key beside `budget=`, parsed the same way (`strtol`, base 10). The script's
  Lua heap cap in MiB: absent or 0 = the default 16; values above the host
  ceiling 64 are clamped to 64 (logged once at load, like a budget past its
  maximum); a negative value is the default. The cap is what an allocation
  past it fails against (Lua's "not enough memory" inside a callback) and
  what the between-callback check collects against; the host's internal
  reserve stays on top of it. `api.sandbox.memory_limit()` answers it in
  bytes (`memory_mb * 1048576`). Quest Helper's `[plugin:quest-helper]` sets
  `memory_mb=48`.

### Passes, shapes and images (awt.lua / rl_overlay.lua <-> the other rt files and the entry)

- **Passes.** The entry calls `QH.render("world", draw)` from on_draw_world and
  `QH.render("canvas", draw)` from on_draw_canvas. `QH.render` sets `QH.draw = draw`
  for the duration and clears it after; rl_world's Perspective (project, minimap,
  world map) reads `QH.draw`. World pass: overlays on ABOVE_SCENE / UNDER_WIDGETS
  whose position is DYNAMIC or DETACHED. Canvas pass: every other overlay
  (ABOVE_WIDGETS, ALWAYS_ON_TOP, and a positioned overlay on any layer -- the
  TOP_LEFT quest panel is UNDER_WIDGETS), then the world map points (each
  remembers the box it was drawn in, `qh_wmp.box`; the one under the mouse is
  the hovered point: its `getTooltip()` joins the tooltips, and a jump-on-click
  one offers "Focus on" through `QH.worldmap_menu()`, client-APIs round), then the
  TooltipManager's tooltips. MANUAL is never drawn. Order inside a pass is
  RuneLite's (layer, then position, then priority). Both passes are canvas pixels.
  An overlay whose render throws a Java exception is logged once and skipped
  (RuneLite's safeRender); a budget error or a pending module propagates.
  `QH.render_reset()` forgets every overlay, map point and tooltip (teardown).
- **World shapes** are rl_world's `java.awt.Polygon`s with `poly.__world` (section
  "World shapes" above). awt routes them: `{kind = "hull"}` -> `fill` is
  `draw.world_hull(element_id, rgb, alpha)`, `draw` is `draw.world_hull(element_id, rgb, 0)`;
  `{kind = "tile"}` -> `fill` is one `draw.world_tile` wash per footprint tile, `draw` is the
  `world_tile` border for a 1x1 footprint and the polygon's own outline for a larger one;
  `OverlayUtil.renderPolygon`/`renderHoverableArea` make one call with wash and border.
  A `java.awt.geom.Area` keeps the shapes it was built from and draws each. Transform and
  clip do not apply to a world shape (the engine draws it in the scene).
- **Model outline**: `ModelOutlineRenderer.drawOutline__*` draws `draw.world_hull(id, rgb, 0, "mesh")`
  for the element of `actor:getConvexHull()` / `tileObject:getClickbox()` (`__world.element_id`;
  nothing when the getter answers nil) and of `runeLiteObject:__element_id()` (nil when not drawn).
- **Images** (`java.awt.image.BufferedImage`): `BufferedImage.qh_of_ref(ref, w?, h?)` wraps a live
  ImageRef; `BufferedImage.qh_of_source(fn, w?, h?)` asks `fn()` -> ref or nil (pending) at
  draw time (item images, sprites) -- rl_client's equivalent spelling, a
  `new BufferedImage(w, h, 2)` carrying `img.host_image = fn` (fn() -> ref?, state), is read
  the same way; a plain `new BufferedImage(w, h, type)` is pixels that
  `getGraphics()` composes into (drawImage, fillRect); `ImageUtil.loadImageResource(cls, "/a/b.png")`
  is the plugin asset `a_b.png` (leading `/` dropped, `/` -> `_`) in
  `script/plugins/assets/quest-helper/`. Readers: `img:qh_ref()` -> ref or nil,
  `img:qh_pixels()` -> row-major ARGB array (1-based) or nil while pending. Derived images
  (fillImage, resizeImage, scaled drawImage, composites) are composed once with
  `api.assets.image_compose` and kept in a bounded cache.
- **Text**: `draw.text` centres on x with y the baseline, so awt draws `drawString(s, x, y)`
  as `draw.text(x + text_width(s) // 2, y, s, rgb)`, shadowed by the components the way
  RuneLite's TextComponent does. There is one font: a Font changes nothing drawn.
  FontMetrics measure with `QH.draw` (aborting outside a pass) and read 0 while
  `text_width`/`text_height` answer nil (font not resident; nothing is drawn then).
- **Colour**: `draw.*` takes rgb `0xRRGGBB` plus a separate alpha; `Color:getRGB()` is Java's signed ARGB.
- **Mouse**: `api.input.pointer()`.

### rl_core.lua: what the entry and the other rt files call

rl_core also owns `net.runelite.client.callback.Hooks` (renderable draw
listeners; `hooks:should_draw(renderable, ui)` for rl_world),
`javax.swing.SwingUtilities` (invokeLater -> the next frame), `KeyListener`,
`RuneLite.getInjector()` and the `Config` interface. The entry drives it
through `QH.core` (set when the file loads):

- `QH.core.boot()` -> the root injector, idempotent. It binds EventBus,
  ClientThread, ConfigManager, ClientToolbar, Hooks, PluginManager and Gson;
  the entry binds the rest it owns: `root:bind("net.runelite.api.Client",
  client)`, `root:bind_named("developerMode", false)`. Any other type an
  `@Inject` field or `inject_ctor` names is built just in time: a
  `com.questhelper` class per injection point unless it is `@Singleton`
  (the def's `singleton = true`, emitted by the transpiler since 2026-11-19;
  `QH.core.QH_SINGLETONS` names the same 17 as a check), any runtime (rt) class once per tree from its no-arg
  `__init` -- so an rt service class (ItemManager, OverlayManager, ...) needs
  a no-arg `__init`, or the entry binds an instance. Each injector memoises
  what answers the same every time (a binding, a Config, a singleton) until
  any binding or `@Named` value changes, keeps a class's flattened `@Inject`
  list, and skips `injectMembers` of an object it already filled under the
  same bindings (QuestHelper.instantiateSteps re-injects an OwnerStep's
  children with every owner: Desert Treasure II's startUp 14.3M -> 4.1M
  instructions). The one difference from Guice: that repeat does not build a
  NON-singleton field (a step's VisibilityHelper) a second time. A Config interface
  resolves to `ConfigManager:getConfig` (RuneLite's `@Provides getConfig`).
- `QH.core.config_step()` -> `'pending'` | `'ready'`: reads the plugin asset
  `qh_config.json` (every non-declared key, RS-profile keys included, as
  RuneLite's whole keys `group[.profile].key` -> string). Call it each frame
  until `'ready'`, THEN `QH.core.start_plugin(key, "Quest Helper")`
  (child injector binding the plugin, members injected, `startUp()`, then
  registered on the EventBus). `QH.core.stop_plugin()` on on_stop
  (unregister, `shutDown()`, flush). A file that does not parse is never
  overwritten.
- `QH.core.frame()` once a frame (ClientThread invokeLater queue, Swing queue,
  debounced save); `QH.core.tick_end()` after posting GameTick
  (invokeAtTickEnd queue).
- Events: `QH.core.post(key, fields)` builds `QH.core.event(key, fields)` (a
  table of the event's Java field names: `{varbitId = .., value = ..}`,
  `{menuEntry = e}`; VarbitChanged's varpId/varbitId default -1) and posts it.
  `QH.core.command_event("::cmd a b")` -> CommandExecuted. A subscriber that
  hit a module still loading (J.PendingMT) does not stop the rest; `post`
  re-raises that error once every subscriber ran. A task that hit one stays
  queued and runs again next frame.
- Config: the definition's `config = {...}` is
  `tools/questhelper_port/gen_config_decl.lua`'s output (static: the
  definition is parsed before any module can load); the runtime decides where
  each keyName is stored with the same function, `QH.core.config_decl(iface)`.
  Client keys are the keyName in snake_case (`showOverlayPanel` ->
  `show_overlay_panel`); colours are `#RRGGBB` (alpha from the Java default);
  enums store the constant name; RuneLite-hidden items have no label; an enum
  whose names do not fit one 95-byte `choices` (filterListBy,
  questDifficulty) lives in the file. The entry forwards
  `on_config_changed(api, key)` to `QH.core.config_changed(key)` (posts
  ConfigChanged with the keyName, old and new strings; our own sets are not
  posted twice).
- RS profile: `QH.core.update_rs_profile()` on login/logout/world change
  (on_screen_changed and the first ticks): the key is
  `rsprofile.<display name lowercased, [^a-z0-9-] -> _>` plus
  `.<profile type>` off STANDARD; nil logged out. Posts RuneScapeProfileChanged.
- `J.inject(obj)` = the root injector's injectMembers; `J.log(s)` ->
  `api.core.log`; `J.now_ms()` -> epoch ms anchored once on
  `api.client.datestamp()` (local time) and advanced by `api.core.frame_ms()`.

### rl_client.lua: what the entry and the other rt files call (`J.rl_client`)

- `J.rl_client.client()` -> THE Client (`C["net.runelite.api.Client"].__new()`
  answers the same object); the entry binds it: `root:bind("net.runelite.api.Client", J.rl_client.client())`.
  ItemManager, SpriteManager, SkillIconManager, ChatMessageManager have a
  no-arg `__init` (built just in time).
- Menu, from the entry's on_menu_build: `local es = J.rl_client.menu_begin(ev)`
  (MenuEntry[] over `ev.rows`, bottom row first, as RuneLite's), post one
  `MenuEntryAdded{menuEntry = es[i]}` per row, then `J.rl_client.menu_end()`
  -- it adds every `menu:createMenuEntry` row with `api.menu.add(option .. " " .. target, tag)`
  (target's `<col=rrggbb>` turned into the `@tag@` of that colour, other tags
  dropped). Both passes, hover included: a row must exist in every build. From
  on_menu_select: `local e = J.rl_client.menu_select(ev)` (the created entry for
  an owned tag, else a MenuEntry over `ev.row`), post `MenuOptionClicked{menuEntry = e}`,
  then `J.rl_client.menu_click(e)` runs its `onClick` consumer (a no-op for
  a native row; return "consume" from on_menu_select for an owned one).
  A row's fields: `option`/`target`/`identifier`/`param0`/`param1`/`type` when
  the host gives them, else split from `text` at the first ` @` (`@tag@` ->
  `<col=>`), identifier = npc slot / player pid / target id, param0 = `slot`,
  param1 = `component_id`, type = `MenuAction` whose `getId()` is `type`
  (else `action`).
- Script callbacks: `J.rl_client.script_enter(ref)` before posting
  ScriptCallbackEvent / ScriptPreFired / ScriptPostFired for an
  `on_script_callback`, `script_leave()` after: in between `getIntStack()` /
  `getStringStack()` are the running script's stacks over `api.scripts.get_int/set_int`
  (Lua index k = Java k-1; host index 0 = the top, Java size-1). Outside one they
  are the last `runScript`'s results; a refused `api.scripts.run` throws
  IllegalStateException with the host's reason.
- `J.rl_client.inventory_changed(inv)` from on_inventory_changed, before posting
  ItemContainerChanged: containers are otherwise read once per `api.core.frame_ms()`.
  A container is RuneLite's id (93 INV, 94 WORN, 95 BANK, 516 a shop, 659 group
  storage, 660 its inventory, ... -- what the event reports). 93/94/95 are read
  through `api.game.inventory_size/inventory_slot` (a `ToriRS_InventoryKind`:
  0 backpack, 1 worn, 2 bank); every other id through `api.game.container_size/
  container_slot`, which take the id itself. A container the client has never
  been sent (size 0) reads as absent (`getItemContainer` null, as RuneLite's).
  `J.rl_client.changed_container(inv)` is the event's container and is NEVER nil:
  the client raised the event because it wrote that container, so one with no
  slots is an empty ItemContainer (RuneLite never posts the event without one;
  QuestHelperPlugin.onItemContainerChanged reads `getItemContainer().getItems()`
  first).
- `J.rl_client.watched_varbits` -- varbit id -> last value: every varbit
  `getVarbitValue` was asked for. The entry re-reads them to post VarbitChanged
  with a varbitId (below).
- `getNpcDefinition` / `getObjectDefinition` / `getEnum` RAISE J.PendingMT
  (`pending = "cache:npc:<id>"`, ...) while the cache is still loading the
  record (RuneLite's getters never answer "not yet"; null would read as "no
  such npc"): a ClientThread task or a one-off dispatch runs again later.
  `npc_composition` (rl_world's) keeps the nil answer.
- Skill: `Skill.ordinal()` is the client's stat index (Attack 0 .. Sailing 23).
  GameState: `api.core.screen()` numbers ARE GameState's (0, 10, 20, 30); in game
  with no `scene_origin` reads LOADING.
- Camera: no plugin API reads it. `getCameraYawTarget`/`getMinimapZoom` are
  derived from two `QH.draw.minimap_project` calls (the player's tile and four
  east), so they are right during a draw pass and 0 / 4.0 before the first;
  `getCameraX/Y` answer the player's scene fine position.
- Images: item icons are `BufferedImage.qh_of_source` over `api.game.item_image(id, qty, 0)`
  (36x32); `getItemOutline` a `qh_derived` of the icon's pixels; sprites a
  source over `api.cache.sprite_image(id, frame)` (below; draws nothing until it exists);
  `SkillIconManager.getSkillImage` the plugin asset `skill_icons_<skill>.png` /
  `skill_icons_small_<skill>.png` (RuneLite's own resources
  `runelite-client/src/main/resources/skill_icons*/`, to be shipped).

rl_client reads, when the host gives them (absent reads as RuneLite's default):
`item_info` `tradeable` (true), `members`, `cert_template`, `placeholder_link`,
`placeholder_template` (-1), `params = {[id] = v}` (-1), `actions`; `npc_def`
`models`, `recolor_from`, `recolor_to` (nil: Cheerer's Wise Old Man model needs
them), `params`; `loc_def` `map_icon`, `params`; `hint_arrow()`'s `npc_slot`
(-1 for a tile arrow); `draw.worldmap_bounds()` `center_x`, `center_z`, `zoom`
(pixels per tile; derived from two `worldmap_project` calls of the player's
tile when absent). `api.cache.sprite_image(id, frame)` exists since the fix
round (sprites draw). Every varc `getVarcIntValue` reads is in
`J.rl_client.watched_varcs` (the entry posts VarClientIntChanged for them).
Client-APIs round: `Client.setVarbit` / `setVarbitValue` -> `api.cache.set_varbit`
(a refusal is logged once); `isMenuOpen` / `Menu.getMenuX/Y/Width/Height` ->
`api.menu.shown()` (0 while no menu is up); `WorldMap.setWorldMapPositionTarget`
-> `api.client.worldmap_jump`; `net.runelite.client.util.LinkBrowser.browse(url)`
-> `api.client.open_url` (a refusal is logged: `quest-helper: could not open
<url>: <reason>`). The menu: `menu_begin` stamps each native entry with its
payload row and what it said; `Menu.setMenuEntries` (replaces the build's list),
`removeMenuEntry` and `createMenuEntry(idx)` edit `getMenuEntries`; `menu_end`
adds the created rows (`api.menu.add`), then -- when anything moved (a created
row, setMenuEntries/removeMenuEntry, a deprioritized entry) -- hands the final
list to `api.menu.set_order` (so a created row sits where RuneLite puts it:
QuestMenuHandler's "Start Quest Helper" just below the top row, a fake npc's
`createMenuEntry(-1)` on top), and `api.menu.set_text` for a native entry whose
option/target a subscriber rewrote. A build nobody edits sends no order (the
client sorts as before). A native entry given an `onClick` is remembered by its
numbers (type, identifier, param0, param1) for that build, so `menu_select` of
the native row returns THAT entry and `menu_click` runs its consumer, and the
game's own action still follows. `menuAction` runs CC_OP (`api.cache.invoke`),
does nothing for a RUNELITE* kind (no client action behind it, as RuneLite),
and logs any other kind once (no helper uses one: ReplacedNpc's copied npc
options would, but nothing calls `createReplacedNpc`).

Every file is tested by `tools/questhelper_port/harness/` against a mock `api`
(`harness/mock_api.lua`, one section per area, each agent owns its section's
functions -- add, do not rewrite others').

## The panel (ui/)

RuneLite's `com.questhelper.panel` Swing classes are not transpiled
(`grep '^manual' gen/inventory.tsv` lists what the framework calls on them:
`QuestHelperPanel` and `JGenerator`). `ui/` implements those classes with
the same keys and method names, on the plugin panel (`api.panel`,
`porcelain.panel`): quest search + filters + ordering, the quest list with
state colours, the selected quest's overview (requirements met/unmet,
recommended, items with icons, rewards, sections with steps, current step
highlighted, collapsible sections, manual skip), assist level, settings.

One file, `ui/panel.lua`, loaded after `rt/` (it only `J.def`s, so before or
after `gen/fw` is fine). It defines `com.questhelper.panel.QuestHelperPanel`
(`__new(plugin, questManager, configManager)` -- QuestHelperPlugin.startUp
builds it -- and every `manual` method in the inventory, plus the field
`questActive` QuestManager reads) and `com.questhelper.panel.JGenerator`
(`makeJTextArea__`, which Fonts' clinit asks for a font).

The plugin entry wires the page to `QH.ui`:

| Entry callback | Calls |
|---|---|
| `on_start` | `api.porcelain.open()`, `api.porcelain.panel(QH.ui.descriptor)` (`{width = 320, faces = "page"}`; add `icon_asset`), and from the entry's ONE `api.porcelain.describe` function, `QH.ui.describe(d)` |
| `on_ui_build(api, panel, view)` | `QH.ui.build(api, panel, view)` -- declares the search field (a raw `TORIRS_PANEL_INPUT` node, id `qh_search`; Porcelain has no input row) and then calls `api.porcelain.panel_build(view)` |
| `on_ui_action(api, ev)` | `QH.ui.action(api, ev)` -- `api.porcelain.panel_action(ev)`, then the search field's `"text"` action; returns true when it was the panel's |
| `on_ui_draw(api, node, draw)` | `QH.ui.draw(api, node, draw)` -- `api.porcelain.panel_draw(node)` |
| `on_frame_start(api)` | `QH.ui.frame(api)` -- re-lays the page out when paint learned a font measure, or an item icon landed (paint may not schedule work) |

And the entry provides `QH.load_quest(questHelperQuest, callback)`: load the
quest's module (`api.sandbox.module_request`/`module_load`), then call
`callback(true)`, or `callback(false, reason)`. It may call back
synchronously. The panel calls it when the player opens a quest (or a
required quest) whose helper is still a `J.lazy_new` proxy
(`not J.lazy_forced(helper)`), and shows "Loading <name>..." meanwhile. The
quest list itself never loads a module: names, keywords and section filters
are read from the `QuestHelperQuest` enum (a section filter is tested on a
stand-in whose `getQuest()` is the enum).

Links (client-APIs round): the quest list opens with a `qh_web` grid row of
QuestHelperPanel's Discord / GitHub / Patreon buttons, and the overview's
"RuneScape Wiki: <url>" line is clickable (QuestOverviewPanel's wiki button);
each calls `LinkBrowser.browse` -> `api.client.open_url`.

The page: at most 48 controls (Porcelain rows + the search field), a row
string over 191 bytes is DROPPED by the layer, and the reconciler compares
(key, kind) only. So the long content -- the quest list, the overview's
requirement groups, rewards, notes, the sections and steps -- is painted into
custom wells (`qh_l1..`, `qh_o1..`; each at most 2000 px, at least 48) as
lines of text with item icons (`api.game.item_image`), and a click is mapped
back to a line by its y (`hit_key` = that mapping's serial, `paint_key` = what
it draws). A well's one callback serves paint, `on_action` and `on_menu` (the
binding keeps one per row). Native rows carry the controls: the
filter/difficulty/ordering selects, skill and region filter toggles,
"Assist level", "Close helper", the quest's HelperConfig selects, the assist
page. The well count never shrinks while typing or collapsing, so those are
setters, not rebuilds.

The wells measure text with `draw.text_width(text)` / `draw.text_height()` on
the PANEL WELL's draw object too (the same `ToriRS_Graphics` the canvas
passes get), learned in paint and applied at the next frame; without them a
6 px / 12 px guess stands.

Tested by `tools/questhelper_port/harness/test_panel.lua` (every helper that inits under the stubs, 271 of 272:
overviews built, ticked through the real QuestManager.handleSelectedQuest,
and painted under the row limits).

## The plugin entry (`script/plugins/quest_helper.lua`) -- DONE 2026-11-19

- **Definition**: id `quest-helper`, title `Quest Helper`. Its `config = {...}`
  is spliced between `BEGIN/END GENERATED CONFIG` by `build.sh`
  (`gen_config_decl.lua` -> `splice_config.lua`), so it is always what
  `QH.core.config_decl` decides; 37 items. Items with nothing behind them in
  this client are declared WITHOUT a label -- stored at their default, never
  offered (`QH.core.CONFIG_NOT_OFFERED`): `use_shortest_path` (RuneLite's
  Shortest Path plugin does not exist here, so activateShortestPath's
  PluginMessage reaches no one) and `dev_show_overlay_on_launch` (read only in
  developer mode, which is bound false for every player).
- **Boot** (`on_start` requests, `on_frame_start` loads ONE module a frame, in
  this order): `rt/j_core .. rt/rl_overlay` (12), `gen/index`, `gen/enums`,
  every `index.framework` module, `ui/panel`, then the quest modules startUp
  and login cannot do without (below); then `QH.core.config_step()` until
  ready; then `QH.core.boot()`, bind `net.runelite.api.Client` =
  `J.rl_client.client()` and `@Named("developerMode")` = false,
  `QH.core.start_plugin("com.questhelper.QuestHelperPlugin", "Quest Helper")`
  (RuneLite's startUp through the injector), seed the watched varbits, and post
  ItemContainerChanged for 93/94/95 once (a login during the boot loaded them
  before anyone listened). 59 modules, ~90 frames; logs
  `quest-helper: started (N modules, K KB Lua heap)`. A re-enable after
  `on_stop` keeps every module and starts the plugin again.
- **Module names**: `api.sandbox` module `gen.helpers.quests.xmarksthespot` is
  the file `plugins/quest_helper/gen/helpers/quests/xmarksthespot.lua`.
- **On-demand loading**: `J.try_class(key)` loads the class's module (and its
  `index.modules` deps) when it is READY and answers nil otherwise;
  `J.request_class(key)` asks for it; `J.on_missing` (a `C[key]` of a quest
  class) loads it or raises J.PendingMT. Every frame the entry loads what was
  asked for and has arrived. `QH.load_quest(qhq, cb)` (the panel's) loads the
  helper's module(s), forces the proxy, calls back (true) or (false, reason);
  synchronously when it can; logs `quest-helper: loaded <module> (K KB Lua heap
  live)` after a full collection.
- **The QuestHelper facade** (`rt/j_core.lua`, `J.lazy_new`): RuneLite's
  startUp (`scanAndInstantiate`: inject, `setQuest/setConfig/...`, a child
  injector with `binder.install(helper)`), login (`QuestManager.setupRequirements`
  -> `initializeRequirements` of EVERY quest) and the quest list
  (`updateQuestList`: filters, sort, `getState`) touch all 272 helpers; loading
  them all is ~14 MB of code alone (131 MB with every helper set up) against a
  16 MB plugin heap. So a not-yet-loaded helper's proxy stands in as its
  framework base class (`index.bases`): fields go to a side table, the
  whitelisted base methods (setters/getters, `getState__Client`,
  `isCompleted`, `getVar__`, `configure`, `equals`/`hashCode`, ...) run on the
  proxy unless the quest overrides them (`index.overrides`), `instanceof` of a
  framework ancestor answers without loading, `initializeRequirements` is
  remembered and run when the module lands, and the rest loads the module (the
  constructor runs, then every field written through the facade is put back
  over it, then the quest's own `@Inject` fields are filled). Quests that
  override a state read the list asks of every helper (Shield of Arrav's
  `isCompleted`, Recipe for Disaster's `getState`) and the Check-items helper
  (`setupOnLogin` inits it) are loaded with the framework instead.
  `clientMeetsRequirements` (QuestFilter.SHOW_MEETS_REQS) is whitelisted too.
  The five list reads `J.FACADE_HARVEST` = `getItemRequirements`,
  `getItemRecommended`, `getGeneralRequirements`, `getGeneralRecommended`,
  `getExperienceRewards` answer what the RESIDENCY pass harvested (below);
  null before the pass reaches the quest, and always null when the quest does
  not override the method (the base answers null).
- **Residency** (2026-11-19, the entry's "residency" section + `rt/j_core.lua`):
  once in game, on IDLE frames (no quest load, retry, replay, module fetch or
  list job outstanding), a background pass walks the quest modules in
  QuestHelperQuest order, ONE module a frame (the next one's read queued
  meanwhile): for each facade helper of it a SHADOW instance
  (`J.lazy_shadow(p)`: constructor, the facade's fields, the quest's own
  `@Inject` fields; the proxy untouched) runs `initializeRequirements` and
  its five answers are kept on the facade (`J.lazy_answer(p, name, v)`;
  `J.lazy_harvested(p)`), then the module is UNLOADED: `J.undef(key)` for
  each of its classes (registry entry, unrealised definition, Class object),
  `api.sandbox.module_unload(name)`, its code is garbage; opening the quest
  later reads the module again and forces the SAME proxy (identity, hash code
  and map keys kept). A module is unloaded only when every helper in it is a
  harvested facade, no loaded module depends on it (`index.modules`), and
  nothing the EventBus holds is an instance of its classes; its dependencies
  follow. Pinned modules (the boot ones: Check items, Shield of Arrav, Recipe
  for Disaster) are never unloaded and their helpers are FORCED by the pass
  instead (Check items' answers are every other quest's, live). General
  requirements and experience rewards are the quest's own objects (checked
  live); item lists are COMPACT ItemRequirements (name, id, quantity,
  consumed; for a quantity -1 item also what its own class's `quantity(1)`
  copy answers), which is all AllNeededItems.refinedList reads -- the full
  objects are ~4 KB each, 7 MB for every quest; any other field read builds
  the rest of a fresh ItemRequirement. When the pass is through the entry
  sets `QuestManager.loadQuestList` (RuneLite's login path: the next
  updateQuestState re-runs the list and the item maps). EVICTION: a quest
  opened and closed again (not selected, not a background helper, not on the
  bus, not opened for `evict_age` = 1500 idle frames) is turned back into its
  facade (`J.lazy_unforce(p)`, answers read off the instance first) and its
  module unloaded while the LIVE heap is over `evict_kb` (cap - 6 MB), checked
  every 60 frames. The collector's pause is lowered (110..150%) while live is
  high so a cycle starts before the cap (`tune_gc`). Harvesting runs each
  helper's setupRequirements, as RuneLite's login does for every helper --
  side effects included (the farm-run helpers write their seed defaults, which
  posts ConfigChanged -> a list update). Seven getters compute their list from
  state at call time (Heroes' Quest, Ghosts Ahoy items; Scrambled, RFD Pirate
  Pete, Morytania Easy, Varrock Hard, Karamja Hard general requirements):
  their harvested answer is the state at harvest. WITHOUT
  `api.sandbox.module_unload` (a client older than the live-sweep round) nothing is unloaded and the
  pass stops at `ceiling_kb` (cap - 8 MB) live, logging
  `residency: stopped at K KB with N quest modules left ...`; keeping every
  module takes ~29 MB live, so that mode sees every quest only under a
  larger cap (`api.sandbox.memory_limit()`, read when present, sets the cap;
  16 MB otherwise). Numbers (harness, `test_residency.lua`): 259 helpers
  harvested + 13 pinned forced, 220 modules unloaded, 258 frames; largest
  callback during the pass 0.66M instructions; live heap 5.8 MB after login,
  8.2-8.6 MB peak/after (answers 1.1 MB); without unloading 38 helpers before
  the 8 MB ceiling.
- **The quest list, spread over frames** (the entry's "list jobs"):
  `QuestManager.updateQuestList`, `getAllItemRequirements` and
  `updateAllItemsHelper` are REPLACED at start (`install_list_jobs`) by jobs
  doing RuneLite's pipeline 32 helpers a frame -- the same predicates in the
  same order (filterListBy, difficulty, QuestDetails.showCompletedQuests,
  SkillFiltering.passesSkillFilter, LeagueFiltering.passesLeagueFilter; the
  item predicate, isNotCompleted, passesLeagueFilter), `getState` of every
  helper into the completed map, then config.orderListBy()'s comparator in a
  STABLE bottom-up merge sort 500 comparisons a frame, then the same ending
  (SwingUtilities panel refresh; the TreeMaps, the background Check-items
  start, and for updateAllItemsHelper Check items' restart AFTER the maps).
  A new call restarts its job. Within a job `SkillFiltering.passesSkillFilter`
  answers each helper once (memo; RuneLite's recursion revisits every
  prerequisite). A kept helper is compared with itself in the filter phase
  (a comparator is pure), so A_TO_Z's regex keys -- memoised by j_regex --
  are first computed 32 a frame rather than all in the first sort frame
  (2.7M). The job runs after the client-thread tasks each frame
  (`plugin._list_jobs` for tests). Measured: in one go the list was ~18-31M
  instructions with every helper answering (89M with every module loaded);
  as jobs the largest frame is 0.22M (0.32M with every module loaded; the
  largest callback of that run 0.8M), ~12 frames per update.
- **Fan-out** (every dispatch through `api.sandbox.protect`):

| Plugin callback | RuneLite |
|---|---|
| `on_server_tick` | first tick: `update_rs_profile`; then VarbitChanged (below), `GameTick`, `QH.core.tick_end()` |
| `on_var_changed` | buffered per tick; at the tick: one `VarbitChanged{varpId, varbitId=-1}` per varp, then one `VarbitChanged{varbitId, varpId}` per WATCHED varbit whose value moved (`J.rl_client.watched_varbits`: every varbit the plugin read, every QuestVarbits id, and the ids the Java compares `getVarbitId()` with: 464, 542, 666, 667, 1777, 18931). A varbit's varp is learned when a tick moves exactly one varp, else -1 |
| `on_interface` | `J.rlw.on_interface`, `WidgetLoaded{groupId}` / `WidgetClosed{groupId, unload=true}` |
| `on_inventory_changed` | `J.rl_client.inventory_changed`, `ItemContainerChanged{containerId, itemContainer = J.rl_client.changed_container(id)}` -- every container id, never a nil container (a shop, trade, group storage 659/660 are read by their id). The boot posts 93/94/95/659/660 once, each only when held |
| `on_chat_message` | `ChatMessage{type = ChatMessageType by getType(), name, message}` |
| `on_npc_spawn/retype/despawn` | `NpcSpawned` / `NpcChanged{npc, old}` / `NpcDespawned` through `NPC.__fromSnap/__bySlot/__forget` |
| `on_item_spawn/despawn` | `ItemSpawned/ItemDespawned{tile, item}` (only when subscribed) |
| `on_world_loaded` | `TileObject.__invalidate()`, `GameStateChanged(LOADING)`, every loc's `*ObjectSpawned` (only the kinds somebody subscribes to), `GameStateChanged(now)` |
| `on_screen_changed` | `J.rlw.on_screen_changed`, `update_rs_profile`, `GameStateChanged` |
| `on_config_changed` | `QH.core.config_changed(key)` |
| `on_key` | `J.rlw.on_key` -> "consume" |
| `on_menu_build` | `J.rl_client.menu_begin`, `MenuEntryAdded` per row, `QH.worldmap_menu()` (rl_overlay: a jump-on-click world map point under the mouse -- the one the last canvas pass drew there -- adds "Focus on <name>" on top, whose click is `WorldMap.setWorldMapPositionTarget(target or worldPoint)`), `menu_end` |
| `on_command` | `CommandExecuted{command = name, arguments = args}`; "consume" only for QuestHelperPlugin's own `questhelperdebug` / `qh-inv` / `qh` and only in developer mode (`QH.developer_mode`, bound as `@Named("developerMode")`, false as for every player) |
| `on_menu_select` | `MenuOptionClicked` (a row on an owned control gets the control's name as its target, `J.rlw.owned_menu_target`, as RuneLite's CC_OP target), `menu_click`, `J.rlw.menu_retarget(e)` (a native CC_OP row a subscriber moved with setParam0/1 is consumed and the same option pressed on the widget it now names; an op the plugin removed from a native widget with clearActions/setAction is dropped), `J.rlw.on_menu_select`; "consume" for an owned row, a consumed event or a retargeted/dropped row |
| `on_script_callback` | `script_enter(ref)`, `ScriptCallbackEvent{eventName}`, `script_leave`, `J.rlw.invalidate` |
| `on_script_pre` / `on_script_post` | (ids watched at start: 505 BANKMAIN_FINISHBUILDING, 514 BANKMAIN_SEARCHING, 281 BANKMAIN_SEARCH_TOGGLE, 750 SEARCHBOX_LOADED, 1711 WORLD_MAP_NEW_MAP_SELECTED, 1712 WORLDMAP_LOADMAP -- NOT 6555, another script in this pack) pre: `J.rlw.on_script_pre(id)` (a script that rebuilds a container drops this plugin's edits on its children: 505 -> Bankmain.ITEMS), then `script_enter(ref)`, `ScriptPreFired{scriptId, scriptEvent}` (a ScriptEvent whose source is the listener widget for a top-level script, nil for an invoked one), `script_leave`, `J.rlw.invalidate`; post: the same with `ScriptPostFired{scriptId}` |
| `on_draw_world` / `on_draw_canvas` | `QH.render("world"/"canvas", draw)` |
| `on_ui_build/action/draw`, `on_frame_start` | `QH.ui.build/action/draw/frame`; the ONE `porcelain.describe` calls `QH.ui.describe` |
| `on_frame_start` | boot step, module pump, `QH.load_quest` polls, `QH.core.frame()`, `QH.ui.frame`, `ClientTick` (in game) |
| `on_stop` | `ClientShutdown` (QuestHelperPlugin saves the quest bank), `QH.core.stop_plugin()`, `QH.render_reset()`, `J.rlw.on_stop()`, `RuneLiteObject.__destroyAll()` |
| `on_loc_changed` | `TileObject.__locs_changed()`, then `*ObjectDespawned` for the old loc (a value-equal stand-in) and `*ObjectSpawned` for the scene's new one (read from its Tile), each only when heard |
| `on_map_anim` | `GraphicsObjectCreated{graphicsObject}` (only when heard) |
| `on_item_search` | `J.rlw.ge_searched` (GrandExchangeSearched); a consumed search answers with the ids `setGeSearchResult*` set |
| (fix round, polled) | `on_frame_start` in game: `VarClientIntChanged{index}` for every WATCHED varc that moved (read by the plugin, or seeded: 1113..1120, the dial puzzles), (the world map's area is no longer polled: WORLDMAP_LOADMAP 1712 / 1711 come from the script hooks). `on_frame_start` also drains `ClientThread.invokeAtTickEnd` once a game tick has run since the start (RuneLite runs it at the end of every CLIENT tick: the bank tab lays its slots out there). `on_server_tick`: `ServerNpcLoot{composition, items}` the tick after an npc whose bar had reached empty despawns (items = ground items that spawned on its footprint that tick), `InteractingChanged{source, target}` for the local player and every npc whose `interacting` moved (only while heard) |
| `ClientToolbar.openPanel` | `api.panel.show()` (a listener the entry registers once) |

- **Errors**: a J.PendingMT (module or cache record not in) requests it. An
  EVENT POST never loses its event: `QH.core.post` runs every subscriber and
  re-raises with `redo = {event, subs}` -- exactly the subscribers that went
  pending -- and the entry delivers that event to them again
  (`EventBus:redeliver`) once nothing is outstanding (8 tries); the others
  never see it twice (fix round: a quest's first GameTick, NpcSpawned, ...
  after its module is requested reach it late instead of never). A ONE-OFF
  dispatch (panel action, menu select, config change, quest-loaded callback)
  that went pending outside a post is queued and run again the same way; a
  PERIODIC dispatch (draw passes) is dropped, its next turn being the retry. Any other error is logged once per distinct
  message (`quest-helper: <what>: <message>`, 200 at most) and the plugin
  carries on. The heap: `collectgarbage("param", "pause", 150)` at start.
- **plugins.ini**: `[plugin:quest-helper] source=plugins/quest_helper.lua
  enabled=0 budget=20000000 memory_mb=48`. Assets: `script/plugins/assets/quest-helper/`
  (the Quest Helper's 23 PNGs, RuneLite's `util/clue_arrow.png` and skill
  icons, by `SOURCES.sh`).

## Transpiler changes (2026-11-19, integration)

`singleton = true` for `@Singleton`; an `@Inject @Named("x")` field's entry is
`{field, type, "x"}` (rl_core resolves the named value by it); boolean
annotation values are Lua booleans (`hidden = true`, `closedByDefault =
true`, were `1`); `enums.lua` is emitted in a fixed order (two builds are
byte-identical); `index.lua` gains `bases` (a lazy class -> its nearest
framework superclass) and `overrides` (the framework methods it redefines);
a quest module that constructs ANOTHER quest module's class names that
module as a dependency (`index.modules`) and constructs it directly, instead
of a lazy proxy that could not load inside it (Desert Treasure II ->
Secrets of the North, The Final Dawn -> DT2, ...). A primitive class literal
is still the string `"int"` (rl_core accepts it).

Live-sweep round (2026-11-19): a compound assignment narrows the OPERATOR's
result back to the variable (`int x; x /= aFloat` is `x = J.d2i(x / aFloat)`,
JLS 15.26.2 -- `QuestPerspective.getMinimapPoint` and
`mapWorldPointToGraphicsPoint` held floats); a ternary whose untaken branch
is more than a plain read (a literal, a name, `this.f`, a static field, a cast
of one) is hoisted into an `if` instead of `J.cond(c, a, b)`, which evaluated
both branches (`s instanceof D ? ((D) s).m() : null` called `m` on a step
without it -- ConditionalStep.makeWidgetOverlayHint, every frame of Lumbridge
Elite). `c and a or b` with an always-truthy `a` stays (it is lazy).
`test_entry` lints gen/ for a `J.cond` branch that calls or constructs.

`build.sh` prints four javac `cannot find symbol` errors, all
`NpcID.HATIUS_LUMBRIDGE_DIARY` in the Lumbridge diary helpers: the 1.13.6
runelite-api jar the port compiles against (our revision) has no such
gameval. They are expected and harmless: `tools/questhelper_port/aliases.txt`
resolves the name to 16815, which our `all.npc.compack` confirms is
`hatius_cosaintus`, and the transpiler emits 16815 (re-running QhToLua into a
scratch directory gives output identical to the checked-in gen/, 0 unresolved
names). A NEW `cannot find symbol` is not expected: add the name to
aliases.txt only after checking its id in our compack.

## Status

- 2026-11-19: transpiler, Java runtime, harness (271/272 helpers set up
  against stubs), Lua sandbox C changes + tests done. UNCOMMITTED.
- 2026-11-19 (integration): the plugin entry, plugins.ini, assets, the
  facade, the transpiler fixes above. UNCOMMITTED.
  - Harness: every `tools/questhelper_port/harness/test_*.lua` passes
    (test_entry 78, test_rl_core 399, test_awt 151, test_panel 135,
    test_rl_client 135, test_rl_world 132, test_rl_widgets 116);
    `run_all.lua` (stubs) 271/272 (HERB_RUN reads a stub config);
    `run_all_real.lua` (the entry booted, the REAL rl layer, every helper
    loaded through `QH.load_quest`) **272/272**. test_entry runs under the
    client's sandbox globals (no os/io/debug/pcall/getmetatable/load) and the
    instruction count: the largest callback of the X Marks the Spot run is
    ~3.8M of the 20M budget.
  - LIVE (private embed build, soft3d, headless SDL dummy, agent API,
    fixture save, plugin enabled in a scratch TORIRS_PLUGIN_PREFS): boots in
    ~90 frames (5.4 MB heap; 6.4 MB live with X Marks the Spot open); the
    panel lists 262 helpers, search finds X Marks the Spot, the click loads
    its module, the assist page, the overview (spade red, then GREEN once
    `::give spade 1` and a restart -- the boot now posts the containers);
    Veos outlined in the world, the minimap arrow, the dialogue option
    "I'm looking for a quest." highlighted blue with its [2]; the varbit
    advanced Talk to Veos -> Dig north of Bob's -> (behind the castle) ->
    Draynor -> pig pen -> Bring the casket to Veos, each in the panel and
    the top-left overlay, dig tiles washed cyan with the spade icon. Two
    bugs found and fixed on the way: the in-canvas presenter dropped every
    word painted into a panel well (`app_chrome.c` pointed the TEXT prim at
    a stack copy; `PanelOverlayStage_KeptText`, regression test in
    `panel_overlay_stage_test.c`), and a step's `Scene.getTiles()` kept a
    snapshot table per loc (20-40k) and ran the 16 MB heap out (rl_world
    now indexes scenery CURSORS per plane, ground items apart from locs,
    a weak tile cache). No error or budget line in stderr.
  - Not done / not verified: see the integration report's open issues
    (sprite_image, ScriptPre/PostFired, VarClientIntChanged,
    CommandExecuted, dynamic loc spawns, the facade's null requirement
    reads, module unloading, world map points, bank tab, other renderers).
- 2026-11-19 (fix round, after the critic's review). UNCOMMITTED.
  - C: `cache.sprite_image`, `panel.show`, `on_loc_changed`, `on_map_anim`,
    `on_item_search` (the GE search hook in OC_FIND), actor fields on
    NpcSnap/PlayerSnap; `PluginHost_ServerTick` asserts its host (the one
    caller that could pass none now tests `app->plugins`); the module table's
    allocation is asserted. Tests: `test-plugin-host` 540 checks,
    `test-plugin-lua` (new verbs/events/snapshot fields), `test-feature-flags`,
    `test-trackers`, `test-tileind-v2`, `test-porcelain`,
    `test-panel-overlay-stage`, `test-nxt-plugins`, `test-plugin-contract`,
    `check-pt-switch`, `check-tree-walks`, the scan-meter gate (desk and
    phone) on a private build -- all pass. `test-plugin-lua-api-inventory`
    fails only on `api.drive` (test-only module, another session's; it
    also no longer misreads the entry's Java class strings as `api.X`);
    `test-plugin-api` fails only on `src/torirsserver/torirs_server_poll_kqueue.c`
    (not this port's).
  - Lua: the entry posts VarClientIntChanged (watched + seeded varcs),
    ClientShutdown, ServerNpcLoot, InteractingChanged, GraphicsObjectCreated,
    *ObjectDespawned/*ObjectSpawned mid-scene, the world map area (via a
    ScriptPostFired(1712) when the map list's text changes), answers the GE
    search, opens the panel on ClientToolbar.openPanel, seeds the rune
    pouch varbits (every `getVarbitId()` comparison in the generated code
    now sees its varbit), and replays a pending post to just the pending
    subscribers. `harness/check_inventory.lua` (checked in): 1494 external
    rows, 0 missing. Harness: test_entry 103, test_rl_core 399, test_awt 151,
    test_panel 135, test_rl_client 135, test_rl_world 132, test_rl_widgets
    116, run_all_real 272/272.
  - LIVE (private OPT=0 embed build, soft3d, a probe plugin beside Quest
    Helper): sprite 1453 pending then ready, a missing frame 'missing';
    opening the Draynor pig-pen gate raised four on_loc_changed (the two
    closed halves removed, the open ones placed) with Quest Helper running
    X Marks the Spot and no error line; talking to an npc showed the
    player's `interacting` = its slot and the orientation turn; an emote
    showed `anim_id` 855 then -1. Not seen live: on_map_anim, on_item_search
    (needs the GE), panel.show (needs a quest started outside the panel),
    ServerNpcLoot, the world-map area poll.
  - STILL OPEN: (the bank tab, the script hooks and the RECTANGLE/MODEL
    controls: done in the bank-tab round below); world-map point
    jump-on-click and hover tooltips (no API moves the map); module
    unloading (residency only grows) and the facade's null requirement
    reads -- Lua side done in the residency round below, the C verb
    `api.sandbox.module_unload` still missing; Menu.setMenuEntries/removeMenuEntry/
    isMenuOpen/non-CC_OP menuAction and Client.setVarbit (no API);
    developer commands (no command hook; developerMode is false); the
    panel's Discord/GitHub/Patreon/wiki links (no URL API);
    `api.cache.seq_def` (Animation frames); renderers other than soft3d.
- 2026-11-19 (residency round, Lua only). UNCOMMITTED.
  - The residency pass and the frame-spread list jobs (the entry, "The
    plugin entry" -> Residency / The quest list); `rt/j_core.lua`
    `J.lazy_is_facade`, `J.lazy_shadow`, `J.lazy_answer`, `J.lazy_harvested`,
    `J.lazy_unforce`, `J.undef`, `J.forget_class_object`, `J.FACADE_HARVEST`;
    `clientMeetsRequirements` on the facade.
  - Harness: NEW `test_residency.lua` 42 (+7 in its `--no-unload` run): the
    skill filter, "Show meets reqs", both quest lists and the Check-items
    totals over all 272 helpers equal a `--reference` run with every module
    loaded (RuneLite's state), with no module loaded to answer them;
    unload/reload identity (X Marks the Spot reopened after unload, evicted
    after closing, opened a third time: same object, same hash, fresh class,
    same steps; the selected quest never evicted; Desert Treasure II with its
    Secrets of the North dependency); every module unloaded; largest pass
    callback 0.66M instructions, live heap peak 8.2 MB (count 10.5 MB), 8.6 MB
    after; with every module loaded the list update's largest frame is 0.32M
    (was ~89M in one callback). test_entry 110 (mocks module_unload; waits for
    the list job after login), run_all_real 272/272, test_rl_core 399,
    test_awt 151, test_panel 135, test_rl_client 135, test_rl_world 132,
    check_inventory 0 missing.
  - OPEN: `api.sandbox.module_unload` (C) -- until it exists the live client
    harvests only until 8 MB live (~38 helpers) and unloads nothing;
    `api.sandbox.memory_limit` (C, with `memory_mb=`) for a larger cap. Not
    run live (Lua-only round; no client build).
- 2026-11-19 (bank-tab round). UNCOMMITTED.
  - C: script hooks -- `CS2VM2_SetScriptHook` / `CS2VM2_ScriptWatch` in
    `src/cs2vm2` (PRE at a top-level script's first opcode and at a gosub
    once the callee is pushed, POST at RETURN; `CS2VM2_Frame::hooked`,
    `hook_int_base/hook_str_base`; one global count while nothing is
    watched), `app_script_hook` in the bridge, `PluginHost_ScriptHook`,
    `scripts.watch`, `on_script_pre/post` (Lua + C + meta). Native cell
    edits written through (`UITree_WidgetSetNativeObject/Transparency/
    Hidden`: saved game value, reset restores, the game's write drops),
    `set_item`/`set_opacity` on natives, `set_native_hidden`,
    `set_scroll_size`; owned RECTANGLE / MODEL controls (`UITree_WidgetCreateRect/
    Model`, `App_PluginModelScene`, a model pump beside the item pump).
    Fixes found live: `widget:listener("onInvTransmit"/"onVarTransmit")`
    reads the CS2 host's transmit registries (they are not node hooks);
    an owned graphic given a cache sprite is IF3 (scaled to its box -- a
    TRADEBACKING_DARK rule painted a band over the bank); a Lua handler that
    faults in on_frame_start no longer crashes the Porcelain commit.
  - Lua: the entry watches 505/514/281/750/1711/1712 and posts
    ScriptPreFired/ScriptPostFired (the world-map area poll is gone);
    invokeAtTickEnd also drains per frame after the first game tick;
    owned-row menu targets; `menu_retarget`. rl_widgets: RECTANGLE/MODEL kinds,
    native item/opacity/hide/scroll writes, per-frame native child cache,
    bank rebuild resets, BankSearch pending retries; rl_client runScript
    raises pending; rl_overlay ImageUtil per-pixel results memoised per
    (source, op) (fillImage every frame made a composite -- and a release,
    two whole-tree walks -- per frame: the debug client's scan meter
    asserted with an item highlighted in the bank).
  - Tests: NEW `make -C src test-cs2-script-hooks` (16 ok);
    `test-plugin-host` 557 (+ watch union, hooks only to watchers, values
    read/written, no run inside a hook, teardown unwatch); `test-uitree`
    (+ native cell edits); `test-plugin-lua`, `test-porcelain` 655,
    `test-feature-flags` 71, `test-trackers` 173, `test-tileind-v2` 60,
    `test-plugin-contract`, `check-pt-switch`, `check-tree-walks` pass;
    `test-plugin-lua-api-inventory` and `test-plugin-api` fail only on the
    known api.drive / kqueue lines. Harness: test_entry 110 (script hooks:
    watched ids, 1712 post + 1711 pre world-map area, 514 return value),
    test_rl_widgets 137 (rect/model kinds, native re-lay, rebuild reset,
    retarget, dropped op), test_rl_client 135, test_rl_core 399, test_awt
    151, test_panel 135, test_rl_world 132, run_all_real 272/272,
    check_inventory 0 missing.
  - LIVE (private OPT=0 embed build, soft3d, SDL dummy, agent API, fixture
    save with 8 items banked at Draynor, a probe plugin beside Quest
    Helper): Cook's Assistant started from the quest list's "Start Quest
    Helper" row; the bank's Quest Helper button; "View tab" lays the bank
    out as RuneLite does (title "Tab Cook's Assistant", Recommended /
    Starting off / Getting the Flour / Getting the Milk / Finishing up /
    Non-quest items sections, have/need counts with tick/cross, the missing
    egg/flour/milk faded, every other banked item under Non-quest items,
    the list scrolls); Withdraw-1 on the re-laid pot withdrew the POT (the
    row moved to the real slot) and the tab re-laid itself after the bank
    rebuilt; a click on a placeholder was dropped; the button toggled the
    tab off and the native bank came back. GE: SEARCHBOX_LOADED put the
    button in the chat box, "View missing items" listed Coins, Bucket,
    Bucket of milk, Pot of flour, Egg. The probe saw pre/post for 277, 505,
    514 with their argument/return counts, and drew an owned rect, the
    player's and an npc's chathead (animated) and an obj model.
  - OPEN: see the round's open issues (fake-npc dialogue and the
    quest-complete scroll not seen live; potion-store withdraw from the tab;
    native ops of a re-laid cell cannot be replaced, so "Details" is not
    offered; the item highlight overlay draws over the scrolled-out part of
    the list).
- 2026-11-19 (client-APIs round). UNCOMMITTED.
  - C: menu editing (`menu.set_order` / `set_text` / `shown`: the host's
    per-build plan, payload rows tracked through the entity holders' drops,
    applied after them; an ordered build is not re-sorted), `cache.set_varbit`
    (a clientscript's own varbit write), `cache.seq_def`, `client.open_url`
    (the URL_OPEN slot; refused headless/replay), `client.worldmap_jump`,
    `on_command` (raised from the three "::" paths before the cheat is sent),
    manifest `memory_mb=` + `api.sandbox.memory_limit()` (per-script heap cap,
    default 16, ceiling 64; the internal reserve sits on top). Tests:
    `test-plugin-host` 583 (+ the plan's order with another plugin's row and a
    deprioritized walk, refusals, set_varbit/seq_def/open_url/worldmap_jump,
    draw-callback refusals, command consume order), `test-plugin-lua` (+ the
    bindings, on_command's split and consume, memory_mb 24 holds 18 MB, the
    default 16 refuses it, 200 clamps to 64), `test-feature-flags` 71,
    `test-nxt-plugins` 89, `test-plugin-contract`, `test-porcelain` 655,
    `test-uitree`, `test-cs2-script-hooks`, `check-pt-switch`,
    `check-tree-walks`, the scan-meter gate (desk + phone, private OPT=1
    build; Quest Helper not enabled in it) pass; `test-plugin-lua-api-inventory`
    / `test-plugin-api` fail only on the known api.drive / kqueue lines.
  - Lua: rl_client menu (setMenuEntries / removeMenuEntry / created-row
    positions / deprioritized / native setOption+setTarget / a native row's
    onClick), isMenuOpen + menu box, setVarbit/setVarbitValue,
    setWorldMapPositionTarget, LinkBrowser; rl_world Animation over seq_def
    (pending retried); rl_overlay world map hover tooltip + "Focus on"; the
    entry's on_command (CommandExecuted) and `QH.worldmap_menu()`; the panel's
    Discord/GitHub/Patreon row and clickable wiki line. Harness: test_rl_client
    155, test_rl_world 135, test_entry 116, test_panel 135, test_rl_core 399,
    test_awt 151, test_rl_widgets 137, test_residency 42 (+7), run_all_real
    272/272, check_inventory 0 missing.
  - LIVE (private OPT=0 embed build, soft3d, SDL dummy, agent API, the
    bank-tab round's fixture save, a scratch script dir with a probe plugin
    beside Quest Helper; `memory_mb=48` in its manifest): Quest Helper's
    residency harvested 259 of 272 helpers at 19 MB live (the 16 MB cap stops
    at ~38); the probe's `memory_limit` 16777216. A ground right-click with the
    probe's edits showed "Probe top" / Cancel / "Walk here (probe)" (walk
    deprioritized below Cancel, its text replaced) and the rewritten row still
    walked the player (3092 -> 3094); "Probe top" reached its owner. The quest
    list's right-click put "Start Quest Helper Sheep Shearer" just below "Read
    journal" (RuneLite's place; it used to sink to Cancel) and it started the
    helper. `::qaprobe ...` lines were consumed (no "Unknown command" from the
    server) and `::questhelperdebug enable` was heard and NOT consumed
    (developer mode off) -- the server answered "Unknown command". set_varbit
    4150 = 3 read back 3, an unknown varbit 'invalid'; seq_def 808 ready (12
    frames, 252 cycles), 9876 / 4230 pending then ready on the next ask;
    open_url 'unsupported' headless and 'invalid' for ftp; the panel's GitHub
    and Discord cells logged `could not open <url>: unsupported`;
    worldmap_jump glided Lumbridge from the map's edge to its centre, an
    uncovered tile 'not_found'; right-clicking Quest Helper's map point
    offered "Focus on Quest Helper" on top and choosing it centred the map on
    the point.
  - OPEN: see the round's open issues (a real browser open not exercised --
    headless refuses by design; a plain LEFT click on this client's world map
    moves the player (a native map behaviour, not a menu row), so a left click
    on a Quest Helper point both focuses and moves; ReplacedNpc's copied npc
    options would need menuAction of npc ops; QH's developer commands do
    nothing while developerMode is false).
- 2026-11-19 (live-sweep round). UNCOMMITTED.
  - HOW: one private embed client (`make -C src EMBED_SERVER=1 OPT=1
    PLATFORM_OBJ_BASE=build_qhsweep PLATFORM_TARGET=torirs_qhsweep`, removed at
    the end), driven with `tools/agent/torirs_agent.py` on a copy of
    `test/quests/fixtures/fresh_lumbridge.ini`, Quest Helper enabled only in a
    scratch `TORIRS_PLUGIN_PREFS` (a scratch script dir of symlinks plus a
    probe plugin). soft3d under `SDL_VIDEODRIVER=dummy` (agent `shot`); the GL
    lanes in a hidden window (`TORIRS_WINDOW_HIDDEN=1`) with
    `TORIRS_CHROME_EXECUTOR=web` (falls to the in-canvas panel, so the agent
    can press it) and shots by the probe's `::qashot` (`api.assets.screenshot`,
    the GL read-back) -- the agent's `shot` refuses a GL window.
  - LIVE, all three renderers: **soft3d**, **opengl3**, **opengl3-zbuffer**
    each showed the panel, the top-left step overlay, Veos/the Cook outlined,
    the dig tile washed cyan with the spade icon (occluded by the fence on the
    zbuffer lane, as it should be), the flashing world arrow, the minimap
    arrow, the spade outlined in the inventory while standing on the dig tile
    (OnTile), and the dialogue option highlighted blue with its [n]
    ("I'm looking for a quest." / "Yes." on soft3d, "What's wrong?" on
    gl3-zbuffer). The GL lanes draw the chrome panel at 2x.
  - LIVE, completed end to end with the helper: **X Marks the Spot** (soft3d
    then gl3-zbuffer: every step in turn, Veos at Port Sarim outlined, the
    reward) and **Cook's Assistant** (gl3-zbuffer: "Purchase a bucket", items
    green after `::give`, the Cook outlined, "What's wrong?" highlighted, quest
    complete). On completion the helper closes itself and the list drops the
    finished quest (262 -> 261 -> 260 with "Show All"), as RuneLite does.
  - LIVE, world map: with Pirate's Treasure's "Talk to Luthas" step the Quest
    Helper point draws on the map; panned away, the edge arrow draws in the
    corner; right-click "Focus on Quest Helper" jumped the map back to Karamja.
  - LIVE, a chat-message-driven step: Pirate's Treasure (`::setvar
    varp71_hunt ^hunt_fetch_rum`, crate varps 10/1), journal opened to sync,
    "Search Crate" -> "The crate is full of bananas." + "There is also some
    rum stashed in here too." moved the step to "Talk to Luthas ... finished
    filling the crate" (ChatMessageRequirement x2 + WidgetTextRequirement).
    The bank tab ("Tab Pirate's Treasure", sections, 0/60 coins) and the quest
    list's "Stop Quest Helper"/"Start Quest Helper" rows checked again. Panel:
    search, assist-level page on first pick (then remembered), Full
    assistance, Close helper, Filters -> Free To Play (22), Ordering -> Z To A.
  - FIXED (found live): (1) Pirate's Treasure's journal requirements ran
    on_server_tick out of the 20M budget and the plugin was DISABLED --
    `getStaticChildren()[i]` inside WidgetTextRequirement's loop re-read and
    re-wrapped every child per index; rl_widgets now reads static and nested
    children once a frame (test_rl_widgets +4). (2) The plugin window snapped
    back to 8,72 at 260 tall on EVERY page rebuild (each quest pick, Close
    helper) -- `src/plugin/torirs_plugin_panel.u.c` now re-places the floating
    box only when it is created, on a fullscreen toggle, on a surface, or on a
    scale/canvas change. (3) A 15-quest session filled the 48 MB heap (nothing
    unloads) and the panel went BLANK with no log line -- added
    `api.sandbox.module_unload` (C; test-plugin-lua re-reads and re-runs an
    unloaded module); live the residency pass now unloads 219 modules and
    sits at 8.8 MB. (4) Desert Treasure II's startUp was 14-16M instructions
    (on_frame_start over half the budget): rl_core's injector memo +
    per-object skip, now 4.1M (test_rl_core +6). (5) Transpiler: compound
    assignment narrowing (the minimap arrow's points were floats) and eager
    ternaries (Lumbridge Elite's widget overlay failed every frame) -- see
    "Transpiler changes"; test_entry +3 (int minimap point, the far minimap
    arrow, the J.cond lint, which fails on the pre-fix gen/). (6) Instrument:
    the host's peak-heap / largest-callback report (API section).
  - NUMBERS (TORIRS_PLUGIN_LOG release lines): X Marks the Spot on soft3d
    peak 34 MB (counting garbage), largest callback 1.5M (on_server_tick);
    GL runs 30-34 MB, 1.5-1.6M (on_ui_action). The long session -- 15 quests
    opened back to back (Sheep Shearer, DS2, MM2, DT2, Song of the Elves,
    WGS, Sins of the Father, A Kingdom Divided, Fremennik Trials, Legends',
    Lumbridge Elite diary, Barbarian Training, Regicide, Curse of Arrav, X
    Marks): before the fixes the heap hit the cap and the panel blanked, one
    on_ui_action ran 15.3M; after them live heap 8.8 MB after the residency
    pass and 32.2 MB after the 15th quest, the allocation high-water mark
    touches the 48 MB cap (garbage; Lua's emergency collection, no failure),
    largest callback 5.7M (on_frame_start), no error, no budget or memory
    line. A short session: 14.4 MB, 0.66M.
  - Tests: every harness test (test_entry 119, test_rl_core 404, test_awt
    151, test_panel 135, test_rl_client 155, test_rl_world 135,
    test_rl_widgets 141, test_residency 42 (+7 --no-unload), run_all_real
    272/272, check_inventory 0 missing); `test-plugin-lua` (OPT=0, -o the
    inventory), `test-plugin-host` 583, `test-porcelain` 655,
    `test-panel-overlay-stage`, `test-plugin-contract`, `test-feature-flags`
    71, `check-scan-meter` PASS (desk + phone);
    `test-plugin-lua-api-inventory` fails only on the known api.drive lines.
  - OPEN: see this round's open issues (hint arrows, a varc puzzle and the
    sidebar's manual skip / lock not reachable live; the GE filter not
    re-checked; the panel's dropdown is overdrawn by the list wells; the
    TOP_LEFT overlay draws over the world map; padded search wells show empty
    boxes; the window at 2x on GL; eviction only above 42 MB).
- 2026-11-19 (fix round 2, after the critic's second review). UNCOMMITTED.
  - Critic's own runs, recorded here: `tools/scan_meter_gate.sh` with Quest
    Helper ENABLED (plugin prefs only; private OPT=1 embed build, 4000 frames)
    passes desk and phone -- boot 59 modules / 6.0 MB, residency 259/272
    harvested, 219 modules unloaded (8.7 MB live), peak heap 14.1 MB, largest
    callback 0.68M (on_frame_start); no quest selected, bank closed. Transpiler
    re-run into a scratch dir: identical to gen/.
  - FIXED: (1) ItemContainerChanged for any container other than 93/94/95
    (a shop, trade, group storage) posted a nil container and
    QuestHelperPlugin.onItemContainerChanged threw an NPE -- C: `api.game.
    container_size/container_slot` read ANY container by its wire id (engine
    hooks in the bridge over InvManager; `inventory_slot` now goes through the
    same code); Lua: rl_client reads every id, `changed_container` is never
    nil (a written container with no slots is empty), the boot also posts
    659/660 -- group storage reaches QuestContainerManager.getGroupStorageData
    and updateLocalGroupBank. (2) Sailing: C `api.game.world_entity_next/
    world_entity_to_main` (over Wevs + the deck box); rl_world WorldEntity,
    boat world views, an aboard actor's world view, the root plane aboard,
    Perspective through the entity (see "Sailing"). (3) Settings with nothing
    behind them (`use_shortest_path`, `dev_show_overlay_on_launch`) are stored,
    not offered. (4) The bank tab's scroll size is given back at stop. (5)
    Found live: the panel asked `panel_scroll_to(0)` on a quest pick with no
    page up (a PORCELAIN_FINDING per pick, "Start Quest Helper" from the quest
    list); it now scrolls only when `panel_scroll() >= 0`.
  - Tests: `test-plugin-host` 590 (+ container reads, a container never sent,
    world entities and to_main through the engine) at OPT=1 and OPT=0;
    `test-plugin-lua` (+ the four bindings; OPT=1 and OPT=0, -o the inventory);
    `test-plugin-contract`, `check-tree-walks`, `check-pt-switch` pass;
    `test-plugin-lua-api-inventory` fails only on the known api.drive lines.
    Harness: test_entry 122 (+ a shop's container, group storage 659 into
    QuestContainerManager, an empty written container), test_rl_world 157
    (+22 sailing), test_rl_client 160, test_rl_core 407 (+ not-offered
    items), test_rl_widgets 144 (+ scroll restore), test_awt 151, test_panel
    135, test_residency 42 (+7 --no-unload), run_all_real 272/272, run_all
    (stubs) 271/272 (HERB_RUN as before), check_inventory 1494 rows 0 missing.
  - LIVE (private OPT=1 embed build `build_qhfix`, removed; soft3d, SDL dummy,
    agent API, a copy of the fresh_lumbridge fixture, Quest Helper enabled in a
    scratch TORIRS_PLUGIN_PREFS, `TORIRS_SCAN_METER_STRICT=1` for the whole
    run): the Lumbridge General Store opened and a bucket bought (shop and
    backpack containers changed) with no error line from quest-helper (before
    the fix the shop's event threw); "Start Quest Helper" on Cook's Assistant
    from the quest list -- the step overlay, the shop assistant outlined, the
    minimap arrow; the step moved to "Grab an egg" once the pot and bucket were
    held; the bank's Quest Helper button -> "View tab" laid out "Tab Cook's
    Assistant" (Recommended / Starting off / Getting the Flour / Getting the
    Milk ...); then 3000 steady frames with the quest open, step overlays
    drawing, the re-laid bank tab and its item highlight under the STRICT
    scan meter: no `uitree:` line, no abort (the case the critic said was
    never gated). Exit report: peak heap 20.9 MB of 48, largest callback
    1.92M (on_frame_start). A second run with the panel fix: "Start Quest
    Helper Sheep Shearer" left no PORCELAIN_FINDING; peak 15.3 MB, 1.09M.
  - Checked, not changed: the bank item highlight drawn over scrolled-out
    slots (seen live below the bank with the tab open) is RuneLite's own path
    -- ConditionalStep.makeWidgetOverlayHint calls renderBank with no
    isBankTabOpen test, QuestStep.renderBank draws every dynamic child's
    getBounds() with no clip, and OverlayRenderer.clipBounds gives an
    ALWAYS_ON_TOP overlay the whole canvas (whether RuneLite's injected
    Widget.isHidden hides scrolled-out cells cannot be read here: the mixins
    are not in the local runelite checkout). setItemQuantityMode(ALWAYS) is
    still not applied (a re-laid cell keeps the game's own count rule).
  - OPEN: see this round's open issues (a boat's own scene and hull outline;
    not seen live: sailing steps, world lines, widget/spell/emote highlights,
    the tooltip overlay, puzzle solvers, ServerNpcLoot / GraphicsObjectCreated
    consumers, fake objects / Cheerer / the quest-complete scroll; web,
    Android, Windows and the phone layout not run; panel and menu items carried
    from earlier rounds).
