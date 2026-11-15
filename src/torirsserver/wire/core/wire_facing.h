#ifndef SRC_TORIRSSERVER_WIRE_CORE_WIRE_FACING_H
#define SRC_TORIRSSERVER_WIRE_CORE_WIRE_FACING_H

/*
 * Revision-239's actor facing block.
 *
 * This is deliberately separate from the rev-230 FACE_ENTITY/FACE_COORD
 * fields.  At 239 they are alternatives encoded by one header: a target
 * entity, a map location, an angle, or an explicit reset.  The desktop player
 * and npc writers use the same payload but different byte transforms for that
 * header (PlayerFacingEncoder / NpcFaceEncoder in RSProt).
 */

#include "rsprot_buf.h"

enum WireFaceKind
{
    WIRE_FACE_ENTITY = 0,
    WIRE_FACE_LOC = 1,
    WIRE_FACE_ANGLE = 2,
    WIRE_FACE_RESET = 3,
};

enum WireFaceEntityType
{
    WIRE_FACE_NPC = 1,
    WIRE_FACE_PLAYER = 2,
    WIRE_FACE_WORLD_ENTITY = 3,
};

struct WireFace
{
    enum WireFaceKind kind;
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
wire_face_init(struct WireFace* face)
{
    face->kind = WIRE_FACE_RESET;
    face->entity_type = WIRE_FACE_NPC;
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
wire_face_write_payload(struct RsprotBuf* buf, struct WireFace const* face)
{
    switch( face->kind )
    {
    case WIRE_FACE_ENTITY:
        rsprot_psmart1or2(buf, face->entity_type);
        /* Any negative index is "no entity" (FaceEntityForClient passes the
         * server's face_entity through unchanged); smart2or4null's null is -1. */
        rsprot_psmart2or4null(buf, face->entity_index < 0 ? -1 : face->entity_index);
        rsprot_psmart1or2(buf, face->entity_fallback_angle);
        break;
    case WIRE_FACE_LOC:
        rsprot_psmart1or2(buf, face->x);
        rsprot_psmart1or2(buf, face->z);
        rsprot_psmart1or2(buf, (face->size_z << 4) | face->size_x);
        break;
    case WIRE_FACE_ANGLE:
        rsprot_psmart1or2(buf, face->angle);
        break;
    case WIRE_FACE_RESET:
    default:
        break;
    }
}

static inline void
wire_face_write_player(struct RsprotBuf* buf, struct WireFace const* face)
{
    int header = face->walk_mode | ((int)face->kind << 3) | (face->instant ? 0x40 : 0);

    rsprot_p1_neg(buf, header);
    wire_face_write_payload(buf, face);
}

static inline void
wire_face_write_npc(struct RsprotBuf* buf, struct WireFace const* face)
{
    int header = face->walk_mode | ((int)face->kind << 3) | (face->instant ? 0x40 : 0);

    rsprot_p1_add128(buf, header);
    wire_face_write_payload(buf, face);
}

#endif
