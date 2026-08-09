#ifndef RSCACHE_REVISIONS_H
#define RSCACHE_REVISIONS_H

#include "rscache_profile.h"

/*
 * One explicit profile per supported revision.
 *
 * Modelled on how rsprot organises protocol revisions: each revision is
 * declared in its own file, by name, rather than inferred. Looking up "what is
 * OSRS revision 230" is a matter of opening rev_dat2_osrs230.c, and a revision
 * can never be broken by an edit made for a different one.
 *
 * Unlike rsprot these modules are *thin*. rsprot copy-forwards a complete
 * ~800-file tree per revision, which is affordable on the JVM; duplicating
 * ~19k lines of C per revision is not. Here a revision module declares identity
 * — game, epoch, revision, quirks — plus the handful of codec versions where
 * the derivation rule in a datatype would get it wrong. The codecs themselves
 * are shared and versioned (`decode_x_v3`).
 *
 * ## Adding a revision
 *
 * 1. Add `rev_<epoch>_<name>.c` here with a single
 *    `RSCache_Profile<Name>(void)` starting from `RSCache_ProfileZero()`, and
 *    set all four identity fields (game, epoch, revision, quirks) plus any
 *    codec pins.
 * 2. Declare it below and add it to the table in revisions.c.
 * 3. Add its `#include` to rscache_unity.c.
 *
 * Do not add per-revision field logic to a datatype's decoder. If a revision
 * needs a field the shared codec does not have, either extend the codec behind a
 * flag (RSCache_<Type>Flags) or add a new codec version — never branch on
 * `cache->revision` inside a codec body, or the knowledge ends up spread across
 * every datatype instead of in one profile.
 */

/* --- dat1 / jagfile era ------------------------------------------------- */

/** LostCity rev 254 (`cache254.lostcity`, manifest_rs254.ini). */
struct RSCache
RSCache_ProfileDat1Lc254(void);

/** RevConfig rev 245.2 (`cache254`). */
struct RSCache
RSCache_ProfileDat1Lc245_2(void);

/* --- dat2 / js5 era ----------------------------------------------------- */

/** The 643 / RS2 branch (`cache.643`). game=rs2 epoch=dat2 revision=643. */
struct RSCache
RSCache_ProfileDat2Rs643(void);

/**
 * The 727 / pre-EoC branch (`cache.rs727_preeoc`). game=rs2 epoch=dat2
 * revision=727. Shares 643's container and geometry codecs; pins its own npc
 * and obj codecs, whose streams changed at build 669/670. Decode only — there
 * is no 727 encoder (EXCEPTIONS.md B23).
 */
struct RSCache
RSCache_ProfileDat2Rs727(void);

/** Kronos-flavoured OSRS 184 (`cache.kronos`). Carries RSCACHE_QUIRK_KRONOS. */
struct RSCache
RSCache_ProfileDat2Osrs184Kronos(void);

/** OSRS 230 (`cache.osrs230`, manifest_osrs230.ini). */
struct RSCache
RSCache_ProfileDat2Osrs230(void);

/** OSRS 231 — npc footprintSize. */
struct RSCache
RSCache_ProfileDat2Osrs231(void);

/** OSRS 232 — model faceZOffsets, sprite alpha. */
struct RSCache
RSCache_ProfileDat2Osrs232(void);

/** OSRS 233 (xrsps, manifest_xrsps.ini). Format-identical to 230 for every
 *  datatype this library decodes; declared so revision=233 resolves. */
struct RSCache
RSCache_ProfileDat2Osrs233(void);

/** OSRS 234 — npc/loc/seq flag opcodes. */
struct RSCache
RSCache_ProfileDat2Osrs234(void);

/** OSRS 235 — npc overlap fields; 4-byte container revisions. */
struct RSCache
RSCache_ProfileDat2Osrs235(void);

/** OSRS 236 — npc idleAnimRestart / zbuf. */
struct RSCache
RSCache_ProfileDat2Osrs236(void);

/** OSRS 237 — EntityOps, int model ids, long params, CS2 longs. */
struct RSCache
RSCache_ProfileDat2Osrs237(void);

/** OSRS 238 — obj untradeable; worldmap groupId/fileId removed. */
struct RSCache
RSCache_ProfileDat2Osrs238(void);

/** OSRS 239 (`cache.osrs239`, manifest_osrs239.ini). */
struct RSCache
RSCache_ProfileDat2Osrs239(void);

/* --- lookup ------------------------------------------------------------- */

/**
 * Profile by name, matching the manifest/CLI vocabulary: "lc254", "lc245_2",
 * "osrs230", "xrsps233", "osrs239", "osrs184", "kronos", "643", "727".
 * Returns false and leaves *out untouched when the name is unknown.
 */
bool
RSCache_ProfileByName(
    const char* name,
    struct RSCache* out);

/**
 * Profile from a fully stated identity.
 *
 * The four fields are authoritative and returned verbatim. The revision registry
 * is consulted only for `codec[]` pins on an exact (game, epoch, revision)
 * match — there is no nearest-lower borrow and no guessing. Quirks from the
 * caller override the registry row when both are present.
 */
struct RSCache
RSCache_ProfileForIdentity(
    int game,
    int epoch,
    int revision,
    uint32_t quirks);

#endif
