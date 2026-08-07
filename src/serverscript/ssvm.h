#ifndef SRC_SERVERSCRIPT_SSVM_H
#define SRC_SERVERSCRIPT_SSVM_H

/*
 * The ServerScript virtual machine.
 *
 * The VM implements the *language* — constants, locals, branches, switch,
 * gosub/jump/return, string joining, arithmetic and the pure string ops — and
 * hands every other opcode to a single host callback. It never dereferences a
 * game object: active entities are void* the host owns and the VM only stores,
 * swaps and null-checks them. That is what keeps this subsystem linkable into
 * the standalone mock server and testable with a stub host.
 *
 * SUSPENSION
 *
 * There is no coroutine and no yield machinery. A command that needs to wait
 * calls SSVM_Suspend, the interpreter returns, and the state is left exactly
 * where it stopped — same pc, same stacks, same locals. The engine parks it on
 * a player, an npc or the world queue and calls SSVM_Execute again on a later
 * tick. This is why a state owns its string pool (see ssvm_strpool.h) and why
 * none of src/cs2vm2's checkpoint/undo-log code is ported: that exists because
 * CS2 host requests are asynchronous and get replayed, which is a different
 * problem with a different shape.
 */

#include "ss_meta.h"
#include "ssvm_provider.h"
#include "ssvm_strpool.h"

#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Limits                                                              */
/* ------------------------------------------------------------------ */

enum
{
    /** Matches the reference's `if (state.fp >= 50) throw 'stack overflow'`. */
    SSVM_MAX_FRAMES = 50,

    /** Per-state instruction budget. Deliberately NOT reset when a suspended
     *  state resumes, matching the reference: a script that suspends a thousand
     *  times still shares one budget, which is what makes this an anti-runaway
     *  guarantee rather than a per-call formality. */
    SSVM_MAX_OPS = 500000,

    /* Depth bounds. test-ss-verify measures the real maximum across the whole
     * corpus and asserts it fits here, so these are measurements rather than
     * guesses. */
    SSVM_INT_STACK_MAX = 256,
    SSVM_STR_STACK_MAX = 128,

    /** Frame locals come out of a per-state bump arena rather than a fixed
     *  array per frame. The corpus maximum is 38 locals; a fixed
     *  int_locals[1024] + str_locals[1024] like CS2VM2_Frame uses would be
     *  ~12 KB per frame and ~600 KB per parked state. */
    SSVM_LOCALS_ARENA_BYTES = 24576,

    /** Args passed to a script at init. */
    SSVM_MAX_INIT_ARGS = 16,

    /** `split_init` is page-oriented, but all split text is owned by one
     *  executing state.  The rev-239 quest journal exposes 210 rows; leave
     *  room for other book/dialogue callers without putting heap ownership in
     *  the game host. */
    SSVM_SPLIT_MAX_LINES = 512,
};

/** Return codes. Values match the reference's ScriptState constants. */
enum SSVM_Exec
{
    SSVM_ABORTED = -1,
    SSVM_RUNNING = 0,
    SSVM_FINISHED = 1,
    /** Waiting on a player delay. */
    SSVM_SUSPENDED = 2,
    /** Waiting for the player to click a resume button. */
    SSVM_PAUSEBUTTON = 3,
    /** Waiting for a count-dialog reply. */
    SSVM_COUNTDIALOG = 4,
    /** Waiting on an npc delay. */
    SSVM_NPC_SUSPENDED = 5,
    /** Parked on the world queue. */
    SSVM_WORLD_SUSPENDED = 6,
};

/** Active-entity slots. Index with SSVM_PRIMARY / SSVM_SECONDARY. */
enum SSVM_EntKind
{
    SSVM_ENT_PLAYER = 0,
    SSVM_ENT_NPC = 1,
    SSVM_ENT_LOC = 2,
    SSVM_ENT_OBJ = 3,
    SSVM_ENT_KIND_COUNT = 4,
};

enum
{
    SSVM_PRIMARY = 0,
    SSVM_SECONDARY = 1,
};

/* ------------------------------------------------------------------ */
/* Host seam                                                           */
/* ------------------------------------------------------------------ */

struct SSVM_State;

/**
 * Every opcode the VM core does not implement itself arrives here.
 *
 * Deliberately one function pointer rather than the request-union shape used by
 * src/cs2vm2/cs2vm2_host.h. That union earns its keep in the client because
 * host requests are asynchronous and must be parked across a yield; here there
 * is neither. It would cost thousands of lines of header carrying nothing the
 * generated opcode table does not already carry, and it still could not
 * describe the variadic queue* commands, which pop a type string and decide
 * from its characters what to pop next.
 *
 * Return 1 when handled, 0 when the opcode is unknown to this host — the VM
 * then reports it through the loud stub rather than silently continuing.
 */
typedef int (*SSVM_HostCommand)(struct SSVM_State* state, int opcode, int dot);

struct SSVM_Host
{
    void* user;
    SSVM_HostCommand command;
};

/* ------------------------------------------------------------------ */
/* Environment                                                         */
/* ------------------------------------------------------------------ */

struct SSVM_Env
{
    struct SSVM_Provider* provider;
    struct SSVM_Host host;

    /** java.util.Random-compatible, so RANDOM/RANDOMINC reproduce the
     *  reference's sequence for a given seed and a session replays exactly. */
    uint64_t rng;

    /** World tick, for MAP_CLOCK. The engine keeps this current. */
    int32_t tick;

    /** Reuse pool for SSVM_StateAlloc. */
    struct SSVM_State* free_list;

    /** Opcodes already reported by the loud stub, so a script in a tick loop
     *  says it once rather than every 600 ms. */
    uint8_t* reported;
};

void
SSVM_EnvInit(
    struct SSVM_Env* env,
    struct SSVM_Provider* provider);

void
SSVM_EnvBindHost(
    struct SSVM_Env* env,
    void* user,
    SSVM_HostCommand command);

void
SSVM_EnvFree(struct SSVM_Env* env);

/** Seeds the java.util.Random-compatible generator. */
void
SSVM_EnvSeed(
    struct SSVM_Env* env,
    uint64_t seed);

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */

struct SSVM_Frame
{
    const struct SSVM_Script* script;
    int32_t pc;
    int32_t* int_locals;
    char** str_locals;
    /** Arena watermark at entry, rewound when the frame pops. */
    size_t locals_mark;
};

struct SSVM_State
{
    struct SSVM_Env* env;

    const struct SSVM_Script* script;
    /** Instruction index. Starts at -1; the loop pre-increments. */
    int32_t pc;
    /** Trigger byte of the entry script's lookup key, or -1. */
    int trigger;
    enum SSVM_Exec execution;
    int32_t opcount;

    int32_t int_stack[SSVM_INT_STACK_MAX];
    int32_t isp;
    /** Borrowed: entries point either into a script's string operands (which
     *  the provider owns and never mutates) or into this state's pool. Never
     *  free one. */
    const char* str_stack[SSVM_STR_STACK_MAX];
    int32_t ssp;

    /** The current frame's locals, hoisted out of frames[fp - 1]. */
    int32_t* int_locals;
    char** str_locals;

    struct SSVM_Frame frames[SSVM_MAX_FRAMES];
    int32_t fp;

    uint32_t pointers;

    /** The entity the script was invoked on, and the eight active slots.
     *  Opaque to the VM. */
    void* self;
    void* active[SSVM_ENT_KIND_COUNT][2];

    /** This op's secondary-pointer flag, latched once per instruction. The
     *  reference re-derives it from intOperand on every access, which breaks
     *  the moment a handler moves pc. */
    int dot;

    /** last_int / the value a queue or countdialog resumed with. */
    int32_t last_int;

    struct SSVM_StrPool pool;

    /** Results of the SPLIT_INIT string primitive.  Entries point into
     *  `pool`, so suspension and nested procs keep the same lifetime as every
     *  other script string. */
    const char* split_lines[SSVM_SPLIT_MAX_LINES];
    int32_t split_line_count;
    int32_t split_lines_per_page;
    int32_t split_mesanim;

    char* locals_arena;
    size_t locals_used;

    struct SSVM_Error err;

    /**
     * Host bookkeeping the VM stores and never reads.
     *
     * Exists because a suspended state outlives the call that created it, so
     * the host cannot keep per-state context on its own stack. The mock uses
     * `host_tag` for the active npc's slot: a raw pointer would dangle if the
     * npc despawned while its script was parked.
     */
    void* host_data;
    int32_t host_tag;

    /** Free-list link, valid only while the state is unused. */
    struct SSVM_State* pool_next;
};

/**
 * Build a state ready to run `script`.
 *
 * `int_args` / `str_args` are bound to the script's declared arguments in
 * order. Returns NULL when the script is NULL or the argument counts do not
 * match what it declares.
 */
struct SSVM_State*
SSVM_StateAlloc(
    struct SSVM_Env* env,
    const struct SSVM_Script* script,
    const int32_t* int_args,
    int int_arg_count,
    const char* const* str_args,
    int str_arg_count);

/** Returns the state to the env's free list and releases its string pool. */
void
SSVM_StateRelease(struct SSVM_State* state);

/**
 * Run until the script finishes, aborts, or suspends.
 *
 * Safe to call again on a suspended state — it resumes where it stopped.
 * Calling it on a finished or aborted state is a no-op that returns the same
 * status.
 */
enum SSVM_Exec
SSVM_Execute(struct SSVM_State* state);

/* ------------------------------------------------------------------ */
/* Host-facing API                                                     */
/* ------------------------------------------------------------------ */

/** 0 on stack underflow, having aborted the script. */
int
SSVM_PopInt(
    struct SSVM_State* state,
    int32_t* out);
int
SSVM_PushInt(
    struct SSVM_State* state,
    int32_t value);

/** The returned pointer is borrowed and valid until the state is released. */
int
SSVM_PopStr(
    struct SSVM_State* state,
    const char** out);

/** Copies into the state's pool, so the caller's buffer need not outlive it. */
int
SSVM_PushStr(
    struct SSVM_State* state,
    const char* text);

/** printf into the state's pool, then push. */
int
SSVM_PushStrFmt(
    struct SSVM_State* state,
    const char* fmt,
    ...);

/** Honours the current op's dot flag. */
void*
SSVM_Active(
    struct SSVM_State* state,
    enum SSVM_EntKind kind);

void*
SSVM_ActiveSlot(
    struct SSVM_State* state,
    enum SSVM_EntKind kind,
    int slot);

void
SSVM_SetActive(
    struct SSVM_State* state,
    enum SSVM_EntKind kind,
    int slot,
    void* entity);

void
SSVM_PointerAdd(
    struct SSVM_State* state,
    uint32_t bits);
void
SSVM_PointerRemove(
    struct SSVM_State* state,
    uint32_t bits);

/** Stop the interpreter with a suspend status; the state stays resumable. */
void
SSVM_Suspend(
    struct SSVM_State* state,
    enum SSVM_Exec status);

/** Abort with a message and a backtrace. */
void
SSVM_Abort(
    struct SSVM_State* state,
    const char* fmt,
    ...);

/** Multi-line "script - file:line" trace of the frames at the abort point.
 *  Empty when the state did not abort. */
const char*
SSVM_Backtrace(struct SSVM_State* state);

/** java.util.Random-compatible: 0 <= result < bound. */
int32_t
SSVM_Random(
    struct SSVM_Env* env,
    int32_t bound);

#endif
