#ifndef CLIENTSCRIPT_VM_H
#define CLIENTSCRIPT_VM_H

#include <stdbool.h>
#include <stdint.h>

struct GGame;
struct FileListDat;

/* Matches integer clientcode field in varp.dat; zero-or-unknown = NONE. */
enum VarClientCodeKind {
    VAR_CLIENTCODE_NONE             = 0,
    VAR_CLIENTCODE_BRIGHTNESS       = 1,  /* colortable update */
    VAR_CLIENTCODE_MUSIC_VOLUME     = 3,  /* midi volume       */
    VAR_CLIENTCODE_WAVE_VOLUME      = 4,  /* wave/sfx volume   */
    VAR_CLIENTCODE_ONE_MOUSE        = 7,  /* one-button mouse  */
    VAR_CLIENTCODE_CHAT_EFFECTS     = 9,
    VAR_CLIENTCODE_SPLIT_PRIVATE_CHAT = 10,
    VAR_CLIENTCODE_BANK_ARRANGE     = 11,
};

/** Varp config entry owned by ClientScriptVM. */
struct VarPType {
    enum VarClientCodeKind clientcode;
};

/** Varbit config entry: bit range [startbit, endbit) within var[basevar]. */
struct VarBitType {
    int basevar;
    int startbit;
    int endbit;
};

#define VARP_VARBIT_READBIT_MAX 32
#define CLIENT_VM_DIRTY_VARP_QUEUE 64

/** Revision-agnostic bytecode for a single component script. */
struct UIScriptBytecode {
    int* code; /* word stream */
    int  len;
};

/**
 * Holds state for interface "client script" evaluation (varp/varbit/stat bytecode on components).
 * Now the canonical owner of var / var_serv / varp_types / varbit_types.
 * Clientcode side-effects are communicated to the game loop via the dirty_varps queue
 * (no function pointers; drained once per frame by clientscript_vm_drain_clientcodes).
 */
struct ClientScriptVM {
    /* Non-owning back-reference to GGame for opcode evaluation (stats, inventory, coords). */
    struct GGame* game_ref;

    /* Type tables loaded from config jagfile. */
    struct VarPType*   varp_types;   int varp_count;
    struct VarBitType* varbit_types; int varbit_count;

    /* Runtime values. */
    int* var;      /* current (client-side) values */
    int* var_serv; /* server-authoritative copy     */

    /* Bit-extraction masks: readbit[n] = (1 << n) - 1 */
    int readbit[VARP_VARBIT_READBIT_MAX];

    /* Dirty queue: varp ids whose clientcode side-effect must be dispatched. */
    int dirty_varps[CLIENT_VM_DIRTY_VARP_QUEUE];
    int dirty_varp_count;

    /* Evaluation stack (kept for future full ClientScript support). */
    int int_stack[256];
    int int_stack_ptr;
};

/* ── Lifecycle ─────────────────────────────────────────────────────────── */

struct ClientScriptVM* clientscript_vm_new(void);
void                   clientscript_vm_free(struct ClientScriptVM* vm);

/** Load varp types from varp.dat in config_jagfile; allocates var/var_serv arrays. */
bool clientscript_vm_load_types(
    struct ClientScriptVM* vm,
    struct FileListDat*    config_jagfile);

/* ── Var / varbit read ─────────────────────────────────────────────────── */

int clientscript_vm_get_var(const struct ClientScriptVM* vm, int id);
int clientscript_vm_get_varbit(const struct ClientScriptVM* vm, int id);

/* ── Network packet handlers (append changed ids to dirty queue) ─────── */

void clientscript_vm_apply_varp_small(struct ClientScriptVM* vm, int variable, int value);
void clientscript_vm_apply_varp_large(struct ClientScriptVM* vm, int variable, int value);
void clientscript_vm_apply_varp_sync(struct ClientScriptVM* vm);

/** Optimistic local update (button toggle/select before server ACK). */
void clientscript_vm_set_var_optimistic(struct ClientScriptVM* vm, int variable, int value);

/* ── Frame drain ───────────────────────────────────────────────────────── */

/**
 * Called once per frame from LibToriRS_FrameBegin.
 * Drains dirty_varps and dispatches clientcode side-effects into GGame/Chat fields.
 * No function pointers; pure switch on enum VarClientCodeKind.
 */
void clientscript_vm_drain_clientcodes(struct ClientScriptVM* vm, struct GGame* game);

/* ── Script evaluation (component scripts) ────────────────────────────── */

/** Evaluate script `script_id` on a UIScriptBytecode array; returns accumulated value. */
int clientscript_vm_eval(
    struct ClientScriptVM*        vm,
    const struct UIScriptBytecode* script);

/**
 * Evaluate all scripts on a component's bytecode+comparator arrays and return
 * whether the component is "active" (all comparisons pass).
 * Signature is revision-agnostic: takes raw arrays from StaticUIComponent.
 */
bool clientscript_vm_active(
    struct ClientScriptVM*        vm,
    const struct UIScriptBytecode* scripts,
    int                            scripts_count,
    const uint8_t*                 script_comparator,
    const int*                     script_operand);

/*
 * Legacy shims — kept for callers that still pass CacheDatConfigComponent* during
 * the ui_agnostic_translation transition.  Will be removed once that todo is done.
 */
struct CacheDatConfigComponent;

int  clientscript_vm_if_var(
    struct ClientScriptVM*        vm,
    struct GGame*                 game,
    struct CacheDatConfigComponent* component,
    int                           script_id);

bool clientscript_vm_if_active(
    struct ClientScriptVM*        vm,
    struct GGame*                 game,
    struct CacheDatConfigComponent* component);

#endif /* CLIENTSCRIPT_VM_H */
