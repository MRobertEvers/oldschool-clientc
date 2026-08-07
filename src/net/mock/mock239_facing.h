#ifndef SRC_NET_MOCK_MOCK239_FACING_H
#define SRC_NET_MOCK_MOCK239_FACING_H

/*
 * Revision-239's actor facing block.
 *
 * This is deliberately separate from the rev-230 FACE_ENTITY/FACE_COORD
 * fields.  At 239 they are alternatives encoded by one header: a target
 * entity, a map location, an angle, or an explicit reset.  The desktop player
 * and npc writers use the same payload but different byte transforms for that
 * header (PlayerFacingEncoder / NpcFaceEncoder in RSProt).
 */

#include <rsareabuf.h>

enum Mock239FaceKind
{
    MOCK239_FACE_ENTITY = 0,
    MOCK239_FACE_LOC = 1,
    MOCK239_FACE_ANGLE = 2,
    MOCK239_FACE_RESET = 3,
};

enum Mock239FaceEntityType
{
    MOCK239_FACE_NPC = 1,
    MOCK239_FACE_PLAYER = 2,
    MOCK239_FACE_WORLD_ENTITY = 3,
};

struct Mock239Face
{
    enum Mock239FaceKind kind;
    int entity_type;
    int entity_index;
    int entity_fallback_angle;
    int angle;
    int x;
    int z;
    int size_x;
    int size_z;
    int walk_mode;
    int instant;
};

static inline void
mock239_face_init(struct Mock239Face* face)
{
    face->kind = MOCK239_FACE_RESET;
    face->entity_type = MOCK239_FACE_NPC;
    face->entity_index = -1;
    face->entity_fallback_angle = 0;
    face->angle = 0;
    face->x = 0;
    face->z = 0;
    face->size_x = 1;
    face->size_z = 1;
    face->walk_mode = 0;
    face->instant = 0;
}

static inline void
mock239_face_psmart1or2(struct RSAreaBuf* buf, int value)
{
    if( value >= 0 && value < 128 )
        rsab_p1(buf, value);
    else
        rsab_p2(buf, (value & 0x7fff) | 0x8000);
}

/* JagByteBuf.pSmart2or4null: -1 is 0x7fff, otherwise two bytes below
 * 32767 and four bytes with its top bit set above it. */
static inline void
mock239_face_psmart2or4null(struct RSAreaBuf* buf, int value)
{
    if( value < 0 )
        rsab_p2(buf, 0x7fff);
    else if( value < 32767 )
        rsab_p2(buf, value);
    else
        rsab_p4(buf, (int32_t)((uint32_t)value | 0x80000000u));
}

static inline void
mock239_face_write_payload(struct RSAreaBuf* buf, struct Mock239Face const* face)
{
    switch( face->kind )
    {
    case MOCK239_FACE_ENTITY:
        mock239_face_psmart1or2(buf, face->entity_type);
        mock239_face_psmart2or4null(buf, face->entity_index);
        mock239_face_psmart1or2(buf, face->entity_fallback_angle);
        break;
    case MOCK239_FACE_LOC:
        mock239_face_psmart1or2(buf, face->x);
        mock239_face_psmart1or2(buf, face->z);
        mock239_face_psmart1or2(buf, (face->size_z << 4) | face->size_x);
        break;
    case MOCK239_FACE_ANGLE:
        mock239_face_psmart1or2(buf, face->angle);
        break;
    case MOCK239_FACE_RESET:
    default:
        break;
    }
}

static inline void
mock239_face_write_player(struct RSAreaBuf* buf, struct Mock239Face const* face)
{
    int header = face->walk_mode | ((int)face->kind << 3) | (face->instant ? 0x40 : 0);

    rsab_p1_alt2(buf, header);
    mock239_face_write_payload(buf, face);
}

static inline void
mock239_face_write_npc(struct RSAreaBuf* buf, struct Mock239Face const* face)
{
    int header = face->walk_mode | ((int)face->kind << 3) | (face->instant ? 0x40 : 0);

    rsab_p1_alt1(buf, header);
    mock239_face_write_payload(buf, face);
}

#endif
