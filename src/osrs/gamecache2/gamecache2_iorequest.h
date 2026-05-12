#ifndef GAMECACHE2_IOREQUEST_H
#define GAMECACHE2_IOREQUEST_H

struct GameCache2IORequest
{
    int table_id;
    int archive_id;
    int flags;
};

struct GameCache2IORequestBuffer
{
    struct GameCache2IORequest* requests;
    int capacity;
    int count;
};

struct GameCache2Archive
{
    int table_id;
    int archive_id;
    int flags;
    void* data;
    int data_size;

    struct GameCache2Archive* next;
};

struct GameCache2IORequestBuffer*
GameCache2IORequestBuffer_New();

void
GameCache2IORequestBuffer_Free(struct GameCache2IORequestBuffer* buffer);

void
GameCache2IORequestBuffer_PushRequest(
    struct GameCache2IORequestBuffer* buffer,
    int table_id,
    int archive_id,
    int flags);

int
GameCache2IORequestBuffer_Count(struct GameCache2IORequestBuffer* buffer);

struct GameCache2IORequest*
GameCache2IORequestBuffer_GetRequest(
    struct GameCache2IORequestBuffer* buffer,
    int index);

void
GameCache2Archive_Push(
    struct GameCache2Archive* archive,
    struct GameCache2Archive** archives);

#endif /* GAMECACHE2_IOREQUEST_H */