#include "torirs_chrome_mirror.h"

#include <assert.h>
#include <string.h>

void
ToriRSChromeMirror_Init(struct ToriRSChromeMirror* mirror)
{
    assert(mirror);
    memset(mirror, 0, sizeof(*mirror));
}

static int
mirror_valid_widget(struct ToriRSChromeMirror const* mirror, int handle)
{
    struct ToriRSChromeMirrorWidget const* widget;

    if( handle < 0 || handle >= TORIRS_CHROME_MAX_WIDGETS )
        return 0;
    widget = &mirror->widgets[handle];
    return widget->live && widget->panel >= 0 &&
           widget->panel < TORIRS_CHROME_MAX_PANELS &&
           mirror->panels[widget->panel].live &&
           widget->panel_mount == mirror->panels[widget->panel].mount;
}

static int
mirror_valid_panel(struct ToriRSChromeMirror const* mirror, int handle)
{
    return handle >= 0 && handle < TORIRS_CHROME_MAX_PANELS && mirror->panels[handle].live;
}


struct ToriRSChromeMirrorWidget*
ToriRSChromeMirror_Widget(struct ToriRSChromeMirror* mirror, int handle)
{
    assert(mirror);
    if( !mirror_valid_widget(mirror, handle) )
        return NULL;
    return &mirror->widgets[handle];
}

int
ToriRSChromeMirror_Apply(struct ToriRSChromeMirror* mirror, struct ToriRSChromeCmd const* cmd)
{
    assert(mirror);
    assert(cmd);

    switch( cmd->kind )
    {
    case TORIRS_CHROME_CMD_PANEL_OPEN:
    {
        uint32_t mount;
        if( cmd->panel < 0 || cmd->panel >= TORIRS_CHROME_MAX_PANELS )
            return 0;
        mount = ++mirror->next_mount;
        if( mount == 0 )
            mount = ++mirror->next_mount;
        memset(&mirror->panels[cmd->panel], 0, sizeof(mirror->panels[cmd->panel]));
        mirror->panels[cmd->panel].live = 1;
        mirror->panels[cmd->panel].mount = mount;
        return 1;
    }

    case TORIRS_CHROME_CMD_PANEL_CLOSE:
        if( cmd->panel < 0 || cmd->panel >= TORIRS_CHROME_MAX_PANELS )
            return 0;
        memset(&mirror->panels[cmd->panel], 0, sizeof(mirror->panels[cmd->panel]));
        /* Children retain stale slots, but their saved mount no longer names a
         * live panel. That invalidates the whole subtree in O(1); an ADD on a
         * later mount overwrites the slot before it can become valid again. */
        return 1;

    case TORIRS_CHROME_CMD_WIDGET_ADD:
        if( cmd->widget < 0 || cmd->widget >= TORIRS_CHROME_MAX_WIDGETS )
            return 0;
        memset(&mirror->widgets[cmd->widget], 0, sizeof(mirror->widgets[cmd->widget]));
        mirror->widgets[cmd->widget].live = 1;
        mirror->widgets[cmd->widget].panel = cmd->panel;
        mirror->widgets[cmd->widget].panel_mount =
            mirror_valid_panel(mirror, cmd->panel)
                ? mirror->panels[cmd->panel].mount
                : 0;
        return 1;

    case TORIRS_CHROME_CMD_WIDGET_REMOVE:
        if( cmd->widget < 0 || cmd->widget >= TORIRS_CHROME_MAX_WIDGETS )
            return 0;
        /* Cleared rather than flagged: the handle can be recycled by an ADD in
         * the very next command. */
        memset(&mirror->widgets[cmd->widget], 0, sizeof(mirror->widgets[cmd->widget]));
        return 1;

    default:
        /* Every property command is applied by the DOM reducer. The mirror
         * keeps only lifecycle identity needed to validate returned intents. */
        return 0;
    }
}

/* ---- intents ------------------------------------------------------------- */

void
ToriRSChromeMirror_PushIntent(
    struct ToriRSChromeMirror* mirror, struct ToriRSChromeIntent const* intent)
{
    assert(mirror);
    assert(intent);
    if( mirror->intent_count >= TORIRS_CHROME_MIRROR_INTENTS )
    {
        /* Reported rather than silently dropped: a click that vanished is
         * indistinguishable from a control that does not work. */
        mirror->intent_overflow = 1;
        return;
    }
    mirror->intents[mirror->intent_count++] = *intent;
}

void
ToriRSChromeMirror_PushActivate(struct ToriRSChromeMirror* mirror, int panel, int widget)
{
    struct ToriRSChromeIntent intent;
    memset(&intent, 0, sizeof(intent));
    intent.kind = TORIRS_CHROME_INTENT_ACTIVATE;
    intent.panel = panel;
    intent.widget = widget;
    ToriRSChromeMirror_PushIntent(mirror, &intent);
}

void
ToriRSChromeMirror_PushToggle(struct ToriRSChromeMirror* mirror, int panel, int widget, int on)
{
    struct ToriRSChromeIntent intent;
    memset(&intent, 0, sizeof(intent));
    intent.kind = TORIRS_CHROME_INTENT_TOGGLE;
    intent.panel = panel;
    intent.widget = widget;
    intent.value = on ? 1 : 0;
    ToriRSChromeMirror_PushIntent(mirror, &intent);
    /* A toggled checkbox is also an activation: the host's TakeActivated drain
     * is what turns a plugin's switch into PluginHost_SetEnabled, and an
     * executor that sent only the new state would set the value without ever
     * running the thing that reacts to it. */
    ToriRSChromeMirror_PushActivate(mirror, panel, widget);
}

void
ToriRSChromeMirror_PushText(
    struct ToriRSChromeMirror* mirror, int panel, int widget, char const* text)
{
    struct ToriRSChromeIntent intent;
    int i = 0;

    memset(&intent, 0, sizeof(intent));
    intent.kind = TORIRS_CHROME_INTENT_TEXT;
    intent.panel = panel;
    intent.widget = widget;
    if( text )
        for( ; i < TORIRS_CHROME_TEXT_MAX - 1 && text[i]; i++ )
            intent.text[i] = text[i];
    intent.text[i] = '\0';
    ToriRSChromeMirror_PushIntent(mirror, &intent);
}

int
ToriRSChromeMirror_Poll(
    struct ToriRSChromeMirror* mirror, struct ToriRSChromeIntent* out, int max)
{
    int n;

    assert(mirror);
    assert(out);
    n = mirror->intent_count < max ? mirror->intent_count : max;
    for( int i = 0; i < n; i++ )
        out[i] = mirror->intents[i];
    /* Shuffle the tail down rather than resetting: the host drains in batches,
     * and dropping what did not fit in one would lose clicks at exactly the
     * moment there were a lot of them. */
    for( int i = n; i < mirror->intent_count; i++ )
        mirror->intents[i - n] = mirror->intents[i];
    mirror->intent_count -= n;
    return n;
}
