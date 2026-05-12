#ifndef GAMECACHE2_IOREQUEST_C
#define GAMECACHE2_IOREQUEST_C

#include "gamecache2_iorequest.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define GAMECACHE2_IOR_REQUEST_BUFFER_DEFAULT_CAPACITY 16

struct GameCache2IORequestBuffer*
GameCache2IORequestBuffer_New()
{
    struct GameCache2IORequestBuffer* buffer = malloc(sizeof(struct GameCache2IORequestBuffer));
    if( !buffer )
        return NULL;
    buffer->requests =
        calloc(GAMECACHE2_IOR_REQUEST_BUFFER_DEFAULT_CAPACITY, sizeof(struct GameCache2IORequest));
    if( !buffer->requests )
    {
        free(buffer);
        return NULL;
    }

    buffer->capacity = GAMECACHE2_IOR_REQUEST_BUFFER_DEFAULT_CAPACITY;
    buffer->count = 0;

    return buffer;
}

void
GameCache2IORequestBuffer_Free(struct GameCache2IORequestBuffer* buffer)
{
    if( !buffer )
        return;
    free(buffer->requests);
    free(buffer);
}

void
GameCache2IORequestBuffer_PushRequest(
    struct GameCache2IORequestBuffer* buffer,
    int table_id,
    int archive_id,
    int flags)
{
    struct GameCache2IORequest* request = NULL;
    if( buffer->count >= buffer->capacity )
    {
        buffer->capacity *= 2;
        buffer->requests =
            realloc(buffer->requests, buffer->capacity * sizeof(struct GameCache2IORequest));
        if( !buffer->requests )
            return;
    }

    request = &buffer->requests[buffer->count];
    request->table_id = table_id;
    request->archive_id = archive_id;
    request->flags = flags;
    buffer->count++;
}

int
GameCache2IORequestBuffer_Count(struct GameCache2IORequestBuffer* buffer)
{
    return buffer->count;
}

struct GameCache2IORequest*
GameCache2IORequestBuffer_GetRequest(
    struct GameCache2IORequestBuffer* buffer,
    int index)
{
    assert(buffer && index >= 0 && index < buffer->count && "Invalid arguments");
    return &buffer->requests[index];
}

void
GameCache2Archive_Push(
    struct GameCache2Archive* archive,
    struct GameCache2Archive** archives)
{
    archive->next = *archives;
    *archives = archive;
}

#endif /* GAMECACHE2_IOREQUEST_C */