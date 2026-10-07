#include "torirs_server_servercodec.h"

#include "torirs_server.h"
#include "torirs_server_shop.h"
#include "torirs_server_db.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * The server's half of the band: field name -> struct member.
 *
 * No opcode and no width appears here. Both are the register's
 * (`fields/<type>.ini`, `server = opcode:<n>:<wire>`), read by the one parser
 * cachepack also uses; a number written here as well would be a second copy that
 * can drift from the one the packer writes with, and the two disagreeing is a
 * value landing in the wrong field with no error. ToriRSServer_ServerCheck holds
 * this table to the register at load, both ways.
 *
 * `sizeof` the member, not the wire width: RSCache_BandBindingCheck refuses a
 * member narrower than its field's width, which is the check that a u4 id with a
 * stated -1 (`death_drop` dropping nothing) is not truncated into a different
 * valid id.
 */
#define BIND(type, member)                                                                         \
    {                                                                                              \
        #member, offsetof(type, member), sizeof(((type*)0)->member), NULL                          \
    }

/*
 * A field whose statement means more than its value. The member is still named
 * (offset and size), so a reader of the table sees where the value lands; the
 * `apply` writes it and whatever else the statement implies.
 */
#define BIND_APPLY(type, field, member, fn)                                                        \
    {                                                                                              \
        field, offsetof(type, member), sizeof(((type*)0)->member), fn                              \
    }

static int32_t
stated_int(
    const struct RSCache_BandRecord* record,
    int index)
{
    assert(record);
    assert(index >= 0);
    assert(index < RSCACHE_REGISTER_MAX);
    return record->values[index];
}

/* `hitpoints=` is the statement "this npc is meant to be fought": the validator
 * reads `authored_combat`, and a speaking npc inheriting the engine's 10 does
 * not set it. */
static void
apply_npc_hitpoints(
    void* object,
    const struct RSCache_BandRecord* record,
    int index)
{
    struct ToriRSServerNpcDef* def = (struct ToriRSServerNpcDef*)object;

    def->hitpoints = stated_int(record, index);
    def->authored_combat = 1;
}

/* `moverestrict=nomove` pins the mover through the collapsed `nomove`, which is
 * what it reads; stating any moverestrict restates nomove with it. */
static void
apply_npc_moverestrict(
    void* object,
    const struct RSCache_BandRecord* record,
    int index)
{
    struct ToriRSServerNpcDef* def = (struct ToriRSServerNpcDef*)object;

    def->moverestrict = stated_int(record, index);
    def->nomove = def->moverestrict == 5;
}

/* Presence is the statement: `defaultmode=none` is 0, the same number as an npc
 * that said nothing, and only `defaultmode_stated` tells them apart. */
static void
apply_npc_defaultmode(
    void* object,
    const struct RSCache_BandRecord* record,
    int index)
{
    struct ToriRSServerNpcDef* def = (struct ToriRSServerNpcDef*)object;

    def->defaultmode = stated_int(record, index);
    def->defaultmode_stated = 1;
}

/*
 * `patrol<N>=<coord>,<pause>`, one tuple per waypoint in the order of N. A coord
 * is packed as everywhere else in this tree: level << 28 | x << 14 | z.
 *
 * Stated with no tuples is "no route" and clears a seeded one. A route longer
 * than TORIRSSERVER_NPC_PATROL_MAX keeps its first MAX waypoints here; the loader
 * reports the overflow as a content error (it holds the record and the register,
 * this cannot report a record by name).
 */
static void
apply_npc_patrol(
    void* object,
    const struct RSCache_BandRecord* record,
    int index)
{
    struct ToriRSServerNpcDef* def = (struct ToriRSServerNpcDef*)object;
    const struct RSCache_BandList* list;
    int count;

    assert(record);
    assert(index >= 0);
    assert(index < RSCACHE_REGISTER_MAX);
    list = record->lists[index];
    free(def->patrol);
    def->patrol = NULL;
    def->patrol_count = 0;
    if( !list || list->count == 0 )
        return;
    assert(list->arity == 2);
    count = list->count < TORIRSSERVER_NPC_PATROL_MAX ? list->count : TORIRSSERVER_NPC_PATROL_MAX;
    /* Full-size, as the text loader allocated it: a later statement may index
     * any slot. */
    def->patrol = calloc(TORIRSSERVER_NPC_PATROL_MAX, sizeof(*def->patrol));
    assert(def->patrol);
    for( int i = 0; i < count; i++ )
    {
        uint32_t coord = (uint32_t)list->items[i * 2].i;

        def->patrol[i].level = (int)((coord >> 28) & 0x3);
        def->patrol[i].x = (int)((coord >> 14) & 0x3FFF);
        def->patrol[i].z = (int)(coord & 0x3FFF);
        def->patrol[i].pause = list->items[i * 2 + 1].i;
    }
    def->patrol_count = count;
}

static const struct RSCache_BandBinding k_npc_bindings[] = {
    BIND(struct ToriRSServerNpcDef, attack),
    BIND(struct ToriRSServerNpcDef, defence),
    BIND(struct ToriRSServerNpcDef, strength),
    BIND_APPLY(struct ToriRSServerNpcDef, "hitpoints", hitpoints, apply_npc_hitpoints),
    BIND(struct ToriRSServerNpcDef, ranged),
    BIND(struct ToriRSServerNpcDef, magic),
    BIND(struct ToriRSServerNpcDef, attackrate),
    BIND(struct ToriRSServerNpcDef, death_drop),
    BIND(struct ToriRSServerNpcDef, attack_anim),
    BIND(struct ToriRSServerNpcDef, defend_anim),
    BIND(struct ToriRSServerNpcDef, death_anim),
    BIND(struct ToriRSServerNpcDef, death_delay),
    BIND(struct ToriRSServerNpcDef, blockwalk),
    BIND(struct ToriRSServerNpcDef, blocksight),
    BIND(struct ToriRSServerNpcDef, attack_sound),
    BIND(struct ToriRSServerNpcDef, defend_sound),
    BIND(struct ToriRSServerNpcDef, death_sound),
    /* Ours: LostCity's engine does not retaliate on the npc's behalf at all. */
    BIND(struct ToriRSServerNpcDef, retaliate),
    /* Which bar, and whether the splat rides with it. */
    BIND(struct ToriRSServerNpcDef, healthbar),
    BIND(struct ToriRSServerNpcDef, hitsplat),
    BIND(struct ToriRSServerNpcDef, forcemulti),
    BIND(struct ToriRSServerNpcDef, wanderrange),
    BIND(struct ToriRSServerNpcDef, maxrange),
    BIND(struct ToriRSServerNpcDef, huntrange),
    /* `timer=`, the tick interval `[ai_timer]` runs at. */
    BIND(struct ToriRSServerNpcDef, timer),
    BIND(struct ToriRSServerNpcDef, respawnrate),
    /* `nomove` first: a record stating both has its collapsed boolean derived
     * from moverestrict, which is the one the text spells. */
    BIND(struct ToriRSServerNpcDef, nomove),
    /* LostCity's moverestrict; its apply restates nomove (moverestrict 5). */
    BIND_APPLY(struct ToriRSServerNpcDef, "moverestrict", moverestrict, apply_npc_moverestrict),
    /* `aggressive_melee` packs as 2 and the engine hunts only on 1
     * (`TORIRSSERVER_HUNT_AGGRESSIVE`), as it did when the text parse read it. */
    BIND(struct ToriRSServerNpcDef, huntmode),
    BIND(struct ToriRSServerNpcDef, givechase),
    BIND_APPLY(struct ToriRSServerNpcDef, "defaultmode", defaultmode, apply_npc_defaultmode),
    BIND_APPLY(struct ToriRSServerNpcDef, "patrol", patrol, apply_npc_patrol),
    BIND(struct ToriRSServerNpcDef, facing),
    /* Params the engine reads as members rather than through npc_param. */
    BIND(struct ToriRSServerNpcDef, damagetype),
    BIND(struct ToriRSServerNpcDef, attackrange),
};

/*
 * A door's other half.
 *
 * `category` is deliberately absent: the tree authors `category=door_closed` on
 * these same blocks, but a loc's category is a *client* field the cache record
 * already carries, so it reaches the server through the decoded record rather
 * than through this band. The register says as much — `[loc.category]` is
 * `scope = client` with no server opcode.
 */
static const struct RSCache_BandBinding k_loc_bindings[] = {
    BIND(struct ToriRSServerLocDef, next_loc_stage),
};

/*
 * An obj's skill requirements: `levelrequire1=<stat>,<level>`, `levelrequire2=...`, a
 * (stat, int) list in the band. The whole list replaces the obj's requirements,
 * as a config block naming an item states the whole requirement for it.
 */
static void
apply_obj_levelrequire(
    void* object,
    const struct RSCache_BandRecord* record,
    int index)
{
    struct ToriRSServerObjBand* obj = (struct ToriRSServerObjBand*)object;
    const struct RSCache_BandList* list;
    int stats[TORIRSSERVER_OBJ_REQUIRE_MAX];
    int levels[TORIRSSERVER_OBJ_REQUIRE_MAX];
    int count = 0;

    assert(obj);
    assert(record);
    assert(index >= 0);
    assert(index < RSCACHE_REGISTER_MAX);
    list = record->lists[index];
    if( list )
    {
        assert(list->arity == 2);
        for( int i = 0; i < list->count && count < TORIRSSERVER_OBJ_REQUIRE_MAX; i++ )
        {
            stats[count] = list->items[i * 2].i;
            levels[count] = list->items[i * 2 + 1].i;
            count++;
        }
    }
    obj->levelrequire = count;
    if( !ToriRSServer_ObjRequireSet(obj->obj_id, stats, levels, count) )
        fprintf(stderr, "torirsserver: server pack: obj %d: %d requirement(s) do not fit\n",
                obj->obj_id, count);
}

/* `scope=perm` is the one scope the varp def keeps (`scope_perm`); temp and
 * shared both read as 0, as the text loader read them. */
static void
apply_varp_scope(
    void* object,
    const struct RSCache_BandRecord* record,
    int index)
{
    struct ToriRSServerVarpDef* def = (struct ToriRSServerVarpDef*)object;

    assert(def);
    def->scope_perm = stated_int(record, index) == 1;
}

static const struct RSCache_BandBinding k_varp_bindings[] = {
    BIND_APPLY(struct ToriRSServerVarpDef, "scope", scope_perm, apply_varp_scope),
    BIND(struct ToriRSServerVarpDef, transmit),
    BIND(struct ToriRSServerVarpDef, protect),
    BIND_APPLY(struct ToriRSServerVarpDef, "wholewrite", wholewrite_allowed, NULL),
    BIND_APPLY(struct ToriRSServerVarpDef, "wholeread", wholeread_allowed, NULL),
};

/* A shop's `scope`: shared is a world container; temp (and perm) are not. */
static void
apply_inv_scope(
    void* object,
    const struct RSCache_BandRecord* record,
    int index)
{
    struct ToriRSServerShopDef* def = (struct ToriRSServerShopDef*)object;

    assert(def);
    def->shared = stated_int(record, index) == 1;
}

/* `stockN=obj,baseline,rate`, in order; the list replaces the shop's stock. */
static void
apply_inv_stock(
    void* object,
    const struct RSCache_BandRecord* record,
    int index)
{
    struct ToriRSServerShopDef* def = (struct ToriRSServerShopDef*)object;
    const struct RSCache_BandList* list;

    assert(def);
    assert(record);
    assert(index >= 0);
    assert(index < RSCACHE_REGISTER_MAX);
    def->stock_count = 0;
    list = record->lists[index];
    if( !list )
        return;
    assert(list->arity == 3);
    for( int i = 0; i < list->count; i++ )
    {
        if( !ToriRSServer_ShopDefAddStock(def, list->items[i * 3].i, list->items[i * 3 + 1].i,
                                          list->items[i * 3 + 2].i) )
        {
            fprintf(stderr, "torirsserver: server pack: inv %d: more than %d stock lines\n",
                    (int)def->inv_id, TORIRSSERVER_SHOP_STOCK_MAX);
            break;
        }
    }
}

static const struct RSCache_BandBinding k_inv_bindings[] = {
    BIND_APPLY(struct ToriRSServerShopDef, "scope", shared, apply_inv_scope),
    BIND(struct ToriRSServerShopDef, restock),
    BIND(struct ToriRSServerShopDef, allstock),
    BIND(struct ToriRSServerShopDef, stackall),
    BIND_APPLY(struct ToriRSServerShopDef, "stock", stock_count, apply_inv_stock),
};

/*
 * A dbtable's column names: its `column=<name>,<types>` lines, in order, one band
 * tuple each (the client record has the types and no names). An `ABSENT` hole
 * keeps its position and names nothing.
 */
static void
apply_dbtable_column(
    void* object,
    const struct RSCache_BandRecord* record,
    int index)
{
    struct ToriRSServerDbTable* table = (struct ToriRSServerDbTable*)object;
    const struct RSCache_BandList* list;

    assert(table);
    assert(record);
    assert(index >= 0);
    assert(index < RSCACHE_REGISTER_MAX);
    list = record->lists[index];
    if( !list )
        return;
    assert(list->arity == 1);
    for( int i = 0; i < list->count; i++ )
    {
        const char* line = list->items[i].s;
        const char* comma = line ? strchr(line, ',') : NULL;
        char name[128];
        size_t length;

        if( !comma )
            continue;
        if( strcmp(comma + 1, "ABSENT") == 0 )
            continue;
        length = (size_t)(comma - line);
        if( length >= sizeof(name) )
            length = sizeof(name) - 1;
        memcpy(name, line, length);
        name[length] = '\0';
        ToriRSServer_DbColumnNameSet(table, i, name);
    }
}

static const struct RSCache_BandBinding k_dbtable_bindings[] = {
    BIND_APPLY(struct ToriRSServerDbTable, "column", column_count, apply_dbtable_column),
};

static const struct RSCache_BandBinding k_obj_bindings[] = {
    BIND_APPLY(struct ToriRSServerObjBand, "levelrequire", levelrequire, apply_obj_levelrequire),
};

#undef BIND
#undef BIND_APPLY

#define COUNT_OF(table) ((int)(sizeof(table) / sizeof((table)[0])))

static const struct ToriRSServerBandType k_types[] = {
    { "npc", k_npc_bindings, COUNT_OF(k_npc_bindings), sizeof(struct ToriRSServerNpcDef) },
    { "loc", k_loc_bindings, COUNT_OF(k_loc_bindings), sizeof(struct ToriRSServerLocDef) },
    { "obj", k_obj_bindings, COUNT_OF(k_obj_bindings), sizeof(struct ToriRSServerObjBand) },
    { "varp", k_varp_bindings, COUNT_OF(k_varp_bindings), sizeof(struct ToriRSServerVarpDef) },
    { "inv", k_inv_bindings, COUNT_OF(k_inv_bindings), sizeof(struct ToriRSServerShopDef) },
    { "dbtable", k_dbtable_bindings, COUNT_OF(k_dbtable_bindings),
      sizeof(struct ToriRSServerDbTable) },
};

const struct ToriRSServerBandType*
ToriRSServer_ServerTypes(int* out_count)
{
    assert(out_count);
    *out_count = COUNT_OF(k_types);
    return k_types;
}

const struct ToriRSServerBandType*
ToriRSServer_ServerTypeFor(const char* name)
{
    assert(name);
    for( int i = 0; i < COUNT_OF(k_types); i++ )
    {
        if( strcmp(k_types[i].name, name) == 0 )
            return &k_types[i];
    }
    return NULL;
}

int
ToriRSServer_ServerCheck(
    const struct ToriRSServerBandType* type,
    const struct RSCache_Register* reg)
{
    int problems;

    assert(type);
    assert(reg);
    problems = RSCache_RegisterCheck(reg);
    problems += RSCache_BandBindingCheck(reg, type->bindings, type->binding_count);

    /* The direction the library cannot check, because it does not know which
     * fields a reader consumes: a band field with no member to land in.
     * cachepack writes it, RSCache_BandBindingApply skips it, and the authored
     * value never reaches the engine. */
    for( int i = 0; i < reg->band_count; i++ )
    {
        int bound = 0;

        for( int b = 0; b < type->binding_count; b++ )
        {
            if( strcmp(type->bindings[b].name, reg->entries[i].name) == 0 )
                bound++;
        }
        /* A field with `param = <name>` lands in the record's param table (a loc's
         * `loc_param`), which the loader fills from the register itself; it needs
         * no member. */
        if( bound == 0 && reg->entries[i].param_name[0] )
            continue;
        if( bound == 0 )
        {
            fprintf(stderr,
                    "fields/%s.ini: [%s.%s] has server opcode %d but this server binds no "
                    "member to it\n",
                    reg->type, reg->type, reg->entries[i].name, reg->entries[i].opcode);
            problems++;
        }
        else if( bound > 1 )
        {
            fprintf(stderr, "fields/%s.ini: [%s.%s] is bound to %d members\n", reg->type,
                    reg->type, reg->entries[i].name, bound);
            problems++;
        }
    }
    return problems;
}

int
ToriRSServer_ServerDecode(
    const struct ToriRSServerBandType* type,
    const struct RSCache_Register* reg,
    struct RSCache_BandRecord* record,
    void* object,
    const uint8_t* src,
    int size)
{
    int consumed;

    assert(type);
    assert(reg);
    assert(record);
    assert(object);
    assert(src);
    assert(size >= 0);
    consumed = RSCache_BandDecode(reg, record, src, size);
    if( consumed < 0 )
        return -1;
    RSCache_BandBindingApply(reg, type->bindings, type->binding_count, record, object);
    return consumed;
}
