/* UPDATE_PLAYER_MODEL_V1. Hand-written; see update_player_model_v1.h. */
#include "update_player_model_v1.h"

/* v1 -- revs 223-225. */
void
packet_update_player_model_v1_v1_in(RsprotExec *x, MsgUpdatePlayerModelV1 *m)
{
    RSPROT_U1(x, m->body_type);
    for (int32_t i = 0; i < 7; i++)
        RSPROT_U1(x, m->ident_kit[i]);
    for (int32_t i = 0; i < 5; i++)
        RSPROT_U1(x, m->colours[i]);
}

const RsprotVersionRange rsprot_update_player_model_v1_in[] = {
    { RSPROT_OSRS_ID(223), RSPROT_OSRS_ID(225), (RsprotCodecFn)packet_update_player_model_v1_v1_in },
};

const int rsprot_update_player_model_v1_in_count =
    (int)(sizeof(rsprot_update_player_model_v1_in) / sizeof(rsprot_update_player_model_v1_in[0]));
