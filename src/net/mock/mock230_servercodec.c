#include "mock230_servercodec.h"

#include "rsbuffer.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

/*
 * One table per type, both directions, one implementation.
 *
 * Encode and decode are generated from the same rows rather than written as two
 * switches, because two switches are exactly how a field comes to be written
 * under one opcode and read under another — a failure with no error attached,
 * since both streams are individually valid. `test_servercodec` walks every table
 * here against its `fields/<type>.ini` so a row that drifts from the register is
 * caught.
 *
 * `wire` is the width the register declares. It is not cosmetic: a field written
 * as u2 and read as u4 consumes two bytes too many and misaligns everything
 * after it, which is the same class of bug as `dat2_config_obj.c` opcode 115.
 *
 * ## The bytes go through RSCache_Buffer
 *
 * `g1/g2/g4` and their `p` inverses, rather than shifts written out here. The
 * library states the property this codec rests on and nothing local can:
 * *"every `g` below has a `p` that is its exact inverse"* (`rsbuffer.h`). A
 * hand-rolled pair would be a fourth place — after the register, these tables and
 * the packer — where the two directions can disagree about a width, and it would
 * disagree silently, because a u2 written big-endian and read little-endian is
 * still a perfectly well-formed stream. It also puts the bounds check on the
 * shared cursor instead of on an `at + wire > size` this file has to keep right
 * by itself.
 */

/*
 * Ordered by opcode so the encoded stream is ascending, which makes a pack diff
 * readable and two packs of the same content byte-identical. Ascending is a
 * *choice*, not a constraint — the decode below dispatches per opcode, so any
 * order reads back identically. Worth stating because the cache's own config
 * records are the opposite case: dat1 does not write them in opcode order, and
 * reproducing one means recording the decoded order and replaying it.
 */
static const struct ServerField k_npc_fields[] = {
    { 74,  WIRE_U2, offsetof(struct Mock230NpcDef, attack),      "attack"      },
    { 75,  WIRE_U2, offsetof(struct Mock230NpcDef, defence),     "defence"     },
    { 76,  WIRE_U2, offsetof(struct Mock230NpcDef, strength),    "strength"    },
    { 77,  WIRE_U2, offsetof(struct Mock230NpcDef, hitpoints),   "hitpoints"   },
    { 78,  WIRE_U2, offsetof(struct Mock230NpcDef, ranged),      "ranged"      },
    { 79,  WIRE_U2, offsetof(struct Mock230NpcDef, magic),       "magic"       },
    { 150, WIRE_U1, offsetof(struct Mock230NpcDef, attackrate),  "attackrate"  },
    { 151, WIRE_U4, offsetof(struct Mock230NpcDef, death_drop),  "death_drop"  },
    { 152, WIRE_U4, offsetof(struct Mock230NpcDef, attack_anim), "attack_anim" },
    { 153, WIRE_U4, offsetof(struct Mock230NpcDef, defend_anim), "defend_anim" },
    { 154, WIRE_U4, offsetof(struct Mock230NpcDef, death_anim),  "death_anim"  },
    { 155, WIRE_U1, offsetof(struct Mock230NpcDef, death_delay), "death_delay" },
    /* 156/157 are ours alone (150..199). LostCity puts blockwalk at 208, but
     * that opcode already carries this tree's `nomove` boolean. */
    { 156, WIRE_U1, offsetof(struct Mock230NpcDef, blockwalk),   "blockwalk"   },
    { 157, WIRE_U1, offsetof(struct Mock230NpcDef, blocksight),  "blocksight"  },
    { 200, WIRE_U2, offsetof(struct Mock230NpcDef, wanderrange), "wanderrange" },
    { 201, WIRE_U2, offsetof(struct Mock230NpcDef, maxrange),    "maxrange"    },
    { 202, WIRE_U1, offsetof(struct Mock230NpcDef, huntrange),   "huntrange"   },
    { 204, WIRE_U2, offsetof(struct Mock230NpcDef, respawnrate), "respawnrate" },
    /* LostCity's moverestrict opcode. `nomove` at 208 stays for packs that
     * already emit the collapsed boolean. */
    { 206, WIRE_U1, offsetof(struct Mock230NpcDef, moverestrict),"moverestrict"},
    { 208, WIRE_U1, offsetof(struct Mock230NpcDef, nomove),      "nomove"      },
    { 209, WIRE_U1, offsetof(struct Mock230NpcDef, huntmode),    "huntmode"    },
    { 213, WIRE_U1, offsetof(struct Mock230NpcDef, givechase),   "givechase"   },
};

/*
 * A door's other half.
 *
 * The cache states which locs exist and what they look like; nothing in it says
 * that closing `poordooropen` produces `poordoor`. `u4` and not `u2` because loc
 * ids in this revision already run past 62,000.
 *
 * `category` is deliberately absent: the tree authors `category=door_closed` on
 * these same blocks, but a loc's category is a *client* field the cache record
 * already carries (`cp_loc.c` emits it), so it reaches the server through the
 * decoded record rather than through this band. The register says as much —
 * there is no `[loc.category]` row — and this table agreeing with it is what the
 * cross-check checks.
 */
static const struct ServerField k_loc_fields[] = {
    { 150, WIRE_U4, offsetof(struct Mock230LocDef, next_loc_stage), "next_loc_stage" },
};

#define FIELD_COUNT(table) ((int)(sizeof(table) / sizeof((table)[0])))

static const struct ServerType k_types[] = {
    { "npc", k_npc_fields, FIELD_COUNT(k_npc_fields), sizeof(struct Mock230NpcDef) },
    { "loc", k_loc_fields, FIELD_COUNT(k_loc_fields), sizeof(struct Mock230LocDef) },
};

#define TYPE_COUNT ((int)(sizeof(k_types) / sizeof(k_types[0])))

const struct ServerType*
Mock230_ServerTypes(int* out_count)
{
    if( out_count )
        *out_count = TYPE_COUNT;
    return k_types;
}

const struct ServerType*
Mock230_ServerTypeFor(const char* name)
{
    if( !name )
        return NULL;
    for( int i = 0; i < TYPE_COUNT; i++ )
    {
        if( strcmp(k_types[i].name, name) == 0 )
            return &k_types[i];
    }
    return NULL;
}

/*
 * `memcpy` rather than a cast through `int*`.
 *
 * The offset is a byte count into a struct this file does not otherwise know, and
 * `*(int*)((char*)rec + off)` is only defined when that offset is `int`-aligned.
 * It always is today; a record that ever grows a `char` field before an `int` one
 * would make it undefined behaviour that works everywhere until it does not.
 */
static int
field_get(
    const void* record,
    const struct ServerField* field)
{
    int value;

    memcpy(&value, (const char*)record + field->offset, sizeof(value));
    return value;
}

static void
field_set(
    void* record,
    const struct ServerField* field,
    int value)
{
    memcpy((char*)record + field->offset, &value, sizeof(value));
}

uint32_t
Mock230_ServerEncodeBound(const struct ServerType* type)
{
    /* Every field at once — an opcode byte plus its widest payload — plus the
     * terminator. Small enough that computing it per record would buy nothing. */
    return type ? ((uint32_t)type->count * 5u) + 1u : 0u;
}

uint32_t
Mock230_ServerEncode(
    const struct ServerType* type,
    const void* record,
    const void* defaults,
    uint8_t* out,
    uint32_t out_capacity)
{
    struct RSCache_Buffer buffer;
    int i;
    assert(type != NULL);
    assert(record != NULL);
    assert(out != NULL);
    assert(out_capacity >= Mock230_ServerEncodeBound(type));

    RSCache_BufferInit(&buffer, out, out_capacity);

    for( i = 0; i < type->count; i++ )
    {
        const struct ServerField* field = &type->fields[i];
        int value = field_get(record, field);

        /*
         * Compared against the *engine default*, not against zero.
         *
         * `death_drop` defaults to -1 (`param=death_drop,null`), so a
         * zero-compare would emit it for every npc that drops nothing and omit it
         * for one that drops obj 0. The default record is the only thing that
         * knows which is which, and the same holds wherever 0 is a real value —
         * `idk.type` 0 is a body part, sprite 0 is a sprite.
         */
        if( defaults && value == field_get(defaults, field) )
            continue;

        RSCache_BufferP1(&buffer, field->opcode);
        switch( field->wire )
        {
        case WIRE_U1:
            /* Masked, because `p1` asserts on anything wider and a field declared
             * u1 whose value is not is a register error, not a stream error — the
             * packer refuses it there (`cp_server_band_put`) rather than truncating
             * a value into a different valid id here. */
            RSCache_BufferP1(&buffer, value & 0xFF);
            break;
        case WIRE_U2:
            RSCache_BufferP2(&buffer, value & 0xFFFF);
            break;
        case WIRE_U4:
            RSCache_BufferP4(&buffer, value);
            break;
        }
    }

    RSCache_BufferP1(&buffer, 0);
    return RSCache_BufferLength(&buffer);
}

int
Mock230_ServerDecode(
    const struct ServerType* type,
    void* record,
    const uint8_t* src,
    int size)
{
    struct RSCache_Buffer buffer;

    if( !type || !record || !src || size < 0 )
        return -1;

    /* Cast away const: the cursor is shared with the encoder and so takes a
     * writable pointer, but only `g` functions run below and none of them write. */
    RSCache_BufferInit(&buffer, (uint8_t*)src, (uint32_t)size);

    while( RSCache_BufferRemaining(&buffer) > 0 )
    {
        uint32_t opcode_at = buffer.position;
        int opcode = RSCache_BufferG1(&buffer);
        const struct ServerField* field = NULL;
        int value = 0;
        int i;

        if( opcode == 0 )
            break;
        for( i = 0; i < type->count; i++ )
        {
            if( type->fields[i].opcode == opcode )
            {
                field = &type->fields[i];
                break;
            }
        }
        /* Unknown opcode: stop. Its payload width is unknown, so continuing would
         * read a payload byte as the next opcode — the short return is the
         * signal, exactly as in rscache's decoders. The cursor is rewound to the
         * opcode itself so the return names the byte that stopped it. */
        if( !field )
            return (int)opcode_at;
        if( RSCache_BufferRemaining(&buffer) < (uint32_t)field->wire )
            return (int)opcode_at;

        switch( field->wire )
        {
        case WIRE_U1:
            value = RSCache_BufferG1(&buffer);
            break;
        case WIRE_U2:
            value = RSCache_BufferG2(&buffer);
            break;
        case WIRE_U4:
            value = RSCache_BufferG4(&buffer);
            break;
        }
        field_set(record, field, value);
    }
    return (int)RSCache_BufferLength(&buffer);
}
