#include "chrome_widget_sync.h"

#include <string.h>

/* CODE: a window of source lines the host fills, a caret, highlight spans per
 * line; edits come back as events for the host's document, never as a
 * rewrite of text the chrome does not hold. */
void
cw_test_code(void)
{
    static char const* const k_lines[] = {
        "[opnpc1,hans]", "~chatnpc(\"Hello.\");", "if (%var = 1) {", "    mes(\"x\");", "}"
    };
    struct ToriRSChromeSpanInput spans[2];
    struct ToriRSChromeEvent ev;
    struct ToriRSChromeCmd const* cmd;
    int panel;
    int code;

    cw_reset();
    panel = ToriRSChrome_PanelAdd(&g_cw_ui, TORIRS_CHROME_PANEL_WINDOW, 10, 10, 400, "hans.rs2");
    code = ToriRSChrome_Code(&g_cw_ui, panel, NULL, 4, 5);
    CW_CHECK(code >= 0, "a code view of four visible lines over five");
    for( int i = 0; i < 4; i++ )
        ToriRSChrome_CodeSetLine(&g_cw_ui, code, i, k_lines[i]);
    spans[0].start = 1;
    spans[0].len = 5;
    spans[0].color = 0xff981fu;
    spans[1].start = 7;
    spans[1].len = 4;
    spans[1].color = 0x00ff00u;
    CW_CHECK(ToriRSChrome_CodeSetLineSpans(&g_cw_ui, code, 0, spans, 2), "two spans on line 0");
    cw_run();
    cmd = cw_find(TORIRS_CHROME_CMD_WIDGET_ADD, code);
    CW_CHECK(cmd && cmd->value == TORIRS_CHROME_W_CODE && cmd->h == 4, "ADD carries the window");
    CW_CHECK(cw_count(TORIRS_CHROME_CMD_WIDGET_ROW) == 4, "the four visible lines are sent");
    CW_CHECK(cw_count(TORIRS_CHROME_CMD_WIDGET_SPAN) == 2, "with line 0's two spans");
    cmd = cw_find(TORIRS_CHROME_CMD_WIDGET_SPAN, code);
    CW_CHECK(cmd && cmd->value == 0 && cmd->x == 1 && cmd->y == 5 && cmd->color == 0xff981fu,
        "a span names line, start, length and colour");
    CW_CHECK(ToriRSChrome_CodeSetLineSpans(&g_cw_ui, code, 0, spans, 2) && cw_run() == 0,
        "the same spans again record nothing");

    /* Typing: the visible line follows and the host gets the edit. */
    (void)ToriRSChrome_FocusWidget(&g_cw_ui, code, 0);
    ToriRSChrome_CodeSetCaret(&g_cw_ui, code, 0, 1);
    CW_CHECK(ToriRSChrome_KeyChar(&g_cw_ui, 'X') == 1, "a key goes to the focused code view");
    CW_CHECK(ToriRSChrome_TakeEvent(&g_cw_ui, &ev) && ev.kind == TORIRS_CHROME_EVENT_CODE_REPLACE &&
                 ev.line == 0 && strcmp(ev.text, "[Xopnpc1,hans]") == 0,
        "the host is told the line's new text");
    CW_CHECK(g_cw_ui.widgets[code].caret == 2, "and the caret moved past it");
    ToriRSChrome_KeyEdit(&g_cw_ui, TORIRS_CHROME_KEY_ENTER);
    CW_CHECK(ToriRSChrome_TakeEvent(&g_cw_ui, &ev) && ev.kind == TORIRS_CHROME_EVENT_CODE_SPLIT &&
                 ev.line == 0 && ev.col == 2,
        "Enter is a SPLIT at the caret, for the host to apply");
    ToriRSChrome_CodeSetCaret(&g_cw_ui, code, 1, 0);
    ToriRSChrome_KeyEdit(&g_cw_ui, TORIRS_CHROME_KEY_BACKSPACE);
    CW_CHECK(ToriRSChrome_TakeEvent(&g_cw_ui, &ev) && ev.kind == TORIRS_CHROME_EVENT_CODE_JOIN &&
                 ev.line == 0,
        "Backspace at column 0 joins onto the line above");
    CW_CHECK(!ToriRSChrome_TakeEvent(&g_cw_ui, &ev), "nothing else was queued");

    /* The caret moving off the window scrolls it. */
    ToriRSChrome_CodeSetCaret(&g_cw_ui, code, 4, 0);
    CW_CHECK(g_cw_ui.widgets[code].imin == 1, "a caret below the window scrolls it");
    CW_CHECK(ToriRSChrome_VListNeedsRows(&g_cw_ui, code), "and the new line is asked for");
    cw_run();
    cmd = cw_find(TORIRS_CHROME_CMD_WIDGET_VALUE, code);
    CW_CHECK(cmd && cmd->value == 4 && cmd->x == 1 && cmd->y == 5 && cmd->h == 0,
        "VALUE carries caret line, first line, count and caret column");

    /* The intent seam: what the DOM editor sends. */
    CW_CHECK(cw_intent(TORIRS_CHROME_INTENT_CODE_EDIT, panel, code, 2, TORIRS_CHROME_EVENT_CODE_REPLACE,
                 3, "if (%var = 2) {") == 1,
        "a CODE_EDIT intent applies");
    CW_CHECK(ToriRSChrome_TakeEvent(&g_cw_ui, &ev) && ev.line == 2 && ev.col == 3 &&
                 strcmp(ev.text, "if (%var = 2) {") == 0,
        "and reaches the host as the same event");
    CW_CHECK(cw_intent(TORIRS_CHROME_INTENT_CODE_EDIT, panel, code, 2, 99, 0, "") == 0,
        "an unknown edit is refused");

    /* An event about a removed widget is dropped, not delivered. */
    ToriRSChrome_KeyChar(&g_cw_ui, 'y');
    ToriRSChrome_WidgetRemove(&g_cw_ui, code);
    CW_CHECK(!ToriRSChrome_TakeEvent(&g_cw_ui, &ev), "events outlive no widget");
}
