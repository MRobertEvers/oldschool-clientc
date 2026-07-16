#ifndef RSCACHE_BUFFER_H
#define RSCACHE_BUFFER_H

#include <stdbool.h>
#include <stdint.h>

struct RSCache_Params
{
    int* keys;
    void** values;
    bool* is_string;
    int count;
    int capacity;
};
struct RSCache_Buffer
{
    uint8_t* data;
    uint32_t size;
    uint32_t position;
};

void
RSCache_BufferInit(
    struct RSCache_Buffer* buffer,
    uint8_t* data,
    uint32_t size);

int
RSCache_BufferG1(struct RSCache_Buffer* buffer);
void
RSCache_BufferP1(
    struct RSCache_Buffer* buffer,
    int value);
// signed
int8_t
RSCache_BufferG1b(struct RSCache_Buffer* buffer);
int
RSCache_BufferG2(struct RSCache_Buffer* buffer);
void
RSCache_BufferP2(
    struct RSCache_Buffer* buffer,
    int value);
// signed
int
RSCache_BufferG2b(struct RSCache_Buffer* buffer);
int
RSCache_BufferG3(struct RSCache_Buffer* buffer);
int
RSCache_BufferG4(struct RSCache_Buffer* buffer);
void
RSCache_BufferP4(
    struct RSCache_Buffer* buffer,
    int value);

int64_t
RSCache_BufferG8(struct RSCache_Buffer* buffer);

/**
 * Read a big-endian IEEE 754 single-precision float.
 * Equivalent to Java's DataInputStream.readFloat / ByteBuffer.readFloat.
 */
float
RSCache_BufferReadFloat(struct RSCache_Buffer* buffer);

int
RSCache_BufferReadUsmart(struct RSCache_Buffer* buffer);
int
RSCache_BufferReadBigSmart(struct RSCache_Buffer* buffer);
char*
RSCache_BufferReadStringNullTerminated(struct RSCache_Buffer* buffer);
char*
RSCache_BufferReadStringNewlineTerminated(struct RSCache_Buffer* buffer);

int
RSCache_BufferReadUnsignedIntSmartShortCompat(struct RSCache_Buffer* buffer);
int
RSCache_BufferReadShortSmart(struct RSCache_Buffer* buffer);
int
RSCache_BufferReadUnsignedShortSmart(struct RSCache_Buffer* buffer);

void
RSCache_BufferReadParams(
    struct RSCache_Buffer* buffer,
    struct RSCache_Params* params);

int
RSCache_BufferReadto(
    struct RSCache_Buffer* buffer,
    char* out,
    int out_size,
    int len);

/**
 * Older revs use newline terminated strings. Newer revs use null terminated strings.
 */
#define RSCACHE_JSTR_TERMINATOR_NEWLINE 0x0A
#define RSCACHE_JSTR_TERMINATOR_NULL 0x00
void
RSCache_BufferPjstr(
    struct RSCache_Buffer* buffer,
    const char* str,
    int terminator);

void
RSCache_BufferPwrite(
    struct RSCache_Buffer* buffer,
    const uint8_t* data,
    int data_size);

/* Raw-pointer + offset readers (for multi-cursor decode, e.g. model format) */
int
RSCache_BufferG1At(
    const uint8_t* data,
    int* offset);
int
RSCache_BufferG2At(
    const uint8_t* data,
    int* offset);
int
RSCache_BufferG4At(
    const uint8_t* data,
    int* offset);
int
RSCache_BufferReadShortSmartAt(
    const uint8_t* data,
    int* offset);

#define g1(buffer) RSCache_BufferG1(buffer)
#define g1b(buffer) RSCache_BufferG1b(buffer)
#define p1(buffer, value) RSCache_BufferP1(buffer, value)

#define g2(buffer) RSCache_BufferG2(buffer)
#define g2b(buffer) RSCache_BufferG2b(buffer)
#define p2(buffer, value) RSCache_BufferP2(buffer, value)

#define g3(buffer) RSCache_BufferG3(buffer)
#define g4(buffer) RSCache_BufferG4(buffer)
#define p4(buffer, value) RSCache_BufferP4(buffer, value)
#define g8(buffer) RSCache_BufferG8(buffer)
#define gf(buffer) RSCache_BufferReadFloat(buffer)
#define gusmart(buffer) RSCache_BufferReadUsmart(buffer)
#define gbigsmart(buffer) RSCache_BufferReadBigSmart(buffer)
#define gushortsmart(buffer) RSCache_BufferReadUnsignedShortSmart(buffer)
#define gshortsmart(buffer) RSCache_BufferReadShortSmart(buffer)

#define gcstring(buffer) RSCache_BufferReadStringNullTerminated(buffer)
#define gstringnewline(buffer) RSCache_BufferReadStringNewlineTerminated(buffer)
#define pjstr(buffer, str, terminator) RSCache_BufferPjstr(buffer, str, terminator)

#define gparams(buffer, params) RSCache_BufferReadParams(buffer, params)
#define greadto(buffer, out, out_size, len) RSCache_BufferReadto(buffer, out, out_size, len)
#define pbuf(buffer, data, data_size) RSCache_BufferPwrite(buffer, data, data_size)

#define g1at(data, offset) RSCache_BufferG1At(data, offset)
#define g2at(data, offset) RSCache_BufferG2At(data, offset)
#define g4at(data, offset) RSCache_BufferG4At(data, offset)
#define gshortsmartat(data, offset) RSCache_BufferReadShortSmartAt(data, offset)

#endif
