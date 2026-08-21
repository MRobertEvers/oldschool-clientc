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
    return handle >= 0 && handle < TORIRS_CHROME_MAX_WIDGETS && mirror->widgets[handle].live;
}

static int
mirror_valid_panel(struct ToriRSChromeMirror const* mirror, int handle)
{
    return handle >= 0 && handle < TORIRS_CHROME_MAX_PANELS && mirror->panels[handle].live;
}

int
ToriRSChromeMirror_Order(struct ToriRSChromeMirror const* mirror, int* out, int max)
{
    int n = 0;

    assert(mirror);
    assert(out);
    for( int i = 0; i < TORIRS_CHROME_MAX_WIDGETS && n < max; i++ )
        if( mirror->widgets[i].live )
            out[n++] = i;

    /* Insertion sort on `order`: the list is already nearly sorted (handles
     * only go out of order where the free list recycled one), and n is a few
     * dozen. */
    for( int i = 1; i < n; i++ )
    {
        int const h = out[i];
        int const key = mirror->widgets[h].order;
        int j = i - 1;
        while( j >= 0 && mirror->widgets[out[j]].order > key )
        {
            out[j + 1] = out[j];
            j--;
        }
        out[j + 1] = h;
    }
    return n;
}

struct ToriRSChromeMirrorWidget*
ToriRSChromeMirror_Widget(struct ToriRSChromeMirror* mirror, int handle)
{
    assert(mirror);
    if( !mirror_valid_widget(mirror, handle) )
        return NULL;
    return &mirror->widgets[handle];
}

struct ToriRSChromeMirrorPanel*
ToriRSChromeMirror_Panel(struct ToriRSChromeMirror* mirror, int handle)
{
    assert(mirror);
    if( !mirror_valid_panel(mirror, handle) )
        return NULL;
    return &mirror->panels[handle];
}

int
ToriRSChromeMirror_HandleOfNative(struct ToriRSChromeMirror const* mirror, intptr_t native)
{
    assert(mirror);
    /* 0 is not a valid native id anywhere this serves: a NULL HWND, a zero DOM
     * node and component index 0 are all "nothing yet", and matching on it
     * would return the first unassigned slot. */
    if( native == 0 )
        return -1;
    for( int i = 0; i < TORIRS_CHROME_MAX_WIDGETS; i++ )
        if( mirror->widgets[i].live && mirror->widgets[i].native == native )
            return i;
    return -1;
}

int
ToriRSChromeMirror_Shown(struct ToriRSChromeMirror const* mirror, int handle)
{
    struct ToriRSChromeMirrorWidget const* w;

    assert(mirror);
    if( !mirror_valid_widget(mirror, handle) )
        return 0;
    w = &mirror->widgets[handle];
    if( w->hidden )
        return 0;
    if( w->tab < 0 )
        return 1;
    if( !mirror_valid_panel(mirror, w->panel) )
        return 0;
    return w->tab == mirror->panels[w->panel].active_tab;
}

int
ToriRSChromeMirror_Apply(struct ToriRSChromeMirror* mirror, struct ToriRSChromeCmd const* cmd)
{
    assert(mirror);
    assert(cmd);

    switch( cmd->kind )
    {
    case TORIRS_CHROME_CMD_PANEL_OPEN:
        if( cmd->panel < 0 || cmd->panel >= TORIRS_CHROME_MAX_PANELS )
            return 0;
        memset(&mirror->panels[cmd->panel], 0, sizeof(mirror->panels[cmd->panel]));
        mirror->panels[cmd->panel].live = 1;
        mirror->panels[cmd->panel].style = cmd->value;
        return 1;

    case TORIRS_CHROME_CMD_PANEL_CLOSE:
        if( cmd->panel < 0 || cmd->panel >= TORIRS_CHROME_MAX_PANELS )
            return 0;
        memset(&mirror->panels[cmd->panel], 0, sizeof(mirror->panels[cmd->panel]));
        /*
         * Every widget of it goes too, because the seam does not send a REMOVE
         * per row on a panel close -- it says so once and means all of them.
         * A mirror that kept them would hand out native ids for controls the
         * executor has already destroyed.
         */
        for( int i = 0; i < TORIRS_CHROME_MAX_WIDGETS; i++ )
            if( mirror->widgets[i].live && mirror->widgets[i].panel == cmd->panel )
                memset(&mirror->widgets[i], 0, sizeof(mirror->widgets[i]));
        return 1;

    case TORIRS_CHROME_CMD_PANEL_TAB:
        if( !mirror_valid_panel(mirror, cmd->panel) )
            return 0;
        mirror->panels[cmd->panel].active_tab = cmd->value;
        /* Shape, not a property: which controls exist on screen just changed,
         * and an executor has to show and hide accordingly. */
        return 1;

    case TORIRS_CHROME_CMD_WIDGET_ADD:
        if( cmd->widget < 0 || cmd->widget >= TORIRS_CHROME_MAX_WIDGETS )
            return 0;
        memset(&mirror->widgets[cmd->widget], 0, sizeof(mirror->widgets[cmd->widget]));
        mirror->widgets[cmd->widget].live = 1;
        mirror->widgets[cmd->widget].kind = cmd->value;
        mirror->widgets[cmd->widget].panel = cmd->panel;
        mirror->widgets[cmd->widget].tab = cmd->tab;
        mirror->widgets[cmd->widget].order = mirror->next_order++;
        return 1;

    case TORIRS_CHROME_CMD_WIDGET_REMOVE:
        if( cmd->widget < 0 || cmd->widget >= TORIRS_CHROME_MAX_WIDGETS )
            return 0;
        /* Cleared rather than flagged: the handle can be recycled by an ADD in
         * the very next command, and a stale native id surviving into it is
         * the executor updating a control that belongs to something else. */
        memset(&mirror->widgets[cmd->widget], 0, sizeof(mirror->widgets[cmd->widget]));
        return 1;

    case TORIRS_CHROME_CMD_WIDGET_HIDDEN:
        if( !mirror_valid_widget(mirror, cmd->widget) )
            return 0;
        mirror->widgets[cmd->widget].hidden = cmd->value;
        return 1;

    default:
        /* Every property command: the executor applies it to the native
         * control it already has, and the mirror holds no copy to update. */
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
