#ifndef GAME_INTERFACE_EDITOR_CS2_HOST_H
#define GAME_INTERFACE_EDITOR_CS2_HOST_H

#include "../vm/cs2vmx.h"
#include "../vm/cs2vmx_host.h"

#include <stdbool.h>

struct GameInterfaceEditor;
struct CS2VMX;

#define IE_CS2_HOST_VARC_INT_MAX 256
#define IE_CS2_HOST_VARC_STRING_MAX 64
#define IE_CS2_HOST_VARC_STRING_LEN 128

struct GameInterfaceEditorCS2Host
{
    struct GameInterfaceEditor* game;

    bool has_pending;
    struct CS2VM_HostRequest pending;

    int varc_int[IE_CS2_HOST_VARC_INT_MAX];
    char varc_string[IE_CS2_HOST_VARC_STRING_MAX][IE_CS2_HOST_VARC_STRING_LEN];

    int client_clock;
    int viewport_w;
    int viewport_h;
    /** IF_GETTOP / client type (default 80). */
    int client_type;
};

void
GameInterfaceEditor_CS2HostInit(
    struct GameInterfaceEditorCS2Host* host,
    struct GameInterfaceEditor* game);

/** Advance CLIENTCLOCK once per editor script tick. */
void
GameInterfaceEditor_CS2HostTick(struct GameInterfaceEditorCS2Host* host);

/**
 * CS2VMX host_exec callback. Expects CS2VM_USER(vm) == GameInterfaceEditorCS2Host*.
 * Editor cache is usually preloaded; missing assets soft-fail (no async yield pump).
 */
int
GameInterfaceEditor_CS2HostExec(
    struct CS2VMX* vm,
    struct CS2VM_HostRequest* request);

#endif /* GAME_INTERFACE_EDITOR_CS2_HOST_H */
