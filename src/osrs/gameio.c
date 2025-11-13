#include "gameio.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static void
gameio_append(struct GameIO* io, struct GameIORequest* request)
{
    struct GameIORequest* last = NULL;
    request->next = NULL;
    request->request_id = io->request_counter++;

    if( !io->requests )
    {
        io->requests = request;
        return;
    }

    last = io->requests;
    while( last->next )
        last = last->next;
    last->next = request;
}

void
gameio_remove(struct GameIO* io, int request_id)
{
    struct GameIORequest* current = io->requests;
    struct GameIORequest* previous = NULL;
    while( current )
    {
        if( current->request_id == request_id )
        {
            if( previous )
                previous->next = current->next;
            else
                io->requests = current->next;
            return;
        }
        previous = current;
        current = current->next;
    }
}

struct GameIO*
gameio_new(void)
{
    struct GameIO* io = malloc(sizeof(struct GameIO));
    memset(io, 0, sizeof(struct GameIO));
    return io;
}

void
gameio_free(struct GameIO* io)
{
    free(io);
}

enum GameIOStatus
gameio_request_new_archive_load(
    struct GameIO* io, int table_id, int archive_id, struct GameIORequest** out)
{
    enum GameIOStatus status = E_GAMEIO_STATUS_ERROR;
    struct GameIORequest* request = NULL;
    if( *out )
    {
        status = (*out)->status;
        if( gameio_resolved(status) )
            gameio_remove(io, (*out)->request_id);

        return status;
    }

    request = malloc(sizeof(struct GameIORequest));
    memset(request, 0, sizeof(struct GameIORequest));
    request->kind = E_GAMEIO_REQUEST_ARCHIVE_LOAD;
    request->status = E_GAMEIO_STATUS_PENDING;
    request->next = NULL;
    request->request_id = io->request_counter++;

    request->_archive_load.table_id = table_id;
    request->_archive_load.archive_id = archive_id;
    request->_archive_load.out_archive_nullable = NULL;

    gameio_append(io, request);

    *out = request;

    return E_GAMEIO_STATUS_PENDING;
}

struct CacheArchive*
gameio_request_free_archive_receive(struct GameIORequest** request)
{
    assert((*request)->kind == E_GAMEIO_REQUEST_ARCHIVE_LOAD);
    assert((*request)->status == E_GAMEIO_STATUS_OK);

    struct CacheArchive* archive = (*request)->_archive_load.out_archive_nullable;
    free(*request);
    *request = NULL;

    return archive;
}

bool
gameio_next(struct GameIO* io, enum GameIOStatus status, struct GameIORequest** out_nullable)
{
    struct GameIORequest* request = NULL;
    if( io->requests == NULL )
        return false;

    if( *out_nullable )
        request = (*out_nullable)->next;
    else
        request = io->requests;

    while( request )
    {
        if( request->status == status )
        {
            *out_nullable = request;
            return true;
        }
        request = request->next;
    }

    return false;
}

bool
gameio_is_idle(struct GameIO* io)
{
    return io->requests == NULL;
}

bool
gameio_resolved(enum GameIOStatus status)
{
    return status == E_GAMEIO_STATUS_OK;
}

char const*
gameio_status_cstr(enum GameIOStatus status)
{
    switch( status )
    {
    case E_GAMEIO_STATUS_OK:
        return "OK";
    case E_GAMEIO_STATUS_ERROR:
        return "ERROR";
    case E_GAMEIO_STATUS_WAIT:
        return "WAIT";
    case E_GAMEIO_STATUS_PENDING:
        return "PENDING";
    }
}