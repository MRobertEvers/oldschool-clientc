#include "plugin/plugin_screenshots.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

struct PluginScreenshot*
PluginScreenshots_Claim(
    struct PluginScreenshots* shots,
    char const* plugin,
    char const* dir,
    char const* name)
{
    assert(shots);
    assert(plugin);
    assert(name);

    for( int i = 0; i < PLUGIN_SCREENSHOTS_MAX; i++ )
    {
        struct PluginScreenshot* shot = &shots->slots[i];
        size_t len;

        if( shot->in_use )
            continue;

        snprintf(shot->plugin, sizeof(shot->plugin), "%s", plugin);
        snprintf(shot->dir, sizeof(shot->dir), "%s", dir ? dir : "");
        snprintf(shot->name, sizeof(shot->name), "%s", name);
        /* The format is not the plugin's choice -- the writer writes PNG -- so
         * a name that does not say so is completed rather than trusted. A
         * name that already carries an extension is left alone, because a
         * plugin that wrote "kill-42.png" meant that file and not
         * "kill-42.png.png". */
        len = strlen(shot->name);
        if( !strchr(shot->name, '.') && len + 4 < sizeof(shot->name) )
            snprintf(shot->name + len, sizeof(shot->name) - len, ".png");
        return shot;
    }
    return NULL;
}

void
PluginScreenshots_Wait(struct PluginScreenshot* shot)
{
    assert(shot);
    assert(!shot->in_use);
    assert(shot->path[0]);
    shot->in_use = 1;
}

int
PluginScreenshots_Waiting(struct PluginScreenshots const* shots)
{
    int waiting = 0;

    assert(shots);
    for( int i = 0; i < PLUGIN_SCREENSHOTS_MAX; i++ )
        waiting += shots->slots[i].in_use;
    return waiting;
}

bool
PluginScreenshots_IsWaiting(
    struct PluginScreenshots const* shots,
    char const* path)
{
    assert(shots);
    assert(path);
    for( int i = 0; i < PLUGIN_SCREENSHOTS_MAX; i++ )
        if( shots->slots[i].in_use && strcmp(shots->slots[i].path, path) == 0 )
            return true;
    return false;
}

char const*
PluginScreenshots_Take(struct PluginScreenshots* shots)
{
    assert(shots);
    for( int i = 0; i < PLUGIN_SCREENSHOTS_MAX; i++ )
    {
        struct PluginScreenshot* shot = &shots->slots[i];

        if( !shot->in_use )
            continue;
        shot->in_use = 0;
        /* The destination was resolved -- and refused, when there was none --
         * back when the request was made, so a waiting capture always has a
         * path to go to. */
        assert(shot->path[0]);
        return shot->path;
    }
    return NULL;
}
