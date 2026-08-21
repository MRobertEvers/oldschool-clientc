#include "torirs_chrome_exec.h"

#include <assert.h>
#include <string.h>

/* ---- helpers ------------------------------------------------------------- */

static void
chrome_copy(char* dst, int cap, char const* src)
{
    int i = 0;
    if( src )
        for( ; i < cap - 1 && src[i]; i++ )
            dst[i] = src[i];
    dst[i] = '\0';
}

/** Start a command with everything cleared and the handles set. */
static void
cmd_init(struct ToriRSChromeCmd* cmd, int kind, int panel, int widget)
{
    memset(cmd, 0, sizeof(*cmd));
    cmd->kind = kind;
    cmd->panel = panel;
    cmd->widget = widget;
    cmd->tab = -1;
    cmd->value = 0;
}

static void
sync_emit(struct ToriRSChromeSync* sync, struct ToriRSChromeCmd const* cmd)
{
    sync->cmd_count++;
    if( sync->exec.apply )
        sync->exec.apply(sync->exec.user, cmd);
}

/** The commonest shape: a kind, a widget and an int. */
static void
sync_emit_value(struct ToriRSChromeSync* sync, int kind, int panel, int widget, int value)
{
    struct ToriRSChromeCmd cmd;
    cmd_init(&cmd, kind, panel, widget);
    cmd.value = value;
    sync_emit(sync, &cmd);
}

/**
 * Restate a widget's whole borrowed list: the count, then one command per entry.
 *
 * Clamped to what the widget says it has rather than trusting the pointer,
 * because a list is borrowed and a caller that shrank its array without telling
 * the widget would otherwise have this walk off the end of it.
 */
static void
sync_emit_options(
    struct ToriRSChromeSync* sync, struct ToriDbgWidget const* w, int panel, int widget)
{
    struct ToriRSChromeCmd cmd;

    sync_emit_value(sync, TORIRS_CHROME_CMD_WIDGET_OPTIONS, panel, widget, w->option_count);
    if( !w->options )
        return;
    for( int i = 0; i < w->option_count; i++ )
    {
        cmd_init(&cmd, TORIRS_CHROME_CMD_WIDGET_OPTION, panel, widget);
        cmd.value = i;
        chrome_copy(cmd.text, TORIRS_CHROME_TEXT_MAX, w->options[i]);
        sync_emit(sync, &cmd);
    }
}

/* ---- sync ---------------------------------------------------------------- */

int
ToriRSChromeSync_Init(struct ToriRSChromeSync* sync, struct ToriRSChromeExec const* exec)
{
    assert(sync);
    assert(exec);

    memset(sync, 0, sizeof(*sync));
    sync->exec = *exec;
    /* No begin at all is a valid executor -- the buffer one has nothing to
     * bring up -- and counts as having come up. */
    if( sync->exec.begin && !sync->exec.begin(sync->exec.user) )
    {
        sync->live = 0;
        return 0;
    }
    sync->live = 1;
    return 1;
}

void
ToriRSChromeSync_Shutdown(struct ToriRSChromeSync* sync)
{
    assert(sync);
    if( sync->live && sync->exec.end )
        sync->exec.end(sync->exec.user);
    memset(sync, 0, sizeof(*sync));
}

/**
 * Is this widget one the executor should be told about at all?
 *
 * Hidden rows ARE announced -- an executor may want to keep the control and
 * hide it, exactly as the chrome does -- but a free slot is not a widget, and
 * a widget on a panel that is closed is covered by the panel's own CLOSE.
 */
static int
sync_widget_relevant(struct ToriRSChrome const* ui, int widget)
{
    struct ToriDbgWidget const* w = &ui->widgets[widget];
    if( w->kind == TORIDBG_W_FREE )
        return 0;
    if( w->panel < 0 || w->panel >= ui->panel_count )
        return 0;
    return ui->panels[w->panel].visible;
}

int
ToriRSChromeSync_Run(struct ToriRSChromeSync* sync, struct ToriRSChrome const* ui)
{
    int const before = sync->cmd_count;
    struct ToriRSChromeCmd cmd;

    assert(sync);
    assert(ui);
    if( !sync->live )
        return 0;

    cmd_init(&cmd, TORIRS_CHROME_CMD_SYNC_BEGIN, -1, -1);
    sync_emit(sync, &cmd);

    /*
     * Panels first, and closes before opens.
     *
     * An executor is entitled to assume a widget's panel exists when it hears
     * about the widget, and that nothing survives a panel it was told closed.
     * Doing the closes in their own pass is what makes that true even when a
     * panel is hidden in the same frame another is shown.
     */
    for( int i = 0; i < TORIDBG_MAX_PANELS; i++ )
    {
        struct ToriRSChromeShadowPanel* sp = &sync->panels[i];
        int const alive = i < ui->panel_count && ui->panels[i].visible;

        if( sp->live && !alive )
        {
            cmd_init(&cmd, TORIRS_CHROME_CMD_PANEL_CLOSE, i, -1);
            sync_emit(sync, &cmd);
            memset(sp, 0, sizeof(*sp));
            /* Every row of it went with it, so the shadow must forget them or
             * the next Run would emit a REMOVE for each one on a panel the
             * executor no longer has. */
            for( int j = 0; j < TORIDBG_MAX_WIDGETS; j++ )
                if( sync->widgets[j].live && sync->widgets[j].panel == i )
                    memset(&sync->widgets[j], 0, sizeof(sync->widgets[j]));
        }
    }

    for( int i = 0; i < ui->panel_count; i++ )
    {
        struct ToriDbgPanel const* p = &ui->panels[i];
        struct ToriRSChromeShadowPanel* sp = &sync->panels[i];

        if( !p->visible )
            continue;

        if( !sp->live )
        {
            cmd_init(&cmd, TORIRS_CHROME_CMD_PANEL_OPEN, i, -1);
            cmd.value = p->style;
            chrome_copy(cmd.text, TORIRS_CHROME_TEXT_MAX, p->title);
            sync_emit(sync, &cmd);
            sp->live = 1;
            sp->style = p->style;
            chrome_copy(sp->title, TORIDBG_LABEL_MAX, p->title);
            /* Force the rect and tab below to be sent for a new panel. */
            sp->x = sp->y = sp->w = sp->h = -1;
            sp->active_tab = -1;
        }
        else if( strcmp(sp->title, p->title) != 0 )
        {
            cmd_init(&cmd, TORIRS_CHROME_CMD_PANEL_TITLE, i, -1);
            chrome_copy(cmd.text, TORIRS_CHROME_TEXT_MAX, p->title);
            sync_emit(sync, &cmd);
            chrome_copy(sp->title, TORIDBG_LABEL_MAX, p->title);
        }

        if( sp->x != p->x || sp->y != p->y || sp->w != p->w || sp->h != p->h )
        {
            cmd_init(&cmd, TORIRS_CHROME_CMD_PANEL_RECT, i, -1);
            cmd.x = p->x;
            cmd.y = p->y;
            cmd.w = p->w;
            cmd.h = p->h;
            sync_emit(sync, &cmd);
            sp->x = p->x;
            sp->y = p->y;
            sp->w = p->w;
            sp->h = p->h;
        }
        if( sp->active_tab != p->active_tab )
        {
            sync_emit_value(sync, TORIRS_CHROME_CMD_PANEL_TAB, i, -1, p->active_tab);
            sp->active_tab = p->active_tab;
        }
    }

    /* Removals before additions, for the same reason panel closes come first:
     * a handle freed this frame can be reused by an add in the same frame, and
     * an executor told "add 7" before "remove 7" would end up with neither. */
    for( int i = 0; i < TORIDBG_MAX_WIDGETS; i++ )
    {
        struct ToriRSChromeShadowWidget* sw = &sync->widgets[i];
        if( !sw->live )
            continue;
        if( i < ui->widget_count && sync_widget_relevant(ui, i) &&
            ui->widgets[i].serial == sw->serial )
            continue;
        sync_emit_value(sync, TORIRS_CHROME_CMD_WIDGET_REMOVE, sw->panel, i, 0);
        memset(sw, 0, sizeof(*sw));
    }

    /*
     * In ROW order, walking each panel's own list -- not by handle index.
     *
     * The array order is allocation order, and the free list makes those two
     * diverge the moment a panel is cleared and rebuilt. An executor that
     * creates its controls in the order the ADDs arrive would then lay the
     * window out in the order rows were first created rather than the order
     * they are in now -- the Save button above the settings it commits, which
     * is exactly how this was found. The row list is the model's own order, so
     * emitting in it means an executor never has to sort.
     */
    for( int p = 0; p < ui->panel_count; p++ )
    {
        if( !ui->panels[p].visible )
            continue;
    for( int i = ui->panels[p].first_widget; i >= 0; i = ui->widgets[i].next )
    {
        struct ToriDbgWidget const* w = &ui->widgets[i];
        struct ToriRSChromeShadowWidget* sw = &sync->widgets[i];

        if( !sync_widget_relevant(ui, i) )
            continue;

        if( !sw->live )
        {
            cmd_init(&cmd, TORIRS_CHROME_CMD_WIDGET_ADD, w->panel, i);
            cmd.value = w->kind;
            cmd.tab = w->tab;
            cmd.color = w->color;
            chrome_copy(cmd.label, TORIDBG_LABEL_MAX, w->label);
            chrome_copy(cmd.text, TORIRS_CHROME_TEXT_MAX, w->text);
            sync_emit(sync, &cmd);

            sw->live = 1;
            sw->serial = w->serial;
            sw->kind = w->kind;
            sw->panel = w->panel;
            sw->tab = w->tab;
            sw->color = w->color;
            chrome_copy(sw->label, TORIDBG_LABEL_MAX, w->label);
            chrome_copy(sw->text, TORIDBG_INPUT_MAX, w->text);
            /* Everything below is compared against a value the ADD did not
             * carry, so seed the shadow with an impossible one to force the
             * first statement of each. */
            sw->hidden = -1;
            sw->checked = -1;
            sw->selected = -2;
            sw->option_count = -1;
            sw->options = NULL;
        }
        else
        {
            if( strcmp(sw->label, w->label) != 0 )
            {
                cmd_init(&cmd, TORIRS_CHROME_CMD_WIDGET_LABEL, w->panel, i);
                chrome_copy(cmd.label, TORIDBG_LABEL_MAX, w->label);
                sync_emit(sync, &cmd);
                chrome_copy(sw->label, TORIDBG_LABEL_MAX, w->label);
            }
            if( strcmp(sw->text, w->text) != 0 )
            {
                cmd_init(&cmd, TORIRS_CHROME_CMD_WIDGET_TEXT, w->panel, i);
                chrome_copy(cmd.text, TORIRS_CHROME_TEXT_MAX, w->text);
                sync_emit(sync, &cmd);
                chrome_copy(sw->text, TORIDBG_INPUT_MAX, w->text);
            }
            if( sw->color != w->color )
            {
                cmd_init(&cmd, TORIRS_CHROME_CMD_WIDGET_COLOR, w->panel, i);
                cmd.color = w->color;
                sync_emit(sync, &cmd);
                sw->color = w->color;
            }
        }

        if( sw->hidden != w->hidden )
        {
            sync_emit_value(sync, TORIRS_CHROME_CMD_WIDGET_HIDDEN, w->panel, i, w->hidden);
            sw->hidden = w->hidden;
        }
        if( sw->checked != w->checked )
        {
            sync_emit_value(sync, TORIRS_CHROME_CMD_WIDGET_CHECKED, w->panel, i, w->checked);
            sw->checked = w->checked;
        }
        /* The list before the selection into it: an executor that resizes a
         * native combo box on OPTIONS would otherwise clamp a selection it
         * has not been told how to hold. */
        if( sw->options != w->options || sw->option_count != w->option_count )
        {
            sync_emit_options(sync, w, w->panel, i);
            sw->options = w->options;
            sw->option_count = w->option_count;
            /* The list changed under it, so the selection must be restated
             * whether or not its index happens to be the same number. */
            sw->selected = -2;
        }
        if( sw->selected != w->selected )
        {
            sync_emit_value(sync, TORIRS_CHROME_CMD_WIDGET_SELECTED, w->panel, i, w->selected);
            sw->selected = w->selected;
        }
    }
    }

    cmd_init(&cmd, TORIRS_CHROME_CMD_SYNC_END, -1, -1);
    sync_emit(sync, &cmd);

    sync->primed = 1;
    /* The two markers are not "something moved": a caller asking whether this
     * frame had any content subtracts them. */
    return sync->cmd_count - before - 2;
}

/* ---- intents ------------------------------------------------------------- */

int
ToriRSChromeIntent_Apply(struct ToriRSChrome* ui, struct ToriRSChromeIntent const* intent)
{
    assert(ui);
    assert(intent);

    switch( intent->kind )
    {
    case TORIRS_CHROME_INTENT_TOGGLE:
        ToriRSChrome_SetChecked(ui, intent->widget, intent->value);
        return 1;

    case TORIRS_CHROME_INTENT_TEXT:
        ToriRSChrome_SetText(ui, intent->widget, intent->text);
        return 1;

    case TORIRS_CHROME_INTENT_PICK:
        ToriRSChrome_DropdownSetSelected(ui, intent->widget, intent->value);
        return 1;

    case TORIRS_CHROME_INTENT_TAB:
        ToriRSChrome_PanelSetActiveTab(ui, intent->panel, intent->value);
        return 1;

    case TORIRS_CHROME_INTENT_ACTIVATE:
        /*
         * Straight into the latch the in-canvas path uses, so a host draining
         * ToriRSChrome_TakeActivated reacts to a click in a browser tab, a
         * Win32 button and an in-canvas row through one code path. That
         * shared drain is the whole point of routing intents at the model
         * rather than at the host's own handlers.
         */
        if( intent->widget >= 0 && intent->widget < ui->widget_count )
        {
            ui->activated = intent->widget;
            return 1;
        }
        return 0;

    case TORIRS_CHROME_INTENT_CLOSE:
        ToriRSChrome_PanelSetVisible(ui, intent->panel, 0);
        return 1;

    default:
        return 0;
    }
}

int
ToriRSChromeSync_Pump(struct ToriRSChromeSync* sync, struct ToriRSChrome* ui)
{
    struct ToriRSChromeIntent batch[16];
    int applied = 0;
    int got;

    assert(sync);
    assert(ui);
    if( !sync->live || !sync->exec.poll )
        return 0;

    /* Drained in batches until the executor runs dry, so a burst of clicks
     * arriving in one frame is not spread over the next sixteen. */
    do
    {
        got = sync->exec.poll(sync->exec.user, batch, (int)(sizeof(batch) / sizeof(batch[0])));
        for( int i = 0; i < got; i++ )
            applied += ToriRSChromeIntent_Apply(ui, &batch[i]);
    } while( got == (int)(sizeof(batch) / sizeof(batch[0])) );

    return applied;
}

/* ---- the buffer executor ------------------------------------------------- */

static void
buffer_apply(void* user, struct ToriRSChromeCmd const* cmd)
{
    (void)user;
    (void)cmd;
    /* Nothing, and deliberately: the model draws itself through
     * ToriRSChrome_Build/Prims. See the header note on why this is not a stub. */
}

struct ToriRSChromeExec
ToriRSChromeExec_Buffer(void)
{
    struct ToriRSChromeExec exec;
    memset(&exec, 0, sizeof(exec));
    exec.apply = buffer_apply;
    return exec;
}

/* ---- choosing one -------------------------------------------------------- */

static char const* const CHROME_EXEC_NAME[TORIRS_CHROME_EXEC_COUNT] = {
    [TORIRS_CHROME_EXEC_BUFFER] = "buffer", [TORIRS_CHROME_EXEC_SDL] = "sdl",
    [TORIRS_CHROME_EXEC_WEB] = "web",       [TORIRS_CHROME_EXEC_GDI] = "gdi",
    [TORIRS_CHROME_EXEC_CS2] = "cs2",
};

char const*
ToriRSChromeExec_KindName(int kind)
{
    if( kind < 0 || kind >= TORIRS_CHROME_EXEC_COUNT || !CHROME_EXEC_NAME[kind] )
        return "buffer";
    return CHROME_EXEC_NAME[kind];
}

int
ToriRSChromeExec_KindFromName(char const* name)
{
    if( !name || !name[0] )
        return -1;
    for( int i = 0; i < TORIRS_CHROME_EXEC_COUNT; i++ )
        if( CHROME_EXEC_NAME[i] && strcmp(CHROME_EXEC_NAME[i], name) == 0 )
            return i;
    return -1;
}

struct ToriRSChromeExec
ToriRSChromeExec_ForKind(
    int kind, void* platform, ToriRSChromeRasteriseFn rasterise, void* rasterise_user,
    int* out_kind)
{
    switch( kind )
    {
    case TORIRS_CHROME_EXEC_CS2:
    {
        /* The only executor whose target is not a platform: `platform` is
         * unused and the tree comes from the host, which binds it separately
         * through ToriRSChromeExec_Cs2. Reaching it from here would need this
         * file to know what an App is. */
        if( out_kind )
            *out_kind = TORIRS_CHROME_EXEC_BUFFER;
        break;
    }
#if defined(TORIRS_CHROME_EXEC_GDI_AVAILABLE)
    case TORIRS_CHROME_EXEC_GDI:
    {
        struct ToriRSChromeExec exec = ToriRSChromeExec_Gdi(platform);
        if( out_kind )
            *out_kind = TORIRS_CHROME_EXEC_GDI;
        return exec;
    }
#endif
#if defined(TORIRS_CHROME_EXEC_WEB_AVAILABLE)
    case TORIRS_CHROME_EXEC_WEB:
    {
        struct ToriRSChromeExec exec = ToriRSChromeExec_Web();
        if( out_kind )
            *out_kind = TORIRS_CHROME_EXEC_WEB;
        return exec;
    }
#endif
#if defined(TORIRS_CHROME_EXEC_SDL_AVAILABLE)
    case TORIRS_CHROME_EXEC_SDL:
    {
        struct ToriRSChromeExec exec = ToriRSChromeExec_Sdl(platform, rasterise, rasterise_user);
        if( out_kind )
            *out_kind = TORIRS_CHROME_EXEC_SDL;
        return exec;
    }
#endif
    default:
        break;
    }
    (void)platform;
    (void)rasterise;
    (void)rasterise_user;
    /* Everything else -- not compiled in, not implemented on this platform, or
     * simply not asked for -- lands on the one every build has. */
    if( out_kind )
        *out_kind = TORIRS_CHROME_EXEC_BUFFER;
    return ToriRSChromeExec_Buffer();
}

/* ---- the recorder -------------------------------------------------------- */

static int
recorder_begin(void* user)
{
    struct ToriRSChromeRecorder* rec = user;
    assert(rec);
    if( rec->refuse )
        return 0;
    rec->begun = 1;
    return 1;
}

static void
recorder_apply(void* user, struct ToriRSChromeCmd const* cmd)
{
    struct ToriRSChromeRecorder* rec = user;
    assert(rec);
    assert(cmd);
    if( rec->count >= TORIRS_CHROME_RECORD_MAX )
    {
        rec->overflow = 1;
        return;
    }
    rec->cmds[rec->count++] = *cmd;
}

static void
recorder_end(void* user)
{
    struct ToriRSChromeRecorder* rec = user;
    assert(rec);
    rec->begun = 0;
}

static int
recorder_poll(void* user, struct ToriRSChromeIntent* out, int max)
{
    struct ToriRSChromeRecorder* rec = user;
    int n;

    assert(rec);
    assert(out);
    n = rec->pending_count < max ? rec->pending_count : max;
    for( int i = 0; i < n; i++ )
        out[i] = rec->pending[i];
    for( int i = n; i < rec->pending_count; i++ )
        rec->pending[i - n] = rec->pending[i];
    rec->pending_count -= n;
    return n;
}

void
ToriRSChromeRecorder_Init(struct ToriRSChromeRecorder* rec)
{
    assert(rec);
    memset(rec, 0, sizeof(*rec));
}

struct ToriRSChromeExec
ToriRSChromeExec_Recorder(struct ToriRSChromeRecorder* rec)
{
    struct ToriRSChromeExec exec;
    assert(rec);
    memset(&exec, 0, sizeof(exec));
    exec.user = rec;
    exec.begin = recorder_begin;
    exec.apply = recorder_apply;
    exec.end = recorder_end;
    exec.poll = recorder_poll;
    return exec;
}

void
ToriRSChromeRecorder_PushIntent(
    struct ToriRSChromeRecorder* rec, struct ToriRSChromeIntent const* intent)
{
    assert(rec);
    assert(intent);
    if( rec->pending_count >= (int)(sizeof(rec->pending) / sizeof(rec->pending[0])) )
        return;
    rec->pending[rec->pending_count++] = *intent;
}

int
ToriRSChromeRecorder_CountKind(struct ToriRSChromeRecorder const* rec, int kind)
{
    int n = 0;
    assert(rec);
    for( int i = 0; i < rec->count; i++ )
        if( rec->cmds[i].kind == kind )
            n++;
    return n;
}

struct ToriRSChromeCmd const*
ToriRSChromeRecorder_Find(struct ToriRSChromeRecorder const* rec, int kind, int widget)
{
    assert(rec);
    for( int i = 0; i < rec->count; i++ )
        if( rec->cmds[i].kind == kind && rec->cmds[i].widget == widget )
            return &rec->cmds[i];
    return NULL;
}
