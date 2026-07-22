#ifndef SRC_CMD_CMDBUS_H
#define SRC_CMD_CMDBUS_H

/**
 * The unified command bus: every input to the game — SDL keyboard/mouse and
 * raw network bytes — is pushed here as a [type][length][payload] frame and
 * drained once per loop iteration into its subsystem (ToriRS_Input for
 * TORIRS_CMD_INPUT_*, ToriRS_Network for TORIRS_CMD_NET_*).
 *
 * The ring's byte layout IS the record file format, so a recorded session is
 * simply the pushed stream teed to disk, and replay is a file-read loop that
 * pushes the same frames back. TORIRS_CMD_FRAME delimits loop iterations and
 * carries the timestamp used for that iteration, making replays reproduce
 * tick catch-up exactly.
 */

#include "cmdring.h"

#include <stdint.h>
#include <stdio.h>

enum ToriRS_CmdType
{
    TORIRS_CMD_NONE = 0,

    /* Frame delimiter; payload struct ToriRS_CmdFrame. */
    TORIRS_CMD_FRAME = 1,

    /* -> ToriRS_Input */
    TORIRS_CMD_INPUT_KEY_DOWN = 16,    /* struct ToriRS_CmdKey */
    TORIRS_CMD_INPUT_KEY_UP = 17,      /* struct ToriRS_CmdKey */
    TORIRS_CMD_INPUT_KEY_EVENT = 18,   /* struct ToriRS_CmdKeyEvent */
    TORIRS_CMD_INPUT_OSRS_KEY = 19,    /* struct ToriRS_CmdOsrsKey */
    TORIRS_CMD_INPUT_MOUSE_DOWN = 20,  /* struct ToriRS_CmdMouseButton */
    TORIRS_CMD_INPUT_MOUSE_UP = 21,    /* struct ToriRS_CmdMouseButton */
    TORIRS_CMD_INPUT_MOUSE_MOVE = 22,  /* struct ToriRS_CmdMouseMove */
    TORIRS_CMD_INPUT_MOUSE_WHEEL = 23, /* struct ToriRS_CmdMouseWheel */
    TORIRS_CMD_INPUT_CLEAR_KEYS = 24,  /* no payload (focus loss) */

    /* -> ToriRS_Network (raw-byte semantics, v0 net ring parity) */
    TORIRS_CMD_NET_CONNECT = 32, /* "host:port" string, not NUL-terminated */
    TORIRS_CMD_NET_RECV = 33,    /* raw socket bytes */
    TORIRS_CMD_NET_STATUS = 34,  /* struct ToriRS_CmdNetStatus */
};

#pragma pack(push, 1)
struct ToriRS_CmdFrame
{
    uint64_t now_ms;
};

struct ToriRS_CmdKey
{
    uint8_t keycode; /* enum LibToriRS_KeyCode */
};

struct ToriRS_CmdKeyEvent
{
    int32_t key_typed;
    int32_t key_pressed;
    uint8_t is_repeat;
};

struct ToriRS_CmdOsrsKey
{
    int32_t osrs_key;
    uint8_t down;
    uint8_t pressed_edge;
};

struct ToriRS_CmdMouseButton
{
    uint8_t button; /* enum LibToriRS_MouseButton */
    int16_t x;
    int16_t y;
};

struct ToriRS_CmdMouseMove
{
    int16_t x;
    int16_t y;
};

struct ToriRS_CmdMouseWheel
{
    int16_t wheel_y;
};

struct ToriRS_CmdNetStatus
{
    int32_t status; /* enum ToriRS_NetConnStatus */
};
#pragma pack(pop)

struct ToriRS_CmdBus
{
    struct ToriRS_CmdRing ring;
    FILE* record; /* NULL = recording off */
};

void
CmdBus_Init(struct ToriRS_CmdBus* bus);

/* Push a command; tees header+payload to the record file when open.
 * Returns 1 on success, 0 when the ring is full or length exceeds
 * TORIRS_CMD_MAX_PAYLOAD. */
int
CmdBus_Push(
    struct ToriRS_CmdBus* bus,
    uint32_t type,
    const void* payload,
    uint16_t length);

/* out_payload must hold TORIRS_CMD_MAX_PAYLOAD bytes. */
int
CmdBus_Pop(
    struct ToriRS_CmdBus* bus,
    struct ToriRS_CmdHeader* out_header,
    uint8_t* out_payload);

/* ---- record / replay ---------------------------------------------------- */

/* Opens `path` and writes the file header. Returns 1 on success. */
int
CmdBus_RecordOpen(
    struct ToriRS_CmdBus* bus,
    const char* path);

void
CmdBus_RecordClose(struct ToriRS_CmdBus* bus);

/* Opens a recording for replay, validating the file header.
 * Returns NULL on open/magic failure. */
FILE*
CmdReplay_Open(const char* path);

/**
 * Reads one loop iteration's commands from `f` into the bus: the leading
 * TORIRS_CMD_FRAME (its now_ms returned via out_now_ms) plus every command up
 * to — but not including — the next FRAME. The FRAME command itself is also
 * pushed onto the bus; drains ignore it.
 *
 * Bypasses the record tee (replaying must not re-record). Returns 1 when a
 * frame was pumped, 0 at end of file.
 */
int
CmdReplay_PumpFrame(
    FILE* f,
    struct ToriRS_CmdBus* bus,
    uint64_t* out_now_ms);

/* ---- typed push helpers -------------------------------------------------- */

int
CmdBus_PushFrame(
    struct ToriRS_CmdBus* bus,
    uint64_t now_ms);

int
CmdBus_PushKey(
    struct ToriRS_CmdBus* bus,
    uint32_t type, /* TORIRS_CMD_INPUT_KEY_DOWN or _UP */
    uint8_t keycode);

int
CmdBus_PushKeyEvent(
    struct ToriRS_CmdBus* bus,
    int32_t key_typed,
    int32_t key_pressed,
    uint8_t is_repeat);

int
CmdBus_PushOsrsKey(
    struct ToriRS_CmdBus* bus,
    int32_t osrs_key,
    uint8_t down,
    uint8_t pressed_edge);

int
CmdBus_PushMouseButton(
    struct ToriRS_CmdBus* bus,
    uint32_t type, /* TORIRS_CMD_INPUT_MOUSE_DOWN or _UP */
    uint8_t button,
    int16_t x,
    int16_t y);

int
CmdBus_PushMouseMove(
    struct ToriRS_CmdBus* bus,
    int16_t x,
    int16_t y);

int
CmdBus_PushMouseWheel(
    struct ToriRS_CmdBus* bus,
    int16_t wheel_y);

int
CmdBus_PushNetStatus(
    struct ToriRS_CmdBus* bus,
    int32_t status);

#endif
