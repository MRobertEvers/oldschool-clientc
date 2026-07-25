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

/** Callback when a client-visible varp value changes. */
typedef void (*VarPManager_ChangeFn)(
    void* userdata,
    int varp_id);

/** Manages varp/varbit state and network protocol updates. */
struct VarPManager
{
    struct VarPType* varp_types;
    int varp_count;

    struct VarBitType* varbit_types;
    int varbit_count;

    /* Runtime state: var[id] = current; var_serv[id] = server authoritative */
    int* var;
    int* var_serv;

    /* readbit[n] = (1 << n) - 1, mask for extracting n bits */
    int readbit[VARP_MANAGER_READBIT_MAX];

    VarPManager_ChangeFn change_fn;
    void* change_userdata;
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

/** Apply VARP_SYNC: reset all vars to server authoritative set. */
void
VarPManager_ApplySync(struct VarPManager* mgr);

/** RESET_CLIENT_VARCACHE: zero every var (client + server-authoritative). */
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

/** Resolve a loc transform target id from transforms[] using varbit/varp state.
 * Returns -1 when the loc should be hidden. Requires transform_count > 0. */
int
VarPManager_ResolveTransform(
    const struct VarPManager* mgr,
    const int* transforms,
    int transform_count,
    int transform_varbit,
    int transform_varp);

#endif /* VARP_MANAGER_H */
