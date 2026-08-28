#ifndef SRC_GAME_RS_PRELOAD_H
#define SRC_GAME_RS_PRELOAD_H

#include "revconfig/revconfig.h"

/*
 * What a revision fetches before it can show a title screen.
 *
 * The list is the revision's, and the two eras do not merely name the same
 * steps differently -- they have different shapes. Client-TS pulls nine jag
 * archives over HTTP one at a time, naming each as it goes and unpacking them
 * afterwards, so its bar is a position in a queue. OldSchool 239 opens eight
 * cache indices at once and watches them complete, so its bar is a weighted
 * sum and the whole span carries a single sentence. Neither list is derivable
 * from the other.
 *
 * The client's half is knowing HOW: how to pull a jagfile, how to open an
 * index, how to unpack an archive into tables. Which ones, in what order, at
 * what percentage and under what caption is the profile's half, and that is
 * all this table holds.
 *
 * Built once per session from the same sources RevConfigRefs reads, and kept
 * alive beside it -- the builder's item buffer is torn down after the bake,
 * and the boot walks this while the bake is still happening.
 */

#define RS_PRELOAD_NAME_LEN 64
#define RS_PRELOAD_KIND_LEN 24

/** Which machinery a step drives. Anything else the profile writes is carried
 *  through verbatim and reported, rather than silently treated as one of
 *  these -- a typo that loads the wrong archive is worse than one that loads
 *  nothing. */
enum RS_PreloadKind
{
    RS_PRELOAD_KIND_UNKNOWN = 0,
    /** The nine-archive checksum fetch that opens the 2004 boot. */
    RS_PRELOAD_KIND_CRC,
    /** One dat1 jag archive, by stem. */
    RS_PRELOAD_KIND_JAGFILE,
    /** One dat2 cache index, by number. */
    RS_PRELOAD_KIND_INDEX,
    /** The 2004 on-demand prefetch, by file type. */
    RS_PRELOAD_KIND_ONDEMAND,
    /** Turning an already-fetched archive into tables. */
    RS_PRELOAD_KIND_UNPACK,
    /** Engine work with nothing to fetch: sound init, the final pass. */
    RS_PRELOAD_KIND_PREPARE
};

struct RS_PreloadStep
{
    char name[RS_PRELOAD_NAME_LEN];
    char kind_name[RS_PRELOAD_KIND_LEN];
    enum RS_PreloadKind kind;
    /** Jagfile stem or index name; empty when the step fetches nothing. */
    char archive[RS_PRELOAD_NAME_LEN];
    /** Cache index or on-demand file type, or -1 when unstated. */
    int id;
    /** Bar position while this step runs, or -1 to leave the bar alone. */
    int percent;
    /** A [string:] name, drawn under the bar. Empty means say nothing. */
    char say[RS_PRELOAD_NAME_LEN];
    /** Share of the bar this step owns where the era computes a weighted sum
     *  rather than stepping through positions. 0 when it does not. */
    int weight;
    /** Publish a frame before running this step. Opt-in: see
     *  TASK_YIELD_TO_RENDER for why it is not the default. */
    int render;
    int order;
};

struct RS_PreloadTable
{
    struct RS_PreloadStep* steps;
    int count;
    int capacity;
};

void
RS_Preload_Init(struct RS_PreloadTable* table);

void
RS_Preload_Free(struct RS_PreloadTable* table);

/** Read every `[preload:]` in these sources, then sort by `order`. */
void
RS_Preload_LoadSources(
    struct RS_PreloadTable* table,
    char const* ui_ini,
    char const* cache_ini,
    char const* inline_ini);

void
RS_Preload_AddFromItems(
    struct RS_PreloadTable* table,
    struct RevConfigItemBuffer const* items);

/** Step `index`, or NULL past the end. */
struct RS_PreloadStep const*
RS_Preload_At(
    struct RS_PreloadTable const* table,
    int index);

/**
 * Sum of every stated weight, or 0 when the profile states none.
 *
 * Zero is the answer for a lane whose bar steps through positions instead of
 * summing shares, and the caller uses `percent` directly there. It is not a
 * failure, and it is why this returns the total rather than asserting one.
 */
int
RS_Preload_TotalWeight(struct RS_PreloadTable const* table);

#endif /* SRC_GAME_RS_PRELOAD_H */
