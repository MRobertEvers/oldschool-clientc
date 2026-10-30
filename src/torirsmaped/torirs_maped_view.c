/*
 * Views on the wire: SPAWN_VIEW, the camera keys' ownership rule, and the
 * STATE_VIEW_SELF question (torirs_maped_view.h has the contract).
 *
 * BUILD NOTE: compiled INTO torirs_maped.c (`#include "torirs_maped_view.c"`
 * at its end, beside the catalog), never linked on its own -- the hand-written
 * link lines that name torirs_maped.c then carry it with no edit. It uses that
 * unit's static send_frame / send_status_extra / broadcast_group, and every
 * file-scope name here starts with `view_` (statics) or `ToriRSMapEd_` (the
 * API) so nothing collides with it.
 */

#include "torirs_maped_view.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

void
ToriRSMapEd_SetViewSpawner(
    struct ToriRSMapEd* maped,
    const struct ToriRSMapEdViewSpawner* spawner)
{
    assert(maped);

    if( spawner )
    {
        assert(spawner->spawn);
        maped->view_spawner = *spawner;
        maped->capabilities |= TORIRSMAPED_CAP_SPAWN_VIEW;
    }
    else
    {
        memset(&maped->view_spawner, 0, sizeof(maped->view_spawner));
        maped->capabilities &= ~(uint32_t)TORIRSMAPED_CAP_SPAWN_VIEW;
    }
}

/* ---- the camera's text form ---------------------------------------------- */

/** One decimal number of tiles, whole or with a fraction, into fine units
 *  (rounded to the nearest 1/128). Advances *cursor past it. */
static int
view_parse_tiles(
    const char** cursor,
    int32_t* out_fine)
{
    const char* at = *cursor;
    long whole = 0;
    long numerator = 0;
    long denominator = 1;
    int digits = 0;

    while( *at >= '0' && *at <= '9' )
    {
        whole = whole * 10 + (*at - '0');
        if( whole > 16383 )
            return 0;
        at++;
        digits++;
    }
    if( digits == 0 )
        return 0;
    if( *at == '.' )
    {
        int fraction_digits = 0;
        at++;
        while( *at >= '0' && *at <= '9' )
        {
            /* Nine digits is far past 1/128's resolution; the rest only
             * rounds, so they are read and ignored. */
            if( fraction_digits < 9 )
            {
                numerator = numerator * 10 + (*at - '0');
                denominator *= 10;
            }
            fraction_digits++;
            at++;
        }
        if( fraction_digits == 0 )
            return 0;
    }
    *out_fine = (int32_t)(whole * 128 + (numerator * 128 * 2 + denominator) / (denominator * 2));
    *cursor = at;
    return 1;
}

static int
view_parse_whole(
    const char** cursor,
    long low,
    long high,
    int32_t* out)
{
    const char* at = *cursor;
    long value = 0;
    int digits = 0;

    while( *at >= '0' && *at <= '9' )
    {
        value = value * 10 + (*at - '0');
        if( value > high )
            return 0;
        at++;
        digits++;
    }
    if( digits == 0 || value < low )
        return 0;
    *out = (int32_t)value;
    *cursor = at;
    return 1;
}

int
ToriRSMapEd_CameraParse(
    const char* text,
    int32_t* out)
{
    const char* at = text;
    int32_t camera[TORIRSMAPED_CAMERA_VALUES];

    assert(text);
    assert(out);

    if( !view_parse_tiles(&at, &camera[TORIRSMAPED_CAMERA_X]) || *at++ != ',' )
        return 0;
    if( !view_parse_tiles(&at, &camera[TORIRSMAPED_CAMERA_Z]) || *at++ != ',' )
        return 0;
    if( !view_parse_whole(&at, 0, 3, &camera[TORIRSMAPED_CAMERA_LEVEL]) || *at++ != ',' )
        return 0;
    if( !view_parse_whole(&at, 0, 2047, &camera[TORIRSMAPED_CAMERA_YAW]) || *at++ != ',' )
        return 0;
    if( !view_parse_whole(&at, 0, 2047, &camera[TORIRSMAPED_CAMERA_PITCH]) || *at++ != ',' )
        return 0;
    if( !view_parse_whole(&at, 0, 65535, &camera[TORIRSMAPED_CAMERA_ZOOM]) || *at != '\0' )
        return 0;
    /* 16383 tiles and a fraction rounding up past it is still out of range. */
    if( camera[TORIRSMAPED_CAMERA_X] > 16383 * 128 || camera[TORIRSMAPED_CAMERA_Z] > 16383 * 128 )
        return 0;
    memcpy(out, camera, sizeof(camera));
    return 1;
}

/** Fine units as tiles: the whole part, then the exact binary fraction
 *  (k/128 = k * 0.0078125) with its trailing zeros dropped. */
static void
view_format_tiles(
    int32_t fine,
    char* out,
    size_t out_size)
{
    int32_t whole;
    int32_t fraction;

    if( fine < 0 )
        fine = 0;
    whole = fine / 128;
    fraction = fine % 128;
    if( fraction == 0 )
    {
        snprintf(out, out_size, "%d", (int)whole);
        return;
    }
    {
        char digits[8];
        int end;
        snprintf(digits, sizeof(digits), "%07ld", (long)fraction * 78125L);
        end = 7;
        while( end > 1 && digits[end - 1] == '0' )
            end--;
        digits[end] = '\0';
        snprintf(out, out_size, "%d.%s", (int)whole, digits);
    }
}

int
ToriRSMapEd_CameraFormat(
    const int32_t* camera,
    char* out,
    size_t out_size)
{
    char x[24];
    char z[24];

    assert(camera);
    assert(out);
    assert(out_size > 0);

    view_format_tiles(camera[TORIRSMAPED_CAMERA_X], x, sizeof(x));
    view_format_tiles(camera[TORIRSMAPED_CAMERA_Z], z, sizeof(z));
    return snprintf(
        out,
        out_size,
        "%s,%s,%d,%d,%d,%d",
        x,
        z,
        (int)camera[TORIRSMAPED_CAMERA_LEVEL],
        (int)camera[TORIRSMAPED_CAMERA_YAW],
        (int)camera[TORIRSMAPED_CAMERA_PITCH],
        (int)camera[TORIRSMAPED_CAMERA_ZOOM]);
}

/* ---- the server side ------------------------------------------------------ */

/** The session slot `session` holds: its connection index. */
static int
view_session_index(
    const struct ToriRSMapEd* maped,
    const struct ToriRSMapEdSession* session)
{
    for( int i = 0; i < TORIRSMAPED_SESSION_MAX; i++ )
    {
        if( maped->sessions[i] == session )
            return i;
    }
    /* Every session that can send a frame was registered by SessionInit. */
    assert(0 && "a session that is not in its server's table");
    return -1;
}

/**
 * STATE_SET's two views rules, ahead of the store. Returns 1 when the frame
 * was the VIEW_SELF question (answered here) or a camera key the sender does
 * not own (dropped here); 0 to let STATE_SET store and relay it as usual.
 */
static int
view_state_set_intercept(
    struct ToriRSMapEdSession* session,
    struct ToriRSMapEd* maped,
    uint32_t key)
{
    int index;
    int owner;

    if( key == TORIRSMAPED_STATE_VIEW_SELF )
    {
        uint8_t payload[12 + 4 * TORIRSMAPED_VIEW_SELF_VALUES];

        index = view_session_index(maped, session);
        maped->seq++;
        ToriRSMapEd_WriteU32(payload, maped->seq);
        ToriRSMapEd_WriteU32(payload + 4, TORIRSMAPED_STATE_VIEW_SELF);
        ToriRSMapEd_WriteU32(payload + 8, TORIRSMAPED_VIEW_SELF_VALUES);
        ToriRSMapEd_WriteU32(payload + 12, (uint32_t)index);
        ToriRSMapEd_WriteU32(payload + 16, ToriRSMapEd_CameraKey(index));
        ToriRSMapEd_WriteU32(payload + 20, session->client_id);
        send_frame(session, TORIRSMAPED_FACT_STATE, payload, sizeof(payload));
        return 1;
    }

    owner = ToriRSMapEd_CameraKeyIndex(key);
    if( owner < 0 )
        return 0;
    index = view_session_index(maped, session);
    if( owner != index )
    {
        TORIRS_LOG(
            "torirsmaped: connection %d published connection %d's camera key %u — dropped\n",
            index,
            owner,
            key);
        return 1;
    }
    return 0;
}

/**
 * A connection is going: forget its camera in its Client's store and tell the
 * rest of the Client, so a view following it stops and a late joiner's
 * STATE_SYNC never replays a camera nobody is flying. Called from
 * SessionFree while the session is still registered.
 */
static void
view_session_gone(
    struct ToriRSMapEd* maped,
    struct ToriRSMapEdSession* session)
{
    uint32_t key;
    int found = -1;

    if( session->client_id == 0 )
        return;
    key = ToriRSMapEd_CameraKey(view_session_index(maped, session));
    for( int i = 0; i < maped->state_count; i++ )
    {
        if( maped->state[i].client_id == session->client_id && maped->state[i].key == key )
        {
            found = i;
            break;
        }
    }
    if( found < 0 )
        return;

    /* Unordered store: the last entry fills the hole. */
    maped->state[found] = maped->state[maped->state_count - 1];
    maped->state_count--;

    {
        uint8_t payload[12];
        maped->seq++;
        ToriRSMapEd_WriteU32(payload, maped->seq);
        ToriRSMapEd_WriteU32(payload + 4, key);
        ToriRSMapEd_WriteU32(payload + 8, 0);
        for( int i = 0; i < TORIRSMAPED_SESSION_MAX; i++ )
        {
            struct ToriRSMapEdSession* other = maped->sessions[i];
            if( other && other != session && other->alive
                && other->client_id == session->client_id )
                send_frame(other, TORIRSMAPED_FACT_STATE, payload, sizeof(payload));
        }
    }
}

static void
handle_spawn_view(
    struct ToriRSMapEdSession* session,
    struct ToriRSMapEd* maped,
    const uint8_t* body,
    uint32_t length)
{
    int32_t camera[TORIRSMAPED_CAMERA_VALUES];
    enum EditorHost_Status status;
    int pid = 0;

    if( length != 4 * TORIRSMAPED_CAMERA_VALUES )
    {
        session->alive = 0;
        return;
    }
    for( int i = 0; i < TORIRSMAPED_CAMERA_VALUES; i++ )
        camera[i] = (int32_t)ToriRSMapEd_ReadU32(body + 4 * i);

    if( !maped->view_spawner.spawn )
    {
        send_status(session, TORIRSMAPED_REQ_SPAWN_VIEW, EDITOR_HOST_UNSUPPORTED);
        return;
    }
    /* A pose the viewer would refuse to boot with (out of range, negative)
     * is refused here, where the requester hears why. */
    {
        char text[96];
        int32_t back[TORIRSMAPED_CAMERA_VALUES];
        ToriRSMapEd_CameraFormat(camera, text, sizeof(text));
        if( !ToriRSMapEd_CameraParse(text, back) || memcmp(back, camera, sizeof(back)) != 0 )
        {
            send_status(session, TORIRSMAPED_REQ_SPAWN_VIEW, EDITOR_HOST_REFUSED);
            return;
        }
    }
    status = maped->view_spawner.spawn(maped->view_spawner.ctx, session->client_id, camera, &pid);
    send_status_extra(
        session,
        TORIRSMAPED_REQ_SPAWN_VIEW,
        status,
        status == EDITOR_HOST_OK ? (uint32_t)pid : 0);
}
