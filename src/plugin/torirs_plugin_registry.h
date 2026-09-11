#ifndef TORIRS_PLUGIN_REGISTRY_H
#define TORIRS_PLUGIN_REGISTRY_H

struct ToriRS_PluginHost;

/** Register every statically linked plugin. Called once, from PluginHost_New's
 *  caller, before the config store is loaded and before PluginHost_Start. */
void PluginRegistry_RegisterAll(struct ToriRS_PluginHost* host);

#endif /* TORIRS_PLUGIN_REGISTRY_H */
