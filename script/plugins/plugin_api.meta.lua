---@meta
--
-- Type definitions for the client's plugin api.
--
-- This file is never loaded: `---@meta` marks it as declaration-only, and
-- plugins.ini lists the scripts the client runs by hand, so nothing here is
-- reachable at runtime. It exists so an editor running the Lua language
-- server (LuaLS / the "Lua" extension, sumneko.lua) can complete `api.` and
-- `draw.`, and flag a handler that was given the wrong arguments.
--
-- It is a hand-written mirror of src/plugin/torirs_plugin_lua.c -- the tables
-- built in lua_build_api_table() and the payloads pushed by
-- lua_push_event_arg(). A field added there and not here is invisible to the
-- editor; a field here and not there completes and then reads nil. Change
-- them together.
--

---------------------------------------------------------------- sandboxing --
--
-- The five libraries the host opens are base, string, table, math and utf8
-- (lua_open_sandbox_libs); io, os, package, debug and coroutine are not
-- linked at all, and are switched off for the editor in .luarc.json.
--
-- These five survive the library selection and are removed by name, because
-- every one of them reaches lauxlib's fopen-backed loader. Redeclared here so
-- calling one is struck through in the editor rather than discovered as a
-- runtime error in a plugin that already shipped.

---@deprecated Removed by the plugin sandbox. Use api.log().
---@type nil
print = nil

---@deprecated Removed by the plugin sandbox. A script is handed to the host as bytes; there is no second way in.
---@type nil
require = nil

---@deprecated Removed by the plugin sandbox.
---@type nil
load = nil

---@deprecated Removed by the plugin sandbox.
---@type nil
loadfile = nil

---@deprecated Removed by the plugin sandbox.
---@type nil
dofile = nil

------------------------------------------------------------------- colours --

--- A colour is either 0xRRGGBB as an integer, or a string the host parses with
--- strtoul: "#00FFFF" or "00FFFF". Alpha is never part of it -- fill strength
--- is the separate 0..255 argument the draw calls take.
---@alias torirs.Colour integer|string

--- What a handler returns. `true`, "consume" or "drop" stops propagation to
--- the next subscriber, and on the events that document themselves as
--- interceptable (key, menu, packets) also suppresses the engine's default.
--- Anything else -- including no return at all -- passes.
---@alias torirs.Verdict boolean|string|nil

----------------------------------------------------------------- snapshots --

--- One player, as of the moment the api was called. A copy, not a handle:
--- holding it across frames gives stale numbers, not a live view.
---@class torirs.PlayerSnap
---@field name string
---@field true_x integer Tile the server believes the player stands on.
---@field true_z integer
---@field level integer
---@field fine_x integer Interpolated position, in 1/128ths of a tile.
---@field fine_z integer
---@field dest_x integer Far end of the route queue; equals true_x when idle.
---@field dest_z integer
---@field flag_x integer Minimap click latch, or -1. The only record of a click the server has not echoed yet.
---@field flag_z integer
---@field server_pid integer
---@field element_id integer Scene element id, for draw.hull().
---@field combat_level integer

--- One npc, as of the moment the api was called.
---@class torirs.NpcSnap
---@field name string
---@field server_slot integer
---@field npc_id integer Current id -- the active multinpc rung, not the shell.
---@field base_npc_id integer The id as spawned.
---@field combat_level integer
---@field size integer Footprint in tiles; true_x/true_z is the SW corner.
---@field true_x integer
---@field true_z integer
---@field level integer
---@field fine_x integer
---@field fine_z integer
---@field element_id integer Scene element id, for draw.hull().
---@field visible_ops integer Bitmask of the ops the minimenu would show.

--- One ground-item stack, as of the moment the api was called. One per
--- (tile, obj) pair: a tile holding three different items yields three.
---@class torirs.ObjSnap
---@field name string As the right-click builder sees it, colour tags and all.
---@field obj_id integer The item on top of the stack.
---@field count integer
---@field cost integer ObjType.cost -- the cache's base value for ONE of them, the same number CS2 reads through OC_COST. Not a live Grand Exchange price; nothing in the client knows one. 0 when the objtype is not resident.
---@field value integer cost * count, precomputed.
---@field tile_x integer
---@field tile_z integer
---@field level integer
---@field element_id integer Scene element id, for draw.hull().

------------------------------------------------------------------ the apis --

--- Handed to every handler as its first argument.
---@class torirs.Api
---@field log fun(...: any) Writes to stderr, prefixed with the plugin name. Values are stringified with __tostring.
---@field world_cycle fun(): integer
---@field frame_ms fun(): integer
---@field local_player fun(): torirs.PlayerSnap? nil before login and while the world is rebuilding.
---@field npcs fun(): fun(): torirs.NpcSnap? Generic-for iterator: `for npc in api.npcs() do`. One value per step, not ipairs' pair.
---@field players fun(): fun(): torirs.PlayerSnap? `for player in api.players() do`
---@field npc_by_slot fun(server_slot: integer): torirs.NpcSnap?
---@field objs fun(): fun(): torirs.ObjSnap? `for obj in api.objs() do`
---@field key_held fun(key: torirs.KeyName|integer): boolean
---@field project fun(fine_x: integer, fine_z: integer, height?: integer): integer?, integer? Fine world position to screen x, y. nil when it is off-screen or behind the camera.
---@field cfg_set fun(key: string, value: string|number|boolean) The only way to write config; the table itself is read-only.
---@field config table<string, any> Live view of this plugin's settings, typed by the schema: a `color` reads back as an integer, a `bool` as a boolean. Reading a key the plugin never declared is an error, not nil.
---@field asset_load fun(name: string): boolean Begin loading one of this plugin's own files. True means it is already resident; false means a read was queued and on_asset will fire. `name` is a bare filename of [A-Za-z0-9._-] -- a path is refused, so one plugin can never read another's.
---@field asset_data fun(name: string): string? The resident bytes, or nil while pending / after a failure. Binary-safe: Lua strings count their bytes and may contain NULs.
---@field asset_save fun(name: string, data: string): boolean Replace `name` and queue the write. The resident copy is updated before returning, so asset_data() answers with the new bytes at once.
---@field asset_release fun(name: string) Drop the resident copy. The file is untouched.
---@field object_create fun(): integer? A world object owned by this plugin, inactive and with no model yet. nil when the plugin is at its object budget.
---@field object_destroy fun(object: integer)
---@field object_model fun(object: integer, id: integer, kind?: '"model"'|'"spotanim"') `model` is a raw model id; `spotanim` is a spotanimtype, drawn with its own recolours, resize, angle and seq. Defaults to "model".
---@field object_recolor fun(object: integer, hsl_from: integer, hsl_to: integer) Append a recolour pair, in packed HSL -- see api.hsl().
---@field object_clear_recolors fun(object: integer) Drop every pair and rebuild from the cache copy.
---@field object_anim fun(object: integer, seq_id: integer, loop?: boolean) -1 leaves the bind pose. Defaults to looping.
---@field object_light fun(object: integer, ambient: integer, contrast: integer) OFFSETS against the client's actor light profile, not absolute values. 0, 0 is the default.
---@field object_position fun(object: integer, tile_x: integer, tile_z: integer, level: integer, height?: integer, yaw?: integer) ABSOLUTE tile, so the object survives a scene rebuild. `height` is 1/128ths of a tile above the ground; `yaw` is 0..2047.
---@field object_active fun(object: integer, active: boolean)
---@field object_ready fun(object: integer): boolean True once the model is built and the object is in the scene. Model loads are asynchronous, exactly as they are for the client's own graphics.
---@field hsl fun(rgb: integer): integer 0xRRGGBB to the packed HSL a model face is actually coloured with.
---@field rgb fun(hsl: integer): integer The inverse, through the palette the rasteriser uses.
---@field hsl_pack fun(hue: integer, saturation: integer, luminance: integer): integer Hue 0-63, saturation 0-7, luminance 0-127.
---@field hsl_unpack fun(hsl: integer): integer, integer, integer

--- The names key_held() understands. Any other key is passed as its
--- enum LibToriRS_KeyCode integer.
---@alias torirs.KeyName
---| '"shift"'
---| '"ctrl"'
---| '"space"'
---| '"tab"'
---| '"escape"'

--- Handed to on_draw_world as its second argument, and legal only there --
--- every call checks the open surface and raises otherwise. `fill` is 0..255
--- and 0 means outline only.
---@class torirs.Draw
---@field tile fun(tile_x: integer, tile_z: integer, level: integer, colour: torirs.Colour, fill?: integer)
---@field hull fun(element_id: integer, colour: torirs.Colour, fill?: integer) Convex hull of a scene element -- the element_id off a snapshot.
---@field line fun(x0: integer, y0: integer, x1: integer, y1: integer, colour: torirs.Colour) Screen space; see api.project().
---@field text fun(x: integer, y: integer, text: string, colour: torirs.Colour)
---@field rect fun(x: integer, y: integer, w: integer, h: integer, colour: torirs.Colour, fill?: integer)

------------------------------------------------------------ event payloads --

---@class torirs.EvFrame
---@field now_ms integer

---@class torirs.EvTick
---@field cycle integer

---@class torirs.EvWorld
---@field base_tile_x integer SW corner of the loaded scene, in world tiles.
---@field base_tile_z integer

---@class torirs.EvPacketIn
---@field name integer Server prot opcode.
---@field size integer

---@class torirs.EvPacketOut
---@field builder string

---@class torirs.EvKey
---@field key integer enum LibToriRS_KeyCode.
---@field ch integer Typed character, or 0.
---@field down boolean

--- One built minimenu row.
---@class torirs.MenuRow
---@field text string Includes the reference colour tags (@yel@ and friends).
---@field action integer RevConfig action id, already normalized.
---@field pick_kind integer enum UIMinimenuPickKind.
---@field npc_slot integer Server slot when the row targets an npc, else -1.
---@field player_pid integer Server pid when the row targets a player, else -1.
---@field target_id integer Loc/obj id when the row targets one, else -1.

---@class torirs.EvMenuBuild
---@field hover_pass boolean True for the hover-text rebuild, which runs EVERY FRAME. A handler that only cares about the right-click menu should return immediately.
---@field rows torirs.MenuRow[]
---@field add fun(text: string, tag?: integer): boolean Append a row. The tag is an integer of the plugin's choosing, handed straight back on select.

---@class torirs.EvMenuSelect
---@field tag integer The tag passed to add(), meaningful only when `owned`.
---@field owned boolean True when the selected row is this plugin's.
---@field x integer Click position.
---@field y integer
---@field row torirs.MenuRow

---@class torirs.EvConfig
---@field key string

---@class torirs.EvAsset
---@field name string The name passed to api.asset_load().
---@field size integer Bytes now resident; 0 when the read failed.
---@field ok boolean False when the asset does not exist or could not be read. A plugin is told either way, so a load never has to be timed out.

------------------------------------------------------------- the manifest --

--- One entry in a plugin's config schema. It drives the settings panel and
--- the type api.config reads back.
---@class torirs.ConfigItem
---@field key string
---@field type? '"bool"'|'"int"'|'"color"'|'"colour"'|'"enum"'|'"string"' Defaults to "string".
---@field default? string|number|boolean Always stored as a string; a bool default is "0"/"1".
---@field label? string Shown in the panel; the key is the fallback.
---@field min? integer `int` only.
---@field max? integer
---@field choices? string `enum` only.

--- The table a plugin script returns.
---
--- Handlers are discovered by name -- an absent one is simply not subscribed,
--- so a script pays nothing for the events it does not use. They are called
--- as plain functions, so declare them with a dot (`function plugin.on_frame`)
--- and not a colon.
---@class torirs.Plugin
---@field name? string Falls back to the file name. It is the ini section and the panel row, and two plugins cannot share it.
---@field version? string
---@field config? torirs.ConfigItem[]
---@field on_start? fun(api: torirs.Api): torirs.Verdict
---@field on_stop? fun(api: torirs.Api): torirs.Verdict
---@field on_frame? fun(api: torirs.Api, ev: torirs.EvFrame): torirs.Verdict
---@field on_logic_tick? fun(api: torirs.Api, ev: torirs.EvTick): torirs.Verdict
---@field on_server_tick? fun(api: torirs.Api, ev: torirs.EvTick): torirs.Verdict
---@field on_world_loaded? fun(api: torirs.Api, ev: torirs.EvWorld): torirs.Verdict
---@field on_npc_spawn? fun(api: torirs.Api, npc: torirs.NpcSnap): torirs.Verdict
---@field on_npc_retype? fun(api: torirs.Api, npc: torirs.NpcSnap): torirs.Verdict
---@field on_npc_despawn? fun(api: torirs.Api, npc: torirs.NpcSnap): torirs.Verdict
---@field on_packet_in? fun(api: torirs.Api, ev: torirs.EvPacketIn): torirs.Verdict Returning "drop" suppresses the packet.
---@field on_packet_out? fun(api: torirs.Api, ev: torirs.EvPacketOut): torirs.Verdict Returning "drop" suppresses the packet.
---@field on_key? fun(api: torirs.Api, ev: torirs.EvKey): torirs.Verdict
---@field on_menu_build? fun(api: torirs.Api, ev: torirs.EvMenuBuild): torirs.Verdict
---@field on_menu_select? fun(api: torirs.Api, ev: torirs.EvMenuSelect): torirs.Verdict
---@field on_draw_world? fun(api: torirs.Api, draw: torirs.Draw): torirs.Verdict
---@field on_config_changed? fun(api: torirs.Api, ev: torirs.EvConfig): torirs.Verdict
---@field on_obj_spawn? fun(api: torirs.Api, obj: torirs.ObjSnap): torirs.Verdict
---@field on_obj_count? fun(api: torirs.Api, obj: torirs.ObjSnap): torirs.Verdict A stack's count changed in place; `obj` carries the new one.
---@field on_obj_despawn? fun(api: torirs.Api, obj: torirs.ObjSnap): torirs.Verdict The last known state; fires before the entity is released.
---@field on_asset? fun(api: torirs.Api, ev: torirs.EvAsset): torirs.Verdict
