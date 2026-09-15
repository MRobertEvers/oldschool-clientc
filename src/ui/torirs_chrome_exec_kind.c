#include "torirs_chrome_exec_kind.h"

#include <string.h>

static char const* const CHROME_EXEC_NAME[TORIRS_CHROME_EXEC_COUNT] = {
    [TORIRS_CHROME_EXEC_BUFFER] = "buffer",
    [TORIRS_CHROME_EXEC_WEB] = "web",
    [TORIRS_CHROME_EXEC_BROWSER] = "browser",
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
    /* BUFFER is deliberately omitted: it is the safe result when no supported
     * web executor is built or starts, never a public configuration choice. */
    for( int i = TORIRS_CHROME_EXEC_WEB; i < TORIRS_CHROME_EXEC_COUNT; i++ )
        if( CHROME_EXEC_NAME[i] && strcmp(CHROME_EXEC_NAME[i], name) == 0 )
            return i;
    return -1;
}
