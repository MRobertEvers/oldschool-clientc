#ifndef VARP_MANAGER_H
#define VARP_MANAGER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Varp (player variable) config from varp.dat */
struct VarPType
{
    int clientcode; /* 0=none, 1=colortable, 3=midi, 4=wave, etc. */
};

/** Varbit config from varbit.dat — maps to bits within a varp */
struct VarBitType
{
    int basevar;  /* varp index */
    int startbit; /* LSB of bit range */
    /**
     * MSB of the bit range, **inclusive**.
     *
     * So the range is `endbit - startbit + 1` bits wide, and `startbit == endbit`
     * is a legal one-bit varbit — which is the commonest shape by far: varbits
     * 0..4 of the rev-230 cache are five consecutive single-bit flags packed into
     * varp 318.
     *
     * Matches the reference, which masks with `(1 << (msb - lsb + 1)) - 1`.
     */
    int endbit;
};

#define VARP_MANAGER_READBIT_MAX 32

/**
 * The map editor's var preview (TORIRSMAPED_STATE_VAR_PREVIEW; the layout is
 * the shell editor's, src/editor/builder/builder_shell.h): up to this many
 * vars whose transform resolve uses a stated value instead of the player's.
 */
#define VARP_MANAGER_PREVIEW_MAX 5

/** One previewed var: a varp or a varbit by id, and the value a shell keyed
 *  on exactly that var resolves with. */
struct VarPManagerPreview
{
    /** 1 = `id` is a varbit, 0 = a varp. */
    int is_varbit;
    int id;
    int value;
};

/** Callback when a client-visible varp value changes (any source). */
typedef void (*VarPManager_ChangeFn)(
    void* userdata,
    int varp_id);

/**
 * Callback when a **server** varp update lands: VARP_SMALL/VARP_LARGE/SYNC/RESET.
 *
 * Separate from VarPManager_ChangeFn because only this set may drive widget
 * var-transmit hooks. The reference pushes the id into its changed-varp ring
 * (`Client.field990[++field1031 - 1 & 31] = varp`) from exactly the three
 * server-packet handlers; a script-side write (CS2 SETVARP, IF1 button, varbit
 * set) updates `Varps_main` and notifies the server, but never touches the ring.
 * Feeding script writes back into the transmit dispatch makes any hook that
 * writes a var re-trigger itself forever.
 *
 * Fires per applied update even when the value did not change — the reference
 * bumps the ring outside its equality check — and with `varp_id < 0` for a
 * whole-cache reset (`field1031 += 32`, i.e. "assume every hook").
 */
typedef void (*VarPManager_ServerUpdateFn)(
    void* userdata,
    int varp_id);

/** Manages varp/varbit state and network protocol updates. */
struct VarPManager
{
    struct VarPType* varp_types;
    /**
     * Length of `varp_types`, which is NOT `varp_count`.
     *
     * The type table is a cache fact — one record per varplayer the cache
     * declares — while the value arrays are sized by what actually gets
     * written. This tree lets content allocate varps ABOVE the cache's highest
     * id (torirs_server.h TORIRSSERVER_VARP_SERVER_HEADROOM), so those two numbers
     * genuinely differ: id 6000 has a value and no type. Reading a clientcode
     * for such an id must answer "none", not walk off the end of the table.
     */
    int varp_type_count;
    int varp_count;

    struct VarBitType* varbit_types;
    int varbit_count;

    /* Runtime state: var[id] = current; var_serv[id] = server authoritative */
    int* var;
    int* var_serv;

    /*
     * The long arm: VARP_LONG (rev 241) and the CS2 long varp opcodes 64/65
     * (ge_newoffer_price, 5753, moved here from varbit 4398 in 241). Separate
     * arrays beside the int arm, same current/server split, as the rev-241 deob
     * keeps them (field4355 / field5648). `var_long_set[id]` is its field4357:
     * which arm of the id is live, and so which one VARP_SYNC restores.
     */
    int64_t* var_long;
    int64_t* var_long_serv;
    unsigned char* var_long_set;
    int var_long_count;

    /* readbit[n] = (1 << n) - 1, mask for extracting n bits */
    int readbit[VARP_MANAGER_READBIT_MAX];

    VarPManager_ChangeFn change_fn;
    void* change_userdata;

    VarPManager_ServerUpdateFn server_update_fn;
    void* server_update_userdata;

    /** The editor's var preview (VarPManager_SetPreview): read by
     *  VarPManager_ResolveTransform alone. Every other reader -- CS2, the
     *  wire, the varbit getters -- sees the player's own values. */
    struct VarPManagerPreview preview[VARP_MANAGER_PREVIEW_MAX];
    int preview_count;
};

void
VarPManager_Init(struct VarPManager* mgr);

void
VarPManager_Free(struct VarPManager* mgr);

/** Load varp.dat from a raw buffer (g2 count + per-id decode). Allocates var/var_serv. */
bool
VarPManager_LoadVarpDat(
    struct VarPManager* mgr,
    const uint8_t* data,
    size_t size);

/** Load varbit.dat from a raw buffer (g2 count + per-id decode). */
bool
VarPManager_LoadVarbitDat(
    struct VarPManager* mgr,
    const uint8_t* data,
    size_t size);

/** Copy prebuilt varp types and allocate/resize var/var_serv to match. */
bool
VarPManager_SetVarpTypes(
    struct VarPManager* mgr,
    const struct VarPType* types,
    int count);

/** Copy prebuilt varbit types. */
bool
VarPManager_SetVarbitTypes(
    struct VarPManager* mgr,
    const struct VarBitType* types,
    int count);

int
VarPManager_GetVarp(
    const struct VarPManager* mgr,
    int id);

int
VarPManager_GetVarbit(
    const struct VarPManager* mgr,
    int id);

/**
 * The varp a varbit is packed into, or -1 when the id is unknown.
 *
 * Var-transmit hooks list *varp* ids as their triggers, so a caller that writes
 * a varbit and needs to announce the change has to name the base varp — the
 * varbit id itself matches no hook.
 */
int
VarPManager_VarbitBaseVar(
    const struct VarPManager* mgr,
    int varbit_id);

int
VarPManager_GetClientcode(
    const struct VarPManager* mgr,
    int id);

/** Apply VARP_SMALL: variable (g2), value (g1 signed byte). */
void
VarPManager_ApplySmall(
    struct VarPManager* mgr,
    int variable,
    int value);

/** Apply VARP_LARGE: variable (g2), value (g4). */
void
VarPManager_ApplyLarge(
    struct VarPManager* mgr,
    int variable,
    int value);

/** Apply VARP_LONG: the varp's long value. Fires change and server-update
 *  exactly as VARP_LARGE does. */
void
VarPManager_ApplyLong(
    struct VarPManager* mgr,
    int variable,
    int64_t value);

/** The varp read as a long (CS2 64): its long arm, 0 when never written. Marks
 *  the id long, as the reference's opcode 64 does. */
int64_t
VarPManager_GetVarpLong(
    struct VarPManager* mgr,
    int variable);

/** Optimistic long write (CS2 65): set locally; does not update the server copy. */
void
VarPManager_SetVarpLongOptimistic(
    struct VarPManager* mgr,
    int variable,
    int64_t value);

/** VARP_SYNC: restore client-visible vars from the server-authoritative set. */
void
VarPManager_ApplySync(struct VarPManager* mgr);

/** VARP_RESET: zero every var (client + server-authoritative). */
void
VarPManager_ResetAll(struct VarPManager* mgr);

/** Optimistic update: set var locally. Does not update var_serv. */
void
VarPManager_SetVarpOptimistic(
    struct VarPManager* mgr,
    int variable,
    int value);

/** Optimistic varbit write: insert bits into base varp, then set optimistically. */
void
VarPManager_SetVarbitOptimistic(
    struct VarPManager* mgr,
    int varbit_id,
    int value);

void
VarPManager_SetChangeCallback(
    struct VarPManager* mgr,
    VarPManager_ChangeFn fn,
    void* userdata);

void
VarPManager_SetServerUpdateCallback(
    struct VarPManager* mgr,
    VarPManager_ServerUpdateFn fn,
    void* userdata);

/**
 * Set the editor's var preview: `count` entries (0 clears it). A multiloc or
 * multinpc keyed on a previewed var resolves (VarPManager_ResolveTransform)
 * with the stated value instead of the player's -- exactly that var, as the
 * shell names it: a varbit preview does not move a shell keyed on the varp
 * that packs it, nor the other way round. The player's values are untouched.
 *
 * Every var the old or the new preview names is then announced through the
 * change callback (VarPManager_ChangeFn) as the BASE varp the wire would carry
 * (a varbit's basevar), so whatever re-resolves transforms on a varp change
 * -- the client's placed locs and npcs -- re-resolves these. A varbit whose
 * type is not loaded has no base varp to announce; it is skipped there and
 * still resolves. Returns how many varps were announced.
 */
int
VarPManager_SetPreview(
    struct VarPManager* mgr,
    const struct VarPManagerPreview* entries,
    int count);

/** Resolve a loc transform target id from transforms[] using varbit/varp state
 * -- and the editor's var preview first (VarPManager_SetPreview).
 * Returns -1 when the loc should be hidden. Requires transform_count > 0. */
int
VarPManager_ResolveTransform(
    const struct VarPManager* mgr,
    const int* transforms,
    int transform_count,
    int transform_varbit,
    int transform_varp);

/**
 * Whether a transform table would resolve differently if `varp_id` changed.
 *
 * A multiloc or multinpc picks its child from one of two keys: a varp
 * directly, or a VARBIT -- which is a field packed inside some varp, and it is
 * that BASE varp the wire actually carries. So a table keyed on a varbit still
 * depends on a varp, just not one it names anywhere, and a reader that only
 * compares `transform_varp` sees no dependency at all.
 *
 * That is the failure this exists to prevent, and its symptom is a door that
 * never opens or an npc that never changes state while every packet involved
 * arrives correctly: the varp lands, nothing thinks it is interested, and the
 * placement keeps the child it resolved at scene build.
 *
 * A table with no entries depends on nothing -- that is a record with no
 * transform at all, not one whose key is missing.
 */
bool
VarPManager_TransformDependsOnVarp(
    const struct VarPManager* mgr,
    const int* transforms,
    int transform_count,
    int transform_varbit,
    int transform_varp,
    int varp_id);

/* --- session checkpoint (replay/replay_checkpoint.h) ---------------------
 * A private copy of the varp VALUES (not the cache's type tables, not the
 * callbacks). Restore replaces the value arrays whole, then fires the change
 * callback for every varp whose value moved, as a packet's write would (loc
 * and npc transforms re-resolve from it) -- but not the server-update one:
 * what the transmit hooks made of the values is restored with them. */
struct VarPManager*
VarPManager_CheckpointTake(struct VarPManager const* mgr);

void
VarPManager_CheckpointRestore(
    struct VarPManager* mgr,
    struct VarPManager const* cp);

/** Accepts NULL. */
void
VarPManager_CheckpointFree(struct VarPManager* cp);

size_t
VarPManager_CheckpointBytes(struct VarPManager const* cp);

/** 1 when equal; else 0 and the first difference in `why`. */
int
VarPManager_CheckpointEqual(
    struct VarPManager const* a,
    struct VarPManager const* b,
    char* why,
    int why_cap);

#endif /* VARP_MANAGER_H */
