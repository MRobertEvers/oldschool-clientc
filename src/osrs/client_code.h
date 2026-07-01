#ifndef CLIENT_CODE_H
#define CLIENT_CODE_H

#include <stdbool.h>

/* Mirrors Client-TS ClientCode.ts ranges used by addSocialOptions. */

#define CLIENT_CODE_FRIENDS_START 1
#define CLIENT_CODE_FRIENDS_END 100
#define CLIENT_CODE_FRIENDS_UPDATE_START 101
#define CLIENT_CODE_FRIENDS_UPDATE_END 200
#define CLIENT_CODE_FRIENDS2_START 701
#define CLIENT_CODE_FRIENDS2_END 800
#define CLIENT_CODE_FRIENDS2_UPDATE_START 801
#define CLIENT_CODE_FRIENDS2_UPDATE_END 900

#define CLIENT_CODE_IGNORES_START 401
#define CLIENT_CODE_IGNORES_END 500

static inline int
client_code_friend_slot_index(int client_code)
{
    if( client_code >= CLIENT_CODE_FRIENDS2_UPDATE_START )
        return client_code - CLIENT_CODE_FRIENDS2_START;
    if( client_code >= CLIENT_CODE_FRIENDS2_START )
        return client_code - 601;
    if( client_code >= CLIENT_CODE_FRIENDS_UPDATE_START )
        return client_code - CLIENT_CODE_FRIENDS_UPDATE_START;
    if( client_code >= CLIENT_CODE_FRIENDS_START && client_code <= CLIENT_CODE_FRIENDS_END )
        return client_code - 1;
    return -1;
}

static inline int
client_code_ignore_slot_index(int client_code)
{
    if( client_code >= CLIENT_CODE_IGNORES_START && client_code <= CLIENT_CODE_IGNORES_END )
        return client_code - CLIENT_CODE_IGNORES_START;
    return -1;
}

static inline bool
client_code_is_friend_list_entry(int client_code)
{
    return (client_code >= CLIENT_CODE_FRIENDS_START &&
            client_code <= CLIENT_CODE_FRIENDS_UPDATE_END) ||
           (client_code >= CLIENT_CODE_FRIENDS2_START && client_code <= 900);
}

static inline bool
client_code_is_ignore_list_entry(int client_code)
{
    return client_code >= CLIENT_CODE_IGNORES_START && client_code <= CLIENT_CODE_IGNORES_END;
}

#endif
