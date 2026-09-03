#include "torirs_chrome_exec.h"

#include "uitree.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

/* ---- the client-chrome group ---------------------------------------------
 *
 * Here rather than beside the one control that uses it: the group is a fact
 * about the TREE (which roots the emit walk will show), not about the button,
 * and the next piece of client furniture will want the same answer.
 */

int
ToriRSChrome_TreeAcceptsChrome(struct UITree const* tree)
{
    int group;

    if( !tree || tree->root_index < 0 )
        return 0;
    if( (uint32_t)tree->root_index >= tree->component_count )
        return 0;
    group = (tree->components[tree->root_index].component_id >> 16) & 0xffff;
    /* Our own group already sitting first means chrome got in ahead of the
     * gameframe -- the state this exists to keep out of. */
    return group != TORIRS_CHROME_GROUP;
}

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
    struct ToriRSChromeSync* sync, struct ToriRSChromeWidget const* w, int panel, int widget)
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
    sync->synced_build_serial = -1;
    sync->presented_build_serial = -1;
    sync->published_drag_build_serial = -1;
    sync->published_drag_panel = -1;
    sync->exec = *exec;
    /* Not a style any model can hold, so the first Run states the real one --
     * @see ToriRSChromeSync::check_style. */
    sync->check_style = -1;
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

int
ToriRSChromeSync_TakePresentChange(
    struct ToriRSChromeSync* sync, struct ToriRSChrome const* ui)
{
    assert(sync);
    assert(ui);
    if( !sync->live || !sync->exec.is_surface ||
        sync->presented_build_serial == ui->build_serial )
        return 0;
    sync->presented_build_serial = ui->build_serial;
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
    struct ToriRSChromeWidget const* w = &ui->widgets[widget];
    if( w->kind == TORIRS_CHROME_W_FREE )
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
    /* A settled retained frame has neither unbuilt mutations nor a display-list
     * generation the executor has not scanned. Do not even bracket a native
     * transaction in that case: begin/end can trigger UI-thread scheduling and
     * a max-capacity shadow walk is still work even when it finds no deltas. */
    if( sync->primed && !ui->dirty && sync->synced_build_serial == ui->build_serial )
        return 0;

    cmd_init(&cmd, TORIRS_CHROME_CMD_SYNC_BEGIN, -1, -1);
    sync_emit(sync, &cmd);

    /*
     * The checkbox style, before any panel.
     *
     * First because it is a property of the whole chrome and an executor that
     * hears it after the rows have been declared has already placed and sized
     * every checkbox against the wrong art. Ahead of the panel CLOSE pass too,
     * which costs nothing: closes carry no geometry.
     */
    if( sync->check_style != ui->check_style )
    {
        cmd_init(&cmd, TORIRS_CHROME_CMD_CHECK_STYLE, -1, -1);
        cmd.value = ui->check_style;
        sync_emit(sync, &cmd);
        sync->check_style = ui->check_style;
    }

    /*
     * Panels first, and closes before opens.
     *
     * An executor is entitled to assume a widget's panel exists when it hears
     * about the widget, and that nothing survives a panel it was told closed.
     * Doing the closes in their own pass is what makes that true even when a
     * panel is hidden in the same frame another is shown.
     */
    for( int i = 0; i < TORIRS_CHROME_MAX_PANELS; i++ )
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
            for( int j = 0; j < TORIRS_CHROME_MAX_WIDGETS; j++ )
                if( sync->widgets[j].live && sync->widgets[j].panel == i )
                    memset(&sync->widgets[j], 0, sizeof(sync->widgets[j]));
        }
    }

    for( int i = 0; i < ui->panel_count; i++ )
    {
        struct ToriRSChromePanel const* p = &ui->panels[i];
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
            chrome_copy(sp->title, TORIRS_CHROME_LABEL_MAX, p->title);
            /* Force the rect and tab below to be sent for a new panel. */
            sp->x = sp->y = sp->w = sp->h = -1;
            sp->active_tab = -1;
        }
        else if( strcmp(sp->title, p->title) != 0 )
        {
            cmd_init(&cmd, TORIRS_CHROME_CMD_PANEL_TITLE, i, -1);
            chrome_copy(cmd.text, TORIRS_CHROME_TEXT_MAX, p->title);
            sync_emit(sync, &cmd);
            chrome_copy(sp->title, TORIRS_CHROME_LABEL_MAX, p->title);
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
    for( int i = 0; i < TORIRS_CHROME_MAX_WIDGETS; i++ )
    {
        struct ToriRSChromeShadowWidget* sw = &sync->widgets[i];
        if( !sw->live )
            continue;
        if( i < ui->widget_count && sync_widget_relevant(ui, i) &&
            ui->widgets[i].serial == sw->serial &&
            (ui->widgets[i].intent_serial
                 ? ui->widgets[i].intent_serial
                 : (uint32_t)ui->widgets[i].serial) == sw->intent_serial )
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
        struct ToriRSChromeWidget const* w = &ui->widgets[i];
        struct ToriRSChromeShadowWidget* sw = &sync->widgets[i];

        if( !sync_widget_relevant(ui, i) )
            continue;

        if( !sw->live )
        {
            cmd_init(&cmd, TORIRS_CHROME_CMD_WIDGET_ADD, w->panel, i);
            cmd.value = w->kind;
            cmd.tab = w->tab;
            cmd.color = w->color;
            /* A LISTROW's action affordance and its lock are part of its
             * SHAPE, so they ride the one command that states a widget's
             * shape. A row that gained or lost either is a different row: the
             * panel rebuild that changed it gives it a new serial, and the
             * shadow answers that with a remove-then-add rather than an
             * update. */
            cmd.w = (w->row_action ? TORIRS_CHROME_ROW_ACTION : 0) |
                    (w->row_locked ? TORIRS_CHROME_ROW_LOCKED : 0);
            /* ...and the kind-specific vertical shape. A CUSTOM region keeps
             * its model height in scaled pixels, but foreign presenters lay
             * out in logical chrome units and apply their own density. */
            cmd.h = w->kind == TORIRS_CHROME_W_CUSTOM
                        ? w->view_h / (ui->scale > 0 ? ui->scale : 1)
                        : w->rows;
            cmd.serial = w->intent_serial ? w->intent_serial : (uint32_t)w->serial;
            chrome_copy(cmd.label, TORIRS_CHROME_LABEL_MAX, w->label);
            chrome_copy(cmd.text, TORIRS_CHROME_TEXT_MAX, w->text);
            sync_emit(sync, &cmd);

            sw->live = 1;
            sw->serial = w->serial;
            sw->intent_serial = cmd.serial;
            sw->kind = w->kind;
            sw->panel = w->panel;
            sw->tab = w->tab;
            sw->color = w->color;
            chrome_copy(sw->label, TORIRS_CHROME_LABEL_MAX, w->label);
            chrome_copy(sw->text, TORIRS_CHROME_INPUT_MAX, w->text);
            /* Everything below is compared against a value the ADD did not
             * carry, so seed the shadow with an impossible one to force the
             * first statement of each. */
            sw->hidden = -1;
            sw->checked = -1;
            sw->selected = -2;
            sw->option_count = -1;
            sw->options = NULL;
            sw->focused = -1;
        }
        else
        {
            if( strcmp(sw->label, w->label) != 0 )
            {
                cmd_init(&cmd, TORIRS_CHROME_CMD_WIDGET_LABEL, w->panel, i);
                chrome_copy(cmd.label, TORIRS_CHROME_LABEL_MAX, w->label);
                sync_emit(sync, &cmd);
                chrome_copy(sw->label, TORIRS_CHROME_LABEL_MAX, w->label);
            }
            if( strcmp(sw->text, w->text) != 0 )
            {
                cmd_init(&cmd, TORIRS_CHROME_CMD_WIDGET_TEXT, w->panel, i);
                chrome_copy(cmd.text, TORIRS_CHROME_TEXT_MAX, w->text);
                sync_emit(sync, &cmd);
                chrome_copy(sw->text, TORIRS_CHROME_INPUT_MAX, w->text);
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
        {
            int const focused = ui->focus == i ? 1 : 0;
            if( sw->focused != focused )
            {
                sync_emit_value(sync, TORIRS_CHROME_CMD_WIDGET_FOCUS, w->panel, i, focused);
                sw->focused = focused;
            }
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
        {
            /* What "selected" means, per kind -- see the command's own note.
             * A TEXTAREA's is WHICH LINE IS AT THE TOP of its box, which is the
             * one thing a presentation that cannot scroll a control of its own
             * (the in-canvas buffer, whose rows are drawn) needs in order
             * to show the same window of a long list the model is showing. */
            int const sel =
                w->kind == TORIRS_CHROME_W_TEXTAREA ? w->scroll : w->selected;
            if( sw->selected != sel )
            {
                /* The chosen option's own string rides along with its index,
                 * for the same reason INTENT_PICK carries both: an executor
                 * that shows the value as TEXT (the in-canvas closed
                 * dropdown) would otherwise need its own copy of the whole
                 * list just to turn the index back into a word. Executors
                 * holding a native combo box ignore it. */
                cmd_init(&cmd, TORIRS_CHROME_CMD_WIDGET_SELECTED, w->panel, i);
                cmd.value = sel;
                if( w->options && sel >= 0 && sel < w->option_count )
                    chrome_copy(cmd.text, TORIRS_CHROME_TEXT_MAX, w->options[sel]);
                sync_emit(sync, &cmd);
                sw->selected = sel;
            }
        }
    }
    }

    cmd_init(&cmd, TORIRS_CHROME_CMD_SYNC_END, -1, -1);
    sync_emit(sync, &cmd);

    sync->primed = 1;
    sync->synced_build_serial = ui->build_serial;
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
        if( intent->widget < 0 || intent->widget >= ui->widget_count ||
            ui->widgets[intent->widget].kind == TORIRS_CHROME_W_FREE )
            return 0;
        if( ui->widgets[intent->widget].checked == (intent->value ? 1 : 0) )
            return 0;
        ToriRSChrome_SetChecked(ui, intent->widget, intent->value);
        /* A native checkbox is one gesture carrying its resulting state. The
         * in-canvas path both changes the value and raises the activation latch;
         * doing only the first here made semantic plugin toggles render yet
         * never reach their active plugin callback. */
        ui->activated = intent->widget;
        ui->activated_action = 0;
        return 1;

    case TORIRS_CHROME_INTENT_TEXT:
    {
        char before[TORIRS_CHROME_INPUT_MAX];
        int selected;

        if( intent->widget < 0 || intent->widget >= ui->widget_count ||
            ui->widgets[intent->widget].kind == TORIRS_CHROME_W_FREE )
            return 0;
        chrome_copy(
            before, (int)sizeof(before), ui->widgets[intent->widget].text);
        selected = ui->widgets[intent->widget].selected;
        ToriRSChrome_SetText(ui, intent->widget, intent->text);
        /* A colour field's text is a RENDERING of its value, so an edit that
         * arrives from a presentation has to be resolved into one -- and the
         * commit rewrites the field to the palette entry it landed on, which is
         * how a native executor's un-quantised "#12ff34" comes back snapped to
         * something the renderer can actually produce. */
        if( intent->widget >= 0 && intent->widget < ui->widget_count &&
            ui->widgets[intent->widget].kind == TORIRS_CHROME_W_COLORPICK )
            ToriRSChrome_ColorPickCommitText(ui, intent->widget);
        return strcmp(before, ui->widgets[intent->widget].text) != 0 ||
               selected != ui->widgets[intent->widget].selected;
    }

    case TORIRS_CHROME_INTENT_PICK:
        /* A colour's "index" is its packed HSL16 -- the same field, a different
         * palette. @see TORIRS_CHROME_W_COLORPICK. */
        if( intent->widget >= 0 && intent->widget < ui->widget_count &&
            ui->widgets[intent->widget].kind == TORIRS_CHROME_W_COLORPICK )
        {
            ToriRSChrome_ColorPickSet(ui, intent->widget, intent->value);
            ui->activated = intent->widget;
            return 1;
        }
        ToriRSChrome_DropdownSetSelected(ui, intent->widget, intent->value);
        /* Latched as activated too, exactly as choosing a row in the
         * in-canvas list does: a plugin-owned dropdown dispatches to its
         * plugin off that latch, and a pick that skipped it worked for
         * staged settings while silently dropping plugin controls. */
        if( intent->widget >= 0 && intent->widget < ui->widget_count )
            ui->activated = intent->widget;
        return 1;

    case TORIRS_CHROME_INTENT_TAB:
        if( intent->panel < 0 || intent->panel >= ui->panel_count )
            return 0;
        {
            int const before = ui->panels[intent->panel].active_tab;
            ToriRSChrome_PanelSetActiveTab(ui, intent->panel, intent->value);
            return before != ui->panels[intent->panel].active_tab;
        }

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
            /* A text input's activation is a click on the field, and what the
             * model does with a click on a field is take the FOCUS -- the same
             * focus the in-canvas MouseDown sets -- so the host's keyboard
             * routing lands typing in it. Latching it as `activated` instead
             * would be a no-op: the staged-settings drain ignores config rows. */
            if( ui->widgets[intent->widget].kind == TORIRS_CHROME_W_TEXTINPUT ||
                ui->widgets[intent->widget].kind == TORIRS_CHROME_W_TEXTAREA )
                return ToriRSChrome_FocusWidget(
                    ui,
                    intent->widget,
                    (int)strlen(ui->widgets[intent->widget].text));
            /* A colour row has two zones and the executor says which fired by
             * WHICH INTENT it sends: ACTIVATE is the swatch, so it toggles the
             * axis popup, and ACTION is the field, so it takes the focus. The
             * alternative -- one intent plus a coordinate -- would make every
             * executor carry the row's geometry, which is precisely what the
             * seam exists to avoid. */
            if( ui->widgets[intent->widget].kind == TORIRS_CHROME_W_COLORPICK )
            {
                ToriRSChrome_ColorPickSetOpen(
                    ui, intent->widget, !ToriRSChrome_ColorPickIsOpen(ui, intent->widget));
                return 1;
            }
            ui->activated = intent->widget;
            return 1;
        }
        return 0;

    case TORIRS_CHROME_INTENT_ACTION:
        /* The same latch, with the flag that says which zone fired it -- so a
         * host draining TakeActivated + ActivationWasAction reads a gear click
         * from a native executor exactly as it reads one from the canvas. */
        if( intent->widget >= 0 && intent->widget < ui->widget_count )
        {
            /* A colour row's second zone is its HEX FIELD, and what a click on
             * a field does is take the focus -- exactly as ACTIVATE on a text
             * input does, and latching it as `activated` instead would be the
             * same no-op it would be there. */
            if( ui->widgets[intent->widget].kind == TORIRS_CHROME_W_COLORPICK )
                return ToriRSChrome_FocusWidget(
                    ui,
                    intent->widget,
                    (int)strlen(ui->widgets[intent->widget].text));
            ui->activated = intent->widget;
            ui->activated_action = 1;
            return 1;
        }
        return 0;

    case TORIRS_CHROME_INTENT_CLOSE:
        /*
         * A panel this model does not have is a message from a presentation
         * that has drifted -- a page holding a handle from before a rebuild,
         * or one that never learned which panel its window was showing. It is
         * dropped rather than asserted on, exactly as a widget intent naming
         * an unknown handle is: the far side is versioned separately and a
         * stale tab must not be able to abort the client.
         *
         * But it is answered with 0, not the 1 this used to give unasked.
         * `Pump` returns how many intents CHANGED something, and a close that
         * hid nothing while reporting success is precisely how the web
         * window's close mark stayed dead: every layer said it had worked.
         */
        if( intent->panel < 0 || intent->panel >= ui->panel_count ||
            !ui->panels[intent->panel].visible )
            return 0;
        ToriRSChrome_PanelSetVisible(ui, intent->panel, 0);
        return 1;

    case TORIRS_CHROME_INTENT_CUSTOM_ACTIVATE:
        if( !ToriRSChrome_CustomActivate(
                ui, intent->widget, intent->x, intent->y) )
            return 0;
        ui->activated_selection_generation = intent->selection_generation;
        ui->activated_widget_serial = intent->widget_serial;
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

int
ToriRSChromeSync_FillSurface(struct ToriRSChromeSync* sync, struct ToriRSChrome* ui, int panel)
{
    int w = 0;
    int h = 0;

    assert(sync);
    assert(ui);
    /* Not asserted: an executor with no window of its own is the COMMON case
     * -- the in-canvas one is every lane's default -- and "there is no window
     * to fill" is an answer, not a caller's mistake. Same for a window that
     * has not come up yet, or one the user has since closed. */
    if( !sync->live || !sync->exec.surface_size )
        return 0;
    if( !sync->exec.surface_size(sync->exec.user, &w, &h) )
        return 0;
    if( w <= 0 || h <= 0 )
        return 0;
    ToriRSChrome_PanelFill(ui, panel, w, h);
    return 1;
}

int
ToriRSChromeSync_PublishDragRegion(
    struct ToriRSChromeSync* sync, struct ToriRSChrome const* ui, int panel)
{
    struct ToriRSChromeDragRegion region;

    assert(sync);
    assert(ui);
    /* Not asserted, for FillSurface's reason: an executor whose window keeps
     * its OS frame has nowhere to put a handle, and that is most of them. */
    if( !sync->live || !sync->exec.set_drag_region )
        return 0;
    if( sync->published_drag_panel == panel &&
        sync->published_drag_build_serial == ui->build_serial )
        return 0;
    /*
     * Published whether or not there IS a region, which is why the return of
     * WindowDragRegion is dropped: it clears `region` on the way to answering
     * 0, and an executor that stopped hearing about handles would go on
     * offering the last set it was told about -- a band of the window that
     * eats presses because a tab strip used to be there.
     */
    ToriRSChrome_WindowDragRegion(ui, panel, &region);
    sync->exec.set_drag_region(sync->exec.user, &region);
    sync->published_drag_panel = panel;
    sync->published_drag_build_serial = ui->build_serial;
    return 1;
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

struct ToriRSChromeExec
ToriRSChromeExec_ForKind(
    int kind, void* platform, ToriRSChromeRasteriseFn rasterise, void* rasterise_user,
    int* out_kind)
{
    switch( kind )
{
    case TORIRS_CHROME_EXEC_PLATFORM:
#if defined(TORIRS_CHROME_EXEC_ANDROID_AVAILABLE)
        if( out_kind )
            *out_kind = TORIRS_CHROME_EXEC_ANDROID;
        return ToriRSChromeExec_Android(platform, rasterise, rasterise_user);
#elif defined(TORIRS_CHROME_EXEC_WEB_AVAILABLE)
        if( out_kind )
            *out_kind = TORIRS_CHROME_EXEC_WEB;
        return ToriRSChromeExec_Web();
#elif defined(TORIRS_CHROME_EXEC_BROWSER_AVAILABLE)
        if( out_kind )
            *out_kind = TORIRS_CHROME_EXEC_BROWSER;
        return ToriRSChromeExec_Browser(platform);
#elif defined(TORIRS_CHROME_EXEC_SDL_AVAILABLE)
        if( out_kind )
            *out_kind = TORIRS_CHROME_EXEC_SDL;
        return ToriRSChromeExec_Sdl(platform, rasterise, rasterise_user);
#else
        break;
#endif
#if defined(TORIRS_CHROME_EXEC_ANDROID_AVAILABLE)
    case TORIRS_CHROME_EXEC_ANDROID:
    {
        if( out_kind )
            *out_kind = TORIRS_CHROME_EXEC_ANDROID;
        return ToriRSChromeExec_Android(platform, rasterise, rasterise_user);
    }
#endif
#if defined(TORIRS_CHROME_EXEC_BROWSER_AVAILABLE)
    case TORIRS_CHROME_EXEC_BROWSER:
    {
        struct ToriRSChromeExec exec = ToriRSChromeExec_Browser(platform);
        if( out_kind )
            *out_kind = TORIRS_CHROME_EXEC_BROWSER;
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

/* A window only when a test asked for one: the recorder stands in for both
 * kinds of surface, and an unset size is the executor that shares somebody
 * else's. */
static int
recorder_surface_size(void* user, int* out_w, int* out_h)
{
    struct ToriRSChromeRecorder* rec = user;

    assert(rec);
    assert(out_w);
    assert(out_h);
    if( rec->surface_w <= 0 || rec->surface_h <= 0 )
        return 0;
    *out_w = rec->surface_w;
    *out_h = rec->surface_h;
    return 1;
}

/* Kept whole rather than diffed: the region is a dozen ints and the count
 * beside it is what a test asserts on to prove an EMPTY one was published too
 * -- which is the case a "publish only when it changed" recorder could not tell
 * apart from never publishing at all. */
static void
recorder_set_drag_region(void* user, struct ToriRSChromeDragRegion const* region)
{
    struct ToriRSChromeRecorder* rec = user;

    assert(rec);
    assert(region);
    rec->drag = *region;
    rec->drag_publishes++;
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
    exec.surface_size = recorder_surface_size;
    exec.set_drag_region = recorder_set_drag_region;
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
