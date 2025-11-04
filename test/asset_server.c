/*
 * Asset Server with Multi-Client Workers
 * Cross-platform socket server for Linux and macOS
 * Each worker thread handles multiple clients using poll()
 * Listens on port 4949
 */

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Socket headers (POSIX - works on Linux and macOS)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

// OSRS Cache
#include "osrs/cache.h"
#include "osrs/xtea_config.h"

#define PORT 4949
#define BACKLOG 128
#define NUM_THREADS 8
#define MAX_CLIENTS_PER_WORKER 128
#define BUFFER_SIZE 4096
#define POLL_TIMEOUT_MS 1000

#undef CACHE_PATH
#define CACHE_PATH "../cache/server"

// Client state enumeration
typedef enum
{
    CLIENT_STATE_READING_REQUEST_CODE,
    CLIENT_STATE_READING_REQUEST_DATA,
    CLIENT_STATE_PROCESSING,
    CLIENT_STATE_SENDING_STATUS,
    CLIENT_STATE_SENDING_SIZE,
    CLIENT_STATE_SENDING_DATA,
} client_state_t;

// Per-client connection state
typedef struct
{
    int fd;
    client_state_t state;
    struct sockaddr_in addr;

    // Request parsing
    char request_code;
    char request_buffer[5];
    int request_bytes_read;

    uint32_t table_num;
    uint32_t archive_num;

    // Response data
    struct CacheArchive* archive;
    int status_value;
    uint32_t status_network;
    uint32_t data_size_network;
    int response_bytes_sent;
} client_conn_t;

// Worker thread data
typedef struct
{
    int thread_id;
    pthread_t thread;
    pthread_mutex_t mutex;

    client_conn_t clients[MAX_CLIENTS_PER_WORKER];
    struct pollfd poll_fds[MAX_CLIENTS_PER_WORKER + 1]; // +1 for wakeup pipe
    int num_clients;

    int wakeup_pipe[2]; // Pipe to wake up poll()
    volatile int shutdown;
} worker_data_t;

// Thread pool structure
typedef struct
{
    worker_data_t workers[NUM_THREADS];
    int next_worker; // Round-robin assignment
    pthread_mutex_t assignment_mutex;
} thread_pool_t;

// Global variables
static thread_pool_t pool;
static int server_fd = -1;
static volatile int running = 1;
static struct Cache* cache = NULL;

// Function prototypes
void* worker_thread(void* arg);
void worker_add_client(worker_data_t* worker, int client_fd, struct sockaddr_in* addr);
void worker_remove_client(worker_data_t* worker, int index);
void worker_handle_client_io(worker_data_t* worker, int client_index);
int set_nonblocking(int fd);
void thread_pool_init(thread_pool_t* pool);
void thread_pool_destroy(thread_pool_t* pool);
int thread_pool_assign_client(thread_pool_t* pool, int client_fd, struct sockaddr_in* addr);
int create_server_socket(int port);
void signal_handler(int sig);
void cleanup_server(void);

// Set socket to non-blocking mode
int
set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if( flags < 0 )
    {
        perror("fcntl F_GETFL");
        return -1;
    }

    if( fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0 )
    {
        perror("fcntl F_SETFL");
        return -1;
    }

    return 0;
}

// Add a client to a worker's client list
void
worker_add_client(worker_data_t* worker, int client_fd, struct sockaddr_in* addr)
{
    pthread_mutex_lock(&worker->mutex);

    if( worker->num_clients >= MAX_CLIENTS_PER_WORKER )
    {
        pthread_mutex_unlock(&worker->mutex);
        fprintf(stderr, "Worker %d: Client limit reached\n", worker->thread_id);
        close(client_fd);
        return;
    }

    // Set socket to non-blocking
    if( set_nonblocking(client_fd) < 0 )
    {
        pthread_mutex_unlock(&worker->mutex);
        close(client_fd);
        return;
    }

    int index = worker->num_clients;
    client_conn_t* client = &worker->clients[index];

    memset(client, 0, sizeof(client_conn_t));
    client->fd = client_fd;
    client->state = CLIENT_STATE_READING_REQUEST_CODE;
    client->addr = *addr;

    // Set up poll fd
    worker->poll_fds[index + 1].fd = client_fd; // +1 because index 0 is wakeup pipe
    worker->poll_fds[index + 1].events = POLLIN;
    worker->poll_fds[index + 1].revents = 0;

    worker->num_clients++;

    printf(
        "Worker %d: Added client %s:%d (total: %d)\n",
        worker->thread_id,
        inet_ntoa(addr->sin_addr),
        ntohs(addr->sin_port),
        worker->num_clients);

    pthread_mutex_unlock(&worker->mutex);

    // Wake up the worker's poll()
    char wake = 1;
    if( write(worker->wakeup_pipe[1], &wake, 1) < 0 )
    {
        perror("write to wakeup pipe");
    }
}

// Remove a client from worker's client list
void
worker_remove_client(worker_data_t* worker, int index)
{
    client_conn_t* client = &worker->clients[index];

    printf(
        "Worker %d: Removing client %s:%d\n",
        worker->thread_id,
        inet_ntoa(client->addr.sin_addr),
        ntohs(client->addr.sin_port));

    if( client->archive )
    {
        cache_archive_free(client->archive);
        client->archive = NULL;
    }

    close(client->fd);

    // Move last client to this slot
    if( index < worker->num_clients - 1 )
    {
        worker->clients[index] = worker->clients[worker->num_clients - 1];
        worker->poll_fds[index + 1] = worker->poll_fds[worker->num_clients]; // +1 for wakeup pipe
    }

    worker->num_clients--;
}

// Handle client I/O for a specific client (non-blocking state machine)
void
worker_handle_client_io(worker_data_t* worker, int client_index)
{
    client_conn_t* client = &worker->clients[client_index];
    ssize_t result;
    int should_remove = 0;

    switch( client->state )
    {
    case CLIENT_STATE_READING_REQUEST_CODE:
    {
        // Try to read 1 byte request code
        result = recv(client->fd, &client->request_code, 1, 0);
        if( result == 0 )
        {
            // Client closed connection
            should_remove = 1;
            break;
        }
        if( result < 0 )
        {
            if( errno != EAGAIN && errno != EWOULDBLOCK )
            {
                perror("recv request_code");
                should_remove = 1;
            }
            break;
        }

        if( client->request_code != 1 )
        {
            fprintf(
                stderr,
                "Worker %d: Invalid request code %d\n",
                worker->thread_id,
                client->request_code);
            should_remove = 1;
            break;
        }

        client->state = CLIENT_STATE_READING_REQUEST_DATA;
        client->request_bytes_read = 0;
        // Fall through to next state
    }
        __attribute__((fallthrough));

    case CLIENT_STATE_READING_REQUEST_DATA:
    {
        // Try to read 5 byte request (table + archive)
        while( client->request_bytes_read < 5 )
        {
            result = recv(
                client->fd,
                client->request_buffer + client->request_bytes_read,
                5 - client->request_bytes_read,
                0);

            if( result == 0 )
            {
                should_remove = 1;
                break;
            }
            if( result < 0 )
            {
                if( errno != EAGAIN && errno != EWOULDBLOCK )
                {
                    perror("recv request_data");
                    should_remove = 1;
                }
                break;
            }

            client->request_bytes_read += result;
        }

        if( should_remove || client->request_bytes_read < 5 )
            break;

        // Parse request
        client->table_num = client->request_buffer[0] & 0xff;
        client->archive_num =
            (client->request_buffer[1] & 0xff) << 24 | (client->request_buffer[2] & 0xff) << 16 |
            (client->request_buffer[3] & 0xff) << 8 | (client->request_buffer[4] & 0xff);

        printf(
            "Worker %d: Request from %s:%d - Table=%u, Archive=%u\n",
            worker->thread_id,
            inet_ntoa(client->addr.sin_addr),
            ntohs(client->addr.sin_port),
            client->table_num,
            client->archive_num);

        client->state = CLIENT_STATE_PROCESSING;
        // Fall through to processing
    }
        __attribute__((fallthrough));

    case CLIENT_STATE_PROCESSING:
    {
        // Load archive from cache
        if( client->table_num == 255 )
        {
            client->archive = cache_archive_new_reference_table_load(cache, client->archive_num);
        }
        else
        {
            if( !cache_is_valid_table_id(client->table_num) )
            {
                fprintf(
                    stderr,
                    "Worker %d: Invalid table id: %u\n",
                    worker->thread_id,
                    client->table_num);
                client->archive = NULL;
            }
            else
            {
                int32_t* xtea_key = NULL;
                if( client->table_num == CACHE_MAPS )
                {
                    xtea_key =
                        cache_archive_xtea_key(cache, client->table_num, client->archive_num);
                }
                client->archive = cache_archive_new_load_decrypted(
                    cache, client->table_num, client->archive_num, xtea_key);
            }
        }

        if( client->archive )
        {
            client->status_value = 1;
            client->status_network = htonl(1);
            client->data_size_network = htonl(client->archive->data_size);
            printf(
                "Worker %d: Loaded archive: %d bytes\n",
                worker->thread_id,
                client->archive->data_size);
        }
        else
        {
            client->status_value = 0;
            client->status_network = htonl(0);
            client->data_size_network = htonl(0);
            fprintf(
                stderr,
                "Worker %d: Failed to load archive Table=%u, Archive=%u\n",
                worker->thread_id,
                client->table_num,
                client->archive_num);
        }

        client->response_bytes_sent = 0;
        client->state = CLIENT_STATE_SENDING_STATUS;

        // Update poll to wait for POLLOUT
        worker->poll_fds[client_index + 1].events = POLLOUT;
        break;
    }

    case CLIENT_STATE_SENDING_STATUS:
    {
        // Send 4-byte status
        while( client->response_bytes_sent < 4 )
        {
            result = send(
                client->fd,
                ((char*)&client->status_network) + client->response_bytes_sent,
                4 - client->response_bytes_sent,
                0);

            if( result < 0 )
            {
                if( errno != EAGAIN && errno != EWOULDBLOCK )
                {
                    perror("send status");
                    should_remove = 1;
                }
                break;
            }

            client->response_bytes_sent += result;
        }

        if( should_remove || client->response_bytes_sent < 4 )
            break;

        client->response_bytes_sent = 0;
        client->state = CLIENT_STATE_SENDING_SIZE;
        // Fall through
    }
        __attribute__((fallthrough));

    case CLIENT_STATE_SENDING_SIZE:
    {
        // Send 4-byte data size
        while( client->response_bytes_sent < 4 )
        {
            result = send(
                client->fd,
                ((char*)&client->data_size_network) + client->response_bytes_sent,
                4 - client->response_bytes_sent,
                0);

            if( result < 0 )
            {
                if( errno != EAGAIN && errno != EWOULDBLOCK )
                {
                    perror("send data_size");
                    should_remove = 1;
                }
                break;
            }

            client->response_bytes_sent += result;
        }

        if( should_remove || client->response_bytes_sent < 4 )
            break;

        client->response_bytes_sent = 0;

        // If no archive data, go back to reading next request
        if( !client->archive || client->archive->data_size == 0 )
        {
            if( client->archive )
            {
                cache_archive_free(client->archive);
                client->archive = NULL;
            }
            client->state = CLIENT_STATE_READING_REQUEST_CODE;
            worker->poll_fds[client_index + 1].events = POLLIN;
            break;
        }

        client->state = CLIENT_STATE_SENDING_DATA;
        // Fall through
    }
        __attribute__((fallthrough));

    case CLIENT_STATE_SENDING_DATA:
    {
        // Send archive data
        while( client->response_bytes_sent < client->archive->data_size )
        {
            result = send(
                client->fd,
                client->archive->data + client->response_bytes_sent,
                client->archive->data_size - client->response_bytes_sent,
                0);

            if( result < 0 )
            {
                if( errno != EAGAIN && errno != EWOULDBLOCK )
                {
                    perror("send data");
                    should_remove = 1;
                }
                break;
            }

            client->response_bytes_sent += result;
        }

        if( should_remove )
            break;

        if( client->response_bytes_sent >= client->archive->data_size )
        {
            printf(
                "Worker %d: Sent %d bytes for Table=%u, Archive=%u\n",
                worker->thread_id,
                client->archive->data_size,
                client->table_num,
                client->archive_num);

            cache_archive_free(client->archive);
            client->archive = NULL;

            // Go back to reading next request
            client->state = CLIENT_STATE_READING_REQUEST_CODE;
            worker->poll_fds[client_index + 1].events = POLLIN;
        }
        break;
    }
    }

    if( should_remove )
    {
        worker_remove_client(worker, client_index);
    }
}

// Worker thread main loop
void*
worker_thread(void* arg)
{
    worker_data_t* worker = (worker_data_t*)arg;

    printf("Worker %d: started\n", worker->thread_id);

    while( !worker->shutdown )
    {
        pthread_mutex_lock(&worker->mutex);
        int num_fds = worker->num_clients + 1; // +1 for wakeup pipe
        pthread_mutex_unlock(&worker->mutex);

        // Poll for events
        int poll_result = poll(worker->poll_fds, num_fds, POLL_TIMEOUT_MS);

        if( poll_result < 0 )
        {
            if( errno != EINTR )
            {
                perror("poll");
            }
            continue;
        }

        if( poll_result == 0 )
        {
            // Timeout, just loop again
            continue;
        }

        pthread_mutex_lock(&worker->mutex);

        // Check wakeup pipe (index 0)
        if( worker->poll_fds[0].revents & POLLIN )
        {
            char buf[256];
            while( read(worker->wakeup_pipe[0], buf, sizeof(buf)) > 0 )
                ; // Drain pipe
        }

        // Handle client events (starting at index 1)
        for( int i = 0; i < worker->num_clients; )
        {
            struct pollfd* pfd = &worker->poll_fds[i + 1];

            if( pfd->revents & (POLLIN | POLLOUT) )
            {
                int old_count = worker->num_clients;
                worker_handle_client_io(worker, i);

                // If client was removed, don't increment i
                if( worker->num_clients < old_count )
                {
                    continue;
                }
            }
            else if( pfd->revents & (POLLERR | POLLHUP | POLLNVAL) )
            {
                printf("Worker %d: Client error/hangup\n", worker->thread_id);
                worker_remove_client(worker, i);
                continue;
            }

            i++;
        }

        pthread_mutex_unlock(&worker->mutex);
    }

    printf("Worker %d: exiting\n", worker->thread_id);
    return NULL;
}

// Assign a client to a worker using round-robin
int
thread_pool_assign_client(thread_pool_t* pool, int client_fd, struct sockaddr_in* addr)
{
    pthread_mutex_lock(&pool->assignment_mutex);

    int worker_id = pool->next_worker;
    pool->next_worker = (pool->next_worker + 1) % NUM_THREADS;

    pthread_mutex_unlock(&pool->assignment_mutex);

    worker_add_client(&pool->workers[worker_id], client_fd, addr);
    return 0;
}

// Initialize thread pool
void
thread_pool_init(thread_pool_t* pool)
{
    memset(pool, 0, sizeof(thread_pool_t));
    pthread_mutex_init(&pool->assignment_mutex, NULL);
    pool->next_worker = 0;

    for( int i = 0; i < NUM_THREADS; i++ )
    {
        worker_data_t* worker = &pool->workers[i];

        worker->thread_id = i;
        worker->num_clients = 0;
        worker->shutdown = 0;

        pthread_mutex_init(&worker->mutex, NULL);

        // Create wakeup pipe
        if( pipe(worker->wakeup_pipe) < 0 )
        {
            perror("pipe");
            exit(EXIT_FAILURE);
        }

        // Set pipe to non-blocking
        set_nonblocking(worker->wakeup_pipe[0]);
        set_nonblocking(worker->wakeup_pipe[1]);

        // Set up poll fd for wakeup pipe (index 0)
        worker->poll_fds[0].fd = worker->wakeup_pipe[0];
        worker->poll_fds[0].events = POLLIN;
        worker->poll_fds[0].revents = 0;

        // Create worker thread
        if( pthread_create(&worker->thread, NULL, worker_thread, worker) != 0 )
        {
            perror("pthread_create");
            exit(EXIT_FAILURE);
        }
    }

    printf("Thread pool initialized with %d workers\n", NUM_THREADS);
}

// Destroy thread pool
void
thread_pool_destroy(thread_pool_t* pool)
{
    printf("Shutting down thread pool...\n");

    // Signal all workers to shutdown
    for( int i = 0; i < NUM_THREADS; i++ )
    {
        pool->workers[i].shutdown = 1;

        // Wake up worker
        char wake = 1;
        write(pool->workers[i].wakeup_pipe[1], &wake, 1);
    }

    // Wait for all workers to exit
    for( int i = 0; i < NUM_THREADS; i++ )
    {
        pthread_join(pool->workers[i].thread, NULL);

        // Cleanup
        pthread_mutex_destroy(&pool->workers[i].mutex);
        close(pool->workers[i].wakeup_pipe[0]);
        close(pool->workers[i].wakeup_pipe[1]);

        // Close any remaining clients
        for( int j = 0; j < pool->workers[i].num_clients; j++ )
        {
            client_conn_t* client = &pool->workers[i].clients[j];
            if( client->archive )
            {
                cache_archive_free(client->archive);
            }
            close(client->fd);
        }
    }

    pthread_mutex_destroy(&pool->assignment_mutex);
    printf("Thread pool shut down\n");
}

// Create server socket
int
create_server_socket(int port)
{
    int sockfd;
    struct sockaddr_in server_addr;
    int opt = 1;

    // Create socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if( sockfd < 0 )
    {
        perror("socket");
        return -1;
    }

    // Set socket options
    if( setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0 )
    {
        perror("setsockopt");
        close(sockfd);
        return -1;
    }

    // Bind socket
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if( bind(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0 )
    {
        perror("bind");
        close(sockfd);
        return -1;
    }

    // Listen
    if( listen(sockfd, BACKLOG) < 0 )
    {
        perror("listen");
        close(sockfd);
        return -1;
    }

    printf("Server listening on port %d\n", port);
    return sockfd;
}

// Signal handler
void
signal_handler(int sig)
{
    (void)sig;
    printf("\nReceived shutdown signal\n");
    running = 0;

    if( server_fd >= 0 )
    {
        close(server_fd);
        server_fd = -1;
    }
}

// Cleanup function
void
cleanup_server(void)
{
    printf("Cleaning up...\n");

    thread_pool_destroy(&pool);

    if( server_fd >= 0 )
    {
        close(server_fd);
        server_fd = -1;
    }

    if( cache )
    {
        cache_free(cache);
        cache = NULL;
    }
}

// Main function
int
main(int argc, char* argv[])
{
    struct sockaddr_in client_addr;
    socklen_t client_addr_len;
    int client_fd;

    int xtea_keys_count = xtea_config_load_keys("../cache/xteas.json");
    if( xtea_keys_count == -1 )
    {
        fprintf(stderr, "Failed to load xtea keys from: ../cache/xteas.json\n");
        return EXIT_FAILURE;
    }
    printf("Loaded %d XTEA keys successfully\n", xtea_keys_count);

    printf("Asset Server starting...\n");
    printf("Platform: ");
#ifdef __APPLE__
    printf("macOS\n");
#elif __linux__
    printf("Linux\n");
#else
    printf("Unknown\n");
#endif

    // Determine cache directory
    char const* cache_dir = CACHE_PATH;
    if( argc > 1 )
    {
        cache_dir = argv[1];
    }

    printf("Cache directory: %s\n", cache_dir);

    // Initialize cache
    printf("Loading cache...\n");
    cache = cache_new_from_directory(cache_dir);
    if( !cache )
    {
        fprintf(stderr, "Failed to initialize cache from directory: %s\n", cache_dir);
        return EXIT_FAILURE;
    }
    printf("Cache initialized successfully\n");

    // Set up signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Register cleanup function
    atexit(cleanup_server);

    // Initialize thread pool
    thread_pool_init(&pool);

    // Create server socket
    server_fd = create_server_socket(PORT);
    if( server_fd < 0 )
    {
        fprintf(stderr, "Failed to create server socket\n");
        return EXIT_FAILURE;
    }

    printf("Server ready. Press Ctrl+C to stop.\n");
    printf(
        "Each of %d workers can handle up to %d clients concurrently\n",
        NUM_THREADS,
        MAX_CLIENTS_PER_WORKER);
    printf("Total capacity: %d concurrent clients\n", NUM_THREADS * MAX_CLIENTS_PER_WORKER);

    // Accept loop
    while( running )
    {
        client_addr_len = sizeof(client_addr);
        client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_addr_len);

        if( client_fd < 0 )
        {
            if( errno == EINTR || !running )
            {
                break;
            }
            perror("accept");
            continue;
        }

        printf(
            "Accepted connection from %s:%d\n",
            inet_ntoa(client_addr.sin_addr),
            ntohs(client_addr.sin_port));

        // Assign to a worker
        thread_pool_assign_client(&pool, client_fd, &client_addr);
    }

    printf("Server shutting down...\n");
    return EXIT_SUCCESS;
}
