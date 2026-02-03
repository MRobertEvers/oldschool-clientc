#ifndef SOCKSTREAM_H
#define SOCKSTREAM_H

#ifdef __cplusplus
extern "C" {
#endif

#define SOCKSTREAM_ERROR_WOULDBLOCK -1
#define SOCKSTREAM_ERROR -2

// Opaque structure for socket stream
struct SockStream;

int
sockstream_init(void);

void
sockstream_cleanup(void);

int 
sockstream_lasterror(struct SockStream* stream);

char*
sockstream_strerror(int error);

/**
 * Create a new socket stream and connect to the specified host and port.
 * The socket will be created in non-blocking mode.
 *
 * @param host The hostname or IP address to connect to
 * @param port The port number to connect to
 * @param timeout_sec Connection timeout in seconds (0 = use default 5 seconds)
 * @return Pointer to SockStream on success, NULL on failure
 */
struct SockStream* sockstream_connect(const char* host, int port, int timeout_sec);

/**
 * Send data on the socket stream.
 *
 * @param stream The socket stream
 * @param buffer The data to send
 * @param size The number of bytes to send
 * @return Number of bytes sent on success, -1 on error
 */
int sockstream_send(struct SockStream* stream, const void* buffer, int size);

/**
 * Receive data from the socket stream (non-blocking).
 *
 * @param stream The socket stream
 * @param buffer The buffer to receive data into
 * @param size The maximum number of bytes to receive
 * @return Number of bytes received on success, 0 if connection closed, -1 on error (check errno/WSAGetLastError)
 */
int sockstream_recv(struct SockStream* stream, void* buffer, int size);

/**
 * Check if the socket stream is valid and connected.
 *
 * @param stream The socket stream
 * @return 1 if valid and connected, 0 otherwise
 */
int sockstream_is_connected(struct SockStream* stream);

int sockstream_poll_connect(struct SockStream* stream);

/**
 * Get the underlying socket file descriptor.
 * Useful for select/poll operations.
 *
 * @param stream The socket stream
 * @return Socket file descriptor, or -1 if invalid
 */
int sockstream_get_fd(struct SockStream* stream);

/**
 * Close and free the socket stream.
 *
 * @param stream The socket stream to close (can be NULL)
 */
void sockstream_close(struct SockStream* stream);

#ifdef __cplusplus
}
#endif

#endif // SOCKSTREAM_H
