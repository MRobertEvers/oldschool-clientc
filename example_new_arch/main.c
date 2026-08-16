#include "io/asyncio.h"

#include <stdio.h>

int
main(
    int argc,
    char** argv)
{
    struct IO* io = IO_New();
    struct TaskQueue* queue = TaskQueue_New();

    int res;
    while( (res = TaskQueue_Run(queue, io)) == ASYNCIO_STAT_YIELD )
    {
    }

    if( res == ASYNCIO_STAT_ERROR )
    {
        fprintf(stderr, "Error: something went wrong\n");
        return 1;
    }

    IO_Free(io);
    return 0;
}