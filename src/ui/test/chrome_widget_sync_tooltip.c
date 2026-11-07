#include "chrome_widget_sync.h"

#include "ui/torirs_chrome_metrics.h"

#include <string.h>

static void cw_test_tooltip_chord(void);

/* TOOLTIP: hover text on any widget, shown after the delay by the host's
 * clock, drawn in the minimenu palette, sent as WIDGET_TOOLTIP. */
void
cw_test_tooltip(void)
{
    struct ToriRSChromeCmd const* cmd;
    int panel;
    int button;
    int tool;
    int n = 0;
    int found = 0;
    struct ToriRSChromePrim const* prims;
    struct ToriRSChromeWidget const* w;

    cw_reset();
    panel = ToriRSChrome_PanelAdd(&g_cw_ui, TORIRS_CHROME_PANEL_WINDOW, 10, 10, 300, "Panel");
    button = ToriRSChrome_Button(&g_cw_ui, panel, "Build");
    ToriRSChrome_SetTooltip(&g_cw_ui, button, "Pack the server scripts");
    tool = ToriRSChrome_IconButton(&g_cw_ui, panel, TORIRS_CHROME_SKIN_RUN, "Run the lane");
    cw_run();
    cmd = cw_find(TORIRS_CHROME_CMD_WIDGET_TOOLTIP, button);
    CW_CHECK(cmd && strcmp(cmd->label, "Pack the server scripts") == 0,
        "WIDGET_TOOLTIP carries the hover text");
    CW_CHECK(cw_find(TORIRS_CHROME_CMD_WIDGET_TOOLTIP, tool) == NULL,
        "a widget with no tooltip of its own is not sent one");

    w = &g_cw_ui.widgets[button];
    ToriRSChrome_Tick(&g_cw_ui, 1000);
    ToriRSChrome_MouseMove(&g_cw_ui, w->x + 5, w->y + 5);
    ToriRSChrome_Tick(&g_cw_ui, 1010);
    CW_CHECK(ToriRSChrome_TooltipWidget(&g_cw_ui) == -1, "not before the delay");
    ToriRSChrome_Tick(&g_cw_ui, 1010 + TORIRS_CHROME_TOOLTIP_DELAY_MS);
    CW_CHECK(ToriRSChrome_TooltipWidget(&g_cw_ui) == button, "shown after the delay");
    (void)ToriRSChrome_Build(&g_cw_ui);
    prims = ToriRSChrome_Prims(&g_cw_ui, &n);
    for( int i = 0; i < n; i++ )
        if( prims[i].kind == TORIRS_CHROME_PRIM_TEXT && prims[i].text &&
            strcmp(prims[i].text, "Pack the server scripts") == 0 && i > n - 4 )
            found = 1;
    CW_CHECK(found, "drawn last, over everything");

    ToriRSChrome_MouseMove(&g_cw_ui, 700, 500);
    CW_CHECK(ToriRSChrome_TooltipWidget(&g_cw_ui) == -1, "gone the moment the pointer leaves");

    /* An icon button falls back to its label. */
    (void)ToriRSChrome_Build(&g_cw_ui);
    w = &g_cw_ui.widgets[tool];
    ToriRSChrome_MouseMove(&g_cw_ui, w->x + 5, w->y + 5);
    ToriRSChrome_Tick(&g_cw_ui, 5000);
    ToriRSChrome_Tick(&g_cw_ui, 5000 + TORIRS_CHROME_TOOLTIP_DELAY_MS);
    CW_CHECK(ToriRSChrome_TooltipWidget(&g_cw_ui) == tool, "an icon button's label is its tooltip");

    cw_run();
    ToriRSChrome_SetTooltip(&g_cw_ui, button, "Pack the server scripts");
    CW_CHECK(cw_run() == 0, "the same tooltip again records nothing");
    ToriRSChrome_SetTooltip(&g_cw_ui, button, NULL);
    cw_run();
    cmd = cw_find(TORIRS_CHROME_CMD_WIDGET_TOOLTIP, button);
    CW_CHECK(cmd && cmd->label[0] == '\0', "clearing it is sent as an empty tooltip");

    cw_test_tooltip_chord();
}

/* The tooltip's chord (the workbench design's section 15: every tooltip ends
 * with the chord), "text  [chord]" with the chord as a keycap. */
static int
cw_show_tooltip(int widget, int64_t at)
{
    struct ToriRSChromeWidget const* w = &g_cw_ui.widgets[widget];

    ToriRSChrome_MouseMove(&g_cw_ui, 700, 500);
    ToriRSChrome_Tick(&g_cw_ui, at);
    (void)ToriRSChrome_Build(&g_cw_ui);
    ToriRSChrome_MouseMove(&g_cw_ui, w->x + 5, w->y + 5);
    ToriRSChrome_Tick(&g_cw_ui, at + 10);
    ToriRSChrome_Tick(&g_cw_ui, at + 10 + TORIRS_CHROME_TOOLTIP_DELAY_MS);
    (void)ToriRSChrome_Build(&g_cw_ui);
    return ToriRSChrome_TooltipWidget(&g_cw_ui) == widget;
}

static struct ToriRSChromePrim const*
cw_prim_text(char const* text)
{
    int n = 0;
    struct ToriRSChromePrim const* prims = ToriRSChrome_Prims(&g_cw_ui, &n);

    for( int i = 0; i < n; i++ )
        if( prims[i].kind == TORIRS_CHROME_PRIM_TEXT && prims[i].text == text )
            return &prims[i];
    return NULL;
}

static void
cw_test_tooltip_chord(void)
{
    struct ToriRSChromeCmd const* cmd;
    struct ToriRSChromePrim const* tip;
    struct ToriRSChromePrim const* key;
    struct ToriRSChromeWidget const* w;
    int panel;
    int check;
    int button;
    int tool;
    int plain_w;
    int const tip_w = ToriRSChrome_MeasureText(TORIRS_CHROME_FONT_SMALL, 1, "Show the grid");

    cw_reset();
    panel = ToriRSChrome_PanelAdd(&g_cw_ui, TORIRS_CHROME_PANEL_WINDOW, 10, 10, 300, "Panel");
    check = ToriRSChrome_Checkbox(&g_cw_ui, panel, "Grid", 0);
    ToriRSChrome_SetTooltip(&g_cw_ui, check, "Show the grid");
    button = ToriRSChrome_Button(&g_cw_ui, panel, "Build");
    ToriRSChrome_SetTooltip(&g_cw_ui, button, "Pack the server scripts");
    tool = ToriRSChrome_IconButton(&g_cw_ui, panel, TORIRS_CHROME_SKIN_RUN, "Run the lane");
    cw_run();

    /* Without a chord the box is the text and its pad, as before. */
    CW_CHECK(cw_show_tooltip(check, 1000), "the checkbox's tooltip is up");
    plain_w = g_cw_ui.tooltip_rect.w;
    CW_CHECK(plain_w == tip_w + 2 * 3, "a tooltip with no chord is its text and its pad");

    /* The setter and the command: WIDGET_TOOLTIP's text. */
    cw_run();
    ToriRSChrome_SetTooltipChord(&g_cw_ui, check, "Alt+G");
    CW_CHECK(strcmp(g_cw_ui.widgets[check].tooltip_chord, "Alt+G") == 0, "the setter copies it");
    cw_run();
    cmd = cw_find(TORIRS_CHROME_CMD_WIDGET_TOOLTIP, check);
    CW_CHECK(cmd && strcmp(cmd->label, "Show the grid") == 0 && strcmp(cmd->text, "Alt+G") == 0,
        "WIDGET_TOOLTIP carries the text and the chord");
    ToriRSChrome_SetTooltipChord(&g_cw_ui, check, "Alt+G");
    CW_CHECK(cw_run() == 0, "the same chord again records nothing");

    /* What Build draws: the tooltip widens by the gap and a keycap holding
     * the chord, borrowed, after the text. */
    CW_CHECK(cw_show_tooltip(check, 3000), "still shown");
    w = &g_cw_ui.widgets[check];
    tip = cw_prim_text(w->tooltip);
    key = cw_prim_text(w->tooltip_chord);
    CW_CHECK(tip && key, "the text and the chord are both drawn");
    CW_CHECK(key && key->font_slot == TORIRS_CHROME_FONT_SMALL &&
                 key->color == g_cw_ui.theme.menu_hover_text,
        "the chord in the Small face and the tooltip's yellow");
    CW_CHECK(tip && key && key->x >= tip->x + tip_w + TORIRS_CHROME_M_KEYCAP_GAP,
        "after the text, past the gap");
    CW_CHECK(g_cw_ui.tooltip_rect.w ==
                 plain_w + TORIRS_CHROME_M_KEYCAP_GAP +
                     ToriRSChrome_MeasureText(TORIRS_CHROME_FONT_SMALL, 1, "Alt+G") +
                     2 * TORIRS_CHROME_M_KEYCAP_PAD_X,
        "the box widens by exactly the gap and the key");
    CW_CHECK(key && key->x + ToriRSChrome_MeasureText(TORIRS_CHROME_FONT_SMALL, 1, "Alt+G") <=
                        g_cw_ui.tooltip_rect.x + g_cw_ui.tooltip_rect.w - 3,
        "and the key fits inside it");

    /* A BUTTON or ICONBUTTON with no tooltip chord of its own ends its
     * tooltip with the chord it wears; the command does not restate it. */
    cw_run();
    ToriRSChrome_ButtonSetChord(&g_cw_ui, button, "Ctrl+B");
    ToriRSChrome_IconButtonSetChord(&g_cw_ui, tool, "F5");
    cw_run();
    cmd = cw_find(TORIRS_CHROME_CMD_WIDGET_TOOLTIP, button);
    CW_CHECK(cmd == NULL, "a worn chord is the WIDGET_CHORD's, not the tooltip's");
    CW_CHECK(cw_show_tooltip(button, 6000), "the button's tooltip is up");
    CW_CHECK(cw_prim_text(g_cw_ui.widgets[button].tooltip) != NULL, "its text");
    {
        int n = 0;
        int keys = 0;
        struct ToriRSChromePrim const* prims = ToriRSChrome_Prims(&g_cw_ui, &n);
        for( int i = 0; i < n; i++ )
            if( prims[i].kind == TORIRS_CHROME_PRIM_TEXT &&
                prims[i].text == g_cw_ui.widgets[button].chord )
                keys++;
        CW_CHECK(keys == 2, "the keycap on the plate and the one ending the tooltip");
    }
    CW_CHECK(cw_show_tooltip(tool, 9000), "the icon button's label tooltip is up");
    CW_CHECK(cw_prim_text(g_cw_ui.widgets[tool].label) != NULL &&
                 g_cw_ui.tooltip_rect.w >
                     ToriRSChrome_MeasureText(TORIRS_CHROME_FONT_SMALL, 1, "Run the lane") + 6 +
                         TORIRS_CHROME_M_KEYCAP_GAP,
        "an icon button's label tooltip ends with its badge's chord");
    ToriRSChrome_SetTooltipChord(&g_cw_ui, tool, "Ctrl+R");
    CW_CHECK(cw_show_tooltip(tool, 12000) &&
                 cw_prim_text(g_cw_ui.widgets[tool].tooltip_chord) != NULL,
        "a tooltip chord of its own wins over the badge's");

    /* Clearing: "" is sent as an empty chord, and the box shrinks back. */
    cw_run();
    ToriRSChrome_SetTooltipChord(&g_cw_ui, check, "");
    cw_run();
    cmd = cw_find(TORIRS_CHROME_CMD_WIDGET_TOOLTIP, check);
    CW_CHECK(cmd && cmd->text[0] == '\0' && strcmp(cmd->label, "Show the grid") == 0,
        "clearing the chord restates the tooltip with none");
    CW_CHECK(cw_show_tooltip(check, 15000) && g_cw_ui.tooltip_rect.w == plain_w,
        "and the box is its text again");
}
