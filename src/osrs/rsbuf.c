#include "rsbuf.h"

void
rsbuf_init(struct RSBuffer* buffer, uint8_t* data, int size)
{
    buffer->data = data;
    buffer->size = size;
    buffer->position = 0;
}

int
rsbuf_g1(struct RSBuffer* buffer)
{
    return buffer->data[buffer->position++] & 0xff;
}

// signed
char
rsbuf_g1b(struct RSBuffer* buffer)
{
    return buffer->data[buffer->position++];
}

int
rsbuf_g2(struct RSBuffer* buffer)
{
    buffer->position += 2;
    return (buffer->data[buffer->position - 2] & 0xff) << 8 |
           (buffer->data[buffer->position - 1] & 0xff);
}

// signed
int
rsbuf_g2b(struct RSBuffer* buffer)
{
    buffer->position += 2;
    int value = (buffer->data[buffer->position - 2] & 0xff) << 8 |
                (buffer->data[buffer->position - 1] & 0xff);

    if( value > 32767 )
        value -= 65536;
    return value;
}

int
rsbuf_g3(struct RSBuffer* buffer)
{
    buffer->position += 3;
    return (buffer->data[buffer->position - 3] & 0xff) << 16 |
           (buffer->data[buffer->position - 2] & 0xff) << 8 |
           (buffer->data[buffer->position - 1] & 0xff);
}

int
rsbuf_g4(struct RSBuffer* buffer)
{
    buffer->position += 4;
    return (buffer->data[buffer->position - 4] & 0xff) << 24 |
           (buffer->data[buffer->position - 3] & 0xff) << 16 |
           (buffer->data[buffer->position - 2] & 0xff) << 8 |
           (buffer->data[buffer->position - 1] & 0xff);
}

int64_t
rsbuf_g8(struct RSBuffer* buffer)
{
    int64_t high = (int64_t)rsbuf_g4(buffer) & 0xffffffffLL;
    int64_t low = (int64_t)rsbuf_g4(buffer) & 0xffffffffLL;
    return (high << 32) | low;
}
