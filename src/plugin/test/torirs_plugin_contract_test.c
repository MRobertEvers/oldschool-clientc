#include "plugin/torirs_plugin_contract.h"
#include <stdio.h>

#define CHECK(c) do { if(!(c)) { fprintf(stderr,"contract FAIL %s:%d: %s\n",__FILE__,__LINE__,#c); return 1; } } while(0)

int main(void)
{
    struct ToriRS_WidgetRef a = {{1,2,3}}, reused = {{1,2,4}}, other_tree = {{2,2,3}};
    CHECK(TORIRS_PLUGIN_CONTRACT_MAJOR == 3);
    CHECK(ToriRS_WidgetRefValid(a));
    CHECK(!ToriRS_WidgetRefValid((struct ToriRS_WidgetRef){{1,0,3}}));
    CHECK(!ToriRS_WidgetRefEqual(a,reused));
    CHECK(!ToriRS_WidgetRefEqual(a,other_tree));
    CHECK(ToriRS_PluginContextAllows(TORIRS_PLUGIN_EVENT,TORIRS_PLUGIN_WRITE_WIDGET));
    CHECK(ToriRS_PluginContextAllows(TORIRS_PLUGIN_EVENT,TORIRS_PLUGIN_RUN_SCRIPT));
    CHECK(ToriRS_PluginContextAllows(TORIRS_PLUGIN_SCRIPT_CALLBACK,TORIRS_PLUGIN_WRITE_WIDGET));
    CHECK(!ToriRS_PluginContextAllows(TORIRS_PLUGIN_SCRIPT_CALLBACK,TORIRS_PLUGIN_RUN_SCRIPT));
    CHECK(ToriRS_PluginContextAllows(TORIRS_PLUGIN_SCRIPT_CALLBACK,TORIRS_PLUGIN_SCHEDULE));
    CHECK(!ToriRS_PluginContextAllows(TORIRS_PLUGIN_PAINT,TORIRS_PLUGIN_WRITE_WIDGET));
    CHECK(!ToriRS_PluginContextAllows(TORIRS_PLUGIN_PAINT,TORIRS_PLUGIN_RUN_SCRIPT));
    CHECK(ToriRS_PluginContextAllows(TORIRS_PLUGIN_SHUTDOWN,TORIRS_PLUGIN_RELEASE_OWNED));
    CHECK(ToriRS_PluginContextAllows(TORIRS_PLUGIN_SHUTDOWN,TORIRS_PLUGIN_WRITE_WIDGET));
    CHECK(!ToriRS_PluginContextAllows(TORIRS_PLUGIN_SHUTDOWN,TORIRS_PLUGIN_SCHEDULE));
    CHECK(!ToriRS_PluginContextAllows(TORIRS_PLUGIN_SHUTDOWN,TORIRS_PLUGIN_RUN_SCRIPT));
    CHECK(!ToriRS_PluginContextAllows(-1,TORIRS_PLUGIN_READ_WIDGET));
    CHECK(!ToriRS_PluginContextAllows(TORIRS_PLUGIN_EVENT,999));
    puts("plugin contract major 3 widget/context policy: passed");
    return 0;
}
