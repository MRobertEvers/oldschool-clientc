#ifndef TORIRS_PLUGIN_H
#define TORIRS_PLUGIN_H

/*
 * The plugin contract: everything a plugin -- C or scripted -- may see or
 * touch. Nothing in this header includes an engine type, and no engine pointer
 * ever crosses it: world state arrives as copy-out POD snapshots and the
 * engine is reached only through the api function table.
 *
 * That is what makes the layer language-agnostic. A scripting adapter (Lua,
 * and whatever comes after it) is an ordinary C plugin that hosts N scripts
 * and forwards this same surface; the engine never learns a VM exists.
 *
 * Coordinates in snapshots are ABSOLUTE tiles (scene tile + world base tile),
 * so a plugin's saved state survives a scene rebuild. Fine positions are
 * scene-relative 128ths of a tile, matching WorldEntityFacet_DrawPosition,
 * because they are only ever handed straight back to api->project.
 */

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

/* Bumped whenever anything below changes shape. A plugin compiled against a
 * different value is refused rather than run against a struct it disagrees
 * about. */
#define TORIRS_PLUGIN_ABI 22

#define TORIRS_PLUGIN_NAME_MAX 48
/** Semantic role spelling, terminator included. Kept in the public contract
 * because replacement claims retain the name for their whole lifetime. */
#define TORIRS_PLUGIN_ROLE_NAME_MAX 64
/** Bytes of a plugin's human title, terminator included. Longer than the name
 *  because a title carries spaces and words the kebab-case id compresses. */
#define TORIRS_PLUGIN_TITLE_MAX 64
#define TORIRS_PLUGIN_MENU_ROWS_MAX 16
/**
 * Controls one plugin may put on its tab. A budget, not a limit on what a
 * window can hold: sixteen plugins sharing one window need the SHARE bounded,
 * or the first one to ask takes the whole chrome.
 *
 * Raised from 24 when the settings pages arrived. 24 was sized against a
 * plugin's own handful of controls, and a page that renders a LIST -- one row
 * per published feature flag, plus a heading and a rule per section -- is a
 * different order of thing: sixteen flags in four sections is twenty-three on
 * its own, which fit only by accident and would have started dropping rows,
 * silently, at the seventeenth flag.
 *
 * It costs nothing to raise: the slots come out of the shared
 * TORIRS_PLUGIN_WIN_WIDGETS_MAX pool, so this number bounds one plugin's
 * SHARE of it and never allocates anything on its own.
 */
#define TORIRS_PLUGIN_WIDGETS_MAX 48
/** Bytes of a control id, terminator included. */
#define TORIRS_PLUGIN_WIDGET_ID_MAX 32
/** Longest asset name a plugin may ask for, including its extension. */
#define TORIRS_PLUGIN_ASSET_NAME_MAX 64
/** Buffer a caller of api->screenshot needs for the path it is handed back.
 *  The engine's own path ceiling (TORIRS_IOITEM_MAX_PATH), stated again here
 *  because a plugin has no business including the client's IO header -- a
 *  shorter buffer is not refused, it is truncated. */
#define TORIRS_PLUGIN_SCREENSHOT_PATH_MAX 256
/** Recolour pairs one world object may carry, matching a spotanimtype's six. */
#define TORIRS_PLUGIN_OBJECT_RECOLORS_MAX 6
/** Vertices and faces one authored mesh may carry. Sized for the shapes a
 *  plugin actually builds by hand -- a beam, a marker, a plinth -- and not for
 *  a model somebody meant to ship as data: past these mesh_vertex and
 *  mesh_face refuse and say so. */
#define TORIRS_PLUGIN_MESH_VERTICES_MAX 1024
#define TORIRS_PLUGIN_MESH_FACES_MAX 2048
/** Face transparency a plugin may state: 0 opaque .. 253 nearly invisible.
 *  254 and 255 are not transparencies at all in this model format -- they are
 *  the two render-type overrides (flat black, and untextured-flat) -- so the
 *  authored range stops one short of them rather than letting a plugin ask for
 *  "almost gone" and get a black triangle. */
#define TORIRS_PLUGIN_MESH_ALPHA_MAX 253
/** Verbs one canvas hit region may offer, matching a component's op1..op5. */
#define TORIRS_PLUGIN_REGION_OPS_MAX 5

/*
 * Key codes for api->key_held, mirroring enum LibToriRS_KeyCode.
 *
 * Restated here rather than including the input header, so that anything built
 * on this contract -- a C plugin, the Lua adapter, whatever comes after it --
 * stays free of engine headers. The static asserts in
 * torirs_plugin_bridge.u.c are what keep the two in step: if the enum ever
 * moves, the client stops compiling rather than silently gating on the wrong
 * key.
 *
 * Only the modifiers are here. They are the ones a plugin gates on -- "hold
 * shift and right-click" is the shape half the client's own settings describe
 * -- and a full mirror of the enum would be a second copy of it to keep true.
 */
#define TORIRS_PLUGIN_KEY_ESCAPE 37
#define TORIRS_PLUGIN_KEY_SHIFT 42
#define TORIRS_PLUGIN_KEY_CTRL 43
#define TORIRS_PLUGIN_KEY_TAB 44
#define TORIRS_PLUGIN_KEY_SPACE 45

/* Opaque per-plugin instance handle. The host defines it. */
/**
 * What `api->screen` answers: where the client is in a session.
 *
 * The values are enum AppScreen's, spelled again here because a plugin header
 * must not include the app's -- the same "local copies of constants" pattern
 * the rest of this file uses. engine/torirs_plugin_host.c static_asserts the
 * two agree, so a value that moved cannot go unnoticed.
 */
enum ToriRS_PluginScreen
{
    /** Engine still coming up; no title tree yet. */
    TORIRS_PLUGIN_SCREEN_BOOT = 0,
    /** Title screen: main menu, login form or the info page. */
    TORIRS_PLUGIN_SCREEN_TITLE = 10,
    /** Login handshake in flight; the title tree is still what draws. */
    TORIRS_PLUGIN_SCREEN_CONNECTING = 20,
    /** In game: the gameframe is rooted and its parts exist. */
    TORIRS_PLUGIN_SCREEN_GAME = 30
};

struct ToriRS_PluginCtx;

/* ------------------------------------------------------------------------ */
/* Events                                                                    */
/* ------------------------------------------------------------------------ */

enum ToriRS_PluginEvent
{
    /** Enabled. Subscribe and allocate here. */
    TORIRS_PLUGIN_EV_START = 0,
    /** Disabled. Release everything; subscriptions are dropped by the host. */
    TORIRS_PLUGIN_EV_STOP,
    /** Top of App_RunOnce, after the command drain. Payload: EvFrame. */
    TORIRS_PLUGIN_EV_FRAME_START,
    /** Each 20ms client cycle (app_logic_tick). Payload: EvTick. */
    TORIRS_PLUGIN_EV_LOGIC_TICK,
    /** SERVER_TICK_END applied: every packet of this server tick is in world
     *  state. The 600ms cadence, and the only safe place to read a snapshot
     *  that must agree with the server. Payload: EvTick. */
    TORIRS_PLUGIN_EV_SERVER_TICK,
    /** Scene rebuild finished; absolute tile origin moved. Payload: EvWorld. */
    TORIRS_PLUGIN_EV_WORLD_LOADED,
    /** Payload: EvNpc. */
    TORIRS_PLUGIN_EV_NPC_SPAWN,
    /** multinpc rung / type change. base_npc_id is unchanged. Payload: EvNpc. */
    TORIRS_PLUGIN_EV_NPC_RETYPE,
    /** Payload: EvNpc. Snapshot is the last known state. */
    TORIRS_PLUGIN_EV_NPC_DESPAWN,
    /** Decoded, before it reaches world state. Payload: EvPacketIn. */
    TORIRS_PLUGIN_EV_PACKET_IN,
    /** Named, BEFORE the builder runs. Payload: EvPacketOut. */
    TORIRS_PLUGIN_EV_PACKET_OUT,
    /** A key event drained this frame. Payload: EvKey. */
    TORIRS_PLUGIN_EV_KEY,
    /** Minimenu rows assembled; rows may be appended. Payload: EvMenuBuild. */
    TORIRS_PLUGIN_EV_MENU_BUILD,
    /** A row was chosen, before dispatch. Payload: EvMenuSelect. */
    TORIRS_PLUGIN_EV_MENU_SELECT,
    /** The world overlay surface is open. The draw api is legal only here.
     *  Payload: EvDraw. */
    TORIRS_PLUGIN_EV_DRAW_WORLD,
    /**
     * The CANVAS surface is open: the whole client window, above the
     * interfaces. Payload: EvDrawCanvas.
     *
     * The other draw event, and the difference is what each is CLIPPED to. An
     * EV_DRAW_WORLD mark is cut to the world viewport and hoisted to just
     * above the 3D scene, so it sits under the inventory and the chatbox the
     * way the reference draws a scene overlay -- which is right for anything
     * that marks a thing in the world, and wrong for anything that belongs to
     * the chrome. In a FIXED gameframe the minimap is not inside the world
     * viewport at all, so an orb drawn beside it through the world surface is
     * clipped away entirely.
     *
     * `draw_tile` and `draw_hull` are not legal here: both name something in
     * the scene, and the scene is what this surface is not about. Everything
     * else -- rect, line, text, image -- is.
     */
    TORIRS_PLUGIN_EV_DRAW_CANVAS,
    /**
     * One of this plugin's canvas hit regions was used. Payload:
     * EvCanvasClick.
     *
     * Raised for the plugin that declared the region and no other, the same
     * way EV_UI is. Both the left-click default and the region's row in the
     * right-click menu arrive here, because to the plugin they are the same
     * thing happening -- which of the two the player did is not a question an
     * orb has an answer for.
     */
    TORIRS_PLUGIN_EV_CANVAS_CLICK,
    /** One of this plugin's config keys changed. Payload: EvConfig. */
    TORIRS_PLUGIN_EV_CONFIG_CHANGED,
    /** A ground-item stack appeared on a tile. Payload: EvObj. */
    TORIRS_PLUGIN_EV_OBJ_SPAWN,
    /** A ground-item stack's count changed in place. Payload: EvObj, carrying
     *  the NEW count. */
    TORIRS_PLUGIN_EV_OBJ_COUNT,
    /** A ground-item stack was taken or timed out. Payload: EvObj, holding the
     *  last known state. */
    TORIRS_PLUGIN_EV_OBJ_DESPAWN,
    /** An api->asset_load finished, either way. Payload: EvAsset. */
    TORIRS_PLUGIN_EV_ASSET,
    /** A line reached the chatbox. Payload: EvChat. */
    TORIRS_PLUGIN_EV_CHAT_MESSAGE,
    /** A notable moment the client recognised -- a level-up, a quest
     *  completion, a valuable drop, a boss kill. Payload: EvGameEvent. */
    TORIRS_PLUGIN_EV_GAME_EVENT,
    /**
     * Someone used a control on this plugin's window tab. Payload: EvUi.
     *
     * Raised for the plugin that owns the widget and no other, so a plugin
     * never sees another's controls -- the window is shared, the tabs are not.
     */
    TORIRS_PLUGIN_EV_UI,
    /**
     * This plugin's tab needs its contents. Payload: EvUi with `widget_id`
     * NULL.
     *
     * Raised after every win_clear the host itself performs -- on a reload, on
     * a re-enable, when the window is first opened -- so a plugin declares its
     * controls in ONE place and that place is re-run whenever the tab is
     * empty. A plugin that built its tab only in on_start would come back from
     * a reload with a blank tab.
     */
    TORIRS_PLUGIN_EV_UI_BUILD,
    /**
     * A row in the cache's own All Settings panel was used. Payload:
     * EvSetting.
     *
     * Every builtin reads its switch out of a varbit and needs no event for
     * it. This is for the rows that HAVE no varbit: a BUTTON row ("Clear your
     * highlighted tiles") is momentary, and the only trace the panel leaves of
     * it is `%varbit9657 = <setting id>`, which the four apply hubs all write
     * before they switch. Polling that varbit cannot see the same button
     * pressed twice; this can.
     */
    TORIRS_PLUGIN_EV_SETTING,
    /**
     * Declare the gameframe. Payload: EvLayout. Legal callers of the layout
     * api, and the only ones.
     *
     * Raised for the plugin that owns the frame (api->layout_claim) and no
     * other: at the claim, whenever the canvas changes size, and after every
     * gameframe rebuild -- which is to say, at each of the three moments the
     * previous answer stopped being true, and at no other.
     *
     * The dispatch is the whole declaration. The host empties the slot table
     * before calling and applies what came back afterwards, so a handler
     * states the frame it wants rather than the difference from the frame it
     * had; a slot left unplaced is a slot HIDDEN, not one left where it was.
     * Same shape as EV_DRAW_CANVAS and its hit regions, and for the same
     * reason: a table rebuilt from nothing cannot disagree with itself.
     */
    TORIRS_PLUGIN_EV_LAYOUT,
    /**
     * Any region moved. Payload: EvTick, carrying the new layout_revision.
     *
     * Raised for EVERY plugin, unlike EV_LAYOUT, which goes only to the frame's
     * owner. That difference is the point: EV_LAYOUT asks one plugin to state
     * the frame, and this tells everyone else that it changed. A readout that
     * composed a picture against the old box learns here that it has to
     * recompose; one that reads the box while drawing can ignore this
     * entirely.
     */
    TORIRS_PLUGIN_EV_LAYOUT_CHANGED,
    /**
     * The FRAME surface is open: the whole canvas, above the 3D scene and
     * BELOW the interfaces. Payload: EvDrawCanvas.
     *
     * The third draw surface, and the one a gameframe needs. EV_DRAW_CANVAS
     * paints over everything, which is right for a readout and wrong for
     * chrome: a sidebar panel drawn there covers the inventory it is supposed
     * to sit behind, and a chatbox backing covers the chat text. This one is
     * ordered exactly where the reference's own frame art is -- the scene is
     * already down, the interfaces have not been drawn yet.
     *
     * Open only while a plugin owns the frame, and only for that plugin. A
     * plugin that has not claimed the layout never sees it, because chrome
     * drawn under the interfaces of a frame somebody else is arranging is
     * chrome in the wrong place.
     *
     * Same verb set as EV_DRAW_CANVAS: rect, line, text and image, and not
     * draw_tile or draw_hull.
     */
    TORIRS_PLUGIN_EV_DRAW_FRAME,
    /**
     * Dress the parts of the frame this plugin has CLAIMED. Payload: EvLayout,
     * carrying the same canvas the arranger was handed.
     *
     * Raised for every plugin holding a chrome claim, in claim order, AFTER
     * the arranger's EV_LAYOUT declaration has been applied -- which is the
     * whole of the ordering guarantee between the two tiers. A plugin that
     * replaces the report button therefore reads the box the gameframe plugin
     * put it in this pass, never last pass's.
     *
     * The dispatch is the whole declaration, exactly as EV_LAYOUT is: this
     * plugin's part table is emptied before the call and applied after, so a
     * part it does not mention is a part HIDDEN rather than one left where it
     * was. chrome_paint and chrome_ops are legal here and nowhere else.
     */
    TORIRS_PLUGIN_EV_CHROME,
    /**
     * `api->screen`'s answer changed. Payload: EvScreen, carrying the new
     * answer and the one it replaced.
     *
     * Raised at the frame boundary, before that frame's EV_FRAME_START, so a
     * handler acts on the same answer every later poll of api->screen this
     * frame will get.
     *
     * This event exists because several handlers GATE on the screen -- a
     * gameframe declares nothing on the title screen, a HUD draws nothing
     * there -- and a gate needs a moment to reopen. A plugin enabled at the
     * title screen would decline to declare, and nothing would ever ask it
     * again: EV_LAYOUT re-fires on a claim, a resize or a rebuild, and logging
     * in is none of the three. Entering the game is the moment such a plugin
     * re-claims (idempotent for the holder, and it marks the frame as needing
     * a fresh EV_LAYOUT); leaving it needs no handler at all, because the
     * per-event gates already answer for every frame drawn on the title.
     */
    TORIRS_PLUGIN_EV_SCREEN_CHANGE,

    /* -- ABI 21 append: application plugin panel ------------------------- */

    /**
     * This plugin was selected in the one shared application-chrome shell and
     * must declare its page. Payload: EvPanelBuild.
     *
     * Selection, not registration, raises this event. A registered plugin
     * which is not selected therefore owns no page widgets and does no page
     * build work.
     */
    TORIRS_PLUGIN_EV_PANEL_BUILD,
    /** A semantic page control or custom hit target was used. Payload:
     *  EvPanelAction. */
    TORIRS_PLUGIN_EV_PANEL_ACTION,
    /** The selected page's visibility or allocation changed. Payload:
     *  EvPanelLayout. */
    TORIRS_PLUGIN_EV_PANEL_LAYOUT,
    /** A dirty custom region on the selected, visible page needs drawing.
     *  Payload: EvPanelDraw. */
    TORIRS_PLUGIN_EV_PANEL_DRAW,

    TORIRS_PLUGIN_EV_COUNT
};

enum ToriRS_PluginVerdict
{
    /** Observed. The next subscriber runs and the engine proceeds. */
    TORIRS_PLUGIN_PASS = 0,
    /** Stop propagation AND suppress the engine's default behaviour, on the
     *  events that document themselves as interceptable. On the others it
     *  stops propagation only. */
    TORIRS_PLUGIN_CONSUME = 1
};

typedef enum ToriRS_PluginVerdict (*ToriRS_PluginHandler)(
    struct ToriRS_PluginCtx* ctx,
    void* event,
    void* userdata);

/* ------------------------------------------------------------------------ */
/* Snapshots                                                                 */
/* ------------------------------------------------------------------------ */

struct ToriRS_PluginPlayerSnap
{
    /** The authoritative server tile: pathing.route[0] lifted to absolute.
     *  This is the "true tile" -- where the server believes the entity is --
     *  as distinct from the interpolated draw position below. */
    int true_x;
    int true_z;
    int level;
    /** Interpolated position, scene-relative, 128 units per tile. Feed
     *  straight to api->project. */
    int fine_x;
    int fine_z;
    /** Where the walk ends, absolute -- the map flag, which lives from the
     *  click to the arrival. Equals true_* when there is no destination, so a
     *  marker only has to ask whether the two differ. LOCAL PLAYER ONLY: every
     *  other player reports itself, because nothing tells this client where
     *  someone else is headed. */
    int dest_x;
    int dest_z;
    /** The same flag, unfolded: -1 when there is no destination at all, which
     *  dest_* cannot say (standing on your own destination reads the same as
     *  having none). Local player only. */
    int flag_x;
    int flag_z;
    /** Server pid, or -1 when unsynced. */
    int server_pid;
    /** ToriDraw scene element, for api->draw_hull. -1 when not drawn. */
    int element_id;
    int combat_level;
    char name[32];
};

struct ToriRS_PluginNpcSnap
{
    int server_slot;
    /** The resolved/drawn type. Changes when a multinpc rung changes. */
    int npc_id;
    /** The wire id -- the multinpc SHELL. Identity for tagging: it is what
     *  survives a varbit-driven appearance change. */
    int base_npc_id;
    char name[64];
    int combat_level;
    /** Footprint in tiles. true_x/true_z are the SW corner. */
    int size;
    int true_x;
    int true_z;
    int level;
    int fine_x;
    int fine_z;
    int element_id;
    /** Bit i set = op i is offered on this npc. */
    uint8_t visible_ops;
};

/**
 * One loc (scenery) standing in the scene.
 *
 * "Loc", not "object": `obj` is a ground ITEM everywhere in this tree, and the
 * two are the pair that get confused. This is the door, the tree, the rock,
 * the poll booth -- a thing the map placed.
 *
 * SCENE-SCOPED, unlike an npc or a ground item: a loc outside the loaded scene
 * simply is not in the list, and the list is rebuilt from nothing on every
 * scene load. A plugin that needs to remember one across a rebuild remembers
 * its absolute tile and finds it again, the way the client's own loc ops do.
 */
struct ToriRS_PluginLocSnap
{
    int loc_id;
    /** As the minimenu shows it, colour tags and all. */
    char name[64];
    /** ABSOLUTE tile of the SW corner, and the walked level. */
    int tile_x;
    int tile_z;
    int level;
    /** ROUTE footprint: the loc's config size, angle-swapped. Ground decor
     *  routes as its full config size but draws on one tile, so this is not
     *  what the model covers. */
    int size_x;
    int size_z;
    /** Placed shape (RSCACHE_LOC_SHAPE_*) and rotation, 0..3 = W/N/E/S. */
    int shape;
    int angle;
    int element_id;
    /** LocType.active: 0 for a wall, gravel or floor decor that nothing can
     *  click. A highlighter that ignores this lights up the pavement. */
    int interactive;
    /** Bit i set = op i is offered on this loc. */
    uint8_t visible_ops;
};

struct ToriRS_PluginObjSnap
{
    /** The obj id of the item on TOP of the stack -- the one the tile draws
     *  and the one the server's ops belong to. */
    int obj_id;
    int count;
    /**
     * ObjType.cost (cache opcode 12) for one of them: the same number CS2
     * reads through OC_COST, and the only value the client has. It is a base
     * price, not a live Grand Exchange quote -- nothing in the client knows
     * one -- so a plugin that wants market prices ships its own table as an
     * asset and looks `obj_id` up in it.
     *
     * 0 when the objtype is not resident. Not -1: 0 is also what a valueless
     * item costs, and a plugin thresholding on value treats both the same way.
     */
    int cost;
    /** Name as the right-click builder sees it, colour tags and all. */
    char name[64];
    /** ABSOLUTE tile, like every other snapshot here. */
    int tile_x;
    int tile_z;
    int level;
    /** ToriDraw scene element, for api->draw_hull. -1 when not drawn. */
    int element_id;
};

/**
 * Which of the client's OWN containers. @see ToriRS_PluginApi::inv_slot.
 *
 * Names rather than numbers, because the numbers are the client's and it
 * already holds them (INV_MANAGER_CONTAINER_WORN and friends). A plugin
 * carrying 94 of its own would be carrying a copy of a constant it cannot
 * check, on a client that boots several revisions.
 */
enum ToriRS_PluginInv
{
    /** The backpack: the 28 slots of the inventory tab. */
    TORIRS_PLUGIN_INV_BACKPACK = 0,
    /** Worn equipment, indexed by an objtype's `wearpos`. */
    TORIRS_PLUGIN_INV_WORN,
    TORIRS_PLUGIN_INV_BANK
};

/**
 * The twelve equipment bonuses, in the order the cache states them.
 *
 * These are param ids 0..11 on an OldSchool obj record -- the numbering
 * OpenRune's ParamMapper documents and the server reads through
 * ToriRSServerCombatParam. The order is load-bearing twice: `ATTACK_STAB + style`
 * picks the attack bonus for a damage type and `+ DEFENCE_STAB` the defence
 * one.
 */
enum ToriRS_PluginBonus
{
    TORIRS_PLUGIN_BONUS_ATTACK_STAB = 0,
    TORIRS_PLUGIN_BONUS_ATTACK_SLASH,
    TORIRS_PLUGIN_BONUS_ATTACK_CRUSH,
    TORIRS_PLUGIN_BONUS_ATTACK_MAGIC,
    TORIRS_PLUGIN_BONUS_ATTACK_RANGE,
    TORIRS_PLUGIN_BONUS_DEFENCE_STAB,
    TORIRS_PLUGIN_BONUS_DEFENCE_SLASH,
    TORIRS_PLUGIN_BONUS_DEFENCE_CRUSH,
    TORIRS_PLUGIN_BONUS_DEFENCE_MAGIC,
    TORIRS_PLUGIN_BONUS_DEFENCE_RANGE,
    TORIRS_PLUGIN_BONUS_STRENGTH,
    TORIRS_PLUGIN_BONUS_PRAYER,
    TORIRS_PLUGIN_BONUS_COUNT
};

/**
 * One objtype, as the cache states it. @see ToriRS_PluginApi::obj_info.
 *
 * The record and nothing else: an obj snapshot describes a stack lying on a
 * tile, and this describes the ITEM -- what it is called, what it is worth,
 * where it is worn and what it does to a combat roll.
 */
struct ToriRS_PluginObjInfo
{
    int obj_id;
    /** ObjType.name, as the minimenu prints it. */
    char name[64];
    /** ObjType.cost, the same number ObjSnap carries. */
    int cost;
    int stackable;
    /**
     * Bank-note linkage: the id of the item this note stands for, or -1.
     *
     * A note carries none of the base item's ops or params of its own, so a
     * reader that wants an item's stats from a noted stack asks again with
     * this id.
     */
    int cert_link;
    /**
     * Equipment placement (dat2 wearpos/wearpos2/wearpos3), -1 for an item
     * that is not worn. The primary position is the slot the item occupies --
     * the same index the WORN container is addressed by -- and the secondary
     * ones are the slots it COVERS: a two-handed weapon is wearpos 3 with a
     * shield slot among the others, which is how the cache says two-handed
     * without a flag for it.
     *
     * Dat1 has no such metadata, so all three are -1 on a classic cache and a
     * caller gets "not equipment" for a sword. That is the honest answer for a
     * revision whose cache does not state it, not a bug to be guessed around.
     */
    int wearpos;
    int wearpos2;
    int wearpos3;
    /**
     * 1 when the record carries the OldSchool bonus params at all.
     *
     * The distinction 0 cannot make: an unarmed slot and a cape with no
     * offensive bonus both read as twelve zeroes, and only this says which was
     * measured. A cache lineage that states no params (dat1, and the pre-EoC
     * dat2 revisions) leaves this 0 for every item in the game.
     */
    int has_bonuses;
    /** Indexed by enum ToriRS_PluginBonus. */
    int bonus[TORIRS_PLUGIN_BONUS_COUNT];
    /** Ticks between swings (cache param 14), or -1 when unstated. */
    int attack_rate;
    /**
     * Ranged strength, which OldSchool keeps OUTSIDE the contiguous block and
     * in two places: param 12 on ammunition and thrown weapons (a dragon arrow
     * reads 60, a dragon dart 35), param 189 on everything else (a twisted bow
     * reads 20, a necklace of anguish 5). Summed here, because they are one
     * number on the equipment screen and no record states both.
     */
    int ranged_strength;
};

/**
 * Which of the client's three inventory-icon variants to rasterise.
 *
 * The same three the interface emitter already asks the scene bridge for, and
 * not a fourth: an icon is baked art, so every variant here is one the client
 * builds anyway for its own inventory and is therefore free to hand over.
 * @see ToriRS_PluginApi::obj_image.
 */
enum ToriRS_PluginObjIconStyle
{
    /**
     * No baked outline and no drop shadow -- the reference's
     * `ObjType.getSprite(outlineRgb = -1)`.
     *
     * For a caller that intends to draw the icon on art of its own and apply
     * its own edge, because stacking an outline pass on a shadow-baked icon
     * doubles the shadow.
     */
    TORIRS_PLUGIN_OBJ_ICON_PLAIN = 0,
    /**
     * A black border baked into the pixels, and the one to reach for.
     *
     * It is what the client's own dense item grids use, and the reason is
     * legibility rather than taste: an unbordered icon on a dark panel loses
     * its silhouette, and a grid of them reads as a smear.
     */
    TORIRS_PLUGIN_OBJ_ICON_BORDERED,
    /** The white outline the client puts on the item armed for "Use"
     *  (`outlineRgb = 0xFFFFFF`). For marking one entry of a set. */
    TORIRS_PLUGIN_OBJ_ICON_SELECTED
};

/** What a highlight item is attached to. @see ToriRS_PluginHighlightItem. */
enum ToriRS_PluginHighlightKind
{
    TORIRS_PLUGIN_HL_TILE = 0,
    TORIRS_PLUGIN_HL_NPC,
    TORIRS_PLUGIN_HL_LOC,
    TORIRS_PLUGIN_HL_OBJ,
    /** A player, named by DISPLAY NAME -- `highlight_player_on` is the one
     *  form of the family whose subject is a string. */
    TORIRS_PLUGIN_HL_PLAYER
};

/**
 * One thing the CACHE has asked to be marked, already resolved to something on
 * the screen.
 *
 * The HIGHLIGHT_* opcode family (7000..7044) is how the settings panel's
 * Activities category reaches this client: 125 clientscripts read a varbit and
 * a colour row and describe a highlight GROUP, then name subjects for it --
 * "every loc of type 23138", "the tile at this coord", "this npc". The engine
 * keeps those groups and resolves them against live world state; what arrives
 * here is the result, one item per thing to draw.
 *
 * A plugin reading this is implementing the client's own settings, not adding
 * a feature: every appearance decision below was made by a cache script, and
 * there is nothing here for a plugin to have an opinion about beyond how to
 * put the pixels down.
 */
struct ToriRS_PluginHighlightItem
{
    /** enum ToriRS_PluginHighlightKind. */
    int kind;
    /** Scene element to outline, or -1 for a bare tile. */
    int element_id;
    /** ABSOLUTE tile of the SW corner, and the walked level. */
    int tile_x;
    int tile_z;
    int level;
    /** Footprint in tiles; 1x1 for a tile item. */
    int size_x;
    int size_z;
    /** 0xRRGGBB, as the group's SETUP stated it. */
    uint32_t rgb;
    /** Fill alpha, 0..255 as the reference clamps it -- NOT a percent. The
     *  call sites (30, 45, 50, 70, 100) are transparent washes. */
    int opacity;
    /** Outline thickness in pixels, 0/1/2. Zero means NO outline however the
     *  outline flags read: the reference's predicates are
     *  `(flags & bit) && thickness != 0`. */
    int outline_width;
    /** The group's flags OR'd with the subject's own. Bit 1 model outline,
     *  2 tile outline, 4 model fill, 8 tile fill, 16 always on top,
     *  64 minimap, 512 mouseover. */
    int flags;
    /** The subject's name, as the minimenu shows it; "" for a bare tile. */
    char name[64];
    /** SCENE-relative fine position, 128 per tile -- feed straight to
     *  api->project. The tile above is absolute and the projector is not, and
     *  a caller cannot convert between them without the scene base, which the
     *  plugin layer deliberately does not expose. An npc's is its interpolated
     *  draw position; everything else's is the centre of its anchor tile. */
    int fine_x;
    int fine_z;
    /** Footprint anchor for an overhead: what api->element_height would answer
     *  for `element_id`, or 0 when there is no element. Carried so a caller
     *  drawing a label over a highlighted thing does not need a second call
     *  per item. */
    int overhead_height;
};

/* ------------------------------------------------------------------------ */
/* Event payloads                                                            */
/* ------------------------------------------------------------------------ */

struct ToriRS_PluginEvFrame
{
    uint64_t now_ms;
    /**
     * Frames the client has RENDERED so far, cumulative. on_frame itself fires
     * once per loop iteration -- the 50 Hz pacer -- whether or not that
     * iteration drew, so counting calls measures the pacer. A frame-rate
     * readout that means the screen differences this instead.
     */
    uint64_t drawn_frames;
};

struct ToriRS_PluginEvTick
{
    /** logic_cycle for LOGIC_TICK, world cycle for SERVER_TICK. */
    int cycle;
};

struct ToriRS_PluginEvWorld
{
    int base_tile_x;
    int base_tile_z;
};

/** @see TORIRS_PLUGIN_EV_SCREEN_CHANGE. Both are TORIRS_PLUGIN_SCREEN_*. */
struct ToriRS_PluginEvScreen
{
    /** What api->screen answers now. */
    int screen;
    /** What it answered until this moment. */
    int previous;
};

struct ToriRS_PluginEvNpc
{
    struct ToriRS_PluginNpcSnap npc;
};

struct ToriRS_PluginEvPacketIn
{
    /** enum GameProtoPktName. Compare against api->packet_name(). */
    int name;
    /** Wire size of the payload, or -1 when the revision does not report it. */
    int size;
    /** Set true to drop the packet: it is freed instead of executed, and no
     *  further subscriber sees it.
     *
     *  This is a live wire. PLAYER_INFO/NPC_INFO extended-info blocks are
     *  indexed by list position, so dropping one desyncs entity bookkeeping
     *  for the rest of the session, and the server-tick fence may never be
     *  dropped at all (the host asserts). */
    bool drop;
};

struct ToriRS_PluginEvPacketOut
{
    /**
     * The builder that is about to run, by name: "net_out_opnpc",
     * "net_out_move_gameclick", and so on.
     *
     * A string rather than the enum the inbound side uses, because outbound
     * genuinely has no id at the call site -- every send is a direct call to a
     * net_out_* builder that returns a byte count, and there is no packet-name
     * argument anywhere in the path. Deriving the name from the call itself is
     * what makes all sixty send sites observable without a hand-written
     * builder-to-enum table that could label any one of them wrong.
     */
    char const* builder;
    /** Set true to veto the send.
     *
     *  Fires BEFORE the builder, because every net_out_* builder encrypts its
     *  opcode by advancing the outbound ISAAC stream: a packet built and then
     *  discarded desyncs the cipher and the server misreads every opcode
     *  after it. There is deliberately no payload here for the same reason --
     *  it does not exist yet. */
    bool drop;
};

struct ToriRS_PluginEvKey
{
    /** enum LibToriRS_KeyCode. */
    int key;
    /** Typed character, or 0. */
    int ch;
    bool down;
};

/** Read-only view of one built minimenu row. */
struct ToriRS_PluginMenuRow
{
    /** Includes the reference colour tags (@yel@ and friends). */
    char const* text;
    /** RevConfig action id, already normalized. */
    int action;
    /** enum UIMinimenuPickKind. */
    int pick_kind;
    /** Server slot when the row targets an npc, else -1. */
    int npc_slot;
    /** Server pid when the row targets a player, else -1. */
    int player_pid;
    /** Loc/obj id when the row targets one, else -1. For an INV_SLOT row that
     *  is the ITEM in the cell -- the one thing a row about an inventory cell
     *  is actually about. */
    int target_id;
    /** `(interface << 16) | component` of the node the row is about, for the
     *  two kinds that name one (UI and INV_SLOT), else -1. Which panel the
     *  cell belongs to -- backpack, worn tab, bank -- is read off this. */
    int component_id;
    /** Slot within that container for an INV_SLOT row, else -1. */
    int slot;
};

struct ToriRS_PluginEvMenuBuild
{
    int row_count;
    struct ToriRS_PluginMenuRow rows[TORIRS_PLUGIN_MENU_ROWS_MAX];
    /** True for the hover-text rebuild, which runs EVERY FRAME. Handlers that
     *  only need rows in the right-click menu should return immediately. */
    bool hover_pass;
    /** Opaque; hand back to api->menu_add. */
    void* host_cursor;
};

struct ToriRS_PluginEvMenuSelect
{
    struct ToriRS_PluginMenuRow row;
    /** The tag passed to api->menu_add, or 0 for a native row. */
    uint32_t plugin_tag;
    /** True when this row belongs to the plugin being dispatched. */
    bool owned;
    int click_x;
    int click_y;
};

struct ToriRS_PluginEvDraw
{
    /** Opaque surface token; hand back to the draw api. */
    void* surface;
};

struct ToriRS_PluginEvCanvasClick
{
    /** The `tag` the region was declared with. */
    uint32_t tag;
    /** Which of the region's ops was chosen, indexing the array it was
     *  declared with. A left click is always op 0 -- the first verb is the
     *  default one, the same rule the reference's own op 1 follows. */
    int op;
    /** Where the pointer was, in canvas coordinates. */
    int x;
    int y;
};

struct ToriRS_PluginEvDrawCanvas
{
    /** Opaque surface token; hand back to the draw api. Never the same token
     *  as EV_DRAW_WORLD's, so a handler that kept one from the wrong event is
     *  caught rather than drawing into the other list. */
    void* surface;
    /** The canvas, in the coordinates every draw call on this surface uses. */
    int width;
    int height;
};

/**
 * The canvas the frame is being declared against. @see EV_LAYOUT.
 *
 * Carries the size and nothing else, because everything else a layout needs is
 * the plugin's own arithmetic: where the sidebar goes at 1440x900 is a
 * statement the plugin makes, not one the host can be asked for.
 */
struct ToriRS_PluginEvLayout
{
    int width;
    int height;
    /** enum ToriRS_PluginLayoutCanvas, as the claim asked for it. A plugin
     *  that claimed FIXED reads its own pinned size back here rather than the
     *  window's, so one handler serves both kinds. */
    int canvas;
};

struct ToriRS_PluginEvConfig
{
    char const* key;
};

struct ToriRS_PluginEvObj
{
    struct ToriRS_PluginObjSnap obj;
};

struct ToriRS_PluginEvChat
{
    /** enum RS_ChatMessageType, as it arrived on the wire: 0 game, 2 public,
     *  3 private-from, and so on. */
    int type;
    /** Who said it, or "" for a system line. */
    char sender[64];
    /** The line, exactly as the chatbox holds it -- colour tags included. The
     *  recognised events below arrive with those stripped; this one does not,
     *  because a plugin reading raw chat may well care about them. */
    char text[200];
};

/*
 * One notable moment.
 *
 * `kind` is a STRING rather than an enum, and that is deliberate: the
 * recogniser (game/rs_game_events.c) already owns the list, a second copy in
 * this header would be a second thing to keep in step, and a plugin's config
 * lists these by name anyway ("screenshot on: level_up, boss_kill"). A kind
 * added to the recogniser reaches every plugin without touching the ABI.
 */
struct ToriRS_PluginEvGameEvent
{
    /** Stable lowercase name: "level_up", "quest_complete", "valuable_drop",
     *  "untradeable_drop", "boss_kill", "pet", "collection_log",
     *  "combat_achievement", "death", "treasure_trail", "duel_end". Never
     *  NULL, and valid for this dispatch only. */
    char const* kind;
    /** The skill, boss, quest or item this is about; "" when the moment names
     *  nothing (a pet drop never says which pet). */
    char subject[64];
    /** New level / kill count / coin value, or -1 when the kind carries none. */
    int value;
    /** The line it was recognised from, markup stripped. Empty for a level-up,
     *  which comes from UPDATE_STAT and has no sentence behind it. */
    char text[200];
};

/**
 * One use of an All Settings row.
 *
 * `setting_id` is the cache's `param_1077`, the number every settings hub
 * switches on -- 117 is "Clear your highlighted tiles", 112 is "Tile
 * highlighting". `value` is the row's new value where the hub carries one
 * (a dropdown, a slider) and -1 where it does not (a toggle, whose hub is
 * handed the id alone, and a button, which has no value at all).
 */
struct ToriRS_PluginEvSetting
{
    int setting_id;
    int value;
};

/** What the pointer is over. @see ToriRS_PluginApi::hover_entity. */
enum ToriRS_PluginHoverKind
{
    TORIRS_PLUGIN_HOVER_NONE = 0,
    TORIRS_PLUGIN_HOVER_SCENERY,
    TORIRS_PLUGIN_HOVER_NPC,
    TORIRS_PLUGIN_HOVER_PLAYER,
    TORIRS_PLUGIN_HOVER_OBJ
};

/**
 * The nearest entity under the pointer, as the last rendered frame picked it.
 *
 * Not the same question as hover_tile, which answers with the GROUND under the
 * pointer and is filled even when the cursor is over open grass. This one is
 * about a thing: the first scenery/npc/player/objstack in the frame's pickset,
 * which is the one the client's own left-click would act on.
 */
struct ToriRS_PluginHoverEntity
{
    /** enum ToriRS_PluginHoverKind. NONE when nothing is under the pointer. */
    int kind;
    /** ToriDraw scene element, for api->draw_hull. */
    int element_id;
    /** ABSOLUTE tile the pick landed on, and the level it was picked at. */
    int tile_x;
    int tile_z;
    int level;
};

/* ------------------------------------------------------------------------ */
/* Plugin windows                                                            */
/* ------------------------------------------------------------------------ */

/**
 * What a plugin can put on its tab.
 *
 * A deliberately small set, and the small set is the point: these are the
 * controls every executor can present natively. A checkbox is a checkbox in
 * the canvas, in the DOM, in comctl32 and in a cache interface; anything
 * richer would be a control one of them has to fake, and a faked control is
 * how a "native" window stops looking native.
 */
enum ToriRS_PluginWidgetKind
{
    TORIRS_PLUGIN_W_LABEL = 0,
    TORIRS_PLUGIN_W_CHECKBOX,
    TORIRS_PLUGIN_W_INPUT,
    TORIRS_PLUGIN_W_DROPDOWN,
    TORIRS_PLUGIN_W_BUTTON,
    TORIRS_PLUGIN_W_SEPARATOR,

    /* ABI 21 semantic page controls. The first six deliberately retain their
     * numbers: win_* and panel_* share this vocabulary, so every existing
     * window presenter is also the compatibility presenter for those rows. */
    TORIRS_PLUGIN_W_SECTION,
    TORIRS_PLUGIN_W_PARAGRAPH,
    TORIRS_PLUGIN_W_KEY_VALUE,
    TORIRS_PLUGIN_W_TOGGLE,
    TORIRS_PLUGIN_W_TEXTAREA,
    TORIRS_PLUGIN_W_LIST_ROW,
    TORIRS_PLUGIN_W_IMAGE,
    TORIRS_PLUGIN_W_PROGRESS,
    TORIRS_PLUGIN_W_ERROR,
    TORIRS_PLUGIN_W_CUSTOM,

    TORIRS_PLUGIN_W_COUNT
};

/** What happened to a control. */
enum ToriRS_PluginUiAction
{
    /** A button was pressed. */
    TORIRS_PLUGIN_UI_ACTIVATE = 0,
    /** A checkbox changed: `value` is its new state. */
    TORIRS_PLUGIN_UI_TOGGLE,
    /** A field was edited: `text` is its whole new contents. */
    TORIRS_PLUGIN_UI_TEXT,
    /** A list choice was made: `value` is the index, `text` the chosen entry. */
    TORIRS_PLUGIN_UI_PICK,
    /** A custom region's pointer capture moved. `value` is implementation
     *  neutral; `x`/`y` in EvPanelAction carry the local position. */
    TORIRS_PLUGIN_UI_DRAG,
    /** A custom region was scrolled. `value` is the signed logical delta. */
    TORIRS_PLUGIN_UI_SCROLL,
    /** A custom region received a key. `value` is a TORIRS_PLUGIN_KEY_* code. */
    TORIRS_PLUGIN_UI_KEY,
};

struct ToriRS_PluginEvUi
{
    /** The id the plugin gave the control in api->win_widget. NULL on
     *  EV_UI_BUILD, which is about the tab rather than about a control. */
    char const* widget_id;
    /** enum ToriRS_PluginUiAction. */
    int action;
    /** Index or flag, per the action; -1 when the action carries none. */
    int value;
    /** The control's text after the change. Never NULL; "" when it has none.
     *  Valid for this dispatch only. */
    char const* text;
};

/* ------------------------------------------------------------------------ */
/* Application plugin panel (ABI 21)                                        */
/* ------------------------------------------------------------------------ */

/** Bytes retained for a rail badge, terminator included. Badges are short
 *  status hints, not a second page title. */
#define TORIRS_PLUGIN_PANEL_BADGE_MAX 24
/** The default and bounded width hints used by every shell presenter. */
#define TORIRS_PLUGIN_PANEL_WIDTH_DEFAULT 320
#define TORIRS_PLUGIN_PANEL_WIDTH_MIN 280
#define TORIRS_PLUGIN_PANEL_WIDTH_MAX 480
/** Bounded logical height of one custom drawing well. */
#define TORIRS_PLUGIN_PANEL_CUSTOM_HEIGHT_DEFAULT 120
#define TORIRS_PLUGIN_PANEL_CUSTOM_HEIGHT_MIN 48
#define TORIRS_PLUGIN_PANEL_CUSTOM_HEIGHT_MAX 512

/** Inert rail metadata copied by panel_request during EV_START. */
struct ToriRS_PluginPanelDesc
{
    /** Human-facing page title. NULL or empty uses the plugin's title. */
    char const* title;
    /** Optional sandboxed PNG asset name. The host loads it automatically;
     * malformed, over-budget, or larger-than-64x64 art uses the baked wrench.
     * NULL or empty asks for that fallback directly. */
    char const* icon_asset;
    /** Logical units; 0 asks for TORIRS_PLUGIN_PANEL_WIDTH_DEFAULT. */
    int preferred_width;
};

/** Neutral allocation class. It describes space, never the platform. */
enum ToriRS_PluginPanelSizeClass
{
    TORIRS_PLUGIN_PANEL_COMPACT = 0,
    TORIRS_PLUGIN_PANEL_MEDIUM,
    TORIRS_PLUGIN_PANEL_EXPANDED,
};

/** The page model was cleared for this exact selection and must be declared. */
struct ToriRS_PluginEvPanelBuild
{
    uint32_t selection_generation;
};

/** One result-state intent from the shared shell. */
struct ToriRS_PluginEvPanelAction
{
    /** Plugin-scoped semantic id. Valid for this dispatch only. */
    char const* id;
    /** enum ToriRS_PluginUiAction. */
    int action;
    /** Result value, index, scroll delta, or key according to action. */
    int value;
    /** Whole result text, never NULL and valid for this dispatch only. */
    char const* text;
    /** Custom-region-local logical coordinates; 0 for ordinary controls. */
    int x;
    int y;
    /** Fences queued input from an older selected page. */
    uint32_t selection_generation;
    /** Fences a removed/redeclared id within the same selection. */
    uint32_t widget_serial;
    /** Monotonic within the selection; duplicates and older intents are
     *  discarded before dispatch. */
    uint64_t intent_sequence;
};

/** Visibility/allocation facts for the selected page. */
struct ToriRS_PluginEvPanelLayout
{
    int width;
    int height;
    /** Scale in thousandths: 1000 is 1x, 2000 is 2x. */
    int scale_milli;
    /** enum ToriRS_PluginPanelSizeClass. */
    int size_class;
    bool visible;
    /** False in attached-exclusive presentation. */
    bool game_visible;
    uint32_t selection_generation;
};

/** A scoped draw pass for one custom semantic node. */
struct ToriRS_PluginEvPanelDraw
{
    char const* id;
    /** Opaque token accepted by the ordinary portable draw_* verbs. */
    void* surface;
    /** Local logical dirty rectangle. */
    int x;
    int y;
    int width;
    int height;
    int scale_milli;
    uint32_t selection_generation;
    uint32_t widget_serial;
};

struct ToriRS_PluginEvAsset
{
    /** The name passed to api->asset_load. Valid for this dispatch only. */
    char const* name;
    /** Bytes now resident; 0 when the read failed. */
    int size;
    /** False when the asset does not exist or could not be read. The plugin is
     *  told either way: a load that simply never fires is indistinguishable
     *  from one still in flight, and a plugin waiting on a file that will
     *  never arrive would wait forever. */
    bool ok;
};

/* ------------------------------------------------------------------------ */
/* Config schema                                                             */
/* ------------------------------------------------------------------------ */

enum ToriRS_PluginConfigType
{
    TORIRS_PLUGIN_CFG_BOOL = 0,
    /** Uses min/max. */
    TORIRS_PLUGIN_CFG_INT,
    /** Written as "#RRGGBB" by the panel, read back as 0xRRGGBB. */
    TORIRS_PLUGIN_CFG_COLOR,
    /** Also the carrier for lists, as comma-separated text. */
    TORIRS_PLUGIN_CFG_STRING,
    /** `choices` is a '|'-separated set, rendered as a dropdown. */
    TORIRS_PLUGIN_CFG_ENUM,
    /**
     * A STRING the panel gives a multiline box instead of a one-line field.
     *
     * The same value, stored the same way -- the difference is entirely about
     * editing it. A CFG_STRING holding "Vial, Ashes, Coins, Bones, Bucket,
     * Jug, Seaweed" in a 60px field shows about a word and a half of it, so
     * changing one entry means arrowing sideways through the rest; the
     * reference client gives exactly these lists a box several lines tall
     * (interface 650's "Highlighted items" and "Filtered items"), and this is
     * the schema saying which lists those are.
     *
     * `rows` is optional and defaults to the chrome's own.
     */
    TORIRS_PLUGIN_CFG_TEXT
};

struct ToriRS_PluginConfigItem
{
    /** INI key: [a-z0-9_]. A NULL key terminates the array. */
    char const* key;
    enum ToriRS_PluginConfigType type;
    /** Panel text. NULL hides the key from the panel -- the idiom for state a
     *  plugin persists but the user does not edit by hand. */
    char const* label;
    /** Always textual, exactly as it would appear in the ini. */
    char const* default_value;
    /** CFG_INT only. */
    int min;
    int max;
    /** CFG_ENUM only: "a|b|c". */
    char const* choices;
    /**
     * CFG_TEXT only: visible lines of the box, 0 for the chrome's default.
     *
     * LAST, and that is not taste: every builtin plugin's schema is a table of
     * POSITIONAL initialisers (`{ "show_dest", TORIRS_PLUGIN_CFG_BOOL, "Show
     * destination", "1", 0, 0, NULL }`), so a field inserted anywhere above
     * `choices` silently shifts what each of those braces means.
     */
    int rows;
};

/* ------------------------------------------------------------------------ */
/* Display settings                                                          */
/* ------------------------------------------------------------------------ */

/**
 * A client-wide DISPLAY preference, as display_setting names it.
 *
 * Not a feature flag and not a varp, and it is worth saying which it is not.
 * A feature flag is a per-era BEHAVIOUR with a revision default to fall back
 * to; a varp is the SERVER's and read-only here. These are the DEVICE's: they
 * describe the screen the client is being looked at on, they have no revision
 * default because no revision has an opinion about a monitor, and they are
 * persisted per install in preferences.ini.
 *
 * The rev-239 cache edits the same two rows in its own All Settings panel, so
 * these are the SAME store rather than a second one -- a lane with that panel
 * shows one value in two places, and a lane without it (every dat1 world:
 * there is no All Settings interface in a 2004 cache) has these and nothing
 * else. That gap is the whole reason the verbs exist.
 */
enum ToriRS_PluginDisplaySetting
{
    /**
     * Interface scale, as a PERCENT of 1:1.
     *
     * The canvas is the window divided by it and the present stretches that
     * back to fill the window, so 200 is half as many pixels each drawn twice
     * the size -- the 3D scene included, which is the trade this buys: chrome
     * and text at a readable size on a high-density display, a scene rendered
     * at fewer pixels. 100 is untouched.
     */
    TORIRS_PLUGIN_DISPLAY_UI_SCALE = 0,
    /** How that stretch is filtered: 0 nearest, 1 linear, 2 bicubic. */
    TORIRS_PLUGIN_DISPLAY_UI_SCALE_FILTER,

    TORIRS_PLUGIN_DISPLAY_SETTING_COUNT
};

/* ------------------------------------------------------------------------ */
/* The lane                                                                  */
/* ------------------------------------------------------------------------ */

/**
 * Which lineage's field layouts the booted cache carries.
 *
 * The LINEAGE and not the revision, because that is the question a plugin
 * actually has. Answering "is this an OldSchool world?" by revision number is
 * wrong on both axes: the two lineages number their revisions independently,
 * so 254 is a 2004 dat1 world and 233 is an OldSchool one, and a threshold
 * written against either number is a threshold against the wrong thing.
 */
enum ToriRS_PluginGame
{
    /** Nothing has stated one yet. @see ToriRS_PluginApi::lane. */
    TORIRS_PLUGIN_GAME_UNKNOWN = 0,
    TORIRS_PLUGIN_GAME_OLDSCHOOL = 1,
    /** The classic client's lineage: the 2004 dat1 worlds AND the later dat2
     *  RS2 revisions, which are that same client with a different container. */
    TORIRS_PLUGIN_GAME_RS2 = 2
};

/** Which on-disk container family the cache is stored in. Not a proxy for the
 *  lineage: OldSchool and the later RS2 revisions are both dat2. */
enum ToriRS_PluginEpoch
{
    TORIRS_PLUGIN_EPOCH_UNKNOWN = 0,
    /** Jagfile era: main_file_cache.dat + .idx1..5. */
    TORIRS_PLUGIN_EPOCH_DAT1 = 1,
    /** JS5: main_file_cache.dat2 + .idx0..N. */
    TORIRS_PLUGIN_EPOCH_DAT2 = 2
};

/**
 * What this client booted, as the boot manifest's `[cache:boot]` stated it.
 *
 * Deliberately NOT a feature flag, and the distinction is the one the feature
 * verbs already draw: a flag is a BEHAVIOUR a plugin may legitimately want a
 * different value of, and this is a FACT about the cache on disk that nothing
 * can want differently. So it is read-only, and there is no "whatever this
 * boot resolved" sentinel -- that is the only thing it ever is.
 */
struct ToriRS_PluginLane
{
    /** enum ToriRS_PluginGame. */
    int game;
    /** enum ToriRS_PluginEpoch. */
    int epoch;
    /** Numbered in `game`'s own lineage, so it means nothing without it. */
    int revision;
};

/* ------------------------------------------------------------------------ */
/* Feature flags                                                             */
/* ------------------------------------------------------------------------ */

/**
 * "Whatever this boot resolved" -- not a value of any flag.
 *
 * -1 rather than 0 because 0 is a real value of nearly every flag in the
 * table: the era tables are written zero-is-classic, so "off" and "the 2004
 * behaviour" are the same number and a sentinel of 0 would make every default
 * indistinguishable from the classic choice.
 */
#define TORIRS_PLUGIN_FEATURE_UNSET (-1)

/** Bytes of a feature key / label, terminator included. */
#define TORIRS_PLUGIN_FEATURE_KEY_MAX 32
#define TORIRS_PLUGIN_FEATURE_LABEL_MAX 64
/** Bytes of a feature's '|'-separated choice list, terminator included. */
#define TORIRS_PLUGIN_FEATURE_CHOICES_MAX 224
/** Choices one flag may offer. */
#define TORIRS_PLUGIN_FEATURE_VALUES_MAX 12

/**
 * How a published flag is WRITTEN DOWN. Both kinds are CHOSEN the same way --
 * from `choices` -- and the difference is only what a settings file holds.
 *
 * That every flag offers a fixed set of choices is deliberate and is the whole
 * reason this is not a text field. A number typed into a box has to be
 * validated, refused, explained and put back, and the failure is silent until
 * it is not: a draw distance of 4000 or an eye height of 3 is a client you
 * cannot see out of, entered one keystroke at a time with nothing to say so.
 * Naming the values that mean something -- the reference's own 600, the deob's
 * 70-tile ceiling, xrsps's 128 -- says more in the list than a range ever said
 * in a caption.
 */
enum ToriRS_PluginFeatureKind
{
    /**
     * A number. `choices` are the values worth naming and `values[i]` is the
     * number each stands for, but `min`..`max` is the flag's real range: a
     * value outside the list is legal, and a settings file that carries one
     * keeps it. So this is stored as the NUMBER.
     */
    TORIRS_PLUGIN_FEATURE_INT = 0,
    /**
     * One of `choices` and nothing else, where choice i has value `values[i]`.
     *
     * The values are carried rather than implied by the index because a flag's
     * legal set is not always 0..n: `target_mask_held` is 0x10 or 0x20, and an
     * index would make the panel write 1 for a bit that is 32. Stored as the
     * CHOICE TEXT, so a settings file survives the list gaining an entry.
     */
    TORIRS_PLUGIN_FEATURE_ENUM
};

/** One published flag, as feature_next reports it. */
struct ToriRS_PluginFeature
{
    char key[TORIRS_PLUGIN_FEATURE_KEY_MAX];
    /**
     * What a PERSON is shown. Never empty.
     *
     * SHORT -- it shares a row with the control, and a settings panel is
     * narrow. What the flag is about belongs in `section`; what its values mean
     * belongs in the choice names.
     */
    char label[TORIRS_PLUGIN_FEATURE_LABEL_MAX];
    /**
     * Heading this flag sits under, or "" for one that sits under none.
     *
     * The walk is in section order, so a reader groups by "the section
     * changed" rather than by collecting: two runs of the same name are two
     * headings, which is a fact about the engine's list and not something to
     * paper over.
     */
    char section[TORIRS_PLUGIN_FEATURE_KEY_MAX];
    /** enum ToriRS_PluginFeatureKind. */
    int kind;
    /** FEATURE_INT: the real range, wider than the named choices. */
    int min;
    int max;
    /** "a|b|c", WITHOUT a "revision default" entry -- that is the sentinel's
     *  job and every flag has it, so no flag states it. */
    char choices[TORIRS_PLUGIN_FEATURE_CHOICES_MAX];
    /** The value each choice stands for. */
    int values[TORIRS_PLUGIN_FEATURE_VALUES_MAX];
    int value_count;
    /** The value the flag holds right now. */
    int value;
    /** 1 when the value in force is this boot's own, i.e. nothing has set it. */
    int is_default;
};

/* ------------------------------------------------------------------------ */
/* World objects                                                             */
/* ------------------------------------------------------------------------ */

/**
 * Where a plugin object's geometry comes from.
 *
 * Both are cache ids rather than bytes a plugin supplies, because a model is
 * not a file the plugin can meaningfully own: it is decoded against the
 * revision's own model codec, lit with the client's light source, and its face
 * colours are indices into the palette the running cache defines. A plugin
 * ships DATA as an asset and names GEOMETRY by id.
 */
enum ToriRS_PluginModelSource
{
    /** A model id from the cache's model table, lit as an actor and drawn as
     *  it decodes. */
    TORIRS_PLUGIN_MODEL_CACHE = 0,
    /** A spotanimtype id: its model with that type's own recolours, retextures,
     *  resize, angle and lighting -- the graphic exactly as the server would
     *  draw it -- and its `seq` bound unless the plugin names another. */
    TORIRS_PLUGIN_MODEL_SPOTANIM,
    /** A mesh handle from mesh_create: geometry the PLUGIN authored, triangle
     *  by triangle. Nothing about it is read from the cache, which is the
     *  whole point -- a cache id names a model that exists in one revision and
     *  is something else, or nothing, in the next, so a plugin that draws its
     *  own furniture by id works on the cache it was written against and
     *  silently draws a rock on the rest. An authored mesh is the same shape
     *  on every revision this client boots. */
    TORIRS_PLUGIN_MODEL_MESH,
    /** A handle from model_load: a model FILE the plugin ships, in its own
     *  asset folder. Portable for the same reason an authored mesh is -- the
     *  geometry travels with the plugin -- and the way to ship real art rather
     *  than something computed from trigonometry. */
    TORIRS_PLUGIN_MODEL_ASSET
};

/**
 * What draw_hull wraps.
 *
 * The two differ in what they measure, not in how carefully they draw. BOUNDS
 * is the model's bounds cylinder, which has one radius for every horizontal
 * direction: a thing with anything sticking out -- a halberd, a cape, a wing
 * -- is wrapped at that reach on all four sides, so on screen it is a box
 * around the entity rather than an outline of it. That is the right shape when
 * what is being marked is the entity's PRESENCE (it never disappears, it never
 * cuts into the model, and it costs eight projections however dense the mesh),
 * and the wrong one when the outline is meant to read as the entity's own
 * edge.
 *
 * MESH hulls the posed vertices themselves, which is that edge, at one
 * projection per vertex per frame. Prefer it for a few marked entities;
 * BOUNDS stays the default and the sane choice for marking a crowd.
 */
enum ToriRS_PluginHullShape
{
    /** The bounds cylinder as an eight-corner box. Fixed cost. */
    TORIRS_PLUGIN_HULL_BOUNDS = 0,
    /** The model's own posed geometry: tight, and linear in the mesh. */
    TORIRS_PLUGIN_HULL_MESH = 1
};

/* ------------------------------------------------------------------------ */
/* Owning the gameframe                                                      */
/* ------------------------------------------------------------------------ */

/**
 * The live surfaces a layout arranges.
 *
 * A layout plugin brings its own ART -- the stones, the panels, the tab strip
 * -- and it cannot bring these. The 3D scene, the minimap, the chat log, the
 * open sidebar interface and the modal region are the CLIENT's, wired to the
 * cache, the server and the world, and a plugin that tried to reproduce one
 * would be writing a second client. So the frame is split in two: the plugin
 * draws the picture and states where each of these belongs inside it, and the
 * host puts them there.
 *
 * Which node each slot is on this lane is the HOST's problem, and it is a real
 * one: on a 2004 dat1 frame they are revconfig builtins, and on an OldSchool
 * cache they are components of interface 548/161/164 carrying a clientCode.
 * Naming them by role rather than by id is what lets one layout serve both.
 *
 * A slot the gameframe does not have is not an error. A frame with no compass
 * answers the placement with 0 and the plugin draws no compass housing.
 */
enum ToriRS_PluginLayoutSlot
{
    /** The 3D scene. Every gameframe has one. */
    TORIRS_PLUGIN_SLOT_VIEWPORT = 0,
    /** The map square itself, not the stone ring around it -- the box a click
     *  hit-tests against, and the box the map was drawn with rather than the
     *  one the layout asked for. The two agree almost always. */
    TORIRS_PLUGIN_SLOT_MINIMAP,
    TORIRS_PLUGIN_SLOT_COMPASS,
    /** The chat log and its input line. */
    TORIRS_PLUGIN_SLOT_CHAT,
    /** Whichever sidebar interface is open -- the inventory, the spellbook,
     *  the stats page. One slot and not fourteen: only one is up at a time,
     *  and a layout that had to place each would be stating the same rectangle
     *  fourteen times. */
    TORIRS_PLUGIN_SLOT_SIDEBAR,
    /** Where a bank, a level-up or a dialogue opens. */
    TORIRS_PLUGIN_SLOT_MAIN_MODAL,
    /**
     * The chat filter buttons -- public, private, trade, report abuse.
     *
     * A role rather than art, and the distinction is the whole reason it is
     * here: they wear the same stone as the surround and sit in the same
     * strip, so a layout replacing the frame's decoration takes them with it
     * -- and they are four working CONTROLS. Suppressed, the player loses the
     * privacy toggles and gets four empty plates where they were.
     *
     * The only role with four members at four DIFFERENT boxes, which is what
     * layout_slot_at exists for. Their member numbers are the filters
     * themselves: 0 public, 1 private, 2 trade, 3 report.
     */
    TORIRS_PLUGIN_SLOT_CHAT_BUTTONS,

    /**
     * Where the roles a layout may PLACE stop.
     *
     * Everything below this line is DERIVED: the host works it out from the
     * frame and from what plugins have reserved, and layout_slot refuses it.
     * The two kinds are in one enum because they are read through one verb and
     * name boxes on one screen -- and they have to be told apart, because
     * "place the canvas somewhere" is not a sentence.
     */
    TORIRS_PLUGIN_SLOT_PLACEABLE_COUNT,

    /**
     * The whole client window.
     *
     * The denominator, and the one region that can never fail to answer, which
     * makes it the bottom of every fallback chain.
     */
    TORIRS_PLUGIN_SLOT_CANVAS = TORIRS_PLUGIN_SLOT_PLACEABLE_COUNT,

    /**
     * The largest part of the canvas no chrome is sitting on.
     *
     * The region a readout actually wants, and the reason this enum grew a
     * derived half. "Where is the middle of the screen" has no single answer a
     * frame can state: on a fixed frame it is the viewport, because the chrome
     * is outside it; on a resizable one the viewport is the whole window and
     * the minimap, the chatbox and the sidebar float on top of it, so the same
     * question has a much smaller answer.
     *
     * DERIVED rather than declared, and that is the value of it: it is
     * computed from what is actually claimed, so it stays right when a plugin
     * adds a dock nobody anticipated. A declared safe region is only ever as
     * current as the last person who remembered to update it.
     *
     * GAMECHROME names the occluder: this is the canvas minus the CLIENT's
     * own furniture. What the OPERATING SYSTEM covers (the soft keyboard) is
     * a different question with a different answer -- @see
     * ToriRS_PluginApi::safe_os.
     *
     * @see layout_reserve, which is how a plugin takes a bite out of it.
     */
    TORIRS_PLUGIN_SLOT_SAFE_GAMECHROME,

    TORIRS_PLUGIN_SLOT_COUNT
};

/** Which side of a region a reservation eats. @see layout_reserve. */
enum ToriRS_PluginEdge
{
    TORIRS_PLUGIN_EDGE_LEFT = 0,
    TORIRS_PLUGIN_EDGE_RIGHT,
    TORIRS_PLUGIN_EDGE_TOP,
    TORIRS_PLUGIN_EDGE_BOTTOM,

    TORIRS_PLUGIN_EDGE_COUNT
};

/** What the client canvas does under this layout. @see layout_claim. */
enum ToriRS_PluginLayoutCanvas
{
    /**
     * The canvas is the window: the layout is re-declared at whatever size the
     * user drags it to.
     *
     * EV_LAYOUT then fires on every resize, and the plugin's arithmetic has to
     * be in terms of the box it was handed rather than in constants.
     */
    TORIRS_PLUGIN_CANVAS_FOLLOW_WINDOW = 0,
    /**
     * The canvas is pinned to the size the claim named, and the window
     * letterboxes it.
     *
     * The 765x503 frames want this and not "resizable at 765x503": the two
     * differ the moment the window is not that size, and the difference is
     * whether the art is magnified into the window or stranded in a corner of
     * it.
     */
    TORIRS_PLUGIN_CANVAS_FIXED = 1
};

/* ------------------------------------------------------------------------ */
/* Chrome: dressing one PART of a frame somebody else arranged               */
/* ------------------------------------------------------------------------ */

/*
 * The second tier of the frame.
 *
 * Arranging is exclusive per FRAME -- layout_claim, one plugin, the whole
 * gameframe. Dressing is exclusive per PART: the report button, one orb, a
 * single control, claimed by any plugin without owning the frame around it.
 *
 * The two are ordered by construction. EV_LAYOUT declares the frame and
 * EV_CHROME dresses it, in that order, in one pass -- so a plugin replacing a
 * button always reads the box THIS pass put it in.
 *
 * A part is addressed by ROLE NAME, the same namespace role_replace and
 * role_anchor use, because a name is the only address that survives changing
 * lanes: the report button is a chat_buttons member on a 2004 frame and cache
 * component 162:31 on OldSchool, and the profile for each revision says which.
 * A plugin says "report_button" and stops caring.
 */

/** Which picture a part wears. -1 in `art` is a state the part has not got. */
enum ToriRS_PluginChromeState
{
    TORIRS_PLUGIN_CHROME_IDLE = 0,
    TORIRS_PLUGIN_CHROME_HOVER,
    /** Selected -- the chat filter this box is showing. */
    TORIRS_PLUGIN_CHROME_ACTIVE,
    TORIRS_PLUGIN_CHROME_ACTIVE_HOVER,
    TORIRS_PLUGIN_CHROME_DISABLED,

    TORIRS_PLUGIN_CHROME_STATE_COUNT
};

/**
 * Where an anchored thing goes relative to the object it is anchored to.
 *
 * An anchor is a NAME, and the name may be provided by the lane, by a plugin
 * that replaced it, or by a plugin that introduced it; wherever the object
 * paints, what is hung off it paints in this order:
 *
 *   BEFORE  the anchored drawing, then the object
 *   AFTER   the object, then the anchored drawing
 *
 * Which is the whole difference between a readout that sits on a housing and
 * one the housing covers. Arrival order used to decide it, and arrival order
 * is which plugin happened to claim first.
 */
enum ToriRS_PluginAnchorPlace
{
    TORIRS_PLUGIN_ANCHOR_AFTER = 0,
    TORIRS_PLUGIN_ANCHOR_BEFORE = 1,
};

/** Which authority a part came from. @see chrome_part. */
enum ToriRS_PluginChromeSource
{
    /** Nothing is there. */
    TORIRS_PLUGIN_CHROME_SOURCE_NONE = 0,
    /**
     * The cache's or the revconfig's own.
     *
     * A real box and no art: the picture belongs to a cache this plugin
     * cannot decode and would be a different picture on the next revision. A
     * dresser on this lane ships its own.
     */
    TORIRS_PLUGIN_CHROME_SOURCE_LANE,
    /** A frame arranger declared it in EV_LAYOUT. Art handles are real. */
    TORIRS_PLUGIN_CHROME_SOURCE_FRAME,
    /** A plugin INTRODUCED it: this revision has no node for it at all.
     *  Art handles are real. @see chrome_add. */
    TORIRS_PLUGIN_CHROME_SOURCE_ADDED
};

/**
 * The SCOPES of a part: which of its aspects a claim takes.
 *
 * A part is not one thing to own. Where it is, what it looks like and what a
 * click on it does are three questions with three different natural owners
 * -- a layout plugin moves the report button, a skin plugin recolours it, an
 * accessibility plugin makes it bigger to hit -- and a claim that took all
 * three at once would make any two of those plugins mutually exclusive for no
 * reason either could name.
 *
 * So a claim is on (part, scope), exclusive per pair, and three plugins may
 * hold the three scopes of one part. Bits, so a plugin that wants two asks
 * once.
 */
enum ToriRS_PluginChromeScope
{
    /** The box: x, y, w, h of chrome_paint. */
    TORIRS_PLUGIN_CHROME_SCOPE_POSITION = 1 << 0,
    /** The pictures: art[], label_x, label_y, and chrome_state. */
    TORIRS_PLUGIN_CHROME_SCOPE_APPEARANCE = 1 << 1,
    /** The click: chrome_ops, and the region it is served from. */
    TORIRS_PLUGIN_CHROME_SCOPE_HITBOX = 1 << 2,

    TORIRS_PLUGIN_CHROME_SCOPE_ALL = (1 << 3) - 1
};

/**
 * One dressable part: where its picture is, and what that picture is.
 *
 * The box is the ART's, which is NOT the role's box, and that difference is
 * the whole reason this struct exists. role_rect answers where the LABEL
 * mounts -- 100x25 for a 2004 chat button -- while the plate composed for it
 * is 100x23 sitting three rows lower. A plugin painting the role's rectangle
 * overhangs the plate it meant to replace.
 */
struct ToriRS_PluginChromePart
{
    /*
     * Canvas coordinates on the way OUT of chrome_part, always.
     *
     * On the way IN they are relative to the part's anchor, which for
     * everything except an added part IS the canvas -- so the two spellings
     * coincide for a native or arranger-declared part and differ only where
     * the difference is the point. @see chrome_add.
     */
    int x;
    int y;
    int w;
    int h;

    /**
     * One handle per enum ToriRS_PluginChromeState, -1 for a state this part
     * has not got.
     *
     * -1 at ACTIVE is an ANSWER and not an omission: it says the button does
     * not select anything, it opens something. Report abuse is that button on
     * every frame in this tree.
     *
     * A state with no art of its own falls back to IDLE, so the common part --
     * one picture, no hover -- states one handle and leaves the rest -1.
     */
    int art[TORIRS_PLUGIN_CHROME_STATE_COUNT];

    /** Where a caption or an icon centres inside the art, for a replacement
     *  that keeps the plate and changes only what is on it. */
    int label_x;
    int label_y;

    /** enum ToriRS_PluginChromeSource. Written by chrome_part; ignored on
     *  every call that DECLARES a part. */
    int source;
};

/* ------------------------------------------------------------------------ */
/* Entities: claiming a thing in the WORLD                                   */
/* ------------------------------------------------------------------------ */

/*
 * The same tier, pointed at the scene instead of the frame.
 *
 * An npc, a player, a ground item or a loc is a PART like the report button
 * is: it has a picture, a click and a place, three plugins may want one each,
 * and two plugins that both outline the same npc every frame are two plugins
 * disagreeing about one thing with nothing to arbitrate. So an entity is
 * claimed through chrome_claim under a name of its own, with the same scopes,
 * the same three answers and the same teardown -- one exclusion set, not two.
 *
 * What differs is what each scope can DO, because a server entity is not a
 * plugin's to move and its model is not a plugin's to repaint:
 *
 *   APPEARANCE  the hull: outline and fill, as draw_hull draws it, declared
 *               once with entity_look and painted by the host every world
 *               frame. draw_hull itself is refused for an entity whose
 *               APPEARANCE another plugin holds, which is the whole of how
 *               two highlighters stop fighting.
 *   HITBOX      the click: the rows a right-click offers, declared with
 *               entity_ops. APPEND keeps the game's own rows and adds these;
 *               REPLACE drops the game's rows for this thing and offers only
 *               these; NONE drops them and offers nothing -- the thing is
 *               unclickable for as long as the claim stands.
 *   POSITION    refused. Where an entity is is the server's sentence, and a
 *               claim that said yes and moved nothing would be the silent
 *               no-op this contract exists to prevent.
 *
 * A name is `<kind>:<ids>`, spelled by entity_part so no plugin formats one by
 * hand: `npc:<server_slot>`, `player:<pid>`, `loc:<x>,<z>,<level>,<id>`,
 * `obj:<x>,<z>,<level>,<id>` -- the identities that survive a scene rebuild,
 * never a scene element, which does not.
 *
 * A claim on a thing that is not there yet is ordinary and stands: an npc
 * slot is claimed at EV_START and binds to whatever spawns into it. That is
 * also the caveat -- a slot is reused, so a plugin that means "this goblin"
 * and not "whatever is in slot 12" watches base_npc_id and releases.
 */

enum ToriRS_PluginEntityKind
{
    TORIRS_PLUGIN_ENTITY_NPC = 1,
    TORIRS_PLUGIN_ENTITY_PLAYER,
    TORIRS_PLUGIN_ENTITY_LOC,
    TORIRS_PLUGIN_ENTITY_OBJ
};

/** What a HITBOX holder does to the game's own rows. @see entity_ops. */
enum ToriRS_PluginEntityOpsMode
{
    /** The game's rows stay; the plugin's are added. */
    TORIRS_PLUGIN_ENTITY_OPS_APPEND = 0,
    /** The game's rows for this thing go; the plugin's stand alone. */
    TORIRS_PLUGIN_ENTITY_OPS_REPLACE,
    /** The game's rows go and nothing replaces them: not clickable. */
    TORIRS_PLUGIN_ENTITY_OPS_NONE
};

/** An APPEARANCE holder's standing declaration for an entity. */
struct ToriRS_PluginEntityLook
{
    /** Draw a hull at all. 0 is "claimed and invisible", which is how a
     *  plugin that only wants the click keeps another's outline off it. */
    int hull;
    /** 0xRRGGBB. */
    uint32_t rgb;
    /** Fill alpha 0..255; 0 for an outline alone. */
    int fill_alpha;
    /** enum ToriRS_PluginHullShape. */
    int shape;
};

/* ------------------------------------------------------------------------ */
/* The api table                                                             */
/* ------------------------------------------------------------------------ */

struct ToriRS_PluginApi
{
    uint32_t abi_version;

    /* -- bus + diagnostics -- */

    void (*subscribe)(
        struct ToriRS_PluginCtx* ctx,
        enum ToriRS_PluginEvent event,
        ToriRS_PluginHandler handler,
        void* userdata);
    /** Prefixed with the plugin name; goes wherever the client's log goes. */
    void (*log)(struct ToriRS_PluginCtx* ctx, char const* fmt, ...);
    /**
     * Say something to the PLAYER, in the chatbox, as a game message.
     *
     * Not api->log, which goes to stderr and which nobody playing the game can
     * see. Several of the client's own settings are worded as "a notification
     * will be sent" -- the bird nest drop, the cannon running low, a trap
     * finishing -- and a notification with nowhere to appear is the same as
     * the setting doing nothing.
     *
     * A GAME message, the same type the server's own system lines use, so it
     * filters and scrolls with them. There is deliberately no sender and no
     * type argument: a plugin speaking as a player, or into the private-chat
     * stream, would be putting words in someone's mouth.
     */
    void (*notify)(struct ToriRS_PluginCtx* ctx, char const* text);

    /**
     * Switch this plugin off, now, and say why.
     *
     * For the plugin that can only tell whether it belongs here once it can
     * SEE where "here" is: the lane it booted on, the cache's own content, a
     * device this build has not got. That decision cannot be made in the def
     * -- a def is compiled -- and it must not be made by running anyway and
     * drawing nothing, because a plugin that is switched on and inert is a
     * feature that silently does not work with nothing on any screen to say
     * so.
     *
     * `reason` is what the roster shows beside the row and what the boot line
     * prints, so it is written for a PERSON: it says which lane refused the
     * plugin, not which branch was taken.
     *
     * WHAT THIS DOES NOT DO IS EDIT THE USER'S SWITCH. The saved `enabled=`
     * is a preference stated once for every lane this client boots, and a
     * client that cleared it on the one lane that cannot use the plugin would
     * lose the preference for all the others -- boot an OldSchool world once
     * and the gameframe chosen on a 2004 world is forgotten. So the plugin is
     * torn down and reported off, the saved line is left exactly as it was,
     * and changing lanes is all it takes to have the feature back.
     *
     * Legal from `init` and from any handler, and the earlier the better:
     * from `init`, before subscribing, nothing is ever registered at all. The
     * calling handler keeps running afterwards -- this cannot return for it --
     * so it should do nothing but return.
     */
    void (*disable_self)(struct ToriRS_PluginCtx* ctx, char const* reason);

    /* -- where the client is -- */

    /**
     * Which SCREEN the client is showing, as a TORIRS_PLUGIN_SCREEN_* value.
     *
     * The question a plugin that dresses the GAMEFRAME has to ask before it
     * does anything at all: there is no gameframe on the title screen, so a
     * claim made there is a claim on parts that do not exist, and the parts
     * that DO exist -- the title background, the logo, the login box -- get
     * covered by furniture drawn for a frame nobody is looking at yet.
     *
     * That is not hypothetical: it is what "half the static sprites are
     * missing" looked like, and the plugin doing it was behaving exactly as
     * written -- it had no way to ask. A plugin whose effect belongs in the
     * game gates on TORIRS_PLUGIN_SCREEN_GAME and leaves the rest alone.
     *
     * CONNECTING is deliberately its own value rather than folded into either
     * neighbour: the title tree is still up and still drawing (that is where
     * "Connecting to server..." appears), so it is a screen where gameframe
     * work is still wrong -- but a plugin that wants to know a login is in
     * flight can see it without inferring it from a transition.
     */
    int (*screen)(struct ToriRS_PluginCtx* ctx);

    /**
     * The part of the canvas the OPERATING SYSTEM is not covering, in canvas
     * coordinates. Always answers 1 and always fills a non-empty box.
     *
     * Not the `safe_gamechrome` role, and the difference is who the occluder
     * answers to. `safe_gamechrome` is the canvas minus the CLIENT's own
     * chrome -- regions the frame declared, edges plugins reserved -- and
     * this, `safe_os`, is the canvas minus what
     * the platform put on top of the whole window: today the soft keyboard, a
     * band off the bottom while it is up. On a desktop, and on a phone with
     * the keyboard away, the answer IS the canvas.
     *
     * The question a mobile frame asks in EV_LAYOUT: a chatbox pinned to the
     * canvas's bottom edge is pinned under the keyboard the moment one is
     * raised, so the bottom it wants is THIS box's. The host re-declares the
     * layout when the answer changes -- a keyboard arriving is a layout event
     * exactly as a resize is -- so reading it in EV_LAYOUT is enough; nothing
     * needs to poll.
     *
     * Answered on every screen, unlike the frame queries: the keyboard is a
     * property of the WINDOW, and a plugin sizing something on the title
     * screen is as entitled to the answer as the gameframe is.
     */
    int (*safe_os)(
        struct ToriRS_PluginCtx* ctx, int* out_x, int* out_y, int* out_w, int* out_h);

    /* -- clocks -- */

    /** World cycle (advances once per 20ms client tick). */
    int (*world_cycle)(struct ToriRS_PluginCtx* ctx);
    uint64_t (*frame_ms)(struct ToriRS_PluginCtx* ctx);

    /**
     * How long the last frame's WORK took, in microseconds.
     *
     * The pacing sleep is not in it. That is the whole point of having this
     * next to frame_ms: two frame_ms stamps subtract to the frame's wall-clock
     * period, and under a frame cap that period is the cap -- a client with
     * 4 ms of work in a 20 ms budget and one with 19 ms of work both measure
     * 20 ms, and neither number moves until the client can no longer keep up.
     * This one moves the whole time, and a second divided by it is the rate
     * the client could sustain if nothing held it back.
     *
     * The newest sample, not an average. A plugin that wants a window keeps
     * its own; one that wants this frame could not get it back out of a mean.
     * 0 before any frame has been measured.
     */
    uint64_t (*frame_work_us)(struct ToriRS_PluginCtx* ctx);

    /* -- world queries; 1 = filled, 0 = absent -- */

    int (*local_player)(struct ToriRS_PluginCtx* ctx, struct ToriRS_PluginPlayerSnap* out);
    /** Iterate: pass -1 to start; returns the next iterator, or -1 when done.
     *  `out` is filled for every returned iterator. */
    int (*npc_next)(struct ToriRS_PluginCtx* ctx, int iter, struct ToriRS_PluginNpcSnap* out);
    int (*npc_by_slot)(
        struct ToriRS_PluginCtx* ctx,
        int server_slot,
        struct ToriRS_PluginNpcSnap* out);
    int (*player_next)(
        struct ToriRS_PluginCtx* ctx,
        int iter,
        struct ToriRS_PluginPlayerSnap* out);
    /** Ground-item stacks, iterated like npc_next. One entry per (tile, obj)
     *  pair -- a tile holding three different items yields three. */
    int (*obj_next)(struct ToriRS_PluginCtx* ctx, int iter, struct ToriRS_PluginObjSnap* out);
    /** Locs in the loaded scene, iterated like npc_next. A busy city scene
     *  holds thousands of them, so a caller that wants one kind tests
     *  `loc_id` inside the walk rather than collecting the list first. */
    int (*loc_next)(struct ToriRS_PluginCtx* ctx, int iter, struct ToriRS_PluginLocSnap* out);
    /**
     * What the cache has asked to be marked, resolved against live world
     * state. Iterated like npc_next.
     *
     * The resolution is redone at the START of each walk -- pass -1 and the
     * list is rebuilt -- so a walk is a consistent snapshot and two walks in
     * one frame cost twice. One walk per frame, in EV_DRAW_WORLD, is the
     * intended shape.
     */
    int (*highlight_next)(
        struct ToriRS_PluginCtx* ctx, int iter, struct ToriRS_PluginHighlightItem* out);

    /* -- input -- */

    /** enum LibToriRS_KeyCode. */
    int (*key_held)(struct ToriRS_PluginCtx* ctx, int keycode);

    /** The tile under the mouse pointer, ABSOLUTE, as the last rendered frame
     *  picked it -- the same tile the client's own click paths would act on.
     *  Returns 0, leaving the outputs untouched, when the pointer is outside
     *  the world viewport or over no terrain at all.
     *
     *  `*out_level` is the WALKED level, the one every other level in this api
     *  is: on a bridge deck it is the plane the player standing there is on,
     *  not the plane the map authored the floor on. */
    int (*hover_tile)(
        struct ToriRS_PluginCtx* ctx,
        int* out_tile_x,
        int* out_tile_z,
        int* out_level);

    /** The nearest entity under the pointer. Returns 0, leaving `out`
     *  untouched, when the pointer is over no entity at all -- which is not
     *  the same as being outside the viewport, and a caller that needs to tell
     *  those apart asks hover_tile too. */
    int (*hover_entity)(struct ToriRS_PluginCtx* ctx, struct ToriRS_PluginHoverEntity* out);

    /**
     * Where the pointer is, in canvas coordinates. Returns 0, leaving the
     * outputs untouched, when the client has no pointer position yet.
     *
     * Not hover_tile, which answers with the GROUND under the pointer, and not
     * hover_entity, which answers with a thing in the world. This is the raw
     * point, and what it is for is chrome: a control the plugin drew knows its
     * own box and needs only the pointer to light up when it is inside one.
     * The alternative -- a hover event per region -- would be the same test
     * done by the host, once per region, on a frame where the plugin is
     * already walking its own list to draw them.
     */
    int (*mouse_pos)(struct ToriRS_PluginCtx* ctx, int* out_x, int* out_y);

    /* -- the chrome's own geometry --
     *
     * Where the client PUT something, as this frame's layout resolved it. A
     * plugin drawing chrome has to be able to anchor to the chrome, and the
     * numbers are not knowable any other way: the minimap is at 550,4 in a
     * 2004 fixed frame, top-right of a resizable canvas in a modern one, and
     * somewhere else again the moment a window is resized.
     */

    /**
     * Any region's box, in canvas coordinates. Any out may be NULL.
     *
     * The read half of the layout vocabulary, and deliberately the SAME
     * vocabulary the write half uses: a plugin that moves
     * TORIRS_PLUGIN_SLOT_VIEWPORT and a plugin that reads it are then talking
     * about one thing rather than two that happen to agree. There were once
     * three names for these boxes -- the role list, a single-purpose
     * `minimap_rect`, and an anchor enum of its own -- and nothing connected
     * them but the implementation happening to route them to the same place.
     * This is the one that survived; the other two are gone.
     *
     * @return 1 when this gameframe has that region and it has a size, 0
     * otherwise, and 0 leaves the outs untouched. A role a frame does not have
     * is an ANSWER, not a fault -- plenty of frames have no compass -- so the
     * idiom is to ask for the tightest region first and fall back:
     *
     *     if( !api->slot_rect(ctx, TORIRS_PLUGIN_SLOT_SAFE_GAMECHROME, ...) &&
     *         !api->slot_rect(ctx, TORIRS_PLUGIN_SLOT_MAIN_MODAL, ...) &&
     *         !api->slot_rect(ctx, TORIRS_PLUGIN_SLOT_VIEWPORT, ...) )
     *         api->slot_rect(ctx, TORIRS_PLUGIN_SLOT_CANVAS, ...);
     *
     * Answered from live state on every call and never cached by the host, so
     * a change by any writer is visible on the next read with no invalidation
     * protocol to get wrong. It is a few field reads; the staleness bug it
     * removes is not.
     */
    int (*slot_rect)(
        struct ToriRS_PluginCtx* ctx,
        int slot,
        int* out_x,
        int* out_y,
        int* out_w,
        int* out_h);

    /**
     * One MEMBER of a region, in canvas coordinates. Otherwise exactly
     * slot_rect. Any out may be NULL.
     *
     * The read half of layout_slot_at, and `member` is the same number that
     * one takes: the role's OWN numbering and never a position in a list. A
     * chat button's is the filter it toggles -- 0 public, 1 private, 2 trade,
     * 3 report abuse -- and a sidebar mount's is its tab number. Reading the
     * report button's box through "the fourth chat button I find" would be
     * asking for whatever order this revision happened to build in.
     *
     * slot_rect answers for the role as a whole, which for CHAT_BUTTONS is ONE
     * of the four and not the strip: a plugin that wants a particular button
     * has to name it, and this is where it does.
     *
     * @return 1 when this gameframe has a member of that role with that
     * number and it has a size, 0 otherwise -- a frame with no Report abuse
     * button is an answer, not a fault.
     */
    int (*slot_member_rect)(
        struct ToriRS_PluginCtx* ctx,
        int slot,
        int member,
        int* out_x,
        int* out_y,
        int* out_w,
        int* out_h);

    /**
     * Take `px` off one edge of a region, for as long as this plugin runs.
     *
     * The COOPERATIVE claim, and the one most plugins should reach for. Many
     * plugins may reserve; the host subtracts them in declaration order, so
     * two plugins that each want the right edge stack rather than fight, and
     * neither has to know the other exists. layout_slot is the exclusive verb
     * -- it says "this region IS this box" and only the frame's owner may say
     * it -- and a design with only that one makes any two plugins that touch
     * the frame mutually exclusive.
     *
     * `px` of 0 drops this plugin's claim on that edge and leaves every other
     * plugin's standing. Every claim is dropped for you when the plugin stops,
     * in the same teardown that reclaims its images, meshes and window tab.
     *
     * Only the derived regions can be reserved from -- SAFE is the one that
     * means anything -- because a placeable role is whatever the frame says it
     * is, and shrinking it here would be arguing with the layout rather than
     * making room beside it.
     *
     * @return 1 when the claim was recorded, 0 for a region that cannot be
     * reserved from, an edge out of range, or a plugin at its claim budget.
     */
    int (*layout_reserve)(
        struct ToriRS_PluginCtx* ctx,
        int slot,
        int edge,
        int px);

    /**
     * A counter that moves whenever anything about the layout does.
     *
     * For plugins that CACHE something measured against a region -- a composed
     * image, a laid-out panel. Reading a region is free and always current, so
     * a plugin that only looks while drawing needs none of this; one that
     * rasterised a picture against last frame's box needs to know the box
     * moved, and comparing one int is the cheapest way to ask.
     *
     * EV_LAYOUT_CHANGED is the same news pushed rather than polled. Both
     * exist because both shapes of plugin exist.
     */
    int (*layout_revision)(struct ToriRS_PluginCtx* ctx);

    /* -- owning the gameframe --
     *
     * One plugin at a time arranges the frame, and while it does, the lane's
     * OWN chrome is switched off: the 2004 stone surround, the OldSchool
     * toplevel's backing sprites, the tab strip that came with the cache. That
     * is not a side effect to be minimised, it is what a layout plugin is --
     * two frames drawn at once is two sets of stones over one inventory.
     *
     * What survives is the list in ToriRS_PluginLayoutSlot: the surfaces no
     * plugin can author. The plugin draws everything else itself, in
     * EV_DRAW_FRAME, out of art it ships.
     */

    /**
     * Claim the frame for this plugin.
     *
     * `canvas` is enum ToriRS_PluginLayoutCanvas. `fixed_w`/`fixed_h` are the
     * pinned canvas for CANVAS_FIXED; for FOLLOW_WINDOW they are the SMALLEST
     * canvas this layout can be declared against, and the window's size is used
     * everywhere above it.
     *
     * A minimum rather than nothing, because the client's own floor is the
     * classic frame's 765x503 and that floor is a statement about a REVCONFIG
     * gameframe: every rev-230 child is authored as an inset off it, so a
     * smaller canvas gives them zero-sized viewports. A plugin layout is
     * authored as arithmetic on the canvas it is handed and has no such
     * breaking point -- a phone-shaped frame is narrower than 765 and is not
     * thereby broken. Whoever computes the frame is who knows how small it can
     * be computed, so the claim is where that number belongs.
     *
     * Passing the classic frame's own size here is what a desktop layout should
     * do, and it reproduces the client's floor exactly.
     *
     * Idempotent for the plugin that already holds it, which is what makes a
     * claim in the START handler and a re-claim after a config change the same
     * call. Refused -- returning false, changing nothing -- when ANOTHER plugin
     * holds it: the loser must be able to carry on drawing whatever it drew
     * before, and a claim that half-succeeded would leave two plugins each
     * believing they own the stones.
     *
     * A successful claim does NOT declare the frame on the spot: it marks the
     * frame as needing one, and EV_LAYOUT arrives on the client's next layout
     * pass, with the canvas the client actually has. A plugin therefore places
     * its slots in exactly one place -- its EV_LAYOUT handler -- and nowhere
     * else, and it must not assume the frame is declared by the time this
     * returns. Anything drawn before the first EV_LAYOUT should draw nothing.
     *
     * The claim is dropped when the plugin stops, so a disabled layout plugin
     * gives the lane's own gameframe back rather than leaving the client with
     * no frame at all.
     */
    bool (*layout_claim)(
        struct ToriRS_PluginCtx* ctx,
        int canvas,
        int fixed_w,
        int fixed_h);
    /** Hand the frame back. The lane's own chrome returns on the next layout
     *  pass. Harmless for a plugin that does not hold the claim. */
    void (*layout_release)(struct ToriRS_PluginCtx* ctx);
    /** 1 when THIS plugin holds the frame. */
    int (*layout_owned)(struct ToriRS_PluginCtx* ctx);

    /**
     * Place one slot, in canvas coordinates. Legal only inside EV_LAYOUT
     * (asserted), because that dispatch IS the declaration -- see the event.
     *
     * `slot` is enum ToriRS_PluginLayoutSlot. A slot placed twice keeps the
     * last rectangle; a slot never placed is hidden for as long as this
     * declaration stands.
     *
     * @return 1 when the slot was recorded AND this gameframe has a surface
     * for it, 0 when the frame has no such surface -- which is the answer to
     * "should I draw the housing for it", and the reason the placement reports
     * anything at all.
     */
    int (*layout_slot)(
        struct ToriRS_PluginCtx* ctx,
        int slot,
        int x,
        int y,
        int w,
        int h);

    /**
     * Place ONE member of a role. Otherwise exactly layout_slot, and legal in
     * the same one place.
     *
     * `member` is the role's OWN numbering and never a position in a list: a
     * chat button's is the filter it toggles, a sidebar mount's is its tab
     * number. That distinction is what keeps a declaration true when the frame
     * is rebuilt, reordered, or authored by a different cache -- a list
     * position is whatever order this revision happened to build in, and the
     * next one changes it.
     *
     * A member placed here wins over anything layout_slot said about the role;
     * a member with no box of its own falls back to it; a member with neither
     * is hidden. So "all four in a row, but the report button wider" is two
     * calls, and "the sidebar, wherever it is" is still one.
     *
     * @return 1 when this gameframe has a member of that role with that
     * number, 0 otherwise -- so a layout can tell a frame with no Trade/duel
     * button from one that has it somewhere unexpected.
     */
    int (*layout_slot_at)(
        struct ToriRS_PluginCtx* ctx,
        int slot,
        int member,
        int x,
        int y,
        int w,
        int h);

    /**
     * The art one live surface is drawn from, and the shape it is cut to.
     * Legal in the same one place as layout_slot.
     *
     * The two surfaces that are neither the plugin's to draw nor the lane's to
     * decide: the COMPASS turns with the camera and the MINIMAP is baked from
     * the world, so a layout cannot blit either -- and both are drawn from a
     * picture that belongs to the FRAME rather than to the world behind it. A
     * 2004 frame's compass on an OldSchool surround is the same mismatch as
     * 2004 stones around an OldSchool inventory, and the layout that replaced
     * the one has no way to replace the other without this.
     *
     * `art` replaces COMPASS art; it must be -1 for MINIMAP, whose picture is
     * the live baked world. -1 keeps the lane's compass art. `mask` is an alpha
     * cut-out, and it is what makes a floating frame possible at all:
     * the OldSchool resizable map surround is a RING with the scene showing
     * through everywhere it is not, so an unmasked square of minimap draws its
     * corners over the world outside the ring. -1 is no mask, which is right
     * for a housing that is opaque around its hole.
     *
     * Plugin masks use a stable polarity across cache eras: transparent pixels
     * are the window and opaque pixels are clipped away.
     *
     * Both are image handles from image_load or image_compose, and an image
     * still crossing the IO queue is refused rather than remembered -- the
     * layout pass runs again on the next resize or rebuild, which is when a
     * handle that was not resident yet becomes one.
     *
     * @return 1 when this gameframe has a surface of that role to skin, 0
     * otherwise.
     */
    int (*layout_slot_skin)(
        struct ToriRS_PluginCtx* ctx,
        int slot,
        int art,
        int mask);

    /**
     * Dress every vertical scrollbar in this layout's art. Legal in the same
     * one place as layout_slot.
     *
     * A scrollbar is not a slot -- there is no one node to place, and a frame
     * has as many of them as it has scrolling panels -- but it IS part of the
     * frame's look, and the 2004 bar inside an OldSchool chatbox is the same
     * mismatch as a 2004 compass inside an OldSchool map housing.
     *
     * SIX pieces, because the reference's own scrollbar is six: a trough tiled
     * down the groove, a dragger built from two end caps and a tiled middle so
     * it can be any length, and the two arrows. That is
     * `~scrollbar_vertical_repaint` (clientscript 838) and the chatbox's call
     * to it, and a layout that supplied fewer would be inventing a bar rather
     * than reproducing one.
     *
     * All six or none: passing -1 anywhere puts every bar back to the client's
     * own painted one, which is what a layout that has no scrollbar art should
     * do rather than leave a bar with a hole in it. The declaration is rebuilt
     * from nothing each EV_LAYOUT, so a layout that stops calling this stops
     * skinning them.
     *
     * @return 1 when the skin was taken, 0 when a handle was not resident yet
     * -- the layout pass runs again at the next resize or rebuild.
     */
    int (*layout_scrollbar)(
        struct ToriRS_PluginCtx* ctx,
        int trough,
        int dragger_top,
        int dragger_mid,
        int dragger_bottom,
        int arrow_up,
        int arrow_down);

    /**
     * Which sidebar tab is showing, or -1 when this frame has no tabs.
     *
     * The tab set is the CACHE's -- which interface is on tab 3 is a fact
     * about the gameframe -- so a layout draws the stones and the icons and
     * asks this which one to draw pressed, rather than keeping a selection of
     * its own that the server could contradict.
     *
     * Which of those tabs the PLAYER has been given is a third question again.
     * @see tab_enabled, which a layout has to ask before it draws either the
     * icon or the pressed stone.
     */
    int (*tab_active)(struct ToriRS_PluginCtx* ctx);
    /**
     * Flip to tab `tabno`, exactly as a click on that tab's stone would.
     *
     * The sibling of if_click, and separate from it for the same reason
     * layout slots are named by role: the component that switches a tab is a
     * different one in every gameframe and some frames switch tabs with no
     * component at all.
     *
     * @return 1 when the frame has that tab and it was selected.
     */
    bool (*tab_select)(struct ToriRS_PluginCtx* ctx, int tabno);

    /* -- the player's numbers --
     *
     * Not varps. A skill level and the run meter arrive in packets of their
     * own (UPDATE_STAT, UPDATE_RUNENERGY) on every revision this client
     * speaks, and a plugin that went looking for them in a var would find
     * nothing at all.
     */

    /**
     * One skill: the BOOSTED level into `out_current` and the earned one into
     * `out_base`. Either out may be NULL.
     *
     * `skill` is the revision's own index -- 0 attack, 1 defence, 2 strength,
     * 3 hitpoints, and so on -- which has not moved since 2001 and is the same
     * number CS2's `stat` opcode takes.
     *
     * @return 1 when the index is in range, 0 otherwise, and 0 leaves the outs
     * untouched. A stat nothing has reported yet reads as 0/0, which is what a
     * logged-out client legitimately knows.
     */
    int (*stat)(struct ToriRS_PluginCtx* ctx, int skill, int* out_current, int* out_base);

    /**
     * One skill's EXPERIENCE, and the two thresholds the level it is inside
     * runs between. Any out may be NULL.
     *
     * Three numbers and not one because a progress bar asks for all three at
     * once and only ever together -- "how far through this level am I" is
     * `(xp - level_xp) / (next_xp - level_xp)`, and a caller handed only the
     * first has to carry its own copy of the xp table to answer it. The table
     * is the CLIENT's (RS_PlayerStats builds it at init, and it is the same
     * one the client's own "xp to next level" readout is computed from), so
     * asking for it here is what keeps one answer rather than two that agree
     * until one of them is edited.
     *
     * `out_next_xp` is 0 at the TOP of that table -- level 99, the last
     * threshold the client holds. That is not "no progress": it is a skill
     * with no next level to progress towards, and a caller drawing a meter
     * should read it as full. Virtual levels are past the client's table
     * entirely and are the caller's own extrapolation, not this one's.
     *
     * @return 1 when the index is in range, 0 otherwise, and 0 leaves the outs
     * untouched.
     */
    int (*stat_xp)(
        struct ToriRS_PluginCtx* ctx,
        int skill,
        int* out_xp,
        int* out_level_xp,
        int* out_next_xp);

    /**
     * The skill's name, "Attack" through "Summoning", or NULL past the end of
     * this client's stat table.
     *
     * A verb and not a table in this header, for the reason EvGameEvent's
     * `kind` is a string: the recogniser (game/rs_game_events.c) already owns
     * the list -- it is the one the level-up line is written from -- and a
     * second copy here would be a second thing to keep in step. A stat table
     * that grows a skill reaches every plugin without touching the ABI.
     *
     * NULL is also how a plugin learns how MANY skills there are: walk up from
     * 0 until it answers NULL. The count is a property of the revision (25
     * here, with sailing and summoning at the top of it) and not a constant a
     * plugin should be carrying.
     *
     * The string is the client's own and lives for the life of the process.
     */
    char const* (*skill_name)(struct ToriRS_PluginCtx* ctx, int skill);

    /** Run energy as the wire carries it: a percent, 0..100. */
    int (*run_energy)(struct ToriRS_PluginCtx* ctx);

    /**
     * How high an OVERHEAD hangs above a scene element, in the projector's
     * units -- feed it to api->project as `height_above_ground`.
     *
     * This is the anchor the client's own health bars, hitsplats and chat
     * heads use, so a plugin drawing a name over an npc puts it where the game
     * would have. It is the model's bounds rather than a guess from the
     * footprint: a 1x1 imp and a 1x1 chicken are different heights, and a
     * marker npc with no model at all has to sit somewhere sensible rather
     * than on the floor.
     *
     * 200 for an element with no model yet -- the reference's own
     * `logicalHeight` default, and the reason a model-less npc reads as
     * floating slightly above its tile there too. 0 for an element that is not
     * in the scene at all.
     */
    int (*element_height)(struct ToriRS_PluginCtx* ctx, int element_id);

    /* -- projection: scene-relative fine position -> canvas x/y.
     *    Returns 0 when the point is behind the near plane or off-map. -- */

    int (*project)(
        struct ToriRS_PluginCtx* ctx,
        int fine_x,
        int fine_z,
        int height_above_ground,
        int* out_x,
        int* out_y);

    /* -- config -- */

    /*
     * The store is TEXT, and what these make of it is revconfig's expression
     * grammar -- the same one a revconfig profile's numeric keys are in:
     *
     *   12   0x1F   1Fh   0b1010   #FF8000   1 << 4   (1088 << 16) | 255
     *   rgb(255, 128, 0)   rgba(0, 0, 0, 128)   hsl16(hue, sat, lum)
     *
     * Because plugin_prefs.ini is a file people edit by hand, and those are
     * the spellings they reach for. A value that is not one whole expression
     * reads as 0, silently: a colour key is read on the draw path, so saying
     * so would print once a frame forever.
     */
    int (*cfg_bool)(struct ToriRS_PluginCtx* ctx, char const* key);
    int (*cfg_int)(struct ToriRS_PluginCtx* ctx, char const* key);
    /** 0xRRGGBB. An rgba() alpha is dropped -- read the key with cfg_int for
     *  the packed ARGB word. */
    uint32_t (*cfg_color)(struct ToriRS_PluginCtx* ctx, char const* key);
    char const* (*cfg_str)(struct ToriRS_PluginCtx* ctx, char const* key);
    /**
     * Does the store hold this key at all?
     *
     * For a plugin whose keys are not a fixed schema but a set discovered at
     * runtime -- the feature-flags page, whose rows are whatever the engine
     * publishes. Every cfg_* reader ASSERTS the key exists, which is right for
     * a declared schema (reading an undeclared key is the plugin's bug) and
     * unanswerable for a discovered one: "has the user ever set this?" is a
     * real question there, and the only alternative is writing a default for
     * every key just so it can be read back.
     */
    int (*cfg_has)(struct ToriRS_PluginCtx* ctx, char const* key);
    /** Marks the store dirty and raises EV_CONFIG_CHANGED. */
    void (*cfg_set)(struct ToriRS_PluginCtx* ctx, char const* key, char const* value);

    /* -- the client's own feature flags --
     *
     * The per-era client-behaviour table (src/features/features.h) and the
     * revision's `[camera]` profile, as far as a plugin may reach them.
     *
     * WHAT IS REACHABLE IS THE ENGINE'S LIST, NOT THE PLUGIN'S. The engine
     * exposes exactly the flags whose value the CLIENT decides on its own, and
     * refuses every flag a server also reads -- the pathing model, the
     * approach model, the ground-click nearest model and its unbounded
     * extension, the route window, symmetric PvP line of sight, the run-energy
     * model, the era itself. Those are not settings, they are agreements: a
     * client that holds a different one from the server it is talking to
     * flags tiles inside a boss, walks routes the server will not honour, or
     * shows an energy bar that drains at a rate nothing else believes. There
     * is no useful "just for me" value of any of them, so there is no way to
     * ask for one here.
     *
     * `key` names a flag the engine publishes; @see feature_next to discover
     * them. Nothing is by id, because a flag is a behaviour and its position
     * in a struct is not a fact a plugin should be pinned to.
     */

    /**
     * Walk the published flags, the way npc_next / loc_next walk theirs:
     * `iter` starts at -1, the return is the iterator to pass next, and -1
     * means the walk is over. `out` is filled on every non-negative return.
     *
     * The walk is what makes the list the ENGINE's: a plugin that renders it
     * grows a row when a flag is published and loses one when a flag stops
     * being client-only, without being edited.
     */
    int (*feature_next)(
        struct ToriRS_PluginCtx* ctx, int iter, struct ToriRS_PluginFeature* out);

    /**
     * This flag's effective value right now, or `TORIRS_PLUGIN_FEATURE_UNSET`
     * for a key the engine does not publish.
     */
    int (*feature_get)(struct ToriRS_PluginCtx* ctx, char const* key);

    /**
     * Set it, or -- with `TORIRS_PLUGIN_FEATURE_UNSET` -- put it back to what
     * this boot resolved before any plugin touched it.
     *
     * The restore is the reason the sentinel exists: the value a flag has out
     * of the box is the era table's, merged with the manifest and the
     * revconfig, and a plugin cannot reconstruct that from anything it can
     * see. "Revision default" has to be a value the user can pick and get
     * back, not a number a plugin remembered once and hoped stayed true.
     *
     * @return false for a key the engine does not publish, or a value outside
     *         the flag's stated range.
     */
    bool (*feature_set)(struct ToriRS_PluginCtx* ctx, char const* key, int value);

    /* -- the client's own display settings -- */

    /**
     * One display preference: its value now, and the range it accepts.
     *
     * @param setting enum ToriRS_PluginDisplaySetting.
     * @param out_value, out_min, out_max may each be NULL.
     * @return 1 when this build has that setting, 0 otherwise, and 0 leaves
     * every out untouched. A build that drops one should cost the page its
     * row, not give it a row over nothing.
     */
    int (*display_setting)(
        struct ToriRS_PluginCtx* ctx,
        int setting,
        int* out_value,
        int* out_min,
        int* out_max);

    /**
     * Set it. Takes effect on the next frame and persists by itself -- the
     * preferences file is captured from the client's own option store, so
     * there is no save here to forget.
     *
     * @return 1 when it was applied, 0 for a setting this build does not have.
     * A value outside the range is CLAMPED rather than refused: the store
     * clamps every writer, including the cache's own settings scripts, and a
     * verb that refused here would be the only one that did.
     */
    int (*display_setting_set)(struct ToriRS_PluginCtx* ctx, int setting, int value);

    /* -- the client's own variables --
     *
     * READ ONLY, and deliberately: a varp is the server's, and a plugin that
     * wrote one would be telling the client something the server never said.
     * What this is for is the other direction -- a BUILTIN reading the switch
     * the user already has, in the cache's All Settings panel, rather than
     * growing a second one of its own in the plugin roster.
     *
     * Ids are the cache's. 0 for an id this revision does not define, which is
     * the same answer an unset var gives: a plugin that needs to tell those
     * apart is reading a var it should not be reading.
     */

    int (*varbit)(struct ToriRS_PluginCtx* ctx, int varbit_id);
    int (*varp)(struct ToriRS_PluginCtx* ctx, int varp_id);

    /**
     * Which id `name` has on THIS cache, or -1.
     *
     * `kind` is a RevConfig section type -- "varbit", "varp", "setting",
     * "script", "iface", "seq" -- and `name` is the section id, so
     * `cache_id(ctx, "varbit", "npc_highlight")` answers what the profile's
     * `[varbit:npc_highlight]` states.
     *
     * A plugin that hardcodes a number instead is pinned to one revision and
     * fails SILENTLY on any other: an id that no longer exists reads as an
     * unset var (0), which for an INVERTED row means the feature turns itself
     * on. -1 here is the honest form of the same answer -- the row does not
     * exist on this cache -- and a builtin that gets it should switch off.
     */
    int (*cache_id)(struct ToriRS_PluginCtx* ctx, char const* kind, char const* name);
    /**
     * What this client booted: the cache's lineage, container and revision.
     *
     * The coarse companion to cache_id. That one answers "does this cache have
     * the row I want", which is the question a plugin that reads a var or
     * drives an interface has; this one answers "which client am I inside",
     * which is the question a plugin whose whole reason to exist is a gap in
     * one lineage and nothing at all in another has.
     *
     * @return 1 with `out` filled, or 0 with `out` zeroed for a boot whose
     * identity is not stated yet -- which is what every field reads as, so a
     * caller that ignores the return still gets UNKNOWN rather than a lineage
     * nothing told it. Treat that as "do not decide yet" and never as "not
     * OldSchool": the cache profile lands during boot, and a plugin started
     * before it would otherwise read the absence as an answer.
     */
    int (*lane)(struct ToriRS_PluginCtx* ctx, struct ToriRS_PluginLane* out);
    /**
     * A colour-row setting, as 0xRRGGBB.
     *
     * The All Settings colour rows do not store a colour: they store
     * `colour + 1`, so that zero can mean "never chosen" for a palette whose
     * every entry -- black included -- is a legal answer. Reading one with
     * api->varp and forgetting the offset gives a colour one unit off, which
     * is invisible, so the arithmetic lives here rather than in each caller.
     *
     * `fallback` is returned for the unset case, and is where the row's own
     * default (the struct's `param_1230`) belongs.
     */
    uint32_t (*setting_color)(
        struct ToriRS_PluginCtx* ctx, int varp_id, uint32_t fallback);

    /* -- the cache's item table, and the containers holding items -- */

    /**
     * One objtype, as this cache states it. @return 1 when the record is
     * RESIDENT, 0 otherwise, and 0 leaves `out` untouched.
     *
     * A hit and never a load: this answers from what the client has already
     * decoded, because every caller is inside a frame -- a hover, a draw --
     * and an api verb that started IO would stall it. In practice that costs
     * nothing, since an item the player can see has had its icon built and its
     * record is therefore resident; an item named out of nowhere (an id read
     * from a plugin's own table) may not be, and the honest answer there is
     * "ask again next frame" rather than a record of zeroes.
     */
    int (*obj_info)(
        struct ToriRS_PluginCtx* ctx, int obj_id, struct ToriRS_PluginObjInfo* out);

    /**
     * What is in one slot of one of the client's containers. Either out may be
     * NULL.
     *
     * @param inv enum ToriRS_PluginInv.
     * @return 1 when the container exists and the slot is inside it, 0
     * otherwise, and 0 leaves the outs untouched. An EMPTY slot answers 1 with
     * an obj id of -1 -- "there is no item there" is a different fact from
     * "there is no such container", and a plugin comparing against worn
     * equipment needs to tell them apart.
     *
     * READ ONLY, for api->varp's reason: a container is the server's.
     */
    int (*inv_slot)(
        struct ToriRS_PluginCtx* ctx,
        int inv,
        int slot,
        int* out_obj_id,
        int* out_count);

    /** How many slots that container has, or 0 when the client has never been
     *  told about it. @param inv enum ToriRS_PluginInv. */
    int (*inv_size)(struct ToriRS_PluginCtx* ctx, int inv);

    /* -- minimenu; legal only inside EV_MENU_BUILD (asserted) -- */

    /** Appends a row owned by this plugin. Returns 0 when the menu is full.
     *  The host allocates the client action id and routes the selection back
     *  to this plugin with `tag` in EV_MENU_SELECT. A plugin row can never
     *  become the left-click default: its action id sorts with the
     *  Cancel/Examine group. */
    int (*menu_add)(
        struct ToriRS_PluginCtx* ctx,
        struct ToriRS_PluginEvMenuBuild* menu,
        char const* text,
        uint32_t tag);

    /* -- drawing; legal only inside EV_DRAW_WORLD / EV_DRAW_CANVAS, and only
     *    on the surface that event handed over (asserted).
     *
     *    draw_tile and draw_hull are WORLD ONLY: both name something in the
     *    scene, which the canvas surface has no relationship to. The rest work
     *    on either, in that surface's own coordinates.
     *
     *    Colours are 0xRRGGBB. `fill_alpha` is 0..255 where 0 means outline
     *    only; it is the wash's opacity, not the reference's inverted
     *    transparency. -- */

    /**
     * Absolute tile, outlined at per-corner terrain height so it stays
     * coplanar on a slope.
     *
     * The wash has a colour OF ITS OWN, unlike a hull's. A tile marker is read
     * against the ground it is drawn on rather than against a model, so the
     * two halves want to differ: a bright outline stays findable at a glance
     * while the fill is whatever tints the tile without hiding what is
     * standing on it. Pass `rgb` again to get the old single-colour marker.
     * `fill_alpha` still decides whether there is a fill at all.
     */
    void (*draw_tile)(
        struct ToriRS_PluginCtx* ctx,
        void* surface,
        int tile_x,
        int tile_z,
        int level,
        uint32_t rgb,
        uint32_t fill_rgb,
        int fill_alpha);
    /** Convex hull of a scene element: the silhouette that wraps the model in
     *  three dimensions. `shape` picks what is hulled -- see
     *  ToriRS_PluginHullShape. */
    void (*draw_hull)(
        struct ToriRS_PluginCtx* ctx,
        void* surface,
        int element_id,
        uint32_t rgb,
        int fill_alpha,
        int shape);
    void (*draw_line)(
        struct ToriRS_PluginCtx* ctx,
        void* surface,
        int x0,
        int y0,
        int x1,
        int y1,
        uint32_t rgb);
    /** Centred on `x`, with `y` as the text BASELINE -- the overlay layer's
     *  text primitive has no other mode, so there is no alignment argument to
     *  pass rather than one that would be quietly ignored. */
    void (*draw_text)(
        struct ToriRS_PluginCtx* ctx,
        void* surface,
        int x,
        int y,
        char const* text,
        uint32_t rgb);
    void (*draw_rect)(
        struct ToriRS_PluginCtx* ctx,
        void* surface,
        int x,
        int y,
        int w,
        int h,
        uint32_t rgb,
        int fill_alpha);

    /* -- canvas hit regions; legal only inside EV_DRAW_CANVAS (asserted) -- */

    /**
     * Claim a rectangle of the canvas, so that what the plugin drew there can
     * be clicked.
     *
     * Declared with the drawing rather than once at start, and that is not
     * laziness: an orb's box is derived from where the gameframe put the
     * minimap THIS frame, and a region registered at start would be a
     * rectangle over whatever used to be there after the first resize. The
     * list is rebuilt every frame from nothing, exactly like the drawing it
     * describes, so the two cannot disagree.
     *
     * `ops` are the verbs the player reads: the first one in the mouseover line
     * and as the LEFT click, all of them as rows in the right-click menu. A
     * region may offer several, because the things it stands for do -- the
     * reference's own orbs carry an op each and its xp orb carries two -- and
     * a plugin that could offer only one would have to redraw the same button
     * as several regions to say so.
     *
     * A left click runs op 0, because a region is the plugin's own real estate
     * with nothing of the game's underneath. The region owns the complete
     * physical press through release, so native hold/repeat/release/drag paths
     * underneath it cannot arm before the plugin receives the click. This is
     * the one place a plugin row may be the default, unlike api->menu_add,
     * which appends to a menu the game owns and where a plugin taking the
     * default click would be taking it from something else.
     *
     * NULL or an `op_count` of 0 claims the pointer without offering anything
     * -- a region that only wants to stop a click falling through to whatever
     * is behind it. Empty strings inside the array are skipped, so a caller
     * with a fixed-size table need not compact it.
     *
     * Rows are drawn in the reference's own order: the LAST op sinks to the
     * bottom of the menu and the first ends up on top, beside Cancel.
     *
     * `tag` comes back in EV_CANVAS_CLICK, and is the plugin's own; the host
     * does not read it.
     *
     * Overlap follows actual paint order. Global Canvas is above role-local
     * Canvas, which is above Frame; role-local regions compare the semantic
     * tree boundaries where their art was inserted. Declaration order breaks
     * ties only when two regions occupy the same paint boundary.
     *
     * @return 1 when the region was recorded, 0 when the frame's region table
     * is full.
     */
    int (*hit_region)(
        struct ToriRS_PluginCtx* ctx,
        void* surface,
        int x,
        int y,
        int w,
        int h,
        char const* const* ops,
        int op_count,
        uint32_t tag);

    /**
     * Press an interface button, exactly as a click on it would.
     *
     * The one verb in this contract that makes the GAME do something, and it
     * is deliberately shaped as "click that": it runs the same dispatch a real
     * click runs -- the local button behaviour, the varp the button owns, the
     * IF_BUTTON or IF_BUTTON<op> the server is waiting for -- rather than
     * reaching past any of it. A plugin that could write a varp directly would
     * be telling the client something the server never said (which is why
     * api->varp is read-only); a plugin that presses the button the player
     * would have pressed is not.
     *
     * `component_id` is `(interface << 16) | component`, the id the wire and
     * the interface tree both use. `op` is the numbered operation, 1..10, or 0
     * for the classic unnumbered button.
     *
     * Which id that is on a given cache is the CALLER's problem, and the
     * config key is where the answer belongs: the run toggle is a different
     * component in a 2004 gameframe than in a modern one, and there is no name
     * in any profile for it. A plugin that has not been told one offers no
     * verb rather than pressing something at random.
     *
     * @return 1 when a component with that id was found and dispatched.
     */
    /**
     * Show or hide the on-screen keyboard.
     *
     * The one verb a touch frame needs that a desktop one never did: a phone
     * has no keys until something asks for them, and this client's chat input
     * has always assumed a keyboard was simply there.
     *
     * It reaches SDL's text-input mode, which is the same switch on every
     * backend that HAS a soft keyboard -- Android, iOS and emscripten alike --
     * so a plugin asking for a keyboard gets one on a device and in a browser
     * without knowing which it is running in. On a desktop backend it governs
     * only whether SDL_TEXTINPUT events arrive, and the shell leaves that on,
     * so asking there is harmless and shows nothing.
     *
     * Asking does NOT hand the chat the keyboard. What the typing reaches is
     * whatever the client has focused, exactly as it is for a physical key.
     */
    void (*text_input)(struct ToriRS_PluginCtx* ctx, int on);

    /**
     * Give the client's chat input line the keyboard focus (or drop it) --
     * the other half of text_input, and the same thing pressing Enter on the
     * unfocused line does.
     *
     * The verb a touch frame's "Tap here to chat..." needs: a tap on the chat
     * sheet is swallowed by the frame's own hit region (that is what keeps it
     * off the world behind it), so the client's click-to-focus never sees it,
     * and raising the keyboard alone points the typing at the HOTKEYS -- the
     * doc above says exactly that. Focusing the line is also what makes the
     * keyboard follow by itself: the client raises and lowers the soft
     * keyboard off its own focus state, so a frame that calls this does not
     * need text_input for the chat at all.
     *
     * A no-op on a lane with no client-drawn chat line (a cache-era chatbox
     * routes its own keys), exactly as the focus flag itself is.
     */
    void (*chat_focus)(struct ToriRS_PluginCtx* ctx, int on);

    int (*if_click)(struct ToriRS_PluginCtx* ctx, int component_id, int op);

    /**
     * Where a component IS, in canvas coordinates. Any out may be NULL.
     *
     * The read half of if_click, and it exists for the same reason slot_rect
     * does beside layout_slot: a plugin that can press a button by id but
     * cannot ask where that button is can only act on things it is unable to
     * draw over. The two take the SAME id -- `(interface << 16) | component`
     * -- so a config key naming a button answers both verbs.
     *
     * Which id that is on a given cache is the CALLER's problem, exactly as it
     * is for if_click. Regions are the way to address the frame by ROLE and
     * survive a change of revision; this is for the buttons that have no role
     * because they are the cache's own widgets rather than the frame's.
     *
     * @return 1 when a component with that id is in the tree and has a
     * resolved, non-empty box. 0 otherwise -- an interface that is not open,
     * or a component this cache does not have, is an ANSWER and not a fault.
     */
    int (*component_rect)(
        struct ToriRS_PluginCtx* ctx,
        int component_id,
        int* out_x,
        int* out_y,
        int* out_w,
        int* out_h);

    /* -- semantic roles --
     *
     * The same four verbs as above, addressed by what an element IS.
     *
     * if_click and component_rect take a number, and their docs say the same
     * thing twice: which id it is on a given cache is the caller's problem,
     * and there is no name in any profile for it. These are that missing name.
     * A revision profile states `[role:report_button]` and what it is bound to
     * on that lane, and a plugin asks for "report_button" -- so the plugin that
     * could only work where its config key had been filled in by hand now
     * works on every lane whose profile has been told, which is the same trade
     * `[iface:…]` already made for the ids C needs.
     *
     * Deliberately a separate family and not an overload of the id verbs. A
     * role is resolved LIVE against the tree on every call, because the thing
     * it names may be a component a CS2 script built -- and a script-built
     * component's id is a rotating handle that a rebuild hands straight back
     * out to something else. A verb that took either a name or a number would
     * be one whose answer is stable for half its callers.
     *
     * `role` is the profile's own spelling, and the well-known ones are the
     * regions (`viewport`, `minimap`, `compass`, `chat`, `sidebar`,
     * `main_modal`, `chat_buttons`, `canvas`, `safe_gamechrome`) plus whatever elements
     * a profile has named. The vocabulary is OPEN: a role nobody declared is
     * not an error, it is a role this revision does not have.
     *
     * Every one of these answers 0 (or -1) for a role that does not resolve,
     * and that is an ANSWER and not a fault -- the same contract slot_rect
     * has. A plugin that gets it offers no verb rather than acting on a guess.
     */

    /**
     * Where the element named by `role` is, in canvas coordinates. Any out may
     * be NULL.
     *
     * A role naming a REGION answers exactly what slot_rect answers for it,
     * through the same lookup -- there is no second table, so a role and a
     * layout cannot come to disagree about where the minimap is.
     *
     * @return 1 when the role resolves to a node with a laid-out, non-empty
     * box.
     */
    int (*role_rect)(
        struct ToriRS_PluginCtx* ctx,
        char const* role,
        int* out_x,
        int* out_y,
        int* out_w,
        int* out_h);

    /**
     * Whether the element named by `role` is on screen right now.
     *
     * "Is the logout screen up", which no other verb in this contract can
     * answer: role_rect says nothing about a node that is laid out and hidden,
     * and a plugin cannot walk the tree to find out for itself.
     *
     * Counts the ancestors as well as the node: a visible child of a hidden
     * parent is not on screen, and a surface a gameframe layout has suppressed
     * is not either.
     *
     * @return 1 when it resolves and is visible, 0 when it is hidden or the
     * role does not resolve at all. The two are deliberately one answer --
     * "the player cannot see it" is what a caller is asking.
     */
    int (*role_visible)(struct ToriRS_PluginCtx* ctx, char const* role);

    /**
     * Press the element named by `role`, exactly as if_click presses one by id.
     *
     * The verb the report button needed. Its component is a chat-button member
     * on a 2004 frame and an interface component on a modern one, neither of
     * which a plugin can name by number and work on both.
     *
     * `op` is the numbered operation, 1..10, or 0 for the classic unnumbered
     * button, the same as if_click. Unlike if_click this can press a node that
     * carries NO component id -- a control the profile authored itself -- so a
     * role is the only way to reach some of them.
     *
     * @return 1 when the role resolved and the press was dispatched.
     */
    int (*role_click)(struct ToriRS_PluginCtx* ctx, char const* role, int op);

    /**
     * The component id the role currently resolves to, or -1.
     *
     * For handing to the id verbs and to nothing else. The answer is only good
     * for as long as the tree does not change underneath it: if the role names
     * a script-built component, the id is recycled on the next rebuild of that
     * subtree and will by then belong to a different node. Do not store it,
     * and do not compare two of them taken at different times -- ask again.
     *
     * -1 for a role that does not resolve, and ALSO for one that resolves to a
     * node with no id of its own, which is an ordinary state for a control a
     * profile authored. Use role_click and role_rect for those.
     */
    int (*role_id)(struct ToriRS_PluginCtx* ctx, char const* role);

    /* -- images --
     *
     * A plugin's OWN art, from its own asset file, drawn at its own pixels.
     *
     * The other half of the answer the model source enum gives for geometry
     * (see ToriRS_PluginModelSource): a MODEL is named by cache id because it
     * cannot mean anything outside the revision that decodes it, and a flat
     * image is the opposite -- it is a picture, it has no palette to be wrong
     * about, and a plugin that ships one works the same on every cache this
     * client boots. That is the whole reason this exists: the client's own
     * orbs, bars and icons are interface art, so a plugin reaching for them by
     * cache id would work on the one revision that has them and silently draw
     * something else on the rest.
     *
     * PNG, 8-bit, colour type 2 or 6, no interlace -- what any paint program
     * writes and what the cache's own world-map images already are. The alpha
     * channel is kept, because a cut-out is what interface art is.
     */

    /**
     * Begin loading `name` as an image, through the ordinary asset sandbox --
     * so a bare filename, resolved saved-copy-first, exactly as asset_load
     * resolves one.
     *
     * @return an image handle, or -1 when the plugin is at its image budget.
     *
     * The handle is live at once and the PIXELS are not: the read crosses the
     * IO queue like every other asset, so image_size answers 0x0 and
     * draw_image draws nothing until they land. A plugin lays out against
     * image_size and skips a frame or two on first run rather than blocking,
     * the same way the client's own graphics do.
     */
    int (*image_load)(struct ToriRS_PluginCtx* ctx, char const* name);
    /**
     * Publish `w`x`h` pixels the plugin RASTERISED ITSELF, under `name`, and
     * get back a handle draw_image blits like any other.
     *
     * `argb` is `w * h` non-premultiplied 0xAARRGGBB in row order, borrowed
     * for the call. Composing again under the same name replaces the pixels
     * in place, so a picture that changes -- a meter, an arc, a caption -- is
     * one handle for the life of the plugin rather than a handle per frame.
     *
     * This is the escape hatch for a shape the draw verbs above have no
     * primitive for. They are the ones the CLIENT needs: a rect, a line, a
     * hull, a blit. A circle, an annulus sector, a gradient or a soft edge is
     * none of those, and the honest ways to get one are to teach every
     * rasteriser in the tree a new primitive, or to approximate it out of
     * hundreds of rects and lines and spend the frame's whole draw budget on
     * one orb. Handing over pixels is the third way, and it is the one that
     * scales: whatever the plugin can compute, it can draw, at one blit.
     *
     * It pairs with image_pixels below -- read the art the plugin ships, put
     * it in the picture it is composing -- which is what makes a composite of
     * cache art and computed shapes possible without a decoder in the plugin.
     *
     * `name` is in the same per-plugin namespace as a loaded file's, so a
     * composed image and a file must not share one. It is a name and not just
     * a handle because a plugin reloaded mid-session re-runs its start
     * handler, and a name is what lets that find the image it already has.
     *
     * @return a handle, or -1 for a name that is not a legal asset name, a
     * non-positive or absurd size, or a plugin at its image budget.
     */
    int (*image_compose)(
        struct ToriRS_PluginCtx* ctx,
        char const* name,
        int w,
        int h,
        uint32_t const* argb);
    /**
     * Copy a resident image's pixels back out, into `out`, which must hold at
     * least `max` of them. Same 0xAARRGGBB layout image_compose takes.
     *
     * A plugin ships art as PNG because that is what a paint program writes
     * and what the client already decodes; it composes in ARGB because that is
     * what pixels are. Without this the two never meet, and a plugin wanting
     * its own icon inside a picture it computed would need a PNG decoder of
     * its own -- a second decoder in the tree, reached only from plugins.
     *
     * @return how many pixels were copied, or 0 for a handle this plugin does
     * not own, one still pending, or a buffer too small for the whole image.
     * Partial copies are not offered: half an image is not a smaller image,
     * and a caller that got one would draw a torn picture rather than none.
     */
    int (*image_pixels)(
        struct ToriRS_PluginCtx* ctx,
        int image,
        uint32_t* out,
        int max);
    /** The image's pixels. Both outs may be NULL. @return 1 once it is
     *  resident, 0 while it is pending or if the file would not decode. */
    int (*image_size)(struct ToriRS_PluginCtx* ctx, int image, int* out_w, int* out_h);
    /** Drop the image and its scene entry. The file is untouched. */
    void (*image_release)(struct ToriRS_PluginCtx* ctx, int image);
    /**
     * Blit an image with its top-left at `x`, `y`, at its own size.
     *
     * `clip_*` is an extra rectangle to cut it to, in the surface's
     * coordinates, and a zero `clip_w` or `clip_h` means no extra cut. It is
     * an argument rather than a second entry point because a partly-drawn
     * image is what a METER is: the client's own orbs are a full-size dark
     * disc drawn over a full-size coloured one, clipped to the unfilled part,
     * and a caller that has to fake that by scaling gets a squashed disc
     * instead of a covered one.
     *
     * `trans` is the reference's sense, not the fill_alpha above it: 0 is
     * opaque and 255 is invisible, matching every other sprite blit in the
     * client.
     */
    void (*draw_image)(
        struct ToriRS_PluginCtx* ctx,
        void* surface,
        int image,
        int x,
        int y,
        int clip_x,
        int clip_y,
        int clip_w,
        int clip_h,
        int trans);

    /* -- assets --
     *
     * A plugin's own files, in a namespace of its own: `name` is a bare
     * filename and never a path, so one plugin can neither read nor overwrite
     * another's, nor anything outside them. A name carrying a separator or a
     * `..` is refused and logged rather than asserted, because it arrives from
     * a script and a client that aborts on a typo in someone's Lua is worse
     * than one that says no.
     *
     * Reads resolve the SAVED copy first and the SHIPPED copy second, so a
     * plugin can ship a default table and later write over it without the two
     * being different names. Writes only ever land on the saved copy: the
     * shipped one sits under the script directory, which the browser lane
     * cannot write to at all.
     *
     * Everything crosses the IO queue, never fopen -- same reason the scripts
     * themselves do.
     */

    /** Begin loading `name`. Returns 1 when it is already resident (asset_data
     *  answers immediately and no event follows), 0 when a read was queued --
     *  EV_ASSET fires when it lands, whether it succeeded or not. */
    int (*asset_load)(struct ToriRS_PluginCtx* ctx, char const* name);
    /** Resident bytes, or NULL while pending / after a failure. `out_size` may
     *  be NULL. The buffer belongs to the host and lives until asset_release,
     *  another asset_save of the same name, or the plugin stopping. */
    void const* (*asset_data)(struct ToriRS_PluginCtx* ctx, char const* name, int* out_size);
    /** Replace `name` with `size` bytes and queue the write. The resident copy
     *  is updated before returning, so asset_data answers with the new bytes at
     *  once. Returns 1 when accepted. */
    int (*asset_save)(
        struct ToriRS_PluginCtx* ctx,
        char const* name,
        void const* data,
        int size);
    /** Drop the resident copy. The file is untouched. */
    void (*asset_release)(struct ToriRS_PluginCtx* ctx, char const* name);

    /* -- screenshots --
     *
     * Capture the client's own frame and write it out as a PNG.
     *
     * DEFERRED, not immediate: the request is recorded and taken at the end of
     * the frame that asked for it, once the interface has been laid out again.
     * Every caller is inside a handler that runs BEFORE that -- a game event
     * is recognised while the packet is being executed -- so capturing on the
     * spot would photograph the frame before the level-up box, the quest
     * scroll or the drop existed. A plugin that wants the moment to settle
     * further counts ticks of its own and calls this later.
     *
     * `dir` is the one place the asset sandbox does not apply, and the reason
     * is that a screenshot is the user's, not the plugin's: it is write-only,
     * of pixels they are already looking at, and the destination comes from a
     * config key THEY typed. `..` is still refused, so a plugin cannot climb
     * out of a directory the user named.
     *
     * It is read two ways, and the split is what lets one plugin work on both
     * lanes without asking which it is on:
     *
     *   ABSOLUTE (leading '/') -- used as given. A desktop user naming a
     *      folder they can find in a file manager.
     *   RELATIVE, or absent -- under the plugin's own saved-asset directory.
     *      Subdirectories still work, so "Bob/Levels" sorts a browser run's
     *      captures exactly like a desktop one; the browser lane has no path
     *      to name and this is the only place it can write.
     *
     * `name` is a bare filename; the host appends ".png" when it has no
     * extension. Returns 1 when the capture was queued.
     *
     * `out_path` is where the file will be, resolved -- the two readings above
     * collapsed into one string, ".png" completion included. A plugin cannot
     * work this out for itself: the saved-asset folder is the engine's, and on
     * the browser lane there is no path to guess. It is filled BEFORE the
     * picture is taken, which is the only time a caller can be told anything
     * at all about a deferred write, and it is what lets a plugin say where a
     * screenshot went instead of only that one happened. Empty and 0 returned
     * when the destination could not be resolved.
     */
    int (*screenshot)(
        struct ToriRS_PluginCtx* ctx,
        char const* dir,
        char const* name,
        char* out_path,
        int out_path_size);

    /**
     * Local wall-clock time as "YYYY-MM-DD_HH-MM-SS", into `out`.
     *
     * Not frame_ms, which is a monotonic client clock with no relation to a
     * calendar, and not something a script can work out for itself: the
     * sandbox does not link `os`, so a plugin has no date at all without this.
     * The format is the one RuneLite names screenshots with (ImageCapture's
     * TIME_FORMAT), which is also, not by accident, filename-safe under the
     * character set `name` above is held to.
     *
     * Returns 1 on success; `out` is always NUL terminated.
     */
    int (*datestamp)(struct ToriRS_PluginCtx* ctx, char* out, int out_size);

    /* -- shipped models --
     *
     * A model FILE out of the plugin's own asset folder, decoded by the host
     * and stood in the world through an object whose source is
     * TORIRS_PLUGIN_MODEL_ASSET.
     *
     * This and mesh_* answer the same need from opposite ends. A mesh is
     * geometry a plugin COMPUTES, which is right when the shape depends on
     * something only known at runtime and wrong as a way to carry art. A
     * shipped model is a file, authored as art is authored, and is what a
     * plugin should reach for when the shape is fixed -- it can be edited,
     * replaced or re-extracted without touching a line of the plugin.
     *
     * What it is NOT is a cache id. The bytes travel with the plugin, and the
     * decoder sniffs the model format off the file's own trailer rather than
     * off the booted revision's profile, so one file draws identically under
     * every cache this client boots. A model carrying TEXTURED faces is the
     * one exception and is not portable at all -- a texture id is a revision's
     * own numbering -- which is why the tool that produces these files refuses
     * a textured model unless told otherwise.
     */

    /**
     * Begin loading `name` as a model, through the ordinary asset sandbox --
     * so a bare filename, resolved saved-copy-first, exactly as asset_load
     * resolves one.
     *
     * @return a model handle, or -1 when the plugin's name is refused or the
     * resident model table is full.
     *
     * The handle is live at once and the GEOMETRY is not: the read crosses the
     * IO queue like every other asset. An object pointed at a model that has
     * not landed simply is not in the scene yet -- object_ready is how a
     * plugin tells that from an object that will never draw.
     *
     * One handle per (plugin, file): asking twice gets the same one back
     * rather than a second decoded copy.
     */
    int (*model_load)(struct ToriRS_PluginCtx* ctx, char const* name);

    /* -- authored meshes --
     *
     * Geometry a plugin builds itself and stands in the world through an
     * object whose source is TORIRS_PLUGIN_MODEL_MESH.
     *
     * A mesh is triangles: vertices in MODEL space -- x right, z forward, y
     * NEGATIVE-UP, the same axes and the same 128-units-to-a-tile scale every
     * cache model is authored in -- and faces naming three of them plus a
     * packed HSL colour and a transparency. Lighting, sorting, clipping and
     * the near plane are the host's, exactly as they are for a cache model:
     * an authored mesh is a model, not an overlay drawn in perspective.
     *
     * Colours are packed HSL because face colours are (see hsl_from_rgb).
     * Vertex ORDER decides which way a face points, and a face is only lit
     * from the side its winding faces; the beam that reads as a hollow tube
     * from outside is the same triangles wound the other way.
     *
     * Editing a mesh an object is already built from rebuilds that object's
     * model, so an animation driven by re-authoring geometry costs a rebuild
     * per change. Spinning, raising or moving an object costs nothing --
     * object_set_position is applied to the live element -- which is why a
     * plugin that wants motion should look for it there first.
     */

    /** An empty mesh owned by this plugin. Returns a handle, or -1 at the
     *  plugin's mesh budget. */
    int (*mesh_create)(struct ToriRS_PluginCtx* ctx);
    void (*mesh_destroy)(struct ToriRS_PluginCtx* ctx, int mesh);
    /** Drop every vertex and face, keeping the handle. Objects built from it
     *  rebuild, so clear-then-rebuild is how a mesh is re-authored. */
    void (*mesh_clear)(struct ToriRS_PluginCtx* ctx, int mesh);
    /** Append a vertex; returns its index, or -1 at
     *  TORIRS_PLUGIN_MESH_VERTICES_MAX. */
    int (*mesh_vertex)(struct ToriRS_PluginCtx* ctx, int mesh, int x, int y, int z);
    /** Append a triangle over three vertex indices, in packed HSL, with
     *  `alpha` 0 (opaque) .. TORIRS_PLUGIN_MESH_ALPHA_MAX. Returns its face
     *  index, or -1 at TORIRS_PLUGIN_MESH_FACES_MAX. */
    int (*mesh_face)(
        struct ToriRS_PluginCtx* ctx,
        int mesh,
        int a,
        int b,
        int c,
        int hsl,
        int alpha);

    /* -- world objects --
     *
     * A model the plugin owns, drawn in the scene among the locs and the
     * entities rather than painted over them the way the draw api is. That
     * difference is the whole point: an overlay is always in front, so it
     * cannot stand behind a wall, sink into a slope or sort against an npc,
     * and a beam of light that ignores the building in front of it does not
     * read as being in the world.
     *
     * Handles are per-plugin and are destroyed for it when it stops. Position
     * is an ABSOLUTE tile, so an object survives a scene rebuild: the host
     * re-places it on the new origin rather than the plugin having to notice.
     *
     * Model and sequence loads are asynchronous, exactly as they are for the
     * client's own graphics. An object is simply not drawn until its assets
     * land; object_ready is how a plugin can tell the difference.
     */

    /** A new object, owned by this plugin, inactive and with no model yet.
     *  Returns a handle, or -1 when the plugin is at its object budget. */
    int (*object_create)(struct ToriRS_PluginCtx* ctx);
    void (*object_destroy)(struct ToriRS_PluginCtx* ctx, int object);
    /** `id` is read by `source`: a cache model id, a spotanimtype id, a mesh
     *  handle from mesh_create, or a model handle from model_load. Setting a
     *  model the object does not already have re-queues its load. */
    void (*object_set_model)(
        struct ToriRS_PluginCtx* ctx,
        int object,
        enum ToriRS_PluginModelSource source,
        int id);
    /** Append one recolour pair, in packed HSL -- the unit the model's face
     *  colours are actually in (see hsl_from_rgb). Applied when the model is
     *  built, so pairs added after it is already drawn take effect on the next
     *  rebuild; clear-then-set is what forces one. */
    void (*object_recolor)(struct ToriRS_PluginCtx* ctx, int object, int hsl_from, int hsl_to);
    /** Drop every recolour pair and rebuild the model from the cache copy. */
    void (*object_clear_recolors)(struct ToriRS_PluginCtx* ctx, int object);
    /** Bind a sequence. -1 leaves the model in its bind pose; a SPOTANIM-source
     *  object that is never given one plays the spotanimtype's own seq. */
    void (*object_set_anim)(struct ToriRS_PluginCtx* ctx, int object, int seq_id, int loop);
    /** Lighting OFFSETS against the actor profile, exactly as a spotanimtype's
     *  own ambient/contrast are applied: 0, 0 is the client's default and a
     *  positive ambient brightens. Not absolute values -- a light source that
     *  ignored the profile would not match the models beside it. */
    void (*object_set_light)(struct ToriRS_PluginCtx* ctx, int object, int ambient, int contrast);
    /** ABSOLUTE tile and level, `height` in the projector's 1/128-of-a-tile
     *  units above the ground (positive raises), `yaw` 0..2047. */
    void (*object_set_position)(
        struct ToriRS_PluginCtx* ctx,
        int object,
        int tile_x,
        int tile_z,
        int level,
        int height,
        int yaw);
    void (*object_set_active)(struct ToriRS_PluginCtx* ctx, int object, int active);
    /** 1 once the model is built and the object is in the scene. */
    int (*object_ready)(struct ToriRS_PluginCtx* ctx, int object);

    /* -- colour --
     *
     * Model faces are not RGB. They are 6-bit hue / 3-bit saturation / 7-bit
     * luminance indices into the revision's palette, which is why a recolour
     * takes packed HSL and why "make this beam #FF0000" needs a conversion
     * rather than a cast.
     */

    /** 0xRRGGBB -> packed HSL. */
    int (*hsl_from_rgb)(struct ToriRS_PluginCtx* ctx, uint32_t rgb);
    /** Packed HSL -> 0xRRGGBB, through the same palette the rasteriser uses. */
    uint32_t (*hsl_to_rgb)(struct ToriRS_PluginCtx* ctx, int hsl);

    /* -- the plugin window -------------------------------------------------
     *
     * ONE window, shared: win_request claims a TAB in it, not a window of its
     * own. That is the sandbox rule, not a simplification -- sixteen plugins
     * that could each open a window could bury the game under sixteen of them,
     * and a plugin cannot be trusted to be reasonable about a resource it does
     * not have to share.
     *
     * Where that tab actually appears is the host's business and the user's:
     * in the game canvas, in an OS window, in a browser tab, or as a panel
     * behind a sidebar button. A plugin declares controls and hears about
     * clicks; it never learns which of those it got.
     *
     * Controls are named by plugin-scoped string ids rather than handles,
     * because a reload rebuilds the tab from nothing: an id survives that and
     * a handle does not, so a saved setting can be routed back to the control
     * it came from on the other side of a reload.
     */

    /**
     * Claim this plugin's tab, titled `tab_title`. Idempotent -- calling it
     * again only renames the tab.
     *
     * @return false when the window is full or the host has none.
     */
    bool (*win_request)(struct ToriRS_PluginCtx* ctx, char const* tab_title);

    /**
     * Append a control. `id` is this plugin's own name for it; `label` is what
     * the user reads, and may be NULL for a control that needs none.
     *
     * @param kind enum ToriRS_PluginWidgetKind.
     * @return false when the per-plugin control budget is spent.
     */
    bool (*win_widget)(
        struct ToriRS_PluginCtx* ctx, int kind, char const* id, char const* label);

    bool (*win_set_text)(struct ToriRS_PluginCtx* ctx, char const* id, char const* text);
    bool (*win_set_checked)(struct ToriRS_PluginCtx* ctx, char const* id, bool on);
    /** `choices` is "a|b|c", as a config schema's enum is. */
    bool (*win_set_options)(
        struct ToriRS_PluginCtx* ctx, char const* id, char const* choices, int selected);

    /** Drop every control on this plugin's tab, to rebuild it from the top. */
    void (*win_clear)(struct ToriRS_PluginCtx* ctx);

    /* -- ABI 17 append --------------------------------------------------- */

    /**
     * Attach one image to a placed layout slot, immediately above that live
     * surface's whole UI subtree.
     *
     * This is the local-z-order counterpart to draw_image. A draw made in
     * EV_DRAW_FRAME or EV_DRAW_CANVAS remains in that global surface's paint
     * order; the host never guesses that an ordinary draw intended to replace
     * or decorate some component. This declaration is explicit, and legal
     * only inside EV_LAYOUT (asserted).
     *
     * The anchor is the role's primary semantic node -- the deterministic node
     * slot_rect addresses for a whole role. The image is emitted after that
     * node and every descendant, before its next sibling, using the node's
     * PARENT clip. That permits a housing to overlap the live surface while
     * preventing it from painting over unrelated chrome outside the containing
     * panel. A role with several independently placed members needs a future
     * member-specific declaration rather than duplicating one image over all
     * of them.
     *
     * `x`/`y` are canvas coordinates and `trans` is 0 opaque, 255 invisible.
     * The same slot must be placed in this declaration. If the target is
     * absent, hidden, collapsed, or emits no visible subtree this image is
     * omitted too. The image handle may still be loading: its stable scene
     * identity is retained and it begins drawing as soon as its pixels land,
     * without waiting for another EV_LAYOUT.
     *
     * @return 1 when this gameframe currently has the primary surface for the
     * role, 0 otherwise. Recording and the answer are separate, exactly as for
     * layout_slot: a declaration remains whole across a later tree rebuild.
     */
    int (*layout_slot_overlay)(
        struct ToriRS_PluginCtx* ctx,
        int slot,
        int image,
        int x,
        int y,
        int trans);

    /**
     * Claim or release replacement of the component named by `role`.
     *
     * A replacement is CSS `display:none` semantics for the native subtree:
     * it is omitted from paint, hit testing, menus, hover and focus while its
     * cache/client scripts remain alive in the background. Claims are owned by
     * the calling plugin, exclusive per role, persistent across tree rebuilds,
     * and released automatically when that plugin stops.
     *
     * `enabled` nonzero claims (or idempotently restates) the role; zero
     * releases this plugin's claim. The claim remains valid while its target is
     * temporarily absent and rebinds to the next incarnation that resolves.
     *
     * @return 1 when the claim/release was accepted, 0 for an invalid role or
     * a role currently claimed by another plugin.
     */
    int (*role_replace)(
        struct ToriRS_PluginCtx* ctx,
        char const* role,
        int enabled);

    /**
     * Anchor subsequent EV_DRAW_CANVAS primitives and hit regions to `role`.
     *
     * The anchor lasts only until the current canvas subscriber returns. Its
     * primitives paint immediately after the target's complete subtree with
     * the target's parent clip. If the role is REPLACED -- by this plugin or
     * by any other -- they paint at the pruned subtree's tombstone instead,
     * over the replacement's own declaration: the name is the object, and
     * the object is wherever its provider paints it. A missing, hidden or
     * rebuilt target drops the declarations; they never fall back to the
     * global canvas overlay.
     *
     * `place` says which side of the object the primitives land on; see
     * enum ToriRS_PluginAnchorPlace.
     *
     * @return 1 when the role resolves for this draw pass, 0 when it does not.
     */
    int (*role_anchor)(struct ToriRS_PluginCtx* ctx, char const* role, int place);

    /* -- ABI 18 append: chrome ------------------------------------------- */

    /**
     * Declare the ART of one member of a role this layout just placed. Legal
     * only inside EV_LAYOUT (asserted), and only for the frame's owner.
     *
     * The arranger's half of the chrome tier, and the reason the tier can
     * exist at all: before this, a gameframe's chat button was four image
     * handles in a plugin-local array and a blit in EV_DRAW_FRAME, which no
     * other plugin could see, measure or take over. Declared, it is a part the
     * HOST paints -- so a dresser claiming it means the host stops painting
     * the arranger's version, with no cooperation between the two and no
     * "did somebody replace me" check in the arranger's draw path.
     *
     * `member` is the role's own numbering, exactly as layout_slot_at takes
     * it. The declaration reaches dressers under whatever role name this
     * revision binds to that member -- `report_button` for
     * `slot(chat_buttons, report)` -- which is the profile's business and not
     * the arranger's.
     *
     * `part` is borrowed for the call. Its box is canvas coordinates, like
     * every other layout call. Passing NULL drops a previously declared part.
     *
     * A part is rebuilt from nothing each EV_LAYOUT, like every other
     * declaration here, so an arranger that stops declaring one stops painting
     * it.
     *
     * @return 1 when this gameframe has that member, 0 otherwise -- the same
     * "does this frame have one" answer layout_slot_at gives.
     */
    int (*layout_slot_art)(
        struct ToriRS_PluginCtx* ctx,
        int slot,
        int member,
        struct ToriRS_PluginChromePart const* part);

    /**
     * Claim or release `scopes` of the part named `part`. `scopes` is a mask
     * of enum ToriRS_PluginChromeScope; `enabled` zero releases those scopes
     * and leaves any others this plugin holds standing.
     *
     * @return -1 when this revision has no such part -- the caller may
     * chrome_add one, or do without; nothing is drawing it either way.
     * Otherwise the mask of the requested scopes this plugin now HOLDS.
     *
     * A mask and not a yes, because the answer is per scope: a plugin asking
     * for APPEARANCE|HITBOX may get APPEARANCE alone, when another plugin
     * already owns the click. Every bit that came back is this plugin's to
     * declare; every bit that did not is ANOTHER PLUGIN'S, and the caller
     * degrades for that scope -- it does not draw it, and it does not
     * complain, because the part is on the screen, just not by its hand. A 0
     * is "everything you asked for is somebody else's", and chrome_owner says
     * whose.
     *
     * The three answers are kept apart because they call for opposite
     * responses, and a plugin told only "no" would have to pick one and be
     * wrong half the time.
     *
     * Take every claim at EV_START, before drawing anything. A claim survives
     * its target being absent and rebinds to the next incarnation that
     * resolves, exactly as role_replace's does, so claiming early is correct
     * rather than premature -- and it puts the arbitration at the moment the
     * user flipped the switch, which is the moment they can act on what the
     * client says about it.
     *
     * POSITION is refused on a part the LANE provides -- a cache component or
     * a revconfig builtin -- and said so in the log: moving a native node is
     * not something this tier can do, and a claim that reported success and
     * moved nothing would be the silent no-op this contract exists to
     * prevent. Every other scope on a lane part, and every scope on a frame
     * or added part, is claimable.
     *
     * Claims are persistent across gameframe rebuilds and released for you
     * when the plugin stops.
     */
    int (*chrome_claim)(
        struct ToriRS_PluginCtx* ctx, char const* part, int scopes, int enabled);

    /**
     * Introduce a part this revision has no node for, hung off `anchor`, and
     * claim it in the same call.
     *
     * The minimap orbs are the case. On an OldSchool cache they are interface
     * 160 and the client draws them; on a 2004 cache the numbers are on the
     * wire and the PICTURE does not exist -- there is no component, and so
     * nothing for a role to bind to and nothing to claim. The part has to be
     * introduced before it can be dressed.
     *
     * `anchor` is an existing role and the part's box is RELATIVE to it, which
     * is what makes an added part follow the frame: a gameframe that moves the
     * minimap moves the orbs with it and the plugin re-declares nothing. The
     * part is emitted after the anchor's own subtree under the anchor's PARENT
     * clip, so it may overlap the anchor -- an orb column hangs past the
     * bottom of the map -- while still being cut by the panel that houses it,
     * and it inherits the anchor's fate: hidden with it, rebuilt with it.
     *
     * An added part is a part like any other. Anyone can find it by name with
     * chrome_part, a second plugin can claim and restyle it, and a profile
     * that later binds the same name to a native node takes over silently --
     * chrome_claim starts answering 1 where it answered 0, and the plugin's
     * own code path does not change.
     *
     * So name it for what it IS -- `orb_hitpoints`, never
     * `minimap_orbs_orb_0`. A name carrying its plugin's identity can never be
     * adopted by a profile and can never be provided by anything else, which
     * defeats both.
     *
     * `place` is which side of the anchor the part paints on, for the life of
     * the part -- enum ToriRS_PluginAnchorPlace. An orb column on a housing
     * is AFTER it.
     *
     * An added part is introduced with EVERY scope held by its introducer,
     * which may then release the ones it does not want kept.
     *
     * @return exactly as chrome_claim(part, SCOPE_ALL, 1), and -1 also for an
     * anchor role this revision does not have -- which is the honest reply to
     * "put an orb column on a gameframe with no minimap".
     */
    int (*chrome_add)(
        struct ToriRS_PluginCtx* ctx,
        char const* part,
        char const* anchor,
        int place,
        struct ToriRS_PluginChromePart const* initial);

    /**
     * The human title of the plugin providing `part`, or NULL when nobody is.
     *
     * For the one line a degrading plugin writes to the LOG. Losing a claim is
     * not a fault to report to the player: their screen is correct, the orb is
     * there, another plugin drew it. What belongs in the chatbox is a part
     * that NOBODY ends up providing.
     */
    char const* (*chrome_owner)(struct ToriRS_PluginCtx* ctx, char const* part, int scope);

    /** Of `scopes`, the ones held by some plugin OTHER than the caller. The
     *  one test a plugin that still draws a part imperatively -- an animated
     *  stone, a live gradient -- has to make before drawing it. */
    int (*chrome_claimed)(struct ToriRS_PluginCtx* ctx, char const* part, int scopes);

    /**
     * Read a part as whatever authority currently owns it, in CANVAS
     * coordinates. `out` is untouched on 0.
     *
     * Art handles come back already borrowed into the caller's own namespace:
     * a second reference onto the same resident pixels, which draw_image and
     * image_size take like any other handle. A borrow is read-only -- it
     * cannot be composed into or released -- and it is idempotent, so asking
     * every EV_CHROME yields the same handle rather than leaking one a pass.
     *
     * A borrow whose lender dropped the image behaves exactly as a PENDING
     * image does: image_size answers 0 and draw_image draws nothing. That is
     * deliberate reuse rather than a third state -- every caller already has
     * correct code for "the pixels are not here yet", written for the IO
     * queue, and it is right for this too.
     *
     * Answers for a part somebody ELSE holds, which is what makes degradation
     * possible rather than merely polite: a plugin that lost two orbs reads
     * where they are and lays its remaining two out around them.
     *
     * @return 1 when this frame has the part, 0 when it does not.
     */
    int (*chrome_part)(
        struct ToriRS_PluginCtx* ctx,
        char const* part,
        struct ToriRS_PluginChromePart* out);

    /**
     * Declare this plugin's art for a part it holds. Legal only inside
     * EV_CHROME (asserted).
     *
     * "Keep the frame's plate and put my icon on it" is therefore: read the
     * part, compose the two into one handle, declare it back at the same box.
     *
     * A part claimed and never painted is a part HIDDEN -- declaration
     * semantics, and also a feature: a plugin whose whole purpose is to remove
     * the report button claims it and declares nothing. A plugin that wants
     * the arranger's part back RELEASES the claim instead. Those are two
     * different sentences and both are sayable.
     *
     * Only the fields of the scopes this plugin HOLDS are read: a POSITION
     * holder's art[] is ignored and an APPEARANCE holder's box is, so a
     * plugin fills in what it owns and leaves the rest zero. A holder of
     * APPEARANCE alone has no box to give and passes w/h of 0 without
     * complaint; the size check applies to a POSITION holder only.
     *
     * @return 1 recorded, 0 for a part this plugin holds no scope of, an
     * anchor that was not placed this pass, or a non-positive size from a
     * POSITION holder.
     */
    int (*chrome_paint)(
        struct ToriRS_PluginCtx* ctx,
        char const* part,
        struct ToriRS_PluginChromePart const* art);

    /**
     * The ops for a held part's hit region, installed by the host along with
     * the declaration. Legal only inside EV_CHROME (asserted).
     *
     * Same meaning as hit_region's: the first op is the mouseover line and the
     * left click, all of them are rows in the menu, and `tag` comes back in
     * EV_CANVAS_CLICK. An `op_count` of 0 claims the pointer and offers
     * nothing, for a part that must only stop a click falling through to what
     * is behind it.
     *
     * Declared here rather than at draw time because the region belongs to the
     * PART: whoever holds the part this frame owns the click, and a region
     * declared by a plugin that has since lost or released it is dropped
     * rather than dispatched.
     *
     * @return 1 when the ops were recorded, 0 for a part this plugin does not
     * hold the HITBOX of.
     */
    int (*chrome_ops)(
        struct ToriRS_PluginCtx* ctx,
        char const* part,
        char const* const* ops,
        int op_count,
        uint32_t tag);

    /**
     * Select which of a held part's pictures is showing. Legal at ANY time,
     * unlike the declaring calls, and cheap enough to call every tick.
     *
     * `state` is an enum ToriRS_PluginChromeState, and it is the half of the
     * choice the host cannot make for itself. Hover it knows -- it has the
     * pointer and the part's box -- so the host picks HOVER over IDLE and
     * ACTIVE_HOVER over ACTIVE without being told. Which chat filter the box
     * is SHOWING is a fact about the game, and only the plugin reading that
     * var knows it.
     *
     * So the split is: the plugin says selected-or-not, the host says
     * hovered-or-not, and the four-plate family the reference ships is
     * addressed without either of them re-deriving the other's half.
     *
     * A part is IDLE until told otherwise, and a state whose art is -1 falls
     * back to IDLE, so a part with one picture never needs this at all.
     *
     * @return 1 when the state was recorded, 0 for a part this plugin does not
     * hold the APPEARANCE of, or a state out of range.
     */
    int (*chrome_state)(struct ToriRS_PluginCtx* ctx, char const* part, int state);

    /**
     * The arranger's spellings of chrome_claimed and chrome_state, in the
     * arranger's own terms. Legal only for the frame's owner (asserted).
     *
     * An arranger places MEMBERS and does not know -- should not have to know
     * -- that this revision calls member 3 of the chat buttons
     * `report_button`. It still has two things to say about a part it
     * declared: whether somebody has taken the click, so it does not install
     * a second region under theirs; and which plate the host should be
     * painting, because "the chat is open on this filter" is the arranger's
     * fact and not the host's.
     *
     * layout_slot_claimed answers, of `scopes`, the ones some plugin holds.
     * layout_slot_state selects the declared part's picture, exactly as
     * chrome_state does for a claimant, and is legal at any time.
     */
    int (*layout_slot_claimed)(struct ToriRS_PluginCtx* ctx, int slot, int member, int scopes);
    int (*layout_slot_state)(struct ToriRS_PluginCtx* ctx, int slot, int member, int state);

    /* -- entities: the same tier, aimed at the world ---------------------- */

    /**
     * Spell the part name for one world entity into `buf`, for chrome_claim
     * and the two verbs below. `a`..`d` are the kind's own ids:
     *
     *   NPC     a = server slot
     *   PLAYER  a = server pid
     *   LOC     a = tile x, b = tile z, c = level, d = loc id   (ABSOLUTE tile)
     *   OBJ     a = tile x, b = tile z, c = level, d = obj id
     *
     * @return `buf`, or NULL for a kind this contract has not got or a buffer
     * too small for the name (TORIRS_PLUGIN_ROLE_NAME_MAX is always enough).
     */
    char const* (*entity_part)(
        struct ToriRS_PluginCtx* ctx, int kind, int a, int b, int c, int d, char* buf, int cap);

    /**
     * Declare how an entity whose APPEARANCE this plugin holds is drawn.
     * Legal at any time and cheap enough for every tick; the host paints it on
     * every world frame until it is restated or the claim goes.
     *
     * @return 1 recorded, 0 for a part this plugin does not hold the
     * APPEARANCE of.
     */
    int (*entity_look)(
        struct ToriRS_PluginCtx* ctx, char const* part, struct ToriRS_PluginEntityLook const* look);

    /**
     * Declare what a right-click on an entity whose HITBOX this plugin holds
     * offers. Legal at any time. `mode` is enum ToriRS_PluginEntityOpsMode,
     * `ops` and `op_count` as hit_region takes them, and `tag` comes back in
     * EV_MENU_SELECT as `plugin_tag` on the chosen row.
     *
     * The rows are added by the HOST on every menu build the entity is the
     * subject of -- the hover line, the right-click, the left-click default
     * are all that same build -- so a plugin declares once and is not asked
     * again.
     *
     * @return 1 recorded, 0 for a part this plugin does not hold the HITBOX
     * of.
     */
    int (*entity_ops)(
        struct ToriRS_PluginCtx* ctx,
        char const* part,
        int mode,
        char const* const* ops,
        int op_count,
        uint32_t tag);

    /* -- ABI 20 append: the tabs the server took away --------------------- */

    /**
     * Whether tab `tabno` has a panel behind it RIGHT NOW.
     *
     * The SERVER's answer, and the other half of the question layout_slot_at
     * answers. Those two are not the same question and a frame needs both:
     *
     *   layout_slot_at  does this cache have a Clan chat tab at all
     *   tab_enabled     has the server given this player that tab yet
     *
     * The first is a fact about the gameframe and cannot change while a world
     * is loaded; the second changes on a packet -- the tutorial hands the tabs
     * out one at a time and starts with none of them, so every tab but one is
     * "not yet" for the first minutes of a new character's life.
     *
     * ONE question, however the lane happens to spell it. A 2004 frame carries
     * its own sidebar mounts and the tab is IF_SETTAB's; a frame that is the
     * cache's own IF3 tree has neither, and there the tab is a node the
     * revision's own scripts hide (IF_SETHIDE) or a sub-interface the server
     * closed. Which of those is in play is the profile's business -- it names
     * the tab, `[role:sidetab_<n>]` -- and the engine's; a plugin asks the
     * question and is not told the mechanism. @see RS_UISlots_TabGiven.
     *
     * A lane whose profile says nothing about a tab answers 1: nothing there
     * claims it is hidden, and a frame that drew no icons at all would be a
     * worse wrong than one that drew every icon.
     *
     * A layout that draws an icon for a tab the server has taken away is
     * showing a panel that cannot open, which is the same wrong as a stone for
     * a tab this cache lacks and worse for being temporary: the player is
     * being invited to tap the very thing the tutorial has just hidden. The
     * client's own chrome gates its icon and its pressed highlight on this
     * (reference drawSidebarIcons, `sideOverlayId[n] !== -1`), and a plugin
     * frame that replaces that chrome takes on the same duty.
     *
     * Answered from live state on every call and never cached by the host, so
     * a frame asks it in its DRAW pass and not in its layout: a tab handed
     * over mid-tutorial is not a resize, a rebuild or a claim, and a layout
     * that recorded the answer once would keep drawing the tutorial's first
     * minute for the rest of the session.
     *
     * `tab_select` already refuses a tab this answers 0 for, so a frame that
     * only draws needs no second gate on the click -- but a frame whose stone
     * does something of its OWN (the mobile drawer opens on any tap) has to
     * ask before doing it.
     *
     * @return 1 when the tab has an interface mounted, 0 when it does not or
     * `tabno` is not a tab.
     */
    int (*tab_enabled)(struct ToriRS_PluginCtx* ctx, int tabno);

    /* -- ABI 21 append: the one shared application plugin panel ----------- */

    /**
     * Register or update this plugin's inert rail entry. Legal only from
     * EV_START; registration does not select, open, or build the page.
     */
    bool (*panel_request)(
        struct ToriRS_PluginCtx* ctx,
        struct ToriRS_PluginPanelDesc const* desc);

    /**
     * Append one semantic node while handling EV_PANEL_BUILD. IDs are stable
     * and plugin-scoped. Only the selected plugin may declare nodes.
     * `kind` is enum ToriRS_PluginWidgetKind.
     */
    bool (*panel_widget)(
        struct ToriRS_PluginCtx* ctx,
        int kind,
        char const* id,
        char const* label);
    /** Update retained state on the selected page. Hidden plugins return
     *  false and are rebuilt when next selected. */
    bool (*panel_set_text)(
        struct ToriRS_PluginCtx* ctx,
        char const* id,
        char const* text);
    bool (*panel_set_value)(struct ToriRS_PluginCtx* ctx, char const* id, int value);
    /** `choices` is the same `a|b|c` spelling win_set_options accepts. */
    bool (*panel_set_options)(
        struct ToriRS_PluginCtx* ctx,
        char const* id,
        char const* choices,
        int selected);

    /** Update rail state without selecting the page. */
    bool (*panel_set_badge)(struct ToriRS_PluginCtx* ctx, char const* text);
    bool (*panel_set_attention)(struct ToriRS_PluginCtx* ctx, bool attention);

    /** Clear the selected page's semantic nodes. Outside its build event this
     *  marks the page for one fresh EV_PANEL_BUILD. */
    void (*panel_clear)(struct ToriRS_PluginCtx* ctx);
    /** Mark a selected custom node dirty. Hidden pages never draw. */
    void (*panel_invalidate)(struct ToriRS_PluginCtx* ctx, char const* custom_view_id);
    /** Set a custom drawing well's preferred logical height. Zero asks for the
     *  default; other values are clamped to the portable bounds. */
    bool (*panel_set_height)(
        struct ToriRS_PluginCtx* ctx,
        char const* custom_view_id,
        int preferred_height);

    /* -- ABI 22 append: the client's own item art ------------------------- */

    /**
     * The inventory icon the client draws for `obj_id` at `count`, as an image
     * handle the ordinary draw_image / image_size / image_pixels verbs accept.
     *
     * @param style enum ToriRS_PluginObjIconStyle.
     * @return an image handle, or -1 when the objtype or its inventory model
     * is not resident yet -- ask again next frame.
     *
     * ## Why the client has to answer this
     *
     * An item's icon is not a file: it is a lit, recoloured, angled render of
     * the objtype's inventory MODEL with the stack number stamped on it, built
     * by the same rasteriser the inventory tab uses. A plugin cannot build one
     * -- it has no model, no lighting rig and no font -- and shipping a folder
     * of PNGs instead means a picture per item per revision that goes stale
     * the moment a cache is swapped. So the client hands over the one it
     * already made.
     *
     * `count` is part of the picture, not a decoration on it: the client bakes
     * the stack digits into the sprite exactly as the inventory shows them, so
     * a caller drawing "Bones x37" asks for the icon at 37 and draws one
     * thing. Pass 1 for the bare item.
     *
     * ## THE HANDLE IS THE CACHE'S, AND IT IS NOT YOURS TO KEEP
     *
     * These live in a bounded, host-owned, least-recently-used cache -- a
     * plugin never releases one, and image_release refuses a handle it did not
     * make. The cache exists because the alternatives are both bad: a plugin
     * holding an icon per item it has ever seen would exhaust the resident
     * image table in one boss trip, and one re-rasterising per frame would pay
     * for a model render sixty times a second to draw a picture that has not
     * changed.
     *
     * The rule that follows is the whole of the contract: ASK AGAIN EVERY TIME
     * YOU DRAW. A call is a hash of three ints and a hit, so the cost of doing
     * so is nothing; a handle REMEMBERED across frames may have been evicted
     * to make room for another icon, and an evicted handle measures 0x0 and
     * draws nothing -- silently, and only for the plugin unlucky enough to
     * have gone quiet for a while, which is the worst shape a bug can have.
     * Every call touches the entry, so an icon a plugin is actually drawing is
     * never the one evicted.
     */
    int (*obj_image)(struct ToriRS_PluginCtx* ctx, int obj_id, int count, int style);
};

/* ------------------------------------------------------------------------ */
/* Plugin definition                                                         */
/* ------------------------------------------------------------------------ */

struct ToriRS_PluginDef
{
    /** kebab-case id; also the ini section suffix. */
    char const* name;
    /**
     * What a PERSON is shown: "Entity Highlighter", not "entity-highlighter".
     *
     * Separate from `name` because the two answer to different readers. The
     * name is an identity -- it keys the ini section, PluginHost_IndexOf and
     * the manifest entry -- so it cannot be changed to read better without
     * orphaning everyone's saved settings. The title has no such duty and can
     * say whatever is clearest, including words the id has no room for.
     *
     * NULL is allowed and means "derive one from the name", which is what a
     * plugin that has not thought about it gets: separators become spaces and
     * words are capitalised. That fallback exists so no roster row can ever
     * show a raw slug -- not so titles can be skipped. Write one.
     */
    char const* title;
    char const* version;
    /** Higher runs earlier within an event. Ties break on registration order. */
    int priority;
    /**
     * Where this plugin's DRAWING sits in the stack, within one draw pass.
     * Higher is nearer the viewer; 0 is the default.
     *
     * Separate from `priority`, because on a draw event the two want opposite
     * things and one number cannot say both. Priority is about seeing an event
     * FIRST -- a feature flag has to be restored before anything reads one --
     * and on a draw pass "first" means UNDERNEATH. So a plugin that raised its
     * priority to be early would sink its own drawing, and a plugin that
     * lowered it to draw on top would stop being early. This orders the
     * pixels; priority goes on ordering the handlers.
     *
     * The case it exists for is a gameframe. Its art is a BACKDROP -- a map
     * housing, a sidebar panel, a chatbox -- and every readout another plugin
     * paints near one belongs over it, which is not something the readout
     * should have to know: the minimap orbs sat under the map surround's ring
     * because the frame happened to be registered after them, and nothing in
     * either plugin said so. A frame declares itself low and the question
     * stops being about registration order.
     *
     * Only the draw events read it -- EV_DRAW_WORLD, EV_DRAW_CANVAS,
     * EV_DRAW_FRAME. Ties break on `priority`, then on registration order.
     */
    int draw_order;
    /** NULL-key-terminated array, or NULL for no config. */
    struct ToriRS_PluginConfigItem const* config;
    /** Start switched off until the user asks for it, or until a saved
     *  `enabled=1` turns it on. For anything that draws on a fresh install: a
     *  client that marks up the screen on first launch without being asked
     *  reads as broken, not as featureful. */
    bool disabled_by_default;
    /**
     * This plugin exists to HOST other plugins; it is machinery, not a
     * feature.
     *
     * Only the settings roster reads it, and only to decide whether to list a
     * row. The Lua adapter registers one plugin per script and is itself
     * registered beside them, which is what makes "a script" and "a C plugin"
     * the same kind of thing everywhere else in the system -- and that
     * uniformity is worth keeping. But it also put a row called "lua" in the
     * roster, sitting among the scripts it runs and looking like a peer of
     * them, and there is nothing a user does with it.
     *
     * So the roster hides an adapter that is working: its scripts are the
     * rows, and they speak for it. It reappears the moment it has something to
     * say -- a load fault to report, or its own switch turned off -- because
     * both are states you have to be able to see to get out of. Hiding it
     * unconditionally would make a broken Lua layer a client with no plugins
     * and no explanation, and a disabled one impossible to switch back on.
     */
    bool adapter;
    /**
     * Not listed in the Plugin settings roster.
     *
     * For a BUILTIN: a feature of the client that happens to be written as a
     * plugin, whose switch is somewhere the user already looks -- the cache's
     * own All Settings panel -- rather than in the roster.
     *
     * The roster row is not merely redundant for those, it is wrong. It would
     * be a second switch over one feature, and the two would disagree the
     * first time either was used: All Settings would say the tile markers are
     * on while the roster had stopped the plugin that draws them, with nothing
     * on either screen to explain the other. One feature, one switch, and for
     * these the switch is the cache's.
     *
     * Different from `adapter`, which hides machinery that has no feature at
     * all and REAPPEARS when it faults or is switched off. A hidden builtin
     * has no such escape hatch and needs none: it is always enabled, and what
     * it does is decided by the varbit it reads.
     */
    bool hidden;
    /**
     * Listed FIRST in the settings roster, and has no switch.
     *
     * For the one plugin that is not a feature at all but the place the
     * client's own knobs are kept. Switching it off would not turn a feature
     * off -- there is none -- it would only take the settings away, which is
     * a state with nothing on either screen to explain it and no way back
     * that does not involve editing an ini by hand.
     *
     * So the roster draws its row without a toggle, PluginHost_SetEnabled
     * refuses to clear it, and a saved `enabled=0` from an older build is
     * ignored rather than obeyed.
     *
     * Different from `hidden`, which has no row at all because its switch
     * lives somewhere else. This one HAS a row -- it is the way in to a page
     * -- and what it does not have is a second state.
     */
    bool essential;
    /** Both may be NULL. `init` is where subscriptions are made. */
    void (*init)(struct ToriRS_PluginCtx* ctx, struct ToriRS_PluginApi const* api);
    void (*shutdown)(struct ToriRS_PluginCtx* ctx);
    /**
     * Rebuild this plugin from its source, in place. NULL for a plugin that has
     * no source to reread -- a compiled-in C plugin, where stopping and
     * starting it IS a full reload.
     *
     * Exists because a scripted plugin's reload is a thing only its adapter can
     * do: the host can drop the subscriptions and call init again, but init for
     * a Lua script re-subscribes the SAME function references, so the file's
     * text is never re-executed and a reload changes nothing. This is the hook
     * where the VM tears its state down and builds a new one.
     *
     * Called between the teardown and the restart, with the plugin stopped. The
     * implementation may rewrite the fields of this very struct -- the host
     * holds it by pointer and rereads them afterwards -- which is how a script
     * that grew a config key or a handler comes back with it.
     */
    void (*reload)(struct ToriRS_PluginCtx* ctx);
};

#endif /* TORIRS_PLUGIN_H */
