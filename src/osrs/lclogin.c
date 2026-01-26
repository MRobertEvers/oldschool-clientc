/*
 * Login State Machine Implementation
 *
 * This module converts the async/await TypeScript login function into a
 * state machine that can be called repeatedly until completion.
 *
 * Usage:
 *   1. Initialize: lclogin_init(&login, client_version, jag_checksum, low_memory)
 *   2. Load RSA keys: lclogin_load_rsa_public_key(&login, "public.pem")
 *                     OR lclogin_load_rsa_public_key_from_env(&login) [uses LOGIN_RSAN/LOGIN_RSAE
 * env vars] OR lclogin_load_rsa_public_key_from_values(&login, modulus_str, exponent_str)
 *                     lclogin_load_rsa_private_key(&login, "private.pem")
 *   3. Open socket connection and set it: lclogin_set_socket(&login, socket_fd)
 *   4. Start login: lclogin_start(&login, username, password, reconnect)
 *   5. Process in loop: while (lclogin_process(&login) == 0) { /* wait or yield
 *   6. Check result: lclogin_get_state(&login) and lclogin_get_result(&login)
 *   7. Cleanup: lclogin_cleanup(&login)
 *
 * The state machine handles all the async operations by breaking them into states.
 * Each call to lclogin_process() advances the state machine one step.
 * Return values:
 *   0 = more processing needed, call again
 *   1 = complete (success or error)
 *  -1 = fatal error
 *
 * RSA encryption is implemented using raw modular exponentiation (no padding),
 * matching the TypeScript implementation: bytesToBigInt -> bigIntModPow -> bigIntToBytes.
 */

#include "lclogin.h"

#include "jbase37.h"
#include "rsa.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <sys/types.h>
#endif

// Helper function to write JString (OSRS string format)
static void
rsbuf_pjstr(
    struct RSBuffer* buffer,
    const char* str,
    int terminator)
{
    int len = strlen(str);
    for( int i = 0; i < len; i++ )
    {
        unsigned char c = (unsigned char)str[i];
        if( c >= 'A' && c <= 'Z' )
        {
            rsbuf_p1(buffer, c);
        }
        else if( c >= 'a' && c <= 'z' )
        {
            rsbuf_p1(buffer, c);
        }
        else if( c >= '0' && c <= '9' )
        {
            rsbuf_p1(buffer, c);
        }
        else
        {
            rsbuf_p1(buffer, c); // Pass through other chars
        }
    }
    rsbuf_p1(buffer, terminator); // Null terminator
}

int
rsbuf_rsaenc(
    struct RSBuffer* buffer,
    const struct rsa* rsa)
{
    int8_t* temp = malloc(buffer->position);
    memcpy(temp, buffer->data, buffer->position);

    int enclen = rsa_crypt(rsa, temp, buffer->position, buffer->data + 1, buffer->size);
    free(temp);

    buffer->data[0] = enclen;
    buffer->position = enclen + 1;

    return enclen;
}

// Socket read helper (non-blocking)
static int
socket_read(
    int fd,
    uint8_t* buffer,
    int size)
{
    if( fd < 0 )
        return -1;
#ifdef __EMSCRIPTEN__
    // Emscripten may need special handling
    return recv(fd, (char*)buffer, size, 0);
#elif defined(_WIN32)
    return recv(fd, (char*)buffer, size, 0);
#else
    return recv(fd, buffer, size, MSG_DONTWAIT);
#endif
}

// Socket write helper
static int
socket_write(
    int fd,
    const uint8_t* buffer,
    int size)
{
    if( fd < 0 )
        return -1;
#ifdef _WIN32
    return send(fd, (const char*)buffer, size, 0);
#else
    return send(fd, buffer, size, 0);
#endif
}

// Generate random seed values
static void
generate_seed(int32_t* seed)
{
    seed[0] = (rand() % 99999999);
    seed[1] = (rand() % 99999999);
    // seed[2] and seed[3] will be set from server_seed
}

// Load RSA public key from modulus and exponent (as decimal strings)
// Returns 0 on success, -1 on error
int
lclogin_load_rsa_public_key_from_values(
    struct LCLogin* login,
    const char* modulus_str,
    const char* exponent_str)
{
    if( !login || !modulus_str || !exponent_str )
    {
        return -1;
    }

    int ret = rsa_init(&login->rsa, exponent_str, modulus_str);
    if( ret < 0 )
    {
        return -1;
    }

    return ret;
}

// Load RSA public key from environment variables with defaults
// Returns 0 on success, -1 on error
int
lclogin_load_rsa_public_key_from_env(struct LCLogin* login)
{
    // const char* default_e_decimal =
    //     "58778699976184461502525193738213253649000149147835990136706041084440742975821";
    // const char* default_n_decimal =
    //     "716290052522979803276181679123052729632931329123232429023784926350120820797289405392906563"
    //     "6522363163621000728841182238772712427862772219676577293600221789";

    char const* default_e_hex =
        "81f390b2cf8ca7039ee507975951d5a0b15a87bf8b3f99c966834118c50fd94d"; // pad exponent to
                                                                            // an even number
                                                                            // (prefix a 0 if
                                                                            // needed)
    char const* default_n_hex =
        "88c38748a58228f7261cdc340b5691d7d0975dee0ecdb717609e6bf971eb3fe723ef9d130e468681373976"
        "8ad9472eb46d8bfcc042c1a5fcb05e931f632eea5d";

    const char* e_str = default_e_hex;
    const char* n_str = default_n_hex;

    return lclogin_load_rsa_public_key_from_values(login, n_str, e_str);
}

void
lclogin_init(
    struct LCLogin* login,
    int client_version,
    int32_t* jag_checksum,
    bool low_memory)
{
    memset(login, 0, sizeof(struct LCLogin));
    login->state = LCLOGIN_STATE_IDLE;
    login->result = LCLOGIN_RESULT_UNEXPECTED;
    login->client_version = client_version;
    login->low_memory = low_memory;
    login->rsa_loaded = false;
    if( jag_checksum )
    {
        memcpy(login->jag_checksum, jag_checksum, sizeof(login->jag_checksum));
    }

    rsbuf_init(&login->out, login->out_data, sizeof(login->out_data));
    rsbuf_init(&login->in, login->in_data, sizeof(login->in_data));
    rsbuf_init(&login->loginout, login->loginout_data, sizeof(login->loginout_data));

    login->socket_fd = -1;
}

void
lclogin_start(
    struct LCLogin* login,
    const char* username,
    const char* password,
    bool reconnect)
{
    if( login->username )
        free(login->username);
    if( login->password )
        free(login->password);

    login->username = username ? strdup(username) : NULL;
    login->password = password ? strdup(password) : NULL;
    login->reconnect = reconnect;

    login->state = LCLOGIN_STATE_INIT;
    login->result = LCLOGIN_RESULT_UNEXPECTED;

    // Reset buffers
    login->out.position = 0;
    login->in.position = 0;
    login->loginout.position = 0;
    login->initial_response_bytes_read = 0;
    login->credentials_sent = 0;
    login->read_bytes_needed = 0;
    login->read_bytes_received = 0;
}

int
lclogin_process(struct LCLogin* login)
{
    if( !login )
        return -1;

    switch( login->state )
    {
    case LCLOGIN_STATE_IDLE:
        // Nothing to do
        return 0;

    case LCLOGIN_STATE_INIT:
    {
        if( !login->reconnect )
        {
            login->login_message0[0] = '\0';
            strncpy(
                login->login_message1,
                "Connecting to server...",
                sizeof(login->login_message1) - 1);
            login->login_message1[sizeof(login->login_message1) - 1] = '\0';
        }

        // Socket should be set via lclogin_set_socket() before calling lclogin_start()
        // or opened in the CONNECTING state
        if( login->socket_fd < 0 )
        {
            // Try to transition to connecting state if socket not set
            // In a full implementation, you'd create the socket here
            login->state = LCLOGIN_STATE_CONNECTING;
            return 0;
        }

        login->state = LCLOGIN_STATE_SENDING_LOGIN_SERVER;
        // Fall through
    }
        __attribute__((fallthrough));

    case LCLOGIN_STATE_CONNECTING:
    {
        // Socket connection should be established here
        // For now, we assume socket_fd is set via lclogin_set_socket()
        // In a full implementation, you would:
        // 1. Create socket: socket(AF_INET, SOCK_STREAM, 0)
        // 2. Connect to server: connect(socket_fd, &server_addr, sizeof(server_addr))
        // 3. Set non-blocking mode if needed

        if( login->socket_fd < 0 )
        {
            login->state = LCLOGIN_STATE_ERROR;
            strncpy(login->login_message0, "", sizeof(login->login_message0) - 1);
            strncpy(
                login->login_message1,
                "Error connecting to server.",
                sizeof(login->login_message1) - 1);
            return -1;
        }

        login->state = LCLOGIN_STATE_SENDING_LOGIN_SERVER;
        // Fall through
    }
        __attribute__((fallthrough));

    case LCLOGIN_STATE_SENDING_LOGIN_SERVER:
    {
        // Calculate login server from username
        uint64_t username37 = strtobase37(login->username);
        int login_server = (int)((username37 >> 16) & 0x1F);

        // Prepare login server selection packet
        login->out.position = 0;
        rsbuf_p1(&login->out, 14);
        rsbuf_p1(&login->out, login_server);

        // Send packet
        int sent = socket_write(login->socket_fd, login->out.data, 2);
        if( sent < 0 )
        {
            if( errno == EAGAIN || errno == EWOULDBLOCK )
            {
                // Would block, try again later
                return 0;
            }
            login->state = LCLOGIN_STATE_ERROR;
            strncpy(login->login_message0, "", sizeof(login->login_message0) - 1);
            strncpy(
                login->login_message1,
                "Error connecting to server.",
                sizeof(login->login_message1) - 1);
            return -1;
        }

        if( sent < 2 )
        {
            // Partial send, would need to track this in a real implementation
            return 0;
        }

        login->state = LCLOGIN_STATE_READING_INITIAL_RESPONSE;
        login->initial_response_bytes_read = 0;
        // Fall through
    }
        __attribute__((fallthrough));

    case LCLOGIN_STATE_READING_INITIAL_RESPONSE:
    {
        // Read 8 bytes
        int needed = 8 - login->initial_response_bytes_read;
        if( needed > 0 )
        {
            int received = socket_read(
                login->socket_fd, login->in.data + login->initial_response_bytes_read, needed);

            if( received < 0 )
            {
                if( errno == EAGAIN || errno == EWOULDBLOCK )
                {
                    // Would block, try again later
                    return 0;
                }
                login->state = LCLOGIN_STATE_ERROR;
                strncpy(login->login_message0, "", sizeof(login->login_message0) - 1);
                strncpy(
                    login->login_message1,
                    "Error connecting to server.",
                    sizeof(login->login_message1) - 1);
                return -1;
            }

            if( received == 0 )
            {
                // Connection closed
                login->state = LCLOGIN_STATE_ERROR;
                strncpy(login->login_message0, "", sizeof(login->login_message0) - 1);
                strncpy(
                    login->login_message1, "Connection closed.", sizeof(login->login_message1) - 1);
                return -1;
            }

            login->initial_response_bytes_read += received;
        }

        if( login->initial_response_bytes_read < 8 )
        {
            // Still need more data
            return 0;
        }

        login->state = LCLOGIN_STATE_READING_REPLY;
        // Fall through
    }
        __attribute__((fallthrough));

    case LCLOGIN_STATE_READING_REPLY:
    {
        // Read reply byte
        uint8_t reply_byte;
        int received = socket_read(login->socket_fd, &reply_byte, 1);

        if( received < 0 )
        {
            if( errno == EAGAIN || errno == EWOULDBLOCK )
            {
                // Would block, try again later
                return 0;
            }
            login->state = LCLOGIN_STATE_ERROR;
            strncpy(login->login_message0, "", sizeof(login->login_message0) - 1);
            strncpy(
                login->login_message1,
                "Error connecting to server.",
                sizeof(login->login_message1) - 1);
            return -1;
        }

        if( received == 0 )
        {
            // Connection closed
            login->state = LCLOGIN_STATE_ERROR;
            strncpy(login->login_message0, "", sizeof(login->login_message0) - 1);
            strncpy(login->login_message1, "Connection closed.", sizeof(login->login_message1) - 1);
            return -1;
        }

        if( received < 1 )
        {
            // Still waiting
            return 0;
        }

        int reply = reply_byte & 0xFF;

        if( reply == 0 )
        {
            // Need to read server seed and send credentials
            login->state = LCLOGIN_STATE_PROCESSING_REPLY_0;
        }
        else
        {
            // Handle other replies directly
            login->state = LCLOGIN_STATE_HANDLING_REPLY;
            login->result = (lclogin_result_t)reply;
        }
        // Fall through
    }
        __attribute__((fallthrough));

    case LCLOGIN_STATE_PROCESSING_REPLY_0:
    {
        // Read 8 bytes for server seed
        int needed = 8 - login->read_bytes_received;
        if( needed > 0 )
        {
            int received =
                socket_read(login->socket_fd, login->in.data + login->read_bytes_received, needed);

            if( received < 0 )
            {
                if( errno == EAGAIN || errno == EWOULDBLOCK )
                {
                    return 0;
                }
                login->state = LCLOGIN_STATE_ERROR;
                strncpy(login->login_message0, "", sizeof(login->login_message0) - 1);
                strncpy(
                    login->login_message1,
                    "Error connecting to server.",
                    sizeof(login->login_message1) - 1);
                return -1;
            }

            if( received == 0 )
            {
                login->state = LCLOGIN_STATE_ERROR;
                strncpy(login->login_message0, "", sizeof(login->login_message0) - 1);
                strncpy(
                    login->login_message1, "Connection closed.", sizeof(login->login_message1) - 1);
                return -1;
            }

            login->read_bytes_received += received;
        }

        if( login->read_bytes_received < 8 )
        {
            return 0;
        }

        // Parse server seed
        login->in.position = 0;
        login->server_seed = rsbuf_g8(&login->in);

        // Generate client seed
        generate_seed(login->seed);
        login->seed[2] = (int32_t)(login->server_seed >> 32);
        login->seed[3] = (int32_t)(login->server_seed & 0xFFFFFFFF);

        // Prepare credentials packet
        login->out.position = 0;
        rsbuf_p1(&login->out, 10);
        rsbuf_p4(&login->out, login->seed[0]);
        rsbuf_p4(&login->out, login->seed[1]);
        rsbuf_p4(&login->out, login->seed[2]);
        rsbuf_p4(&login->out, login->seed[3]);
        rsbuf_p4(&login->out, 1337); // uid

        // Old revs use newline termination on strings.
        rsbuf_pjstr(&login->out, login->username, '\n');
        rsbuf_pjstr(&login->out, login->password, '\n');

        if( rsbuf_rsaenc(&login->out, &login->rsa) < 0 )
        {
            login->state = LCLOGIN_STATE_ERROR;
            strncpy(login->login_message0, "", sizeof(login->login_message0) - 1);
            strncpy(
                login->login_message1, "RSA encryption failed.", sizeof(login->login_message1) - 1);
            return -1;
        }

        // Prepare login packet header
        login->loginout.position = 0;
        if( login->reconnect )
        {
            rsbuf_p1(&login->loginout, 18);
        }
        else
        {
            rsbuf_p1(&login->loginout, 16);
        }

        login->client_version = 245;
        rsbuf_p1(&login->loginout, login->out.position + 36 + 1 + 1);
        rsbuf_p1(&login->loginout, login->client_version);
        rsbuf_p1(&login->loginout, login->low_memory ? 1 : 0);

        // TODO: Get this from the server.
        login->jag_checksum[0] = 0;
        login->jag_checksum[1] = -945108033;
        login->jag_checksum[2] = -323580723;
        login->jag_checksum[3] = 1539972921;
        login->jag_checksum[4] = -259567598;
        login->jag_checksum[5] = -446588279;
        login->jag_checksum[6] = -1840622973;
        login->jag_checksum[7] = -87627495;
        login->jag_checksum[8] = -1625923170;
        for( int i = 0; i < 9; i++ )
        {
            rsbuf_p4(&login->loginout, login->jag_checksum[i]);
        }

        // Append encrypted credentials
        memcpy(
            login->loginout.data + login->loginout.position, login->out.data, login->out.position);
        login->loginout.position += login->out.position;

        // Initialize ISAAC ciphers
        if( login->random_out )
        {
            isaac_free(login->random_out);
        }
        login->random_out = isaac_new(login->seed, 4);

        // Prepare seed for input cipher
        int32_t seed_in[4];
        for( int i = 0; i < 4; i++ )
        {
            seed_in[i] = login->seed[i] + 50;
        }
        if( login->random_in )
        {
            isaac_free(login->random_in);
        }
        login->random_in = isaac_new(seed_in, 4);

        login->state = LCLOGIN_STATE_SENDING_CREDENTIALS;
        login->credentials_sent = 0;
        // Fall through
    }
        __attribute__((fallthrough));

    case LCLOGIN_STATE_SENDING_CREDENTIALS:
    {
        int total_size = login->loginout.position;
        int remaining = total_size - login->credentials_sent;

        if( remaining > 0 )
        {
            int sent = socket_write(
                login->socket_fd, login->loginout.data + login->credentials_sent, remaining);

            if( sent < 0 )
            {
                if( errno == EAGAIN || errno == EWOULDBLOCK )
                {
                    return 0;
                }
                login->state = LCLOGIN_STATE_ERROR;
                strncpy(login->login_message0, "", sizeof(login->login_message0) - 1);
                strncpy(
                    login->login_message1,
                    "Error connecting to server.",
                    sizeof(login->login_message1) - 1);
                return -1;
            }

            if( sent == 0 )
            {
                login->state = LCLOGIN_STATE_ERROR;
                strncpy(login->login_message0, "", sizeof(login->login_message0) - 1);
                strncpy(
                    login->login_message1, "Connection closed.", sizeof(login->login_message1) - 1);
                return -1;
            }

            login->credentials_sent += sent;
        }

        if( login->credentials_sent < total_size )
        {
            // Still sending
            return 0;
        }

        login->state = LCLOGIN_STATE_READING_FINAL_REPLY;
        login->read_bytes_received = 0;
        // Fall through
    }
        __attribute__((fallthrough));

    case LCLOGIN_STATE_READING_FINAL_REPLY:
    {
        // Read final reply byte
        uint8_t reply_byte;
        int received = socket_read(login->socket_fd, &reply_byte, 1);

        if( received < 0 )
        {
            if( errno == EAGAIN || errno == EWOULDBLOCK )
            {
                return 0;
            }
            login->state = LCLOGIN_STATE_ERROR;
            strncpy(login->login_message0, "", sizeof(login->login_message0) - 1);
            strncpy(
                login->login_message1,
                "Error connecting to server.",
                sizeof(login->login_message1) - 1);
            return -1;
        }

        if( received == 0 )
        {
            login->state = LCLOGIN_STATE_ERROR;
            strncpy(login->login_message0, "", sizeof(login->login_message0) - 1);
            strncpy(login->login_message1, "Connection closed.", sizeof(login->login_message1) - 1);
            return -1;
        }

        if( received < 1 )
        {
            return 0;
        }

        int reply = reply_byte & 0xFF;
        login->state = LCLOGIN_STATE_HANDLING_REPLY;
        login->result = (lclogin_result_t)reply;
        // Fall through
    }
        __attribute__((fallthrough));

    case LCLOGIN_STATE_HANDLING_REPLY:
    {
        int reply = (int)login->result;

        if( reply == 1 )
        {
            // Retry after delay
            // In a real implementation, you'd set a timer and retry
            // For now, we'll just indicate retry needed
            login->state = LCLOGIN_STATE_ERROR;
            strncpy(login->login_message0, "", sizeof(login->login_message0) - 1);
            strncpy(
                login->login_message1,
                "Please wait and try again.",
                sizeof(login->login_message1) - 1);
            return 1; // Indicate retry needed
        }
        else if( reply == 2 || reply == 18 || reply == 19 )
        {
            // Success
            login->staffmodlevel = 0;
            if( reply == 18 )
            {
                login->staffmodlevel = 1;
            }
            else if( reply == 19 )
            {
                login->staffmodlevel = 2;
            }

            login->ingame = true;
            login->state = LCLOGIN_STATE_SUCCESS;
            return 1;
        }
        else if( reply == 3 )
        {
            login->login_message0[0] = '\0';
            strncpy(
                login->login_message1,
                "Invalid username or password.",
                sizeof(login->login_message1) - 1);
            login->login_message1[sizeof(login->login_message1) - 1] = '\0';
        }
        else if( reply == 4 )
        {
            strncpy(
                login->login_message0,
                "Your account has been disabled.",
                sizeof(login->login_message0) - 1);
            login->login_message0[sizeof(login->login_message0) - 1] = '\0';
            strncpy(
                login->login_message1,
                "Please check your message-centre for details.",
                sizeof(login->login_message1) - 1);
            login->login_message1[sizeof(login->login_message1) - 1] = '\0';
        }
        else if( reply == 5 )
        {
            strncpy(
                login->login_message0,
                "Your account is already logged in.",
                sizeof(login->login_message0) - 1);
            login->login_message0[sizeof(login->login_message0) - 1] = '\0';
            strncpy(
                login->login_message1,
                "Try again in 60 secs...",
                sizeof(login->login_message1) - 1);
            login->login_message1[sizeof(login->login_message1) - 1] = '\0';
        }
        else if( reply == 6 )
        {
            strncpy(
                login->login_message0,
                "RuneScape has been updated!",
                sizeof(login->login_message0) - 1);
            login->login_message0[sizeof(login->login_message0) - 1] = '\0';
            strncpy(
                login->login_message1,
                "Please reload this page.",
                sizeof(login->login_message1) - 1);
            login->login_message1[sizeof(login->login_message1) - 1] = '\0';
        }
        else if( reply == 7 )
        {
            strncpy(
                login->login_message0, "This world is full.", sizeof(login->login_message0) - 1);
            login->login_message0[sizeof(login->login_message0) - 1] = '\0';
            strncpy(
                login->login_message1,
                "Please use a different world.",
                sizeof(login->login_message1) - 1);
            login->login_message1[sizeof(login->login_message1) - 1] = '\0';
        }
        else if( reply == 8 )
        {
            strncpy(login->login_message0, "Unable to connect.", sizeof(login->login_message0) - 1);
            login->login_message0[sizeof(login->login_message0) - 1] = '\0';
            strncpy(
                login->login_message1, "Login server offline.", sizeof(login->login_message1) - 1);
            login->login_message1[sizeof(login->login_message1) - 1] = '\0';
        }
        else if( reply == 9 )
        {
            strncpy(
                login->login_message0, "Login limit exceeded.", sizeof(login->login_message0) - 1);
            login->login_message0[sizeof(login->login_message0) - 1] = '\0';
            strncpy(
                login->login_message1,
                "Too many connections from your address.",
                sizeof(login->login_message1) - 1);
            login->login_message1[sizeof(login->login_message1) - 1] = '\0';
        }
        else if( reply == 10 )
        {
            strncpy(login->login_message0, "Unable to connect.", sizeof(login->login_message0) - 1);
            login->login_message0[sizeof(login->login_message0) - 1] = '\0';
            strncpy(login->login_message1, "Bad session id.", sizeof(login->login_message1) - 1);
            login->login_message1[sizeof(login->login_message1) - 1] = '\0';
        }
        else if( reply == 11 )
        {
            strncpy(
                login->login_message1,
                "Login server rejected session.",
                sizeof(login->login_message1) - 1);
            login->login_message1[sizeof(login->login_message1) - 1] = '\0';
            strncpy(login->login_message1, "Please try again.", sizeof(login->login_message1) - 1);
            login->login_message1[sizeof(login->login_message1) - 1] = '\0';
        }
        else if( reply == 12 )
        {
            strncpy(
                login->login_message0,
                "You need a members account to login to this world.",
                sizeof(login->login_message0) - 1);
            login->login_message0[sizeof(login->login_message0) - 1] = '\0';
            strncpy(
                login->login_message1,
                "Please subscribe, or use a different world.",
                sizeof(login->login_message1) - 1);
            login->login_message1[sizeof(login->login_message1) - 1] = '\0';
        }
        else if( reply == 13 )
        {
            strncpy(
                login->login_message0,
                "Could not complete login.",
                sizeof(login->login_message0) - 1);
            login->login_message0[sizeof(login->login_message0) - 1] = '\0';
            strncpy(
                login->login_message1,
                "Please try using a different world.",
                sizeof(login->login_message1) - 1);
            login->login_message1[sizeof(login->login_message1) - 1] = '\0';
        }
        else if( reply == 14 )
        {
            strncpy(
                login->login_message0,
                "The server is being updated.",
                sizeof(login->login_message0) - 1);
            login->login_message0[sizeof(login->login_message0) - 1] = '\0';
            strncpy(
                login->login_message1,
                "Please wait 1 minute and try again.",
                sizeof(login->login_message1) - 1);
            login->login_message1[sizeof(login->login_message1) - 1] = '\0';
        }
        else if( reply == 15 )
        {
            // Scene loading - special case
            login->ingame = true;
            login->state = LCLOGIN_STATE_SUCCESS;
            return 1;
        }
        else if( reply == 16 )
        {
            strncpy(
                login->login_message0,
                "Login attempts exceeded.",
                sizeof(login->login_message0) - 1);
            login->login_message0[sizeof(login->login_message0) - 1] = '\0';
            strncpy(
                login->login_message1,
                "Please wait 1 minute and try again.",
                sizeof(login->login_message1) - 1);
            login->login_message1[sizeof(login->login_message1) - 1] = '\0';
        }
        else if( reply == 17 )
        {
            strncpy(
                login->login_message0,
                "You are standing in a members-only area.",
                sizeof(login->login_message0) - 1);
            login->login_message0[sizeof(login->login_message0) - 1] = '\0';
            strncpy(
                login->login_message1,
                "To play on this world move to a free area first",
                sizeof(login->login_message1) - 1);
            login->login_message1[sizeof(login->login_message1) - 1] = '\0';
        }
        else if( reply == 20 )
        {
            strncpy(
                login->login_message0,
                "Invalid loginserver requested",
                sizeof(login->login_message0) - 1);
            login->login_message0[sizeof(login->login_message0) - 1] = '\0';
            strncpy(
                login->login_message1,
                "Please try using a different world.",
                sizeof(login->login_message1) - 1);
            login->login_message1[sizeof(login->login_message1) - 1] = '\0';
        }
        else
        {
            strncpy(
                login->login_message0,
                "Unexpected server response",
                sizeof(login->login_message0) - 1);
            login->login_message0[sizeof(login->login_message0) - 1] = '\0';
            strncpy(
                login->login_message1,
                "Please try using a different world.",
                sizeof(login->login_message1) - 1);
            login->login_message1[sizeof(login->login_message1) - 1] = '\0';
        }

        login->state = LCLOGIN_STATE_ERROR;
        return 1;
    }

    case LCLOGIN_STATE_SUCCESS:
        return 1;

    case LCLOGIN_STATE_ERROR:
        return 1;

    default:
        login->state = LCLOGIN_STATE_ERROR;
        return -1;
    }

    return 0;
}

void
lclogin_cleanup(struct LCLogin* login)
{
    if( !login )
        return;

    if( login->username )
    {
        free(login->username);
        login->username = NULL;
    }

    if( login->password )
    {
        free(login->password);
        login->password = NULL;
    }

    if( login->random_out )
    {
        isaac_free(login->random_out);
        login->random_out = NULL;
    }

    if( login->random_in )
    {
        isaac_free(login->random_in);
        login->random_in = NULL;
    }

    login->rsa_loaded = false;

    if( login->socket_fd >= 0 )
    {
        close(login->socket_fd);
        login->socket_fd = -1;
    }
}

lclogin_state_t
lclogin_get_state(const struct LCLogin* login)
{
    return login ? login->state : LCLOGIN_STATE_IDLE;
}

lclogin_result_t
lclogin_get_result(const struct LCLogin* login)
{
    return login ? login->result : LCLOGIN_RESULT_UNEXPECTED;
}

const char*
lclogin_get_message0(const struct LCLogin* login)
{
    return login ? login->login_message0 : "";
}

const char*
lclogin_get_message1(const struct LCLogin* login)
{
    return login ? login->login_message1 : "";
}

void
lclogin_set_socket(
    struct LCLogin* login,
    int socket_fd)
{
    if( login )
    {
        login->socket_fd = socket_fd;
    }
}
