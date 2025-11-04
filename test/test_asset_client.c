/*
 * Test client for asset_server
 * Sends a request with Table # and Archive # and receives the data
 */

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PORT 4949

typedef struct
{
    uint32_t table_num;
    uint32_t archive_num;
} asset_request_t;

int
main(int argc, char* argv[])
{
    int sockfd;
    struct sockaddr_in server_addr;
    asset_request_t request;

    // Parse command line arguments
    if( argc != 3 )
    {
        fprintf(stderr, "Usage: %s <table_num> <archive_num>\n", argv[0]);
        return EXIT_FAILURE;
    }

    uint32_t table_num = atoi(argv[1]);
    uint32_t archive_num = atoi(argv[2]);

    // Create socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if( sockfd < 0 )
    {
        perror("socket");
        return EXIT_FAILURE;
    }

    // Set up server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    // Connect to server
    if( connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0 )
    {
        perror("connect");
        close(sockfd);
        return EXIT_FAILURE;
    }

    printf("Connected to server on port %d\n", PORT);
    printf("Requesting: Table=%u, Archive=%u\n", table_num, archive_num);

    // Prepare request (convert to network byte order)
    request.table_num = htonl(table_num);
    request.archive_num = htonl(archive_num);

    // Send request
    ssize_t bytes_sent = send(sockfd, &request, sizeof(asset_request_t), 0);
    if( bytes_sent != sizeof(asset_request_t) )
    {
        perror("send");
        close(sockfd);
        return EXIT_FAILURE;
    }

    printf("Sent request: %zd bytes\n", bytes_sent);

    // Receive data size first
    uint32_t data_size_network;
    ssize_t bytes_received = recv(sockfd, &data_size_network, sizeof(uint32_t), 0);
    if( bytes_received != sizeof(uint32_t) )
    {
        if( bytes_received < 0 )
        {
            perror("recv data_size");
        }
        else
        {
            fprintf(stderr, "Received incomplete data size: %zd bytes\n", bytes_received);
        }
        close(sockfd);
        return EXIT_FAILURE;
    }

    uint32_t data_size = ntohl(data_size_network);
    printf("Data size: %u bytes\n", data_size);

    if( data_size == 0 )
    {
        fprintf(stderr, "Error: Server returned 0 bytes (archive not found or invalid table)\n");
        close(sockfd);
        return EXIT_FAILURE;
    }

    // Allocate buffer for data
    char* data = malloc(data_size);
    if( !data )
    {
        perror("malloc");
        close(sockfd);
        return EXIT_FAILURE;
    }

    // Receive the actual data
    size_t total_received = 0;
    while( total_received < data_size )
    {
        bytes_received = recv(sockfd, data + total_received, data_size - total_received, 0);
        if( bytes_received <= 0 )
        {
            if( bytes_received < 0 )
            {
                perror("recv data");
            }
            else
            {
                fprintf(stderr, "Connection closed before all data received\n");
            }
            free(data);
            close(sockfd);
            return EXIT_FAILURE;
        }
        total_received += bytes_received;
    }

    printf("✓ Received %zu bytes successfully!\n", total_received);

    // Show first 64 bytes as hex
    printf("\nFirst %d bytes (hex):\n", (int)(data_size < 64 ? data_size : 64));
    for( int i = 0; i < (int)(data_size < 64 ? data_size : 64); i++ )
    {
        printf("%02x ", (unsigned char)data[i]);
        if( (i + 1) % 16 == 0 )
            printf("\n");
    }
    printf("\n");

    free(data);
    close(sockfd);
    return EXIT_SUCCESS;
}
