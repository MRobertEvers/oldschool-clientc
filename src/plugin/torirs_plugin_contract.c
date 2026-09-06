#include "plugin/torirs_plugin_contract.h"

bool ToriRS_WidgetRefValid(struct ToriRS_WidgetRef ref)
{
    return ref.opaque[0] && ref.opaque[1] && ref.opaque[2];
}

bool ToriRS_WidgetRefEqual(struct ToriRS_WidgetRef a, struct ToriRS_WidgetRef b)
{
    return a.opaque[0] == b.opaque[0] && a.opaque[1] == b.opaque[1] && a.opaque[2] == b.opaque[2];
}

bool ToriRS_PluginContextAllows(enum ToriRS_PluginExecutionContext context,
                               enum ToriRS_PluginOperation operation)
{
    if( context < TORIRS_PLUGIN_STARTUP || context > TORIRS_PLUGIN_SHUTDOWN ) return false;
    switch( operation )
    {
    case TORIRS_PLUGIN_READ_WIDGET: return true;
    case TORIRS_PLUGIN_WRITE_WIDGET:
        return context != TORIRS_PLUGIN_PAINT;
    case TORIRS_PLUGIN_RUN_SCRIPT:
        return context == TORIRS_PLUGIN_STARTUP || context == TORIRS_PLUGIN_EVENT;
    case TORIRS_PLUGIN_SCHEDULE:
        return context != TORIRS_PLUGIN_SHUTDOWN;
    case TORIRS_PLUGIN_RELEASE_OWNED:
        return context != TORIRS_PLUGIN_PAINT;
    default: return false;
    }
}
