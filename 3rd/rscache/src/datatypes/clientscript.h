#ifndef RSCACHE_DATATYPES_CLIENTSCRIPT_H
#define RSCACHE_DATATYPES_CLIENTSCRIPT_H

#include "../dat2disk.h"
#include "../rscache_profile.h"
#include "cs2_script.h"

#include <stdint.h>

#define RSCACHE_CLIENTSCRIPT_DECODE_TRAILER_MODERN 0
#define RSCACHE_CLIENTSCRIPT_DECODE_TRAILER_LEGACY 1

/**
 * Trailer flag for this cache.
 *
 * Returns LEGACY for every declared profile, which is what the client already
 * hardcoded at its one call site (with the MODERN branch commented out above
 * it). This function exists to give that decision a single home rather than to
 * change it: no revision threshold for the trailer width has been verified
 * against a cache yet, and guessing one would silently break script decode for
 * whichever side of the guess is wrong.
 *
 * Note the decoder is a *try* — cs2_script_try_decode_footer validates the
 * footer and returns NULL on a mismatch — so when the threshold is established
 * it can be added here and checked against every cache in the corpus.
 */
int
RSCache_ClientScriptFlags(const struct RSCache* cache);

struct RSCache_ClientScript
{
    struct RSCache_CS2_Script script;
};

struct RSCache_ClientScript*
RSCache_ClientScriptNewFromDecodeFlags(
    int script_id,
    const uint8_t* data,
    int data_size,
    int flags);

struct RSCache_ClientScript*
RSCache_ClientScriptNewFromArchive(
    struct RSCache_Dat2DiskArchive* archive,
    int script_id,
    int flags);

void
RSCache_ClientScriptFree(struct RSCache_ClientScript* script);

#endif
