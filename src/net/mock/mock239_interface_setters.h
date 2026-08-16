#ifndef SRC_NET_MOCK_MOCK239_INTERFACE_SETTERS_H
#define SRC_NET_MOCK_MOCK239_INTERFACE_SETTERS_H

/*
 * Fixed-body revision-239 interface setters whose exact bodies need focused
 * coverage.  Keeping these bodies in a small codec makes their byte order
 * independently testable; mock230_wire.c's revision vtable delegates to these
 * functions one-for-one.
 */

struct RSAreaBuf;

void
mock239_encode_if_setrotatespeed(
    struct RSAreaBuf* buf,
    int combined_id,
    int x_speed,
    int y_speed);

void
mock239_encode_if_setangle(
    struct RSAreaBuf* buf,
    int combined_id,
    int zoom,
    int angle_x,
    int angle_y);

void
mock239_encode_if_setposition(
    struct RSAreaBuf* buf,
    int combined_id,
    int x,
    int y);

void
mock239_encode_if_setnpchead_active(
    struct RSAreaBuf* buf,
    int combined_id,
    int index);

void
mock239_encode_if_setplayermodel_basecolour(
    struct RSAreaBuf* buf,
    int combined_id,
    int index,
    int colour);

void
mock239_encode_if_setplayermodel_bodytype(
    struct RSAreaBuf* buf,
    int combined_id,
    int body_type);

void
mock239_encode_if_setplayermodel_obj(
    struct RSAreaBuf* buf,
    int combined_id,
    int obj_id);

void
mock239_encode_if_setplayermodel_self(
    struct RSAreaBuf* buf,
    int combined_id,
    int copy_objs);

#endif
