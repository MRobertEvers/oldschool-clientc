#include "cmdbus.h"

#include <string.h>

/* 8-byte magic + u32 version + u32 reserved. */
static const char TRSCMD_MAGIC[8] = { 'T', 'R', 'S', 'C', 'M', 'D', '1', '\0' };
#define TRSCMD_VERSION 1u

void
CmdBus_Init(struct ToriRS_CmdBus* bus)
{
    CmdRing_Init(&bus->ring);
    bus->record = NULL;
}

int
CmdBus_Push(
    struct ToriRS_CmdBus* bus,
    uint32_t type,
    const void* payload,
    uint16_t length)
{
    if( !CmdRing_Push(&bus->ring, type, (const uint8_t*)payload, length) )
        return 0;

    if( bus->record )
    {
        struct ToriRS_CmdHeader header = { type, length };
        fwrite(&header, sizeof(header), 1, bus->record);
        if( length > 0 && payload )
            fwrite(payload, 1, length, bus->record);
    }
    return 1;
}

int
CmdBus_Pop(
    struct ToriRS_CmdBus* bus,
    struct ToriRS_CmdHeader* out_header,
    uint8_t* out_payload)
{
    return CmdRing_Pop(&bus->ring, out_header, out_payload);
}

int
CmdBus_RecordOpen(
    struct ToriRS_CmdBus* bus,
    const char* path)
{
    FILE* f = fopen(path, "wb");
    if( !f )
        return 0;

    uint32_t version = TRSCMD_VERSION;
    uint32_t reserved = 0;
    fwrite(TRSCMD_MAGIC, sizeof(TRSCMD_MAGIC), 1, f);
    fwrite(&version, sizeof(version), 1, f);
    fwrite(&reserved, sizeof(reserved), 1, f);

    bus->record = f;
    return 1;
}

void
CmdBus_RecordClose(struct ToriRS_CmdBus* bus)
{
    if( bus->record )
    {
        fclose(bus->record);
        bus->record = NULL;
    }
}

FILE*
CmdReplay_Open(const char* path)
{
    FILE* f = fopen(path, "rb");
    if( !f )
        return NULL;

    char magic[8];
    uint32_t version = 0;
    uint32_t reserved = 0;
    if( fread(magic, sizeof(magic), 1, f) != 1 ||
        memcmp(magic, TRSCMD_MAGIC, sizeof(magic)) != 0 ||
        fread(&version, sizeof(version), 1, f) != 1 || version != TRSCMD_VERSION ||
        fread(&reserved, sizeof(reserved), 1, f) != 1 )
    {
        fclose(f);
        return NULL;
    }
    return f;
}

int
CmdReplay_PumpFrame(
    FILE* f,
    struct ToriRS_CmdBus* bus,
    uint64_t* out_now_ms)
{
    struct ToriRS_CmdHeader header;
    uint8_t payload[TORIRS_CMD_MAX_PAYLOAD];
    int seen_frame = 0;

    for( ;; )
    {
        long pos = ftell(f);
        if( fread(&header, sizeof(header), 1, f) != 1 )
            break;
        if( header.length > TORIRS_CMD_MAX_PAYLOAD )
            break; /* corrupt record; stop replaying */
        if( header.length > 0 && fread(payload, 1, header.length, f) != header.length )
            break;

        if( header.type == TORIRS_CMD_FRAME )
        {
            if( seen_frame )
            {
                /* Next iteration's delimiter: rewind so the following pump
                 * starts on it. */
                fseek(f, pos, SEEK_SET);
                break;
            }
            seen_frame = 1;
            if( out_now_ms && header.length >= sizeof(uint64_t) )
                memcpy(out_now_ms, payload, sizeof(uint64_t));
        }

        /* Bypass CmdBus_Push so replay never tees back into a record file. */
        CmdRing_Push(&bus->ring, header.type, payload, header.length);
    }

    return seen_frame;
}

int
CmdBus_PushFrame(
    struct ToriRS_CmdBus* bus,
    uint64_t now_ms)
{
    struct ToriRS_CmdFrame cmd = { now_ms };
    return CmdBus_Push(bus, TORIRS_CMD_FRAME, &cmd, sizeof(cmd));
}

int
CmdBus_PushKey(
    struct ToriRS_CmdBus* bus,
    uint32_t type,
    uint8_t keycode)
{
    struct ToriRS_CmdKey cmd = { keycode };
    return CmdBus_Push(bus, type, &cmd, sizeof(cmd));
}

int
CmdBus_PushKeyEvent(
    struct ToriRS_CmdBus* bus,
    int32_t key_typed,
    int32_t key_pressed,
    uint8_t is_repeat)
{
    struct ToriRS_CmdKeyEvent cmd = { key_typed, key_pressed, is_repeat };
    return CmdBus_Push(bus, TORIRS_CMD_INPUT_KEY_EVENT, &cmd, sizeof(cmd));
}

int
CmdBus_PushOsrsKey(
    struct ToriRS_CmdBus* bus,
    int32_t osrs_key,
    uint8_t down,
    uint8_t pressed_edge)
{
    struct ToriRS_CmdOsrsKey cmd = { osrs_key, down, pressed_edge };
    return CmdBus_Push(bus, TORIRS_CMD_INPUT_OSRS_KEY, &cmd, sizeof(cmd));
}

int
CmdBus_PushMouseButton(
    struct ToriRS_CmdBus* bus,
    uint32_t type,
    uint8_t button,
    int16_t x,
    int16_t y)
{
    struct ToriRS_CmdMouseButton cmd = { button, x, y };
    return CmdBus_Push(bus, type, &cmd, sizeof(cmd));
}

int
CmdBus_PushMouseMove(
    struct ToriRS_CmdBus* bus,
    int16_t x,
    int16_t y)
{
    struct ToriRS_CmdMouseMove cmd = { x, y };
    return CmdBus_Push(bus, TORIRS_CMD_INPUT_MOUSE_MOVE, &cmd, sizeof(cmd));
}

int
CmdBus_PushMouseLeave(struct ToriRS_CmdBus* bus)
{
    return CmdBus_Push(bus, TORIRS_CMD_INPUT_MOUSE_LEAVE, NULL, 0);
}

int
CmdBus_PushMouseWheel(
    struct ToriRS_CmdBus* bus,
    int16_t wheel_y)
{
    struct ToriRS_CmdMouseWheel cmd = { wheel_y };
    return CmdBus_Push(bus, TORIRS_CMD_INPUT_MOUSE_WHEEL, &cmd, sizeof(cmd));
}

int
CmdBus_PushWindowResize(
    struct ToriRS_CmdBus* bus,
    int32_t width,
    int32_t height)
{
    struct ToriRS_CmdWindowResize cmd = { width, height };
    return CmdBus_Push(bus, TORIRS_CMD_WINDOW_RESIZE, &cmd, sizeof(cmd));
}

int
CmdBus_PushKeyboardInset(
    struct ToriRS_CmdBus* bus,
    int32_t bottom)
{
    struct ToriRS_CmdKeyboardInset cmd = { bottom };
    return CmdBus_Push(bus, TORIRS_CMD_KEYBOARD_INSET, &cmd, sizeof(cmd));
}

int
CmdBus_PushNetStatus(
    struct ToriRS_CmdBus* bus,
    int32_t status)
{
    struct ToriRS_CmdNetStatus cmd = { status };
    return CmdBus_Push(bus, TORIRS_CMD_NET_STATUS, &cmd, sizeof(cmd));
}

/* ---- host commands ------------------------------------------------------ */

int
CmdBus_PushUiOpenRoot(
    struct ToriRS_CmdBus* bus,
    int32_t interface_id)
{
    struct ToriRS_CmdUiOpenRoot cmd = { interface_id };
    return CmdBus_Push(bus, TORIRS_CMD_UI_OPEN_ROOT, &cmd, sizeof(cmd));
}

int
CmdBus_PushUiSetVar(
    struct ToriRS_CmdBus* bus,
    uint32_t type,
    int32_t id,
    int32_t value)
{
    struct ToriRS_CmdUiSetVar cmd = { id, value };
    assert(type == TORIRS_CMD_UI_SET_VARP || type == TORIRS_CMD_UI_SET_VARBIT);
    return CmdBus_Push(bus, type, &cmd, sizeof(cmd));
}

int
CmdBus_PushUiRunScript(
    struct ToriRS_CmdBus* bus,
    int32_t script_id,
    int32_t const* args,
    int argc)
{
    struct ToriRS_CmdUiRunScript cmd;

    assert(argc >= 0);
    assert(argc <= TORIRS_CMD_UI_RUNSCRIPT_MAX_ARGS);
    if( argc > 0 )
        assert(args);

    memset(&cmd, 0, sizeof(cmd));
    cmd.script_id = script_id;
    cmd.argc = argc;
    if( argc > 0 )
        memcpy(cmd.args, args, (size_t)argc * sizeof(cmd.args[0]));
    return CmdBus_Push(bus, TORIRS_CMD_UI_RUNSCRIPT, &cmd, CmdBus_UiRunScriptBytes(argc));
}

int
CmdBus_PushExecText(
    struct ToriRS_CmdBus* bus,
    char const* text)
{
    size_t length;

    assert(text);
    /* Not NUL-terminated on the wire, like NET_CONNECT: the header's length is
     * the string's length, so the drain must bound its copy by it. */
    length = strlen(text);
    if( length > TORIRS_CMD_MAX_PAYLOAD )
        return 0;
    return CmdBus_Push(bus, TORIRS_CMD_EXEC_TEXT, text, (uint16_t)length);
}
