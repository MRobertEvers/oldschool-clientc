#ifndef RSCACHE_RSCACHEDAT2DISK_INET_H
#define RSCACHE_RSCACHEDAT2DISK_INET_H

struct RSCacheDat2Disk_Inet;

struct RSCacheDat2Disk_Inet*
RSCacheDat2Disk_InetNewConnect(
    char const* ip,
    int port);

void
RSCacheDat2Disk_InetFree(struct RSCacheDat2Disk_Inet* cache_inet);

enum RSCacheDat2Disk_InetRequestType
{
    CACHE_INET_REQUEST_TYPE_ARCHIVE = 1,
};

struct RSCacheDat2Disk_InetPayload
{
    int table_id;
    int archive_id;

    char* data;
    int data_size;
};

struct RSCacheDat2Disk_InetPayload*
RSCacheDat2Disk_InetPayloadNewArchiveRequest(
    struct RSCacheDat2Disk_Inet* cache_inet,
    int table_id,
    int archive_id);

void
RSCacheDat2Disk_InetPayloadFree(struct RSCacheDat2Disk_InetPayload* payload);

#endif