/*
 * The native half of the page's black box. @see platform_web_telemetry.h.
 */
#include "platform_web_telemetry.h"

#include <assert.h>
#include <emscripten.h>
#include <stdarg.h>
#include <stdio.h>

/*
 * A page without the black box (an older cached index.html against this
 * wasm, or ?telemetry=0) is not an error: the event goes nowhere.
 */
// clang-format off
EM_JS(void, web_telemetry_event, (char const* kind, char const* detail), {
    if( typeof window === 'undefined' || typeof window.__torirs_tel !== 'function' )
        return;
    try
    {
        window.__torirs_tel(UTF8ToString(kind), UTF8ToString(detail));
    }
    catch( e )
    {
    }
});
// clang-format on

void
ToriPlatformWeb_Telemetry(
    char const* kind,
    char const* fmt,
    ...)
{
    char detail[512];
    va_list ap;

    assert(kind);
    assert(fmt);
    va_start(ap, fmt);
    vsnprintf(detail, sizeof(detail), fmt, ap);
    va_end(ap);
    web_telemetry_event(kind, detail);
}
