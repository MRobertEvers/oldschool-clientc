/*
 * Asset Server with Work Queue
 * Cross-platform socket server for Linux and macOS
 * Listens on port 4949
 */

#include <errno.h>
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
#define MAX_QUEUE_SIZE 256
#define BUFFER_SIZE 4096

#undef CACHE_PATH
#define CACHE_PATH "../cache/server"

// Task structure
typedef struct task
{
    int client_fd;
    struct task* next;
} task_t;

// Work queue structure
typedef struct
{
    task_t* head;
    task_t* tail;
    int count;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int shutdown;
} work_queue_t;

// Thread pool structure
typedef struct
{
    pthread_t threads[NUM_THREADS];
    work_queue_t queue;
} thread_pool_t;

// Global variables
static thread_pool_t pool;
static int server_fd = -1;
static volatile int running = 1;
static struct Cache* cache = NULL;

// Function prototypes
void work_queue_init(work_queue_t* queue);
void work_queue_destroy(work_queue_t* queue);
int work_queue_push(work_queue_t* queue, int client_fd);
task_t* work_queue_pop(work_queue_t* queue);
void* worker_thread(void* arg);
void handle_client(int client_fd);
void thread_pool_init(thread_pool_t* pool);
void thread_pool_destroy(thread_pool_t* pool);
int create_server_socket(int port);
void signal_handler(int sig);
void cleanup_server(void);

// Initialize work queue
void
work_queue_init(work_queue_t* queue)
{
    queue->head = NULL;
    queue->tail = NULL;
    queue->count = 0;
    queue->shutdown = 0;
    pthread_mutex_init(&queue->mutex, NULL);
    pthread_cond_init(&queue->cond, NULL);
}

// Destroy work queue
void
work_queue_destroy(work_queue_t* queue)
{
    pthread_mutex_lock(&queue->mutex);
    queue->shutdown = 1;

    // Free remaining tasks
    task_t* task = queue->head;
    while( task )
    {
        task_t* next = task->next;
        if( task->client_fd >= 0 )
        {
            close(task->client_fd);
        }
        free(task);
        task = next;
    }

    pthread_cond_broadcast(&queue->cond);
    pthread_mutex_unlock(&queue->mutex);

    pthread_mutex_destroy(&queue->mutex);
    pthread_cond_destroy(&queue->cond);
}

// Push task to queue
int
work_queue_push(work_queue_t* queue, int client_fd)
{
    pthread_mutex_lock(&queue->mutex);

    if( queue->shutdown )
    {
        pthread_mutex_unlock(&queue->mutex);
        return -1;
    }

    if( queue->count >= MAX_QUEUE_SIZE )
    {
        pthread_mutex_unlock(&queue->mutex);
        fprintf(stderr, "Work queue is full\n");
        return -1;
    }

    task_t* task = (task_t*)malloc(sizeof(task_t));
    if( !task )
    {
        pthread_mutex_unlock(&queue->mutex);
        return -1;
    }

    task->client_fd = client_fd;
    task->next = NULL;

    if( queue->tail )
    {
        queue->tail->next = task;
        queue->tail = task;
    }
    else
    {
        queue->head = queue->tail = task;
    }

    queue->count++;
    pthread_cond_signal(&queue->cond);
    pthread_mutex_unlock(&queue->mutex);

    return 0;
}

// Pop task from queue
task_t*
work_queue_pop(work_queue_t* queue)
{
    pthread_mutex_lock(&queue->mutex);

    while( queue->count == 0 && !queue->shutdown )
    {
        pthread_cond_wait(&queue->cond, &queue->mutex);
    }

    if( queue->shutdown && queue->count == 0 )
    {
        pthread_mutex_unlock(&queue->mutex);
        return NULL;
    }

    task_t* task = queue->head;
    if( task )
    {
        queue->head = task->next;
        if( !queue->head )
        {
            queue->tail = NULL;
        }
        queue->count--;
    }

    pthread_mutex_unlock(&queue->mutex);
    return task;
}

// Worker thread function
void*
worker_thread(void* arg)
{
    work_queue_t* queue = (work_queue_t*)arg;

    printf("Worker thread %lu started\n", (unsigned long)pthread_self());

    while( 1 )
    {
        task_t* task = work_queue_pop(queue);
        if( !task )
        {
            break; // Shutdown signal
        }

        handle_client(task->client_fd);
        close(task->client_fd);
        free(task);
    }

    printf("Worker thread %lu exiting\n", (unsigned long)pthread_self());
    return NULL;
}

// Request structure: Table # and Archive #
typedef struct
{
    uint32_t table_num;
    uint32_t archive_num;
} asset_request_t;

// Handle client connection
void
handle_client(int client_fd)
{
    asset_request_t request;
    ssize_t bytes_read;
    char request_code = 0;

    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    getpeername(client_fd, (struct sockaddr*)&addr, &addr_len);

    printf("Handling client from %s:%d\n", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));

    // Keep handling requests from the same client until they disconnect
    while( 1 )
    {
        // Read request code (1 byte)
        bytes_read = recv(client_fd, &request_code, sizeof(char), 0);
        if( bytes_read == 0 )
        {
            // Client closed connection
            printf("Client %s:%d disconnected\n", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
            break;
        }
        if( bytes_read != sizeof(char) )
        {
            perror("recv request code");
            break;
        }

        if( request_code != 1 )
        {
            fprintf(stderr, "Invalid request code: %d\n", request_code);
            break;
        }

        char asset_request_buffer[5] = { 0 };
        bytes_read = recv(client_fd, &asset_request_buffer, sizeof(asset_request_buffer), 0);
        if( bytes_read == 0 )
        {
            // Client closed connection
            printf("Client %s:%d disconnected\n", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
            break;
        }
        if( bytes_read != sizeof(asset_request_buffer) )
        {
            perror("recv request");
            break;
        }

        int status = 0;

        request.table_num = asset_request_buffer[0] & 0xff;
        request.archive_num =
            (asset_request_buffer[1] & 0xff) << 24 | (asset_request_buffer[2] & 0xff) << 16 |
            (asset_request_buffer[3] & 0xff) << 8 | (asset_request_buffer[4] & 0xff);

        // Convert from network byte order to host byte order
        uint32_t table_num = (request.table_num);
        uint32_t archive_num = (request.archive_num);

        printf("Request: Table=%u, Archive=%u\n", table_num, archive_num);

        // Load archive from cache
        if( !cache )
        {
            fprintf(stderr, "Cache not initialized\n");
            break;
        }

        struct CacheArchive* archive = NULL;

        if( table_num == 255 )
        {
            // Load the reference table
            archive = cache_archive_new_reference_table_load(cache, archive_num);
            if( !archive )
            {
                fprintf(
                    stderr, "Failed to load reference table: Table=255, Archive=%u\n", archive_num);
                uint32_t error_status = htonl(0);
                send(client_fd, &error_status, sizeof(uint32_t), 0);
                uint32_t error_size = htonl(0);
                send(client_fd, &error_size, sizeof(uint32_t), 0);
                continue; // Try to handle next request
            }
        }
        else
        {
            // Check if table_num is valid
            if( !cache_is_valid_table_id(table_num) )
            {
                fprintf(stderr, "Invalid table id: %u\n", table_num);
                uint32_t error_status = htonl(0);
                send(client_fd, &error_status, sizeof(uint32_t), 0);
                uint32_t error_size = htonl(0);
                send(client_fd, &error_size, sizeof(uint32_t), 0);
                continue; // Try to handle next request
            }

            int32_t* xtea_key = NULL;
            if( table_num == CACHE_MAPS )
            {
                xtea_key = cache_archive_xtea_key(cache, table_num, archive_num);
                if( !xtea_key )
                {
                    fprintf(
                        stderr,
                        "Failed to load xtea key for table %d, archive %d\n",
                        table_num,
                        archive_num);
                }
            }
            // Load the archive
            archive = cache_archive_new_load_decrypted(cache, table_num, archive_num, xtea_key);
            if( !archive )
            {
                fprintf(
                    stderr,
                    "Failed to load archive: Table=%u, Archive=%u\n",
                    table_num,
                    archive_num);
                uint32_t error_status = htonl(0);
                send(client_fd, &error_status, sizeof(uint32_t), 0);
                uint32_t error_size = htonl(0);
                send(client_fd, &error_size, sizeof(uint32_t), 0);
                continue; // Try to handle next request
            }
        }

        printf("Loaded archive: %d bytes\n", archive->data_size);

        status = 1;
        status = htonl(status);
        ssize_t bytes_sent = send(client_fd, &status, sizeof(int), 0);
        if( bytes_sent != sizeof(int) )
        {
            perror("send status");
            cache_archive_free(archive);
            break;
        }

        // Send the data size first (4 bytes, network byte order)
        uint32_t data_size_network = htonl(archive->data_size);
        bytes_sent = send(client_fd, &data_size_network, sizeof(uint32_t), 0);
        if( bytes_sent != sizeof(uint32_t) )
        {
            perror("send data_size");
            cache_archive_free(archive);
            break;
        }

        // Send the actual data
        bytes_sent = send(client_fd, archive->data, archive->data_size, 0);
        if( bytes_sent != archive->data_size )
        {
            perror("send data");
            cache_archive_free(archive);
            break;
        }

        printf(
            "Sent %d bytes for Table=%u, Archive=%u\n", archive->data_size, table_num, archive_num);

        cache_archive_free(archive);

        // Continue to wait for the next request from the same client
    }
}

// Initialize thread pool
void
thread_pool_init(thread_pool_t* pool)
{
    work_queue_init(&pool->queue);

    for( int i = 0; i < NUM_THREADS; i++ )
    {
        if( pthread_create(&pool->threads[i], NULL, worker_thread, &pool->queue) != 0 )
        {
            perror("pthread_create");
            exit(EXIT_FAILURE);
        }
    }

    printf("Thread pool initialized with %d threads\n", NUM_THREADS);
}

// Destroy thread pool
void
thread_pool_destroy(thread_pool_t* pool)
{
    printf("Shutting down thread pool...\n");

    pthread_mutex_lock(&pool->queue.mutex);
    pool->queue.shutdown = 1;
    pthread_cond_broadcast(&pool->queue.cond);
    pthread_mutex_unlock(&pool->queue.mutex);

    for( int i = 0; i < NUM_THREADS; i++ )
    {
        pthread_join(pool->threads[i], NULL);
    }

    work_queue_destroy(&pool->queue);
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

        // Push to work queue
        if( work_queue_push(&pool.queue, client_fd) < 0 )
        {
            fprintf(stderr, "Failed to queue client connection\n");
            close(client_fd);
        }
    }

    printf("Server shutting down...\n");
    return EXIT_SUCCESS;
}
