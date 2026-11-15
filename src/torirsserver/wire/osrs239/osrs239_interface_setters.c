/* See osrs239_interface_setters.h. */

#include "torirsserver/wire/osrs239/osrs239_interface_setters.h"

#include "rsprot_buf.h"

/*
 * Every field order and alternative transform below is transcribed literally
 * from the revision-239 layouts in 3rd/rsprot/packets/.  Similar-looking
 * packets do not establish a pattern in this protocol, so each encoder is
 * stated separately.
 */

/* IfSetRotateSpeedEncoder:
 *   p2(ySpeed), p2Alt2(xSpeed), pCombinedIdAlt3(combinedId). */
void
osrs239_encode_if_setrotatespeed(
    struct RsprotBuf* buf,
    int combined_id,
    int x_speed,
    int y_speed)
{
    rsprot_p2_12(buf, y_speed);
    rsprot_p2_12_add128(buf, x_speed);
    rsprot_p4_2143(buf, combined_id);
}

/* IfSetAngleEncoder:
 *   pCombinedIdAlt3(combinedId), p2(zoom), p2(angleX), p2Alt3(angleY). */
void
osrs239_encode_if_setangle(
    struct RsprotBuf* buf,
    int combined_id,
    int zoom,
    int angle_x,
    int angle_y)
{
    rsprot_p4_2143(buf, combined_id);
    rsprot_p2_12(buf, zoom);
    rsprot_p2_12(buf, angle_x);
    rsprot_p2_21_add128(buf, angle_y);
}

/* IfSetPositionEncoder:
 *   p2Alt3(y), pCombinedIdAlt2(combinedId), p2Alt3(x). */
void
osrs239_encode_if_setposition(
    struct RsprotBuf* buf,
    int combined_id,
    int x,
    int y)
{
    rsprot_p2_21_add128(buf, y);
    rsprot_p4_3412(buf, combined_id);
    rsprot_p2_21_add128(buf, x);
}

/* IfSetNpcHeadActiveEncoder:
 *   p2Alt2(index), pCombinedIdAlt2(combinedId). */
void
osrs239_encode_if_setnpchead_active(
    struct RsprotBuf* buf,
    int combined_id,
    int index)
{
    rsprot_p2_12_add128(buf, index);
    rsprot_p4_3412(buf, combined_id);
}

/* IfSetPlayerModelBaseColourEncoder:
 *   p1(index), p1(colour), pCombinedId(combinedId). */
void
osrs239_encode_if_setplayermodel_basecolour(
    struct RsprotBuf* buf,
    int combined_id,
    int index,
    int colour)
{
    rsprot_p1(buf, index);
    rsprot_p1(buf, colour);
    rsprot_p4_1234(buf, combined_id);
}

/* IfSetPlayerModelBodyTypeEncoder:
 *   pCombinedIdAlt2(combinedId), p1(bodyType). */
void
osrs239_encode_if_setplayermodel_bodytype(
    struct RsprotBuf* buf,
    int combined_id,
    int body_type)
{
    rsprot_p4_3412(buf, combined_id);
    rsprot_p1(buf, body_type);
}

/* IfSetPlayerModelObjEncoder:
 *   pCombinedIdAlt2(combinedId), p4Alt3(obj). */
void
osrs239_encode_if_setplayermodel_obj(
    struct RsprotBuf* buf,
    int combined_id,
    int obj_id)
{
    rsprot_p4_3412(buf, combined_id);
    rsprot_p4_2143(buf, obj_id);
}

/* IfSetPlayerModelSelfEncoder:
 *   p1Alt3(copyObjs), pCombinedId(combinedId). */
void
osrs239_encode_if_setplayermodel_self(
    struct RsprotBuf* buf,
    int combined_id,
    int copy_objs)
{
    rsprot_p1_sub128(buf, copy_objs ? 1 : 0);
    rsprot_p4_1234(buf, combined_id);
}
