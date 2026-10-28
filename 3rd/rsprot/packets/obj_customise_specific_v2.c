/* OBJ_CUSTOMISE_SPECIFIC_V2. Hand-written -- see obj_customise_specific_v2.h. */
#include "obj_customise_specific_v2.h"

/*
 * v1 -- revs 241.
 * g4 packedCoord, g2_21_add128 id, g2_21_add128 modelId, g1_sub128 retextureCount,
 * [g1_neg index, g2_12_add128 value]*, g4_2143 quantity, g1_neg recolourCount,
 * [g1_sub128 index, g2_21 value]*, g1 hasColour, [g2 colour].
 */
void
packet_obj_customise_specific_v2_v1_out(RsprotExec *x, MsgObjCustomiseSpecificV2 *m)
{
    RSPROT_U4_1234(x, m->packed_coord);
    RSPROT_U2_21_ADD128(x, m->id);
    RSPROT_U2_21_ADD128(x, m->model_id);
    RSPROT_COUNT_AS(x, U1_SUB128, m->retexture_count, MSG_OBJ_CUSTOMISE_SPECIFIC_V2_MAX_REPLACEMENTS);
    for (int32_t i = 0; i < m->retexture_count; i++) {
        RSPROT_U1_NEG(x, m->retextures[i].index);
        RSPROT_U2_12_ADD128(x, m->retextures[i].value);
    }
    RSPROT_U4_2143(x, m->quantity);
    RSPROT_COUNT_AS(x, U1_NEG, m->recolour_count, MSG_OBJ_CUSTOMISE_SPECIFIC_V2_MAX_REPLACEMENTS);
    for (int32_t i = 0; i < m->recolour_count; i++) {
        RSPROT_U1_SUB128(x, m->recolours[i].index);
        RSPROT_U2_21(x, m->recolours[i].value);
    }
    RSPROT_U1(x, m->has_colour);
    if (RSPROT_BRANCH(x, m->has_colour) == 1)
        RSPROT_U2_12(x, m->colour);
}

/* osrsm241 -- the Android 241.4 client (Old School mobile). */
void
packet_obj_customise_specific_v2_osrsm241_out(RsprotExec *x, MsgObjCustomiseSpecificV2 *m)
{
    RSPROT_U2_12(x, m->id);
    RSPROT_COUNT(x, m->recolour_count, MSG_OBJ_CUSTOMISE_SPECIFIC_V2_MAX_REPLACEMENTS);
    for (int32_t i = 0; i < m->recolour_count; i++) {
        RSPROT_U1_ADD128(x, m->recolours[i].index);
        RSPROT_U2_21_ADD128(x, m->recolours[i].value);
    }
    RSPROT_U1_SUB128(x, m->has_colour);
    if (RSPROT_BRANCH(x, m->has_colour) == 1)
        RSPROT_U2_12_ADD128(x, m->colour);
    RSPROT_U2_21_ADD128(x, m->model_id);
    RSPROT_COUNT(x, m->retexture_count, MSG_OBJ_CUSTOMISE_SPECIFIC_V2_MAX_REPLACEMENTS);
    for (int32_t i = 0; i < m->retexture_count; i++) {
        RSPROT_U1_ADD128(x, m->retextures[i].index);
        RSPROT_U2_12(x, m->retextures[i].value);
    }
    RSPROT_U4_1234(x, m->packed_coord);
    RSPROT_U4_1234(x, m->quantity);
}

const RsprotVersionRange rsprot_obj_customise_specific_v2_out[] = {
    { RSPROT_OSRS_ID(241), RSPROT_OSRS_ID(241), (RsprotCodecFn)packet_obj_customise_specific_v2_v1_out },
    { RSPROT_OSRSM_ID(241), RSPROT_OSRSM_ID(241), (RsprotCodecFn)packet_obj_customise_specific_v2_osrsm241_out },
};

const int rsprot_obj_customise_specific_v2_out_count =
    (int)(sizeof(rsprot_obj_customise_specific_v2_out) / sizeof(rsprot_obj_customise_specific_v2_out[0]));
