#ifndef GAMECACHE_IDK_H
#define GAMECACHE_IDK_H

/** Standalone GameCache idk-config type -- only fields consumed by entity build. */
struct GameCacheIdk
{
    int* models;
    int models_count;
    int recol_s[10];
    int recol_d[10];
    int heads[10];
};

#endif
