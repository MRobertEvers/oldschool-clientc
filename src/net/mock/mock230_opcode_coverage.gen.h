/*
 * GENERATED FILE — do not edit.
 *
 * The ServerScript opcodes this server implements, derived from the
 * `case SS_OP_*:` labels in the sources that dispatch them. Regenerate with:
 *
 *     python3 net/mock/gen_opcode_coverage.py
 *
 * `make -C src test-mock230-coverage` fails when this file is stale.
 *
 * Coverage by layer:
 *     63  VM core
 *    248  host commands
 *      9  host commands (db)
 *      8  host commands (inv)
 *      8  host commands (loc)
 *      8  host commands (npc)
 *     13  host commands (obj)
 *      2  host commands (param)
 *      3  host commands (player)
 *    362  total, of 441 declared opcodes
 */

#ifndef SRC_NET_MOCK_MOCK230_OPCODE_COVERAGE_GEN_H
#define SRC_NET_MOCK_MOCK230_OPCODE_COVERAGE_GEN_H

#include <stdint.h>

#define MOCK230_OPCODE_COVERAGE_COUNT 362
#define MOCK230_OPCODE_DECLARED_COUNT 441

/*
 * One past the highest opcode *value*, which is nothing like the number of
 * opcodes: RuneScript numbers them sparsely and the host commands start at
 * 4000, so 396 declared opcodes span values up to 10003. Anything sizing an
 * array by opcode wants this, not the count — using the count silently
 * treats every real opcode as out of range.
 */
#define MOCK230_OPCODE_VALUE_LIMIT 11037

/* Ascending, so a lookup can binary-search. */
static const uint16_t MOCK230_OPCODE_COVERAGE[MOCK230_OPCODE_COVERAGE_COUNT] = {
    0, /* SS_OP_PUSH_CONSTANT_INT (VM core) */
    1, /* SS_OP_PUSH_VARP (host commands) */
    2, /* SS_OP_POP_VARP (host commands) */
    3, /* SS_OP_PUSH_CONSTANT_STRING (VM core) */
    6, /* SS_OP_BRANCH (VM core) */
    7, /* SS_OP_BRANCH_NOT (VM core) */
    8, /* SS_OP_BRANCH_EQUALS (VM core) */
    9, /* SS_OP_BRANCH_LESS_THAN (VM core) */
    10, /* SS_OP_BRANCH_GREATER_THAN (VM core) */
    21, /* SS_OP_RETURN (VM core) */
    22, /* SS_OP_GOSUB (VM core) */
    23, /* SS_OP_JUMP (VM core) */
    24, /* SS_OP_SWITCH (VM core) */
    25, /* SS_OP_PUSH_VARBIT (host commands) */
    27, /* SS_OP_POP_VARBIT (host commands) */
    31, /* SS_OP_BRANCH_LESS_THAN_OR_EQUALS (VM core) */
    32, /* SS_OP_BRANCH_GREATER_THAN_OR_EQUALS (VM core) */
    33, /* SS_OP_PUSH_INT_LOCAL (VM core) */
    34, /* SS_OP_POP_INT_LOCAL (VM core) */
    35, /* SS_OP_PUSH_STRING_LOCAL (VM core) */
    36, /* SS_OP_POP_STRING_LOCAL (VM core) */
    37, /* SS_OP_JOIN_STRING (VM core) */
    38, /* SS_OP_POP_INT_DISCARD (VM core) */
    39, /* SS_OP_POP_STRING_DISCARD (VM core) */
    40, /* SS_OP_GOSUB_WITH_PARAMS (VM core) */
    41, /* SS_OP_JUMP_WITH_PARAMS (VM core) */
    44, /* SS_OP_DEFINE_ARRAY (VM core) */
    45, /* SS_OP_PUSH_ARRAY_INT (VM core) */
    46, /* SS_OP_POP_ARRAY_INT (VM core) */
    1000, /* SS_OP_COORDX (host commands) */
    1001, /* SS_OP_COORDY (host commands) */
    1002, /* SS_OP_COORDZ (host commands) */
    1003, /* SS_OP_DISTANCE (host commands) */
    1004, /* SS_OP_INZONE (host commands) */
    1005, /* SS_OP_LINEOFSIGHT (host commands) */
    1006, /* SS_OP_LINEOFWALK (host commands) */
    1007, /* SS_OP_MAP_BLOCKED (host commands) */
    1008, /* SS_OP_MAP_CLOCK (host commands) */
    1009, /* SS_OP_MAP_FINDSQUARE (host commands) */
    1013, /* SS_OP_MAP_LOC (host commands) */
    1014, /* SS_OP_MAP_MEMBERS (host commands) */
    1015, /* SS_OP_MAP_MULTIWAY (host commands) */
    1016, /* SS_OP_MAP_PLAYERCOUNT (host commands) */
    1017, /* SS_OP_MOVECOORD (host commands) */
    1019, /* SS_OP_PROJANIM_MAP (host commands) */
    1021, /* SS_OP_SPOTANIM_MAP (host commands) */
    1022, /* SS_OP_WORLD_DELAY (host commands) */
    2002, /* SS_OP_ANIM (host commands) */
    2005, /* SS_OP_BUSY (host commands) */
    2007, /* SS_OP_CAM_LOOKAT (host commands) */
    2008, /* SS_OP_CAM_MOVETO (host commands) */
    2009, /* SS_OP_CAM_RESET (host commands) */
    2010, /* SS_OP_CAM_SHAKE (host commands) */
    2011, /* SS_OP_CLEARQUEUE (host commands) */
    2012, /* SS_OP_CLEARSOFTTIMER (host commands) */
    2013, /* SS_OP_CLEARTIMER (host commands) */
    2014, /* SS_OP_COORD (host commands) */
    2015, /* SS_OP_DAMAGE (host commands) */
    2016, /* SS_OP_DISPLAYNAME (host commands) */
    2017, /* SS_OP_FACESQUARE (host commands) */
    2019, /* SS_OP_FINDUID (host commands) */
    2020, /* SS_OP_GENDER (host commands) */
    2021, /* SS_OP_GETQUEUE (host commands) */
    2022, /* SS_OP_GETTIMER (host commands) */
    2023, /* SS_OP_GETWALKTRIGGER (host commands) */
    2024, /* SS_OP_HEADICONS_GET (host commands) */
    2025, /* SS_OP_HEADICONS_SET (host commands) */
    2026, /* SS_OP_HEALENERGY (host commands) */
    2031, /* SS_OP_HUNTALL (host commands) */
    2032, /* SS_OP_HUNTNEXT (host commands) */
    2033, /* SS_OP_IF_CLOSE (host commands) */
    2034, /* SS_OP_IF_OPENCHAT (host commands) */
    2035, /* SS_OP_IF_OPENMAIN_SIDE (host commands) */
    2036, /* SS_OP_IF_OPENMAIN (host commands) */
    2037, /* SS_OP_IF_OPENOVERLAY (host commands) */
    2039, /* SS_OP_IF_SETANIM (host commands) */
    2040, /* SS_OP_IF_SETCOLOUR (host commands) */
    2041, /* SS_OP_IF_SETHIDE (host commands) */
    2042, /* SS_OP_IF_SETMODEL (host commands) */
    2043, /* SS_OP_IF_SETNPCHEAD (host commands) */
    2044, /* SS_OP_IF_SETOBJECT (host commands) */
    2045, /* SS_OP_IF_SETPLAYERHEAD (host commands) */
    2046, /* SS_OP_IF_SETPOSITION (host commands) */
    2047, /* SS_OP_IF_ADDRESUMEBUTTON (host commands) */
    2048, /* SS_OP_IF_SETSCROLLPOS (host commands) */
    2051, /* SS_OP_IF_SETTEXT (host commands) */
    2052, /* SS_OP_LAST_COM (host commands) */
    2053, /* SS_OP_LAST_INT (host commands) */
    2054, /* SS_OP_LAST_ITEM (host commands) */
    2056, /* SS_OP_LAST_SLOT (host commands) */
    2057, /* SS_OP_LAST_TARGETSLOT (host commands) */
    2058, /* SS_OP_LAST_USEITEM (host commands) */
    2059, /* SS_OP_LAST_USESLOT (host commands) */
    2060, /* SS_OP_LONGQUEUE (host commands) */
    2061, /* SS_OP_LONGQUEUEVARARG (host commands) */
    2063, /* SS_OP_MES (host commands) */
    2065, /* SS_OP_MIDI_SONG (host commands) */
    2069, /* SS_OP_P_APRANGE (host commands) */
    2070, /* SS_OP_P_ARRIVEDELAY (host commands) */
    2072, /* SS_OP_P_COUNTDIALOG (host commands) */
    2073, /* SS_OP_P_DELAY (host commands) */
    2074, /* SS_OP_P_EXACTMOVE (host commands) */
    2075, /* SS_OP_P_FINDUID (host commands) */
    2076, /* SS_OP_P_LOCMERGE (host commands) */
    2077, /* SS_OP_P_LOGOUT (host commands) */
    2079, /* SS_OP_P_OPLOC (host commands (player)) */
    2080, /* SS_OP_P_OPNPC (host commands) */
    2081, /* SS_OP_P_OPNPCT (host commands) */
    2082, /* SS_OP_P_OPOBJ (host commands (player)) */
    2083, /* SS_OP_P_OPPLAYER (host commands (player)) */
    2085, /* SS_OP_P_PAUSEBUTTON (host commands) */
    2089, /* SS_OP_P_STOPACTION (host commands) */
    2090, /* SS_OP_P_TELEJUMP (host commands) */
    2091, /* SS_OP_P_TELEPORT (host commands) */
    2093, /* SS_OP_P_WALK (host commands) */
    2095, /* SS_OP_PROJANIM_PL (host commands) */
    2096, /* SS_OP_QUEUE (host commands) */
    2097, /* SS_OP_QUEUEVARARG (host commands) */
    2098, /* SS_OP_READYANIM (host commands) */
    2099, /* SS_OP_RUNANIM (host commands) */
    2100, /* SS_OP_RUNENERGY (host commands) */
    2101, /* SS_OP_SAY (host commands) */
    2102, /* SS_OP_SESSION_LOG (host commands) */
    2103, /* SS_OP_SET_PLAYER_OP (host commands) */
    2108, /* SS_OP_SETTIMER (host commands) */
    2109, /* SS_OP_SOFTTIMER (host commands) */
    2110, /* SS_OP_SOUND_SYNTH (host commands) */
    2111, /* SS_OP_SPOTANIM_PL (host commands) */
    2113, /* SS_OP_STAT_ADD (host commands) */
    2114, /* SS_OP_STAT_ADVANCE (host commands) */
    2115, /* SS_OP_STAT_BASE (host commands) */
    2116, /* SS_OP_STAT_BOOST (host commands) */
    2117, /* SS_OP_STAT_DRAIN (host commands) */
    2118, /* SS_OP_STAT_HEAL (host commands) */
    2119, /* SS_OP_STAT_RANDOM (host commands) */
    2120, /* SS_OP_STAT_SUB (host commands) */
    2121, /* SS_OP_STAT_TOTAL (host commands) */
    2122, /* SS_OP_STAT (host commands) */
    2123, /* SS_OP_STRONGQUEUE (host commands) */
    2124, /* SS_OP_STRONGQUEUEVARARG (host commands) */
    2125, /* SS_OP_TURNANIM (host commands) */
    2129, /* SS_OP_UID (host commands) */
    2130, /* SS_OP_WALKANIM_B (host commands) */
    2131, /* SS_OP_WALKANIM_L (host commands) */
    2132, /* SS_OP_WALKANIM_R (host commands) */
    2133, /* SS_OP_WALKANIM (host commands) */
    2134, /* SS_OP_WALKTRIGGER (host commands) */
    2135, /* SS_OP_WEAKQUEUE (host commands) */
    2136, /* SS_OP_WEAKQUEUEVARARG (host commands) */
    2500, /* SS_OP_NPC_ADD (host commands) */
    2501, /* SS_OP_NPC_ANIM (host commands) */
    2502, /* SS_OP_NPC_ARRIVEDELAY (host commands) */
    2503, /* SS_OP_NPC_ATTACKRANGE (host commands) */
    2504, /* SS_OP_NPC_BASESTAT (host commands) */
    2505, /* SS_OP_NPC_CATEGORY (host commands (npc)) */
    2506, /* SS_OP_NPC_CHANGETYPE_KEEPALL (host commands) */
    2507, /* SS_OP_NPC_CHANGETYPE (host commands) */
    2508, /* SS_OP_NPC_COORD (host commands) */
    2509, /* SS_OP_NPC_DAMAGE (host commands) */
    2510, /* SS_OP_NPC_DEL (host commands) */
    2511, /* SS_OP_NPC_DELAY (host commands) */
    2512, /* SS_OP_NPC_FACESQUARE (host commands) */
    2513, /* SS_OP_NPC_FIND (host commands) */
    2514, /* SS_OP_NPC_FINDALL (host commands) */
    2515, /* SS_OP_NPC_FINDALLANY (host commands) */
    2516, /* SS_OP_NPC_FINDALLZONE (host commands (npc)) */
    2517, /* SS_OP_NPC_FINDCAT (host commands (npc)) */
    2518, /* SS_OP_NPC_FINDEXACT (host commands) */
    2519, /* SS_OP_NPC_FINDHERO (host commands) */
    2520, /* SS_OP_NPC_FINDNEXT (host commands) */
    2521, /* SS_OP_NPC_FINDUID (host commands) */
    2522, /* SS_OP_NPC_GETMODE (host commands) */
    2523, /* SS_OP_NPC_HASOP (host commands (npc)) */
    2525, /* SS_OP_NPC_HUNT (host commands (npc)) */
    2526, /* SS_OP_NPC_HUNTALL (host commands (npc)) */
    2529, /* SS_OP_NPC_NAME (host commands) */
    2530, /* SS_OP_NPC_PARAM (host commands (npc)) */
    2531, /* SS_OP_NPC_QUEUE (host commands) */
    2532, /* SS_OP_NPC_RANGE (host commands) */
    2533, /* SS_OP_NPC_SAY (host commands) */
    2535, /* SS_OP_NPC_SETHUNTMODE (host commands) */
    2536, /* SS_OP_NPC_SETMODE (host commands) */
    2537, /* SS_OP_NPC_SETTIMER (host commands) */
    2538, /* SS_OP_NPC_STAT (host commands) */
    2539, /* SS_OP_NPC_STATADD (host commands) */
    2540, /* SS_OP_NPC_STATHEAL (host commands) */
    2541, /* SS_OP_NPC_STATSUB (host commands) */
    2542, /* SS_OP_NPC_TELE (host commands) */
    2543, /* SS_OP_NPC_TYPE (host commands) */
    2544, /* SS_OP_NPC_UID (host commands) */
    2545, /* SS_OP_NPC_WALK (host commands) */
    2547, /* SS_OP_PROJANIM_NPC (host commands) */
    2548, /* SS_OP_SPOTANIM_NPC (host commands) */
    3000, /* SS_OP_LOC_ADD (host commands) */
    3001, /* SS_OP_LOC_ANGLE (host commands) */
    3002, /* SS_OP_LOC_ANIM (host commands) */
    3003, /* SS_OP_LOC_CATEGORY (host commands (loc)) */
    3004, /* SS_OP_LOC_CHANGE (host commands) */
    3005, /* SS_OP_LOC_COORD (host commands) */
    3006, /* SS_OP_LOC_DEL (host commands) */
    3007, /* SS_OP_LOC_FIND (host commands) */
    3008, /* SS_OP_LOC_FINDALLZONE (host commands) */
    3009, /* SS_OP_LOC_FINDNEXT (host commands) */
    3010, /* SS_OP_LOC_NAME (host commands (loc)) */
    3011, /* SS_OP_LOC_PARAM (host commands (loc)) */
    3012, /* SS_OP_LOC_SHAPE (host commands) */
    3013, /* SS_OP_LOC_TYPE (host commands) */
    3500, /* SS_OP_OBJ_ADD (host commands) */
    3501, /* SS_OP_OBJ_ADDALL (host commands (obj)) */
    3502, /* SS_OP_OBJ_COORD (host commands (obj)) */
    3503, /* SS_OP_OBJ_COUNT (host commands (obj)) */
    3504, /* SS_OP_OBJ_DEL (host commands (obj)) */
    3505, /* SS_OP_OBJ_FIND (host commands (obj)) */
    3510, /* SS_OP_OBJ_TAKEITEM (host commands (obj)) */
    3511, /* SS_OP_OBJ_TYPE (host commands (obj)) */
    4000, /* SS_OP_NC_CATEGORY (host commands (npc)) */
    4001, /* SS_OP_NC_DEBUGNAME (host commands) */
    4003, /* SS_OP_NC_NAME (host commands) */
    4004, /* SS_OP_NC_OP (host commands) */
    4005, /* SS_OP_NC_PARAM (host commands) */
    4006, /* SS_OP_NC_SIZE (host commands) */
    4007, /* SS_OP_NC_VISLEVEL (host commands) */
    4100, /* SS_OP_LC_CATEGORY (host commands (loc)) */
    4101, /* SS_OP_LC_DEBUGNAME (host commands (loc)) */
    4103, /* SS_OP_LC_LENGTH (host commands (loc)) */
    4104, /* SS_OP_LC_NAME (host commands (loc)) */
    4106, /* SS_OP_LC_PARAM (host commands (param)) */
    4107, /* SS_OP_LC_WIDTH (host commands (loc)) */
    4200, /* SS_OP_OC_CATEGORY (host commands) */
    4201, /* SS_OP_OC_CERT (host commands) */
    4202, /* SS_OP_OC_COST (host commands (obj)) */
    4203, /* SS_OP_OC_DEBUGNAME (host commands) */
    4204, /* SS_OP_OC_DESC (host commands) */
    4206, /* SS_OP_OC_MEMBERS (host commands (obj)) */
    4207, /* SS_OP_OC_NAME (host commands) */
    4209, /* SS_OP_OC_PARAM (host commands) */
    4210, /* SS_OP_OC_STACKABLE (host commands) */
    4211, /* SS_OP_OC_TRADEABLE (host commands (obj)) */
    4212, /* SS_OP_OC_UNCERT (host commands) */
    4213, /* SS_OP_OC_WEARPOS (host commands (obj)) */
    4214, /* SS_OP_OC_WEARPOS2 (host commands (obj)) */
    4215, /* SS_OP_OC_WEARPOS3 (host commands (obj)) */
    4302, /* SS_OP_INV_ADD (host commands) */
    4304, /* SS_OP_INV_CHANGESLOT (host commands (inv)) */
    4305, /* SS_OP_INV_CLEAR (host commands) */
    4306, /* SS_OP_INV_DEBUGNAME (host commands) */
    4307, /* SS_OP_INV_DEL (host commands) */
    4308, /* SS_OP_INV_DELSLOT (host commands) */
    4309, /* SS_OP_INV_DROPALL (host commands (inv)) */
    4310, /* SS_OP_INV_DROPITEM_DELAYED (host commands (inv)) */
    4312, /* SS_OP_INV_DROPSLOT (host commands (inv)) */
    4313, /* SS_OP_INV_FREESPACE (host commands) */
    4314, /* SS_OP_INV_GETNUM (host commands) */
    4315, /* SS_OP_INV_GETOBJ (host commands) */
    4316, /* SS_OP_INV_ITEMSPACE (host commands) */
    4317, /* SS_OP_INV_ITEMSPACE2 (host commands) */
    4318, /* SS_OP_INV_MOVEFROMSLOT (host commands (inv)) */
    4319, /* SS_OP_INV_MOVEITEM_CERT (host commands (inv)) */
    4320, /* SS_OP_INV_MOVEITEM_UNCERT (host commands (inv)) */
    4321, /* SS_OP_INV_MOVEITEM (host commands (inv)) */
    4322, /* SS_OP_INV_MOVETOSLOT (host commands) */
    4323, /* SS_OP_INV_SETSLOT (host commands) */
    4324, /* SS_OP_INV_SIZE (host commands) */
    4326, /* SS_OP_INV_STOPTRANSMIT (host commands) */
    4327, /* SS_OP_INV_TOTAL (host commands) */
    4328, /* SS_OP_INV_TOTALCAT (host commands) */
    4331, /* SS_OP_INV_TRANSMIT (host commands) */
    4400, /* SS_OP_ENUM (host commands) */
    4401, /* SS_OP_ENUM_GETOUTPUTCOUNT (host commands) */
    4500, /* SS_OP_APPEND_NUM (VM core) */
    4501, /* SS_OP_APPEND (VM core) */
    4502, /* SS_OP_APPEND_SIGNNUM (VM core) */
    4503, /* SS_OP_LOWERCASE (VM core) */
    4504, /* SS_OP_TEXT_GENDER (host commands) */
    4505, /* SS_OP_TOSTRING (VM core) */
    4506, /* SS_OP_COMPARE (VM core) */
    4507, /* SS_OP_TEXT_SWITCH (VM core) */
    4508, /* SS_OP_APPEND_CHAR (VM core) */
    4509, /* SS_OP_STRING_LENGTH (VM core) */
    4510, /* SS_OP_SUBSTRING (VM core) */
    4511, /* SS_OP_STRING_INDEXOF_CHAR (VM core) */
    4512, /* SS_OP_STRING_INDEXOF_STRING (VM core) */
    4513, /* SS_OP_SPLIT_GET (host commands) */
    4514, /* SS_OP_SPLIT_GETANIM (host commands) */
    4515, /* SS_OP_SPLIT_INIT (host commands) */
    4516, /* SS_OP_SPLIT_LINECOUNT (host commands) */
    4517, /* SS_OP_SPLIT_PAGECOUNT (host commands) */
    4600, /* SS_OP_ADD (VM core) */
    4601, /* SS_OP_SUB (VM core) */
    4602, /* SS_OP_MULTIPLY (VM core) */
    4603, /* SS_OP_DIVIDE (VM core) */
    4604, /* SS_OP_RANDOM (VM core) */
    4605, /* SS_OP_RANDOMINC (VM core) */
    4606, /* SS_OP_INTERPOLATE (VM core) */
    4607, /* SS_OP_ADDPERCENT (VM core) */
    4608, /* SS_OP_SETBIT (VM core) */
    4609, /* SS_OP_CLEARBIT (VM core) */
    4610, /* SS_OP_TESTBIT (VM core) */
    4611, /* SS_OP_MODULO (VM core) */
    4612, /* SS_OP_POW (VM core) */
    4613, /* SS_OP_INVPOW (VM core) */
    4614, /* SS_OP_AND (VM core) */
    4615, /* SS_OP_OR (VM core) */
    4616, /* SS_OP_MIN (VM core) */
    4617, /* SS_OP_MAX (VM core) */
    4618, /* SS_OP_SCALE (VM core) */
    4619, /* SS_OP_BITCOUNT (VM core) */
    4620, /* SS_OP_TOGGLEBIT (VM core) */
    4621, /* SS_OP_SETBIT_RANGE (VM core) */
    4622, /* SS_OP_CLEARBIT_RANGE (VM core) */
    4623, /* SS_OP_GETBIT_RANGE (VM core) */
    4624, /* SS_OP_SETBIT_RANGE_TOINT (VM core) */
    4628, /* SS_OP_ABS (VM core) */
    4629, /* SS_OP_DATE_MINUTES (host commands) */
    4700, /* SS_OP_STRUCT_PARAM (host commands (param)) */
    7500, /* SS_OP_DB_FIND_WITH_COUNT (host commands (db)) */
    7501, /* SS_OP_DB_FINDNEXT (host commands (db)) */
    7502, /* SS_OP_DB_GETFIELD (host commands (db)) */
    7503, /* SS_OP_DB_GETFIELDCOUNT (host commands (db)) */
    7504, /* SS_OP_DB_LISTALL_WITH_COUNT (host commands (db)) */
    7505, /* SS_OP_DB_GETROWTABLE (host commands (db)) */
    7506, /* SS_OP_DB_FINDBYINDEX (host commands (db)) */
    7508, /* SS_OP_DB_FIND (host commands (db)) */
    7510, /* SS_OP_DB_LISTALL (host commands (db)) */
    10001, /* SS_OP_ERROR (host commands) */
    11000, /* SS_OP_IF_SETEVENTS (host commands) */
    11001, /* SS_OP_IF_OPENSUB (host commands) */
    11002, /* SS_OP_RUNCLIENTSCRIPT_SS (host commands) */
    11003, /* SS_OP_RUNCLIENTSCRIPTVARARG (host commands) */
    11004, /* SS_OP_P_COUNTDIALOG_NOPROMPT (host commands) */
    11005, /* SS_OP_IF_CLOSESUB (host commands) */
    11006, /* SS_OP_IF_OPENTOP (host commands) */
    11007, /* SS_OP_IF_MOVESUB (host commands) */
    11008, /* SS_OP_IF_GETMAIN (host commands) */
    11009, /* SS_OP_MAP_INSTANCE_ALLOC (host commands) */
    11010, /* SS_OP_MAP_INSTANCE_SETCHUNK (host commands) */
    11011, /* SS_OP_MAP_INSTANCE_BUILD (host commands) */
    11012, /* SS_OP_MAP_INSTANCE_COORD (host commands) */
    11013, /* SS_OP_MAP_INSTANCE_FREE (host commands) */
    11014, /* SS_OP_MAP_INSTANCE_FIND (host commands) */
    11015, /* SS_OP_NPC_SETRESPAWN (host commands) */
    11016, /* SS_OP_INV_SETVAR (host commands) */
    11017, /* SS_OP_INV_GETVAR (host commands) */
    11018, /* SS_OP_NPC_FREEZE (host commands) */
    11019, /* SS_OP_NPC_FROZEN (host commands) */
    11020, /* SS_OP_OC_PLACEHOLDER (host commands) */
    11021, /* SS_OP_OC_UNPLACEHOLDER (host commands) */
    11022, /* SS_OP_NPC_SETOWNER (host commands) */
    11023, /* SS_OP_NPC_OWNER (host commands) */
    11024, /* SS_OP_NPC_FINDOWNED (host commands) */
    11025, /* SS_OP_NPC_VAR_GET (host commands) */
    11026, /* SS_OP_NPC_VAR_SET (host commands) */
    11027, /* SS_OP_PLAYER_LOCK (host commands) */
    11028, /* SS_OP_PLAYER_UNLOCK (host commands) */
    11029, /* SS_OP_WALKSTEP_COORD (host commands) */
    11030, /* SS_OP_NPC_FINDCOMBAT (host commands) */
    11031, /* SS_OP_NPC_FINDOWNED2 (host commands) */
    11032, /* SS_OP_OBJ_ADD_PRIVATE (host commands) */
    11033, /* SS_OP_NPC_POISON (host commands) */
    11034, /* SS_OP_NPC_ATTACKNPC (host commands) */
    11035, /* SS_OP_NPC_ATTACKPLAYER (host commands) */
    11036, /* SS_OP_NPC_HASTARGET (host commands) */
};

#endif
