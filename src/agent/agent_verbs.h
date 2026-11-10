#ifndef SRC_AGENT_AGENT_VERBS_H
#define SRC_AGENT_AGENT_VERBS_H

/**
 * The agent server's inside: what a verb handler is handed, how it answers,
 * and the INPUT SCRIPT every pointer and keyboard verb is made of.
 *
 * Private to src/agent/. main.c sees agent_server.h only.
 *
 * A verb handler (agent_verbs_core.c, _editor.c, _game.c) reads its arguments
 * off the request, and either writes the whole reply and returns
 * AGENT_VERB_DONE, or arms the connection's OP and returns AGENT_VERB_PENDING:
 * the op is stepped once per frame until it writes the reply. One op per
 * connection, so a connection's requests are answered in the order they
 * arrived; another connection's are not held up.
 *
 * An INPUT SCRIPT is a list of command-bus pushes cut into frames: "move;
 * next frame; press; next frame; release". A person's click is spread over
 * frames too, and the client is written for that: the chrome's hover is what
 * a press lands on, and the world's pick is stamped at the pointer when a
 * frame renders. One script runs at a time across every connection (two
 * agents moving one pointer would each see the other's clicks land), and
 * whatever keys or buttons a script still holds when it ends early are let go.
 */

#include "agent/agent_json.h"

#include <stdint.h>

struct App;
struct ToriRS_CmdBus;
struct PlatformWindow;
struct AgentServer;
struct AgentConn;
struct AgentOp;
struct EditorUi;

/** `hello`'s version: raised when a verb's meaning changes, never when one
 *  is added (a client reads `verbs` for that). */
#define AGENT_API_VERSION 1

enum AgentVerbResult
{
    /** The reply is written. */
    AGENT_VERB_DONE = 0,
    /** The connection's op is armed and will reply on a later frame. */
    AGENT_VERB_PENDING,
};

struct AgentCtx
{
    struct AgentServer* server;
    struct AgentConn* conn;
    struct App* app;
    struct ToriRS_CmdBus* bus;
    struct PlatformWindow* platform;
    /** Main-loop iterations since the server started. */
    long frame;
    /** PlatformWindow_Ticks64 at this iteration. */
    uint64_t now_ms;
    /** The request being dispatched; NULL while an op is stepped. */
    const struct AgentJsonDoc* request;
};

typedef enum AgentVerbResult (*AgentVerbFn)(struct AgentCtx* ctx);

/** Verb flags. */
enum
{
    /** Pushes pointer or keyboard input: one at a time across connections. */
    AGENT_VERB_INPUT = 1 << 0,
    /** Needs the map editor's panels: refused in a window without them. */
    AGENT_VERB_EDITOR = 1 << 1,
};

struct AgentVerb
{
    const char* name;
    /** The argument names it takes, space separated ("" for none). A request
     *  carrying any other member is refused by name: a misspelt argument that
     *  silently did the default thing would be the worst answer. */
    const char* args;
    unsigned flags;
    AgentVerbFn run;
    /** One line for `help`. */
    const char* about;
};

/** The three verb tables, each ended by a NULL name. */
extern const struct AgentVerb g_agent_verbs_core[];
extern const struct AgentVerb g_agent_verbs_editor[];
extern const struct AgentVerb g_agent_verbs_game[];

/** The verb named `name` in any table, or NULL. */
const struct AgentVerb*
Agent_FindVerb(const char* name);

/** Every verb, in table order: `index` 0.. , NULL past the last. */
const struct AgentVerb*
Agent_VerbAt(int index);

/* ---- replies ------------------------------------------------------------------
 *
 * Exactly one reply per request, a line: Agent_ReplyBegin .. Agent_ReplyEnd
 * around the members a verb adds, or Agent_ReplyOk / Agent_ReplyError for the
 * whole of it. Writing a second asserts.
 */

/** Open `{"id":<id>,"ok":true` and hand back the writer to add members to. */
struct AgentJsonWriter*
Agent_ReplyBegin(struct AgentCtx* ctx);

void
Agent_ReplyEnd(struct AgentCtx* ctx);

/** `{"id":<id>,"ok":true}`. */
void
Agent_ReplyOk(struct AgentCtx* ctx);

/** `{"id":<id>,"ok":false,"error":"<sentence>"}`. One sentence: what was
 *  refused and what to do next. */
void
Agent_ReplyError(
    struct AgentCtx* ctx,
    const char* format,
    ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;

/* ---- arguments ----------------------------------------------------------------
 *
 * Each answers 1 when the member is there and well-typed (the output set), 0
 * when it is absent (the output untouched: the caller's default stands), and
 * -1 when it is there and wrong -- the error reply is then already written,
 * and the handler returns AGENT_VERB_DONE.
 */
int
Agent_ArgInt(
    struct AgentCtx* ctx,
    const char* key,
    int* out);

int
Agent_ArgBool(
    struct AgentCtx* ctx,
    const char* key,
    int* out);

int
Agent_ArgString(
    struct AgentCtx* ctx,
    const char* key,
    const char** out);

/** An array of exactly `count` whole numbers ("from": [x, y]). */
int
Agent_ArgInts(
    struct AgentCtx* ctx,
    const char* key,
    int* out,
    int count);

/** As above, but an absent member is an error too: 1, or 0 with the error
 *  reply written. */
int
Agent_NeedInt(
    struct AgentCtx* ctx,
    const char* key,
    int* out);

int
Agent_NeedString(
    struct AgentCtx* ctx,
    const char* key,
    const char** out);

int
Agent_NeedInts(
    struct AgentCtx* ctx,
    const char* key,
    int* out,
    int count);

/**
 * "mods": ["shift", "ctrl", "alt"] as AGENT_MOD_* bits. 1, 0 absent (0 bits),
 * -1 with the error written.
 */
#define AGENT_MOD_CTRL 1u
#define AGENT_MOD_SHIFT 2u
#define AGENT_MOD_ALT 4u

int
Agent_ArgMods(
    struct AgentCtx* ctx,
    unsigned* out_mods);

/** "button": "left" | "right" | "middle" as enum LibToriRS_MouseButton
 *  (default left). 1, 0 absent, -1 with the error written. */
int
Agent_ArgButton(
    struct AgentCtx* ctx,
    int* out_button);

/* ---- the surface ---------------------------------------------------------------- */

/** The layout the pointer lives in: every x, y the API takes or gives is in
 *  these pixels (UITREE_LAYOUT_ROOT_W x _H). */
void
Agent_SurfaceSize(
    int* out_w,
    int* out_h);

/** 1 when (x, y) is on the surface; else 0 with the error reply written. */
int
Agent_NeedOnSurface(
    struct AgentCtx* ctx,
    int x,
    int y);

/** The editor's registry when this window has one and its panels are up,
 *  else NULL. Says nothing. */
struct EditorUi*
Agent_EditorUi(const struct AgentCtx* ctx);

/**
 * The same, for a verb that cannot go on without it: NULL comes with the
 * error reply written ("not an editor window", or that the panels are still
 * coming up).
 */
struct EditorUi*
Agent_NeedEditor(struct AgentCtx* ctx);

/**
 * The right-click menu as a JSON value: `{"visible": false}`, or its box and
 * its entries, each with its row's box (a click on a row goes to its middle).
 * The `ui` verb's "minimenu" and the `game` verb's.
 */
void
Agent_WriteMinimenu(
    struct AgentCtx* ctx,
    struct AgentJsonWriter* out);

/* ---- ops ---------------------------------------------------------------------------- */

/** Steps an armed op. 1 when it wrote its reply (the op ends), 0 to be
 *  stepped again next frame. */
typedef int (*AgentOpStep)(
    struct AgentCtx* ctx,
    struct AgentOp* op);

/** A script's CALL act: 1 go on, 2 ask again next frame (a condition not met
 *  yet), 0 stop -- the error reply is written and the script lets go of
 *  whatever it holds. */
typedef int (*AgentOpCheck)(
    struct AgentCtx* ctx,
    struct AgentOp* op,
    int value);

/** Writes the reply of a script that ran to its end. */
typedef void (*AgentOpFinish)(
    struct AgentCtx* ctx,
    struct AgentOp* op);

enum AgentActKind
{
    /** The pointer to (x, y). */
    AGENT_ACT_MOVE = 1,
    /** `code` (enum LibToriRS_MouseButton) down / up at (x, y). A DOWN with
     *  `value` 1 outlives the script (Agent_ScriptHoldDown). */
    AGENT_ACT_DOWN,
    AGENT_ACT_UP,
    /** `value` wheel notches, positive away from the user (scroll up). */
    AGENT_ACT_WHEEL,
    /** `code` (enum LibToriRS_KeyCode) down / up, as a key press writes it:
     *  the platform-neutral edge, the OSRS key state and its key event, and
     *  the character it types when no Ctrl or Alt is held. `value` 1 = the
     *  hold outlives the script (the `key` verb's "down"). */
    AGENT_ACT_KEY_DOWN,
    AGENT_ACT_KEY_UP,
    /** One auto-repeat of `code`, a key the script is holding: the OSRS key
     *  event again, marked as a repeat, and no new edge (what the OS sends
     *  while Backspace is held down). */
    AGENT_ACT_KEY_REPEAT,
    /** The typed character `value` (32..255): a key event with no key. */
    AGENT_ACT_CHAR,
    /** Everything after this runs next frame. */
    AGENT_ACT_FRAME,
    /** op->check(ctx, op, value). */
    AGENT_ACT_CALL,
};

struct AgentAct
{
    uint8_t kind;
    uint8_t code;
    int16_t x;
    int16_t y;
    int32_t value;
};

struct AgentScript
{
    struct AgentAct* acts;
    int count;
    int capacity;
    int at;
};

#define AGENT_OP_INTS 16
#define AGENT_OP_TEXT 512

struct AgentOp
{
    int active;
    AgentOpStep step;
    /** Set for an op that pushes input (it holds the one input lane). */
    int uses_input;
    long start_frame;
    uint64_t start_ms;
    /** 0 = none. */
    uint64_t deadline_ms;

    /** The script, and what to do around it. */
    struct AgentScript script;
    AgentOpCheck check;
    AgentOpFinish finish;

    /** Scratch the verb that armed the op reads back: its own meaning. */
    int phase;
    long until_frame;
    int ints[AGENT_OP_INTS];
    char text[AGENT_OP_TEXT];
    char path[AGENT_OP_TEXT];
};

/**
 * Arm the connection's op (zeroed; asserts none is armed) and return it. The
 * handler fills it and returns AGENT_VERB_PENDING. `uses_input` for an op
 * that will push input.
 */
struct AgentOp*
Agent_OpArm(
    struct AgentCtx* ctx,
    AgentOpStep step,
    int uses_input);

/**
 * Arm an op that runs a script: append acts with the Agent_Script* calls,
 * then return AGENT_VERB_PENDING. When the script ends `finish` writes the
 * reply (NULL: Agent_ReplyOk); two frames are left after the last act first,
 * so what the input caused has been handled and drawn when the reply goes.
 */
struct AgentOp*
Agent_ScriptArm(
    struct AgentCtx* ctx,
    AgentOpCheck check,
    AgentOpFinish finish);

void
Agent_ScriptMove(
    struct AgentOp* op,
    int x,
    int y);

void
Agent_ScriptDown(
    struct AgentOp* op,
    int button,
    int x,
    int y);

/** A press that outlives the script (the `mouse` verb's "down"): held until
 *  an "up", or until the connection closes. */
void
Agent_ScriptHoldDown(
    struct AgentOp* op,
    int button,
    int x,
    int y);

void
Agent_ScriptUp(
    struct AgentOp* op,
    int button,
    int x,
    int y);

void
Agent_ScriptWheel(
    struct AgentOp* op,
    int notches);

void
Agent_ScriptKeyDown(
    struct AgentOp* op,
    int key,
    int persist);

void
Agent_ScriptKeyUp(
    struct AgentOp* op,
    int key,
    int persist);

void
Agent_ScriptKeyRepeat(
    struct AgentOp* op,
    int key);

void
Agent_ScriptChar(
    struct AgentOp* op,
    int character);

/** End this frame's slice `frames` times over (at least 1). */
void
Agent_ScriptFrames(
    struct AgentOp* op,
    int frames);

void
Agent_ScriptCall(
    struct AgentOp* op,
    int value);

/** The modifier keys of `mods` down (and the frame that makes them held), or
 *  up. A modifier the connection already holds is left as it is. */
void
Agent_ScriptModsDown(
    struct AgentCtx* ctx,
    struct AgentOp* op,
    unsigned mods);

void
Agent_ScriptModsUp(
    struct AgentCtx* ctx,
    struct AgentOp* op,
    unsigned mods);

/** A whole click: move, a frame, press, a frame, release. */
void
Agent_ScriptClick(
    struct AgentOp* op,
    int button,
    int x,
    int y);

/** 1 while this connection holds `key` down through the `key` verb. */
int
Agent_KeyHeld(
    const struct AgentCtx* ctx,
    int key);

/** Where the last injected pointer event put the pointer. */
void
Agent_PointerAt(
    const struct AgentCtx* ctx,
    int* out_x,
    int* out_y);

#endif
