/*
 * The agent control server (agent_server.h): the loopback listener, the
 * connections, the line protocol, request dispatch, and the input-script
 * runner every pointer and keyboard verb is made of.
 *
 * The verbs themselves are agent_verbs_core.c, agent_verbs_editor.c and
 * agent_verbs_game.c; this file names no App field. It is handed the App as a
 * pointer and passes it through.
 */

#include "agent/agent_server.h"

#include "log/torirs_log.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#if !defined(__EMSCRIPTEN__)

/* Sockets as the embedded editor server has them
 * (torirsmaped/torirs_maped_net.c): winsock on Windows, BSD sockets elsewhere,
 * a socket an int either way. winsock2.h comes before every header that could
 * pull in windows.h, which would bring the older winsock.h with it. */
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#define agent_net_errno() WSAGetLastError()
/* Nothing to read or room to write now; come back next frame. */
#define AGENT_NET_RETRY(error) ((error) == WSAEWOULDBLOCK || (error) == WSAEINTR)
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#define agent_net_errno() errno
#define AGENT_NET_RETRY(error) ((error) == EAGAIN || (error) == EWOULDBLOCK || (error) == EINTR)
#endif

#include "agent/agent_json.h"
#include "agent/agent_verbs.h"
#include "cmd/cmdbus.h"
#include "input/torirs_input.h"
#include "input/torirs_keymap.h"
#include "mem/torirs_mem.h"
#include "platform/platform_window.h"
#include "ui/uitree_layout.h"

#include <stdarg.h>
#include <stdio.h>

/** Connections at once: an agent, its helper, a person's shell. */
#define AGENT_CONNS_MAX 8
/** The longest request line. `type` of a page of text is a few KB. */
#define AGENT_LINE_MAX (1u << 20)
/** Reply backlog one connection may hold before it is dropped: the `ui` of a
 *  full editor is a few hundred KB, so reaching this means the peer stopped
 *  reading. */
#define AGENT_OUT_MAX (32u << 20)
/** Requests one connection may have answered in one frame, so a client that
 *  floods cheap verbs cannot hold the frame. */
#define AGENT_REQUESTS_PER_FRAME 64
/** Frames left to run between a script's last push and its reply: the frame
 *  that takes the input, and the frame that draws what it caused. */
#define AGENT_SCRIPT_TAIL_FRAMES 2

struct AgentConn
{
    int open;
    int fd;
    int dead;
    /** The peer finished sending (an orderly close of its half). What it
     *  already sent is still answered: `echo '{...}' | nc` closes its write
     *  side at once and reads the reply after. */
    int eof;
    struct AgentBuf in;
    struct AgentBuf out;
    struct AgentJsonWriter writer;
    /** The id of the request being answered, as JSON text ("7", "\"a\"",
     *  "null"), and whether its reply is open / written. */
    struct AgentBuf id;
    int replying;
    int replied;
    struct AgentOp op;
    /** Keys and buttons this connection holds down: through the `key` and
     *  `mouse` verbs' "down" (they outlive the request), and through the
     *  script that is running (let go when it ends, however it ends). */
    unsigned char keys_verb[TORIRSK_COUNT];
    unsigned char keys_script[TORIRSK_COUNT];
    unsigned buttons_verb;
    unsigned buttons_script;
};

struct AgentServer
{
    int fd;
    int port;
    char port_file[1024];
    long frame;
    struct AgentConn conns[AGENT_CONNS_MAX];
    /** The connection whose op holds the input lane, or NULL. */
    struct AgentConn* input_owner;
    /** Where the last injected pointer event put the pointer. */
    int pointer_x;
    int pointer_y;
    /** A push found the command ring full during this frame's scripts. */
    int push_failed;
};

/* ---- sockets -------------------------------------------------------------------- */

static void
agent_sock_close(int fd)
{
#ifdef _WIN32
    closesocket((SOCKET)fd);
#else
    close(fd);
#endif
}

/** A frame's poll must never block in accept() or recv(). */
static void
agent_sock_nonblocking(int fd)
{
#ifdef _WIN32
    u_long on = 1;
    ioctlsocket((SOCKET)fd, FIONBIO, &on);
#else
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
#endif
}

/** The last socket error, for a person ("10048" means little). */
static const char*
agent_sock_why(void)
{
#ifdef _WIN32
    static char text[160];
    int const error = WSAGetLastError();
    if( !FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, (DWORD)error, 0, text,
            sizeof(text), NULL) )
        snprintf(text, sizeof(text), "winsock error %d", error);
    for( char* scan = text; *scan; scan++ )
        if( *scan == '\r' || *scan == '\n' )
            *scan = '\0';
    return text;
#else
    return strerror(errno);
#endif
}

/** send() that never raises SIGPIPE: an agent that hangs up mid-reply must
 *  not take the client down with it. */
static int
agent_sock_send(
    int fd,
    const char* bytes,
    size_t count)
{
#if defined(MSG_NOSIGNAL)
    return (int)send(fd, bytes, count, MSG_NOSIGNAL);
#else
    return (int)send(fd, bytes, (int)count, 0);
#endif
}

/* ---- the surface ---------------------------------------------------------------- */

void
Agent_SurfaceSize(
    int* out_w,
    int* out_h)
{
    assert(out_w);
    assert(out_h);
    *out_w = UITREE_LAYOUT_ROOT_W;
    *out_h = UITREE_LAYOUT_ROOT_H;
}

int
Agent_NeedOnSurface(
    struct AgentCtx* ctx,
    int x,
    int y)
{
    int w;
    int h;

    assert(ctx);
    Agent_SurfaceSize(&w, &h);
    if( x >= 0 && y >= 0 && x < w && y < h )
        return 1;
    Agent_ReplyError(ctx, "(%d, %d) is off the %d x %d surface: every x and y is in the pixels hello's surface states",
        x, y, w, h);
    return 0;
}

/* ---- verbs ------------------------------------------------------------------------ */

static const struct AgentVerb* const g_agent_verb_tables[] = {
    g_agent_verbs_core,
    g_agent_verbs_editor,
    g_agent_verbs_game,
};

const struct AgentVerb*
Agent_VerbAt(int index)
{
    assert(index >= 0);
    for( size_t t = 0; t < sizeof(g_agent_verb_tables) / sizeof(g_agent_verb_tables[0]); t++ )
        for( const struct AgentVerb* verb = g_agent_verb_tables[t]; verb->name; verb++ )
            if( index-- == 0 )
                return verb;
    return NULL;
}

const struct AgentVerb*
Agent_FindVerb(const char* name)
{
    assert(name);
    for( int i = 0;; i++ )
    {
        const struct AgentVerb* verb = Agent_VerbAt(i);
        if( !verb )
            return NULL;
        if( strcmp(verb->name, name) == 0 )
            return verb;
    }
}

/** Is `key` one of the space-separated names in `list`? */
static int
agent_name_in_list(
    const char* list,
    const char* key)
{
    size_t const length = strlen(key);

    for( const char* at = list; *at; )
    {
        const char* end = strchr(at, ' ');
        size_t const span = end ? (size_t)(end - at) : strlen(at);
        if( span == length && memcmp(at, key, length) == 0 )
            return 1;
        if( !end )
            break;
        at = end + 1;
    }
    return 0;
}

/* ---- replies ------------------------------------------------------------------------ */

struct AgentJsonWriter*
Agent_ReplyBegin(struct AgentCtx* ctx)
{
    struct AgentConn* conn;

    assert(ctx);
    conn = ctx->conn;
    assert(conn);
    /* One reply per request. */
    assert(!conn->replied);
    assert(!conn->replying);
    conn->replying = 1;
    AgentJsonW_Init(&conn->writer, &conn->out);
    AgentJsonW_ObjBegin(&conn->writer);
    AgentJsonW_Key(&conn->writer, "id");
    AgentJsonW_Raw(&conn->writer, conn->id.data);
    AgentJsonW_KBool(&conn->writer, "ok", 1);
    return &conn->writer;
}

void
Agent_ReplyEnd(struct AgentCtx* ctx)
{
    struct AgentConn* conn;

    assert(ctx);
    conn = ctx->conn;
    assert(conn);
    assert(conn->replying);
    AgentJsonW_ObjEnd(&conn->writer);
    assert(conn->writer.depth == 0);
    AgentBuf_Append(&conn->out, "\n", 1);
    conn->replying = 0;
    conn->replied = 1;
}

void
Agent_ReplyOk(struct AgentCtx* ctx)
{
    (void)Agent_ReplyBegin(ctx);
    Agent_ReplyEnd(ctx);
}

void
Agent_ReplyError(
    struct AgentCtx* ctx,
    const char* format,
    ...)
{
    struct AgentConn* conn;
    char text[1024];
    va_list args;

    assert(ctx);
    assert(format);
    conn = ctx->conn;
    assert(conn);
    assert(!conn->replied);
    assert(!conn->replying);
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);

    AgentJsonW_Init(&conn->writer, &conn->out);
    AgentJsonW_ObjBegin(&conn->writer);
    AgentJsonW_Key(&conn->writer, "id");
    AgentJsonW_Raw(&conn->writer, conn->id.data);
    AgentJsonW_KBool(&conn->writer, "ok", 0);
    AgentJsonW_KStr(&conn->writer, "error", text);
    AgentJsonW_ObjEnd(&conn->writer);
    AgentBuf_Append(&conn->out, "\n", 1);
    conn->replied = 1;
}

/* ---- arguments ------------------------------------------------------------------------ */

static const char*
agent_type_name(int type)
{
    static const char* const k_names[] = { "null", "true or false", "a number", "a string", "an array" };
    assert(type >= 0);
    assert(type <= AGENT_JSON_ARRAY);
    return k_names[type];
}

/** A NUMBER value as an int: 1, or 0 when it is not a whole number an int holds. */
static int
agent_value_int(
    const struct AgentJsonValue* value,
    int* out)
{
    if( value->type != AGENT_JSON_NUMBER )
        return 0;
    if( value->number < -2147483648.0 || value->number > 2147483647.0 )
        return 0;
    if( value->number != (double)(int)value->number )
        return 0;
    *out = (int)value->number;
    return 1;
}

int
Agent_ArgInt(
    struct AgentCtx* ctx,
    const char* key,
    int* out)
{
    const struct AgentJsonValue* value;

    assert(ctx);
    assert(ctx->request);
    assert(key);
    assert(out);
    value = AgentJson_Get(ctx->request, key);
    if( !value )
        return 0;
    if( !agent_value_int(value, out) )
    {
        Agent_ReplyError(ctx, "\"%s\" must be a whole number, not %s", key,
            value->type == AGENT_JSON_NUMBER ? "a fraction" : agent_type_name(value->type));
        return -1;
    }
    return 1;
}

int
Agent_ArgBool(
    struct AgentCtx* ctx,
    const char* key,
    int* out)
{
    const struct AgentJsonValue* value;

    assert(ctx);
    assert(ctx->request);
    assert(key);
    assert(out);
    value = AgentJson_Get(ctx->request, key);
    if( !value )
        return 0;
    if( value->type != AGENT_JSON_BOOL )
    {
        Agent_ReplyError(ctx, "\"%s\" must be true or false, not %s", key, agent_type_name(value->type));
        return -1;
    }
    *out = value->boolean;
    return 1;
}

int
Agent_ArgString(
    struct AgentCtx* ctx,
    const char* key,
    const char** out)
{
    const struct AgentJsonValue* value;

    assert(ctx);
    assert(ctx->request);
    assert(key);
    assert(out);
    value = AgentJson_Get(ctx->request, key);
    if( !value )
        return 0;
    if( value->type != AGENT_JSON_STRING )
    {
        Agent_ReplyError(ctx, "\"%s\" must be a string, not %s", key, agent_type_name(value->type));
        return -1;
    }
    *out = value->string;
    return 1;
}

int
Agent_ArgInts(
    struct AgentCtx* ctx,
    const char* key,
    int* out,
    int count)
{
    const struct AgentJsonValue* value;

    assert(ctx);
    assert(ctx->request);
    assert(key);
    assert(out);
    assert(count > 0);
    value = AgentJson_Get(ctx->request, key);
    if( !value )
        return 0;
    if( value->type != AGENT_JSON_ARRAY || value->count != count )
    {
        Agent_ReplyError(ctx, "\"%s\" must be an array of %d whole numbers", key, count);
        return -1;
    }
    for( int i = 0; i < count; i++ )
        if( !agent_value_int(AgentJson_At(ctx->request, value, i), &out[i]) )
        {
            Agent_ReplyError(ctx, "\"%s\" must be an array of %d whole numbers", key, count);
            return -1;
        }
    return 1;
}

int
Agent_NeedInt(
    struct AgentCtx* ctx,
    const char* key,
    int* out)
{
    int const got = Agent_ArgInt(ctx, key, out);
    if( got == 0 )
        Agent_ReplyError(ctx, "\"%s\" is missing: it is a whole number", key);
    return got == 1;
}

int
Agent_NeedString(
    struct AgentCtx* ctx,
    const char* key,
    const char** out)
{
    int const got = Agent_ArgString(ctx, key, out);
    if( got == 0 )
        Agent_ReplyError(ctx, "\"%s\" is missing: it is a string", key);
    return got == 1;
}

int
Agent_NeedInts(
    struct AgentCtx* ctx,
    const char* key,
    int* out,
    int count)
{
    int const got = Agent_ArgInts(ctx, key, out, count);
    if( got == 0 )
        Agent_ReplyError(ctx, "\"%s\" is missing: it is an array of %d whole numbers", key, count);
    return got == 1;
}

int
Agent_ArgMods(
    struct AgentCtx* ctx,
    unsigned* out_mods)
{
    const struct AgentJsonValue* value;

    assert(ctx);
    assert(ctx->request);
    assert(out_mods);
    *out_mods = 0;
    value = AgentJson_Get(ctx->request, "mods");
    if( !value )
        return 0;
    if( value->type != AGENT_JSON_ARRAY )
    {
        Agent_ReplyError(ctx, "\"mods\" must be an array of \"shift\", \"ctrl\", \"alt\"");
        return -1;
    }
    for( int i = 0; i < value->count; i++ )
    {
        const struct AgentJsonValue* item = AgentJson_At(ctx->request, value, i);
        if( item->type == AGENT_JSON_STRING && strcmp(item->string, "shift") == 0 )
            *out_mods |= AGENT_MOD_SHIFT;
        else if( item->type == AGENT_JSON_STRING
                 && (strcmp(item->string, "ctrl") == 0 || strcmp(item->string, "cmd") == 0) )
            *out_mods |= AGENT_MOD_CTRL;
        else if( item->type == AGENT_JSON_STRING && strcmp(item->string, "alt") == 0 )
            *out_mods |= AGENT_MOD_ALT;
        else
        {
            Agent_ReplyError(ctx, "\"mods\" must be an array of \"shift\", \"ctrl\", \"alt\"");
            return -1;
        }
    }
    return 1;
}

int
Agent_ArgButton(
    struct AgentCtx* ctx,
    int* out_button)
{
    const char* name = NULL;
    int const got = Agent_ArgString(ctx, "button", &name);

    assert(out_button);
    if( got <= 0 )
        return got;
    if( strcmp(name, "left") == 0 )
        *out_button = TORIRSM_LEFT;
    else if( strcmp(name, "right") == 0 )
        *out_button = TORIRSM_RIGHT;
    else if( strcmp(name, "middle") == 0 )
        *out_button = TORIRSM_MIDDLE;
    else
    {
        Agent_ReplyError(ctx, "\"button\" is \"left\", \"right\" or \"middle\", not \"%s\"", name);
        return -1;
    }
    return 1;
}

/* ---- injected input ------------------------------------------------------------------
 *
 * What a key press, a typed character, a pointer move, a button and a wheel
 * notch each push is the window procedure's (platform/platform_win32gdi.c
 * wnd_proc), command for command.
 */

static const struct
{
    unsigned bit;
    enum LibToriRS_KeyCode key;
} k_agent_mods[] = {
    { AGENT_MOD_CTRL, TORIRSK_CTRL },
    { AGENT_MOD_SHIFT, TORIRSK_SHIFT },
    { AGENT_MOD_ALT, TORIRSK_ALT },
};

static void
agent_pushed(
    struct AgentCtx* ctx,
    int accepted)
{
    if( !accepted )
        ctx->server->push_failed = 1;
}

static int
agent_conn_key_down(
    const struct AgentConn* conn,
    int key)
{
    return conn->keys_verb[key] || conn->keys_script[key];
}

/**
 * The character a key types on a US keyboard, or 0: what WM_CHAR would carry
 * for the press. Only the keys enum LibToriRS_KeyCode names.
 */
static int
agent_key_character(
    int key,
    int shift)
{
    static const char k_digits_shift[] = ")!@#$%^&*(";

    if( key >= TORIRSK_A && key <= TORIRSK_Z )
        return (shift ? 'A' : 'a') + (key - TORIRSK_A);
    if( key >= TORIRSK_0 && key <= TORIRSK_9 )
        return shift ? k_digits_shift[key - TORIRSK_0] : '0' + (key - TORIRSK_0);
    if( key == TORIRSK_SPACE )
        return ' ';
    if( key == TORIRSK_COMMA )
        return shift ? '<' : ',';
    if( key == TORIRSK_PERIOD )
        return shift ? '>' : '.';
    if( key == TORIRSK_MINUS )
        return shift ? '_' : '-';
    if( key == TORIRSK_EQUALS )
        return shift ? '+' : '=';
    if( key == TORIRSK_LBRACKET )
        return shift ? '{' : '[';
    if( key == TORIRSK_RBRACKET )
        return shift ? '}' : ']';
    if( key == TORIRSK_SEMICOLON )
        return shift ? ':' : ';';
    if( key == TORIRSK_APOSTROPHE )
        return shift ? '"' : '\'';
    if( key == TORIRSK_GRAVE )
        return shift ? '~' : '`';
    if( key == TORIRSK_SLASH )
        return shift ? '?' : '/';
    if( key == TORIRSK_BACKSLASH )
        return shift ? '|' : '\\';
    return 0;
}

/** The OSRS code of a platform-neutral key, or -1: the cache keymap's own
 *  name lookup, as TORIRS_SIM_HOTKEY resolves it (main.c). */
static int
agent_key_osrs(int key)
{
    const char* name = LibToriRS_KeyCodeName((enum LibToriRS_KeyCode)key);
    return name ? LibToriRS_OsrsKeyFromName(name) : -1;
}

static void
agent_push_key_down(
    struct AgentCtx* ctx,
    int key)
{
    struct AgentConn const* conn = ctx->conn;
    int const osrs = agent_key_osrs(key);

    agent_pushed(ctx, CmdBus_PushKey(ctx->bus, TORIRS_CMD_INPUT_KEY_DOWN, (uint8_t)key));
    if( osrs >= 0 )
    {
        agent_pushed(ctx, CmdBus_PushOsrsKey(ctx->bus, osrs, 1, 1));
        agent_pushed(ctx, CmdBus_PushKeyEvent(ctx->bus, osrs, 0, 0));
    }
    /* The character the press types, unless Ctrl or Alt makes it a chord. */
    if( !agent_conn_key_down(conn, TORIRSK_CTRL) && !agent_conn_key_down(conn, TORIRSK_ALT) )
    {
        int const character = agent_key_character(key, agent_conn_key_down(conn, TORIRSK_SHIFT));
        if( character )
            agent_pushed(ctx, CmdBus_PushKeyEvent(ctx->bus, -1, character, 0));
    }
}

static void
agent_push_key_up(
    struct AgentCtx* ctx,
    int key)
{
    int const osrs = agent_key_osrs(key);

    agent_pushed(ctx, CmdBus_PushKey(ctx->bus, TORIRS_CMD_INPUT_KEY_UP, (uint8_t)key));
    if( osrs >= 0 )
        agent_pushed(ctx, CmdBus_PushOsrsKey(ctx->bus, osrs, 0, 0));
}

static void
agent_push_move(
    struct AgentCtx* ctx,
    int x,
    int y)
{
    agent_pushed(ctx, CmdBus_PushMouseMove(ctx->bus, (int16_t)x, (int16_t)y));
    ctx->server->pointer_x = x;
    ctx->server->pointer_y = y;
}

static void
agent_push_button(
    struct AgentCtx* ctx,
    int down,
    int button,
    int x,
    int y)
{
    agent_pushed(ctx,
        CmdBus_PushMouseButton(ctx->bus, down ? TORIRS_CMD_INPUT_MOUSE_DOWN : TORIRS_CMD_INPUT_MOUSE_UP,
            (uint8_t)button, (int16_t)x, (int16_t)y));
    ctx->server->pointer_x = x;
    ctx->server->pointer_y = y;
}

/**
 * Let go of everything the connection's SCRIPT holds, and -- with `all` --
 * everything its `key` and `mouse` verbs left down too (the connection is
 * closing). `ctx->bus` may be NULL at shutdown, when there is nothing left to
 * tell.
 */
static void
agent_conn_release(
    struct AgentCtx* ctx,
    int all)
{
    struct AgentConn* conn = ctx->conn;

    for( int key = TORIRSK_UNKNOWN + 1; key < TORIRSK_COUNT; key++ )
    {
        int const was = agent_conn_key_down(conn, key);
        conn->keys_script[key] = 0;
        if( all )
            conn->keys_verb[key] = 0;
        if( was && !agent_conn_key_down(conn, key) && ctx->bus )
            agent_push_key_up(ctx, key);
    }
    for( int button = TORIRSM_UNKNOWN + 1; button < TORIRSM_COUNT; button++ )
    {
        unsigned const bit = 1u << button;
        int const was = ((conn->buttons_script | conn->buttons_verb) & bit) != 0;
        conn->buttons_script &= ~bit;
        if( all )
            conn->buttons_verb &= ~bit;
        if( was && !((conn->buttons_script | conn->buttons_verb) & bit) && ctx->bus )
            agent_push_button(ctx, 0, button, ctx->server->pointer_x, ctx->server->pointer_y);
    }
}

int
Agent_KeyHeld(
    const struct AgentCtx* ctx,
    int key)
{
    assert(ctx);
    assert(ctx->conn);
    assert(key > TORIRSK_UNKNOWN);
    assert(key < TORIRSK_COUNT);
    return ctx->conn->keys_verb[key];
}

void
Agent_PointerAt(
    const struct AgentCtx* ctx,
    int* out_x,
    int* out_y)
{
    assert(ctx);
    assert(out_x);
    assert(out_y);
    *out_x = ctx->server->pointer_x;
    *out_y = ctx->server->pointer_y;
}

/* ---- ops and scripts ------------------------------------------------------------------ */

struct AgentOp*
Agent_OpArm(
    struct AgentCtx* ctx,
    AgentOpStep step,
    int uses_input)
{
    struct AgentOp* op;

    assert(ctx);
    assert(ctx->conn);
    assert(step);
    op = &ctx->conn->op;
    assert(!op->active);
    memset(op, 0, sizeof(*op));
    op->active = 1;
    op->step = step;
    op->uses_input = uses_input;
    op->start_frame = ctx->frame;
    op->start_ms = ctx->now_ms;
    return op;
}

static void
agent_op_end(
    struct AgentServer* server,
    struct AgentConn* conn)
{
    ToriMem_Free(ToriMem_Net(), conn->op.script.acts);
    memset(&conn->op, 0, sizeof(conn->op));
    if( server->input_owner == conn )
        server->input_owner = NULL;
}

static struct AgentAct*
agent_script_add(
    struct AgentOp* op,
    int kind)
{
    struct AgentScript* script;
    struct AgentAct* act;

    assert(op);
    script = &op->script;
    if( script->count == script->capacity )
    {
        script->capacity = script->capacity ? script->capacity * 2 : 32;
        script->acts =
            ToriMem_Realloc(ToriMem_Net(), script->acts, (size_t)script->capacity * sizeof(*script->acts));
        assert(script->acts);
    }
    act = &script->acts[script->count++];
    memset(act, 0, sizeof(*act));
    act->kind = (uint8_t)kind;
    return act;
}

void
Agent_ScriptMove(
    struct AgentOp* op,
    int x,
    int y)
{
    struct AgentAct* act = agent_script_add(op, AGENT_ACT_MOVE);
    act->x = (int16_t)x;
    act->y = (int16_t)y;
}

void
Agent_ScriptDown(
    struct AgentOp* op,
    int button,
    int x,
    int y)
{
    struct AgentAct* act = agent_script_add(op, AGENT_ACT_DOWN);
    assert(button > TORIRSM_UNKNOWN);
    assert(button < TORIRSM_COUNT);
    act->code = (uint8_t)button;
    act->x = (int16_t)x;
    act->y = (int16_t)y;
}

void
Agent_ScriptHoldDown(
    struct AgentOp* op,
    int button,
    int x,
    int y)
{
    Agent_ScriptDown(op, button, x, y);
    op->script.acts[op->script.count - 1].value = 1;
}

void
Agent_ScriptUp(
    struct AgentOp* op,
    int button,
    int x,
    int y)
{
    struct AgentAct* act = agent_script_add(op, AGENT_ACT_UP);
    assert(button > TORIRSM_UNKNOWN);
    assert(button < TORIRSM_COUNT);
    act->code = (uint8_t)button;
    act->x = (int16_t)x;
    act->y = (int16_t)y;
}

void
Agent_ScriptWheel(
    struct AgentOp* op,
    int notches)
{
    struct AgentAct* act = agent_script_add(op, AGENT_ACT_WHEEL);
    act->value = notches;
}

void
Agent_ScriptKeyDown(
    struct AgentOp* op,
    int key,
    int persist)
{
    struct AgentAct* act = agent_script_add(op, AGENT_ACT_KEY_DOWN);
    assert(key > TORIRSK_UNKNOWN);
    assert(key < TORIRSK_COUNT);
    act->code = (uint8_t)key;
    act->value = persist;
}

void
Agent_ScriptKeyUp(
    struct AgentOp* op,
    int key,
    int persist)
{
    struct AgentAct* act = agent_script_add(op, AGENT_ACT_KEY_UP);
    assert(key > TORIRSK_UNKNOWN);
    assert(key < TORIRSK_COUNT);
    act->code = (uint8_t)key;
    act->value = persist;
}

void
Agent_ScriptKeyRepeat(
    struct AgentOp* op,
    int key)
{
    struct AgentAct* act = agent_script_add(op, AGENT_ACT_KEY_REPEAT);
    assert(key > TORIRSK_UNKNOWN);
    assert(key < TORIRSK_COUNT);
    act->code = (uint8_t)key;
}

void
Agent_ScriptChar(
    struct AgentOp* op,
    int character)
{
    struct AgentAct* act = agent_script_add(op, AGENT_ACT_CHAR);
    assert(character >= 32);
    assert(character <= 255);
    act->value = character;
}

void
Agent_ScriptFrames(
    struct AgentOp* op,
    int frames)
{
    assert(frames >= 1);
    for( int i = 0; i < frames; i++ )
        (void)agent_script_add(op, AGENT_ACT_FRAME);
}

void
Agent_ScriptCall(
    struct AgentOp* op,
    int value)
{
    struct AgentAct* act = agent_script_add(op, AGENT_ACT_CALL);
    assert(op->check);
    act->value = value;
}

void
Agent_ScriptModsDown(
    struct AgentCtx* ctx,
    struct AgentOp* op,
    unsigned mods)
{
    int any = 0;

    assert(ctx);
    for( size_t m = 0; m < sizeof(k_agent_mods) / sizeof(k_agent_mods[0]); m++ )
        if( (mods & k_agent_mods[m].bit) && !Agent_KeyHeld(ctx, k_agent_mods[m].key) )
        {
            Agent_ScriptKeyDown(op, k_agent_mods[m].key, 0);
            any = 1;
        }
    /* Held is a fact of the frame AFTER the press: the chrome and the tools
     * read the modifier off the held keys when the button goes down. */
    if( any )
        Agent_ScriptFrames(op, 1);
}

void
Agent_ScriptModsUp(
    struct AgentCtx* ctx,
    struct AgentOp* op,
    unsigned mods)
{
    int any = 0;

    assert(ctx);
    for( size_t m = 0; m < sizeof(k_agent_mods) / sizeof(k_agent_mods[0]); m++ )
        if( (mods & k_agent_mods[m].bit) && !Agent_KeyHeld(ctx, k_agent_mods[m].key) )
        {
            if( !any )
                Agent_ScriptFrames(op, 1);
            Agent_ScriptKeyUp(op, k_agent_mods[m].key, 0);
            any = 1;
        }
}

void
Agent_ScriptClick(
    struct AgentOp* op,
    int button,
    int x,
    int y)
{
    Agent_ScriptMove(op, x, y);
    Agent_ScriptFrames(op, 1);
    Agent_ScriptDown(op, button, x, y);
    Agent_ScriptFrames(op, 1);
    Agent_ScriptUp(op, button, x, y);
}

/** Run this frame's slice. 0 more to come, 1 the script is spent, -1 a CALL
 *  stopped it (its error reply is written). */
static int
agent_script_run(
    struct AgentCtx* ctx,
    struct AgentOp* op)
{
    struct AgentScript* script = &op->script;
    struct AgentConn* conn = ctx->conn;

    while( script->at < script->count )
    {
        struct AgentAct const* act = &script->acts[script->at];
        int const key = act->code;

        if( act->kind == AGENT_ACT_FRAME )
        {
            script->at++;
            return 0;
        }
        if( act->kind == AGENT_ACT_CALL )
        {
            int const verdict = op->check(ctx, op, act->value);
            if( verdict == 0 )
                return -1;
            if( verdict == 2 )
                return 0;
            script->at++;
            continue;
        }
        if( act->kind == AGENT_ACT_MOVE )
            agent_push_move(ctx, act->x, act->y);
        else if( act->kind == AGENT_ACT_DOWN )
        {
            agent_push_button(ctx, 1, act->code, act->x, act->y);
            if( act->value )
                conn->buttons_verb |= 1u << act->code;
            else
                conn->buttons_script |= 1u << act->code;
        }
        else if( act->kind == AGENT_ACT_UP )
        {
            agent_push_button(ctx, 0, act->code, act->x, act->y);
            conn->buttons_script &= ~(1u << act->code);
            conn->buttons_verb &= ~(1u << act->code);
        }
        else if( act->kind == AGENT_ACT_WHEEL )
        {
            /* One notch as WM_MOUSEWHEEL pushes it: the integer wheel, and
             * the exact scroll beside it at 48 px a notch. */
            agent_pushed(ctx, CmdBus_PushMouseWheel(ctx->bus, (int16_t)act->value));
            agent_pushed(ctx, CmdBus_PushScroll(ctx->bus, 0.0, (double)act->value * 48.0, 48, 0, 0));
        }
        else if( act->kind == AGENT_ACT_KEY_DOWN )
        {
            int const was = agent_conn_key_down(conn, key);
            if( !was )
                agent_push_key_down(ctx, key);
            if( act->value )
                conn->keys_verb[key] = 1;
            else
                conn->keys_script[key] = 1;
        }
        else if( act->kind == AGENT_ACT_KEY_UP )
        {
            int const was = agent_conn_key_down(conn, key);
            if( act->value )
                conn->keys_verb[key] = 0;
            else
                conn->keys_script[key] = 0;
            if( was && !agent_conn_key_down(conn, key) )
                agent_push_key_up(ctx, key);
        }
        else if( act->kind == AGENT_ACT_KEY_REPEAT )
        {
            /* WM_KEYDOWN with the repeat bit: held again, no press edge, and
             * the key event marked a repeat. */
            int const osrs = agent_key_osrs(key);
            if( osrs >= 0 && agent_conn_key_down(conn, key) )
            {
                agent_pushed(ctx, CmdBus_PushOsrsKey(ctx->bus, osrs, 1, 0));
                agent_pushed(ctx, CmdBus_PushKeyEvent(ctx->bus, osrs, 0, 1));
            }
        }
        else
        {
            assert(act->kind == AGENT_ACT_CHAR);
            agent_pushed(ctx, CmdBus_PushKeyEvent(ctx->bus, -1, act->value, 0));
        }
        script->at++;
    }
    return 1;
}

static int
agent_script_step(
    struct AgentCtx* ctx,
    struct AgentOp* op)
{
    if( op->script.at < op->script.count )
    {
        int const state = agent_script_run(ctx, op);

        if( ctx->server->push_failed )
        {
            ctx->server->push_failed = 0;
            agent_conn_release(ctx, 0);
            /* A CALL may have answered already (state -1): one reply only. */
            if( !ctx->conn->replied )
                Agent_ReplyError(ctx,
                    "the client's command ring is full, so part of this input was dropped: wait a few frames and "
                    "send it again");
            return 1;
        }
        if( state < 0 )
        {
            agent_conn_release(ctx, 0);
            return 1;
        }
        if( state == 0 )
            return 0;
        op->until_frame = ctx->frame + AGENT_SCRIPT_TAIL_FRAMES;
        return 0;
    }
    if( ctx->frame < op->until_frame )
        return 0;
    agent_conn_release(ctx, 0);
    if( op->finish )
        op->finish(ctx, op);
    else
        Agent_ReplyOk(ctx);
    return 1;
}

struct AgentOp*
Agent_ScriptArm(
    struct AgentCtx* ctx,
    AgentOpCheck check,
    AgentOpFinish finish)
{
    struct AgentOp* op = Agent_OpArm(ctx, agent_script_step, 1);
    op->check = check;
    op->finish = finish;
    return op;
}

/* ---- connections ---------------------------------------------------------------------- */

static void
agent_conn_close(
    struct AgentServer* server,
    struct AgentConn* conn,
    struct AgentCtx* ctx)
{
    ctx->conn = conn;
    ctx->request = NULL;
    /* Nothing an agent pressed outlives the agent. */
    agent_conn_release(ctx, 1);
    agent_op_end(server, conn);
    AgentBuf_Free(&conn->in);
    AgentBuf_Free(&conn->out);
    AgentBuf_Free(&conn->id);
    agent_sock_close(conn->fd);
    memset(conn, 0, sizeof(*conn));
    TORIRS_LOG("agent: a client left\n");
}

/** Push what is queued as far as the socket takes it now. */
static void
agent_conn_flush(struct AgentConn* conn)
{
    while( conn->out.length > 0 )
    {
        int const sent = agent_sock_send(conn->fd, conn->out.data, conn->out.length);

        if( sent > 0 )
        {
            AgentBuf_Consume(&conn->out, (size_t)sent);
            continue;
        }
        if( sent < 0 && AGENT_NET_RETRY(agent_net_errno()) )
            return;
        conn->dead = 1;
        return;
    }
}

static void
agent_conn_read(struct AgentConn* conn)
{
    char chunk[16384];

    for( int reads = 0; reads < 64; reads++ )
    {
        int const taken = (int)recv(conn->fd, chunk, (int)sizeof(chunk), 0);

        if( taken > 0 )
        {
            AgentBuf_Append(&conn->in, chunk, (size_t)taken);
            continue;
        }
        if( taken == 0 )
        {
            /* An orderly close of the peer's sending half. A last line it
             * did not end is still a request (printf with no newline). */
            conn->eof = 1;
            if( conn->in.length > 0 && conn->in.data[conn->in.length - 1] != '\n' )
                AgentBuf_Append(&conn->in, "\n", 1);
        }
        else if( !AGENT_NET_RETRY(agent_net_errno()) )
            conn->dead = 1;
        return;
    }
}

/** Nothing more will come from the peer and nothing is owed to it: no op in
 *  flight, no whole request left unanswered, every reply sent. */
static int
agent_conn_finished(const struct AgentConn* conn)
{
    if( !conn->eof || conn->op.active || conn->out.length > 0 )
        return 0;
    return !conn->in.length || !memchr(conn->in.data, '\n', conn->in.length);
}

/** Remember the request's id as JSON text for the reply: a number or a string
 *  as it was given, anything else (or none) null. */
static void
agent_conn_take_id(
    struct AgentConn* conn,
    const struct AgentJsonDoc* request)
{
    const struct AgentJsonValue* id = request ? AgentJson_Get(request, "id") : NULL;
    struct AgentJsonWriter writer;

    conn->id.length = 0;
    AgentJsonW_Init(&writer, &conn->id);
    if( id && id->type == AGENT_JSON_NUMBER )
        AgentJsonW_Dbl(&writer, id->number);
    else if( id && id->type == AGENT_JSON_STRING )
        AgentJsonW_Str(&writer, id->string);
    else
        AgentJsonW_Null(&writer);
    AgentBuf_Append(&conn->id, "", 1);
}

static void
agent_op_step(
    struct AgentServer* server,
    struct AgentCtx* ctx)
{
    struct AgentConn* conn = ctx->conn;

    assert(conn->op.active);
    ctx->request = NULL;
    if( conn->op.step(ctx, &conn->op) )
    {
        assert(conn->replied);
        agent_op_end(server, conn);
    }
}

/**
 * Answer one request line. Returns 0 when the line must wait (another
 * connection holds the input lane) and stays in the buffer, else 1.
 */
static int
agent_conn_dispatch(
    struct AgentServer* server,
    struct AgentCtx* ctx,
    const char* line,
    size_t length)
{
    struct AgentConn* conn = ctx->conn;
    struct AgentJsonDoc request;
    char error[256];
    const struct AgentJsonValue* verb_value;
    const struct AgentVerb* verb;
    enum AgentVerbResult result;

    if( !AgentJson_Parse(&request, line, length, error, sizeof(error)) )
    {
        conn->replied = 0;
        agent_conn_take_id(conn, NULL);
        Agent_ReplyError(ctx, "%s", error);
        AgentJson_Free(&request);
        return 1;
    }
    verb_value = AgentJson_Get(&request, "verb");
    verb = verb_value && verb_value->type == AGENT_JSON_STRING ? Agent_FindVerb(verb_value->string) : NULL;

    /* Input from two connections at once would interleave on one pointer:
     * the second waits its turn, unanswered and unconsumed. */
    if( verb && (verb->flags & AGENT_VERB_INPUT) && server->input_owner && server->input_owner != conn )
    {
        AgentJson_Free(&request);
        return 0;
    }

    conn->replied = 0;
    agent_conn_take_id(conn, &request);
    ctx->request = &request;
    if( !verb_value || verb_value->type != AGENT_JSON_STRING )
        Agent_ReplyError(ctx, "a request names its \"verb\": {\"verb\": \"help\"} lists them");
    else if( !verb )
        Agent_ReplyError(ctx, "there is no verb \"%s\": {\"verb\": \"help\"} lists them", verb_value->string);
    else
    {
        const char* stray = NULL;

        for( int i = 0; i < request.member_count && !stray; i++ )
        {
            const char* key = request.members[i].key;
            if( strcmp(key, "id") != 0 && strcmp(key, "verb") != 0 && !agent_name_in_list(verb->args, key) )
                stray = key;
        }
        if( stray )
            Agent_ReplyError(ctx, "%s does not take \"%s\": it takes %s%s", verb->name, stray,
                verb->args[0] ? "" : "no arguments", verb->args);
        else
        {
            result = verb->run(ctx);
            if( result == AGENT_VERB_PENDING )
            {
                assert(conn->op.active);
                assert(!conn->replied);
                if( conn->op.uses_input )
                    server->input_owner = conn;
            }
            else
            {
                assert(conn->replied);
                assert(!conn->op.active);
            }
        }
    }
    ctx->request = NULL;
    AgentJson_Free(&request);
    /* An armed op runs its first slice now, so what it pushes lands in this
     * frame's input. */
    if( conn->op.active )
        agent_op_step(server, ctx);
    return 1;
}

static void
agent_conn_service(
    struct AgentServer* server,
    struct AgentCtx* ctx)
{
    struct AgentConn* conn = ctx->conn;

    if( conn->op.active )
        agent_op_step(server, ctx);

    for( int answered = 0; answered < AGENT_REQUESTS_PER_FRAME && !conn->op.active && !conn->dead; answered++ )
    {
        const char* newline = conn->in.length ? memchr(conn->in.data, '\n', conn->in.length) : NULL;
        size_t length;
        size_t taken;

        if( !newline )
        {
            if( conn->in.length > AGENT_LINE_MAX )
            {
                conn->replied = 0;
                agent_conn_take_id(conn, NULL);
                Agent_ReplyError(ctx, "a request is one line of at most %u bytes, ended by a newline; closing",
                    (unsigned)AGENT_LINE_MAX);
                conn->dead = 1;
            }
            return;
        }
        length = (size_t)(newline - conn->in.data);
        taken = length + 1;
        if( length > 0 && conn->in.data[length - 1] == '\r' )
            length--;
        /* A blank line is nothing to answer (a shell's stray Enter). */
        if( length > 0 && !agent_conn_dispatch(server, ctx, conn->in.data, length) )
            return;
        AgentBuf_Consume(&conn->in, taken);
    }
}

static void
agent_accept(struct AgentServer* server)
{
    for( ;; )
    {
        int const fd = (int)accept(server->fd, NULL, NULL);
        struct AgentConn* conn = NULL;

        if( fd < 0 )
        {
            if( !AGENT_NET_RETRY(agent_net_errno()) )
                TORIRS_ERR("agent: accept: %s\n", agent_sock_why());
            return;
        }
        for( int i = 0; i < AGENT_CONNS_MAX && !conn; i++ )
            if( !server->conns[i].open )
                conn = &server->conns[i];
        if( !conn )
        {
            /* Full is said and closed, not queued: a client parked in a
             * backlog looks connected and hears nothing. */
            static const char k_full[] =
                "{\"id\":null,\"ok\":false,\"error\":\"the agent server already has every connection it takes; "
                "close one and connect again\"}\n";
            (void)agent_sock_send(fd, k_full, sizeof(k_full) - 1);
            agent_sock_close(fd);
            continue;
        }
        agent_sock_nonblocking(fd);
#if defined(SO_NOSIGPIPE)
        {
            int on = 1;
            setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
        }
#endif
        memset(conn, 0, sizeof(*conn));
        conn->open = 1;
        conn->fd = fd;
        AgentBuf_Init(&conn->in);
        AgentBuf_Init(&conn->out);
        AgentBuf_Init(&conn->id);
        TORIRS_LOG("agent: a client connected\n");
    }
}

/* ---- the listener ---------------------------------------------------------------------- */

/** Parse a port: 1 with `*out` set, 0 for anything but a whole 0..65535. */
static int
agent_port_parse(
    const char* text,
    int* out)
{
    char* end = NULL;
    long value;

    if( !text || !text[0] )
        return 0;
    for( const char* c = text; *c; c++ )
        if( *c < '0' || *c > '9' )
            return 0;
    value = strtol(text, &end, 10);
    if( !end || *end || value < 0 || value > 65535 )
        return 0;
    *out = (int)value;
    return 1;
}

int
AgentServer_PortTextOk(const char* text)
{
    int port;

    assert(text);
    if( agent_port_parse(text, &port) )
        return 1;
    TORIRS_ERR("agent: --agent-port takes a port number 0..65535 (0 picks a free one), not '%s'\n", text);
    return 0;
}

/** The port as text in `path`, whole or not at all: a reader polling for the
 *  file must never find it empty. */
static int
agent_write_port_file(
    const char* path,
    int port)
{
    char temp[1100];
    FILE* file;

    snprintf(temp, sizeof(temp), "%s.tmp", path);
    file = fopen(temp, "w");
    if( !file )
        return 0;
    fprintf(file, "%d\n", port);
    if( fclose(file) != 0 )
        return 0;
    /* rename() does not replace on Windows. */
    remove(path);
    return rename(temp, path) == 0;
}

struct AgentServer*
AgentServer_Start(
    const char* port_text,
    const char* port_file)
{
    struct AgentServer* server;
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    int reuse = 1;
    int port = 0;
    int fd;

    if( !port_text )
        port_text = getenv("TORIRS_AGENT_PORT");
    if( !port_text || !port_text[0] )
        return NULL;
    if( !agent_port_parse(port_text, &port) )
    {
        TORIRS_ERR("agent: TORIRS_AGENT_PORT is a port number 0..65535 (0 picks a free one), not '%s'; "
                   "the agent server is off\n",
            port_text);
        return NULL;
    }
    if( !port_file )
        port_file = getenv("TORIRS_AGENT_PORT_FILE");
    if( port_file && !port_file[0] )
        port_file = NULL;

#ifdef _WIN32
    {
        /* Counted by winsock: whoever else in this process started it too. */
        WSADATA data;
        if( WSAStartup(MAKEWORD(2, 2), &data) != 0 )
        {
            TORIRS_ERR("agent: WSAStartup failed; the agent server is off\n");
            return NULL;
        }
    }
#endif
    fd = (int)socket(AF_INET, SOCK_STREAM, 0);
    if( fd < 0 )
    {
        TORIRS_ERR("agent: socket: %s; the agent server is off\n", agent_sock_why());
        return NULL;
    }
#ifdef _WIN32
    /* Windows' SO_REUSEADDR lets a second process take a port that is in use;
     * EXCLUSIVEADDRUSE is the POSIX meaning (a busy port refuses the bind). */
    setsockopt(fd, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*)&reuse, sizeof(reuse));
#else
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    /* Loopback only, never another interface: this port runs the client. */
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((uint16_t)port);
    if( bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0 )
    {
        TORIRS_ERR("agent: cannot listen on 127.0.0.1:%d: %s; the agent server is off (pass --agent-port 0 for a "
                   "free port)\n",
            port, agent_sock_why());
        agent_sock_close(fd);
        return NULL;
    }
    if( listen(fd, 4) < 0 || getsockname(fd, (struct sockaddr*)&addr, &addr_len) < 0 )
    {
        TORIRS_ERR("agent: listen on 127.0.0.1:%d: %s; the agent server is off\n", port, agent_sock_why());
        agent_sock_close(fd);
        return NULL;
    }
    agent_sock_nonblocking(fd);
    port = ntohs(addr.sin_port);

    if( port_file && !agent_write_port_file(port_file, port) )
    {
        TORIRS_ERR("agent: cannot write the port to '%s'; the agent server is off (does its folder exist?)\n",
            port_file);
        agent_sock_close(fd);
        return NULL;
    }

    server = ToriMem_Calloc(ToriMem_Net(), 1, sizeof(*server));
    assert(server);
    server->fd = fd;
    server->port = port;
    if( port_file )
        snprintf(server->port_file, sizeof(server->port_file), "%s", port_file);

    /* One control port per session: this window's. A window the editor pops
     * out inherits its leader's environment (app_editor_view.c), and with the
     * variables still set each would open a server of its own -- on port 0,
     * over the leader's port file. The flags are not inherited, so only the
     * variables need taking back. */
    unsetenv("TORIRS_AGENT_PORT");
    unsetenv("TORIRS_AGENT_PORT_FILE");

    /* The one line a launcher waits for. Flushed: stderr is fully buffered
     * here, and the frame loop's own flush may be a boot away. */
    TORIRS_REPORT("agent: listening on 127.0.0.1:%d\n", port);
    fflush(stderr);
    return server;
}

void
AgentServer_Frame(
    struct AgentServer* server,
    struct App* app,
    struct ToriRS_CmdBus* bus,
    struct PlatformWindow* platform)
{
    struct AgentCtx ctx;

    assert(server);
    assert(app);
    assert(bus);
    assert(platform);

    server->frame++;
    memset(&ctx, 0, sizeof(ctx));
    ctx.server = server;
    ctx.app = app;
    ctx.bus = bus;
    ctx.platform = platform;
    ctx.frame = server->frame;
    ctx.now_ms = PlatformWindow_Ticks64();

    agent_accept(server);
    for( int i = 0; i < AGENT_CONNS_MAX; i++ )
    {
        struct AgentConn* conn = &server->conns[i];

        if( !conn->open )
            continue;
        ctx.conn = conn;
        if( !conn->eof )
            agent_conn_read(conn);
        agent_conn_service(server, &ctx);
        agent_conn_flush(conn);
        if( agent_conn_finished(conn) )
            conn->dead = 1;
        if( conn->out.length > AGENT_OUT_MAX )
        {
            TORIRS_ERR("agent: a client stopped reading (%u bytes queued); dropping it\n",
                (unsigned)conn->out.length);
            conn->dead = 1;
        }
        if( conn->dead )
            agent_conn_close(server, conn, &ctx);
    }
    /* A drop outside any script (a `key` release at a close) is not owed an
     * answer by anyone. */
    server->push_failed = 0;
}

void
AgentServer_Stop(
    struct AgentServer* server,
    struct ToriRS_CmdBus* bus)
{
    struct AgentCtx ctx;

    if( !server )
        return;
    memset(&ctx, 0, sizeof(ctx));
    ctx.server = server;
    ctx.bus = bus;
    ctx.frame = server->frame;
    for( int i = 0; i < AGENT_CONNS_MAX; i++ )
        if( server->conns[i].open )
        {
            /* Whatever was still queued for it, as far as the socket takes it
             * now: a departing host owes no more. */
            agent_conn_flush(&server->conns[i]);
            agent_conn_close(server, &server->conns[i], &ctx);
        }
    agent_sock_close(server->fd);
    /* The port is nobody's now: a file that still named it would send the
     * next reader to a closed door. */
    if( server->port_file[0] )
        remove(server->port_file);
    ToriMem_Free(ToriMem_Net(), server);
}

#else /* __EMSCRIPTEN__ */

struct AgentServer
{
    int unused;
};

int
AgentServer_PortTextOk(const char* text)
{
    assert(text);
    return 1;
}

struct AgentServer*
AgentServer_Start(
    const char* port_text,
    const char* port_file)
{
    (void)port_file;
    if( !port_text )
        port_text = getenv("TORIRS_AGENT_PORT");
    if( port_text && port_text[0] )
        TORIRS_ERR("agent: --agent-port is not built on the web: a page cannot listen on a socket\n");
    return NULL;
}

void
AgentServer_Frame(
    struct AgentServer* server,
    struct App* app,
    struct ToriRS_CmdBus* bus,
    struct PlatformWindow* platform)
{
    assert(server);
    (void)app;
    (void)bus;
    (void)platform;
}

void
AgentServer_Stop(
    struct AgentServer* server,
    struct ToriRS_CmdBus* bus)
{
    (void)server;
    (void)bus;
}

#endif
