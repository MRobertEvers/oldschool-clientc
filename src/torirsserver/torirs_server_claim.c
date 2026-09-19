/*
 * Which slot a login's character goes into. See torirs_server_claim.h.
 */

#include "torirs_server_claim.h"
#include <assert.h>

#include "torirs_server.h"
#include "torirs_server_save.h"
#include "torirs_server_session.h"

#include <stdio.h>
#include <string.h>

void
ToriRSServer_ClaimNoteDeparted(
    struct ToriRSServerClaims* claims,
    const struct ToriRSServerPlayer* player,
    long now_ms)
{
    struct ToriRSServerClaimDeparted* record;

    assert(claims);
    assert(player);
    /* A selftest player has no session and so no seed to reclaim with. */
    if( !player->session )
        return;

    record = &claims->departed[claims->next];
    claims->next = (claims->next + 1) % TORIRSSERVER_CLAIM_DEPARTED_MAX;
    snprintf(record->save_path, sizeof(record->save_path), "%s",
             ToriRSServer_SavePath(player->display_name));
    memcpy(record->seed, player->session->seed, sizeof(record->seed));
    record->pid = player->pid;
    record->left_ms = now_ms;
}

int
ToriRSServer_ClaimSlot(
    struct ToriRSServerClaims* claims,
    const struct ToriRSServer* srv,
    const struct ToriRSServerSession* session,
    long now_ms,
    struct ToriRSServerPlayer** out_evict)
{
    char save_path[1024];

    assert(claims);
    assert(srv);
    assert(session);
    assert(out_evict);
    *out_evict = NULL;

    snprintf(save_path, sizeof(save_path), "%s", ToriRSServer_SavePath(session->display_name));
    /* A name that sanitises to nothing has no save and so no identity. */
    if( !save_path[0] )
        return session->reconnect ? TORIRSSERVER_CLAIM_REFUSE : TORIRSSERVER_CLAIM_ANY_SLOT;

    /*
     * Online in another session. A fresh login takes it over -- this server
     * checks no password, so refusing it would only strand a player whose old
     * socket has not closed yet. A reconnect must also hold that session's key,
     * and comes back in its slot.
     */
    for( int pid = 0; pid < srv->player_count; pid++ )
    {
        const struct ToriRSServerPlayer* online = &srv->players[pid];

        if( !online->active || !online->session || online->session == session )
            continue;
        if( strcmp(ToriRSServer_SavePath(online->display_name), save_path) != 0 )
            continue;
        if( session->reconnect && memcmp(session->presented_seed, online->session->seed,
                                         sizeof(session->presented_seed)) != 0 )
        {
            fprintf(stderr,
                    "torirsserver: reconnect for %s presents another session's key; refusing\n",
                    online->display_name);
            return TORIRSSERVER_CLAIM_REFUSE;
        }
        fprintf(stderr, "torirsserver: %s %s; dropping the old session (pid %d)\n",
                online->display_name, session->reconnect ? "reconnected" : "logged in again",
                pid);
        *out_evict = (struct ToriRSServerPlayer*)online;
        return session->reconnect ? pid : TORIRSSERVER_CLAIM_ANY_SLOT;
    }

    if( !session->reconnect )
        return TORIRSSERVER_CLAIM_ANY_SLOT;

    /* Gone already: reclaimable while the record is fresh and the slot free. */
    for( int i = 0; i < TORIRSSERVER_CLAIM_DEPARTED_MAX; i++ )
    {
        struct ToriRSServerClaimDeparted* record = &claims->departed[i];

        if( !record->save_path[0] || strcmp(record->save_path, save_path) != 0 )
            continue;
        if( memcmp(record->seed, session->presented_seed, sizeof(record->seed)) != 0 )
            continue;
        record->save_path[0] = '\0'; /* spent, whatever the answer below */
        if( now_ms - record->left_ms > TORIRSSERVER_CLAIM_WINDOW_MS )
            break;
        if( srv->players[record->pid].active )
            break; /* somebody else has the slot now */
        return record->pid;
    }
    fprintf(stderr, "torirsserver: reconnect for %s has no session to reclaim; refusing\n",
            session->display_name);
    return TORIRSSERVER_CLAIM_REFUSE;
}
