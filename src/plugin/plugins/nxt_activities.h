#ifndef TORIRS_PLUGIN_NXT_ACTIVITIES_H
#define TORIRS_PLUGIN_NXT_ACTIVITIES_H

#include "plugin/torirs_plugin_types.h"

#include <assert.h>
#include <stdint.h>

/*
 * The Activities category of the cache's All Settings panel, as NAMES.
 *
 * One header for the whole family because none of this is the client's to
 * choose: every row was read out of the cache and any of them being wrong is a
 * feature that silently does nothing. Collected here, a reader can check the
 * whole set against the panel in one pass instead of hunting three constants
 * per plugin.
 *
 * What each name resolves to is the boot profile's -- `[varbit:npc_highlight]`
 * and friends in revconfig/osrs239 -- reached through api->cache_id. The
 * derivation below is how those ids were obtained and how to re-derive them for
 * another revision; it is not a promise that the numbers are the same there.
 *
 * Where each number comes from, and how to re-derive it:
 *
 *   struct_3620 param_745 -> enum_4024, the 86 setting structs in panel order.
 *   Each struct's param_1077 is the SETTING ID below; param_1078 is the row
 *   kind; param_1084 / param_1230 are the defaults.
 *
 *   The var behind a row is in the cache's own hubs, not guessed:
 *     toggles   read [proc,script6716](id)
 *     dropdowns read [proc,script3962](id)
 *     sliders   read [proc,script3964](id)
 *     colours   read [proc,script4181](id), which returns `varp - 1`
 *
 *       3rd/rscache/tools/cs2/cs2 decompile --cache cache.osrs239 \
 *           --rev osrs239 --out /tmp/cs2 6716 3962 3964 4181
 *
 * See NXT_CLIENT_PLUGINS.md for the whole table and what each row has to do.
 *
 * A COLOUR row's varp holds `colour + 1` so that zero can mean "never chosen";
 * read one with api->setting_color, which does that arithmetic and takes the
 * row's own default as its fallback. The NXT_COL_* values below ARE those
 * defaults (`param_1230`), so a colour never read as unset differs from the
 * panel's swatch.
 *
 * ---- HALF THESE VARBITS MEAN "OFF" ----
 *
 * `param_1084` is not a default. It is a DISPLAY INVERSION: the row builder
 * (clientscript 3846) reads it into a boolean and, when it is set, draws the
 * checkbox as `1 - varbit`. So for those rows a varbit of 0 is a TICKED box
 * and a switched-ON feature, and the driving scripts agree --
 * clientscript 6681 installs the tile-marker client op when `%varbit12342 = 0`,
 * and 8319 lights the poll booths when `%varbit9538 = 0`.
 *
 * 30 of the 54 desktop toggles in this category are inverted and 24 are not,
 * with no pattern to them, so every one below says which it is and is read
 * through NXT_ON() or NXT_ON_INVERTED() rather than as a bare truth value.
 * Reading an inverted row the plain way is a feature that is on exactly when
 * the user asked for it to be off, which looks like it works until someone
 * switches it off.
 */

/*
 * Rows are named, not numbered.
 *
 * The ids below are the profile's -- `[varbit:npc_highlight] id=14168` and its
 * kin in revconfig/osrs239 -- and what this header holds is the NAME. A number
 * here would pin every builtin to one revision and fail silently on any other:
 * an id the cache renumbered reads as an unset var, which for an inverted row
 * is a feature that switches itself ON for a user who never asked.
 *
 * `absent` is what a row this cache does not have must read as. It is always
 * the OFF answer, which is 0 for a plain row and 1 for an inverted one -- so
 * both spellings below come out false, and a builtin whose switch does not
 * exist here stays switched off.
 */
static inline int
nxt_varbit(
    struct ToriRS_PluginApi const* api,
    struct ToriRS_PluginCtx* ctx,
    char const* name,
    int absent)
{
    int id;
    assert(api);
    assert(name);
    id = api->cache_id(ctx, "varbit", name);
    return id < 0 ? absent : api->varbit(ctx, id);
}

static inline int
nxt_varp(
    struct ToriRS_PluginApi const* api,
    struct ToriRS_PluginCtx* ctx,
    char const* name,
    int absent)
{
    int id;
    assert(api);
    assert(name);
    id = api->cache_id(ctx, "varp", name);
    return id < 0 ? absent : api->varp(ctx, id);
}

/** A colour row, as 0xRRGGBB; `fallback` covers unset AND absent alike. */
static inline uint32_t
nxt_setting_color(
    struct ToriRS_PluginApi const* api,
    struct ToriRS_PluginCtx* ctx,
    char const* name,
    uint32_t fallback)
{
    int id;
    assert(api);
    assert(name);
    id = api->cache_id(ctx, "varp", name);
    return id < 0 ? fallback : api->setting_color(ctx, id, fallback);
}

/** A PLAIN row: the varbit is the feature. */
#define NXT_ON(api, ctx, name) (nxt_varbit((api), (ctx), (name), 0) != 0)
/** An INVERTED row (`param_1084 = 1`): the varbit is the feature's ABSENCE. */
#define NXT_ON_INVERTED(api, ctx, name) (nxt_varbit((api), (ctx), (name), 1) == 0)

/* ---- General ----------------------------------------------------------- */

/** "Tile highlighting": shift + right-click the ground to place a marker.
 *  INVERTED. Implemented entirely by the cache now -- clientscript 6681
 *  installs the "Mark tile" client op and sets up highlight tile group 6 --
 *  so nothing here reads it; it is listed to keep the table complete. */
#define NXT_VARBIT_TILE_MARKERS "tile_markers"
/** "Tile highlight colour". Default #00FF00. */
#define NXT_VARP_TILE_MARKER_COLOR "tile_marker_color"
#define NXT_COL_TILE_MARKER 0x00FF00u
/** "Clear your highlighted tiles" -- a BUTTON row, so it has no var at all.
 *  Seen through EV_SETTING; see ToriRS_SettingEvent. */
#define NXT_SETTING_CLEAR_TILE_MARKERS "clear_tile_markers"

/** "Highlight entities on mouse-over". PLAIN. No cache script drives it. */
#define NXT_VARBIT_HOVER_ENTITY "hover_entity"

/** "Highlight hovered tile" (+ always-on-top, + colour). PLAIN. #BEBA6E. */
#define NXT_VARBIT_HOVER_TILE "hover_tile"
#define NXT_VARBIT_HOVER_TILE_ONTOP "hover_tile_ontop"
#define NXT_VARP_HOVER_TILE_COLOR "hover_tile_color"
#define NXT_COL_HOVER_TILE 0xBEBA6Eu

/** "Highlight current tile". PLAIN. #9A9733. */
#define NXT_VARBIT_CURRENT_TILE "current_tile"
#define NXT_VARBIT_CURRENT_TILE_ONTOP "current_tile_ontop"
#define NXT_VARP_CURRENT_TILE_COLOR "current_tile_color"
#define NXT_COL_CURRENT_TILE 0x9A9733u

/** "Highlight destination tile". PLAIN. #A9A753. */
#define NXT_VARBIT_DEST_TILE "dest_tile"
#define NXT_VARBIT_DEST_TILE_ONTOP "dest_tile_ontop"
#define NXT_VARP_DEST_TILE_COLOR "dest_tile_color"
#define NXT_COL_DEST_TILE 0xA9A753u

/** "NPC highlight" and its seven qualifiers. All PLAIN. */
#define NXT_VARBIT_NPC_HIGHLIGHT "npc_highlight"
/** "- Display name": 0 off, 1 normal, 2 bold (enum_4604). */
#define NXT_VARBIT_NPC_NAME "npc_name"
/** "- Highlight tile": 0 off, 1 outline only, 2 outline and fill (enum_4603). */
#define NXT_VARBIT_NPC_TILE "npc_tile"
/** "- Highlight outline": the model silhouette. */
#define NXT_VARBIT_NPC_OUTLINE "npc_outline"
/** "- Highlighting colour" / "- Text colour". Both default #05F8F8. */
#define NXT_VARP_NPC_HIGHLIGHT_COLOR "npc_highlight_color"
#define NXT_VARP_NPC_TEXT_COLOR "npc_text_color"
#define NXT_COL_NPC_HIGHLIGHT 0x05F8F8u
/** "- Tagging": offer Tag/Untag on the right-click menu. */
#define NXT_VARBIT_NPC_TAGGING "npc_tagging"
/** "Clear your highlighted NPCs" -- a BUTTON row, like 117 above. */
#define NXT_SETTING_CLEAR_NPC_TAGS "clear_npc_tags"
/** "Display all NPC names above their body": 0 off, 1 normal, 2 bold. */
#define NXT_VARBIT_NPC_NAMES_ALL "npc_names_all"
/** "NPC names text colour". Default #05F8F8. */
#define NXT_VARP_NPC_NAMES_COLOR "npc_names_color"

/* ---- Skills ------------------------------------------------------------ */

/** "Bird nest notification". INVERTED -- struct_3737 carries `param_1084`. */
#define NXT_VARBIT_BIRD_NEST "bird_nest"

/* ---- Combat ------------------------------------------------------------ */

/*
 * The three cannon ammunition rows. The cache's own varbit names pin all of
 * them: `cannon_low_notification_enabled`, `cannon_low_amount`,
 * `cannon_no_ammo_notification_enabled`. Both toggles are PLAIN.
 */
#define NXT_VARBIT_CANNON_LOW_NOTIFY "cannon_low_notify"
#define NXT_VARBIT_CANNON_LOW_AMOUNT "cannon_low_amount"
#define NXT_VARBIT_CANNON_NO_AMMO_NOTIFY "cannon_no_ammo_notify"

/* The two varps the notification reads. Not settings -- they are the game's
 * own state -- but they are cache ids the client knows by name all the same.
 * `rockthrower` is the count left in your cannon; `ownedmcannon_temp` is its
 * coord, 0 when you have none. */
#define NXT_VARP_CANNON_AMMO "cannon_ammo"
#define NXT_VARP_CANNON_COORD "cannon_coord"

/* ---- back to General --------------------------------------------------- */

/** "Highlight poll booths". INVERTED -- clientscript 8319 lights them when
 *  this reads 0, beside `%varbit4337` for "there is an active poll". */
#define NXT_VARBIT_POLL_BOOTHS "poll_booths"

/** The two three-way name/tile choices, which share their meaning across
 *  settings 258, 264 (name) and 259 (tile). */
#define NXT_NAME_OFF 0
#define NXT_NAME_NORMAL 1
#define NXT_NAME_BOLD 2
#define NXT_TILE_OFF 0
#define NXT_TILE_OUTLINE 1
#define NXT_TILE_OUTLINE_FILL 2

#endif /* TORIRS_PLUGIN_NXT_ACTIVITIES_H */
