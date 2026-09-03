---@meta
-- LuaLS mirror of the native plugin API v2. The runtime/meta inventory test
-- compares every module and callable in this file with torirs_plugin_lua.c.

---@deprecated Removed by the sandbox; use api.core.log.
---@type nil
print = nil
---@deprecated Removed by the sandbox.
---@type nil
require = nil
---@deprecated Removed by the sandbox.
---@type nil
load = nil
---@deprecated Removed by the sandbox.
---@type nil
loadfile = nil
---@deprecated Removed by the sandbox.
---@type nil
dofile = nil

---@alias torirs.Colour integer|string
---@alias torirs.ResultName 'ok'|'not_found'|'pending'|'unsupported'|'conflict'|'budget'|'invalid'|'error'
---@alias torirs.AssetState 'pending'|'ready'|'missing'|'invalid'|'budget'|'error'
---@alias torirs.Verdict boolean|'consume'|nil
---@alias torirs.UiFacet 'bounds'|'appearance'|'actions'|'all'
---@alias torirs.Area 'platform_safe'|'frame_build'|'overlay_safe'|'raw_viewport'|integer
---@alias torirs.Anchor 'top-left'|'top'|'top-right'|'left'|'center'|'right'|'bottom-left'|'bottom'|'bottom-right'|integer
---@alias torirs.Edge 'top'|'right'|'bottom'|'left'|integer
---@alias torirs.PanelView 'page'|'settings'
---@alias torirs.Surface 'viewport'|'minimap'|'sidebar'|'chat'|'chat_buttons'|'modal'|'compass'|'orbs'|integer
---@alias torirs.KeyName 'shift'|'ctrl'|'space'|'tab'|'escape'
---@alias torirs.ImageRef integer
---@alias torirs.ModelRef integer
---@alias torirs.MeshRef integer
---@alias torirs.SceneInstanceRef integer
---@alias torirs.UiNodeRef integer
---@alias torirs.PlacementAreaRef integer

---@class torirs.Rect
---@field x integer
---@field y integer
---@field width integer
---@field height integer
---@field w integer Alias of width.
---@field h integer Alias of height.

---@class torirs.PlayerSnap
---@field name string
---@field true_x integer
---@field true_z integer
---@field level integer
---@field fine_x integer
---@field fine_z integer
---@field dest_x integer
---@field dest_z integer
---@field flag_x integer
---@field flag_z integer
---@field server_pid integer
---@field element_id integer
---@field combat_level integer

---@class torirs.NpcSnap
---@field server_slot integer
---@field npc_id integer
---@field base_npc_id integer
---@field name string
---@field combat_level integer
---@field size integer
---@field true_x integer
---@field true_z integer
---@field level integer
---@field fine_x integer
---@field fine_z integer
---@field element_id integer
---@field visible_ops integer
---@field health_ratio integer
---@field health_scale integer

---@class torirs.ItemSnap
---@field obj_id integer
---@field count integer
---@field cost integer
---@field value integer
---@field name string
---@field tile_x integer
---@field tile_z integer
---@field level integer
---@field element_id integer

---@class torirs.ScenerySnap
---@field loc_id integer
---@field name string
---@field tile_x integer
---@field tile_z integer
---@field level integer
---@field size_x integer
---@field size_z integer
---@field shape integer
---@field angle integer
---@field element_id integer
---@field interactive boolean
---@field visible_ops integer

---@class torirs.FrameEvent
---@field now_ms integer
---@field drawn_frames integer

---@class torirs.TickEvent
---@field cycle integer

---@class torirs.WorldLoadedEvent
---@field base_tile_x integer
---@field base_tile_z integer

---@class torirs.ScreenChangedEvent
---@field screen string
---@field previous string

---@class torirs.AssetEvent
---@field name string
---@field size integer
---@field ok boolean

---@class torirs.ChatMessageEvent
---@field type integer
---@field sender string
---@field text string

---@class torirs.GameEvent
---@field kind string
---@field subject string
---@field value integer
---@field text string

---@class torirs.KeyEvent
---@field key integer
---@field ch integer
---@field down boolean

---@class torirs.MenuRow
---@field text string
---@field action integer
---@field pick_kind integer
---@field npc_slot integer
---@field player_pid integer
---@field target_id integer
---@field component_id integer
---@field slot integer

---@class torirs.MenuBuildEvent
---@field hover_pass boolean
---@field rows torirs.MenuRow[]

---@class torirs.MenuSelectEvent
---@field row torirs.MenuRow
---@field tag integer
---@field owned boolean
---@field x integer
---@field y integer

---@class torirs.PanelActionEvent
---@field id string
---@field action 'activate'|'toggle'|'text'|'pick'|'drag'|'scroll'|'key'|'unknown'
---@field value integer
---@field on boolean
---@field text string Stable option value for a select action, never its label.
---@field x integer
---@field y integer
---@field generation integer
---@field serial integer
---@field sequence integer

---@class torirs.PanelLayoutEvent
---@field width integer
---@field height integer
---@field scale_milli integer
---@field scale number
---@field size_class 'compact'|'medium'|'expanded'|'unknown'
---@field visible boolean
---@field game_visible boolean
---@field generation integer

---@class torirs.CanvasActionEvent
---@field id integer
---@field operation integer
---@field x integer
---@field y integer

---@class torirs.CoreApi
---@field log fun(...: any)
---@field notify fun(text: string)
---@field screen fun(): integer
---@field frame_ms fun(): integer
---@field frame_work_us fun(): integer
---@field lane fun(): torirs.Lane?
---@field capability fun(name: string): boolean
---@field plugin_id fun(): string

---@class torirs.Lane
---@field game integer
---@field epoch integer
---@field revision integer

---@class torirs.ConfigApi
---@field has fun(key: string): boolean
---@field get_bool fun(key: string): boolean?
---@field get_int fun(key: string): integer?
---@field get_color fun(key: string): integer?
---@field get_string fun(key: string): string?
---@field set fun(key: string, value: any): boolean, torirs.ResultName
---@field [string] boolean|integer|string|function Declared config keys are readable properties.

---@class torirs.WorldApi
---@field local_player fun(): torirs.PlayerSnap?
---@field npc_next fun(cursor?: integer): integer?, torirs.NpcSnap?
---@field npc_by_slot fun(server_slot: integer): torirs.NpcSnap?
---@field player_next fun(cursor?: integer): integer?, torirs.PlayerSnap?
---@field item_next fun(cursor?: integer): integer?, torirs.ItemSnap?
---@field scenery_next fun(cursor?: integer): integer?, torirs.ScenerySnap?

---@class torirs.InputApi
---@field key_held fun(key: torirs.KeyName|integer): boolean
---@field pointer fun(): integer?, integer?
---@field hover_tile fun(): integer?, integer?, integer?
---@field hover_entity fun(): torirs.HoverEntity?
---@field text_input fun(enabled: boolean)
---@field chat_focus fun(focused: boolean)

---@class torirs.HoverEntity
---@field kind integer
---@field element_id integer
---@field tile_x integer
---@field tile_z integer
---@field level integer

---@class torirs.UiApi
---@field ref fun(name: string): torirs.UiNodeRef?
---@field info fun(node: torirs.UiNodeRef): torirs.UiNodeInfo?
---@field invoke fun(node: torirs.UiNodeRef, action: string): boolean
---@field contribution_info fun(node: string, facets: torirs.UiFacet[]|torirs.UiFacet|integer): torirs.UiContributionInfo?
---@field update fun(node: torirs.UiNodeRef, facets: torirs.UiFacet[]|torirs.UiFacet|integer, value: torirs.UiNode): boolean, torirs.ResultName
---@field menu_add fun(text: string, action_id: integer): boolean Only during on_menu_build.
---@field set_enabled fun(node: torirs.UiNodeRef, enabled: boolean): boolean, torirs.ResultName Activate or release this plugin's static contribution.

---@class torirs.UiNode
---@field bounds? torirs.Rect
---@field parent? string
---@field anchor? torirs.Anchor
---@field paint_order? integer
---@field flags? integer
---@field image? torirs.ImageRef
---@field label? string
---@field action? string
---@field clip? integer
---@field label_x? integer
---@field label_y? integer
---@field hit_rect? torirs.Rect
---@field state_images? table<string, torirs.ImageRef>
---@field actions? string[]

---@class torirs.UiNodeInfo
---@field bounds torirs.Rect
---@field available_facets integer
---@field visible boolean
---@field enabled boolean
---@field active boolean
---@field parent torirs.UiNodeRef?
---@field anchor integer
---@field paint_order integer
---@field clip integer
---@field label string
---@field label_x integer
---@field label_y integer
---@field hit_rect torirs.Rect
---@field actions string[]
---@field state_images torirs.ImageRef[]

---@class torirs.UiContributionInfo
---@field state integer
---@field active_facets integer
---@field conflict_plugin string

---@class torirs.PlacementApi
---@field revision fun(): integer
---@field area fun(area: torirs.Area): torirs.PlacementAreaRef?
---@field primary fun(area: torirs.PlacementAreaRef): torirs.Rect?
---@field place fun(area: torirs.Area, anchor: torirs.Anchor, width: integer, height: integer, margin?: integer): torirs.Rect?
---@field rect_next fun(area: torirs.PlacementAreaRef, cursor?: integer): integer?, torirs.Rect?
---@field contains fun(area: torirs.PlacementAreaRef, rect: torirs.Rect): boolean
---@field reserve fun(name: string, area: torirs.Area, edge: torirs.Edge, pixels: integer): boolean, string
---@field reservation_rect fun(name: string): torirs.Rect?

---@class torirs.FrameApi
---@field offer_next fun(cursor?: integer): integer?, torirs.FrameOfferInfo?
---@field selection fun(): torirs.FrameSelection
---@field select fun(id: string): boolean, torirs.ResultName
---@field invalidate fun()
---@field surface_native_size fun(surface: torirs.Surface): integer?, integer?

---@class torirs.FrameOfferInfo
---@field id string Canonical `<plugin-id>/<local-id>`.
---@field title string
---@field provider string
---@field canvas integer
---@field width integer
---@field height integer
---@field min_width integer
---@field min_height integer
---@field available boolean
---@field detail string

---@class torirs.FrameSelection
---@field requested_id string
---@field active_id string
---@field status integer
---@field reason string
---@field revision integer

---@class torirs.FrameBuildContext
---@field offer_id string Local offer id.
---@field canvas 'fixed'|'window'
---@field logical_canvas torirs.Rect
---@field available torirs.PlacementAreaRef?
---@field lane torirs.Lane

---@class torirs.FrameBuilder
---@field surface fun(surface: torirs.Surface, rect: torirs.Rect)
---@field surface_member fun(surface: torirs.Surface, member: integer, rect: torirs.Rect)
---@field skin fun(surface: torirs.Surface, skin: table)
---@field ui_node fun(name: string, node: torirs.UiNode)
---@field scrollbar fun(skin: table)
---@field reason fun(text: string)
---@field surface_overlay fun(surface: torirs.Surface, overlay: table)

---@class torirs.FrameOffer
---@field id string Local stable id; the catalogue exposes `<plugin-id>/<id>`.
---@field title string
---@field canvas 'fixed'|'window'
---@field width? integer Required for fixed canvas.
---@field height? integer Required for fixed canvas.
---@field min_width? integer Required for window canvas.
---@field min_height? integer Required for window canvas.
---@field build fun(api: torirs.Api, frame: torirs.FrameBuilder, context: torirs.FrameBuildContext): 'ready'|'pending'|'unsupported'|'error'
---@field draw? fun(api: torirs.Api, draw: torirs.DrawBuilder)

---@class torirs.DrawApi
---@field project fun(fine_x: integer, fine_z: integer, height?: integer): integer?, integer?
---@field element_height fun(element_id: integer): integer
---@field hsl_from_rgb fun(rgb: torirs.Colour): integer
---@field hsl_to_rgb fun(hsl: integer): integer

---@class torirs.AssetsApi
---@field request fun(name: string): torirs.AssetState
---@field bytes fun(name: string): string?
---@field save fun(name: string, data: string): boolean, torirs.ResultName
---@field release fun(name: string)
---@field image fun(name: string): torirs.ImageRef?, torirs.AssetState
---@field image_size fun(image: torirs.ImageRef): integer?, integer?
---@field image_release fun(image: torirs.ImageRef)
---@field model fun(name: string): torirs.ModelRef?, torirs.AssetState
---@field model_release fun(model: torirs.ModelRef)
---@field screenshot fun(destination: string, name: string): boolean, string
---@field image_pixels fun(image: torirs.ImageRef): integer[]?
---@field image_compose fun(name: string, width: integer, height: integer, argb: integer[]): torirs.ImageRef?, torirs.AssetState

---@class torirs.SceneApi
---@field mesh_create fun(): torirs.MeshRef?, torirs.ResultName
---@field mesh_destroy fun(mesh: torirs.MeshRef)
---@field mesh_vertex fun(mesh: torirs.MeshRef, x: integer, y: integer, z: integer): boolean, torirs.ResultName
---@field mesh_face fun(mesh: torirs.MeshRef, a: integer, b: integer, c: integer, hsl: integer, alpha?: integer): boolean, torirs.ResultName
---@field instance_create fun(): torirs.SceneInstanceRef?, torirs.ResultName
---@field instance_destroy fun(instance: torirs.SceneInstanceRef)
---@field instance_model fun(instance: torirs.SceneInstanceRef, model: torirs.ModelRef): boolean, torirs.ResultName
---@field instance_position fun(instance: torirs.SceneInstanceRef, tile_x: integer, tile_z: integer, level: integer, height?: integer, yaw?: integer): boolean, torirs.ResultName
---@field instance_active fun(instance: torirs.SceneInstanceRef, active: boolean)
---@field instance_mesh fun(instance: torirs.SceneInstanceRef, mesh: torirs.MeshRef): boolean, torirs.ResultName
---@field instance_cache_model fun(instance: torirs.SceneInstanceRef, kind: integer, id: integer): boolean, torirs.ResultName
---@field instance_recolor fun(instance: torirs.SceneInstanceRef, from_hsl: integer, to_hsl: integer): boolean, torirs.ResultName
---@field instance_clear_recolors fun(instance: torirs.SceneInstanceRef)
---@field instance_animation fun(instance: torirs.SceneInstanceRef, sequence_id: integer, loop: boolean): boolean, torirs.ResultName
---@field instance_light fun(instance: torirs.SceneInstanceRef, ambient: integer, contrast: integer): boolean, torirs.ResultName
---@field instance_ready fun(instance: torirs.SceneInstanceRef): boolean

---@class torirs.PanelApi
---@field request fun(description: torirs.PanelDescription): boolean, torirs.ResultName
---@field invalidate fun()
---@field attention fun(wanted: boolean)
---@field set_text fun(id: string, text: string): boolean, torirs.ResultName
---@field set_value fun(id: string, value: integer|boolean): boolean, torirs.ResultName
---@field set_height fun(id: string, preferred_height: integer): boolean, torirs.ResultName
---@field set_options fun(id: string, value: string, options: torirs.SelectOption[]): boolean, torirs.ResultName
---@field redraw fun(id: string)

---@class torirs.CacheApi
---@field frame_root fun(): integer
---@field varbit fun(id: integer): integer
---@field varp fun(id: integer): integer
---@field component_rect fun(component_id: integer): torirs.Rect?
---@field invoke fun(component_id: integer, operation: integer): boolean
---@field named_id fun(kind: string, name: string): integer?
---@field tab_active fun(): integer
---@field tab_enabled fun(tab: integer): boolean
---@field tab_select fun(tab: integer): boolean

---@class torirs.ClientApi
---@field display_get fun(setting: integer): integer?, integer?, integer?
---@field display_set fun(setting: integer, value: integer): boolean, torirs.ResultName
---@field feature_next fun(cursor?: integer): integer?, torirs.Feature?
---@field feature_get fun(key: string): integer?
---@field feature_set fun(key: string, value: integer): boolean, torirs.ResultName
---@field world_cycle fun(): integer
---@field datestamp fun(): string?
---@field setting_color fun(varp_id: integer, fallback?: integer): integer
---@field memory_bytes fun(): integer
---@field disable_self fun(reason: string)

---@class torirs.Feature
---@field key string
---@field label string
---@field kind integer
---@field value integer
---@field min integer
---@field max integer
---@field choices string

---@class torirs.Skill
---@field index integer
---@field name string
---@field current_level integer
---@field base_level integer
---@field xp integer
---@field level_xp integer
---@field next_level_xp integer

---@class torirs.ItemInfo
---@field obj_id integer
---@field name string
---@field cost integer
---@field stackable boolean
---@field cert_link integer
---@field wearpos integer
---@field wearpos2 integer
---@field wearpos3 integer
---@field has_bonuses boolean
---@field bonuses integer[]
---@field attack_rate integer
---@field ranged_strength integer

---@class torirs.Highlight
---@field kind integer
---@field element_id integer
---@field tile_x integer
---@field tile_z integer
---@field level integer
---@field size_x integer
---@field size_z integer
---@field rgb integer
---@field opacity integer
---@field outline_width integer
---@field flags integer
---@field name string
---@field fine_x integer
---@field fine_z integer
---@field overhead_height integer

---@class torirs.LootSource
---@field id integer
---@field name string
---@field row_count integer
---@field kill_count integer

---@class torirs.LootRow
---@field obj_id integer
---@field quantity integer
---@field value integer

---@class torirs.GameApi
---@field skill fun(index: integer): torirs.Skill?
---@field run_energy fun(): integer
---@field inventory_size fun(inventory: integer): integer
---@field inventory_slot fun(inventory: integer, slot: integer): integer?, integer?
---@field item_info fun(obj_id: integer): torirs.ItemInfo?
---@field item_image fun(obj_id: integer, count?: integer, style?: integer): torirs.ImageRef?, torirs.AssetState
---@field highlight_next fun(cursor?: integer): integer?, torirs.Highlight?
---@field loot_source_next fun(cursor?: integer): integer?, torirs.LootSource?
---@field loot_row_next fun(source_id: integer, cursor?: integer): integer?, torirs.LootRow?
---@field entity_part fun(kind: integer, a: integer, b: integer, c: integer, d: integer): string?
---@field entity_look fun(part: string, look: table): boolean, torirs.ResultName
---@field entity_ops fun(part: string, mode: integer, operations: string[], action_id?: integer): boolean, torirs.ResultName
---@field loot_revision fun(): integer
---@field loot_source_clear fun(source_id: integer): boolean

---@class torirs.DrawBuilder
---@field rect fun(x: integer, y: integer, width: integer, height: integer, rgb: torirs.Colour, alpha?: integer)
---@field line fun(x0: integer, y0: integer, x1: integer, y1: integer, rgb: torirs.Colour, alpha?: integer)
---@field text fun(x: integer, y: integer, text: string, rgb?: torirs.Colour)
---@field image fun(image: torirs.ImageRef, x: integer, y: integer, alpha?: integer)
---@field world_tile fun(tile_x: integer, tile_z: integer, level: integer, fill_rgb: torirs.Colour, outline_rgb?: torirs.Colour, alpha?: integer): boolean, torirs.ResultName
---@field world_hull fun(element_id: integer, rgb: torirs.Colour, alpha?: integer, shape?: 'bounds'|'mesh'|integer): boolean, torirs.ResultName
---@field action_region fun(rect: torirs.Rect, action: string): boolean, torirs.ResultName
---@field image_clip fun(image: torirs.ImageRef, x: integer, y: integer, clip: torirs.Rect, alpha?: integer)
---@field action_region_id fun(rect: torirs.Rect, action: string, action_id: integer): boolean, torirs.ResultName
---@field context fun(): torirs.DrawContext?

---@class torirs.PanelBuilder
---@field heading fun(text: string)
---@field paragraph fun(text: string)
---@field toggle fun(id: string, label: string, value: boolean)
---@field select fun(id: string, label: string, value: string, options: torirs.SelectOption[])
---@field button fun(id: string, label: string, enabled?: boolean)
---@field custom fun(id: string, preferred_height?: integer)
---@field label fun(id: string, text: string)
---@field key_value fun(id: string, label: string, value: string)
---@field node fun(node: table): boolean, torirs.ResultName

---@class torirs.DrawContext
---@field bounds torirs.Rect
---@field clip torirs.Rect

---@class torirs.SelectOption
---@field value string Stable value delivered in an action.
---@field label string Presentation only; duplicates and '|' are legal.
---@field enabled? boolean
---@field detail? string Accessible explanatory text.

---@class torirs.PanelDescription
---@field icon_asset? string
---@field preferred_width? integer

---@class torirs.UiContribution
---@field node string Canonical semantic name.
---@field mode? 'modify'|'provide_if_missing'|'replace_or_provide'
---@field facets? torirs.UiFacet[]|torirs.UiFacet|integer
---@field value torirs.UiNode

---@class torirs.ConfigItem
---@field key string
---@field type? 'bool'|'int'|'color'|'colour'|'string'|'enum'|'text'
---@field label? string
---@field default? boolean|integer|string
---@field min? integer
---@field max? integer
---@field choices? string
---@field rows? integer

---@class torirs.Api
---@field core torirs.CoreApi
---@field config torirs.ConfigApi
---@field world torirs.WorldApi
---@field input torirs.InputApi
---@field ui torirs.UiApi
---@field placement torirs.PlacementApi
---@field frame torirs.FrameApi
---@field draw torirs.DrawApi
---@field assets torirs.AssetsApi
---@field scene torirs.SceneApi
---@field panel torirs.PanelApi
---@field cache torirs.CacheApi
---@field client torirs.ClientApi
---@field game torirs.GameApi

---@class torirs.Plugin
---@field id string Stable plugin id.
---@field title? string
---@field version? string
---@field config? torirs.ConfigItem[]
---@field ui_contributions? torirs.UiContribution[]
---@field frames? torirs.FrameOffer[] Static offers published before startup.
---@field on_start? fun(api: torirs.Api)
---@field on_stop? fun(api: torirs.Api)
---@field on_frame_start? fun(api: torirs.Api, ev: torirs.FrameEvent)
---@field on_logic_tick? fun(api: torirs.Api, ev: torirs.TickEvent)
---@field on_server_tick? fun(api: torirs.Api, ev: torirs.TickEvent)
---@field on_world_loaded? fun(api: torirs.Api, ev: torirs.WorldLoadedEvent)
---@field on_screen_changed? fun(api: torirs.Api, ev: torirs.ScreenChangedEvent)
---@field on_npc_spawn? fun(api: torirs.Api, npc: torirs.NpcSnap)
---@field on_npc_retype? fun(api: torirs.Api, npc: torirs.NpcSnap)
---@field on_npc_despawn? fun(api: torirs.Api, npc: torirs.NpcSnap)
---@field on_item_spawn? fun(api: torirs.Api, item: torirs.ItemSnap)
---@field on_item_changed? fun(api: torirs.Api, item: torirs.ItemSnap)
---@field on_item_despawn? fun(api: torirs.Api, item: torirs.ItemSnap)
---@field on_config_changed? fun(api: torirs.Api, key: string)
---@field on_asset? fun(api: torirs.Api, ev: torirs.AssetEvent)
---@field on_chat_message? fun(api: torirs.Api, ev: torirs.ChatMessageEvent)
---@field on_game_event? fun(api: torirs.Api, ev: torirs.GameEvent)
---@field on_key? fun(api: torirs.Api, ev: torirs.KeyEvent): torirs.Verdict
---@field on_menu_build? fun(api: torirs.Api, ev: torirs.MenuBuildEvent): torirs.Verdict
---@field on_menu_select? fun(api: torirs.Api, ev: torirs.MenuSelectEvent): torirs.Verdict
---@field on_draw_world? fun(api: torirs.Api, draw: torirs.DrawBuilder)
---@field on_draw_canvas? fun(api: torirs.Api, draw: torirs.DrawBuilder)
---@field on_ui_build? fun(api: torirs.Api, panel: torirs.PanelBuilder, view: torirs.PanelView)
---@field on_ui_action? fun(api: torirs.Api, ev: torirs.PanelActionEvent)
---@field on_ui_draw? fun(api: torirs.Api, node: string, draw: torirs.DrawBuilder)
---@field on_placement_changed? fun(api: torirs.Api, revision: integer)
---@field on_ui_node_draw? fun(api: torirs.Api, node: torirs.UiNodeRef, draw: torirs.DrawBuilder)
---@field on_ui_node_action? fun(api: torirs.Api, node: torirs.UiNodeRef, action: string): torirs.Verdict
---@field on_canvas_action? fun(api: torirs.Api, ev: torirs.CanvasActionEvent): torirs.Verdict
---@field on_ui_layout? fun(api: torirs.Api, ev: torirs.PanelLayoutEvent)
