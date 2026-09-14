/*
 * The plugin panel host. The body is plugin/torirs_plugin_panel.u.c, which the plugin-engine owner edits.
 *
 * One translation unit of the App layer. Everything here may read and write
 * `struct App`; what crosses to another unit of the layer is declared in
 * app/app_internal.h, and nothing outside the layer may include either.
 */

#include "app/app_internal.h"

#include "plugin/torirs_plugin_panel.u.c"
