#include "mock230_servercodec.h"

#include <stddef.h>
#include <string.h>

/*
 * One table, both directions.
 *
 * Encode and decode are generated from the same rows rather than written as two
 * switches, because two switches are exactly how a field comes to be written
 * under one opcode and read under another — a failure with no error attached,
 * since both streams are individually valid. `test_servercodec` walks this table
 * against `fields/npc.ini` so a row that drifts from the register is caught.
 *
 * `wire` is the width the register declares. It is not cosmetic: a field written
 * as u2 and read as u4 consumes two bytes too many and misaligns everything
 * after it, which is the same class of bug as `dat2_config_obj.c` opcode 115.
 */

/* Ordered by opcode so the encoded stream is ascending, which makes a pack diff
 * readable and two packs of the same content byte-identical. */
static const struct ServerField k_npc_fields[] = {
    { 74, WIRE_U2, offsetof(struct Mock230NpcDef, attack), "attack" },
    { 75, WIRE_U2, offsetof(struct Mock230NpcDef, defence), "defence" },
    { 76, WIRE_U2, offsetof(struct Mock230NpcDef, strength), "strength" },
    { 77, WIRE_U2, offsetof(struct Mock230NpcDef, hitpoints), "hitpoints" },
    { 78, WIRE_U2, offsetof(struct Mock230NpcDef, ranged), "ranged" },
    { 79, WIRE_U2, offsetof(struct Mock230NpcDef, magic), "magic" },
    { 150, WIRE_U1, offsetof(struct Mock230NpcDef, attackrate), "attackrate" },
    { 151, WIRE_U4, offsetof(struct Mock230NpcDef, death_drop), "death_drop" },
    { 152, WIRE_U4, offsetof(struct Mock230NpcDef, attack_anim), "attack_anim" },
    { 153, WIRE_U4, offsetof(struct Mock230NpcDef, defend_anim), "defend_anim" },
    { 154, WIRE_U4, offsetof(struct Mock230NpcDef, death_anim), "death_anim" },
    { 200, WIRE_U2, offsetof(struct Mock230NpcDef, wanderrange), "wanderrange" },
    { 202, WIRE_U1, offsetof(struct Mock230NpcDef, huntrange), "huntrange" },
    { 204, WIRE_U2, offsetof(struct Mock230NpcDef, respawnrate), "respawnrate" },
    { 208, WIRE_U1, offsetof(struct Mock230NpcDef, nomove), "nomove" },
    { 209, WIRE_U1, offsetof(struct Mock230NpcDef, huntmode), "huntmode" },
};

#define NPC_FIELD_COUNT ((int)(sizeof(k_npc_fields) / sizeof(k_npc_fields[0])))

const struct ServerField*
Mock230_ServerNpcFields(int* out_count)
{
    if( out_count )
        *out_count = NPC_FIELD_COUNT;
    return k_npc_fields;
}

static int
field_get(const struct Mock230NpcDef* def, const struct ServerField* field)
{
    int value;

    memcpy(&value, (const char*)def + field->offset, sizeof(value));
    return value;
}

static void
field_set(struct Mock230NpcDef* def, const struct ServerField* field, int value)
{
    memcpy((char*)def + field->offset, &value, sizeof(value));
}

uint32_t
Mock230_ServerNpcEncodeBound(const struct Mock230NpcDef* def)
{
    (void)def;
    /* Every field at once — an opcode byte plus its widest payload — plus the
     * terminator. Small enough that computing it per record would buy nothing. */
    return (uint32_t)NPC_FIELD_COUNT * 5u + 1u;
}

uint32_t
Mock230_ServerNpcEncode(
    const struct Mock230NpcDef* def,
    const struct Mock230NpcDef* defaults,
    uint8_t* out,
    uint32_t out_capacity)
{
    uint32_t at = 0;
    int i;

    if( !def || !out || out_capacity < Mock230_ServerNpcEncodeBound(def) )
        return 0;

    for( i = 0; i < NPC_FIELD_COUNT; i++ )
    {
        const struct ServerField* field = &k_npc_fields[i];
        int value = field_get(def, field);

        /*
         * Compared against the *engine default*, not against zero.
         *
         * `death_drop` defaults to -1 (`param=death_drop,null`), so a
         * zero-compare would emit it for every npc that drops nothing and omit it
         * for one that drops obj 0. The default record is the only thing that
         * knows which is which.
         */
        if( defaults && value == field_get(defaults, field) )
            continue;

        out[at++] = (uint8_t)field->opcode;
        switch( field->wire )
        {
        case WIRE_U1:
            out[at++] = (uint8_t)(value & 0xFF);
            break;
        case WIRE_U2:
            out[at++] = (uint8_t)((value >> 8) & 0xFF);
            out[at++] = (uint8_t)(value & 0xFF);
            break;
        case WIRE_U4:
            out[at++] = (uint8_t)((value >> 24) & 0xFF);
            out[at++] = (uint8_t)((value >> 16) & 0xFF);
            out[at++] = (uint8_t)((value >> 8) & 0xFF);
            out[at++] = (uint8_t)(value & 0xFF);
            break;
        }
    }

    out[at++] = 0;
    return at;
}

int
Mock230_ServerNpcDecode(
    struct Mock230NpcDef* def,
    const uint8_t* src,
    int size)
{
    int at = 0;

    if( !def || !src || size < 0 )
        return -1;

    while( at < size )
    {
        int opcode = src[at++];
        const struct ServerField* field = NULL;
        int value = 0;
        int i;

        if( opcode == 0 )
            break;
        for( i = 0; i < NPC_FIELD_COUNT; i++ )
        {
            if( k_npc_fields[i].opcode == opcode )
            {
                field = &k_npc_fields[i];
                break;
            }
        }
        /* Unknown opcode: stop. Its payload width is unknown, so continuing would
         * read a payload byte as the next opcode — the short return is the
         * signal, exactly as in rscache's decoders. */
        if( !field )
            return at - 1;
        if( at + (int)field->wire > size )
            return at - 1;

        switch( field->wire )
        {
        case WIRE_U1:
            value = src[at];
            break;
        case WIRE_U2:
            value = (src[at] << 8) | src[at + 1];
            break;
        case WIRE_U4:
            value = (src[at] << 24) | (src[at + 1] << 16) | (src[at + 2] << 8) | src[at + 3];
            break;
        }
        at += (int)field->wire;
        field_set(def, field, value);
    }
    return at;
}
