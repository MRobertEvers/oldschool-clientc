#ifndef SRC_GAME_RS_SOCIAL_H
#define SRC_GAME_RS_SOCIAL_H

/*
 * Friends / ignores store (reference friendUsername/friendNodeId/
 * ignoreUserhash + friendServerStatus). Filled by network packets once state
 * sync exists; seeded with demo entries so the friends/ignore tabs render.
 */

#define RS_SOCIAL_FRIEND_MAX 200
#define RS_SOCIAL_IGNORE_MAX 100
#define RS_SOCIAL_NAME_LEN 64

enum RS_SocialServerStatus
{
    RS_SOCIAL_SERVER_LOADING = 0,
    RS_SOCIAL_SERVER_CONNECTING = 1,
    RS_SOCIAL_SERVER_CONNECTED = 2,
};

struct RS_Social
{
    char friend_name[RS_SOCIAL_FRIEND_MAX][RS_SOCIAL_NAME_LEN];
    /** World the friend is on; 0 = offline (reference friendNodeId). */
    int friend_world[RS_SOCIAL_FRIEND_MAX];
    int friend_count;

    char ignore_name[RS_SOCIAL_IGNORE_MAX][RS_SOCIAL_NAME_LEN];
    int ignore_count;

    int server_status; /* enum RS_SocialServerStatus */
    /** Our world id, for the green same-world highlight. */
    int node_id;
};

void
RS_Social_Init(struct RS_Social* social);

/** Demo content until the friend-server packets fill the store. */
void
RS_Social_SeedDefaults(struct RS_Social* social);

int
RS_Social_AddFriend(struct RS_Social* social, char const* name, int world);

int
RS_Social_DelFriend(struct RS_Social* social, char const* name);

int
RS_Social_AddIgnore(struct RS_Social* social, char const* name);

int
RS_Social_DelIgnore(struct RS_Social* social, char const* name);

#endif
