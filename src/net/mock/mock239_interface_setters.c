/* See mock239_interface_setters.h. */

#include "mock239_interface_setters.h"

#include <rsareabuf.h>

/*
 * Every field order and alternative transform below is transcribed literally
 * from 3rd/rsprot/gen/codec/encoders_239.h.  Similar-looking packets do not
 * establish a pattern in this protocol, so each encoder is stated separately.
 */

/* IfSetRotateSpeedEncoder:
 *   p2(ySpeed), p2Alt2(xSpeed), pCombinedIdAlt3(combinedId). */
void
mock239_encode_if_setrotatespeed(
    struct RSAreaBuf* buf,
    int combined_id,
    int x_speed,
    int y_speed)
{
    rsab_p2(buf, y_speed);
    rsab_p2_alt2(buf, x_speed);
    rsab_p4_alt3(buf, combined_id);
}

/* IfSetAngleEncoder:
 *   pCombinedIdAlt3(combinedId), p2(zoom), p2(angleX), p2Alt3(angleY). */
void
mock239_encode_if_setangle(
    struct RSAreaBuf* buf,
    int combined_id,
    int zoom,
    int angle_x,
    int angle_y)
{
    rsab_p4_alt3(buf, combined_id);
    rsab_p2(buf, zoom);
    rsab_p2(buf, angle_x);
    rsab_p2_alt3(buf, angle_y);
}

/* IfSetNpcHeadActiveEncoder:
 *   p2Alt2(index), pCombinedIdAlt2(combinedId). */
void
mock239_encode_if_setnpchead_active(
    struct RSAreaBuf* buf,
    int combined_id,
    int index)
{
    rsab_p2_alt2(buf, index);
    rsab_p4_alt2(buf, combined_id);
}

/* IfSetPlayerModelBaseColourEncoder:
 *   p1(index), p1(colour), pCombinedId(combinedId). */
void
mock239_encode_if_setplayermodel_basecolour(
    struct RSAreaBuf* buf,
    int combined_id,
    int index,
    int colour)
{
    rsab_p1(buf, index);
    rsab_p1(buf, colour);
    rsab_p4(buf, combined_id);
}

/* IfSetPlayerModelBodyTypeEncoder:
 *   pCombinedIdAlt2(combinedId), p1(bodyType). */
void
mock239_encode_if_setplayermodel_bodytype(
    struct RSAreaBuf* buf,
    int combined_id,
    int body_type)
{
    rsab_p4_alt2(buf, combined_id);
    rsab_p1(buf, body_type);
}

/* IfSetPlayerModelObjEncoder:
 *   pCombinedIdAlt2(combinedId), p4Alt3(obj). */
void
mock239_encode_if_setplayermodel_obj(
    struct RSAreaBuf* buf,
    int combined_id,
    int obj_id)
{
    rsab_p4_alt2(buf, combined_id);
    rsab_p4_alt3(buf, obj_id);
}

/* IfSetPlayerModelSelfEncoder:
 *   p1Alt3(copyObjs), pCombinedId(combinedId). */
void
mock239_encode_if_setplayermodel_self(
    struct RSAreaBuf* buf,
    int combined_id,
    int copy_objs)
{
    rsab_p1_alt3(buf, copy_objs ? 1 : 0);
    rsab_p4(buf, combined_id);
}
