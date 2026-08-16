#ifndef RUNESCAPE_CS2_HOST_H
#define RUNESCAPE_CS2_HOST_H

#include "../vm/cs2vmx.h"
#include "../vm/cs2vmx_host.h"

#include <stdbool.h>

struct GameRunescape;
struct CS2VMX;

#define RS_CS2_HOST_VARC_INT_MAX 256
#define RS_CS2_HOST_VARC_STRING_MAX 64
#define RS_CS2_HOST_VARC_STRING_LEN 128

#define RS_CS2_HOST_INV_TRANSMIT_HOOK_MAX 128
#define RS_CS2_HOST_VAR_TRANSMIT_HOOK_MAX 128
#define RS_CS2_HOST_TRANSMIT_TRIGGER_MAX 32
#define RS_CS2_HOST_TRANSMIT_INT_ARG_MAX 16

struct GameRunescapeCS2InvTransmitHook
{
    int component_id;
    int script_id;
    int int_args[RS_CS2_HOST_TRANSMIT_INT_ARG_MAX];
    int int_arg_count;
    int trigger_ids[RS_CS2_HOST_TRANSMIT_TRIGGER_MAX];
    int trigger_count;
};

struct GameRunescapeCS2VarTransmitHook
{
    int component_id;
    int script_id;
    int int_args[RS_CS2_HOST_TRANSMIT_INT_ARG_MAX];
    int int_arg_count;
    int trigger_ids[RS_CS2_HOST_TRANSMIT_TRIGGER_MAX];
    int trigger_count;
};

struct GameRunescapeCS2Host
{
    struct GameRunescape* game;

    bool has_pending;
    struct CS2VM_HostRequest pending;

    int varc_int[RS_CS2_HOST_VARC_INT_MAX];
    char varc_string[RS_CS2_HOST_VARC_STRING_MAX][RS_CS2_HOST_VARC_STRING_LEN];

    int client_clock;
    int viewport_w;
    int viewport_h;
    /** IF_GETTOP / client type (default 80). */
    int client_type;

    struct GameRunescapeCS2InvTransmitHook inv_transmit_hooks[RS_CS2_HOST_INV_TRANSMIT_HOOK_MAX];
    int inv_transmit_hook_count;

    struct GameRunescapeCS2VarTransmitHook var_transmit_hooks[RS_CS2_HOST_VAR_TRANSMIT_HOOK_MAX];
    int var_transmit_hook_count;
};

void
GameRunescape_CS2HostInit(
    struct GameRunescapeCS2Host* host,
    struct GameRunescape* game);

/** Advance CLIENTCLOCK once per game tick. */
void
GameRunescape_CS2HostTick(struct GameRunescapeCS2Host* host);

/**
 * CS2VMX host_exec callback. Expects CS2VM_USER(vm) == GameRunescapeCS2Host*.
 * Never reads disk: missing clientscript / interface group / sprite / font / enum /
 * struct / obj / model / obj-icon stages into host->pending and returns YIELD.
 */
int
GameRunescape_CS2HostExec(
    struct CS2VMX* vm,
    struct CS2VM_HostRequest* request);

#endif /* RUNESCAPE_CS2_HOST_H */
