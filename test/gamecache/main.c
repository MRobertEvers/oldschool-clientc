#include "toriauxlib/core/toriauxlibcore.h"

#include <stdio.h>
#include <stdlib.h>

int
main(void)
{
    struct ToriAuxLibCore* core = ToriAuxLibCore_New();
    if( !core )
    {
        fprintf(stderr, "ToriAuxLibCore_New failed\n");
        return 1;
    }

    printf("ToriAuxLibCore_New ok\n");
    ToriAuxLibCore_Free(core);
    return 0;
}
