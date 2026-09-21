---@meta
-- LuaLS mirror of the native plugin API major 3. The runtime/meta inventory test
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

---@deprecated Removed so an instruction-budget error cannot be caught and retried forever.
---@type nil
pcall = nil
---@deprecated Removed so an instruction-budget error cannot be caught and retried forever.
---@type nil
xpcall = nil
---@deprecated Removed so scripts cannot install unbudgeted __gc finalizers.
---@type nil
setmetatable = nil
---@deprecated Removed so protected runtime metatables cannot be mutated.
---@type nil
getmetatable = nil
---@deprecated Removed; use api.core.log so output follows the client log policy.
---@type nil
warn = nil

---@alias torirs.Colour integer|string
---@alias torirs.ResultName 'ok'|'not_found'|'pending'|'unsupported'|'conflict'|'budget'|'invalid'|'error'
---@alias torirs.AssetState 'pending'|'ready'|'missing'|'invalid'|'budget'|'error'
---@alias torirs.Verdict boolean|'consume'|nil
---@alias torirs.PanelView 'page'|'settings'
---@alias torirs.Surface 'viewport'|'minimap'|'sidebar'|'chat'|'chat_buttons'|'modal'|'compass'|'orbs'|integer
---@alias torirs.FrameBuildResultName 'ready'|'pending'|'unsupported'|'error'|'native'
---@alias torirs.KeyName 'shift'|'ctrl'|'space'|'tab'|'escape'
---@alias torirs.ImageRef integer
---@alias torirs.ModelRef integer
---@alias torirs.MeshRef integer
---@alias torirs.SceneInstanceRef integer

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

---@class torirs.GameframeEvent
---@field offer_id string This plugin's offer id.
---@field active boolean Selected and being laid out (true) or released (false).
---@field canvas "fixed"|"window"
---@field width integer Logical canvas width the frame is laid out against.
---@field height integer
---@field safe torirs.Rect The canvas less what the platform is covering (the soft keyboard band), in canvas pixels; the whole canvas when no band is up. Hang a bottom strip from safe.y + safe.height. The event is raised again when it changes. The lane's own popout strip (lane_chrome_0) is not subtracted; find and subtract that widget yourself.

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
---@field action 'activate'|'toggle'|'text'|'pick'|'drag'|'scroll'|'key'|'menu'|'unknown'
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
---@field scene_origin fun(): integer?, integer? Current southwest scene corner in absolute tiles, or nil without a world; works on mid-session enable.
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

---@class torirs.MenuApi
---@field add fun(text: string, action_id: integer): boolean Only during on_menu_build; retain the intended operation, not an unchecked native slot.

---@class torirs.FrameApi
---@field offer_next fun(cursor?: integer): integer?, torirs.FrameOfferInfo?
---@field selection fun(): torirs.FrameSelection
---@field select fun(id: string): boolean, torirs.ResultName
---@field invalidate fun()
---@field surface_native_size fun(surface: torirs.Surface): integer?, integer?
---@field surface_member_native_box fun(surface: torirs.Surface, member: integer): integer?, integer?, integer?, integer? The authored box of one numbered MEMBER of that surface, relative to the surface's own block. The role's own numbering; -1 is refused.
---@field surface_fit fun(surface: torirs.Surface, width: integer, height: integer): integer?, integer? The largest box inside width x height that surface lays itself out to fill -- never below its authored size, always on a whole line. nil when the surface has only its authored size (the 2004 chat builtin is the one that grows) or the box is smaller than it.
---@field surface_center_content fun(surface: torirs.Surface, centered: boolean): boolean, torirs.ResultName Lay what the lane mounts into that surface (a 2004 chat dialog) out at the surface's authored size, centred in the box the frame gave it. From on_gameframe only; holds until stated false or the frame is released.
---@field native_layout fun(): integer Which native top-level chrome the lane wears now: 0 fixed, 1 resizable-classic, 2 resizable-modern, 3 mobile, -1 unknown or between a request and its remount.
---@field native_layout_select fun(layout: integer): boolean, torirs.ResultName Ask the lane for one of its own chromes (0..2), the way its Display row does. From on_gameframe or a player's own action only. true means asked, not yet changed; the remount arrives as a new frame root.

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

---@class torirs.FrameOffer
---@field id string Local stable id; the catalogue exposes `<plugin-id>/<id>`.
---@field title string
---@field canvas 'fixed'|'window'
---@field width? integer Required for fixed canvas.
---@field height? integer Required for fixed canvas.
---@field min_width? integer Required for window canvas.
---@field min_height? integer Required for window canvas.

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
--- The row's NAME, not its value. key_value, toggle, select and action_row are
--- built from a label and carry their value in a second string, so set_text
--- cannot restate one -- on those four it is the reading, the chosen entry or
--- the summary. A kind whose single string already travels as `text` refuses
--- this rather than taking a second spelling for it.
---@field set_label fun(id: string, label: string): boolean, torirs.ResultName
---@field set_value fun(id: string, value: integer|boolean): boolean, torirs.ResultName
---@field set_height fun(id: string, preferred_height: integer): boolean, torirs.ResultName
---@field set_options fun(id: string, value: string, options: torirs.SelectOption[]): boolean, torirs.ResultName
---@field redraw fun(id: string)
--- Mint one row a new identity, so a click authored against the picture it
--- used to show is refused. For a row whose INPUT identity changed while the
--- page's row sequence did not -- a custom well whose y-to-item mapping moved.
--- Changing only a caption or a value is set_text/set_value, not this.
---@field reidentify fun(id: string): boolean, torirs.ResultName
--- The reader's place, in logical pixels of page scrolled past the top.
--- -1 when no page of this plugin's is up, which is NOT 0 -- the top of one
--- that is. The host carries the place across a rebuild of the same page by
--- itself; these are for MOVING it.
---@field scroll fun(): integer
---@field scroll_to fun(scroll: integer): boolean, torirs.ResultName

---@class torirs.CacheApi
---@field frame_root fun(): integer
---@field varbit fun(id: integer): integer
---@field varp fun(id: integer): integer
---@field component_rect fun(component_id: integer): torirs.Rect?
---@field invoke fun(component_id: integer, operation: integer): boolean
---@field named_id fun(kind: string, name: string): integer?
---@field tab_active fun(): integer
---@field tab_enabled fun(tab: integer): boolean
---@field tab_flash_hidden fun(tab: integer): boolean
---@field tab_select fun(tab: integer): boolean

---@class torirs.ClientApi
---@field display_get fun(setting: integer): integer?, integer?, integer?
---@field display_set fun(setting: integer, value: integer): boolean, torirs.ResultName
---@field display table<string, integer> TORIRS_DISPLAY_* setting numbers for display_get/display_set, by lower-case name (renderer_active, render_width, ...).
---@field renderer_label table<integer, string> What each renderer is called, keyed by the display_get(display.renderer_active) value (0 = Software).
---@field feature_next fun(cursor?: integer): integer?, torirs.Feature?
---@field feature_get fun(key: string): integer?
---@field feature_set fun(key: string, value: integer): boolean, torirs.ResultName
---@field world_cycle fun(): integer
---@field datestamp fun(): string?
---@field setting_color fun(varp_id: integer, fallback?: integer): integer
---@field memory_bytes fun(): integer
---@field disable_self fun(reason: string)
---@field plugin_window_open fun(): boolean Is the client's shared plugin window on screen?
---@field plugin_window_show fun(open: boolean) Open or close it. For a plugin that has taken over the launcher, which in practice means a gameframe.

---@class torirs.Feature
---@field key string
---@field label string
---@field section string
---@field kind integer
---@field value integer
---@field min integer
---@field max integer
---@field choices string
---@field values integer[] Numeric value corresponding to each choice label.
---@field value_count integer
---@field is_default boolean True when the current value comes from the revision default.

---@class torirs.Skill
---@field index integer
---@field name string
---@field current_level integer
---@field base_level integer
---@field xp integer
---@field level_xp integer
---@field next_level_xp integer
---@field stated boolean True when the server has stated this skill, so every number above is a reading. api.game.skill returns nil while a skill has no reading, so a snapshot you hold is stated.

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

---@class torirs.Graphics
---@field rect fun(x: integer, y: integer, width: integer, height: integer, rgb: torirs.Colour, alpha?: integer)
---@field line fun(x0: integer, y0: integer, x1: integer, y1: integer, rgb: torirs.Colour, alpha?: integer)
---@field text fun(x: integer, y: integer, text: string, rgb?: torirs.Colour)
---@field image fun(image: torirs.ImageRef, x: integer, y: integer, alpha?: integer)
---@field world_tile fun(tile_x: integer, tile_z: integer, level: integer, fill_rgb: torirs.Colour, outline_rgb?: torirs.Colour, alpha?: integer, outline_width?: integer): boolean, torirs.ResultName the wash is `alpha` and the border `outline_width` (default 2, 0 draws no border at all)
---@field world_hull fun(element_id: integer, rgb: torirs.Colour, alpha?: integer, shape?: 'bounds'|'mesh'|integer): boolean, torirs.ResultName
---@field image_clip fun(image: torirs.ImageRef, x: integer, y: integer, clip: torirs.Rect, alpha?: integer)
---@field context fun(): torirs.DrawContext?

---@class torirs.PanelBuilder
---@field heading fun(text: string)
---@field paragraph fun(text: string)
---@field toggle fun(id: string, label: string, value: boolean)
---@field select fun(id: string, label: string, value: string, options: torirs.SelectOption[])
---@field button fun(id: string, label: string, enabled?: boolean)
---@field custom fun(id: string, preferred_height?: integer) Low-level bitmap surface; do not use for ordinary text, lists, stats, or settings.
---@field label fun(id: string, text: string)
---@field key_value fun(id: string, label: string, value: string)
---@field action_row fun(id: string, label: string, summary?: string) Full-width native navigation row; activation reports `action == 'activate'`.
---@field node fun(node: torirs.PanelNode): boolean, torirs.ResultName

---@class torirs.PanelNode
---@field kind integer
---@field id string
---@field label? string
---@field text? string
---@field value? integer
---@field preferred_height? integer
---@field options? torirs.SelectOption[]

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

---@class torirs.ConfigItem
---@field key string
---@field type? 'bool'|'int'|'color'|'colour'|'string'|'enum'|'text'
---@field label? string
---@field default? boolean|integer|string
---@field min? integer
---@field max? integer
---@field choices? string
---@field rows? integer

---@class torirs.WidgetActionRef

---@class torirs.WidgetAction
---@field label string Current native action label.
---@field ref torirs.WidgetActionRef Checked retained action.

---@class torirs.Widget
---@field visible fun(self:torirs.Widget):boolean?
---@field actions fun(self:torirs.Widget):torirs.WidgetAction[]?
---@field create_text fun(self:torirs.Widget,key:string):torirs.Widget? Creates or returns this owner's child.
---@field set_text fun(self:torirs.Widget,text:string):boolean,string Owned text only.
---@field set_text_color fun(self:torirs.Widget,color:torirs.Colour):boolean,string Owned text only.
---@field set_text_outline fun(self:torirs.Widget,outline:boolean):boolean,string Native or owned text; false resumes native shadow style.
---@field set_text_align fun(self:torirs.Widget,horizontal:integer,vertical:integer):boolean,string 0=start, 1=center, 2=end; owned text only.
---@field create_image fun(self:torirs.Widget,key:string):torirs.Widget? Creates or returns this owner's keyed image child.
---@field set_image fun(self:torirs.Widget,image:integer,width:integer,height:integer):boolean,string Owned image controls take one of this plugin's live images and a size; a native sprite, graphic or compass with size 0,0 takes the image as a retained re-skin while it shows a graphic of its own (size stays the widget's; non-zero is invalid_argument).
---@field set_mask fun(self:torirs.Widget,image:integer?):boolean,string Native minimap, compass or sprite only: retained clip where the image is transparent; nil removes the native mask.
---@field set_opacity fun(self:torirs.Widget,opacity:integer):boolean,string Owned widgets only; 255 opaque, 0 invisible.
---@field set_anchor fun(self:torirs.Widget,target:torirs.Widget?,relation:"native"|"over"|"behind"|"replace"):boolean,string Retained depth relation to another widget: drawn and hit directly over it, behind it, or in its place (replace inherits the target's native visibility both ways). "native" with a nil target clears this owner's relation; self, ancestor/descendant pairs and cycles are invalid_argument.
---@field set_on_op fun(self:torirs.Widget,op:integer,label:string?,callback:fun(widget:torirs.Widget,event:torirs.WidgetOperationEvent)?):boolean,string Owned controls only. Arms menu operation `op` (1..10, numbered as the cache numbers a component's; 1 is the left-click default and the rows read down the menu in op order) with this label, delivered through the native hit test and retained-menu checks. One callback answers for every row of a control and the event carries which was chosen; replacing it retires earlier menu rows. A nil callback clears that one operation and leaves the others armed.
---@field remove fun(self:torirs.Widget):boolean,string Removes only this owner's widget.
---@field position fun(self:torirs.Widget):torirs.Rect? Native-parent-local, unscrolled geometry.
---@field bounds fun(self:torirs.Widget):torirs.Rect? Drawn canvas geometry, including scroll/drag.
---@field state fun(self:torirs.Widget):torirs.WidgetState? Everything a follower needs in one read; the host also raises 'state_changed' on a watch when any of it moves.
---@field children fun(self:torirs.Widget):torirs.Widget[]?
---@field parent fun(self:torirs.Widget):torirs.Widget? Native parent; not a presentation reparent.
---@field set_projection_height fun(self:torirs.Widget,height:integer):boolean,string World-unit lift for an anchored overlay layer; camera projection stays native.
---@field text fun(self:torirs.Widget):string? Current native text input.
---@field set_position fun(self:torirs.Widget,x:integer,y:integer):boolean,string
---@field move_after fun(self:torirs.Widget,sibling:torirs.Widget):boolean,string Owned controls only: move this control to directly after `sibling` among their shared parent's children, its place in the draw order.
---@field set_canvas_position fun(self:torirs.Widget,x:integer,y:integer):boolean,string Owned controls only: the box at canvas x,y under whatever parent the control was created in; the parent decides draw order, this decides screen position. Retained until set_position.
---@field set_size fun(self:torirs.Widget,width:integer,height:integer):boolean,string
---@field set_hidden fun(self:torirs.Widget,hidden:boolean):boolean,string Presentation only; native hiding remains authoritative. Reset/disable reveals current native state or another owner's edit.
---@field revalidate fun(self:torirs.Widget):boolean,string
---@field reset fun(self:torirs.Widget):boolean,string Releases only this plugin's edits.

---@class torirs.WidgetState
---@field x integer Drawn canvas geometry.
---@field y integer
---@field width integer
---@field height integer
---@field local_x integer Native-parent-local, unscrolled geometry.
---@field local_y integer
---@field local_width integer
---@field local_height integer
---@field presented boolean Paints this frame; the same answer as :visible().
---@field own_hidden boolean The node's own hide bit: a CS2 if_sethide or a dat1 IF_SETTAB.
---@field native_hidden boolean The engine's native suppression, not the script's.
---@field input_present boolean Reachable by the native hit test as the lane left it; a plugin's own hiding is not folded in.
---@field graphic_token integer Change token for a node that carries art, 0 for one that does not. Never an identity.
---@field paints_own_art boolean This node or something below it paints a PICTURE this frame -- what a REPLACE of it would consume. Text does not count. Zero graphic_token does not answer this: a button's art is on a child.
---@field text_hash integer FNV-1a 64 of a text node's string, 0 for a non-text node.
---@field facets integer What the LANE says about this widget, as a mask of api.widgets.facet values. 0 is 'no', never 'unknown'.
---@field facet torirs.WidgetFacets The same bits already unpacked.
---@field incarnation integer The reference's incarnation.

--- What the lane says about a widget, as opposed to what the tree says about the node.
--- A bit is set only for a widget the facet is about -- `selected` is meaningless on the
--- compass -- and a facet this revision cannot derive reads false.
---@class torirs.WidgetFacets
---@field given boolean A sidebar tab the lane has given the player. Clear means the icon must not be drawn: the click would open nothing.
---@field selected boolean The sidebar tab currently showing.
---@field flashing boolean The sidebar tab the game flagged to flash. The flag, not the blink -- the half-second gap is the frame's to draw.
---@field drawn boolean The minimap or compass surface is permitted to paint. Not `presented`, which is whether this node draws.
---@field oriented boolean The compass rose is live, so north is readable. Reported on the minimap too: one MINIMAP_TOGGLE mode governs both.
---@field walkable boolean A click on the minimap walks. Clear in the modes that draw the map and refuse the step.
---@field active boolean This orb's toggle is on: run is on, or the special attack is armed.
---@field hidden_by_cutscene boolean A cutscene has folded the gameplay HUD away. Set on every watched widget -- a plugin's own decoration goes with it.

--- The mask constants behind torirs.WidgetState.facets, for code that tests `facets`
--- directly rather than reading the unpacked `facet` table.
---@class torirs.WidgetFacetBits
---@field given integer
---@field selected integer
---@field flashing integer
---@field drawn integer
---@field oriented integer
---@field walkable integer
---@field active integer
---@field hidden_by_cutscene integer

---@class torirs.WidgetBindingEvent
---@field kind 'bound'|'unbound'|'tree_changed'|'state_changed'
---@field role string
---@field native_revision integer

---@class torirs.WidgetOperationEvent
---@field kind 'operation'
---@field operation integer Always 1 for an owned control's single operation.
---@field native_revision integer The registration that produced this operation.

---@class torirs.WidgetsApi
---@field invoke fun(action:torirs.WidgetActionRef):boolean, string? Rechecks current native visibility, masks and widget identity.
---@field watch fun(role:string,callback:fun(widget:torirs.Widget,event:torirs.WidgetBindingEvent)?):boolean,string Follows native binding identity; nil removes this subscription.
---@field watch_state fun(role:string,callback:fun(widget:torirs.Widget,event:torirs.WidgetBindingEvent)?):boolean,string As watch, and also raises 'state_changed' once per fence in which the bound widget's native state moved (read it with widget:state()); a plain watch never does.
---@field find fun(role:string):torirs.Widget?
---@field find_all fun(role:string):(torirs.Widget|false)[]? All current matches in the role's own numbering: t[m+1] is member m, false where this frame has no member m; count is one past the highest present. Skip false entries when iterating. ground_item_labels is unavailable without the native CS2 overlay adapter.
---@field watch_tree fun(callback:fun(widget:torirs.Widget?,event:torirs.WidgetBindingEvent)?):boolean,string Initial and topology-change publication notifications; nil unregisters. The callback receives nil widget and queries current references.
---@field get fun(component_id:integer):torirs.Widget? Revision-specific lookup.
---@field facet torirs.WidgetFacetBits The mask constants behind torirs.WidgetState.facets.

---@class torirs.ScriptRef
---@class torirs.ScriptEvent
---@field name string
---@field script_id integer
---@field widget? torirs.Widget The live widget at an approved hook site.
---@field ref torirs.ScriptRef Valid only during this synchronous callback.
---@class torirs.ScriptsApi
---@field available fun():boolean False on CS1/revconfig adapters.
---@field invalidate fun(callback_name:string):boolean Rebuild cached native results at the next safe point, including during shutdown.
---@field counts fun(ref:torirs.ScriptRef):integer,integer Integer and string stack sizes.
---@field get_int fun(ref:torirs.ScriptRef,index:integer):integer Index 0 is the top remaining slot.
---@field set_int fun(ref:torirs.ScriptRef,index:integer,value:integer):boolean
---@field get_string fun(ref:torirs.ScriptRef,index:integer):string
---@field set_string fun(ref:torirs.ScriptRef,index:integer,value:string):boolean At most 16384 bytes, no embedded NUL.

---@alias torirs.PorcelainInput 'element'|'asset'|'config'|'screen'|'canvas'|'explicit'
---@alias torirs.PorcelainCadence 'logic_tick'|'server_tick'|'frame'
---@alias torirs.PorcelainBind 'bound'|'absent'|'pending'
---@alias torirs.PorcelainPlacementKind 'replace'|'inside'|'beside'|'at_element'|'at_canvas'|'at_usable'|'within'
---@alias torirs.PorcelainCorner 'top_left'|'top_right'|'bottom_left'|'bottom_right'|'centre'
---@alias torirs.PorcelainSide 'left'|'right'|'above'|'below'
---@alias torirs.PorcelainDerivedState 'ready'|'pending'|'failed'
---@alias torirs.PorcelainFindingName 'absent'|'refused'|'arbitration_lost'|'asset_missing'|'asset_error'|'derived_failed'|'budget'|'unsupported'
---@alias torirs.PorcelainFace 'page'|'settings'|'both'
---@alias torirs.PorcelainRowKind 'heading'|'paragraph'|'label'|'key_value'|'toggle'|'select'|'button'|'action_row'|'separator'|'progress'|'custom'


--- The Porcelain layer: describe what should exist relative to NAMED ELEMENTS
--- of the game UI, and the client keeps that description true as the cache's
--- scripts, the server and the engine change those elements.
---
--- Every verb has the same NAME as its C counterpart on `api->porcelain`.
--- Only the arity is shorter: a Lua script is one plugin, so the handle is the
--- script's own and never an argument -- the same shortening `api.widgets.get`
--- already takes. Call `open` once from `on_start`.
---
--- `open` installs the pump. The runtime fences before your on_frame_start and
--- commits after it, notes 'config' before your on_config_changed, and
--- forwards the server tick before your on_server_tick -- so a Lua plugin
--- writes none of that and cannot half-write it. A fence with no commit is a
--- `fence without commit` finding, which is the failure the hand-written pump
--- made possible. `on_asset` is NOT pumped: the layer polls its own pending
--- images at every fence and stamps the asset input itself.
---
--- So do NOT call `fence`, `commit`, `note('config')` or `tick('server_tick')`
--- from a Lua plugin. Fencing twice in one frame is what the finding above
--- names, and the API inventory test refuses any script/plugins/*.lua that
--- spells one. The verbs remain on this table because the C library has no
--- pump and reaches them by hand.
---
--- The pump costs a plugin with nothing to reconcile nothing at all: no
--- description, no timer, no asset and no armed key edge is ZERO engine calls
--- and ZERO allocations per frame, which is why there is no opt-out.
---
--- An element is named by a small grammar: "minimap", "orb:run",
--- "chat_filter:3", "tab:inventory", "panel:inventory", "role:my_role".
--- An element a lane does not have answers `bind == 'absent'` with one
--- finding; it is never a guess and never a lineage test.
---@class torirs.PorcelainApi
---@field open fun(): boolean Open the layer for this plugin, and INSTALL ITS PUMP. False when the host did not install the layer.
---@field close fun() Remove every owned control, release every image, reset every edit, drop every claim.
---@field describe fun(fn: fun(d: torirs.PorcelainDescribe)) The one describe function. Re-run only when an input moved.
---@field invalidate fun() Re-run describe at the next fence.
---@field note fun(input: torirs.PorcelainInput) One of this plugin's inputs moved.
---@field fence fun() Reconcile this plugin. `open` installs this: the runtime fences BEFORE your on_frame_start.
---@field commit fun() Flush every fenced plugin with EXACTLY ONE layout resolve. `open` installs this too, AFTER your on_frame_start.
---@field relinquish fun() Drop every claim, before arbitration runs again.
---@field element fun(element: string): torirs.PorcelainElementState
---@field count fun(family: string): integer How many members this LANE has. Never a picked number.
---@field set fun(key: string, motion: torirs.PorcelainMotion): boolean, torirs.ResultName The direct per-frame path.
---@field findings fun(): torirs.PorcelainFinding[]
---@field expect_absent fun(element: string, why: string) Legal at any time. Fails loudly in BOTH directions.
---@field expect_unsupported fun(feature: string, why: string) Declare a lane limitation. One expected finding, and every later unsupported finding naming it is expected too.
---@field note_key fun(key: integer, down: boolean) Forward this plugin's own on_key; key_edge takes the edge from it.
---@field has fun(capability: string): boolean
---@field require fun(capability: string, feature: string): boolean False turns the feature off and records one finding.
---@field tier fun(tiers: torirs.PorcelainTiers, value: integer): integer Strictly greater; a threshold at or below zero disables its tier.
---@field tiers_from_config fun(): torirs.PorcelainTiers? This plugin's own four keys.
---@field config_list_add fun(key: string, item: string): boolean Measures before joining; refuses rather than truncating.
---@field menu_tag fun(subject: integer, op: integer): integer Subject and intent frozen into the retained row.
---@field setting fun(varbit_name: string, inverted?: boolean): boolean Absent is OFF, with one finding across many reads.
---@field setting_value fun(name: string, absent: integer): integer The same named row as a NUMBER; name is `varbit:x`, `varp:x` or a bare varbit name. `absent` is this feature's OFF answer and is not optional.
---@field key_edge fun(config_key: string, fn: fun(down: boolean)): boolean False on a touch lane, with one finding.
---@field image fun(name: string): integer?, torirs.AssetStateName
---@field image_size fun(name: string): integer?, integer The picture's own size, or nil while it is not READY.
---@field model fun(name: string): nil, torirs.AssetStateName A model handle has no Lua representation; the STATE is the answer.
---@field derived fun(key: string, inputs: string, width: integer, height: integer, paint: fun(w: integer, h: integer): integer[]?): integer?, torirs.PorcelainDerivedState
---@field when_ready fun(what: string, fn: fun(what: integer)) Comma-separated: game, world, stats, player, derived.
---@field every fun(cadence: torirs.PorcelainCadence, fn: fun(elapsed_ms: integer))
---@field every_server_tick fun(fn: fun()) Fires on EVERY lane; there is no synthesised cadence.
---@field every_ms fun(milliseconds: integer, fn: fun(elapsed_ms: integer)) Re-registering the same handler RE-INTERVALS it; it does not append.
---@field cancel_every fun(fn: fun()) Drop the timer registered for this handler.
---@field tick fun(cadence: torirs.PorcelainCadence) Forward this plugin's own tick callback. `open` already forwards 'server_tick'.
---@field counters_read fun(): torirs.PorcelainCounters Engine calls and allocations this handle has made since the last reset.
---@field counters_reset fun() Zero them, so a steady-state assertion starts from a known point.
---@field draw_context fun(element?: string): torirs.PorcelainDrawContext? The drawable rect of the pass now running, and an element's box on it.
---@field menu_add fun(text: string, action_id: integer): boolean menu.add, with the refusal recorded as a finding.
---@field note_menu fun() From on_menu_build: stamps the hovered cell on the hover pass.
---@field hover fun(): torirs.PorcelainHover? The hovered cell, while it is still live.
---@field native_overlay fun(labels_role: string, callback: string, fn: fun(name: string): boolean?) The suppress-then-format latch over a lane's own caption script.
---@field note_script fun() From on_script_callback: drives the latch and routes the caption.
---@field table fun(asset: string, parse: fun(bytes: string): boolean?): boolean Read, parse and release a shipped data file, once.
---@field notify fun(kind: string, subject: integer, text: string) One announcement per (kind, subject) per frame.
---@field panel fun(descriptor?: torirs.PorcelainPanel) on_start only. Register the shared pane and say which faces the description covers.
---@field panel_build fun(view?: 'page'|'settings') Forward on_ui_build. Declares nothing on a face the description does not cover.
---@field panel_action fun(event: torirs.PanelAction): boolean Forward on_ui_action. False when no described row owns the id.
---@field panel_draw fun(node: string): boolean Forward on_ui_draw. False when no described CUSTOM row paints it.
---@field panel_restate fun(key: string) The HOST's copy of ONE row drifted -- a refused pick, which the host commits before it dispatches. That row's setters, and not the page.
---@field panel_scroll fun(): integer The reader's place; -1 when no page of this plugin's is up.
---@field panel_scroll_to fun(scroll: integer) Move it. Clamped by the presenter's next layout, never here.
---@field hull fun(element_id: integer, rgb: integer|string, alpha?: integer, shape?: 'bounds'|'mesh'): boolean draw.world_hull with both refusals recorded: false means nothing was drawn.
---@field tile fun(tile_x: integer, tile_z: integer, level: integer, fill_rgb: integer|string, outline_rgb?: integer|string, alpha?: integer, outline_width?: integer): boolean draw.world_tile with its budget refusal recorded: false means the footprint was cut short. `outline_width` defaults to 2; 0 draws no border.
---@field finding fun(verb: string, element: string|nil, result: torirs.PorcelainFindingName|integer, detail?: string) This plugin's OWN finding, in the channel Porcelain's verbs already use.
---@field menu_untag fun(tag: integer): integer, integer The inverse of menu_tag, so the operations-per-subject constant lives in one place.
---@field key_down fun(key: string): boolean Is this key held NOW. The edge form's VALUE vocabulary: a name, a decimal code, or one character.
---@field config_list_remove fun(key: string, item: string): boolean Take one item out of a stored list. Absent is true and costs no write.
---@field config_list_set fun(key: string, items: string[]): boolean State the whole list: sorted, deduplicated, refused rather than truncated.
---@field frame fun(offer_id: string, canvas: 'fixed'|'window', min_width: integer, min_height: integer, fn: fun(d: torirs.PorcelainDescribe)) Bind a description to one offer this plugin's definition publishes. Boot only.
---@field frame_event fun(event: torirs.GameframeEvent): torirs.FrameBuildResultName, string Forward on_gameframe. A release (active = false) runs a description that stages nothing, which is what takes the frame back off.
---@field frame_native fun(event: {offer_id: string}): torirs.FrameBuildResultName, string The 'native' answer: the lane's own chrome is the offer, so the description stages nothing and every retained edit comes off. Return its word from on_gameframe, after frame.native_layout_select answered true.
---@field usable fun(): torirs.PorcelainBox? The canvas a frame may lay out in: the frame root less a PRESENTED lane strip spanning a full edge. nil before anything of this lane has bound.
---@field native_size fun(element: string): torirs.PorcelainBox? The box the LANE authored for that element, before any plugin edit. A member answers block-relative x and y; a whole surface answers 0, 0. nil when the lane states no pixel box.
---@field lane_icon fun(tab: string): integer This lane's own number for that tab's panel, or -1 where the lane numbers none or mounts none.
---@field tab_group_count fun(axis: 'rows'|'columns'): integer How many rows or columns of stones this root lays out.
---@field tab_group fun(axis: 'rows'|'columns', group: integer): string[] The tabs of one group, as element specs, in the root's own order.
---@field tab_detached fun(): string? The tab this root hangs outside every group, or nil.

--- The describe builder, handed to the describe function and legal only
--- inside it. Items are applied in DESCRIPTION ORDER and a later item is over
--- an earlier one by default.
---@class torirs.PorcelainDescribe
---@field control fun(item: torirs.PorcelainItem) Picture, operation and hit box. `image = nil` is an invisible hit box.
---@field piece fun(item: torirs.PorcelainItem) Picture only: no operation, no hit box.
---@field text fun(item: torirs.PorcelainItem) Text in an EXPLICIT box: nothing measures a string.
---@field blocker fun(item: torirs.PorcelainItem) An invisible, armed hit box.
---@field move fun(element: string, box: torirs.PorcelainBox, anchor_modes?: integer) Omit x or y to keep it block-relative.
---@field hide fun(element: string) A presentation hide. Never an unhide.
---@field raise fun(element: string, over?: string, behind?: boolean) Where a NATIVE element sits in the draw order. Omit `over` for "above everything this plugin owns", which is the frame provider's sentence.
---@field skin fun(element: string, image?: string, mask?: string) Independent halves.
---@field opacity fun(element: string, opacity: integer)
---@field unsupported fun(reason: string) This feature cannot run on this lane. One finding, no items.
---@field row fun(row: torirs.PorcelainRow) One panel row. The ordered (key, kind, identity label) sequence IS the declaration.
---@field reidentify fun(key: string) Mint ONE described row a new serial: growth without a page rebuild.

---@class torirs.PorcelainItem
---@field key string Stable identity. A key not re-described is removed.
---@field image? string Asset or derived key. nil means "exists, draws nothing".
---@field place torirs.PorcelainPlace
---@field w? integer
---@field h? integer
---@field opacity? integer 255 opaque .. 1 barely there. NOT 0: an absent field reads as 0, so 0 is UNSET and means opaque; pass -1 for invisible. A fade that counts down to 0 is reported as a finding rather than silently snapping back to fully painted.
---@field text? string
---@field rgb? integer
---@field align? integer 0 left, 1 centre (the default), 2 right.
---@field outline? boolean
---@field op_label? string
---@field on_op? fun(key: string)
---@field hit? boolean
---@field enabled? boolean False is drawn, inert, no menu row.
---@field visible_with? string An extra presented-gate: OVER inherits nothing.

--- What Porcelain_Panel registers. `icon_asset` nil asks for the baked
--- wrench, which is a meaning and not an absence.
---@class torirs.PorcelainPanel
---@field icon_asset? string
---@field width? integer Default 320.
---@field faces? torirs.PorcelainFace Default 'both'.

--- One described panel row.
---
--- A zeroed row is a legal, live heading, which is why the inert spelling is
--- `disabled` rather than `enabled`.
---
--- The declaration identity is (key, kind). `label` USED to be part of it for
--- key_value, toggle, select and action_row, because the host's patch path had
--- no arm that restated one and renaming one cost a page rebuild; panel
--- set_label exists now, so a rename is a setter like any other property. For
--- heading, paragraph, label and button the string travels as the row's text,
--- so either spelling works there.
---@class torirs.PorcelainRow
---@field key string Stable identity, and the id every action and setter names.
---@field kind torirs.PorcelainRowKind
---@field label? string
---@field text? string
---@field value? integer toggle: the checked state. progress: the bar.
---@field options? torirs.SelectOption[] select only. Copied: a stack local is fine.
---@field height? integer custom: the well's logical height.
---@field disabled? boolean button: drawn dim, pressing it does nothing.
---@field hit_key? integer custom: the y-to-item identity. A VALUE change must not be in it.
---@field paint_key? integer custom: what the next paint will draw. A change to it is one redraw.
---@field on_action? fun(key: string, action: torirs.PanelAction)
--- custom only: a SECONDARY click in the well, at `action.x`/`y`. Its own slot
--- and never a fall-through from on_action -- a row that asked for clicks only
--- must not be handed a right click as one.
---@field on_menu? fun(key: string, action: torirs.PanelAction)
---@field paint? fun(key: string, draw: torirs.Graphics) custom only.

---@class torirs.PorcelainPlace
---@field kind torirs.PorcelainPlacementKind
---@field on? string The element this item belongs to.
---@field depth? string Sit over (or behind) THIS element instead.
---@field sibling_of? string at_canvas/at_usable only: create the control under THIS element's parent (its place in the tree), keeping the canvas box.
---@field behind? boolean
---@field corner? torirs.PorcelainCorner For kind 'inside' and kind 'within'.
---@field side? torirs.PorcelainSide For kind 'beside'.
---@field dx? integer
---@field dy? integer

---@class torirs.PorcelainBox
---@field x? integer
---@field y? integer
---@field width? integer
---@field height? integer

--- What the pass now running may draw on. `bounds` and `clip` are the
--- callback's own, pass-local; `element` is a canvas-space answer and is
--- filled only when `canvas_space` is true, which the world and canvas
--- passes are and a panel well is not. For the usable canvas -- a frame
--- provider's question, and a cost every other caller used to pay -- ask
--- porcelain.usable().
---@class torirs.PorcelainDrawContext
---@field bounds torirs.Rect
---@field clip torirs.Rect
---@field element torirs.Rect
---@field element_bound boolean
---@field canvas_space boolean

--- The hovered container cell, from the menu build's hover pass. `container`
--- is 'inv', 'worn', 'bank', 'other' or 'none'; `container_id` is the cell's
--- own `(interface << 16) | component`, so two containers this vocabulary
--- cannot name are still two keys.
---@class torirs.PorcelainHover
---@field obj integer
---@field container string
---@field container_id integer
---@field slot integer
---@field frame integer

---@class torirs.PorcelainElementState
---@field bind torirs.PorcelainBind
---@field ok boolean
---@field presented boolean Every veto folded in.
---@field own_hidden boolean The node's own hide bit.
---@field native_hidden boolean The engine's native suppression.
---@field input_present boolean
---@field graphic_token integer A CHANGE token, never an identity.
---@field paints_own_art boolean This element or something below it paints a PICTURE -- what a REPLACE of it consumes. Text does not count.
---@field facets integer
---@field incarnation integer
---@field box torirs.PorcelainBox Canvas space.
---@field local_box torirs.PorcelainBox Parent-local, unscrolled.

---@class torirs.PorcelainMotion
---@field x? integer
---@field y? integer
---@field opacity? integer
---@field image? string

---@class torirs.PorcelainFinding
---@field verb string
---@field result integer
---@field detail string
---@field expected boolean
---@field first_frame integer
---@field count integer

--- What the layer has actually spent. The steady-state rule -- an unchanged
--- description makes zero engine calls and zero allocations -- is a reading
--- from here, not a belief: `property_applies` counts items whose property set
--- was walked AT ALL, so "no engine call because the hash matched" is
--- distinguishable from "every per-field compare happened to match".
---@class torirs.PorcelainCounters
---@field engine_calls integer
---@field allocations integer
---@field describe_runs integer
---@field creates integer
---@field removes integer
---@field setters integer
---@field revalidates integer
---@field property_applies integer

---@class torirs.PorcelainTiers
---@field low integer
---@field medium integer
---@field high integer
---@field insane integer

---@class torirs.Api
---@field widgets torirs.WidgetsApi
---@field scripts torirs.ScriptsApi
---@field core torirs.CoreApi
---@field config torirs.ConfigApi
---@field world torirs.WorldApi
---@field input torirs.InputApi
---@field menu torirs.MenuApi
---@field frame torirs.FrameApi
---@field draw torirs.DrawApi
---@field assets torirs.AssetsApi
---@field scene torirs.SceneApi
---@field panel torirs.PanelApi
---@field cache torirs.CacheApi
---@field client torirs.ClientApi
---@field game torirs.GameApi
---@field porcelain torirs.PorcelainApi
---@field drive torirs.DriveApi @testonly Present only when ContentTest_Enabled(): the quest driver's engine seam.

--[[ The quest driver's test-only module (src/plugin/torirs_plugin_drive.h).

It is registered by src/plugin/torirs_plugin_drive.c through
PluginLua_SetTestModules and exists only in a process started with
TORIRS_CONTENT_TEST, which is why the field above carries @testonly: the
inventory test pins its surface like any other module, but does not require it
to be in the canonical module set a shipped plugin may use.

The table is FLAT and is assembled from six files, one per owner
(docs/ARCHITECT.md).  Names must not collide across those files; the ordering
of the groups below is the order they are registered in.

Every verb answers (result, detail) with result in
ok|timeout|not_found|refused|covered|no_row|not_visible|closed|unsupported,
except the plain readers marked as returning a value.
]]
---@class torirs.DriveApi
--- core-scheduler
---@field await fun(descriptor: table, deadline_ticks: integer): string, any Yield until the descriptor is satisfied; deadline in SERVER TICKS.
---@field pump fun() Resume the coroutine while its await is satisfied. Called once per frame from on_frame_start.
---@field events fun(after_serial: integer): string, table Drive event ring, by cursor. `refused` when the cursor fell off the end.
---@field tick fun(): integer The world cycle: the unit every deadline is counted in.
---@field settled fun(): boolean No async pending, frame settled, no world load in flight.
---@field symbol fun(kind: string, name: string): string, integer Content symbol to id. Never a literal id in a test.
---@field symbol_name fun(kind: string, id: integer): string, string Id back to the content symbol, so a test compares names.
---@field cheat fun(text: string): string, string Run a debugproc in-process and return its verdict: ran|failed|none.
---@field ledger fun(row: table): string, string Append one ledger row and mirror it to stderr.
---@field report fun(text: string) The stderr mirror on its own, for a note that is not a step.
---@field finish fun(code: integer): string, string End the process with this code at the next frame boundary.
---@field session fun(): table { dir, script }: where artefacts land and which quest is running.
--- core-state
---@field varp fun(varp_id: integer): string, integer
---@field varbit fun(varbit_id: integer): string, integer
---@field varbit_base fun(varbit_id: integer): string, integer The base varp, because var events carry a varp id.
---@field var_server fun(varp_id: integer): string, integer The client's record of the SERVER's value; not the same read as varp.
---@field varbit_server fun(varbit_id: integer): string, integer The varbit-width var_server: the bits varbit would read, out of the server record instead of var[].
---@field var_content fun(varp_id: integer): string, integer The EMBEDDED SERVER's own copy, not the client's arrays at all. For an id the client's varp table cannot address -- never transmitted, so varp and var_server both answer not_found forever. unsupported on a socket-server run. It cannot see a desync; a row that reads it says so.
---@field inv_count fun(container_id: integer, obj_id: integer): string, integer
---@field inv_slot fun(container_id: integer, slot: integer): string, table
---@field inv_capacity fun(container_id: integer): string, integer
---@field skill fun(stat_index: integer): string, table { level, base_level, experience, stated }.
---@field messages fun(count: integer): string, table Chat lines newest first, including clan chat and the logout line.
---@field message_serial fun(): string, integer The serial an await must be scoped above.
--- verbs-chat
---@field modal_group fun(): string, integer The interface mounted under chat_modal_host.
---@field resume fun(component_id: integer): string, string Arm the resume-pausebutton seam: the only way a dialogue row is clicked.
---@field pause_pending fun(): string, integer The component a resume is outstanding on, or -1. Compared by id, never by presence.
---@field close_modal fun(): string, string Idempotent.
---@field meslayer_mode fun(): string, integer Which prompt the chat input is, if any. Typing blind would send a public message.
---@field click_armed fun(component_id: integer): string, boolean Does this component's effective IF_SETEVENTS carry CLICK.
---@field options fun(): string, table { title, rows }: chatmenu's cc_create'd title and rows, no content symbol exists for either.
---@field option_row fun(row: integer): string, integer A live chatmenu row's component id, for arming resume; row is 1..5.
--- verbs-read
---@field widget_model fun(component_id: integer): string, table { kind, id }: the raw identity the server sent, not the composite.
---@field widget_text fun(component_id: integer): string, string A text component's string, "" if it has none or is not a text node.
---@field widget_presented fun(component_id: integer): string, boolean Visible right now: not display-hidden and natively visible.
---@field widget_own_hidden fun(component_id: integer): string, boolean What the cache or a script said, independent of native hiding. not_found reads as hidden.
--- verbs-pointer
---@field screen_position fun(kind: string, id: integer): string, table { x, y, element_id }.
---@field pick_holds fun(element_id: integer): string, boolean Does this frame's pickset hold it. Meaningless before a frame rendered at the moved-to point.
---@field pick_point fun(): string, table { valid, x, y, view_x, view_y, view_w, view_h }: WHICH pixel the pickset above was hittested at, so a held=false is a reading of a rendered frame rather than a guess about timing. view_w 0 means there is no world rectangle to test a candidate pixel against.
---@field mouse_move fun(x: integer, y: integer): string, string
---@field mouse_button fun(button: integer, down: boolean, x: integer, y: integer): string, string
---@field menu_visible fun(): string, boolean
---@field menu_rows fun(): string, table
---@field menu_row_find fun(action: integer, kind: string, target_id: integer): string, table Action < 0 is the wildcard the collapsed use-item row needs.
---@field action_for_slot fun(kind: string, slot: integer): string, integer The action id the client's own builder would use for that op.
---@field world_op fun(kind: string, id: integer, option: integer): string, string The LOGGED bypass. Never the default; every call is a ledger note.
---@field op_available fun(kind: string, id: integer, option: integer): string, boolean Does this world target actually OFFER that op? app_minimenu_ui_pick_live validates only UI and INV_SLOT picks, so the bypass owes its own answer.
---@field inv_op fun(component_id: integer, slot: integer, obj_id: integer, count: integer, option: integer): string, string A backpack/worn CELL's numbered held op. 1..5 = OPHELD1..5, 0 = Examine, negative arms the held-item selection (Use) and is refused unless objsel came back holding it. refused ALSO when the client itself declined the pick, and the detail is then the sentence naming which condition -- nothing was dispatched, so a retry cannot double-send.
---@field inv_arm fun(component_id: integer, slot: integer, obj_id: integer, count: integer): string, string The same cell's Use arming, taken WHATEVER is armed now -- which inv_op(..., -1) cannot do: with a selection live it is encoded as an OPHELDU of the item on itself and leaves nothing armed. An arming already live for this cell sends nothing and says so.
---@field inv_use_on fun(component_id: integer, slot: integer, obj_id: integer, count: integer): string, string The CLICKED cell of an item-on-item (OPHELDU); the armed one is already in app->objsel, put there by inv_op(..., -1). The client encodes the use itself. no_row for a cell used on itself, refused when nothing was armed or the client did not encode it.
---@field move_to fun(tile_x: integer, tile_z: integer): string, string
---@field move_near fun(kind: string, id: integer): string, string Re-issued every tick while pending: the target can walk.
---@field camera fun(yaw: integer, pitch: integer, zoom: integer): string, string
---@field player_idle fun(): string, boolean route_length 0 AND the map flag cleared; they settle a tick apart.
--- verbs-ui
---@field group_present fun(interface_id: integer): string, boolean Mount liveness, both lanes.
---@field component fun(symbol: string, sub: integer): string, integer Qualified "<iface>:<child>" symbol to a component id.
---@field if_click fun(component_id: integer, op: integer): string, string One path for IF1 button types and IF3 numbered ops.
---@field tab fun(tab_number: integer): string, string
---@field tab_by_name fun(name: string): string, integer|nil Tab NAME through app->revconfig_refs' "tab" kind (the [tabs] map, else a panel_<name> role); no_row for a name neither source declares.
---@field modal_live fun(): string, boolean Re-verified; modal_host_uid is never cleared on close.
---@field npcs fun(radius: integer): string, table Nearest first; names normalised of <col=..>.
---@field locs fun(radius: integer): string, table Each row carries loc_id (the id the MAP or a zone packet placed) AND resolved_loc_id (the multiloc child it currently draws as).
---@field loc_variants fun(loc_id: integer): string, table|nil { resolved, slots } -- the multiloc child this def draws as now, and its flattened family. `timeout` while the def is being fetched: poll again next frame.
---@field objs fun(radius: integer): string, table
---@field player_tile fun(): string, table { x, z, level }.
---@field key fun(name: string, down: boolean): string, string
---@field text fun(text: string): string, string 1..63 printable ASCII.
---@field shot fun(name: string, keep: boolean|nil): string, string, boolean Request a capture and await the file. A third return, true when the picture was byte-identical to the last one written and was deleted again -- the detail is then "unchanged since <name>", not a path. `keep` writes it regardless (t.exec's -FAIL shot).

---@class torirs.Plugin
---@field id string Stable plugin id.
---@field title? string
---@field version? string
---@field event_priority? integer Higher values receive ordinary events first.
---@field draw_order? integer Lower values draw first within a draw pass.
---@field config? torirs.ConfigItem[]
---@field frames? torirs.FrameOffer[] Static offers published before startup; each is served by on_gameframe.
---@field on_start? fun(api: torirs.Api)
---@field on_stop? fun(api: torirs.Api)
---@field on_frame_start? fun(api: torirs.Api, ev: torirs.FrameEvent)
---@field on_logic_tick? fun(api: torirs.Api, ev: torirs.TickEvent)
---@field on_server_tick? fun(api: torirs.Api, ev: torirs.TickEvent)
---@field on_script_callback? fun(api:torirs.Api,event:torirs.ScriptEvent) Synchronous; cannot yield or invoke native scripts.
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
---@field on_gameframe? fun(api: torirs.Api, ev: torirs.GameframeEvent): ("ready"|"pending"|"unsupported"|boolean)?, string? Frame provision through the widget API: with ev.active the script's declared frame offer is the selected gameframe and is laid out against ev.width x ev.height by editing widgets (move, hide, skin, anchor); return nothing or "ready", or "pending"/"unsupported" with a reason. Raised again on every canvas change; ev.active=false announces the release before teardown.
---@field on_menu_build? fun(api: torirs.Api, ev: torirs.MenuBuildEvent): torirs.Verdict
---@field on_menu_select? fun(api: torirs.Api, ev: torirs.MenuSelectEvent): torirs.Verdict
---@field on_draw_world? fun(api: torirs.Api, draw: torirs.Graphics)
---@field on_draw_canvas? fun(api: torirs.Api, draw: torirs.Graphics)
---@field on_ui_build? fun(api: torirs.Api, panel: torirs.PanelBuilder, view: torirs.PanelView)
---@field on_ui_action? fun(api: torirs.Api, ev: torirs.PanelActionEvent)
---@field on_ui_draw? fun(api: torirs.Api, node: string, draw: torirs.Graphics)
---@field on_ui_layout? fun(api: torirs.Api, ev: torirs.PanelLayoutEvent)
