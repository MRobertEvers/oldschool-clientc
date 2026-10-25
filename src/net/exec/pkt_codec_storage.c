/* Element storage for caller-array codecs. See pkt_codec_storage.h. */
#include "pkt_codec_storage.h"

#include "packets/if_initialstate.h"
#include "packets/if_resync.h"
#include "packets/if_resync_v1.h"
#include "packets/if_resync_v2.h"
#include "packets/loc_add_change_v2.h"
#include "packets/runclientscript.h"
#include "packets/update_friendlist.h"
#include "packets/update_ignorelist.h"
#include "packets/update_inv_full.h"
#include "packets/update_inv_partial.h"

#include <assert.h>
#include <string.h>

static union
{
    long double align;
    uint8_t bytes[1 << 20];
} g_pool[2];

static MsgRunClientScriptArg g_cs2_args[2][MSG_RUNCLIENTSCRIPT_MAX_ARGS];
static int32_t g_cs2_ints[2][MSG_RUNCLIENTSCRIPT_MAX_ARGS][MSG_RUNCLIENTSCRIPT_MAX_ELEMS];
static const char *g_cs2_strs[2][MSG_RUNCLIENTSCRIPT_MAX_ARGS][MSG_RUNCLIENTSCRIPT_MAX_ELEMS];

#define POOL(T, side) ((T *)(void *)g_pool[side].bytes)
#define POOL_HALF(T, side) ((T *)(void *)(g_pool[side].bytes + sizeof(g_pool[side].bytes) / 2))

void
pkt_codec_storage_provide(char const* prot, void* msg, int side)
{
    assert(prot);
    assert(msg);
    assert(side == 0 || side == 1);
    if (strcmp(prot, "IF_RESYNC_V2") == 0) {
        MsgIfResyncV2 *m = msg;
        m->sub_interfaces = POOL(Rsprot_MsgIfResyncV2_sub_interfacesElem, side);
        m->events = POOL_HALF(Rsprot_MsgIfResyncV2_eventsElem, side);
    } else if (strcmp(prot, "IF_RESYNC_V1") == 0) {
        MsgIfResyncV1 *m = msg;
        m->sub_interfaces = POOL(Rsprot_MsgIfResyncV1_sub_interfacesElem, side);
        m->events = POOL_HALF(Rsprot_MsgIfResyncV1_eventsElem, side);
    } else if (strcmp(prot, "IF_RESYNC") == 0) {
        MsgIfResync *m = msg;
        m->sub_interfaces = POOL(Rsprot_MsgIfResync_sub_interfacesElem, side);
        m->events = POOL_HALF(Rsprot_MsgIfResync_eventsElem, side);
    } else if (strcmp(prot, "IF_INITIALSTATE") == 0) {
        MsgIfInitialState *m = msg;
        m->sub_interfaces = POOL(Rsprot_MsgIfInitialState_sub_interfacesElem, side);
        m->events = POOL_HALF(Rsprot_MsgIfInitialState_eventsElem, side);
    } else if (strcmp(prot, "LOC_ADD_CHANGE_V2") == 0) {
        ((MsgLocAddChangeV2 *)msg)->ops = POOL(Rsprot_MsgLocAddChangeV2_opsElem, side);
    } else if (strcmp(prot, "UPDATE_FRIENDLIST") == 0) {
        ((MsgUpdateFriendList *)msg)->entries = POOL(MsgUpdateFriendListEntry, side);
    } else if (strcmp(prot, "UPDATE_IGNORELIST") == 0) {
        ((MsgUpdateIgnoreList *)msg)->entries = POOL(MsgUpdateIgnoreListEntry, side);
    } else if (strcmp(prot, "UPDATE_INV_FULL") == 0) {
        ((MsgUpdateInvFull *)msg)->slots = POOL(MsgUpdateInvFullSlot, side);
    } else if (strcmp(prot, "UPDATE_INV_PARTIAL") == 0) {
        ((MsgUpdateInvPartial *)msg)->slots = POOL(MsgUpdateInvPartialSlot, side);
    } else if (strcmp(prot, "RUNCLIENTSCRIPT") == 0) {
        MsgRunClientScript *m = msg;
        memset(g_cs2_args[side], 0, sizeof(g_cs2_args[side]));
        for (int i = 0; i < MSG_RUNCLIENTSCRIPT_MAX_ARGS; i++) {
            g_cs2_args[side][i].ints = g_cs2_ints[side][i];
            g_cs2_args[side][i].strs = g_cs2_strs[side][i];
        }
        m->args = g_cs2_args[side];
    }
}
