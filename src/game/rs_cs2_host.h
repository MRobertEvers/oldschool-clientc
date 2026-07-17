#ifndef RS_CS2_HOST_H
#define RS_CS2_HOST_H

#include "cs2vm2/cs2vm2_host.h"

#include <stdbool.h>

struct UITree;
struct CacheProvider;
struct InvManager;
struct VarPManager;
struct CS2VM2_Thread;

#define RS_CS2_HOST_VARC_INT_MAX 256
#define RS_CS2_HOST_VARC_STRING_MAX 64
#define RS_CS2_HOST_VARC_STRING_LEN 128

#define RS_CS2_HOST_INV_TRANSMIT_HOOK_MAX 128
#define RS_CS2_HOST_VAR_TRANSMIT_HOOK_MAX 128
#define RS_CS2_HOST_TRANSMIT_TRIGGER_MAX 32
#define RS_CS2_HOST_TRANSMIT_INT_ARG_MAX 16

struct RS_CS2InvTransmitHook
{
    int component_id;
    int script_id;
    int int_args[RS_CS2_HOST_TRANSMIT_INT_ARG_MAX];
    int int_arg_count;
    int trigger_ids[RS_CS2_HOST_TRANSMIT_TRIGGER_MAX];
    int trigger_count;
};

struct RS_CS2VarTransmitHook
{
    int component_id;
    int script_id;
    int int_args[RS_CS2_HOST_TRANSMIT_INT_ARG_MAX];
    int int_arg_count;
    int trigger_ids[RS_CS2_HOST_TRANSMIT_TRIGGER_MAX];
    int trigger_count;
};

struct RS_CS2Host
{
    struct UITree* tree;
    struct CacheProvider* provider;
    struct InvManager* invs;
    struct VarPManager* varps; /* may be NULL */

    bool has_pending;
    struct CS2VM_HostRequest pending;

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
};

void
RS_CS2Host_Init(
    struct RS_CS2Host* host,
    struct UITree* tree,
    struct CacheProvider* provider,
    struct InvManager* invs,
    struct VarPManager* varps);

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
