/* OBJ_CUSTOMISE_SPECIFIC_V2. Hand-written -- see obj_customise_specific_v2.h. */
#include "obj_customise_specific_v2.h"

/*
 * v1 (rsprox_versions.txt 100) -- revs 241.
 * g4 packedCoord, g2Alt3 id, g2Alt3 modelId, g1Alt3 retextureCount,
 * [g1Alt2 index, g2Alt2 value]*, g4Alt3 quantity, g1Alt2 recolourCount,
 * [g1Alt3 index, g2Alt1 value]*, g1 hasColour, [g2 colour].
 */
void
packet_obj_customise_specific_v2_v1_out(RsprotExec *x, MsgObjCustomiseSpecificV2 *m)
{
    RSPROT_U4(x, m->packed_coord);
    RSPROT_U2_ALT3(x, m->id);
    RSPROT_U2_ALT3(x, m->model_id);
    RSPROT_COUNT_AS(x, U1_ALT3, m->retexture_count, MSG_OBJ_CUSTOMISE_SPECIFIC_V2_MAX_REPLACEMENTS);
    for (int32_t i = 0; i < m->retexture_count; i++) {
        RSPROT_U1_ALT2(x, m->retextures[i].index);
        RSPROT_U2_ALT2(x, m->retextures[i].value);
    }
    RSPROT_U4_ALT3(x, m->quantity);
    RSPROT_COUNT_AS(x, U1_ALT2, m->recolour_count, MSG_OBJ_CUSTOMISE_SPECIFIC_V2_MAX_REPLACEMENTS);
    for (int32_t i = 0; i < m->recolour_count; i++) {
        RSPROT_U1_ALT3(x, m->recolours[i].index);
        RSPROT_U2_ALT1(x, m->recolours[i].value);
    }
    RSPROT_U1(x, m->has_colour);
    if (RSPROT_BRANCH(x, m->has_colour) == 1)
        RSPROT_U2(x, m->colour);
}

const RsprotVersionRange rsprot_obj_customise_specific_v2_out[] = {
    { 241, 241, (RsprotCodecFn)packet_obj_customise_specific_v2_v1_out },
};

const int rsprot_obj_customise_specific_v2_out_count =
    (int)(sizeof(rsprot_obj_customise_specific_v2_out) / sizeof(rsprot_obj_customise_specific_v2_out[0]));
