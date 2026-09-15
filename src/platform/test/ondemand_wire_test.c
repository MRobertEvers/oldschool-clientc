/*
 * The on-demand file wire, pipelined, against a fake LostCity in this process.
 *
 * The fake server runs on its own thread and speaks exactly the protocol
 * platform_x_io_ondemand.c does: byte 15 in, eight bytes out, then four-byte
 * requests answered as six-byte chunk headers plus up to 500 payload bytes.
 * It deliberately answers OUT OF ORDER and INTERLEAVED, one chunk of each
 * file in turn, because that is what the reassembly has to be right about --
 * and it hangs up once mid-session, because LostCity does that to an idle
 * client and the first request afterwards has to survive it.
 *
 * Pinned:
 *   1. N files begun before any is waited for are answered from one pass of
 *      requests -- the server sees them all before it sends a byte.
 *   2. Interleaved chunks land in the right files, byte for byte.
 *   3. A zero-length answer is "absent", not a failure, and it costs nothing.
 *   4. Two Begins of one file share one request and each gets its bytes.
 *   5. A socket the server closed is redialled and the files re-requested,
 *      once, without the caller doing anything.
 */
#include "platform/platform_x_io_ondemand.h"
#include "platform/sockstream.h"

#include <rscache.h>

#include <arpa/inet.h>
#include <assert.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define TEST_CHECK(cond)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if( !(cond) )                                                                              \
        {                                                                                          \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", #cond, __FILE__, __LINE__);                     \
            abort();                                                                               \
        }                                                                                          \
    } while( 0 )

#define FILE_COUNT 6
#define ABSENT_FILE 4
#define CHUNK 500

static double
now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec * 1000.0 + (double)tv.tv_usec / 1000.0;
}

/* The fake server sits on every request for this long before answering, so
 * a wire that waited for each answer before sending the next request would
 * cost FILE_COUNT of these and a pipelined one costs one. */
#define ANSWER_DELAY_MS 100

/* The corpus: file i is (i + 1) * 333 bytes of a per-file pattern, so a
 * chunk filed under the wrong file or offset is visible. File ABSENT_FILE
 * is one the server does not have. */
static int
corpus_size(int file)
{
    return file == ABSENT_FILE ? 0 : (file + 1) * 333;
}

static unsigned char
corpus_byte(int file, int at)
{
    return (unsigned char)((file * 37 + at * 11) & 0xFF);
}

struct FakeServer
{
    int listen_fd;
    int port;
    /* Requests seen before the first byte of any answer was sent, per
     * session -- the pipelining witness. */
    int requests_before_first_answer[4];
    int sessions;
    /* Hang up after the handshake of session `hangup_session`, having
     * accepted the requests: simulates LostCity's idle timeout. */
    int hangup_session;
    int stop;
};

static int
read_exact(int fd, void* buf, int n)
{
    int have = 0;
    while( have < n )
    {
        ssize_t got = recv(fd, (char*)buf + have, (size_t)(n - have), 0);
        if( got <= 0 )
            return -1;
        have += (int)got;
    }
    return 0;
}

static int
write_all(int fd, void const* buf, int n)
{
    int done = 0;
    while( done < n )
    {
        ssize_t sent = send(fd, (char const*)buf + done, (size_t)(n - done), 0);
        if( sent <= 0 )
            return -1;
        done += (int)sent;
    }
    return 0;
}

static void*
fake_server_main(void* arg)
{
    struct FakeServer* server = (struct FakeServer*)arg;

    while( !server->stop )
    {
        int fd = accept(server->listen_fd, NULL, NULL);
        unsigned char hello;
        unsigned char seed[8] = { 0 };
        int session;
        int requests[64][2];
        int request_count = 0;
        int sent_any = 0;
        int pending;

        if( fd < 0 )
            break;
        session = server->sessions++;
        if( read_exact(fd, &hello, 1) != 0 || hello != 15 )
        {
            close(fd);
            continue;
        }
        write_all(fd, seed, 8);

        if( session == server->hangup_session )
        {
            /* Take what the client sends, then drop it on the floor: the
             * client must notice on its own and ask again. */
            unsigned char req[4];
            usleep(50 * 1000);
            while( recv(fd, req, 4, MSG_DONTWAIT) == 4 )
                ;
            close(fd);
            continue;
        }

        /*
         * Gather everything the client has queued before answering a byte,
         * then answer round-robin, one chunk per file per turn. A real
         * server answers as it drains its queue; this one is arranged to be
         * the worst case for a reassembler that assumed order.
         */
        usleep(ANSWER_DELAY_MS * 1000);
        for( ;; )
        {
            unsigned char req[4];
            ssize_t got = recv(fd, req, 4, MSG_DONTWAIT);
            if( got != 4 || request_count >= 64 )
                break;
            requests[request_count][0] = req[0];
            requests[request_count][1] = (req[1] << 8) | req[2];
            request_count++;
        }
        server->requests_before_first_answer[session & 3] = request_count;

        pending = request_count;
        for( int part = 0; pending > 0; part++ )
        {
            for( int r = 0; r < request_count; r++ )
            {
                int const file = requests[r][1];
                int const total = corpus_size(file);
                unsigned char header[6];
                unsigned char payload[CHUNK];
                int offset = part * CHUNK;
                int count;

                if( offset > total || (total > 0 && offset == total) )
                    continue;
                if( total == 0 && part > 0 )
                    continue;
                header[0] = (unsigned char)requests[r][0];
                header[1] = (unsigned char)(file >> 8);
                header[2] = (unsigned char)file;
                header[3] = (unsigned char)(total >> 8);
                header[4] = (unsigned char)total;
                header[5] = (unsigned char)part;
                write_all(fd, header, 6);
                sent_any = 1;
                if( total == 0 )
                {
                    pending--;
                    continue;
                }
                count = total - offset;
                if( count > CHUNK )
                    count = CHUNK;
                for( int i = 0; i < count; i++ )
                    payload[i] = corpus_byte(file, offset + i);
                write_all(fd, payload, count);
                if( offset + count >= total )
                    pending--;
            }
        }
        (void)sent_any;
        /* Linger for anything else this session, answered in order. */
        for( ;; )
        {
            unsigned char req[4];
            int file;
            int total;
            if( read_exact(fd, req, 4) != 0 )
                break;
            file = (req[1] << 8) | req[2];
            total = corpus_size(file);
            for( int offset = 0; offset < total || (total == 0 && offset == 0); offset += CHUNK )
            {
                unsigned char header[6] = {
                    req[0], (unsigned char)(file >> 8), (unsigned char)file,
                    (unsigned char)(total >> 8), (unsigned char)total,
                    (unsigned char)(offset / CHUNK),
                };
                unsigned char payload[CHUNK];
                int count = total - offset;
                if( count > CHUNK )
                    count = CHUNK;
                write_all(fd, header, 6);
                for( int i = 0; i < count; i++ )
                    payload[i] = corpus_byte(file, offset + i);
                if( count > 0 )
                    write_all(fd, payload, count);
                if( total == 0 )
                    break;
            }
        }
        close(fd);
    }
    return NULL;
}

static void
fake_server_start(struct FakeServer* server, pthread_t* thread)
{
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    int one = 1;

    memset(server, 0, sizeof(*server));
    server->hangup_session = -1;
    server->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    TEST_CHECK(server->listen_fd >= 0);
    setsockopt(server->listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    TEST_CHECK(bind(server->listen_fd, (struct sockaddr*)&addr, sizeof(addr)) == 0);
    TEST_CHECK(listen(server->listen_fd, 8) == 0);
    TEST_CHECK(getsockname(server->listen_fd, (struct sockaddr*)&addr, &len) == 0);
    server->port = ntohs(addr.sin_port);
    TEST_CHECK(pthread_create(thread, NULL, fake_server_main, server) == 0);
}

static void
fake_server_stop(struct FakeServer* server, pthread_t thread)
{
    server->stop = 1;
    shutdown(server->listen_fd, SHUT_RDWR);
    close(server->listen_fd);
    pthread_join(thread, NULL);
}

static void
check_bytes(int file, char const* data, int size)
{
    TEST_CHECK(size == corpus_size(file));
    for( int i = 0; i < size; i++ )
        TEST_CHECK((unsigned char)data[i] == corpus_byte(file, i));
}

/* Pump until every file in `files` has settled, taking each as it does. */
static void
pump_until_taken(
    struct PlatformXIOOnDemand* od,
    int table,
    int const* files,
    int count,
    char** out_data,
    int* out_sizes)
{
    int taken = 0;
    int spins = 0;
    int done[64] = { 0 };

    while( taken < count )
    {
        PlatformXIOOnDemand_Pump(od);
        for( int i = 0; i < count; i++ )
        {
            if( done[i] )
                continue;
            if( PlatformXIOOnDemand_FetchTake(od, table, files[i], &out_data[i], &out_sizes[i]) )
            {
                done[i] = 1;
                taken++;
            }
        }
        usleep(2000);
        TEST_CHECK(++spins < 5000);
    }
}

static void
test_pipelined_interleaved_fetch(void)
{
    struct FakeServer server;
    pthread_t thread;
    struct PlatformXIOOnDemand* od;
    int files[FILE_COUNT];
    char* data[FILE_COUNT] = { 0 };
    int sizes[FILE_COUNT] = { 0 };
    int const table = RSCACHE_DAT1_DISK_TABLE_MODELS;

    fake_server_start(&server, &thread);
    od = PlatformXIOOnDemand_NewWireOnly("127.0.0.1", server.port);
    TEST_CHECK(od);

    for( int i = 0; i < FILE_COUNT; i++ )
    {
        files[i] = i;
        TEST_CHECK(PlatformXIOOnDemand_FetchBegin(od, table, i) == 0);
    }
    /* A second waiter on file 1: one request, two answers. */
    TEST_CHECK(PlatformXIOOnDemand_FetchBegin(od, table, 1) == 0);

    for( int i = 0; i < FILE_COUNT; i++ )
        TEST_CHECK(PlatformXIOOnDemand_FetchPending(od, table, i));

    {
        double const t0 = now_ms();
        pump_until_taken(od, table, files, FILE_COUNT, data, sizes);
        /* One answer delay for the lot, not one per file: the whole point.
         * The bound leaves room for the pump's own sleeps and a slow box;
         * a serial wire would take FILE_COUNT delays, well past it. */
        TEST_CHECK(now_ms() - t0 < ANSWER_DELAY_MS * 3);
    }

    /* Every request was on the wire before the server answered any. */
    TEST_CHECK(server.requests_before_first_answer[0] == FILE_COUNT);

    for( int i = 0; i < FILE_COUNT; i++ )
    {
        if( i == ABSENT_FILE )
        {
            TEST_CHECK(data[i] == NULL);
            TEST_CHECK(sizes[i] == 0);
            continue;
        }
        check_bytes(i, data[i], sizes[i]);
        free(data[i]);
    }

    /* The second waiter on file 1 still gets its own copy. */
    {
        char* again = NULL;
        int size = 0;
        TEST_CHECK(PlatformXIOOnDemand_FetchTake(od, table, 1, &again, &size) == 1);
        check_bytes(1, again, size);
        free(again);
    }
    /* And now nobody is waiting on anything. */
    TEST_CHECK(!PlatformXIOOnDemand_FetchPending(od, table, 1));

    PlatformXIOOnDemand_Free(od);
    fake_server_stop(&server, thread);
    printf("ok - %d files requested in one pass, answered interleaved, one absent, one shared\n",
        FILE_COUNT);
}

static void
test_hangup_is_redialled_once(void)
{
    struct FakeServer server;
    pthread_t thread;
    struct PlatformXIOOnDemand* od;
    int files[2] = { 0, 2 };
    char* data[2] = { 0 };
    int sizes[2] = { 0 };
    int const table = RSCACHE_DAT1_DISK_TABLE_ANIMATIONS;

    fake_server_start(&server, &thread);
    /* Session 0 -- the one New's handshake opens -- is the one that hangs
     * up, and it has to be marked before the server thread reaches its
     * check, which is the moment the handshake completes. */
    server.hangup_session = 0;
    od = PlatformXIOOnDemand_NewWireOnly("127.0.0.1", server.port);
    TEST_CHECK(od);

    TEST_CHECK(PlatformXIOOnDemand_FetchBegin(od, table, 0) == 0);
    TEST_CHECK(PlatformXIOOnDemand_FetchBegin(od, table, 2) == 0);
    pump_until_taken(od, table, files, 2, data, sizes);

    TEST_CHECK(server.sessions == 2);
    check_bytes(0, data[0], sizes[0]);
    check_bytes(2, data[1], sizes[1]);
    free(data[0]);
    free(data[1]);

    PlatformXIOOnDemand_Free(od);
    fake_server_stop(&server, thread);
    printf("ok - a server hang-up is redialled and the in-flight files re-requested\n");
}

int
main(void)
{
    test_pipelined_interleaved_fetch();
    test_hangup_is_redialled_once();
    printf("ondemand-wire: all tests passed\n");
    return 0;
}
