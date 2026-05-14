#include "../../libtorirs.h"

#include <assert.h>
#include <emscripten.h>
#include <stdio.h>

static int i = 0;
static enum BrowserMainLoopState state = BROWSER_MAIN_LOOP_STATE_BEGIN;

enum BrowserMainLoopState
{
    BROWSER_MAIN_LOOP_STATE_BEGIN,
    BROWSER_MAIN_LOOP_STATE_WAITING_FOR_JS,
    BROWSER_MAIN_LOOP_STATE_END
};

EMSCRIPTEN_KEEPALIVE
void
ToriPlatformEmscripten_JSHost_BrowserMainUnlock(void)
{
    assert(state == BROWSER_MAIN_LOOP_STATE_WAITING_FOR_JS);
    printf("Browser main loop unlocked\n");
    state = BROWSER_MAIN_LOOP_STATE_END;
}

void
browser_main_loop(void)
{
    switch( state )
    {
    case BROWSER_MAIN_LOOP_STATE_BEGIN:
        state = BROWSER_MAIN_LOOP_STATE_WAITING_FOR_JS;
        break;
    case BROWSER_MAIN_LOOP_STATE_WAITING_FOR_JS:
        // Do nothing
        break;
    case BROWSER_MAIN_LOOP_STATE_END:
        printf("Browser main loop ended\n");
        state = BROWSER_MAIN_LOOP_STATE_BEGIN;
        return;
    }
}

int
main(
    int argc,
    char* argv[])
{
    (void)argc;
    (void)argv;

    struct LibToriRS_Instance* instance = LibToriRS_InstanceNew();
    if( !instance )
    {
        printf("Failed to create instance\n");
        return 1;
    }

    emscripten_set_main_loop(browser_main_loop, 0, 1);

    return 0;
}