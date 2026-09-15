#ifndef RSCACHE_LOG_H
#define RSCACHE_LOG_H

/*
 * rscache's diagnostics, compiled in or out.
 *
 * ## Why this exists
 *
 * The library narrated straight to stdout with bare printf. Two things were
 * wrong with that, and the second one only became visible once a cache started
 * hydrating rather than arriving complete.
 *
 * STDOUT IS DATA. Several tools in this tree read a cache and write bytes to
 * their stdout; a diagnostic landing in the middle of that corrupts the thing
 * being produced, and does it silently. Diagnostics belong on stderr.
 *
 * NOT EVERY CALLER WANTS THEM. A client that hydrates its cache from a server
 * misses every archive until it has fetched it once -- that is what a sparse
 * cache IS -- so on a cold boot the library had a message for hundreds of
 * ordinary events. The individual misses are fixed at their call sites (a miss
 * returns an answer, not a fault), but the general point stands: an embedder
 * should be able to decide whether the library talks at all.
 *
 * ## The switch
 *
 * ON by default, including under NDEBUG, because this commit adds a capability
 * and should not also change what any existing build prints. Several of these
 * messages are real faults -- a corrupt sector chain, a decompress that failed
 * -- and deciding they should vanish from release builds is a separate
 * decision from making them switchable, taken with evidence rather than as a
 * side effect.
 *
 *     -DRSCACHE_LOG_ENABLED=0    strip them entirely
 *     -DRSCACHE_LOG_ENABLED=1    keep them (the default)
 *
 * Measured with logging off: dat2disk.o loses 1,712 bytes, every message's
 * string literal, and its reference to fprintf.
 *
 * When disabled the arguments are still parsed by the compiler -- so a format
 * string that does not match its arguments is still an error, and a build with
 * logging off cannot drift away from one with it on -- and then discarded, so
 * nothing is evaluated at run time.
 */

#include <stdio.h>

#ifndef RSCACHE_LOG_ENABLED
#  define RSCACHE_LOG_ENABLED 1
#endif

#if RSCACHE_LOG_ENABLED
#  define RSCACHE_LOG(...) fprintf(stderr, __VA_ARGS__)
#else
/* sizeof on a comma expression: the compiler still type-checks the call, and
 * nothing is emitted. `(void)0` alone would let a stale format string rot. */
#  define RSCACHE_LOG(...) ((void)sizeof(printf(__VA_ARGS__)))
#endif

#endif
