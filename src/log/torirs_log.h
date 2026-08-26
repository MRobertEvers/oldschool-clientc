#ifndef SRC_LOG_TORIRS_LOG_H
#define SRC_LOG_TORIRS_LOG_H

/*
 * Two output channels, deliberately not one.
 *
 * `TORIRS_ERR` is for something that went wrong and that a user or a bug report
 * needs to carry. It survives every build.
 *
 * `TORIRS_LOG` is developer narration -- what loaded, what was skipped, what a
 * subsystem decided. It is compiled out of optimized builds, argument
 * evaluation included, so a log on a hot path costs nothing there.
 *
 * The reason this exists rather than a house style of bare fprintf: stderr is
 * unbuffered, so every write is a syscall. Measured on the Windows XP target,
 * an in-world client was writing 178 KB of stderr per 30 s -- about one write
 * per frame, all of it a plugin restating that an asset it had already failed
 * to find was still missing. This tree has paid for that mistake before, when a
 * per-spawn fprintf cost 6 ms a frame. An unconditional log on a per-frame path
 * is a performance bug, and the point of the split is that the compiler removes
 * that whole class of it from the shipped build.
 *
 * Neither macro is a substitute for the other. Demoting a real error to
 * TORIRS_LOG makes it vanish in exactly the build where it matters; promoting
 * narration to TORIRS_ERR puts the syscall back.
 */

#include <stdarg.h>
#include <stdio.h>

/*
 * `OPT=1` compiles -DNDEBUG (src/makefile), so NDEBUG is this tree's marker for
 * an optimized build. Define TORIRS_LOG_ENABLED yourself to override -- 1 to
 * keep narration in a release build you are debugging, 0 to strip it from a
 * debug build that is drowning in it.
 */
#ifndef TORIRS_LOG_ENABLED
#  if defined(NDEBUG)
#    define TORIRS_LOG_ENABLED 0
#  else
#    define TORIRS_LOG_ENABLED 1
#  endif
#endif

/** Errors. Always compiled in. */
#define TORIRS_ERR(...) fprintf(stderr, __VA_ARGS__)

/** Errors from a variadic wrapper that already holds a va_list. */
#define TORIRS_VERR(fmt, ap) vfprintf(stderr, (fmt), (ap))

/*
 * Output the user explicitly asked for: a profiler report, a census, a dump
 * behind its own environment variable. Always compiled in, because it is
 * already gated -- at runtime, by the flag that turned it on -- and a report
 * that disappears from the optimized build is missing from the only build whose
 * numbers anyone wants. TORIRS_PERF=1 is the case that named this: its whole
 * report was narration by shape and would have vanished under OPT=1.
 *
 * The test for this channel is not "is it interesting" but "did someone turn it
 * on". If the line prints unconditionally, it is TORIRS_LOG.
 */
#define TORIRS_REPORT(...) fprintf(stderr, __VA_ARGS__)

#if TORIRS_LOG_ENABLED
/** Developer narration. Compiled out when TORIRS_LOG_ENABLED is 0. */
#  define TORIRS_LOG(...) fprintf(stderr, __VA_ARGS__)
/** Narration from a variadic wrapper that already holds a va_list. */
#  define TORIRS_VLOG(fmt, ap) vfprintf(stderr, (fmt), (ap))
#  define TORIRS_LOGC(c) fputc((c), stderr)
#else
#  define TORIRS_VLOG(fmt, ap)                                                                     \
      do                                                                                           \
      {                                                                                            \
          (void)(fmt);                                                                             \
          (void)(ap);                                                                              \
      } while( 0 )
#  define TORIRS_LOGC(c) ((void)(c))
/*
 * The arguments must still be *parsed* (so the call cannot rot unnoticed) but
 * must not be *evaluated* -- a log line that calls a function would otherwise
 * keep running in the build that strips the output. `sizeof` under `if(0)` gets
 * both: the expression is type-checked and never emitted.
 */
#  define TORIRS_LOG(...)                                                                          \
      do                                                                                           \
      {                                                                                            \
          if( 0 )                                                                                  \
              (void)sizeof(printf(__VA_ARGS__));                                                    \
      } while( 0 )
#endif

#endif
