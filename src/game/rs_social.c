#include "rs_social.h"

#include <assert.h>
#include <string.h>
#include <strings.h>

void
RS_Social_Init(struct RS_Social* social)
{
    assert(social);
    memset(social, 0, sizeof(*social));
    social->server_status = RS_SOCIAL_SERVER_CONNECTED;
    social->node_id = 1;
}

void
RS_Social_SeedDefaults(struct RS_Social* social)
{
    assert(social);
    RS_Social_AddFriend(social, "Durial321", 1);
    RS_Social_AddFriend(social, "Zezima", 2);
    RS_Social_AddFriend(social, "Cow31337", 0);
    RS_Social_AddIgnore(social, "Bluerose13x");
}

static int
find_name(
    char const names[][RS_SOCIAL_NAME_LEN],
    int count,
    char const* name)
{
    for( int i = 0; i < count; i++ )
    {
        if( strcasecmp(names[i], name) == 0 )
            return i;
    }
    return -1;
}

int
RS_Social_AddFriend(struct RS_Social* social, char const* name, int world)
{
    assert(social);
    if( !name || !name[0] || social->friend_count >= RS_SOCIAL_FRIEND_MAX )
        return 0;
    if( find_name(social->friend_name, social->friend_count, name) >= 0 )
        return 0;
    strncpy(
        social->friend_name[social->friend_count], name, RS_SOCIAL_NAME_LEN - 1);
    social->friend_world[social->friend_count] = world;
    social->friend_count++;
    return 1;
}

int
RS_Social_DelFriend(struct RS_Social* social, char const* name)
{
    assert(social);
    int idx = find_name(social->friend_name, social->friend_count, name);
    if( idx < 0 )
        return 0;
    for( int i = idx; i + 1 < social->friend_count; i++ )
    {
        memcpy(social->friend_name[i], social->friend_name[i + 1], RS_SOCIAL_NAME_LEN);
        social->friend_world[i] = social->friend_world[i + 1];
    }
    social->friend_count--;
    return 1;
}

int
RS_Social_AddIgnore(struct RS_Social* social, char const* name)
{
    assert(social);
    if( !name || !name[0] || social->ignore_count >= RS_SOCIAL_IGNORE_MAX )
        return 0;
    if( find_name(social->ignore_name, social->ignore_count, name) >= 0 )
        return 0;
    strncpy(
        social->ignore_name[social->ignore_count], name, RS_SOCIAL_NAME_LEN - 1);
    social->ignore_count++;
    return 1;
}

int
RS_Social_DelIgnore(struct RS_Social* social, char const* name)
{
    assert(social);
    int idx = find_name(social->ignore_name, social->ignore_count, name);
    if( idx < 0 )
        return 0;
    for( int i = idx; i + 1 < social->ignore_count; i++ )
        memcpy(social->ignore_name[i], social->ignore_name[i + 1], RS_SOCIAL_NAME_LEN);
    social->ignore_count--;
    return 1;
}
