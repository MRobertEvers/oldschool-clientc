#ifndef PLATFORM_IMPL_EMSCRIPTEN_ASYNCIO_U_CPP
#define PLATFORM_IMPL_EMSCRIPTEN_ASYNCIO_U_CPP

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define JAVASCRIPT_ARCHIVE_REQUEST_MAGIC 12345678

static void
w4(void* ptr, int value)
{
    ((unsigned char*)ptr)[0] = (value >> 24) & 0xff;
    ((unsigned char*)ptr)[1] = (value >> 16) & 0xff;
    ((unsigned char*)ptr)[2] = (value >> 8) & 0xff;
    ((unsigned char*)ptr)[3] = value & 0xff;
}

static int
r4(void* ptr)
{
    uint8_t* data = (uint8_t*)ptr;
    return data[3] << 24 | data[2] << 16 | data[1] << 8 | data[0];
}

struct JavascriptArchiveRequest
{
    int magic;
    int request_id;
    int table_id;
    int archive_id;
    int status;
    int size;
    int filled;
    char* data;
};

static void
javascript_archive_request_init(
    struct JavascriptArchiveRequest* request, int request_id, int table_id, int archive_id)
{
    request->magic = JAVASCRIPT_ARCHIVE_REQUEST_MAGIC;
    request->request_id = request_id;
    request->table_id = table_id;
    request->archive_id = archive_id;
    request->status = 0;
    request->size = 0;
    request->data = NULL;
};

void*
javascript_archive_request_new_buffer(void)
{
    return malloc(sizeof(struct JavascriptArchiveRequest));
}

void
javascript_archive_request_free_buffer(void* buffer)
{
    free(buffer);
}

static void
javascript_archive_request_deserialize(struct JavascriptArchiveRequest* request, void* data_ptr)
{
    uint8_t* data = (uint8_t*)data_ptr;
    request->magic = r4(data);
    request->request_id = r4(data + 4);
    request->table_id = r4(data + 8);
    request->archive_id = r4(data + 12);
    request->status = r4(data + 16);
    request->size = r4(data + 20);
    request->filled = r4(data + 24);
    request->data = (char*)r4(data + 28);
}

#endif