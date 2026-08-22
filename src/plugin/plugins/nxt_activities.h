#ifndef TORIRS_PLUGIN_NXT_ACTIVITIES_H
#define TORIRS_PLUGIN_NXT_ACTIVITIES_H

/*
 * The Activities category of the cache's All Settings panel (interface 134),
 * as ids.
 *
 * One header for the whole family because the numbers are not this client's to
 * choose: every one of them was read out of the cache and any of them being
 * wrong is a feature that silently does nothing. Collected here, a reader can
 * check the whole set against the panel in one pass instead of hunting three
 * constants per plugin.
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

/** A PLAIN row: the varbit is the feature. */
#define NXT_ON(api, ctx, varbit_id) ((api)->varbit((ctx), (varbit_id)) != 0)
/** An INVERTED row (`param_1084 = 1`): the varbit is the feature's ABSENCE. */
#define NXT_ON_INVERTED(api, ctx, varbit_id) ((api)->varbit((ctx), (varbit_id)) == 0)

/* ---- General ----------------------------------------------------------- */

/** "Tile highlighting": shift + right-click the ground to place a marker.
 *  INVERTED. Implemented entirely by the cache now -- clientscript 6681
 *  installs the "Mark tile" client op and sets up highlight tile group 6 --
 *  so nothing here reads it; it is listed to keep the table complete. */
#define NXT_VARBIT_TILE_MARKERS 12342
/** "Tile highlight colour". Default #00FF00. */
#define NXT_VARP_TILE_MARKER_COLOR 3108
#define NXT_COL_TILE_MARKER 0x00FF00u
/** "Clear your highlighted tiles" -- a BUTTON row, so it has no var at all.
 *  Seen through EV_SETTING; see ToriRS_PluginEvSetting. */
#define NXT_SETTING_CLEAR_TILE_MARKERS 117

/** "Highlight entities on mouse-over". PLAIN. No cache script drives it. */
#define NXT_VARBIT_HOVER_ENTITY 13088

/** "Highlight hovered tile" (+ always-on-top, + colour). PLAIN. #BEBA6E. */
#define NXT_VARBIT_HOVER_TILE 12977
#define NXT_VARBIT_HOVER_TILE_ONTOP 12980
#define NXT_VARP_HOVER_TILE_COLOR 3155
#define NXT_COL_HOVER_TILE 0xBEBA6Eu

/** "Highlight current tile". PLAIN. #9A9733. */
#define NXT_VARBIT_CURRENT_TILE 12978
#define NXT_VARBIT_CURRENT_TILE_ONTOP 12981
#define NXT_VARP_CURRENT_TILE_COLOR 3156
#define NXT_COL_CURRENT_TILE 0x9A9733u

/** "Highlight destination tile". PLAIN. #A9A753. */
#define NXT_VARBIT_DEST_TILE 12979
#define NXT_VARBIT_DEST_TILE_ONTOP 12982
#define NXT_VARP_DEST_TILE_COLOR 3157
#define NXT_COL_DEST_TILE 0xA9A753u

/** "NPC highlight" and its seven qualifiers. All PLAIN. */
#define NXT_VARBIT_NPC_HIGHLIGHT 14168
/** "- Display name": 0 off, 1 normal, 2 bold (enum_4604). */
#define NXT_VARBIT_NPC_NAME 14169
/** "- Highlight tile": 0 off, 1 outline only, 2 outline and fill (enum_4603). */
#define NXT_VARBIT_NPC_TILE 14171
/** "- Highlight outline": the model silhouette. */
#define NXT_VARBIT_NPC_OUTLINE 14170
/** "- Highlighting colour" / "- Text colour". Both default #05F8F8. */
#define NXT_VARP_NPC_HIGHLIGHT_COLOR 3540
#define NXT_VARP_NPC_TEXT_COLOR 3541
#define NXT_COL_NPC_HIGHLIGHT 0x05F8F8u
/** "- Tagging": offer Tag/Untag on the right-click menu. */
#define NXT_VARBIT_NPC_TAGGING 11518
/** "Clear your highlighted NPCs" -- a BUTTON row, like 117 above. */
#define NXT_SETTING_CLEAR_NPC_TAGS 267
/** "Display all NPC names above their body": 0 off, 1 normal, 2 bold. */
#define NXT_VARBIT_NPC_NAMES_ALL 14178
/** "NPC names text colour". Default #05F8F8. */
#define NXT_VARP_NPC_NAMES_COLOR 3542

/* ---- Skills ------------------------------------------------------------ */

/** "Bird nest notification". INVERTED -- struct_3737 carries `param_1084`. */
#define NXT_VARBIT_BIRD_NEST 13087

/* ---- Combat ------------------------------------------------------------ */

/*
 * The three cannon ammunition rows. The cache's own varbit names pin all of
 * them: `cannon_low_notification_enabled`, `cannon_low_amount`,
 * `cannon_no_ammo_notification_enabled`. Both toggles are PLAIN.
 */
#define NXT_VARBIT_CANNON_LOW_NOTIFY 14175
#define NXT_VARBIT_CANNON_LOW_AMOUNT 14176
#define NXT_VARBIT_CANNON_NO_AMMO_NOTIFY 14177

/* ---- back to General --------------------------------------------------- */

/** "Highlight poll booths". INVERTED -- clientscript 8319 lights them when
 *  this reads 0, beside `%varbit4337` for "there is an active poll". */
#define NXT_VARBIT_POLL_BOOTHS 9538

/** The two three-way name/tile choices, which share their meaning across
 *  settings 258, 264 (name) and 259 (tile). */
#define NXT_NAME_OFF 0
#define NXT_NAME_NORMAL 1
#define NXT_NAME_BOLD 2
#define NXT_TILE_OFF 0
#define NXT_TILE_OUTLINE 1
#define NXT_TILE_OUTLINE_FILL 2

#endif /* TORIRS_PLUGIN_NXT_ACTIVITIES_H */
