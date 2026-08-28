#include "torirs_chrome_exec_kind.h"

#include <string.h>

static char const* const CHROME_EXEC_NAME[TORIRS_CHROME_EXEC_COUNT] = {
    [TORIRS_CHROME_EXEC_BUFFER] = "buffer",
    [TORIRS_CHROME_EXEC_SDL] = "sdl",
    [TORIRS_CHROME_EXEC_WEB] = "web",
    [TORIRS_CHROME_EXEC_GDI] = "gdi",
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
