#include "test_harness.h"

#include "torirs_chrome_shell.h"
#include "torirs_chrome_exec.h"

void
test_chrome_shell(void)
{
    struct ToriRSChromeShell shell;
    struct ToriRSChromeShellInput in;
    struct ToriRSChromeShellLayout out;
    uint32_t first;
    uint32_t second;

    printf("TEST: one plugin-chrome shell selection and attached layout\n");
    TEST_ASSERT(
        ToriRSChromeExec_KindFromName("web") == TORIRS_CHROME_EXEC_WEB &&
            ToriRSChromeExec_KindFromName("browser") == TORIRS_CHROME_EXEC_BROWSER,
        "only the two web-backed presenter names resolve");
    TEST_ASSERT(
        ToriRSChromeExec_KindFromName("platform") < 0 &&
            ToriRSChromeExec_KindFromName("buffer") < 0 &&
            ToriRSChromeExec_KindFromName("sdl") < 0 &&
            ToriRSChromeExec_KindFromName("gdi") < 0 &&
            ToriRSChromeExec_KindFromName("android") < 0,
        "legacy/native and internal fallback names are not selectable");
    ToriRSChromeShell_Init(&shell, 320);
    TEST_ASSERT(shell.active_plugin == TORIRS_CHROME_SHELL_PAGE_NONE,
        "the shell starts with no selected plugin");
    TEST_ASSERT(!shell.expanded, "the shell starts collapsed");

    memset(&in, 0, sizeof(in));
    in.window_w = 805;
    in.window_h = 503;
    in.target_game_w = 765;
    in.target_game_h = 503;
    in.min_game_w = 640;
    in.min_panel_w = 280;
    in.preferred_panel_w = 320;
    in.rail_w = 40;
    in.may_grow = 1;

    ToriRSChromeShell_Layout(&shell, &in, &out);
    TEST_ASSERT(out.mode == TORIRS_CHROME_SHELL_COLLAPSED, "no selection is collapsed");
    TEST_ASSERT(out.game.w == 765 && out.rail.x == 765,
        "a collapsed rail is outside the unchanged game presentation");

    first = ToriRSChromeShell_Select(&shell, 2);
    TEST_ASSERT(ToriRSChromeShell_Accepts(&shell, 2, first),
        "the selected plugin and generation are accepted");
    ToriRSChromeShell_Layout(&shell, &in, &out);
    TEST_ASSERT(out.request_window_w == 1125 && out.request_window_h == 503,
        "opening requests game plus rail plus panel, never relative repeated growth");
    TEST_ASSERT(out.mode == TORIRS_CHROME_SHELL_EXCLUSIVE,
        "a compact current window replaces the game instead of overlapping it");
    TEST_ASSERT(out.game.w == 0 && out.panel.x == 40 && out.panel.w == 765,
        "exclusive mode gives the page all content after its rail");

    in.window_w = out.request_window_w;
    ToriRSChromeShell_Layout(&shell, &in, &out);
    TEST_ASSERT(out.mode == TORIRS_CHROME_SHELL_SPLIT, "accepted growth becomes split");
    TEST_ASSERT(out.game.w == 765 && out.rail.x == 765 && out.panel.x == 805 &&
                    out.panel.w == 320,
        "attached-grow preserves the game and places rail and page beside it");
    TEST_ASSERT(out.request_window_w == 0, "accepted growth is not requested again");

    in.window_w = 1000;
    in.may_grow = 0;
    ToriRSChromeShell_Layout(&shell, &in, &out);
    TEST_ASSERT(out.mode == TORIRS_CHROME_SHELL_SPLIT && out.game.w == 640,
        "a constrained window uses attached-fit at the minimum game width");

    second = ToriRSChromeShell_Select(&shell, 7);
    TEST_ASSERT(second != first, "selecting another plugin advances the generation");
    TEST_ASSERT(!ToriRSChromeShell_Accepts(&shell, 2, first),
        "a late event from the prior page is rejected");
    TEST_ASSERT(ToriRSChromeShell_Accepts(&shell, 7, second),
        "only the most recently selected plugin is live");

    {
        uint32_t const collapsed = ToriRSChromeShell_Collapse(&shell);
        TEST_ASSERT(shell.active_plugin == 7 && !shell.expanded,
            "collapse remembers the most recently selected plugin");
        TEST_ASSERT(collapsed != second && !ToriRSChromeShell_Accepts(&shell, 7, second),
            "collapse stops rendering and rejects queued input");
        ToriRSChromeShell_Layout(&shell, &in, &out);
        TEST_ASSERT(out.mode == TORIRS_CHROME_SHELL_COLLAPSED && out.panel.w == 0,
            "collapsed layout keeps only the narrow rail");
        second = ToriRSChromeShell_Select(&shell, 7);
        TEST_ASSERT(shell.expanded && ToriRSChromeShell_Accepts(&shell, 7, second),
            "selecting the remembered icon expands that same page again");
    }

    ToriRSChromeShell_SetDetached(&shell, 1);
    ToriRSChromeShell_Layout(&shell, &in, &out);
    TEST_ASSERT(out.mode == TORIRS_CHROME_SHELL_DETACHED && shell.active_plugin == 7,
        "detach moves the same selection rather than making another shell");

    second = ToriRSChromeShell_Select(&shell, TORIRS_CHROME_SHELL_PAGE_MANAGE);
    TEST_ASSERT(
        ToriRSChromeShell_Accepts(
            &shell, TORIRS_CHROME_SHELL_PAGE_MANAGE, second),
        "the permanent Manage destination uses the same generation-fenced shell");

    ToriRSChromeShell_Select(&shell, TORIRS_CHROME_SHELL_PAGE_NONE);
    TEST_ASSERT(!shell.detached, "collapsing also ends optional detached presentation");
}
