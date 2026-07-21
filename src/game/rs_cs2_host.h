#ifndef RS_CS2_HOST_H
#define RS_CS2_HOST_H

#include "cs2vm2/cs2vm2_host.h"
#include "input/torirs_keymap.h"

#include <stdbool.h>
#include <stdint.h>

struct UITree;
struct CacheProvider;
struct InvManager;
struct VarPManager;
struct CS2VM2_Thread;
struct UITreeSceneBridge;

#define RS_CS2_HOST_VARC_INT_MAX 256
/* The chatbox input dialog mirrors its string into VarC string 335, so a cap of
 * 64 silently dropped every read and write of it. */
#define RS_CS2_HOST_VARC_STRING_MAX 512
#define RS_CS2_HOST_VARC_STRING_LEN 128

#define RS_CS2_HOST_INV_TRANSMIT_HOOK_MAX 128
#define RS_CS2_HOST_VAR_TRANSMIT_HOOK_MAX 128
#define RS_CS2_HOST_TRANSMIT_TRIGGER_MAX 32
/* Must match CS2VM_HostRequest_IF_SetOnInvTransmit.int_args[32]. */
#define RS_CS2_HOST_TRANSMIT_INT_ARG_MAX 32

struct RS_CS2InvTransmitHook
{
    int component_id;
    int script_id;
    int int_args[RS_CS2_HOST_TRANSMIT_INT_ARG_MAX];
    int int_arg_count;
    /** String args by arg position (see CS2VM_HostRequest str_arg_mask docs).
     *  Replayed into the hook script's string locals on dispatch. */
    uint32_t str_arg_mask;
    int str_arg_count;
    char str_args[CS2VM_SETON_STR_ARG_MAX][CS2VM_SETON_STR_ARG_LEN];
    int trigger_ids[RS_CS2_HOST_TRANSMIT_TRIGGER_MAX];
    int trigger_count;
    /** inv_change_serial this hook last fired for (0 = never fired). Hooks fire
     *  once when first dispatched visible, then only when the serial advances
     *  (TS parity: node.lastChangedInvCount vs cycles.changedInvCount). */
    uint32_t last_seen_serial;
};

struct RS_CS2VarTransmitHook
{
    int component_id;
    int script_id;
    int int_args[RS_CS2_HOST_TRANSMIT_INT_ARG_MAX];
    int int_arg_count;
    /** String args by arg position (see CS2VM_HostRequest str_arg_mask docs). */
    uint32_t str_arg_mask;
    int str_arg_count;
    char str_args[CS2VM_SETON_STR_ARG_MAX][CS2VM_SETON_STR_ARG_LEN];
    int trigger_ids[RS_CS2_HOST_TRANSMIT_TRIGGER_MAX];
    int trigger_count;
    /** var_change_serial this hook last fired for (0 = never fired). */
    uint32_t last_seen_serial;
};

struct RS_CS2Host
{
    struct UITree* tree;
    struct CacheProvider* provider;
    struct InvManager* invs;
    struct VarPManager* varps;        /* may be NULL */
    struct UITreeSceneBridge* bridge; /* may be NULL until set */

    bool has_pending;
    struct CS2VM_HostRequest pending;

    /* What the last yield parked for a cache load. A handler whose resource is
     * still missing after this exact wait must complete with a default instead
     * of yielding again: one opcode, one yield. Cleared in RS_CS2Host_Exec on
     * any non-yield return. */
    bool has_awaited;
    enum CS2VM_HostRequestKind awaited_kind;
    int awaited_id;
    int awaited_id2; /* second resource of a two-resource request, else -1 */

    int varc_int[RS_CS2_HOST_VARC_INT_MAX];
    char varc_string[RS_CS2_HOST_VARC_STRING_MAX][RS_CS2_HOST_VARC_STRING_LEN];

    int client_clock;
    int viewport_w;
    int viewport_h;
    /** IF_GETTOP / client type (default 80). */
    int client_type;

    struct RS_CS2InvTransmitHook inv_transmit_hooks[RS_CS2_HOST_INV_TRANSMIT_HOOK_MAX];
    int inv_transmit_hook_count;

    struct RS_CS2VarTransmitHook var_transmit_hooks[RS_CS2_HOST_VAR_TRANSMIT_HOOK_MAX];
    int var_transmit_hook_count;

    /** Set when IF_SETHIDE unhides a subtree (TS markWidgetsLoaded). Consumed once
     *  per logic tick by RS_CS2_PumpTransmits; per-hook last_seen_serial gating
     *  keeps already-fired hooks from re-running. */
    int widgets_loaded_dirty;
    /** Bumped when a var/inv value actually changes (none do yet in this port —
     *  future server integration bumps these at the write site). Start at 1 so
     *  freshly registered hooks (last_seen_serial=0) fire once on first dispatch. */
    uint32_t var_change_serial;
    uint32_t inv_change_serial;

    /** Live CS2 event locals for script arg substitution (drag / mouse). */
    int event_mouse_x;
    int event_mouse_y;
    int event_drag_target_id;
    int event_drag_target_child_index;

    /**
     * Live onKey event locals. The naming follows the reference and is INVERTED
     * relative to canonical OSRS clientscript naming: event_key_typed carries
     * the OSRS internal KEY CODE (-1 when the event is a typed character), and
     * event_key_pressed carries the CHARACTER code (0 when it is a key code).
     * See struct LibToriRS_KeyEvent -- both ends must stay inverted together.
     */
    int event_key_typed;
    int event_key_pressed;

    /** Op index (1..10) for the hook being dispatched; 1 is the primary
     *  left-click op, which is what every mouse-driven dispatch uses. */
    int event_op_index;
    int event_op_subindex;

    /** Per-frame key state snapshot, indexed by OSRS internal code, backing the
     *  KEYHELD and KEYPRESSED opcodes. Refreshed by RS_CS2_SyncKeyState. */
    unsigned char osrs_key_held[TORIRS_OSRSKEY_COUNT];
    unsigned char osrs_key_pressed[TORIRS_OSRSKEY_COUNT];
};

void
RS_CS2Host_Init(
    struct RS_CS2Host* host,
    struct UITree* tree,
    struct CacheProvider* provider,
    struct InvManager* invs,
    struct VarPManager* varps);

void
RS_CS2Host_SetBridge(
    struct RS_CS2Host* host,
    struct UITreeSceneBridge* bridge);

/** Advance CLIENTCLOCK once per game tick. */
void
RS_CS2Host_Tick(struct RS_CS2Host* host);

/**
 * CS2VM2 host_exec callback. Expects CS2VM_USER(thread) == RS_CS2Host*.
 * Never reads disk: missing clientscript / component / sprite / font / enum /
 * struct / obj / model stages into host->pending and returns YIELD.
 */
int
RS_CS2Host_Exec(
    struct CS2VM2_Thread* thread,
    struct CS2VM_HostRequest* request);

#endif /* RS_CS2_HOST_H */
