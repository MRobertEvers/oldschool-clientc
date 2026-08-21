/*
 * The Win32 chrome executor: the plugin window as real Windows controls.
 *
 * A NATIVE-WIDGET executor (see the two kinds in torirs_chrome_exec.h), and the
 * only one whose widgets are the operating system's own: a BUTTON with
 * BS_AUTOCHECKBOX is a checkbox, an EDIT is a text field, a COMBOBOX is a
 * dropdown. The chrome's display list is not used at all -- none of those can
 * be reconstructed from rectangles, which is the entire reason this kind of
 * executor exists.
 *
 * ONE OWNED TOOL WINDOW, NEVER A RENDER TARGET. WS_EX_TOOLWINDOW keeps it off
 * the taskbar and `hwnd_owner` keeps it above the game and minimising with it.
 * No renderer is ever bound to it: see COMMON-CHROME-001, and the narrow
 * amendment to WINDOWS-HOST-001 that permits this window precisely because it
 * holds controls rather than a device.
 *
 * NO COMCTL32. The five control classes used here -- BUTTON, EDIT, STATIC,
 * COMBOBOX and the tab strip, which is drawn as a row of BUTTONs -- are all in
 * USER32, present since Win32 began. A real WC_TABCONTROL would mean linking
 * comctl32, shipping a manifest for the v6 common controls, and adding both to
 * the lane's import audit; a row of buttons is the same affordance with none of
 * that, on a window that already looks like a tool palette. The XP lane's
 * one-file artifact contract (WINXP-ABI-001) is what makes that trade worth
 * taking.
 *
 * Compiled only in the Windows lanes; TORIRS_CHROME_EXEC_GDI_AVAILABLE tells
 * the chooser it is here.
 */

#include "torirs_chrome_exec.h"
#include "torirs_chrome_mirror.h"

#include "../platform/platform_sdl2.h"

#include <windows.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

static char const CHROME_GDI_WNDCLASS[] = "TorirsChromeToolWindow";

/** Opening size. The window is resizable; this is only where it starts. */
#define CHROME_GDI_W 380
#define CHROME_GDI_H 460

/* Layout, in the tool window's own pixels. Not the chrome's metrics: these are
 * Windows controls at the system font, and matching them to a baked bitmap
 * font's line box would size them for the wrong glyphs. */
#define CHROME_GDI_PAD 8
#define CHROME_GDI_ROW_H 22
#define CHROME_GDI_ROW_GAP 4
#define CHROME_GDI_LABEL_W 110
#define CHROME_GDI_TAB_H 24
#define CHROME_GDI_TAB_W 96

/**
 * Control ids. Chrome handles are small and dense, so the id IS the handle
 * plus a base -- which makes the WM_COMMAND route a subtraction rather than a
 * search, and makes an id collision impossible by construction.
 */
#define CHROME_GDI_ID_BASE 0x4000
#define CHROME_GDI_ID_TAB_BASE 0x5000

struct ChromeGdi
{
    HWND owner;
    HWND hwnd;
    HFONT font;
    int open;
    struct ToriRSChromeMirror mirror;
    /** The panel whose tabs the strip shows, and its titles. One window, so
     *  one strip; a list would be a list with one entry. */
    int tab_panel;
    int tab_strip_widget;
    char tabs[16][64];
    int tab_count;
    HWND tab_buttons[16];
};

static struct ChromeGdi g_chrome_gdi;

/* ---- layout --------------------------------------------------------------- */

/**
 * Place every visible control down the window, in handle order.
 *
 * A full relayout rather than an incremental one, because the set of VISIBLE
 * rows changes wholesale on a tab switch and there is no incremental version of
 * that. It runs on shape changes only -- an add, a remove, a hide, a tab -- not
 * per frame, so the cost is a few dozen SetWindowPos calls when something
 * actually moved.
 *
 * Deferred through BeginDeferWindowPos so the window repaints once instead of
 * once per control, which is the difference between a tab switch and a flicker.
 */
static void
chrome_gdi_layout(struct ChromeGdi* s)
{
    RECT client;
    HDWP dwp;
    int y;
    int width;
    int live = 0;

    if( !s->hwnd )
        return;
    GetClientRect(s->hwnd, &client);
    width = client.right - client.left;

    for( int i = 0; i < TORIDBG_MAX_WIDGETS; i++ )
        if( ToriRSChromeMirror_Widget(&s->mirror, i) )
            live++;

    dwp = BeginDeferWindowPos(live + s->tab_count + 1);
    if( !dwp )
        return;

    /* The strip first, pinned above the rows -- the same rule the in-canvas
     * chrome follows, and for the same reason: a strip that scrolled away with
     * its rows would take the only way back with it. */
    for( int t = 0; t < s->tab_count; t++ )
        if( s->tab_buttons[t] )
            dwp = DeferWindowPos(
                dwp,
                s->tab_buttons[t],
                NULL,
                CHROME_GDI_PAD + t * (CHROME_GDI_TAB_W + 2),
                CHROME_GDI_PAD,
                CHROME_GDI_TAB_W,
                CHROME_GDI_TAB_H,
                SWP_NOZORDER | SWP_SHOWWINDOW);

    y = CHROME_GDI_PAD + (s->tab_count > 1 ? CHROME_GDI_TAB_H + CHROME_GDI_ROW_GAP : 0);

    /* In ROW order, not handle order -- see ToriRSChromeMirrorWidget::order.
     * A free-list-recycled handle walked by index lays the window out in an
     * order the model never had. */
    int order[TORIDBG_MAX_WIDGETS];
    int const ordered = ToriRSChromeMirror_Order(&s->mirror, order, TORIDBG_MAX_WIDGETS);
    for( int oi = 0; oi < ordered; oi++ )
    {
        int const i = order[oi];
        struct ToriRSChromeMirrorWidget* w = ToriRSChromeMirror_Widget(&s->mirror, i);
        HWND control;
        int row_h = CHROME_GDI_ROW_H;
        int x = CHROME_GDI_PAD;
        int row_w = width - 2 * CHROME_GDI_PAD;

        if( !w || !w->native )
            continue;
        control = (HWND)w->native;

        if( !ToriRSChromeMirror_Shown(&s->mirror, i) )
        {
            /* Hidden, not destroyed: the control keeps its text and its
             * selection, so switching back to a tab restores what was on it
             * rather than a rebuilt blank. */
            dwp = DeferWindowPos(dwp, control, NULL, 0, 0, 0, 0,
                                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_HIDEWINDOW);
            continue;
        }

        /* A separator is a thin etched STATIC, so it needs a row of its own
         * height rather than a control-sized one. */
        if( w->kind == TORIDBG_W_SEPARATOR )
            row_h = 2;

        /* Labelled controls start at the shared label column, matching the
         * in-canvas chrome's table mode -- the label is a second STATIC placed
         * by the ADD, and this only has to leave room for it. */
        if( w->kind == TORIDBG_W_TEXTINPUT || w->kind == TORIDBG_W_DROPDOWN )
        {
            x += CHROME_GDI_LABEL_W;
            row_w -= CHROME_GDI_LABEL_W;
        }
        if( row_w < 16 )
            row_w = 16;

        dwp = DeferWindowPos(dwp, control, NULL, x, y, row_w, row_h,
                             SWP_NOZORDER | SWP_SHOWWINDOW);
        y += row_h + CHROME_GDI_ROW_GAP;
    }

    EndDeferWindowPos(dwp);
}

/* ---- the window ----------------------------------------------------------- */

static LRESULT CALLBACK
chrome_gdi_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    struct ChromeGdi* s = &g_chrome_gdi;

    switch( msg )
    {
    case WM_COMMAND:
    {
        int const id = LOWORD(wp);
        int const notify = HIWORD(wp);

        /* A tab button: reported, not acted on. The MODEL decides which tab is
         * up -- it answers with a PANEL_TAB command -- and switching here as
         * well would make the window and the model disagree the moment
         * anything else moved the tab. */
        if( id >= CHROME_GDI_ID_TAB_BASE && id < CHROME_GDI_ID_TAB_BASE + 16 )
        {
            struct ToriRSChromeIntent intent;
            memset(&intent, 0, sizeof(intent));
            intent.kind = TORIRS_CHROME_INTENT_TAB;
            intent.panel = s->tab_panel;
            intent.widget = s->tab_strip_widget;
            intent.value = id - CHROME_GDI_ID_TAB_BASE;
            ToriRSChromeMirror_PushIntent(&s->mirror, &intent);
            return 0;
        }

        if( id >= CHROME_GDI_ID_BASE && id < CHROME_GDI_ID_BASE + TORIDBG_MAX_WIDGETS )
        {
            int const handle = id - CHROME_GDI_ID_BASE;
            struct ToriRSChromeMirrorWidget* w =
                ToriRSChromeMirror_Widget(&s->mirror, handle);
            HWND control = (HWND)lp;

            if( !w )
                return 0;

            switch( w->kind )
            {
            case TORIDBG_W_CHECKBOX:
                if( notify == BN_CLICKED )
                    ToriRSChromeMirror_PushToggle(
                        &s->mirror, w->panel, handle,
                        SendMessageA(control, BM_GETCHECK, 0, 0) == BST_CHECKED);
                break;

            case TORIDBG_W_TEXTINPUT:
                /* EN_KILLFOCUS, not EN_CHANGE: an intent per keystroke would
                 * send the model a value for every half-typed state, and the
                 * chrome's own input commits the same way. */
                if( notify == EN_KILLFOCUS )
                {
                    char buf[TORIRS_CHROME_TEXT_MAX];
                    GetWindowTextA(control, buf, (int)sizeof(buf));
                    ToriRSChromeMirror_PushText(&s->mirror, w->panel, handle, buf);
                }
                break;

            case TORIDBG_W_DROPDOWN:
                if( notify == CBN_SELCHANGE )
                {
                    struct ToriRSChromeIntent intent;
                    int const sel = (int)SendMessageA(control, CB_GETCURSEL, 0, 0);
                    memset(&intent, 0, sizeof(intent));
                    intent.kind = TORIRS_CHROME_INTENT_PICK;
                    intent.panel = w->panel;
                    intent.widget = handle;
                    intent.value = sel;
                    SendMessageA(control, CB_GETLBTEXT, (WPARAM)sel, (LPARAM)intent.text);
                    ToriRSChromeMirror_PushIntent(&s->mirror, &intent);
                }
                break;

            default:
                if( notify == BN_CLICKED )
                    ToriRSChromeMirror_PushActivate(&s->mirror, w->panel, handle);
                break;
            }
        }
        return 0;
    }

    case WM_SIZE:
        chrome_gdi_layout(s);
        return 0;

    case WM_CLOSE:
    {
        /* Reported, and the window stays up until the model says otherwise --
         * the same rule the tab buttons follow. Destroying it here would leave
         * the model presenting into a window that no longer exists. */
        struct ToriRSChromeIntent intent;
        memset(&intent, 0, sizeof(intent));
        intent.kind = TORIRS_CHROME_INTENT_CLOSE;
        intent.panel = s->tab_panel;
        intent.widget = -1;
        ToriRSChromeMirror_PushIntent(&s->mirror, &intent);
        return 0;
    }

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        /* The dialog face, so STATIC labels do not sit on a white patch over a
         * grey window -- the default for a STATIC on a non-dialog parent. */
        SetBkMode((HDC)wp, TRANSPARENT);
        return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);

    default:
        break;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static int
chrome_gdi_begin(void* user)
{
    struct ChromeGdi* s = user;
    WNDCLASSA wc;
    NONCLIENTMETRICSA ncm;

    assert(s);
    if( s->hwnd )
        return 1;

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = chrome_gdi_wndproc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = CHROME_GDI_WNDCLASS;
    /* A duplicate class is not an error: begin() can run again after the model
     * closed the window, and the class outlives the window. */
    if( !RegisterClassA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS )
        return 0;

    s->hwnd = CreateWindowExA(
        WS_EX_TOOLWINDOW,
        CHROME_GDI_WNDCLASS,
        "Plugins",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        CHROME_GDI_W,
        CHROME_GDI_H,
        s->owner,
        NULL,
        GetModuleHandleA(NULL),
        NULL);
    if( !s->hwnd )
        return 0;

    /* The shell's own UI font, so the window reads as part of the system
     * rather than as the 1990s SYSTEM_FONT a control defaults to. */
    memset(&ncm, 0, sizeof(ncm));
    ncm.cbSize = sizeof(ncm);
    if( SystemParametersInfoA(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0) )
        s->font = CreateFontIndirectA(&ncm.lfMessageFont);

    ToriRSChromeMirror_Init(&s->mirror);
    s->tab_panel = -1;
    s->tab_strip_widget = -1;
    s->tab_count = 0;
    s->open = 1;
    ShowWindow(s->hwnd, SW_SHOWNOACTIVATE);
    return 1;
}

static void
chrome_gdi_end(void* user)
{
    struct ChromeGdi* s = user;

    assert(s);
    if( !s->open )
        return;
    /* The children go with the parent; Windows destroys them for us, which is
     * why nothing here walks the mirror to destroy controls one at a time. */
    if( s->hwnd )
        DestroyWindow(s->hwnd);
    if( s->font )
        DeleteObject(s->font);
    s->hwnd = NULL;
    s->font = NULL;
    s->open = 0;
    s->tab_count = 0;
    memset(s->tab_buttons, 0, sizeof(s->tab_buttons));
}

/* ---- creating controls ---------------------------------------------------- */

static HWND
chrome_gdi_child(
    struct ChromeGdi* s, char const* cls, DWORD style, char const* text, int id)
{
    HWND h = CreateWindowExA(
        0,
        cls,
        text ? text : "",
        WS_CHILD | style,
        0,
        0,
        10,
        10,
        s->hwnd,
        (HMENU)(INT_PTR)id,
        GetModuleHandleA(NULL),
        NULL);
    if( h && s->font )
        SendMessageA(h, WM_SETFONT, (WPARAM)s->font, TRUE);
    return h;
}

static void
chrome_gdi_add(struct ChromeGdi* s, struct ToriRSChromeCmd const* cmd)
{
    struct ToriRSChromeMirrorWidget* w =
        ToriRSChromeMirror_Widget(&s->mirror, cmd->widget);
    int const id = CHROME_GDI_ID_BASE + cmd->widget;
    HWND control = NULL;

    if( !w )
        return;

    switch( cmd->value )
    {
    case TORIDBG_W_CHECKBOX:
        control = chrome_gdi_child(s, "BUTTON", BS_AUTOCHECKBOX, cmd->label, id);
        break;

    case TORIDBG_W_TEXTINPUT:
        /* The label is a STATIC of its own, placed by the layout's label
         * column. It carries no id: nothing routes to it, and giving it one
         * would put a second control in the handle's id slot. */
        chrome_gdi_child(s, "STATIC", SS_LEFT, cmd->label, -1);
        control = chrome_gdi_child(s, "EDIT", WS_BORDER | ES_AUTOHSCROLL, cmd->text, id);
        break;

    case TORIDBG_W_DROPDOWN:
        chrome_gdi_child(s, "STATIC", SS_LEFT, cmd->label, -1);
        control = chrome_gdi_child(
            s, "COMBOBOX", CBS_DROPDOWNLIST | WS_VSCROLL, NULL, id);
        break;

    case TORIDBG_W_BUTTON:
    case TORIDBG_W_MENUITEM:
        control = chrome_gdi_child(
            s, "BUTTON", BS_PUSHBUTTON, cmd->text[0] ? cmd->text : cmd->label, id);
        break;

    case TORIDBG_W_SEPARATOR:
        control = chrome_gdi_child(s, "STATIC", SS_ETCHEDHORZ, NULL, -1);
        break;

    case TORIDBG_W_TABSTRIP:
        /* No control of its own: the strip is a row of buttons the OPTION
         * commands below build, and this only records who owns it. */
        s->tab_panel = cmd->panel;
        s->tab_strip_widget = cmd->widget;
        return;

    case TORIDBG_W_LABEL:
    default:
        control = chrome_gdi_child(
            s, "STATIC", SS_LEFT, cmd->text[0] ? cmd->text : cmd->label, -1);
        break;
    }

    w->native = (intptr_t)control;
}

static void
chrome_gdi_rebuild_tabs(struct ChromeGdi* s)
{
    for( int i = 0; i < 16; i++ )
    {
        if( s->tab_buttons[i] )
            DestroyWindow(s->tab_buttons[i]);
        s->tab_buttons[i] = NULL;
    }
    if( s->tab_count < 2 )
        return;
    for( int i = 0; i < s->tab_count && i < 16; i++ )
        s->tab_buttons[i] = chrome_gdi_child(
            s, "BUTTON", BS_PUSHBUTTON, s->tabs[i], CHROME_GDI_ID_TAB_BASE + i);
}

static void
chrome_gdi_apply(void* user, struct ToriRSChromeCmd const* cmd)
{
    struct ChromeGdi* s = user;
    struct ToriRSChromeMirrorWidget* w;
    int shape;

    assert(s);
    assert(cmd);
    if( !s->open )
        return;

    /*
     * Destroy BEFORE the mirror folds the command in, not after.
     *
     * The mirror clears a slot on REMOVE and clears a panel's whole set on
     * CLOSE -- deliberately, so a recycled handle cannot inherit a stale
     * native id. That means the HWND is unreachable the instant Apply returns,
     * so taking it afterwards leaks the window and leaves a dead control
     * sitting in the tool window with nothing behind it.
     */
    if( cmd->kind == TORIRS_CHROME_CMD_WIDGET_REMOVE )
    {
        struct ToriRSChromeMirrorWidget* gone =
            ToriRSChromeMirror_Widget(&s->mirror, cmd->widget);
        if( gone && gone->native )
            DestroyWindow((HWND)gone->native);
    }
    else if( cmd->kind == TORIRS_CHROME_CMD_PANEL_CLOSE )
    {
        for( int i = 0; i < TORIDBG_MAX_WIDGETS; i++ )
        {
            struct ToriRSChromeMirrorWidget* gone =
                ToriRSChromeMirror_Widget(&s->mirror, i);
            if( gone && gone->panel == cmd->panel && gone->native )
                DestroyWindow((HWND)gone->native);
        }
    }

    shape = ToriRSChromeMirror_Apply(&s->mirror, cmd);

    switch( cmd->kind )
    {
    case TORIRS_CHROME_CMD_SYNC_END:
        /* One relayout per frame at most, and only when something moved. The
         * flag is what keeps a frame that only changed a label from
         * repositioning every control in the window. */
        if( s->mirror.intent_overflow )
        {
            fprintf(stderr, "chrome: gdi intent queue overflowed; input was dropped\n");
            s->mirror.intent_overflow = 0;
        }
        return;

    case TORIRS_CHROME_CMD_PANEL_TITLE:
        SetWindowTextA(s->hwnd, cmd->text);
        return;

    case TORIRS_CHROME_CMD_PANEL_CLOSE:
        /* The windows were destroyed above, before the mirror forgot them. */
        chrome_gdi_layout(s);
        return;

    case TORIRS_CHROME_CMD_WIDGET_ADD:
        chrome_gdi_add(s, cmd);
        chrome_gdi_layout(s);
        return;

    case TORIRS_CHROME_CMD_WIDGET_REMOVE:
        /* Destroyed above, for the same reason. The row it vacated has to be
         * closed up or every control below it stays a row too low. */
        chrome_gdi_layout(s);
        return;

    default:
        break;
    }

    w = ToriRSChromeMirror_Widget(&s->mirror, cmd->widget);
    if( !w || !w->native )
    {
        /* A strip's options are its tab titles, and it has no control. */
        if( cmd->widget == s->tab_strip_widget )
        {
            if( cmd->kind == TORIRS_CHROME_CMD_WIDGET_OPTIONS )
            {
                s->tab_count = cmd->value < 16 ? cmd->value : 16;
                memset(s->tabs, 0, sizeof(s->tabs));
            }
            else if( cmd->kind == TORIRS_CHROME_CMD_WIDGET_OPTION && cmd->value < 16 )
            {
                snprintf(
                    s->tabs[cmd->value], sizeof(s->tabs[0]), "%s", cmd->text);
                chrome_gdi_rebuild_tabs(s);
                chrome_gdi_layout(s);
            }
        }
        if( shape )
            chrome_gdi_layout(s);
        return;
    }

    switch( cmd->kind )
    {
    case TORIRS_CHROME_CMD_WIDGET_TEXT:
        /* Never while it has focus: the model is echoing a value the user is
         * still editing, and writing it back would move the caret and undo
         * whatever they typed since the last commit. */
        if( w->kind != TORIDBG_W_TEXTINPUT || GetFocus() != (HWND)w->native )
            SetWindowTextA((HWND)w->native, cmd->text);
        break;

    case TORIRS_CHROME_CMD_WIDGET_LABEL:
        if( w->kind == TORIDBG_W_CHECKBOX )
            SetWindowTextA((HWND)w->native, cmd->label);
        break;

    case TORIRS_CHROME_CMD_WIDGET_CHECKED:
        SendMessageA(
            (HWND)w->native, BM_SETCHECK, cmd->value ? BST_CHECKED : BST_UNCHECKED, 0);
        break;

    case TORIRS_CHROME_CMD_WIDGET_OPTIONS:
        SendMessageA((HWND)w->native, CB_RESETCONTENT, 0, 0);
        break;

    case TORIRS_CHROME_CMD_WIDGET_OPTION:
        SendMessageA((HWND)w->native, CB_ADDSTRING, 0, (LPARAM)cmd->text);
        break;

    case TORIRS_CHROME_CMD_WIDGET_SELECTED:
        SendMessageA((HWND)w->native, CB_SETCURSEL, (WPARAM)cmd->value, 0);
        break;

    default:
        break;
    }

    if( shape )
        chrome_gdi_layout(s);
}

static int
chrome_gdi_poll(void* user, struct ToriRSChromeIntent* out, int max)
{
    struct ChromeGdi* s = user;

    assert(s);
    assert(out);
    if( !s->open )
        return 0;
    /* The messages themselves are pumped by the game's own PeekMessage loop in
     * platform_win32gdi.c -- one queue per thread, and both windows are on
     * this one. Nothing extra runs here; the wndproc has already filled the
     * mirror's queue by the time anything asks. */
    return ToriRSChromeMirror_Poll(&s->mirror, out, max);
}

struct ToriRSChromeExec
ToriRSChromeExec_Gdi(void* platform)
{
    struct ToriRSChromeExec exec;

    memset(&exec, 0, sizeof(exec));
    memset(&g_chrome_gdi, 0, sizeof(g_chrome_gdi));
    /* The shell hands every executor the same platform handle; this one asks
     * it for the game's HWND, which is all an owned tool window needs. Asking
     * here rather than making the shell branch per executor is what keeps the
     * chooser's call site identical on every lane. */
    g_chrome_gdi.owner = platform ? (HWND)PlatformSDL2_NativeWindowHandle(platform) : NULL;

    exec.user = &g_chrome_gdi;
    exec.begin = chrome_gdi_begin;
    exec.apply = chrome_gdi_apply;
    exec.end = chrome_gdi_end;
    exec.poll = chrome_gdi_poll;
    return exec;
}
