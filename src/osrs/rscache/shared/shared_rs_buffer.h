#ifndef RSCACHE_RSCACHESHARED_RSBUFFER_H
#define RSCACHE_RSCACHESHARED_RSBUFFER_H

#include <stdbool.h>
#include <stdint.h>

struct RSCacheShared_Params
{
    int* keys;
    void** values;
    bool* is_string;
    int count;
    int capacity;
};

struct RSCacheShared_RSBuffer
{
    uint8_t* data;
    uint32_t size;
    uint32_t position;
};

void
RSCacheShared_RSBufferInit(
    struct RSCacheShared_RSBuffer* buffer,
    uint8_t* data,
    uint32_t size);

int
RSCacheShared_RSBufferG1(struct RSCacheShared_RSBuffer* buffer);
void
RSCacheShared_RSBufferP1(
    struct RSCacheShared_RSBuffer* buffer,
    int value);
// signed
int8_t
RSCacheShared_RSBufferG1b(struct RSCacheShared_RSBuffer* buffer);
int
RSCacheShared_RSBufferG2(struct RSCacheShared_RSBuffer* buffer);
void
RSCacheShared_RSBufferP2(
    struct RSCacheShared_RSBuffer* buffer,
    int value);
// signed
int
RSCacheShared_RSBufferG2b(struct RSCacheShared_RSBuffer* buffer);
int
RSCacheShared_RSBufferG3(struct RSCacheShared_RSBuffer* buffer);
int
RSCacheShared_RSBufferG4(struct RSCacheShared_RSBuffer* buffer);
void
RSCacheShared_RSBufferP4(
    struct RSCacheShared_RSBuffer* buffer,
    int value);

int64_t
RSCacheShared_RSBufferG8(struct RSCacheShared_RSBuffer* buffer);

/**
 * Read a big-endian IEEE 754 single-precision float.
 * Equivalent to Java's DataInputStream.readFloat / ByteBuffer.readFloat.
 */
float
RSCacheShared_RSBufferReadFloat(struct RSCacheShared_RSBuffer* buffer);

int
RSCacheShared_RSBufferReadUsmart(struct RSCacheShared_RSBuffer* buffer);
int
RSCacheShared_RSBufferReadBigSmart(struct RSCacheShared_RSBuffer* buffer);
char*
RSCacheShared_RSBufferReadStringNullTerminated(struct RSCacheShared_RSBuffer* buffer);
char*
RSCacheShared_RSBufferReadStringNewlineTerminated(struct RSCacheShared_RSBuffer* buffer);

int
RSCacheShared_RSBufferReadUnsignedIntSmartShortCompat(struct RSCacheShared_RSBuffer* buffer);
int
RSCacheShared_RSBufferReadShortSmart(struct RSCacheShared_RSBuffer* buffer);
int
RSCacheShared_RSBufferReadUnsignedShortSmart(struct RSCacheShared_RSBuffer* buffer);

void
RSCacheShared_RSBufferReadParams(
    struct RSCacheShared_RSBuffer* buffer,
    struct RSCacheShared_Params* params);

int
RSCacheShared_RSBufferReadto(
    struct RSCacheShared_RSBuffer* buffer,
    char* out,
    int out_size,
    int len);

/**
 * Older revs use newline terminated strings. Newer revs use null terminated strings.
 */
void
RSCacheShared_RSBufferPjstr(
    struct RSCacheShared_RSBuffer* buffer,
    const char* str,
    int terminator);

void
RSCacheShared_RSBufferPwrite(
    struct RSCacheShared_RSBuffer* buffer,
    const uint8_t* data,
    int data_size);

/* Raw-pointer + offset readers (for multi-cursor decode, e.g. model format) */
int
RSCacheShared_RSBufferG1At(
    const uint8_t* data,
    int* offset);
int
RSCacheShared_RSBufferG2At(
    const uint8_t* data,
    int* offset);
int
RSCacheShared_RSBufferG4At(
    const uint8_t* data,
    int* offset);
int
RSCacheShared_RSBufferReadShortSmartAt(
    const uint8_t* data,
    int* offset);

#define g1(buffer) RSCacheShared_RSBufferG1(buffer)
#define g1b(buffer) RSCacheShared_RSBufferG1b(buffer)
#define p1(buffer, value) RSCacheShared_RSBufferP1(buffer, value)

#define g2(buffer) RSCacheShared_RSBufferG2(buffer)
#define g2b(buffer) RSCacheShared_RSBufferG2b(buffer)
#define p2(buffer, value) RSCacheShared_RSBufferP2(buffer, value)

#define g3(buffer) RSCacheShared_RSBufferG3(buffer)
#define g4(buffer) RSCacheShared_RSBufferG4(buffer)
#define p4(buffer, value) RSCacheShared_RSBufferP4(buffer, value)
#define g8(buffer) RSCacheShared_RSBufferG8(buffer)
#define gf(buffer) RSCacheShared_RSBufferReadFloat(buffer)
#define gusmart(buffer) RSCacheShared_RSBufferReadUsmart(buffer)
#define gbigsmart(buffer) RSCacheShared_RSBufferReadBigSmart(buffer)
#define gushortsmart(buffer) RSCacheShared_RSBufferReadUnsignedShortSmart(buffer)
#define gshortsmart(buffer) RSCacheShared_RSBufferReadShortSmart(buffer)

#define gcstring(buffer) RSCacheShared_RSBufferReadStringNullTerminated(buffer)
#define gstringnewline(buffer) RSCacheShared_RSBufferReadStringNewlineTerminated(buffer)
#define pjstr(buffer, str, terminator) RSCacheShared_RSBufferPjstr(buffer, str, terminator)

#define gparams(buffer, params) RSCacheShared_RSBufferReadParams(buffer, params)
#define greadto(buffer, out, out_size, len) RSCacheShared_RSBufferReadto(buffer, out, out_size, len)
#define pbuf(buffer, data, data_size) RSCacheShared_RSBufferPwrite(buffer, data, data_size)

#define g1at(data, offset) RSCacheShared_RSBufferG1At(data, offset)
#define g2at(data, offset) RSCacheShared_RSBufferG2At(data, offset)
#define g4at(data, offset) RSCacheShared_RSBufferG4At(data, offset)
#define gshortsmartat(data, offset) RSCacheShared_RSBufferReadShortSmartAt(data, offset)

#endif