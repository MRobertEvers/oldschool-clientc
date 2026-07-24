#ifndef VARC_MANAGER_H
#define VARC_MANAGER_H

#include <stdbool.h>

/*
 * VarC (client variable) store — the CS2 "varcInt" / "varcString" address
 * space. Unlike varps (server-authoritative, see varp/varp_manager.h), varcs
 * live entirely on the client: CS2 scripts read and write them, and interface
 * transmit hooks re-run when they change. This manager mirrors VarPManager's
 * shape (dynamic-growing state + a change callback) so the reactive loop is the
 * same on both: a write that actually changes a value fires change_fn, which the
 * owner uses to bump the CS2 var-change serial and re-dispatch var-transmit
 * hooks. Ids are dense and small in practice; the arrays grow on demand.
 */

/** Fired when a varc value actually changes (write with a new value). */
typedef void (*VarCManager_ChangeFn)(
    void* userdata,
    int varc_id,
    bool is_string);

struct VarCManager
{
    int* ints;
    int int_count;

    char** strings; /* each entry heap-allocated (strdup); NULL = "" */
    int string_count;

    VarCManager_ChangeFn change_fn;
    void* change_userdata;
};

void
VarCManager_Init(struct VarCManager* mgr);

void
VarCManager_Free(struct VarCManager* mgr);

void
VarCManager_SetChangeCallback(
    struct VarCManager* mgr,
    VarCManager_ChangeFn fn,
    void* userdata);

/** Read varc int `id`; 0 when never set / out of range. */
int
VarCManager_GetInt(
    const struct VarCManager* mgr,
    int id);

/** Write varc int `id` (grows the store as needed); fires change_fn on a real
 *  change. */
void
VarCManager_SetInt(
    struct VarCManager* mgr,
    int id,
    int value);

/** Read varc string `id`; "" when never set / out of range. Never NULL. */
const char*
VarCManager_GetString(
    const struct VarCManager* mgr,
    int id);

/** Write varc string `id` (copies `value`, NULL treated as ""); fires change_fn
 *  on a real change. */
void
VarCManager_SetString(
    struct VarCManager* mgr,
    int id,
    const char* value);

/** Zero every int and clear every string (fires change_fn per changed id). */
void
VarCManager_ResetAll(struct VarCManager* mgr);

#endif /* VARC_MANAGER_H */
