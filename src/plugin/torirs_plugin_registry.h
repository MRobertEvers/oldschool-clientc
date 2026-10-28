#ifndef TORIRS_PLUGIN_REGISTRY_H
#define TORIRS_PLUGIN_REGISTRY_H

struct ToriRS_PluginHost;

/** Register every statically linked plugin. Called once, from PluginHost_New's
 *  caller, before the config store is loaded and before PluginHost_Start. */
void PluginRegistry_RegisterAll(struct ToriRS_PluginHost* host);

/** Register only the nxt-* builtins: the client's own features, which the
 *  cache's All Settings switches and CS2 and server state drive. They are not
 *  user plugins, so they run when the plugin layer is switched off
 *  (TORIRS_PLUGINS=0, `[ui:boot] plugins=0`) too. */
void PluginRegistry_RegisterBuiltins(struct ToriRS_PluginHost* host);

#endif /* TORIRS_PLUGIN_REGISTRY_H */
