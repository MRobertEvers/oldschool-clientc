#include "tool_profile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
tool_resolve_profile(
    const char* rev_name,
    const char* game_name,
    const char* epoch_name,
    const char* revision_text,
    const char* quirks_list,
    struct RSCache* out)
{
    if( rev_name )
    {
        if( game_name || epoch_name || revision_text )
        {
            fprintf(stderr, "Use either --rev or --game/--epoch/--revision, not both\n");
            return 0;
        }
        if( !RSCache_ProfileByName(rev_name, out) )
        {
            fprintf(stderr, "Unknown revision profile: %s\n", rev_name);
            return 0;
        }
        return 1;
    }

    if( !game_name || !epoch_name || !revision_text )
    {
        fprintf(
            stderr,
            "Cache identity required: pass --rev NAME or --game/--epoch/--revision\n");
        return 0;
    }

    int game = RSCache_GameFromName(game_name);
    int epoch = RSCache_EpochFromName(epoch_name);
    if( game == RSCACHE_GAME_UNSET )
    {
        fprintf(stderr, "Unknown game: %s (expected oldschool or rs2)\n", game_name);
        return 0;
    }
    if( epoch == RSCACHE_EPOCH_UNSET )
    {
        fprintf(stderr, "Unknown epoch: %s (expected dat1 or dat2)\n", epoch_name);
        return 0;
    }

    int revision = atoi(revision_text);
    uint32_t quirks = RSCACHE_QUIRK_NONE;
    if( quirks_list )
    {
        if( !RSCache_QuirksFromList(quirks_list, &quirks) )
        {
            fprintf(stderr, "Unknown quirks: %s\n", quirks_list);
            return 0;
        }
    }

    *out = RSCache_ProfileForIdentity(game, epoch, revision, quirks);
    if( !RSCache_ProfileIsIdentified(out) )
    {
        fprintf(stderr, "Resolved profile identity is unset\n");
        return 0;
    }
    return 1;
}

void
tool_print_profile(
    const char* cache_dir,
    const struct RSCache* profile)
{
    char quirks_buf[32];
    RSCache_QuirksName(profile->quirks, quirks_buf, sizeof(quirks_buf));
    printf(
        "# cache=%s game=%s epoch=%s revision=%d quirks=%s\n",
        cache_dir,
        RSCache_GameName(profile->game),
        RSCache_EpochName(profile->epoch),
        profile->revision,
        quirks_buf);
}
