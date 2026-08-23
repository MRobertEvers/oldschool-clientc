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
---@field dest_x integer Where the walk ends -- the map flag, which lives from the click to the arrival. Equals true_x when there is no destination. Local player only: every other player reports itself.
---@field dest_z integer
---@field flag_x integer The same flag, unfolded: -1 when there is no destination at all, which dest_x cannot say. Local player only.
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

--- One loc (scenery) in the loaded scene: the door, the tree, the rock. Not
--- an "obj" -- that is a ground ITEM everywhere in this tree, and the two are
--- the pair that get confused.
---
--- Scene-scoped, unlike an npc or a ground item: a loc outside the loaded
--- scene is simply not in the list, and the list is rebuilt from nothing on
--- every scene load. A script that wants to remember one remembers its
--- absolute tile and finds it again.
---@class torirs.LocSnap
---@field name string As the minimenu shows it, colour tags and all.
---@field loc_id integer
---@field tile_x integer Absolute tile of the SW corner.
---@field tile_z integer
---@field level integer
---@field size_x integer Route footprint -- the config size, angle-swapped. Ground decor routes as its full size but draws on one tile, so this is not what the model covers.
---@field size_z integer
---@field shape integer Placed loc shape.
---@field angle integer Rotation, 0..3 = W/N/E/S.
---@field element_id integer Scene element id, for draw.hull().
---@field interactive boolean False for a wall, gravel or floor decor that nothing can click. A highlighter that ignores this lights up the pavement.
---@field visible_ops integer Bitmask of the ops the minimenu would show.

------------------------------------------------------------------ the apis --

--- Handed to every handler as its first argument.
---@class torirs.Api
---@field log fun(...: any) Writes to stderr, prefixed with the plugin name. Values are stringified with __tostring.
---@field world_cycle fun(): integer
---@field frame_ms fun(): integer
---@field memory_bytes fun(): integer Current client memory footprint in bytes (resident process memory on desktop; the live WebAssembly heap in a browser). Returns 0 when unavailable.
---@field local_player fun(): torirs.PlayerSnap? nil before login and while the world is rebuilding.
---@field npcs fun(): fun(): torirs.NpcSnap? Generic-for iterator: `for npc in api.npcs() do`. One value per step, not ipairs' pair.
---@field players fun(): fun(): torirs.PlayerSnap? `for player in api.players() do`
---@field npc_by_slot fun(server_slot: integer): torirs.NpcSnap?
---@field objs fun(): fun(): torirs.ObjSnap? `for obj in api.objs() do`
---@field locs fun(): fun(): torirs.LocSnap? `for loc in api.locs() do`. A busy city scene holds thousands, so test loc_id or name inside the walk rather than collecting the list first.
---@field key_held fun(key: torirs.KeyName|integer): boolean
---@field hover_tile fun(): integer?, integer?, integer? Absolute tile under the mouse pointer, as x, z, level. nil when the pointer is outside the world viewport or over no terrain. One frame stale at most -- it comes from the pick that rides the render.
---@field hover_entity fun(): torirs.HoverEntity? The nearest ENTITY under the pointer -- the one a left click would act on. nil over open ground, which hover_tile still answers for.
---@field element_height fun(element_id: integer): integer Where an overhead hangs above an element, in the projector's units -- the anchor the client's own health bars and chat heads use. Feed it to project() as `height`. 200 for an element whose model is not built yet, 0 for one not in the scene.
---@field varbit fun(varbit_id: integer): integer The client's live varbit. READ ONLY -- a varp is the server's, and writing one would tell the client something the server never said. 0 for an id this revision does not define.
---@field varp fun(varp_id: integer): integer
---@field setting_color fun(varp_id: integer, fallback?: integer): integer An All Settings colour row, as 0xRRGGBB. Those rows store `colour + 1` so that zero can mean "never chosen"; this drops the offset and answers `fallback` for the unset case.
---@field project fun(fine_x: integer, fine_z: integer, height?: integer): integer?, integer? Fine world position to screen x, y. nil when it is off-screen or behind the camera.
---@field cfg_set fun(key: string, value: string|number|boolean) The only way to write config; the table itself is read-only.
---@field config table<string, any> Live view of this plugin's settings, typed by the schema: a `color` reads back as an integer, a `bool` as a boolean. Reading a key the plugin never declared is an error, not nil.
--- A numeric or colour setting is stored as text and read as an integer EXPRESSION, the same grammar a revconfig profile uses: `12`, `0x1F`, `1Fh`, `0b1010`, `#FF8000`, `1 << 4`, `rgb(255, 128, 0)`, `rgba(0, 0, 0, 128)`, `hsl16(hue, sat, lum)`. Text that is not one whole expression reads as 0.
---@field image_load fun(name: string): integer? A PICTURE this plugin ships, out of the same asset folder asset_load reads, decoded by the host: hand the handle to draw.image. nil when the name is refused or the plugin is at its image budget. ASYNCHRONOUS -- image_size answers nil and draw.image draws nothing until the read lands, so lay out against image_size and skip a frame rather than waiting.
---@field image_size fun(image: integer): integer?, integer? The picture's `w, h`, or nil while the read is still in flight.
---@field image_release fun(image: integer) Drop it. The file is untouched, and every image is dropped for you when the plugin stops.
---@field component_rect fun(component_id: integer): torirs.Rect? Where one of the INTERFACE's own widgets is, by `(interface << 16) | component` -- the read half of a component id, for the buttons the gameframe's roles do not cover. A cache frame's chat filters are interface widgets, so api.layout.chat_buttons has no members to number there and this is the only handle on them. nil for a component this cache does not have or an interface that is not open.
---@field mouse_pos fun(): integer?, integer? The pointer in canvas coordinates, or nil when the client has none. What a hover highlight is drawn from -- a hit region answers CLICKS, not hovers.
---@field role torirs.RoleFn Address an interface element by what it IS -- `api.role("report_button")` -- and get back a table of verbs bound to that name. The missing name component_rect's caveat is about; see torirs.Role.
---@field hit_region fun(x: integer, y: integer, w: integer, h: integer, ops?: string|string[], tag?: integer): boolean Claim a box of the canvas so what was drawn there can be clicked. Legal only inside on_draw_canvas, and DECLARED WITH THE DRAWING every frame: the box comes from where the frame put things this frame, and a region registered once at start is a rectangle over whatever used to be there after the first resize. The first op is the mouseover line and the LEFT click; all of them are rows in the right-click menu. No ops claims the pointer without offering anything, which is how a plugin stops a click falling through to the world behind its own art. `tag` comes back in on_canvas_click.
---@field asset_load fun(name: string): boolean Begin loading one of this plugin's own files. True means it is already resident; false means a read was queued and on_asset will fire. `name` is a bare filename of [A-Za-z0-9._-] -- a path is refused, so one plugin can never read another's.
---@field asset_data fun(name: string): string? The resident bytes, or nil while pending / after a failure. Binary-safe: Lua strings count their bytes and may contain NULs.
---@field asset_save fun(name: string, data: string): boolean Replace `name` and queue the write. The resident copy is updated before returning, so asset_data() answers with the new bytes at once.
---@field asset_release fun(name: string) Drop the resident copy. The file is untouched.
---@field screenshot fun(name: string, dir?: string): boolean, string? Capture the client's frame as a PNG. Answers `ok, path` -- the path being where the file lands, resolved by the engine, which a script cannot work out from what it passed in; nil when the capture was refused. DEFERRED: taken at the top of the next frame, because every caller runs before the interface it wants has been laid out. `name` is a bare filename of [A-Za-z0-9._-]; ".png" is appended when it carries no extension. `dir` is a destination the USER named -- the one place the asset sandbox does not apply, since a screenshot is write-only and the path comes from a config field; `..` is still refused. ABSOLUTE (leading '/') is used as given; RELATIVE or absent lands under the plugin's own saved-asset folder, so subdirectories work on the browser lane too.
---@field datestamp fun(): string? Local wall-clock as "YYYY-MM-DD_HH-MM-SS". The only clock a script has with any relation to a calendar -- the sandbox does not link `os`, and frame_ms is monotonic. Filename-safe by construction, and the format RuneLite names screenshots with.
---@field model_load fun(name: string): integer? A model FILE this plugin ships, out of its own asset folder, decoded by the host. nil when the name is refused or the resident model table is full. The bytes travel with the plugin and the decoder reads the format off the file's own trailer, so one file draws the same under every cache -- unlike a cache model id, which means something different in every revision. Asynchronous: an object pointed at it is simply not in the scene until the read lands.
---@field mesh_create fun(): integer? A mesh this plugin authors itself, triangle by triangle. nil when the plugin is at its mesh budget. Geometry a plugin builds is the same shape on every cache revision, which a model id is not.
---@field mesh_destroy fun(mesh: integer)
---@field mesh_clear fun(mesh: integer) Drop every vertex and face, keeping the handle. Objects built from it rebuild.
---@field mesh_vertex fun(mesh: integer, x: integer, y: integer, z: integer): integer? Append a vertex in MODEL space -- x right, z forward, y NEGATIVE-UP, 128 units to a tile. Returns its index, nil at the 1024-vertex ceiling.
---@field mesh_face fun(mesh: integer, a: integer, b: integer, c: integer, hsl: integer, alpha?: integer): integer? Append a triangle over three vertex indices, coloured in packed HSL (see api.hsl). `alpha` is transparency, 0 opaque .. 253; vertex ORDER decides which way the face points. Returns its index, nil at the 2048-face ceiling.
---@field object_create fun(): integer? A world object owned by this plugin, inactive and with no model yet. nil when the plugin is at its object budget.
---@field object_destroy fun(object: integer)
---@field object_model fun(object: integer, id: integer, kind?: '"model"'|'"spotanim"'|'"mesh"'|'"asset"') `model` is a raw model id; `spotanim` is a spotanimtype, drawn with its own recolours, resize, angle and seq; `mesh` is a handle from api.mesh_create; `asset` is a handle from api.model_load. Defaults to "model".
---@field object_recolor fun(object: integer, hsl_from: integer, hsl_to: integer) Append a recolour pair, in packed HSL -- see api.hsl().
---@field object_clear_recolors fun(object: integer) Drop every pair and rebuild from the cache copy.
---@field object_anim fun(object: integer, seq_id: integer, loop?: boolean) -1 leaves the bind pose. Defaults to looping. A "mesh" object carries no rig for a sequence to drive, so binding one to it is an error; move it with object_position instead.
---@field object_light fun(object: integer, ambient: integer, contrast: integer) OFFSETS against the client's actor light profile, not absolute values. 0, 0 is the default.
---@field object_position fun(object: integer, tile_x: integer, tile_z: integer, level: integer, height?: integer, yaw?: integer) ABSOLUTE tile, so the object survives a scene rebuild. `height` is 1/128ths of a tile above the ground; `yaw` is 0..2047.
---@field object_active fun(object: integer, active: boolean)
---@field object_ready fun(object: integer): boolean True once the model is built and the object is in the scene. Model loads are asynchronous, exactly as they are for the client's own graphics.
---@field hsl fun(rgb: integer): integer 0xRRGGBB to the packed HSL a model face is actually coloured with.
---@field rgb fun(hsl: integer): integer The inverse, through the palette the rasteriser uses.
---@field hsl_pack fun(hue: integer, saturation: integer, luminance: integer): integer Hue 0-63, saturation 0-7, luminance 0-127.
---@field hsl_unpack fun(hsl: integer): integer, integer, integer
---@field layout torirs.Layout The gameframe's regions: where the scene, the chat, the sidebar and the modal area are, and how to take a bite out of the space beside them.

--- The names key_held() understands. Any other key is passed as its
--- enum LibToriRS_KeyCode integer.
---@alias torirs.KeyName
---| '"shift"'
---| '"ctrl"'
---| '"space"'
---| '"tab"'
---| '"escape"'

--- What draw.hull() wraps. "bounds" (the default) is the model's bounds
--- cylinder as a box: it never disappears, never cuts into the model, and
--- costs the same however dense the model is -- but anything with a part that
--- sticks out (a halberd, a cape, a wing) is wrapped at that reach on every
--- side, which reads as a square around the entity. "mesh" hulls the posed
--- geometry itself, so the outline is the entity's own edge, at one projection
--- per vertex. Prefer it for a few marked entities, "bounds" for a crowd.
---@alias torirs.HullShape
---| '"bounds"'
---| '"mesh"'


------------------------------------------------------------------- layout --

--- One region's box, in canvas pixels.
---@class torirs.Rect
---@field x integer
---@field y integer
---@field w integer
---@field h integer

--- Which side of a region a reservation eats.
---@alias torirs.Edge
---| '"left"'
---| '"right"'
---| '"top"'
---| '"bottom"'

--- The verbs every region carries. Which of them MEAN anything depends on the
--- region, and the host says no rather than the surface hiding them: `reserve`
--- works on `safe` and nothing else, because a placeable region is whatever
--- the frame says it is; `replace` is legal only for the plugin that owns the
--- frame, and only inside its layout handler.
---@class torirs.LayoutRegion
---@field rect fun(member?: integer): torirs.Rect? Where it is now, or nil when this gameframe has no such region. With a `member`, that one member of it -- the role's OWN numbering, so `chat_buttons.rect(3)` is the report abuse button and not the fourth chat button this cache happened to build (0 public, 1 private, 2 trade, 3 report; a sidebar mount's is its tab number). Answered live, so a region another plugin moved reads correctly on the very next call -- there is nothing to invalidate.
---@field reserve fun(edge: torirs.Edge, px: integer): boolean Take `px` off one side, for as long as this plugin runs. Many plugins may reserve and they STACK in the order they asked, so two docks on the same edge sit beside each other rather than on top of each other. `px` of 0 gives this plugin's back.
---@field release fun() Give back every edge this plugin took from this region. Happens automatically when the plugin stops.
---@field replace fun(rect: torirs.Rect): boolean State where the frame puts this region. The EXCLUSIVE verb: only the frame's owner may call it, and only from its layout handler.

--- The whole gameframe, as a region with the same three verbs.
---@class torirs.LayoutTopLevel
---@field rect fun(): torirs.Rect? The canvas.
---@field replace fun(opts?: {w: integer, h: integer}): boolean Own the frame: this plugin now draws the chrome and states where every region goes. With `w`/`h` the canvas is PINNED to that size and the window letterboxes it (what a 765x503 frame wants); without, the canvas follows the window and the layout is re-declared on every resize.
---@field release fun() Hand the frame back; the lane's own chrome returns on the next layout pass.

--- The gameframe's regions, addressed by what they ARE rather than by a
--- component id -- an id is a fact about one revision, and a role survives the
--- move from a 2004 frame to a modern one.
---
--- Reading is free and always current, so a plugin that only looks while
--- drawing needs no bookkeeping. One that CACHES something measured against a
--- region -- a composed image, a laid-out panel -- watches `revision` or
--- subscribes to on_layout_changed.
---@class torirs.Layout
---@field viewport torirs.LayoutRegion The 3D scene. On a fixed frame this is the play area; on a resizable one it is the whole window with the chrome floating over it.
---@field minimap torirs.LayoutRegion The map square itself, not the stone ring around it.
---@field compass torirs.LayoutRegion
---@field chat torirs.LayoutRegion The chat log and its input line.
---@field chat_buttons torirs.LayoutRegion The four filter buttons.
---@field sidebar torirs.LayoutRegion Whichever sidebar interface is open.
---@field main_modal torirs.LayoutRegion Where a bank, a level-up or a dialogue opens.
---@field modal_viewport torirs.LayoutRegion The same region as `main_modal`, under the other name people know it by.
---@field canvas torirs.LayoutRegion The whole client window. Never fails, which makes it the bottom of every fallback chain.
---@field safe torirs.LayoutRegion The largest part of the canvas no chrome is sitting on -- the scene, minus the minimap, the chatbox, the sidebar, and every reservation. DERIVED, so it stays right when a plugin nobody anticipated docks a panel down one side. This is the region a readout wants.
---@field top_level torirs.LayoutTopLevel
---@field revision fun(): integer Moves whenever anything about the layout does. Compare it against the value a cached picture was built at.


-------------------------------------------------------------------- roles --

--- One semantically-named element, bound once so the name is typed once.
---
--- Every verb answers "not here" for a role this revision has not bound, which
--- is an ANSWER and not an error: the same script runs on the lane whose
--- profile names the report button and on the one whose profile does not, and
--- offering no verb is what it should do on the second.
---@class torirs.Role
---@field rect fun(): torirs.Rect? Where the element is, in canvas pixels. nil when the role does not resolve, or resolves to something with no laid-out box.
---@field visible fun(): boolean Whether the player can actually see it -- the node and every ancestor, counting both the cache's own hiding and a gameframe layout's suppression. The verb that answers "is the logout screen up"; `rect` still returns a box for a hidden element, which is why they are separate.
---@field click fun(op?: integer): boolean Press it, exactly as a real click would: the local button behaviour, the varp it owns, and the packet the server is waiting for. `op` is the numbered operation 1..10, or 0 (the default) for the classic unnumbered button.
---@field id fun(): integer? The component id it resolves to RIGHT NOW, for handing to the id verbs. Do not keep it and do not compare two taken at different times: if the role names a component a CS2 script built, the id is recycled the next time that subtree is rebuilt and by then belongs to something else. Ask again. nil for a role that does not resolve, and also for one whose element carries no id of its own.

--- Elements addressed by what they ARE.
---
--- `api.layout` does this for the gameframe's REGIONS; this does it for the
--- things inside them, and for the same reason. `component_rect` and its
--- relatives take `(interface << 16) | component`, and their whole caveat is
--- that which number that is changes with the revision and no profile names
--- it. A role is that missing name: the revision's profile states
--- `[role:report_button]` and what it is bound to on that lane, and the script
--- asks for `"report_button"`.
---
--- Bind once and keep the table -- `local report = api.role("report_button")`
--- -- rather than spelling the name at every call site, the same way a region
--- is a name you index and not a string you pass.
---
--- The vocabulary is OPEN -- a profile may name anything -- but these are the
--- ones the client's own profiles ship, and the ones to reach for first:
---
--- REGIONS. Every `api.layout` region name works here too and answers the
--- identical rectangle: `"viewport"`, `"minimap"`, `"compass"`, `"chat"`,
--- `"sidebar"`, `"main_modal"`, `"chat_buttons"`, `"canvas"`, `"safe"`.
---
--- SIDEBAR. `"tab_<name>"` is the STONE you click; `"panel_<name>"` is the
--- interface mounted behind it. `<name>` is one of `combat`, `stats`,
--- `quests`, `inventory`, `equipment`, `prayer`, `magic`, `friends`, `ignore`,
--- `logout`, `options`, `emotes`, `music`, plus `clan` and `account` on the
--- OldSchool frames that have them. Which tab number any of these is stays in
--- the profile -- that is the fact that moves between revisions.
---
--- ELEMENTS. `"report_button"` and `"logout_screen"` (the same node as
--- `"panel_logout"`, under the name people ask for it by).
---
--- Not every lane binds every name, and that is the contract rather than a
--- gap: `panel_clan` is absent on a 2004 frame because that frame has no clan
--- tab, and `tab_<name>` is absent on a frame whose stones are the cache's own
--- widgets rather than the profile's.
---@alias torirs.RoleFn fun(name: string): torirs.Role

--- Handed to on_draw_world and on_draw_canvas as their second argument, and
--- legal only inside one of them -- every call checks the open surface and
--- raises otherwise. `tile` and `hull` are WORLD-only on top of that: both
--- name something in the scene, and the canvas has no scene behind it.
---
--- `fill` is 0..255 and 0 means outline only. On the canvas surface, `width`
--- and `height` are the canvas itself -- anything anchored to an edge is
--- `draw.width - something`, so the size rides on the table rather than
--- arriving as an argument every handler has to accept in order to use.
---@class torirs.Draw
---@field width integer Canvas width. Set for on_draw_canvas only.
---@field height integer Canvas height. Set for on_draw_canvas only.
--- A tile's wash has a colour of its own -- the marker is read against the
--- ground rather than against a model, so a bright outline can stay findable
--- while the fill only tints the tile. Both halves are given together: pass
--- the outline colour again for the old single-colour marker.
---@field tile fun(tile_x: integer, tile_z: integer, level: integer, colour: torirs.Colour, fill_colour?: torirs.Colour, fill?: integer)
---@field hull fun(element_id: integer, colour: torirs.Colour, fill?: integer, shape?: torirs.HullShape) Convex hull of a scene element -- the element_id off a snapshot.
---@field line fun(x0: integer, y0: integer, x1: integer, y1: integer, colour: torirs.Colour) Screen space; see api.project().
---@field text fun(x: integer, y: integer, text: string, colour: torirs.Colour)
---@field rect fun(x: integer, y: integer, w: integer, h: integer, colour: torirs.Colour, fill?: integer)
---@field image fun(image: integer, x: integer, y: integer, trans?: integer, clip_x?: integer, clip_y?: integer, clip_w?: integer, clip_h?: integer) Blit an image from api.image_load at its own size. `trans` is the reference's sense and NOT the `fill` above it: 0 is solid and 255 is invisible, matching every sprite blit in the client. The clip is an extra rectangle to cut the blit to, in this surface's coordinates -- what a meter is made of, and why it is an argument rather than a second verb. An image whose read has not landed draws nothing.

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

--- One line that reached the chatbox.
--- What the pointer is over: the nearest scenery / npc / player / ground-item
--- stack of the frame, which is the one a left click would act on.
---@class torirs.HoverEntity
---@field kind integer 1 scenery, 2 npc, 3 player, 4 ground-item stack.
---@field element_id integer Scene element id, for draw.hull().
---@field tile_x integer Absolute tile the pick landed on.
---@field tile_z integer
---@field level integer

--- One use of a row in the cache's own All Settings panel.
---@class torirs.EvSetting
---@field id integer The setting id -- struct param_1077, the number every settings hub switches on.
---@field value integer The row's new value, or -1 for a row whose hub carries none (every toggle, and the buttons this event exists for).

---@class torirs.EvChat
---@field type integer Protocol message type: 0 game, 2 public, 3 private-from, and so on.
---@field sender string Empty for a system line.
---@field text string As the chatbox holds it, colour tags and all.

--- One notable moment the client recognised.
---
--- `kind` is a string rather than a number because the recogniser
--- (src/game/rs_game_events.c) owns the list and a plugin's config lists these
--- by name; a kind added there reaches every plugin without an ABI change.
---@class torirs.EvGameEvent
---@field kind '"level_up"'|'"quest_complete"'|'"valuable_drop"'|'"untradeable_drop"'|'"boss_kill"'|'"pet"'|'"collection_log"'|'"combat_achievement"'|'"death"'|'"treasure_trail"'|'"duel_end"'
---@field subject string The skill, boss, quest or item; "" when the moment names none.
---@field value integer New level / kill count / coin value, or -1 when the kind carries none.
---@field text string The line it was recognised from, markup stripped. Empty for a level-up, which comes from UPDATE_STAT rather than from prose.

--- One of THIS plugin's canvas hit regions was used.
---
--- Raised for the plugin that declared the region and no other. The left-click
--- default and the region's row in the right-click menu both arrive here,
--- because to the plugin they are the same thing happening -- which of the two
--- the player did is not a question a button has an answer for.
---@class torirs.EvCanvasClick
---@field tag integer The `tag` the region was declared with.
---@field op integer Which of the region's ops was chosen, indexing the list it was declared with. A left click is always op 0.
---@field x integer Where the pointer was, in canvas coordinates.
---@field y integer

---@class torirs.EvConfig
---@field key string

---@class torirs.EvAsset
---@field name string The name passed to api.asset_load().
---@field size integer Bytes now resident; 0 when the read failed.
---@field ok boolean False when the asset does not exist or could not be read. A plugin is told either way, so a load never has to be timed out.

------------------------------------------------------------- the manifest --

--- One entry in a plugin's config schema. It drives the settings panel and
--- the type api.config reads back.
---
--- A script may declare at most PLUGIN_LUA_MAX_CONFIG (32) of them, and the
--- host's value store holds TORIRS_PLUGIN_CONFIG_MAX (40) -- the schema plus
--- room for keys an older ini left behind. Past either, the script is REFUSED
--- with a message naming both numbers; neither silently truncates.
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
---@field name? string Falls back to the file name. It is the ini section and the plugin's identity, and two plugins cannot share it.
---@field title? string What the settings roster shows: "Ground Items", not "ground-items". Falls back to the name with separators spaced and words capitalised.
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
---@field on_draw_world? fun(api: torirs.Api, draw: torirs.Draw): torirs.Verdict The WORLD surface: marks cut to the viewport and hoisted to just above the scene, so they sit under the inventory and the chatbox. Right for anything that marks a thing in the world.
---@field on_draw_canvas? fun(api: torirs.Api, draw: torirs.Draw): torirs.Verdict The CANVAS surface: the whole client window, above the interfaces. Right for anything that belongs to the CHROME -- an orb beside the minimap, a button in a corner. `draw.tile` and `draw.hull` are not legal here; api.hit_region is, and only here.
---@field on_canvas_click? fun(api: torirs.Api, ev: torirs.EvCanvasClick): torirs.Verdict One of this plugin's hit regions was clicked, or its menu row chosen.
---@field on_layout_changed? fun(api: torirs.Api, ev: torirs.EvTick): torirs.Verdict A region moved -- a resize, a gameframe rebuild, or another plugin reserving space. `ev.cycle` is the new api.layout.revision(). Only for plugins that CACHE something measured against a region; one that reads a region while drawing already sees the new box.
---@field on_config_changed? fun(api: torirs.Api, ev: torirs.EvConfig): torirs.Verdict
---@field on_obj_spawn? fun(api: torirs.Api, obj: torirs.ObjSnap): torirs.Verdict
---@field on_obj_count? fun(api: torirs.Api, obj: torirs.ObjSnap): torirs.Verdict A stack's count changed in place; `obj` carries the new one.
---@field on_obj_despawn? fun(api: torirs.Api, obj: torirs.ObjSnap): torirs.Verdict The last known state; fires before the entity is released.
---@field on_asset? fun(api: torirs.Api, ev: torirs.EvAsset): torirs.Verdict
---@field on_chat_message? fun(api: torirs.Api, ev: torirs.EvChat): torirs.Verdict Every chat line, raw. Use on_game_event for the moments the client already recognises.
---@field on_game_event? fun(api: torirs.Api, ev: torirs.EvGameEvent): torirs.Verdict A level-up, quest completion, drop, boss kill or other notable moment.
---@field on_setting? fun(api: torirs.Api, ev: torirs.EvSetting): torirs.Verdict A row in the cache's All Settings panel was used. Only needed for the rows that have no var to read -- a BUTTON row is momentary, and this is the only trace of it.
