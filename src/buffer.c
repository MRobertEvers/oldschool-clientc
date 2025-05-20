#include "buffer.h"

#include <assert.h>
int
read_8(struct Buffer* buffer)
{
    assert(buffer->position + 1 <= buffer->data_size);
    buffer->position += 1;
    return buffer->data[buffer->position - 1] & 0xFF;
}

int
read_24(struct Buffer* buffer)
{
    assert(buffer->position + 3 <= buffer->data_size);
    buffer->position += 3;
    return ((buffer->data[buffer->position - 3] & 0xFF) << 16) |
           ((buffer->data[buffer->position - 2] & 0xFF) << 8) |
           (buffer->data[buffer->position - 1] & 0xFF);
}

int
read_16(struct Buffer* buffer)
{
    assert(buffer->position + 2 <= buffer->data_size);
    buffer->position += 2;
    return ((buffer->data[buffer->position - 2] & 0xFF) << 8) |
           (buffer->data[buffer->position - 1] & 0xFF);
}

int
read_32(struct Buffer* buffer)
{
    assert(buffer->position + 4 <= buffer->data_size);
    buffer->position += 4;
    // print the 4 bytes at the current position

    return ((buffer->data[buffer->position - 4] & 0xFF) << 24) |
           ((buffer->data[buffer->position - 3] & 0xFF) << 16) |
           ((buffer->data[buffer->position - 2] & 0xFF) << 8) |
           (buffer->data[buffer->position - 1] & 0xFF);
}

int
readto(char* out, int out_size, int len, struct Buffer* buffer)
{
    assert(buffer->position + len <= buffer->data_size);
    int bytes_read = 0;
    while( len > 0 && buffer->position < buffer->data_size )
    {
        out[bytes_read] = buffer->data[buffer->position];
        len--;
        buffer->position++;
        bytes_read++;
    }

    return bytes_read;
}

void
bump(int count, struct Buffer* buffer)
{
    assert(buffer->position + count <= buffer->data_size);
    buffer->position += count;
}
