#include "rsbuf_isaac.h"

#include "osrs/isaac.h"

#include <stdio.h>
void
rsbuf_p1isaac(
    struct RSBuffer* buf,
    struct Isaac* isaac_out,
    int opcode)
{
    int next = isaac_next(isaac_out);
    printf("[pkout] opcode: %d, next: %d\n", opcode, next);
    rsbuf_p1(buf, (opcode + next) & 0xff);
}

// {
// if_conf
// -265087435