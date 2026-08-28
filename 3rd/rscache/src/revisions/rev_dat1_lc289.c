#include "revisions.h"

/*
 * LostCity revision 289 — the world manifests/manifest_rs289lc.ini boots,
 * streamed off a LostCity_Server branch-289 checkout (or read from a local
 * copy of its pack by manifest_rs289lc_local.ini).
 *
 * Format-identical to 254 for every datatype this library decodes: the
 * 2004-2005 dat1 config formats did not move between the two builds, and no
 * rs2 codec threshold falls in (254, 289]. What 289 renamed — the four title
 * fonts, p11 -> p11_full — is addressed by name through a revconfig, not by
 * codec. Declared separately so `revision=289` resolves to a real profile
 * instead of silently matching nothing in the registry.
 */

struct RSCache
RSCache_ProfileDat1Lc289(void)
{
    struct RSCache cache = RSCache_ProfileZero();

    cache.game = RSCACHE_GAME_RS2;
    cache.epoch = RSCACHE_EPOCH_DAT1;
    cache.revision = 289;
    cache.quirks = RSCACHE_QUIRK_NONE;

    return cache;
}
