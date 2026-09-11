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

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* RUNCLIENTSCRIPT's own headless harness (TORIRS_SIM_RUNSCRIPT) carries four,
 * and the packet path is variadic. Four covers every call a host has needed;
 * raising it costs only frame bytes. */
#define TORIRS_CMD_UI_RUNSCRIPT_MAX_ARGS 4

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
    /* No payload. The pointer stopped resting anywhere: the finger that was
     * the pointer has left the glass, or the cursor left the window. NOT a
     * move -- the last position stays, because the popup a tap opened is
     * anchored to it and a move off it would dismiss the popup. */
    TORIRS_CMD_INPUT_MOUSE_LEAVE = 25,

    /* -> ToriRS_Network (raw-byte semantics, v0 net ring parity) */
    TORIRS_CMD_NET_CONNECT = 32, /* "host:port" string, not NUL-terminated */
    TORIRS_CMD_NET_RECV = 33,    /* raw socket bytes */
    TORIRS_CMD_NET_STATUS = 34,  /* struct ToriRS_CmdNetStatus */

    /* -> App. The window changed size and the client canvas has to follow it:
     * the whole gameframe is laid out against the canvas, so this is a layout
     * event, not a presentation one. On the bus (rather than a direct call)
     * because a resize has to land in the same recorded/replayed stream as the
     * input that happened around it — a replay of a session that was resized
     * mid-way must resize at the same frame. */
    TORIRS_CMD_WINDOW_RESIZE = 48, /* struct ToriRS_CmdWindowResize */

    /* -> App. The OS soft keyboard covers the bottom of the canvas, or stopped
     * covering it. `bottom` is CANVAS rows (the platform owns the surface->
     * canvas letterbox, so it converts before pushing — the same rule its
     * touch mapping follows), 0 when the keyboard is away. A layout event for
     * the reason WINDOW_RESIZE is: the mobile gameframe slides its chatbox
     * above the keyboard, and a replayed session must slide at the same
     * frame. Desktop backends never push it. */
    TORIRS_CMD_KEYBOARD_INSET = 49, /* struct ToriRS_CmdKeyboardInset */

    /* -> App. What the device itself reports about its power and its link:
     * a battery percentage, whether it is charging, and which network it is
     * on. The CS2 scripts ask for all three (the MOBILE_BATTERYLEVEL,
     * MOBILE_BATTERYCHARGING and MOBILE_WIFIAVAILABLE opcodes), so the answers
     * have to come from the platform rather than from a constant. Pushed on
     * change, not per frame; desktop backends never push it and the host keeps
     * its "plugged in, on wifi" defaults. */
    TORIRS_CMD_DEVICE_STATUS = 50, /* struct ToriRS_CmdDeviceStatus */
    /* -> App. The persistent application-chrome rail was pressed while the
     * page executor was collapsed, so there is no executor intent queue to
     * carry it. No payload: it toggles the one shared plugin pane. */
    TORIRS_CMD_PLUGIN_CHROME_TOGGLE = 51,

    /* -> App, from a HOST rather than from a device.
     *
     * The client is embedded — in a browser tab, under an editor, under a test
     * — and the thing embedding it wants to say "open interface 600" or "set
     * varp 300 to 100", which no sequence of clicks expresses. The TORIRS_SIM_*
     * environment harnesses answer the same need for a native run, but they are
     * read once before the loop and several of them call App_BootWait, which
     * spins on TaskRunner_Step and never returns against an asynchronous IO
     * backend (docs/web_build.md). A host that is still there after boot needs a
     * seam that is drained per iteration, and this ring already is one.
     *
     * On the bus rather than as direct calls for the reason WINDOW_RESIZE is:
     * the ring's byte layout is the record format, so a host-driven session
     * records and replays with its commands interleaved with the input around
     * them, at the same frames. */
    TORIRS_CMD_UI_OPEN_ROOT = 64,  /* struct ToriRS_CmdUiOpenRoot */
    TORIRS_CMD_UI_SET_VARP = 65,   /* struct ToriRS_CmdUiSetVar */
    TORIRS_CMD_UI_SET_VARBIT = 66, /* struct ToriRS_CmdUiSetVar */
    TORIRS_CMD_UI_RUNSCRIPT = 67,  /* struct ToriRS_CmdUiRunScript */
    TORIRS_CMD_EXEC_TEXT = 68,     /* debugproc text, no leading "::", not NUL-terminated */
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

struct ToriRS_CmdWindowResize
{
    int32_t width;
    int32_t height;
};

struct ToriRS_CmdKeyboardInset
{
    /** Canvas rows covered at the bottom; 0 = keyboard away. */
    int32_t bottom;
};

/** @see TORIRS_CMD_DEVICE_STATUS::network_kind. Same values as the host's
 *  RS_CS2_NETWORK_*, which is what the app forwards them to. */
enum ToriRS_CmdNetworkKind
{
    TORIRS_CMD_NETWORK_NONE = 0,
    TORIRS_CMD_NETWORK_WIFI = 1,
    TORIRS_CMD_NETWORK_CELLULAR = 2
};

struct ToriRS_CmdDeviceStatus
{
    /** 0..100. */
    int32_t battery_percent;
    /** Nonzero while the battery is charging. */
    int32_t battery_charging;
    /** enum ToriRS_CmdNetworkKind */
    int32_t network_kind;
};

struct ToriRS_CmdUiOpenRoot
{
    int32_t interface_id;
};

/** Both UI_SET_VARP and UI_SET_VARBIT: same shape, different id space. */
struct ToriRS_CmdUiSetVar
{
    int32_t id;
    int32_t value;
};

/* RUNCLIENTSCRIPT's shape, minus the strings: a script id and its int
 * arguments. `argc` is how many of `args` are meant; the frame is sized to
 * carry exactly that many, so a four-argument call costs no more than four. */
struct ToriRS_CmdUiRunScript
{
    int32_t script_id;
    int32_t argc;
    int32_t args[TORIRS_CMD_UI_RUNSCRIPT_MAX_ARGS];
};
#pragma pack(pop)

/** Bytes of a run-script frame carrying `argc` arguments. */
static inline uint16_t
CmdBus_UiRunScriptBytes(int argc)
{
    assert(argc >= 0);
    assert(argc <= TORIRS_CMD_UI_RUNSCRIPT_MAX_ARGS);
    return (uint16_t)(offsetof(struct ToriRS_CmdUiRunScript, args) + (size_t)argc * sizeof(int32_t));
}

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

/* Room for a `length`-byte payload right now? See CmdRing_CanPush: a socket
 * producer must ask before it consumes bytes it cannot hand over. */
static inline int
CmdBus_CanPush(
    const struct ToriRS_CmdBus* bus,
    uint16_t length)
{
    return CmdRing_CanPush(&bus->ring, length);
}

/* Bytes still free, headers included — for producers sizing a multi-message
 * batch that has to be admitted or refused as a whole. */
static inline uint32_t
CmdBus_FreeBytes(const struct ToriRS_CmdBus* bus)
{
    return CmdRing_FreeBytes(&bus->ring);
}

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

/** The pointer left the surface -- @see TORIRS_CMD_INPUT_MOUSE_LEAVE. */
int
CmdBus_PushMouseLeave(struct ToriRS_CmdBus* bus);

int
CmdBus_PushMouseWheel(
    struct ToriRS_CmdBus* bus,
    int16_t wheel_y);

int
CmdBus_PushWindowResize(
    struct ToriRS_CmdBus* bus,
    int32_t width,
    int32_t height);

int
CmdBus_PushKeyboardInset(
    struct ToriRS_CmdBus* bus,
    int32_t bottom);

int
CmdBus_PushNetStatus(
    struct ToriRS_CmdBus* bus,
    int32_t status);

/** The device's power and link, @see TORIRS_CMD_DEVICE_STATUS. */
int
CmdBus_PushDeviceStatus(
    struct ToriRS_CmdBus* bus,
    int32_t battery_percent,
    int32_t battery_charging,
    int32_t network_kind);

/** Persistent application-chrome rail -> the one shared pane. */
int
CmdBus_PushPluginChromeToggle(struct ToriRS_CmdBus* bus);

/* ---- host commands ------------------------------------------------------ */

int
CmdBus_PushUiOpenRoot(
    struct ToriRS_CmdBus* bus,
    int32_t interface_id);

/** `type` is TORIRS_CMD_UI_SET_VARP or TORIRS_CMD_UI_SET_VARBIT. */
int
CmdBus_PushUiSetVar(
    struct ToriRS_CmdBus* bus,
    uint32_t type,
    int32_t id,
    int32_t value);

/** `args` may be NULL only when argc is 0. */
int
CmdBus_PushUiRunScript(
    struct ToriRS_CmdBus* bus,
    int32_t script_id,
    int32_t const* args,
    int argc);

/** `text` is the command WITHOUT the leading "::", matching App_SendCommand. */
int
CmdBus_PushExecText(
    struct ToriRS_CmdBus* bus,
    char const* text);

#endif
