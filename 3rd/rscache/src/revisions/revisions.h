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
 * — game, container, epoch, revision, quirks — plus the handful of codec
 * versions where the derivation rule in a datatype would get it wrong. The
 * codecs themselves are shared and versioned (`decode_x_v3`).
 *
 * ## Adding a revision
 *
 * 1. Add `rev_<container>_<name>.c` here with a single
 *    `RSCache_Profile<Name>(void)` starting from `RSCache_ProfileZero()`.
 * 2. Declare it below and add it to the table in revisions.c.
 * 3. Add its `#include` to rscache_unity.c.
 *
 * Do not add per-revision field logic to a datatype's decoder. If a revision
 * needs a field the shared codec does not have, either extend the codec behind a
 * flag (RSCache_<Type>Flags) or add a new codec version — never branch on
 * `cache->version` inside a codec body, or the knowledge ends up spread across
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

/** The 643 / RS2 branch (`cache.643`). Its reference-table revisions overlap
 *  OSRS's small-integer range, so the epoch has to be stated, not detected. */
struct RSCache
RSCache_ProfileDat2Rs643(void);

/** Kronos-flavoured OSRS 184 (`cache.kronos`). Carries RSCACHE_QUIRK_KRONOS. */
struct RSCache
RSCache_ProfileDat2Osrs184Kronos(void);

/** OSRS 230 (`cache.osrs230`, manifest_osrs230.ini). */
struct RSCache
RSCache_ProfileDat2Osrs230(void);

/** OSRS 233 (xrsps, manifest_xrsps.ini). */
struct RSCache
RSCache_ProfileDat2Osrs233(void);

/** OSRS 239 (`cache.osrs239`). */
struct RSCache
RSCache_ProfileDat2Osrs239(void);

/* --- lookup ------------------------------------------------------------- */

/**
 * Profile by name, matching the manifest/CLI vocabulary: "lc254", "lc245_2",
 * "osrs230", "xrsps233", "osrs239", "osrs184", "kronos", "643".
 * Returns false and leaves *out untouched when the name is unknown.
 */
bool
RSCache_ProfileByName(
    const char* name,
    struct RSCache* out);

/**
 * Profile for a container kind and game revision, for callers that have those
 * two facts and no revision name — which is the common case at boot, since the
 * manifest supplies `kind=` and `client_version=`.
 *
 * Falls back to the nearest declared profile of the same container when the
 * revision is not one we declare, so an undeclared revision still gets the right
 * container, epoch and string terminator rather than nothing.
 *
 * The 643 / RS2 profile cannot be selected by bare revision number: its
 * `version` is left unknown so it never matches an OSRS threshold (OSRS and RS2
 * are independent lineages). Use RSCache_ProfileByName("643") or a manifest
 * `rev=` instead.
 */
struct RSCache
RSCache_ProfileForContainerRevision(
    int container,
    int game_revision);

#endif
