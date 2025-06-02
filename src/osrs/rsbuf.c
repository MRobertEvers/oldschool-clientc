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

int
rsbuf_read_unsigned_int_smart_short_compat(struct RSBuffer* buffer)
{
    // int var1 = 0;

    // int var2;
    // for (var2 = this.readUnsignedShortSmart(); var2 == 32767; var2 =
    // this.readUnsignedShortSmart())
    // {
    // 	var1 += 32767;
    // }

    // var1 += var2;
    // return var1;

    int var1 = 0;
    int var2;
    for( var2 = rsbuf_read_unsigned_short_smart(buffer); var2 == 32767;
         var2 = rsbuf_read_unsigned_short_smart(buffer) )
    {
        var1 += 32767;
    }
    var1 += var2;
    return var1;
}

int
rsbuf_read_unsigned_short_smart(struct RSBuffer* buffer)
{
    int peek = buffer->data[buffer->position] & 0xFF;
    return peek < 128 ? rsbuf_g1(buffer) : rsbuf_g2(buffer) - 0x8000;
}