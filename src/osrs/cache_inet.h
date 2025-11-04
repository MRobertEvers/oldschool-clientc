#ifndef CACHE_INET_H
#define CACHE_INET_H

struct CacheInet;

struct CacheInet* cache_inet_new_connect(char const* ip, int port);
void cache_inet_free(struct CacheInet* cache_inet);

enum CacheInetRequestType
{
    CACHE_INET_REQUEST_TYPE_ARCHIVE = 1,
};

struct CacheInetPayload
{
    int table_id;
    int archive_id;

    char* data;
    int data_size;
};

struct CacheInetPayload*
cache_inet_payload_new_archive_request(struct CacheInet* cache_inet, int table_id, int archive_id);
void cache_inet_payload_free(struct CacheInetPayload* payload);

#endif