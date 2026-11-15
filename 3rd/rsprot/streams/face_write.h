#ifndef RSPROT_STREAM_FACE_WRITE_H
#define RSPROT_STREAM_FACE_WRITE_H

/*
 * Revision-239's actor facing block.
 *
 * This is deliberately separate from the rev-230 FACE_ENTITY/FACE_COORD
 * fields.  At 239 they are alternatives encoded by one header: a target
 * entity, a map location, an angle, or an explicit reset.  The desktop player
 * and npc writers use the same payload but different byte transforms for that
 * header (PlayerFacingEncoder / NpcFaceEncoder in RSProt).
 */

#include "../src/rsprot_buf.h"
#include "rsprot_info.h" /* RSPROT_FACE_TARGET_*: the same numbers the reader names */

enum RsprotFaceKind
{
    RSPROT_FACE_KIND_ENTITY = 0,
    RSPROT_FACE_KIND_LOC = 1,
    RSPROT_FACE_KIND_ANGLE = 2,
    RSPROT_FACE_KIND_RESET = 3,
};

struct RsprotFace
{
    enum RsprotFaceKind kind;
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
rsprot_face_init(struct RsprotFace* face)
{
    face->kind = RSPROT_FACE_KIND_RESET;
    face->entity_type = RSPROT_FACE_TARGET_NPC;
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
rsprot_face_write_payload(struct RsprotBuf* buf, struct RsprotFace const* face)
{
    switch( face->kind )
    {
    case RSPROT_FACE_KIND_ENTITY:
        rsprot_psmart1or2(buf, face->entity_type);
        /* Any negative index is "no entity" (FaceEntityForClient passes the
         * server's face_entity through unchanged); smart2or4null's null is -1. */
        rsprot_psmart2or4null(buf, face->entity_index < 0 ? -1 : face->entity_index);
        rsprot_psmart1or2(buf, face->entity_fallback_angle);
        break;
    case RSPROT_FACE_KIND_LOC:
        rsprot_psmart1or2(buf, face->x);
        rsprot_psmart1or2(buf, face->z);
        rsprot_psmart1or2(buf, (face->size_z << 4) | face->size_x);
        break;
    case RSPROT_FACE_KIND_ANGLE:
        rsprot_psmart1or2(buf, face->angle);
        break;
    case RSPROT_FACE_KIND_RESET:
    default:
        break;
    }
}

static inline void
rsprot_face_write_player(struct RsprotBuf* buf, struct RsprotFace const* face)
{
    int header = face->walk_mode | ((int)face->kind << 3) | (face->instant ? 0x40 : 0);

    rsprot_p1_neg(buf, header);
    rsprot_face_write_payload(buf, face);
}

static inline void
rsprot_face_write_npc(struct RsprotBuf* buf, struct RsprotFace const* face)
{
    int header = face->walk_mode | ((int)face->kind << 3) | (face->instant ? 0x40 : 0);

    rsprot_p1_add128(buf, header);
    rsprot_face_write_payload(buf, face);
}

#endif
