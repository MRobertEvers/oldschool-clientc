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

void
rsbuf_init(
    struct RSBuffer* buffer,
    int8_t* data,
    int size);

int
rsbuf_g1(struct RSBuffer* buffer);
void
rsbuf_p1(
    struct RSBuffer* buffer,
    int value);
// signed
int8_t
rsbuf_g1b(struct RSBuffer* buffer);
int
rsbuf_g2(struct RSBuffer* buffer);
void
rsbuf_p2(
    struct RSBuffer* buffer,
    int value);
// signed
int
rsbuf_g2b(struct RSBuffer* buffer);
int
rsbuf_g3(struct RSBuffer* buffer);
int
rsbuf_g4(struct RSBuffer* buffer);
void
rsbuf_p4(
    struct RSBuffer* buffer,
    int value);

int64_t
rsbuf_g8(struct RSBuffer* buffer);

int
rsbuf_read_usmart(struct RSBuffer* buffer);
int
rsbuf_read_big_smart(struct RSBuffer* buffer);
char*
rsbuf_read_string_null_terminated(struct RSBuffer* buffer);
char*
rsbuf_read_string_newline_terminated(struct RSBuffer* buffer);

int
rsbuf_read_unsigned_int_smart_short_compat(struct RSBuffer* buffer);
int
rsbuf_read_short_smart(struct RSBuffer* buffer);
int
rsbuf_read_unsigned_short_smart(struct RSBuffer* buffer);

void
rsbuf_read_params(
    struct RSBuffer* buffer,
    struct Params* params);

int
rsbuf_readto(
    struct RSBuffer* buffer,
    char* out,
    int out_size,
    int len);

#define g1(buffer) rsbuf_g1(buffer)
#define g1b(buffer) rsbuf_g1b(buffer)
#define p1(buffer, value) rsbuf_p1(buffer, value)

#define g2(buffer) rsbuf_g2(buffer)
#define g2b(buffer) rsbuf_g2b(buffer)
#define p2(buffer, value) rsbuf_p2(buffer, value)

#define g3(buffer) rsbuf_g3(buffer)
#define g4(buffer) rsbuf_g4(buffer)
#define p4(buffer, value) rsbuf_p4(buffer, value)
#define g8(buffer) rsbuf_g8(buffer)
#define gusmart(buffer) rsbuf_read_usmart(buffer)
#define gbigsmart(buffer) rsbuf_read_big_smart(buffer)
#define gushortsmart(buffer) rsbuf_read_unsigned_short_smart(buffer)
#define gshortsmart(buffer) rsbuf_read_short_smart(buffer)

#define gcstring(buffer) rsbuf_read_string_null_terminated(buffer)
#define gstringnewline(buffer) rsbuf_read_string_newline_terminated(buffer)
#define gparams(buffer, params) rsbuf_read_params(buffer, params)
#define greadto(buffer, out, out_size, len) rsbuf_readto(buffer, out, out_size, len)

#endif