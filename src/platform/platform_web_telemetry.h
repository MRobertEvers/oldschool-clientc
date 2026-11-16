#ifndef PLATFORM_WEB_TELEMETRY_H
#define PLATFORM_WEB_TELEMETRY_H

/*
 * One event into the page's black box (src/web/torirs_telemetry.js).
 *
 * The page keeps a ring of what happened -- world rebuilds, GPU build sizes,
 * context losses, main-thread stalls, errors -- stores it on the device as it
 * goes, and posts it to io_server's /telemetry. It exists for the failures a
 * tablet cannot show anyone: a Safari tab that froze on a rebuild, went gray,
 * or was killed and reloaded, with no console attached. The breadcrumb this
 * writes is what survives that.
 *
 * `kind` is a short snake_case name. `fmt` formats the detail, which should be
 * a JSON object (`"{\"zone_x\":%d}"`); the page keeps it as a string when it
 * does not parse. Rare events only: this is not a per-frame channel.
 *
 * Every lane may call it; anywhere but the web it is nothing.
 */
#if defined(TORIRS_PLATFORM_WEB)
void
ToriPlatformWeb_Telemetry(
    char const* kind,
    char const* fmt,
    ...) __attribute__((format(printf, 2, 3)));
#else
static inline void
ToriPlatformWeb_Telemetry(
    char const* kind,
    char const* fmt,
    ...)
{
    (void)kind;
    (void)fmt;
}
#endif

#endif
