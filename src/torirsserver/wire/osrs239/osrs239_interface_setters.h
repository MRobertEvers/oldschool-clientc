#ifndef SRC_TORIRSSERVER_WIRE_OSRS239_OSRS239_INTERFACE_SETTERS_H
#define SRC_TORIRSSERVER_WIRE_OSRS239_OSRS239_INTERFACE_SETTERS_H

/*
 * Fixed-body revision-239 interface setters whose exact bodies need focused
 * coverage.  Keeping these bodies in a small codec makes their byte order
 * independently testable; torirs_server_wire.c's revision vtable delegates to these
 * functions one-for-one.
 */

struct RsprotBuf;

void
osrs239_encode_if_setrotatespeed(
    struct RsprotBuf* buf,
    int combined_id,
    int x_speed,
    int y_speed);

void
osrs239_encode_if_setangle(
    struct RsprotBuf* buf,
    int combined_id,
    int zoom,
    int angle_x,
    int angle_y);

void
osrs239_encode_if_setposition(
    struct RsprotBuf* buf,
    int combined_id,
    int x,
    int y);

void
osrs239_encode_if_setnpchead_active(
    struct RsprotBuf* buf,
    int combined_id,
    int index);

void
osrs239_encode_if_setplayermodel_basecolour(
    struct RsprotBuf* buf,
    int combined_id,
    int index,
    int colour);

void
osrs239_encode_if_setplayermodel_bodytype(
    struct RsprotBuf* buf,
    int combined_id,
    int body_type);

void
osrs239_encode_if_setplayermodel_obj(
    struct RsprotBuf* buf,
    int combined_id,
    int obj_id);

void
osrs239_encode_if_setplayermodel_self(
    struct RsprotBuf* buf,
    int combined_id,
    int copy_objs);

#endif
