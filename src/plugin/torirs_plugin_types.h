#ifndef TORIRS_PLUGIN_TYPES_H
#define TORIRS_PLUGIN_TYPES_H

/*
 * Language-neutral values shared by the public V2 API and the host's engine
 * seam. This file contains data only: no registration contract, callback bus,
 * host context, or callable API table.
 *
 * Coordinates in snapshots are absolute tiles. Fine positions are
 * scene-relative 128ths of a tile, matching the scene projection contract.
 */

#include <stdbool.h>
#include <stdint.h>

#include "plugin/torirs_plugin_placement.h"

/* Bumped whenever anything below changes shape. A plugin compiled against a
 * different value is refused rather than run against a struct it disagrees
 * about. */
#define TORIRS_PLUGIN_NAME_MAX 48
/** Semantic role spelling, terminator included. Kept in the public contract
 * because replacement claims retain the name for their whole lifetime. */
#define TORIRS_PLUGIN_ROLE_NAME_MAX 64
/** Stable canonical frame id (`plugin-id/local-id`), terminator included. */
#define TORIRS_PLUGIN_FRAME_ID_MAX 128
/** One provider-local frame id, before the host adds the plugin namespace. */
#define TORIRS_PLUGIN_FRAME_LOCAL_ID_MAX 48
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
#define TORIRS_PLUGIN_REGION_OPS_MAX 8

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
#define TORIRS_KEY_ESCAPE 37
#define TORIRS_KEY_SHIFT 42
#define TORIRS_KEY_CTRL 43
#define TORIRS_KEY_TAB 44
#define TORIRS_KEY_SPACE 45

/* Opaque per-plugin instance handle. The host defines it. */
/**
 * What `api->screen` answers: where the client is in a session.
 *
 * The values are enum AppScreen's, spelled again here because a plugin header
 * must not include the app's -- the same "local copies of constants" pattern
 * the rest of this file uses. engine/torirs_plugin_host.c static_asserts the
 * two agree, so a value that moved cannot go unnoticed.
 */
enum ToriRS_Screen
{
    /** Engine still coming up; no title tree yet. */
    TORIRS_SCREEN_BOOT = 0,
    /** Title screen: main menu, login form or the info page. */
    TORIRS_SCREEN_TITLE = 10,
    /** Login handshake in flight; the title tree is still what draws. */
    TORIRS_SCREEN_CONNECTING = 20,
    /** In game: the gameframe is rooted and its parts exist. */
    TORIRS_SCREEN_GAME = 30
};

/* ------------------------------------------------------------------------ */
/* Snapshots                                                                 */
/* ------------------------------------------------------------------------ */

struct ToriRS_PlayerSnapshot
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

struct ToriRS_NpcSnapshot
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
    /**
     * The overhead health bar's FILL, as the server last sent it, and the
     * denominator it is a fraction of. Both -1 when no bar has ever been sent
     * for this entity.
     *
     * The reference's `Actor.getHealthRatio()` / `getHealthScale()`, and the
     * primitive rather than the conclusion: "is it dying" is one question a
     * caller can ask of these, and "is it below a third" is another. A HEADBAR
     * block carries no hitpoints at all -- only this fraction -- so this is
     * the whole of what the wire says about an npc's health, and a plugin
     * cannot get real hitpoints out of it however it is scaled.
     *
     * A `health_ratio` of 0 with a scale that exists is what RuneLite's
     * `NpcUtil.isDying` is built on: the bar reached empty, which for almost
     * every monster in the game is the moment it died. The exceptions there
     * are a hand-written table -- gargoyles and the other "finish it with an
     * item" slayer monsters despawn with hitpoints left, and a few bosses
     * transform rather than die -- and a caller that cares about them keeps
     * its own list, because this cannot know which is which.
     *
     * -1 and not 0 for "never sent", because 0 is the most meaningful value
     * this field takes and a caller that could not tell the two apart would
     * read every npc that has never been in combat as a corpse.
     */
    int health_ratio;
    int health_scale;
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
struct ToriRS_ScenerySnapshot
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

struct ToriRS_GroundItemSnapshot
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
 * Which of the client's OWN containers. @see api->game->inventory_slot.
 *
 * Names rather than numbers, because the numbers are the client's and it
 * already holds them (INV_MANAGER_CONTAINER_WORN and friends). A plugin
 * carrying 94 of its own would be carrying a copy of a constant it cannot
 * check, on a client that boots several revisions.
 */
enum ToriRS_InventoryKind
{
    /** The backpack: the 28 slots of the inventory tab. */
    TORIRS_INVENTORY_BACKPACK = 0,
    /** Worn equipment, indexed by an objtype's `wearpos`. */
    TORIRS_INVENTORY_WORN,
    TORIRS_INVENTORY_BANK
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
enum ToriRS_EquipmentBonus
{
    TORIRS_EQUIPMENT_BONUS_ATTACK_STAB = 0,
    TORIRS_EQUIPMENT_BONUS_ATTACK_SLASH,
    TORIRS_EQUIPMENT_BONUS_ATTACK_CRUSH,
    TORIRS_EQUIPMENT_BONUS_ATTACK_MAGIC,
    TORIRS_EQUIPMENT_BONUS_ATTACK_RANGE,
    TORIRS_EQUIPMENT_BONUS_DEFENCE_STAB,
    TORIRS_EQUIPMENT_BONUS_DEFENCE_SLASH,
    TORIRS_EQUIPMENT_BONUS_DEFENCE_CRUSH,
    TORIRS_EQUIPMENT_BONUS_DEFENCE_MAGIC,
    TORIRS_EQUIPMENT_BONUS_DEFENCE_RANGE,
    TORIRS_EQUIPMENT_BONUS_STRENGTH,
    TORIRS_EQUIPMENT_BONUS_PRAYER,
    TORIRS_EQUIPMENT_BONUS_COUNT
};

/**
 * One objtype, as the cache states it. @see api->game->item_info.
 *
 * The record and nothing else: an obj snapshot describes a stack lying on a
 * tile, and this describes the ITEM -- what it is called, what it is worth,
 * where it is worn and what it does to a combat roll.
 */
struct ToriRS_ItemInfo
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
    /** Indexed by enum ToriRS_EquipmentBonus. */
    int bonus[TORIRS_EQUIPMENT_BONUS_COUNT];
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
 * One group of recorded loot: what killed you gave up, and how often.
 * @see api->game->loot_source_next.
 */
struct ToriRS_LootSource
{
    /** The store's own id, which addresses its rows. */
    int id;
    /** What the kill was called, as the store recorded it. */
    char name[64];
    /** Distinct item rows under it. */
    int row_count;
    /**
     * How many of this thing you have killed.
     *
     * Bumped once per DEATH and not once per item, because the store groups a
     * multi-item drop under one event id -- which is the difference between a
     * kill count and a drop count, and the number the game's own "Name x N"
     * shows.
     */
    int kill_count;
};

/** One row of one source: an item, a quantity, and what the store priced it
 *  at when it landed. @see api->game->loot_row_next. */
struct ToriRS_LootRow
{
    int obj_id;
    int quantity;
    /** The value the store recorded, which is ObjType.cost at drop time. */
    int value;
};

/**
 * Which of the client's three inventory-icon variants to rasterise.
 *
 * The same three the interface emitter already asks the scene bridge for, and
 * not a fourth: an icon is baked art, so every variant here is one the client
 * builds anyway for its own inventory and is therefore free to hand over.
 * @see api->game->item_image.
 */
enum ToriRS_ItemIconStyle
{
    /**
     * No baked outline and no drop shadow -- the reference's
     * `ObjType.getSprite(outlineRgb = -1)`.
     *
     * For a caller that intends to draw the icon on art of its own and apply
     * its own edge, because stacking an outline pass on a shadow-baked icon
     * doubles the shadow.
     */
    TORIRS_ITEM_ICON_PLAIN = 0,
    /**
     * A black border baked into the pixels, and the one to reach for.
     *
     * It is what the client's own dense item grids use, and the reason is
     * legibility rather than taste: an unbordered icon on a dark panel loses
     * its silhouette, and a grid of them reads as a smear.
     */
    TORIRS_ITEM_ICON_BORDERED,
    /** The white outline the client puts on the item armed for "Use"
     *  (`outlineRgb = 0xFFFFFF`). For marking one entry of a set. */
    TORIRS_ITEM_ICON_SELECTED
};

/** What a highlight item is attached to. @see ToriRS_HighlightItem. */
enum ToriRS_HighlightKind
{
    TORIRS_HIGHLIGHT_TILE = 0,
    TORIRS_HIGHLIGHT_NPC,
    TORIRS_HIGHLIGHT_LOC,
    TORIRS_HIGHLIGHT_OBJ,
    /** A player, named by DISPLAY NAME -- `highlight_player_on` is the one
     *  form of the family whose subject is a string. */
    TORIRS_HIGHLIGHT_PLAYER
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
struct ToriRS_HighlightItem
{
    /** enum ToriRS_HighlightKind. */
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

struct ToriRS_FrameEvent
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

struct ToriRS_TickEvent
{
    /** logic_cycle for LOGIC_TICK, world cycle for SERVER_TICK. */
    int cycle;
};

struct ToriRS_WorldLoadedEvent
{
    int base_tile_x;
    int base_tile_z;
};

/** @see on_screen_changed. Both are TORIRS_SCREEN_*. */
struct ToriRS_ScreenChangedEvent
{
    /** What api->screen answers now. */
    int screen;
    /** What it answered until this moment. */
    int previous;
};




struct ToriRS_KeyEvent
{
    /** enum LibToriRS_KeyCode. */
    int key;
    /** Typed character, or 0. */
    int ch;
    bool down;
};

/** Read-only view of one built minimenu row. */
struct ToriRS_MenuRow
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

struct ToriRS_MenuBuildEvent
{
    int row_count;
    struct ToriRS_MenuRow rows[TORIRS_PLUGIN_MENU_ROWS_MAX];
    /** True for the hover-text rebuild, which runs EVERY FRAME. Handlers that
     *  only need rows in the right-click menu should return immediately. */
    bool hover_pass;
    /** Opaque; hand back to api->menu_add. */
    void* host_cursor;
};

struct ToriRS_MenuSelectEvent
{
    struct ToriRS_MenuRow row;
    /** The tag passed to api->menu_add, or 0 for a native row. */
    uint32_t plugin_tag;
    /** True when this row belongs to the plugin being dispatched. */
    bool owned;
    int click_x;
    int click_y;
};




/**
 * The canvas the frame is being declared against. @see EV_LAYOUT.
 *
 * Carries the size and nothing else, because everything else a layout needs is
 * the plugin's own arithmetic: where the sidebar goes at 1440x900 is a
 * statement the plugin makes, not one the host can be asked for.
 */



struct ToriRS_ChatMessageEvent
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
struct ToriRS_GameEvent
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

/** What the pointer is over. @see api->input.hover_entity. */
enum ToriRS_HoverKind
{
    TORIRS_HOVER_NONE = 0,
    TORIRS_HOVER_SCENERY,
    TORIRS_HOVER_NPC,
    TORIRS_HOVER_PLAYER,
    TORIRS_HOVER_OBJ
};

/**
 * The nearest entity under the pointer, as the last rendered frame picked it.
 *
 * Not the same question as hover_tile, which answers with the GROUND under the
 * pointer and is filled even when the cursor is over open grass. This one is
 * about a thing: the first scenery/npc/player/objstack in the frame's pickset,
 * which is the one the client's own left-click would act on.
 */
struct ToriRS_HoverTarget
{
    /** enum ToriRS_HoverKind. NONE when nothing is under the pointer. */
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

/** What happened to a control. */
enum ToriRS_PanelActionKind
{
    /** A button was pressed. */
    TORIRS_PANEL_ACTION_ACTIVATE = 0,
    /** A checkbox changed: `value` is its new state. */
    TORIRS_PANEL_ACTION_TOGGLE,
    /** A field was edited: `text` is its whole new contents. */
    TORIRS_PANEL_ACTION_TEXT,
    /** A list choice was made: `value` is the index, `text` the chosen entry. */
    TORIRS_PANEL_ACTION_PICK,
    /** A custom region's pointer capture moved. `value` is implementation
     *  neutral; `x`/`y` in EvPanelAction carry the local position. */
    TORIRS_PANEL_ACTION_DRAG,
    /** A custom region was scrolled. `value` is the signed logical delta. */
    TORIRS_PANEL_ACTION_SCROLL,
    /** A custom region received a key. `value` is a TORIRS_KEY_* code. */
    TORIRS_PANEL_ACTION_KEY,
};


/* ------------------------------------------------------------------------ */
/* Application plugin panel (ABI 21)                                        */
/* ------------------------------------------------------------------------ */

/** The default and bounded width hints used by every shell presenter. */
#define TORIRS_PANEL_WIDTH_DEFAULT 320
#define TORIRS_PANEL_WIDTH_MIN 280
#define TORIRS_PANEL_WIDTH_MAX 480
/** Bounded logical height of one custom drawing well. */
#define TORIRS_PANEL_CUSTOM_HEIGHT_DEFAULT 120
#define TORIRS_PANEL_CUSTOM_HEIGHT_MIN 48
#define TORIRS_PANEL_CUSTOM_HEIGHT_MAX 512

/** Inert rail metadata copied by panel_request during EV_START. */
struct ToriRS_PanelDescriptor
{
    /**
     * There is no title here, and that absence is the point.
     *
     * A plugin's name is `ToriRS_PluginDefV2::title` -- the one a person sees in
     * the roster, beside its switch, and against its saved settings. Letting
     * `panel_request` supply a second one made the name a thing a plugin could
     * change at runtime: two plugins could claim one spelling, a row could
     * stop matching the settings section it opens, and the name in the roster
     * could differ from the name in the manifest with nothing to say which was
     * real. The def's title is the identity, and identity is not the page's to
     * edit.
     */
    /** Optional sandboxed PNG asset name. The host loads it automatically;
     * malformed, over-budget, or larger-than-64x64 art uses the baked wrench.
     * NULL or empty asks for that fallback directly. */
    char const* icon_asset;
    /** Logical units; 0 asks for TORIRS_PANEL_WIDTH_DEFAULT. */
    int preferred_width;
};

/** Neutral allocation class. It describes space, never the platform. */
enum ToriRS_PanelSizeClass
{
    TORIRS_PANEL_SIZE_COMPACT = 0,
    TORIRS_PANEL_SIZE_MEDIUM,
    TORIRS_PANEL_SIZE_EXPANDED,
};

/**
 * WHICH of a plugin's two faces a page build is for.
 *
 * A plugin with a panel has two things a person might have come to it for, and
 * they are not the same thing: what it is SAYING right now -- the loot it has
 * recorded, the xp it has watched -- and how it is CONFIGURED. Stacking both
 * on one page made the second arrive by scrolling past the first, and made the
 * plugin's own rail stone and its row in the settings roster lead to identical
 * screens, so neither destination meant anything.
 *
 * So the ENTRY POINT chooses, and the build is told which one it is answering.
 * The stone opens the page; the roster opens the settings.
 *
 * A plugin that declares nothing for SETTINGS is not broken and needs no
 * handler for it: the host presents the form generated from its config schema,
 * which is exactly what every plugin had before this existed. Declaring there
 * ADDS to that form rather than replacing it -- the schema's rows are staged
 * and committed by the page's own Save, which a plugin cannot reimplement and
 * should not have to.
 */
enum ToriRS_PanelView
{
    /** The plugin's ACTIVE screen. Opened from its own rail entry. */
    TORIRS_PANEL_VIEW_PAGE = 0,
    /** Its SETTINGS. Opened from the Manage Plugins roster. */
    TORIRS_PANEL_VIEW_SETTINGS
};

/** The page model was cleared for this exact selection and must be declared. */

/** One result-state intent from the shared shell. */
struct ToriRS_PanelActionEvent
{
    /** Plugin-scoped semantic id. Valid for this dispatch only. */
    char const* id;
    /** enum ToriRS_PanelActionKind. */
    int action;
    /** Result value, index, scroll delta, or key according to action. */
    int value;
    /** Whole result text, never NULL and valid for this dispatch only. For a
     * V2 structured PICK this is the selected stable value, never its label. */
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
struct ToriRS_PanelLayoutEvent
{
    int width;
    int height;
    /** Scale in thousandths: 1000 is 1x, 2000 is 2x. */
    int scale_milli;
    /** enum ToriRS_PanelSizeClass. */
    int size_class;
    bool visible;
    /** False in attached-exclusive presentation. */
    bool game_visible;
    uint32_t selection_generation;
};

/** A scoped draw pass for one custom semantic node. */

struct ToriRS_AssetEvent
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

enum ToriRS_ConfigType
{
    TORIRS_CONFIG_BOOL = 0,
    /** Uses min/max. */
    TORIRS_CONFIG_INT,
    /** Written as "#RRGGBB" by the panel, read back as 0xRRGGBB. */
    TORIRS_CONFIG_COLOR,
    /** Also the carrier for lists, as comma-separated text. */
    TORIRS_CONFIG_STRING,
    /** `choices` is a '|'-separated set, rendered as a dropdown. */
    TORIRS_CONFIG_ENUM,
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
    TORIRS_CONFIG_TEXT
};

struct ToriRS_ConfigItem
{
    /** INI key: [a-z0-9_]. A NULL key terminates the array. */
    char const* key;
    enum ToriRS_ConfigType type;
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
     * POSITIONAL initialisers (`{ "show_dest", TORIRS_CONFIG_BOOL, "Show
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
enum ToriRS_DisplaySetting
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
    TORIRS_DISPLAY_UI_SCALE = 0,
    /** How that stretch is filtered: 0 nearest, 1 linear, 2 bicubic. */
    TORIRS_DISPLAY_UI_SCALE_FILTER,

    TORIRS_DISPLAY_SETTING_COUNT
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
enum ToriRS_GameVariant
{
    /** Nothing has stated one yet. @see api->core.lane. */
    TORIRS_GAME_UNKNOWN = 0,
    TORIRS_GAME_OLDSCHOOL = 1,
    /** The classic client's lineage: the 2004 dat1 worlds AND the later dat2
     *  RS2 revisions, which are that same client with a different container. */
    TORIRS_GAME_RS2 = 2
};

/** Which on-disk container family the cache is stored in. Not a proxy for the
 *  lineage: OldSchool and the later RS2 revisions are both dat2. */
enum ToriRS_CacheEpoch
{
    TORIRS_CACHE_EPOCH_UNKNOWN = 0,
    /** Jagfile era: main_file_cache.dat + .idx1..5. */
    TORIRS_CACHE_EPOCH_DAT1 = 1,
    /** JS5: main_file_cache.dat2 + .idx0..N. */
    TORIRS_CACHE_EPOCH_DAT2 = 2
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
struct ToriRS_LaneInfo
{
    /** enum ToriRS_GameVariant. */
    int game;
    /** enum ToriRS_CacheEpoch. */
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
#define TORIRS_FEATURE_UNSET (-1)

/** Bytes of a feature key / label, terminator included. */
#define TORIRS_FEATURE_KEY_MAX 32
#define TORIRS_FEATURE_LABEL_MAX 64
/** Bytes of a feature's '|'-separated choice list, terminator included. */
#define TORIRS_FEATURE_CHOICES_MAX 224
/** Choices one flag may offer. */
#define TORIRS_FEATURE_VALUES_MAX 12

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
enum ToriRS_FeatureKind
{
    /**
     * A number. `choices` are the values worth naming and `values[i]` is the
     * number each stands for, but `min`..`max` is the flag's real range: a
     * value outside the list is legal, and a settings file that carries one
     * keeps it. So this is stored as the NUMBER.
     */
    TORIRS_FEATURE_INT = 0,
    /**
     * One of `choices` and nothing else, where choice i has value `values[i]`.
     *
     * The values are carried rather than implied by the index because a flag's
     * legal set is not always 0..n: `target_mask_held` is 0x10 or 0x20, and an
     * index would make the panel write 1 for a bit that is 32. Stored as the
     * CHOICE TEXT, so a settings file survives the list gaining an entry.
     */
    TORIRS_FEATURE_ENUM
};

/** One published flag, as feature_next reports it. */
struct ToriRS_FeatureInfo
{
    char key[TORIRS_FEATURE_KEY_MAX];
    /**
     * What a PERSON is shown. Never empty.
     *
     * SHORT -- it shares a row with the control, and a settings panel is
     * narrow. What the flag is about belongs in `section`; what its values mean
     * belongs in the choice names.
     */
    char label[TORIRS_FEATURE_LABEL_MAX];
    /**
     * Heading this flag sits under, or "" for one that sits under none.
     *
     * The walk is in section order, so a reader groups by "the section
     * changed" rather than by collecting: two runs of the same name are two
     * headings, which is a fact about the engine's list and not something to
     * paper over.
     */
    char section[TORIRS_FEATURE_KEY_MAX];
    /** enum ToriRS_FeatureKind. */
    int kind;
    /** FEATURE_INT: the real range, wider than the named choices. */
    int min;
    int max;
    /** "a|b|c", WITHOUT a "revision default" entry -- that is the sentinel's
     *  job and every flag has it, so no flag states it. */
    char choices[TORIRS_FEATURE_CHOICES_MAX];
    /** The value each choice stands for. */
    int values[TORIRS_FEATURE_VALUES_MAX];
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
enum ToriRS_HullShape
{
    /** The bounds cylinder as an eight-corner box. Fixed cost. */
    TORIRS_HULL_BOUNDS = 0,
    /** The model's own posed geometry: tight, and linear in the mesh. */
    TORIRS_HULL_MESH = 1
};

/* ------------------------------------------------------------------------ */
/* Entities: claiming a thing in the WORLD                                   */
/* ------------------------------------------------------------------------ */

/*
 * Named scene entities can carry retained appearance and menu declarations.
 * What each declaration can do is deliberately narrow because a server
 * entity is not a plugin's to move and its model is not a plugin's to repaint:
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

enum ToriRS_EntityKind
{
    TORIRS_ENTITY_NPC = 1,
    TORIRS_ENTITY_PLAYER,
    TORIRS_ENTITY_LOC,
    TORIRS_ENTITY_OBJ
};

/** What a HITBOX holder does to the game's own rows. @see entity_ops. */
enum ToriRS_EntityOpsMode
{
    /** The game's rows stay; the plugin's are added. */
    TORIRS_ENTITY_OPS_APPEND = 0,
    /** The game's rows for this thing go; the plugin's stand alone. */
    TORIRS_ENTITY_OPS_REPLACE,
    /** The game's rows go and nothing replaces them: not clickable. */
    TORIRS_ENTITY_OPS_NONE
};

/** An APPEARANCE holder's standing declaration for an entity. */
struct ToriRS_EntityAppearance
{
    /** Draw a hull at all. 0 is "claimed and invisible", which is how a
     *  plugin that only wants the click keeps another's outline off it. */
    int hull;
    /** 0xRRGGBB. */
    uint32_t rgb;
    /** Fill alpha 0..255; 0 for an outline alone. */
    int fill_alpha;
    /** enum ToriRS_HullShape. */
    int shape;
};

#endif /* TORIRS_PLUGIN_TYPES_H */
