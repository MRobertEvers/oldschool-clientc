#ifndef RS_CS1_HOST_H
#define RS_CS1_HOST_H

#include "cs1vm/cs1vm.h"
#include "game/rs_player_stats.h"

#include <stdbool.h>

struct UITree;
struct UITreeComponent;
struct CacheProvider;
struct InvManager;
struct VarPManager;

/*
 * Game-side CS1VM host: resolves the VM's state reads against the UI tree, the
 * inventory/varp managers, and the player stat store. Mirrors RS_CS2Host —
 * never reads disk, and stages a cache load into `pending` when an inventory
 * opcode names a component whose interface is not mounted yet.
 */
struct RS_CS1Host
{
    struct UITree* tree;
    struct CacheProvider* provider;
    struct InvManager* invs;          /* may be NULL */
    struct VarPManager* varps;        /* may be NULL */
    struct RS_PlayerStats* stats;     /* may be NULL -> defaults */

    struct CS1VM vm;

    bool has_pending;
    struct CS1VM_HostRequest pending;

    /* What the last yield parked for a cache load. A handler whose resource is
     * still missing after this exact wait must complete with a default instead
     * of yielding again: one opcode, one yield. Cleared in RS_CS1Host_Exec on
     * any non-yield return. */
    bool has_awaited;
    enum CS1VM_HostRequestKind awaited_kind;
    int awaited_id;

    /** Set by the eval task when a cached component result changed; the app
     *  consumes it to trigger a redraw. */
    bool eval_dirty;
};

void
RS_CS1Host_Init(
    struct RS_CS1Host* host,
    struct UITree* tree,
    struct CacheProvider* provider,
    struct InvManager* invs,
    struct VarPManager* varps,
    struct RS_PlayerStats* stats);

/**
 * CS1VM host_exec callback. Expects CS1VM_USER(vm) == RS_CS1Host*.
 * Never reads disk: an inventory opcode naming an unmounted interface stages
 * into host->pending and returns CS1VM_EXECNO_YIELD.
 */
int
RS_CS1Host_Exec(
    struct CS1VM* vm,
    struct CS1VM_HostRequest* request);

/**
 * getIfActive for one component. Returns CS1VM_EVAL_* so the driver can service
 * a staged load and retry; *out_active is false unless evaluation completed.
 */
enum CS1VM_EvalStatus
RS_CS1Host_ComponentActive(
    struct RS_CS1Host* host,
    struct UITreeComponent const* component,
    bool* out_active);

/** getIfVar for %N substitution. *out_value is 0 unless evaluation completed. */
enum CS1VM_EvalStatus
RS_CS1Host_ComponentValue(
    struct RS_CS1Host* host,
    struct UITreeComponent const* component,
    int script_index,
    int* out_value);

#endif /* RS_CS1_HOST_H */
