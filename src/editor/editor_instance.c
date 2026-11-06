#include "editor_instance.h"

#include "mem/torirs_mem.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
Editor_InstanceNameValid(const char* name)
{
    assert(name);
    return Editor_WmaNameValid(name);
}

int
Editor_InstanceLocNameValid(const char* name)
{
    size_t length;

    assert(name);
    length = strlen(name);
    if( length == 0 || length >= EDITOR_INSTANCE_LOC_MAX )
        return 0;
    for( size_t i = 0; i < length; i++ )
    {
        char const c = name[i];
        if( !((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') )
            return 0;
    }
    return 1;
}

int
Editor_InstancePlanInit(
    struct Editor_InstancePlan* plan,
    const char* const* keys,
    int count)
{
    int x0 = 256;
    int z0 = 256;
    int x1 = -1;
    int z1 = -1;

    assert(plan);
    assert(keys);
    assert(count >= 1);
    assert(count <= EDITOR_INSTANCE_SQUARES_MAX);

    memset(plan, 0, sizeof(*plan));
    for( int i = 0; i < count; i++ )
    {
        struct Editor_InstanceSquare* square = &plan->sources[i];
        const char* slash;

        assert(keys[i]);
        /* "m50_50" or "<storage>/m50_50": the bare key is the last part. */
        slash = strrchr(keys[i], '/');
        if( strlen(keys[i]) >= EDITOR_DOC_KEY_MAX
            || !Editor_SquareKeyParse(slash ? slash + 1 : keys[i], &square->map_x, &square->map_z) )
            return 0;
        for( int j = 0; j < i; j++ )
            if( plan->sources[j].map_x == square->map_x && plan->sources[j].map_z == square->map_z )
                return 0;
        snprintf(square->key, sizeof(square->key), "%s", keys[i]);
        x0 = square->map_x < x0 ? square->map_x : x0;
        z0 = square->map_z < z0 ? square->map_z : z0;
        x1 = square->map_x > x1 ? square->map_x : x1;
        z1 = square->map_z > z1 ? square->map_z : z1;
    }
    /* South-west first (z, then x): the first square is the box's lowest,
     * whatever order the picker listed them in, so the default landing tile
     * and the creation order do not depend on the atlas's order. */
    for( int i = 1; i < count; i++ )
    {
        struct Editor_InstanceSquare const moving = plan->sources[i];
        int j = i;
        while( j > 0
               && (plan->sources[j - 1].map_z > moving.map_z
                   || (plan->sources[j - 1].map_z == moving.map_z && plan->sources[j - 1].map_x > moving.map_x)) )
        {
            plan->sources[j] = plan->sources[j - 1];
            j--;
        }
        plan->sources[j] = moving;
    }
    plan->count = count;
    plan->src_x0 = x0;
    plan->src_z0 = z0;
    plan->w = x1 - x0 + 1;
    plan->h = z1 - z0 + 1;
    plan->dst_x0 = x0;
    plan->dst_z0 = z0;
    plan->level_lo = 0;
    plan->level_hi = EDITOR_SQUARE_LEVELS - 1;
    return 1;
}

int
Editor_InstanceInSpace(
    int map_x,
    int map_z)
{
    return map_x >= EDITOR_INSTANCE_X0 && map_x <= EDITOR_INSTANCE_X1 && map_z >= EDITOR_INSTANCE_Z0
           && map_z <= EDITOR_INSTANCE_Z1;
}

/** The instance space's occupancy: [z - Z0][x - X0]. */
#define INSTANCE_SPACE_W (EDITOR_INSTANCE_X1 - EDITOR_INSTANCE_X0 + 1)
#define INSTANCE_SPACE_H (EDITOR_INSTANCE_Z1 - EDITOR_INSTANCE_Z0 + 1)

static void
instance_occupancy(
    const int* taken,
    int count,
    unsigned char grid[INSTANCE_SPACE_H][INSTANCE_SPACE_W])
{
    memset(grid, 0, (size_t)INSTANCE_SPACE_H * INSTANCE_SPACE_W);
    for( int i = 0; i < count; i++ )
    {
        int const x = taken[i * 2];
        int const z = taken[i * 2 + 1];
        if( Editor_InstanceInSpace(x, z) )
            grid[z - EDITOR_INSTANCE_Z0][x - EDITOR_INSTANCE_X0] = 1;
    }
}

int
Editor_InstanceFindFree(
    const int* taken,
    int count,
    int w,
    int h,
    int* out_x,
    int* out_z)
{
    unsigned char grid[INSTANCE_SPACE_H][INSTANCE_SPACE_W];

    assert(taken || count == 0);
    assert(w >= 1);
    assert(h >= 1);
    assert(out_x);
    assert(out_z);

    if( w > INSTANCE_SPACE_W || h > INSTANCE_SPACE_H )
        return 0;
    instance_occupancy(taken, count, grid);
    for( int z = 0; z + h <= INSTANCE_SPACE_H; z++ )
    {
        for( int x = 0; x + w <= INSTANCE_SPACE_W; x++ )
        {
            int clear = 1;
            for( int dz = 0; dz < h && clear; dz++ )
                for( int dx = 0; dx < w && clear; dx++ )
                    clear = !grid[z + dz][x + dx];
            if( clear )
            {
                *out_x = EDITOR_INSTANCE_X0 + x;
                *out_z = EDITOR_INSTANCE_Z0 + z;
                return 1;
            }
        }
    }
    return 0;
}

int
Editor_InstanceFreeCount(
    const int* taken,
    int count)
{
    unsigned char grid[INSTANCE_SPACE_H][INSTANCE_SPACE_W];
    int free_count = 0;

    assert(taken || count == 0);
    instance_occupancy(taken, count, grid);
    for( int z = 0; z < INSTANCE_SPACE_H; z++ )
        for( int x = 0; x < INSTANCE_SPACE_W; x++ )
            free_count += !grid[z][x];
    return free_count;
}

void
Editor_InstanceDest(
    const struct Editor_InstancePlan* plan,
    int index,
    int* out_map_x,
    int* out_map_z)
{
    assert(plan);
    assert(index >= 0);
    assert(index < plan->count);
    assert(out_map_x);
    assert(out_map_z);
    *out_map_x = plan->sources[index].map_x - plan->src_x0 + plan->dst_x0;
    *out_map_z = plan->sources[index].map_z - plan->src_z0 + plan->dst_z0;
}

int
Editor_InstanceSourceOfTile(
    const struct Editor_InstancePlan* plan,
    int abs_x,
    int abs_z)
{
    assert(plan);
    if( abs_x < 0 || abs_z < 0 )
        return -1;
    for( int i = 0; i < plan->count; i++ )
        if( plan->sources[i].map_x == abs_x / EDITOR_SQUARE_X && plan->sources[i].map_z == abs_z / EDITOR_SQUARE_Z )
            return i;
    return -1;
}

int
Editor_InstanceRebaseTile(
    const struct Editor_InstancePlan* plan,
    int abs_x,
    int abs_z,
    int* out_x,
    int* out_z)
{
    assert(plan);
    assert(out_x);
    assert(out_z);
    if( Editor_InstanceSourceOfTile(plan, abs_x, abs_z) < 0 )
        return 0;
    *out_x = abs_x + (plan->dst_x0 - plan->src_x0) * EDITOR_SQUARE_X;
    *out_z = abs_z + (plan->dst_z0 - plan->src_z0) * EDITOR_SQUARE_Z;
    return 1;
}

int
Editor_InstanceRebaseSpawn(
    const struct Editor_InstancePlan* plan,
    const struct Editor_SpawnRow* row,
    struct Editor_SpawnRow* out)
{
    int x;
    int z;

    assert(plan);
    assert(row);
    assert(out);
    if( row->suppress || row->level < plan->level_lo || row->level > plan->level_hi )
        return 0;
    if( !Editor_InstanceRebaseTile(plan, row->abs_x, row->abs_z, &x, &z) )
        return 0;
    *out = *row;
    out->abs_x = x;
    out->abs_z = z;
    return 1;
}

void
Editor_InstanceCoordText(
    int level,
    int abs_x,
    int abs_z,
    char out[EDITOR_INSTANCE_COORD_MAX])
{
    assert(out);
    assert(level >= 0);
    assert(level < EDITOR_SQUARE_LEVELS);
    assert(abs_x >= 0);
    assert(abs_x < 256 * EDITOR_SQUARE_X);
    assert(abs_z >= 0);
    assert(abs_z < 256 * EDITOR_SQUARE_Z);
    snprintf(out, EDITOR_INSTANCE_COORD_MAX, "%d_%d_%d_%d_%d", level, abs_x / EDITOR_SQUARE_X,
        abs_z / EDITOR_SQUARE_Z, abs_x % EDITOR_SQUARE_X, abs_z % EDITOR_SQUARE_Z);
}

/** Up to `max` non-negative integers separated by `sep` (or blanks when sep
 *  is ' '), the whole text. Returns how many. */
static int
instance_ints(
    const char* text,
    char sep,
    int* out,
    int max)
{
    const char* p = text;
    int n = 0;

    while( *p == ' ' )
        p++;
    while( *p )
    {
        char* end = NULL;
        long value;
        if( n >= max || *p < '0' || *p > '9' )
            return -1;
        value = strtol(p, &end, 10);
        if( value > 1000000 )
            return -1;
        out[n++] = (int)value;
        p = end;
        if( sep == ' ' )
        {
            if( *p && *p != ' ' && *p != ',' )
                return -1;
            while( *p == ' ' || *p == ',' )
                p++;
        }
        else if( *p == sep )
        {
            p++;
            if( !*p )
                return -1;
        }
        else if( *p )
            return -1;
    }
    return n;
}

int
Editor_InstanceCoordParse(
    const char* text,
    int* out_level,
    int* out_x,
    int* out_z)
{
    int v[5];
    int n;

    assert(text);
    assert(out_level);
    assert(out_x);
    assert(out_z);

    if( strchr(text, '_') )
    {
        n = instance_ints(text, '_', v, 5);
        if( n != 5 || v[0] > 3 || v[1] > 255 || v[2] > 255 || v[3] > 63 || v[4] > 63 )
            return 0;
        *out_level = v[0];
        *out_x = v[1] * EDITOR_SQUARE_X + v[3];
        *out_z = v[2] * EDITOR_SQUARE_Z + v[4];
        return 1;
    }
    n = instance_ints(text, ' ', v, 3);
    if( n < 2 || v[0] >= 256 * EDITOR_SQUARE_X || v[1] >= 256 * EDITOR_SQUARE_Z || (n == 3 && v[2] > 3) )
        return 0;
    *out_x = v[0];
    *out_z = v[1];
    *out_level = n == 3 ? v[2] : 0;
    return 1;
}

void
Editor_InstanceEntryTile(
    const struct Editor_InstancePlan* plan,
    int* out_level,
    int* out_x,
    int* out_z)
{
    int map_x;
    int map_z;

    assert(plan);
    assert(plan->count >= 1);
    assert(out_level);
    assert(out_x);
    assert(out_z);
    Editor_InstanceDest(plan, 0, &map_x, &map_z);
    *out_level = plan->level_lo;
    *out_x = map_x * EDITOR_SQUARE_X + EDITOR_SQUARE_X / 2;
    *out_z = map_z * EDITOR_SQUARE_Z + EDITOR_SQUARE_Z / 2;
}

enum Editor_DocTemplate
Editor_InstanceTemplate(unsigned copy)
{
    return (copy & EDITOR_INSTANCE_COPY_TERRAIN) ? EDITOR_DOC_TEMPLATE_COPY : EDITOR_DOC_TEMPLATE_EMPTY;
}

static int
instance_level_copied(
    const struct Editor_InstancePlan* plan,
    int level)
{
    return level >= plan->level_lo && level <= plan->level_hi;
}

int
Editor_InstanceCopyCommands(
    const struct Editor_InstancePlan* plan,
    unsigned copy,
    const struct Editor_Square* created,
    const struct Editor_Square* source,
    struct Editor_Cmd* out,
    int max)
{
    static const struct Editor_Tile empty;
    int count = 0;

    assert(plan);
    assert(created);
    assert(out || max == 0);

    if( copy & EDITOR_INSTANCE_COPY_TERRAIN )
    {
        /* The copy template brought every level: clear the ones not asked
         * for, tiles then locs. */
        for( int level = 0; level < EDITOR_SQUARE_LEVELS; level++ )
        {
            if( instance_level_copied(plan, level) )
                continue;
            for( int z = 0; z < EDITOR_SQUARE_Z; z++ )
                for( int x = 0; x < EDITOR_SQUARE_X; x++ )
                {
                    const struct Editor_Tile* tile = &created->tiles[Editor_TileIndex(x, z, level)];
                    if( memcmp(tile, &empty, sizeof(empty)) == 0 )
                        continue;
                    if( out && count < max )
                    {
                        struct Editor_Cmd* c = &out[count];
                        Editor_CmdInitSquare(c, EDITOR_CMD_TILE, created->map_x, created->map_z);
                        c->has_before = 1;
                        c->has_after = 1;
                        c->u.tile.level = level;
                        c->u.tile.x = x;
                        c->u.tile.z = z;
                        c->u.tile.before = *tile;
                        c->u.tile.after = empty;
                    }
                    count++;
                }
        }
        for( int i = 0; i < created->loc_count; i++ )
        {
            const struct Editor_Loc* loc = &created->locs[i];
            if( (copy & EDITOR_INSTANCE_COPY_LOCS) && instance_level_copied(plan, loc->level) )
                continue;
            if( out && count < max )
            {
                Editor_CmdInitSquare(&out[count], EDITOR_CMD_LOC, created->map_x, created->map_z);
                out[count].has_before = 1;
                out[count].u.loc.before = *loc;
            }
            count++;
        }
        return count;
    }

    /* An empty square: the locs, if asked for, added from the source. */
    if( !(copy & EDITOR_INSTANCE_COPY_LOCS) )
        return 0;
    assert(source);
    for( int i = 0; i < source->loc_count; i++ )
    {
        const struct Editor_Loc* loc = &source->locs[i];
        if( !instance_level_copied(plan, loc->level) )
            continue;
        if( out && count < max )
        {
            Editor_CmdInitSquare(&out[count], EDITOR_CMD_LOC, created->map_x, created->map_z);
            out[count].has_after = 1;
            out[count].u.loc.after = *loc;
        }
        count++;
    }
    return count;
}

void
Editor_InstanceTriggerKey(
    const char* name,
    char out[EDITOR_DOC_KEY_MAX])
{
    assert(name);
    assert(out);
    assert(Editor_InstanceNameValid(name));
    snprintf(out, EDITOR_DOC_KEY_MAX, "rs2:edited/%s/scripts/%s.rs2", name, name);
}

size_t
Editor_InstanceTriggerText(
    const struct Editor_InstancePlan* plan,
    const char* name,
    const char* loc,
    enum Editor_InstanceEntry entry,
    int level,
    int x,
    int z,
    char* out,
    size_t out_capacity)
{
    char coord[EDITOR_INSTANCE_COORD_MAX];
    char base[EDITOR_INSTANCE_COORD_MAX];
    int written;

    assert(plan);
    assert(name);
    assert(loc);
    assert(out || out_capacity == 0);
    assert(Editor_InstanceNameValid(name));
    assert(Editor_InstanceLocNameValid(loc));
    assert(entry == EDITOR_INSTANCE_ENTRY_TELEPORT || entry == EDITOR_INSTANCE_ENTRY_RUNTIME);

    Editor_InstanceCoordText(level, x, z, coord);
    if( entry == EDITOR_INSTANCE_ENTRY_TELEPORT )
    {
        written = snprintf(out, out_capacity,
            "// server/scripts/edited/%s/scripts/%s.rs2: written by the ToriRSEd instance helper\n"
            "// (src/editor/editor_instance.c) for the instance %s. Make it again from the helper.\n"
            "\n"
            "[oploc1,%s]\n"
            "p_telejump(%s);\n",
            name, name, name, loc, coord);
        assert(written > 0);
        return (size_t)written;
    }

    /* The runtime copy: the destination box, zone by zone (8 tiles a zone,
     * 8 zones a square), each level the plan copies, unturned; then the
     * entry tile's offset inside it. map_instance_alloc takes at most 16 x
     * 16 zones (docs/map_instances.md section 3). */
    assert(plan->w <= 2);
    assert(plan->h <= 2);
    Editor_InstanceCoordText(0, plan->dst_x0 * EDITOR_SQUARE_X, plan->dst_z0 * EDITOR_SQUARE_Z, base);
    written = snprintf(out, out_capacity,
        "// server/scripts/edited/%s/scripts/%s.rs2: written by the ToriRSEd instance helper\n"
        "// (src/editor/editor_instance.c) for the instance %s, as a runtime map instance\n"
        "// (docs/map_instances.md). Make it again from the helper.\n"
        "\n"
        "[oploc1,%s]\n"
        "def_int $handle = map_instance_alloc(%d, %d);\n"
        "def_int $level = %d;\n"
        "def_int $zx = 0;\n"
        "def_int $zz = 0;\n"
        "if ($handle = 0) {\n"
        "    mes(\"There is no room for another instance just now.\");\n"
        "    return;\n"
        "}\n"
        "while ($level <= %d) {\n"
        "    $zx = 0;\n"
        "    while ($zx < %d) {\n"
        "        $zz = 0;\n"
        "        while ($zz < %d) {\n"
        "            map_instance_setchunk($handle, $level, $zx, $zz, movecoord(%s, calc($zx * 8), $level, calc($zz * 8)), 0);\n"
        "            $zz = calc($zz + 1);\n"
        "        }\n"
        "        $zx = calc($zx + 1);\n"
        "    }\n"
        "    $level = calc($level + 1);\n"
        "}\n"
        "map_instance_build($handle);\n"
        "p_telejump(map_instance_coord($handle, %d, %d, %d));\n",
        name, name, name, loc, plan->w * 8, plan->h * 8, plan->level_lo, plan->level_hi, plan->w * 8, plan->h * 8,
        base, x - plan->dst_x0 * EDITOR_SQUARE_X, z - plan->dst_z0 * EDITOR_SQUARE_Z, level);
    assert(written > 0);
    return (size_t)written;
}
