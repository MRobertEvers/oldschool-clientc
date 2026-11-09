#include "editor_npc.h"

#include "editor_cfg.h"
#include "editor_cmd.h"
#include "map_editor_ghost.h"

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* const k_facing_words[8] = {
    "northwest", "north", "northeast", "west", "east", "southwest", "south", "southeast",
};

static const char* const k_restrict_words[7] = {
    "normal", "blocked", "blocked+normal", "indoors", "outdoors", "nomove", "passthru",
};

/** A whole non-negative decimal; -1 otherwise. */
static int
npc_int(const char* text)
{
    char* end = NULL;
    long value;

    while( *text == ' ' || *text == '\t' )
        text++;
    if( !isdigit((unsigned char)text[0]) )
        return -1;
    value = strtol(text, &end, 10);
    while( end && (*end == ' ' || *end == '\t') )
        end++;
    if( !end || *end != '\0' || value < 0 || value > 1000000 )
        return -1;
    return (int)value;
}

static int
npc_bool(const char* text)
{
    while( *text == ' ' || *text == '\t' )
        text++;
    return strcmp(text, "yes") == 0 || strcmp(text, "true") == 0 || strcmp(text, "1") == 0;
}

void
Editor_NpcMovementInit(struct Editor_NpcMovement* movement)
{
    assert(movement);
    memset(movement, 0, sizeof(*movement));
    movement->defaultmode_stated = 0;
    movement->defaultmode = EDITOR_NPC_MODE_NONE;
    movement->wanderrange = -1;
    movement->maxrange = -1;
    movement->huntrange = -1;
    movement->nomove = 0;
    movement->moverestrict = EDITOR_NPC_RESTRICT_UNSTATED;
    movement->facing = -1;
    movement->size = 1;
    movement->respawnrate = -1;
    movement->timer = -1;
    movement->blockwalk = -1;
    movement->patrol_count = 0;
    movement->patrol_dropped = 0;
}

int
Editor_NpcMovementRead(
    struct Editor_NpcMovement* movement,
    const char* key,
    const char* value)
{
    assert(movement);
    assert(key);
    assert(value);
    if( strcmp(key, "wanderrange") == 0 )
    {
        movement->wanderrange = npc_int(value);
        return 1;
    }
    if( strcmp(key, "maxrange") == 0 )
    {
        movement->maxrange = npc_int(value);
        return 1;
    }
    if( strcmp(key, "huntrange") == 0 )
    {
        movement->huntrange = npc_int(value);
        return 1;
    }
    if( strcmp(key, "param") == 0 )
    {
        if( strncmp(value, "huntrange,", 10) == 0 )
        {
            movement->huntrange = npc_int(value + 10);
            return 1;
        }
        return 0;
    }
    if( strcmp(key, "respawnrate") == 0 )
    {
        movement->respawnrate = npc_int(value);
        return 1;
    }
    if( strcmp(key, "timer") == 0 )
    {
        movement->timer = npc_int(value);
        return 1;
    }
    if( strcmp(key, "blockwalk") == 0 )
    {
        static const char* const words[4] = { "none", "npc", "all", "player" };
        const char* word = value;
        while( *word == ' ' || *word == '\t' )
            word++;
        movement->blockwalk = -1;
        for( int i = 0; i < 4; i++ )
            if( strcmp(word, words[i]) == 0 || (i == 1 && strcmp(word, "NPC") == 0) )
                movement->blockwalk = i;
        if( movement->blockwalk < 0 )
        {
            int const n = npc_int(value);
            movement->blockwalk = n >= 0 && n <= 3 ? n : -1;
        }
        return 1;
    }
    if( strcmp(key, "size") == 0 )
    {
        int const size = npc_int(value);
        movement->size = size >= 1 ? size : 1;
        return 1;
    }
    if( strcmp(key, "nomove") == 0 )
    {
        movement->nomove = npc_bool(value);
        return 1;
    }
    if( strcmp(key, "defaultmode") == 0 )
    {
        const char* word = value;
        while( *word == ' ' || *word == '\t' )
            word++;
        movement->defaultmode_stated = 1;
        if( strcmp(word, "wander") == 0 || strcmp(word, "1") == 0 )
            movement->defaultmode = EDITOR_NPC_MODE_WANDER;
        else if( strcmp(word, "patrol") == 0 || strcmp(word, "2") == 0 )
            movement->defaultmode = EDITOR_NPC_MODE_PATROL;
        else
            movement->defaultmode = EDITOR_NPC_MODE_NONE;
        return 1;
    }
    if( strcmp(key, "moverestrict") == 0 )
    {
        int const parsed = Editor_NpcRestrictParse(value);
        movement->moverestrict = parsed >= 0 ? (enum Editor_NpcRestrict)parsed : EDITOR_NPC_RESTRICT_UNSTATED;
        /* The text loader sets both from `moverestrict=nomove`. */
        if( parsed == EDITOR_NPC_RESTRICT_NOMOVE )
            movement->nomove = 1;
        return 1;
    }
    if( strcmp(key, "facing") == 0 )
    {
        movement->facing = Editor_NpcFacingParse(value);
        if( movement->facing < 0 )
        {
            int const n = npc_int(value);
            movement->facing = n >= 0 && n <= 7 ? n : -1;
        }
        return 1;
    }
    if( strncmp(key, "patrol", 6) == 0 && isdigit((unsigned char)key[6]) )
    {
        int const index = npc_int(key + 6);
        struct Editor_NpcWaypoint waypoint;
        if( index < 1 )
            return 1;
        if( !Editor_NpcWaypointParse(value, &waypoint) )
            return 1;
        if( index > EDITOR_NPC_PATROL_MAX )
        {
            movement->patrol_dropped++;
            return 1;
        }
        /* Rows may arrive in any order: place by index, pad the gap. */
        while( movement->patrol_count < index )
        {
            memset(&movement->patrol[movement->patrol_count], 0, sizeof(movement->patrol[0]));
            movement->patrol[movement->patrol_count].level = -1;
            movement->patrol_count++;
        }
        movement->patrol[index - 1] = waypoint;
        return 1;
    }
    return 0;
}

int
Editor_NpcLegSteps(
    const struct Editor_NpcWaypoint* a,
    const struct Editor_NpcWaypoint* b)
{
    int dx;
    int dz;

    assert(a);
    assert(b);
    if( a->level != b->level )
        return 0;
    dx = abs(a->x - b->x);
    dz = abs(a->z - b->z);
    return dx > dz ? dx : dz;
}

void
Editor_NpcEffectiveMode(
    const struct Editor_NpcMovement* movement,
    struct Editor_NpcEffective* out)
{
    int const wanderrange = movement->wanderrange >= 0 ? movement->wanderrange : EDITOR_NPC_WANDER_DEFAULT;
    int real_waypoints = 0;

    assert(movement);
    assert(out);
    memset(out, 0, sizeof(*out));
    for( int i = 0; i < movement->patrol_count; i++ )
        real_waypoints += movement->patrol[i].level >= 0;
    out->nomove = movement->nomove || movement->moverestrict == EDITOR_NPC_RESTRICT_NOMOVE;
    out->leash = movement->maxrange >= 0 ? movement->maxrange : EDITOR_NPC_LEASH_DEFAULT;
    if( real_waypoints > 0 )
    {
        out->mode = EDITOR_NPC_MODE_PATROL;
        out->why = "patrol rows";
        out->route_contradicts_mode = movement->defaultmode_stated && movement->defaultmode != EDITOR_NPC_MODE_PATROL;
        for( int i = 0; i < movement->patrol_count; i++ )
        {
            const struct Editor_NpcWaypoint* a = &movement->patrol[i];
            const struct Editor_NpcWaypoint* b = &movement->patrol[(i + 1) % movement->patrol_count];
            if( a->level < 0 || b->level < 0 )
                continue;
            out->lap_steps += Editor_NpcLegSteps(a, b);
            out->lap_pause += a->pause > 0 ? a->pause : 0;
        }
    }
    else if( movement->defaultmode_stated )
    {
        /* A stated patrol with no rows: the server's mode is PATROL with
         * nothing to walk; it stands. Say so as NONE. */
        out->mode = movement->defaultmode == EDITOR_NPC_MODE_WANDER ? EDITOR_NPC_MODE_WANDER : EDITOR_NPC_MODE_NONE;
        out->why = "defaultmode";
    }
    else if( wanderrange > 0 )
    {
        out->mode = EDITOR_NPC_MODE_WANDER;
        out->why = "wanderrange";
    }
    else
    {
        out->mode = EDITOR_NPC_MODE_NONE;
        out->why = "no wanderrange";
    }
    if( out->mode == EDITOR_NPC_MODE_WANDER && !out->nomove )
        out->wander = wanderrange;
    else
        out->wander = 0;
    out->unused_wanderrange = movement->wanderrange > 0 && out->wander == 0;
}

int
Editor_NpcEffectiveSentence(
    const struct Editor_NpcMovement* movement,
    const struct Editor_NpcEffective* effective,
    char* out,
    size_t out_size)
{
    int n = 0;
    const char* restrict_word;

    assert(movement);
    assert(effective);
    assert(out || out_size == 0);
    if( effective->mode == EDITOR_NPC_MODE_PATROL )
    {
        int waypoints = 0;
        for( int i = 0; i < movement->patrol_count; i++ )
            waypoints += movement->patrol[i].level >= 0;
        n = snprintf(out, out_size, "patrols %d waypoint%s, %d ticks a lap", waypoints, waypoints == 1 ? "" : "s",
            effective->lap_steps + effective->lap_pause);
    }
    else if( effective->nomove )
        n = snprintf(out, out_size, "stands still (nomove)");
    else if( effective->mode == EDITOR_NPC_MODE_WANDER )
        n = snprintf(out, out_size, "wanders %d tile%s from its spawn", effective->wander,
            effective->wander == 1 ? "" : "s");
    else
        n = snprintf(out, out_size, "stays on its spawn tile (%s)", effective->why);
    if( n < 0 )
        return n;
    if( effective->mode != EDITOR_NPC_MODE_PATROL || effective->nomove )
    {
        size_t const at = (size_t)n < out_size ? (size_t)n : out_size;
        n += snprintf(out + at, out_size - at, "; leash %d", effective->leash);
    }
    restrict_word = Editor_NpcRestrictWord(movement->moverestrict);
    if( restrict_word && movement->moverestrict != EDITOR_NPC_RESTRICT_NOMOVE
        && movement->moverestrict != EDITOR_NPC_RESTRICT_NORMAL )
    {
        size_t const at = (size_t)n < out_size ? (size_t)n : out_size;
        n += snprintf(out + at, out_size - at, "; %s", restrict_word);
    }
    if( effective->unused_wanderrange )
    {
        size_t const at = (size_t)n < out_size ? (size_t)n : out_size;
        n += snprintf(out + at, out_size - at, "; wanderrange %d is unused", movement->wanderrange);
    }
    if( effective->route_contradicts_mode )
    {
        size_t const at = (size_t)n < out_size ? (size_t)n : out_size;
        n += snprintf(out + at, out_size - at, "; defaultmode=%s is overridden by the route",
            Editor_NpcModeWord(movement->defaultmode));
    }
    return n;
}

int
Editor_NpcCoordParse(
    const char* text,
    int* out_x,
    int* out_z,
    int* out_level)
{
    int parts[5];
    int n = 0;
    const char* at;

    assert(text);
    assert(out_x);
    assert(out_z);
    assert(out_level);
    at = text;
    while( *at == ' ' || *at == '\t' )
        at++;
    for( ;; )
    {
        char* end = NULL;
        long value;
        if( !isdigit((unsigned char)*at) )
            return 0;
        value = strtol(at, &end, 10);
        if( n >= 5 || value < 0 )
            return 0;
        parts[n++] = (int)value;
        at = end;
        if( *at == '_' )
        {
            at++;
            continue;
        }
        break;
    }
    if( n != 5 || parts[0] > 3 || parts[1] > 255 || parts[2] > 255 || parts[3] > 63 || parts[4] > 63 )
        return 0;
    if( *at != '\0' && *at != ',' && *at != ' ' && *at != '\t' )
        return 0;
    *out_level = parts[0];
    *out_x = parts[1] * 64 + parts[3];
    *out_z = parts[2] * 64 + parts[4];
    return 1;
}

int
Editor_NpcCoordFormat(
    int x,
    int z,
    int level,
    char* out,
    size_t out_size)
{
    assert(out || out_size == 0);
    assert(x >= 0);
    assert(z >= 0);
    assert(level >= 0);
    return snprintf(out, out_size, "%d_%d_%d_%d_%d", level, x / 64, z / 64, x % 64, z % 64);
}

int
Editor_NpcWaypointParse(
    const char* text,
    struct Editor_NpcWaypoint* out)
{
    const char* comma;
    int x;
    int z;
    int level;

    assert(text);
    assert(out);
    if( !Editor_NpcCoordParse(text, &x, &z, &level) )
        return 0;
    memset(out, 0, sizeof(*out));
    out->x = x;
    out->z = z;
    out->level = level;
    comma = strchr(text, ',');
    if( comma )
    {
        int const pause = npc_int(comma + 1);
        out->pause = pause > 0 ? pause : 0;
    }
    return 1;
}

int
Editor_NpcWaypointFormat(
    const struct Editor_NpcWaypoint* waypoint,
    char* out,
    size_t out_size)
{
    char coord[48];

    assert(waypoint);
    assert(out || out_size == 0);
    Editor_NpcCoordFormat(waypoint->x, waypoint->z, waypoint->level, coord, sizeof(coord));
    return snprintf(out, out_size, "%s,%d", coord, waypoint->pause > 0 ? waypoint->pause : 0);
}

const char*
Editor_NpcFacingWord(int facing)
{
    return facing >= 0 && facing < 8 ? k_facing_words[facing] : NULL;
}

int
Editor_NpcFacingParse(const char* word)
{
    assert(word);
    while( *word == ' ' || *word == '\t' )
        word++;
    for( int i = 0; i < 8; i++ )
        if( strcmp(word, k_facing_words[i]) == 0 )
            return i;
    return -1;
}

const char*
Editor_NpcBlockwalkWord(int blockwalk)
{
    static const char* const words[4] = { "none", "npc", "all", "player" };
    return blockwalk >= 0 && blockwalk < 4 ? words[blockwalk] : NULL;
}

const char*
Editor_NpcModeWord(enum Editor_NpcMode mode)
{
    return mode == EDITOR_NPC_MODE_WANDER ? "wander" : mode == EDITOR_NPC_MODE_PATROL ? "patrol" : "none";
}

const char*
Editor_NpcRestrictWord(enum Editor_NpcRestrict restrict_mode)
{
    return restrict_mode >= 0 && restrict_mode < 7 ? k_restrict_words[restrict_mode] : NULL;
}

int
Editor_NpcRestrictParse(const char* word)
{
    assert(word);
    while( *word == ' ' || *word == '\t' )
        word++;
    for( int i = 0; i < 7; i++ )
        if( strcmp(word, k_restrict_words[i]) == 0 )
            return i;
    /* The register's alias, and the engine's numbers. */
    if( strcmp(word, "los") == 0 )
        return EDITOR_NPC_RESTRICT_BLOCKED_NORMAL;
    {
        int const n = npc_int(word);
        return n >= 0 && n < 7 ? n : -1;
    }
}

int
Editor_NpcRouteRects(
    const struct Editor_NpcWaypoint* waypoints,
    int count,
    struct MapEditorWanderRect* out,
    int max)
{
    int n = 0;

    assert(waypoints || count == 0);
    assert(out || max == 0);
    for( int i = 0; i < count && n < max; i++ )
    {
        const struct Editor_NpcWaypoint* a = &waypoints[i];
        if( a->level < 0 )
            continue;
        memset(&out[n], 0, sizeof(out[n]));
        out[n].kind = MAP_EDITOR_WANDER_WAYPOINT;
        out[n].x0 = out[n].x1 = a->x;
        out[n].z0 = out[n].z1 = a->z;
        out[n].level = a->level;
        out[n].index = i + 1;
        n++;
    }
    if( count < 2 )
        return n;
    for( int i = 0; i < count && n < max; i++ )
    {
        const struct Editor_NpcWaypoint* a = &waypoints[i];
        const struct Editor_NpcWaypoint* b = &waypoints[(i + 1) % count];
        if( a->level < 0 || b->level < 0 )
            continue;
        memset(&out[n], 0, sizeof(out[n]));
        out[n].kind = MAP_EDITOR_WANDER_LEG;
        out[n].x0 = a->x;
        out[n].z0 = a->z;
        out[n].x1 = b->x;
        out[n].z1 = b->z;
        out[n].level = a->level;
        out[n].index = i + 1;
        out[n].flags = (i + 1 == count ? MAP_EDITOR_WANDER_FLAG_CLOSING : 0)
                       | (a->level != b->level ? MAP_EDITOR_WANDER_FLAG_TELEPORT : 0);
        n++;
    }
    return n;
}

int
Editor_NpcPreviewRects(
    const struct Editor_NpcMovement* movement,
    int x,
    int z,
    int level,
    struct MapEditorWanderRect* out,
    int max)
{
    struct Editor_NpcEffective effective;
    int n = 0;

    assert(movement);
    assert(out || max == 0);
    Editor_NpcEffectiveMode(movement, &effective);
    if( effective.mode == EDITOR_NPC_MODE_PATROL )
        return Editor_NpcRouteRects(movement->patrol, movement->patrol_count, out, max);
    if( effective.nomove )
    {
        if( n < max )
        {
            memset(&out[n], 0, sizeof(out[n]));
            out[n].kind = MAP_EDITOR_WANDER_PIN;
            out[n].x0 = x;
            out[n].z0 = z;
            out[n].x1 = x + movement->size - 1;
            out[n].z1 = z + movement->size - 1;
            out[n].level = level;
            n++;
        }
        return n;
    }
    /* The wander square (0 for a stander: none drawn) and the leash. */
    n = MapEditorWander_Rects(x, z, level, movement->size, effective.wander, effective.leash, out, max);
    for( int i = 0; i < n; i++ )
    {
        out[i].index = 0;
        out[i].flags = 0;
    }
    return n;
}

/* ---- the route as commands ------------------------------------------------- */

int
Editor_NpcRoutePlan(
    const struct Editor_CfgRecord* record,
    const struct Editor_NpcWaypoint* waypoints,
    int count,
    enum Editor_NpcMode mode,
    int layer,
    struct Editor_Cmd* out,
    int max)
{
    int n = 0;
    int stated = 0;
    char key[EDITOR_CMD_NAME_MAX];
    char value[EDITOR_CMD_VALUE_MAX];

    assert(record);
    assert(waypoints || count == 0);
    assert(out || max == 0);
    assert(count >= 0);
    if( count > EDITOR_NPC_PATROL_MAX || layer < 0 || layer >= record->layer_count )
        return -1;
    /* The rows the record states now, whichever layer: the ones past the
     * new count go, from every layer that states them. */
    for( int r = 0; r < record->row_count; r++ )
        if( strncmp(record->rows[r].key, "patrol", 6) == 0 && isdigit((unsigned char)record->rows[r].key[6])
            && record->rows[r].layer >= 0 )
        {
            int const index = atoi(record->rows[r].key + 6);
            if( index > stated )
                stated = index;
        }
    for( int i = 0; i < count; i++ )
    {
        int written;
        snprintf(key, sizeof(key), "patrol%d", i + 1);
        Editor_NpcWaypointFormat(&waypoints[i], value, sizeof(value));
        written = Editor_CfgRecordFieldCommands(record, key, "", 0, value, layer, out + n, max - n);
        if( written < 0 )
            return -1;
        n += written;
    }
    for( int i = count; i < stated; i++ )
    {
        snprintf(key, sizeof(key), "patrol%d", i + 1);
        /* Removed from every layer stating it, so no lower file shows a
         * tail of the old route through. */
        for( int l = record->layer_count - 1; l >= 0; l-- )
        {
            int row;
            int written;
            if( record->layers[l].rank == 1 )
                continue; /* a generated file is never written */
            row = Editor_CfgRecordFindRow(record, key, "", 0);
            if( row < 0 || Editor_CfgRecordLayerStates(record, l, row, NULL, 0) < 0 )
                continue;
            written = Editor_CfgRecordFieldCommands(record, key, "", 0, "", l, out + n, max - n);
            if( written < 0 )
                return -1;
            n += written;
        }
    }
    {
        const char* word = count > 0 ? "patrol" : Editor_NpcModeWord(mode);
        int written = Editor_CfgRecordFieldCommands(record, "defaultmode", "", 0, word, layer, out + n, max - n);
        if( written < 0 )
            return -1;
        n += written;
    }
    return n;
}

/* ---- the ledger ------------------------------------------------------------- */

void
Editor_NpcMoveDocKey(
    char out[128],
    const char* name)
{
    char shard;

    assert(out);
    assert(name);
    assert(name[0]);
    shard = isalnum((unsigned char)name[0]) ? (char)tolower((unsigned char)name[0]) : '_';
    snprintf(out, 128, "move:%c/%s", shard, name);
}

/** One line of a ledger: [start, end) without its ending; the key span
 *  when it is a `key = value` line. */
static int
move_line_key(
    const char* text,
    size_t start,
    size_t end,
    size_t* out_key_start,
    size_t* out_key_end,
    size_t* out_value_start)
{
    size_t at = start;
    size_t eq;

    while( at < end && (text[at] == ' ' || text[at] == '\t') )
        at++;
    if( at >= end || text[at] == '/' )
        return 0;
    eq = at;
    while( eq < end && text[eq] != '=' )
        eq++;
    if( eq >= end )
        return 0;
    /* A "//" before the '=' is a comment, not a line. */
    for( size_t i = at; i + 1 < eq; i++ )
        if( text[i] == '/' && text[i + 1] == '/' )
            return 0;
    *out_key_start = at;
    *out_key_end = eq;
    while( *out_key_end > at && (text[*out_key_end - 1] == ' ' || text[*out_key_end - 1] == '\t') )
        (*out_key_end)--;
    *out_value_start = eq + 1;
    return 1;
}

static int
move_word_ok(const char* word)
{
    if( !word || !word[0] )
        return 0;
    for( const char* at = word; *at; at++ )
        if( !isalnum((unsigned char)*at) && *at != '_' && *at != '+' && *at != '-' )
            return 0;
    return 1;
}

/** Append to a snprintf-contract buffer. */
static void
move_put(
    char* out,
    size_t capacity,
    size_t* at,
    const char* text,
    size_t length)
{
    if( out && *at < capacity )
    {
        size_t const room = capacity - *at - 1;
        size_t const n = length < room ? length : room;
        memcpy(out + *at, text, n);
        out[*at + n] = '\0';
    }
    *at += length;
}

int
Editor_NpcMoveTextSet(
    const char* old_text,
    size_t old_length,
    const char* npc,
    const char* movement,
    const char* restrict_word,
    char* out,
    size_t out_capacity,
    size_t* out_length)
{
    /* The three lines to state, and whether each has been placed. */
    const char* keys[3] = { "source", "movement", "restrict" };
    const char* values[3] = { "authored", movement, restrict_word };
    int placed[3] = { 0, 0, restrict_word ? 0 : 1 };
    size_t at = 0;
    size_t last_field_end = 0;
    size_t pos = 0;

    assert(old_text || old_length == 0);
    assert(npc);
    assert(movement);
    assert(out || out_capacity == 0);
    assert(out_length);
    if( !move_word_ok(movement) || (restrict_word && !move_word_ok(restrict_word)) )
        return 0;
    if( old_length == 0 )
    {
        char header[512];
        int const n = snprintf(header, sizeof(header),
            "// %s -- movement authored in ToriRSMapEd's NPC editor (Movement tab).\n"
            "// `source = authored`: tools/gen_npc_movement.py reads this file back and\n"
            "// leaves it exactly as it stands.\n"
            "\n"
            "source       = authored\n"
            "movement     = %s\n",
            npc, movement);
        assert(n > 0 && (size_t)n < sizeof(header));
        move_put(out, out_capacity, &at, header, (size_t)n);
        if( restrict_word )
        {
            char line[128];
            int const m = snprintf(line, sizeof(line), "restrict     = %s\n", restrict_word);
            move_put(out, out_capacity, &at, line, (size_t)m);
        }
        *out_length = at;
        return 1;
    }
    /* Walk the lines: a stated key's value is replaced in place (the bytes
     * between '=' and the note or the line's end), everything else copied. */
    while( pos < old_length )
    {
        size_t end = pos;
        size_t eol;
        size_t key_start;
        size_t key_end;
        size_t value_start;
        while( end < old_length && old_text[end] != '\n' )
            end++;
        eol = end < old_length ? end + 1 : end;
        if( move_line_key(old_text, pos, end, &key_start, &key_end, &value_start) )
        {
            int which = -1;
            for( int k = 0; k < 3; k++ )
                if( key_end - key_start == strlen(keys[k]) && strncmp(old_text + key_start, keys[k], key_end - key_start) == 0 )
                    which = k;
            if( which >= 0 && !placed[which] && values[which] )
            {
                /* key, " = ", value, then the old line's note (from "//")
                 * at the column it stood in when the new value allows. */
                size_t note = value_start;
                size_t value_end;
                size_t v = value_start;
                while( note + 1 < end && !(old_text[note] == '/' && old_text[note + 1] == '/') )
                    note++;
                if( note + 1 >= end )
                    note = end;
                value_end = note;
                while( value_end > value_start && (old_text[value_end - 1] == ' ' || old_text[value_end - 1] == '\t') )
                    value_end--;
                while( v < value_end && (old_text[v] == ' ' || old_text[v] == '\t') )
                    v++;
                move_put(out, out_capacity, &at, old_text + pos, v - pos);
                move_put(out, out_capacity, &at, values[which], strlen(values[which]));
                if( note < end )
                {
                    /* Pad to the note's old column, at least one space. */
                    size_t const old_width = value_end - v;
                    size_t const new_width = strlen(values[which]);
                    size_t pad = note - value_end;
                    if( new_width < old_width )
                        pad += old_width - new_width;
                    else if( new_width > old_width && pad > new_width - old_width )
                        pad -= new_width - old_width;
                    else if( new_width > old_width )
                        pad = 1;
                    if( pad < 1 )
                        pad = 1;
                    for( size_t i = 0; i < pad; i++ )
                        move_put(out, out_capacity, &at, " ", 1);
                    move_put(out, out_capacity, &at, old_text + note, eol - note);
                }
                else
                    move_put(out, out_capacity, &at, old_text + end, eol - end);
                placed[which] = 1;
            }
            else
                move_put(out, out_capacity, &at, old_text + pos, eol - pos);
            last_field_end = at;
        }
        else
            move_put(out, out_capacity, &at, old_text + pos, eol - pos);
        pos = eol;
    }
    /* Missing lines after the last field line (the file's own column). */
    for( int k = 0; k < 3; k++ )
    {
        char line[160];
        int n;
        if( placed[k] )
            continue;
        n = snprintf(line, sizeof(line), "%-12s = %s\n", keys[k], values[k]);
        if( last_field_end == 0 )
        {
            /* No field line at all: at the end, after a blank line. */
            if( at > 0 && out && at - 1 < out_capacity && out[at - 1] != '\n' )
                move_put(out, out_capacity, &at, "\n", 1);
            move_put(out, out_capacity, &at, line, (size_t)n);
        }
        else
        {
            /* Insert at last_field_end: shift the tail. */
            size_t const tail = at - last_field_end;
            if( out && at + (size_t)n < out_capacity )
            {
                memmove(out + last_field_end + n, out + last_field_end, tail + 1);
                memcpy(out + last_field_end, line, (size_t)n);
            }
            at += (size_t)n;
            last_field_end += (size_t)n;
        }
        placed[k] = 1;
    }
    *out_length = at;
    return 1;
}
