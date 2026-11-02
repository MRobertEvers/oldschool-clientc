#include "chrome_widget_sync.h"

/* MODAL: a panel that takes the pointer and keyboard until it closes, drawn
 * last over a veil, centred; PANEL_MODE tells an executor; Escape and CLOSE
 * dismiss it. */
void
cw_test_modal(void)
{
    struct ToriRSChromeCmd const* cmd;
    int behind;
    int field;
    int modal;
    int save;
    int n = 0;
    struct ToriRSChromePrim const* prims;
    struct ToriRSChromeRect r;

    cw_reset();
    behind = ToriRSChrome_PanelAdd(&g_cw_ui, TORIRS_CHROME_PANEL_WINDOW, 10, 10, 300, "Inspector");
    field = ToriRSChrome_TextInput(&g_cw_ui, behind, "Height", "12");
    (void)ToriRSChrome_Build(&g_cw_ui);
    (void)ToriRSChrome_FocusWidget(&g_cw_ui, field, 2);

    modal = ToriRSChrome_ModalAdd(&g_cw_ui, "Unsaved edits", 240);
    save = ToriRSChrome_Button(&g_cw_ui, modal, "Save");
    (void)ToriRSChrome_Button(&g_cw_ui, modal, "Discard");
    CW_CHECK(modal >= 0 && ToriRSChrome_ModalPanel(&g_cw_ui) == modal, "the dialog is the modal");
    CW_CHECK(g_cw_ui.focus == -1, "the field behind the dialog lost the keyboard");
    cw_run();
    cmd = cw_find(TORIRS_CHROME_CMD_PANEL_MODE, -1);
    CW_CHECK(cmd && cmd->panel == modal && (cmd->value & TORIRS_CHROME_PANEL_MODE_MODAL),
        "PANEL_MODE tells the executor it is modal");
    CW_CHECK(cw_count(TORIRS_CHROME_CMD_PANEL_MODE) == 1, "and only the modal panel is told a mode");

    r = ToriRSChrome_PanelRect(&g_cw_ui, modal);
    CW_CHECK(r.w == 240 && r.x == (800 - 240) / 2 && r.y == (600 - r.h) / 2,
        "it is centred on the surface");
    prims = ToriRSChrome_Prims(&g_cw_ui, &n);
    {
        int veil = -1;
        int dialog = -1;
        for( int i = 0; i < n; i++ )
        {
            if( veil < 0 && prims[i].kind == TORIRS_CHROME_PRIM_RECT && prims[i].w == 800 &&
                prims[i].h == 600 && prims[i].trans > 0 )
                veil = i;
            if( prims[i].kind == TORIRS_CHROME_PRIM_RECT && prims[i].x == r.x && prims[i].y == r.y &&
                prims[i].w == r.w )
                dialog = i;
        }
        CW_CHECK(veil >= 0 && dialog > veil, "a veil over everything, and the dialog over it");
    }

    CW_CHECK(ToriRSChrome_WantsPointer(&g_cw_ui, 5, 5), "the whole surface is the dialog's");
    CW_CHECK(ToriRSChrome_HitTest(&g_cw_ui, 30, 40) == -1, "nothing behind it can be hit");
    cw_click(30, 40);
    CW_CHECK(g_cw_ui.focus == -1 && ToriRSChrome_TakeActivated(&g_cw_ui) == -1,
        "a click behind the dialog is consumed into nothing");
    {
        struct ToriRSChromeWidget const* b = &g_cw_ui.widgets[save];
        cw_click(b->x + 10, b->y + 5);
        CW_CHECK(ToriRSChrome_TakeActivated(&g_cw_ui) == save, "the dialog's own buttons work");
    }

    /* Escape closes it; the veil goes and the panel behind is reachable. */
    CW_CHECK(ToriRSChrome_KeyEdit(&g_cw_ui, TORIRS_CHROME_KEY_ESCAPE) == 1 &&
                 ToriRSChrome_ModalPanel(&g_cw_ui) == -1,
        "Escape closes the dialog");
    (void)ToriRSChrome_Build(&g_cw_ui);
    CW_CHECK(ToriRSChrome_HitTest(&g_cw_ui, 30, 40) >= 0 || ToriRSChrome_HitTest(&g_cw_ui, 150, 40) >= 0 ||
                 ToriRSChrome_WantsPointer(&g_cw_ui, 30, 40),
        "the panel behind is reachable again");
    CW_CHECK(!ToriRSChrome_WantsPointer(&g_cw_ui, 790, 590), "and the rest of the surface is the world's");

    /* The intent path: shown again, then CLOSE through the seam. */
    ToriRSChrome_PanelSetVisible(&g_cw_ui, modal, 1);
    CW_CHECK(ToriRSChrome_ModalPanel(&g_cw_ui) == modal, "shown again, it is modal again");
    CW_CHECK(cw_intent(TORIRS_CHROME_INTENT_CLOSE, modal, -1, 0, 0, 0, NULL) == 1 &&
                 ToriRSChrome_ModalPanel(&g_cw_ui) == -1,
        "CLOSE dismisses it");
    cw_run();
    ToriRSChrome_PanelSetModal(&g_cw_ui, modal, 1);
    CW_CHECK(cw_run() == 0, "an equal modal flag records nothing");
}
