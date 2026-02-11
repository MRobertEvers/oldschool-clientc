#ifndef VARP_VARBIT_MANAGER_H
#define VARP_VARBIT_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

struct FileListDat;

/** Varp (player variable) config from varp.dat */
struct VarPType
{
    int clientcode; /* 0=none, 1=colortable, 3=midi, 4=wave, etc. */
};

/** Varbit config from varbit.dat - maps to bits within a varp */
struct VarBitType
{
    int basevar;  /* varp index */
    int startbit; /* LSB of bit range */
    int endbit;   /* MSB of bit range (exclusive) */
};

#define VARP_VARBIT_READBIT_MAX 32

/** Callback when a varp changes (e.g. redraw sidebar, update client settings). */
typedef void (*VarPVarBitClientVarFn)(void* userdata, int varp_id);

/** Manages varp/varbit state and network protocol updates */
struct VarPVarBitManager
{
    struct VarPType* varp_types;
    int varp_count;

    struct VarBitType* varbit_types;
    int varbit_count;

    /* Runtime state: var[id] = current value; var_serv[id] = server authoritative */
    int* var;
    int* var_serv;

    /* readbit[n] = (1 << n) - 1, mask for extracting n bits */
    int readbit[VARP_VARBIT_READBIT_MAX];

    VarPVarBitClientVarFn client_var_fn;
    void* client_var_userdata;
};

void
varp_varbit_init(struct VarPVarBitManager* mgr);

void
varp_varbit_free(struct VarPVarBitManager* mgr);

/** Load varp.dat and varbit.dat from config jagfile. Call after config is loaded. */
bool
varp_varbit_load_from_config_jagfile(
    struct VarPVarBitManager* mgr,
    struct FileListDat* config_jagfile);

/** Get varp value by id */
int
varp_varbit_get_varp(const struct VarPVarBitManager* mgr, int id);

/** Get varbit value by id. Varbits are bit ranges within a varp. */
int
varp_varbit_get_varbit(const struct VarPVarBitManager* mgr, int id);

/** Apply VARP_SMALL: variable (g2), value (g1 signed byte) */
void
varp_varbit_apply_small(
    struct VarPVarBitManager* mgr,
    int variable,
    int value);

/** Apply VARP_LARGE: variable (g2), value (g4) */
void
varp_varbit_apply_large(
    struct VarPVarBitManager* mgr,
    int variable,
    int value);

/** Apply VARP_SYNC: reset all vars to server authoritative set (no payload) */
void
varp_varbit_apply_sync(struct VarPVarBitManager* mgr);

/** Optimistic update: set var locally (e.g. TOGGLE/SELECT button click).
 * Does not update var_serv; server will send VARP_SMALL/LARGE to confirm. */
void
varp_varbit_set_varp_optimistic(
    struct VarPVarBitManager* mgr,
    int variable,
    int value);

void
varp_varbit_set_client_var_callback(
    struct VarPVarBitManager* mgr,
    VarPVarBitClientVarFn fn,
    void* userdata);

#endif /* VARP_VARBIT_MANAGER_H */
