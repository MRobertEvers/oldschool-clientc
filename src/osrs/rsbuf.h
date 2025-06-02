#ifndef _RSBUF_H
#define _RSBUF_H

#include <stdint.h>

struct RSBuffer
{
    uint8_t* data;
    int size;
    int position;
};

void rsbuf_init(struct RSBuffer* buffer, uint8_t* data, int size);

int rsbuf_g1(struct RSBuffer* buffer);
// signed
char rsbuf_g1b(struct RSBuffer* buffer);
int rsbuf_g2(struct RSBuffer* buffer);
// signed
int rsbuf_g2b(struct RSBuffer* buffer);
int rsbuf_g3(struct RSBuffer* buffer);
int rsbuf_g4(struct RSBuffer* buffer);
int64_t rsbuf_g8(struct RSBuffer* buffer);

int rsbuf_read_unsigned_int_smart_short_compat(struct RSBuffer* buffer);
int rsbuf_read_unsigned_short_smart(struct RSBuffer* buffer);

#endif