#ifndef TORIRS_SERVER_PACKCHECK_H
#define TORIRS_SERVER_PACKCHECK_H

/*
 * Is a pack still what its tree says? Answered per unit, from the manifest the
 * build wrote beside it (docs/serverpack.md, "Staleness").
 *
 * The check this replaces was all-or-nothing: one source anywhere newer than the
 * pack (the script pack), or a hash of the whole content tree (the server pack),
 * refused the boot. Several sessions edit one tree, so a run was refused for
 * another session's unrelated edit between its build and its boot. Now every
 * input the build recorded is stat'd — and hashed only when its size or mtime
 * moved — the source roots are walked for files the build never saw, and each
 * stale unit is NAMED. The pack still runs unless TORIRSSERVER_STALE=refuse.
 *
 * A pack with no manifest (built before the incremental builds) is checked the
 * old way by the caller.
 */

#include <stddef.h>

enum
{
    TORIRSSERVER_PACKCHECK_NO_MANIFEST = -1,
};

/**
 * Check the manifest at `<pack_dir>/pack.manifest` against `content_dir`.
 *
 * Returns TORIRSSERVER_PACKCHECK_NO_MANIFEST when there is none, else the number
 * of stale entries, each already printed in one banner headed by `label`
 * ("script pack", "server pack"). Never refuses by itself; see
 * ToriRSServer_PackStaleRefuses.
 */
int
ToriRSServer_PackCheck(
    const char* label,
    const char* pack_dir,
    const char* content_dir);

/** TORIRSSERVER_STALE=refuse: a stale unit stops the boot. */
int
ToriRSServer_PackStaleRefuses(void);

/**
 * Hold `<pack_dir>/.pack.lock` shared while a pack is read, so a build (which
 * holds it exclusive while it swaps files in) is never seen half-done. Returns a
 * handle for ToriRSServer_PackUnlock; a pack directory with no lock file (an old
 * pack, a read-only copy) is read unlocked.
 */
long
ToriRSServer_PackLockShared(const char* pack_dir);

void
ToriRSServer_PackUnlock(long handle);

#endif
