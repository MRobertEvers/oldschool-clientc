/*
 * The plugin host's view of the engine. The body is
 * plugin/torirs_plugin_bridge.u.c, which the plugin-engine owner edits.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "app/app_internal.h"

/* The bridge counts its own hot paths and mirrors one Porcelain constant, and
 * it is a translation unit of its own now rather than a fragment of app.c, so
 * it needs both headers itself. */
#include "perf_audit.h"
#include "plugin/porcelain/torirs_porcelain.h"

#include "plugin/torirs_plugin_bridge.u.c"
