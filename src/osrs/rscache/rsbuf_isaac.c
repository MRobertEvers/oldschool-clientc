#include "rsbuf_isaac.h"

#include "osrs/isaac.h"

void
rsbuf_p1isaac(
    struct RSBuffer* buf,
    struct Isaac* isaac_out,
    int opcode)
{
    int next = isaac_next(isaac_out);
    rsbuf_p1(buf, (opcode + next) & 0xff);
}
