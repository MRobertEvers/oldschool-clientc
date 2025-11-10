

enum AsyncIORequestKind
{
    E_ASYNCIO_REQUEST_READ,
    E_ASYNCIO_REQUEST_WRITE,
    E_ASYNCIO_REQUEST_ERROR,
    E_ASYNCIO_REQUEST_TIMEOUT,
};

struct AsyncIORequest
{
    enum AsyncIORequestKind kind;
    int fd;
    int flags;
    int events;
    int revents;
};

enum AsyncIOCondition
{
    E_ASYNCIO_READY,
    E_ASYNCIO_ERROR,
    E_ASYNCIO_WAIT
};

struct AsyncIO
{
    int fd;
    int flags;
    int events;
    int revents;
};

struct AsyncIO* asyncio_new(int fd);
void asyncio_free(struct AsyncIO* asyncio);

void asyncio_wait(struct AsyncIO* asyncio);