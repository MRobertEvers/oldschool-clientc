#ifndef _RSBUF_H
#define _RSBUF_H

#include <stdbool.h>
#include <stdint.h>

struct Params
{
    int* keys;
    void** values;
    bool* is_string;
    int count;
    int capacity;
};

struct RSBuffer
{
    int8_t* data;
    int size;
    int position;
};

void rsbuf_init(struct RSBuffer* buffer, int8_t* data, int size);

int rsbuf_g1(struct RSBuffer* buffer);
// signed
int8_t rsbuf_g1b(struct RSBuffer* buffer);
int rsbuf_g2(struct RSBuffer* buffer);
// signed
int rsbuf_g2b(struct RSBuffer* buffer);
int rsbuf_g3(struct RSBuffer* buffer);
int rsbuf_g4(struct RSBuffer* buffer);
int64_t rsbuf_g8(struct RSBuffer* buffer);

int rsbuf_read_usmart(struct RSBuffer* buffer);
int rsbuf_read_big_smart(struct RSBuffer* buffer);
char* rsbuf_read_string(struct RSBuffer* buffer);

int rsbuf_read_unsigned_int_smart_short_compat(struct RSBuffer* buffer);
int rsbuf_read_short_smart(struct RSBuffer* buffer);
int rsbuf_read_unsigned_short_smart(struct RSBuffer* buffer);

void rsbuf_read_params(struct RSBuffer* buffer, struct Params* params);

int rsbuf_readto(struct RSBuffer* buffer, char* out, int out_size, int len);

#define g1(buffer) rsbuf_g1(buffer)
#define g1b(buffer) rsbuf_g1b(buffer)
#define g2(buffer) rsbuf_g2(buffer)
#define g2b(buffer) rsbuf_g2b(buffer)
#define g3(buffer) rsbuf_g3(buffer)
#define g4(buffer) rsbuf_g4(buffer)
#define g8(buffer) rsbuf_g8(buffer)
#define gusmart(buffer) rsbuf_read_usmart(buffer)
#define gbigsmart(buffer) rsbuf_read_big_smart(buffer)
#define gushortsmart(buffer) rsbuf_read_unsigned_short_smart(buffer)
#define gshortsmart(buffer) rsbuf_read_short_smart(buffer)

#define gstring(buffer) rsbuf_read_string(buffer)
#define gparams(buffer, params) rsbuf_read_params(buffer, params)
#define greadto(buffer, out, out_size, len) rsbuf_readto(buffer, out, out_size, len)

#endif