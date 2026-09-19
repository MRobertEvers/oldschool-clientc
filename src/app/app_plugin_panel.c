/*
 * The plugin panel host. The body is plugin/torirs_plugin_panel.u.c, which the
 * plugin-engine owner edits, and plugin/torirs_plugin_popout_nav.u.c, the rail's
 * destinations offered in a lane's own pop-out column.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "app/app_internal.h"

#include "plugin/torirs_plugin_panel.u.c"
#include "plugin/torirs_plugin_popout_nav.u.c"
