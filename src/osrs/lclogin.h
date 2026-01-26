#ifndef OSRS_LCLOGIN_H
#define OSRS_LCLOGIN_H

#include "rscache/rsbuf.h"
#include "isaac.h"

#include <stdbool.h>
#include <stdint.h>

#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/err.h>

// Login state machine states
typedef enum
{
    LCLOGIN_STATE_IDLE,
    LCLOGIN_STATE_INIT,
    LCLOGIN_STATE_CONNECTING,
    LCLOGIN_STATE_SENDING_LOGIN_SERVER,
    LCLOGIN_STATE_READING_INITIAL_RESPONSE,
    LCLOGIN_STATE_READING_REPLY,
    LCLOGIN_STATE_PROCESSING_REPLY_0,
    LCLOGIN_STATE_SENDING_CREDENTIALS,
    LCLOGIN_STATE_READING_FINAL_REPLY,
    LCLOGIN_STATE_HANDLING_REPLY,
    LCLOGIN_STATE_SUCCESS,
    LCLOGIN_STATE_ERROR,
} lclogin_state_t;

// Login result codes (matching server reply codes)
typedef enum
{
    LCLOGIN_RESULT_SUCCESS = 2,
    LCLOGIN_RESULT_SUCCESS_MOD = 18,
    LCLOGIN_RESULT_SUCCESS_ADMIN = 19,
    LCLOGIN_RESULT_RETRY = 1,
    LCLOGIN_RESULT_INVALID_CREDENTIALS = 3,
    LCLOGIN_RESULT_ACCOUNT_DISABLED = 4,
    LCLOGIN_RESULT_ALREADY_LOGGED_IN = 5,
    LCLOGIN_RESULT_UPDATED = 6,
    LCLOGIN_RESULT_WORLD_FULL = 7,
    LCLOGIN_RESULT_SERVER_OFFLINE = 8,
    LCLOGIN_RESULT_LOGIN_LIMIT = 9,
    LCLOGIN_RESULT_BAD_SESSION = 10,
    LCLOGIN_RESULT_REJECTED_SESSION = 11,
    LCLOGIN_RESULT_MEMBERS_REQUIRED = 12,
    LCLOGIN_RESULT_COULD_NOT_COMPLETE = 13,
    LCLOGIN_RESULT_SERVER_UPDATING = 14,
    LCLOGIN_RESULT_SCENE_LOADING = 15,
    LCLOGIN_RESULT_ATTEMPTS_EXCEEDED = 16,
    LCLOGIN_RESULT_MEMBERS_AREA = 17,
    LCLOGIN_RESULT_INVALID_LOGSERVER = 20,
    LCLOGIN_RESULT_UNEXPECTED = 255,
} lclogin_result_t;

// Forward declarations
struct ClientStream;

// Login context structure
struct LCLogin
{
    lclogin_state_t state;
    lclogin_result_t result;
    bool reconnect;

    // Credentials
    char* username;
    char* password;

    // Network
    struct ClientStream* stream;
    int socket_fd;

    // Buffers
    struct RSBuffer out;
    struct RSBuffer in;
    struct RSBuffer loginout;
    uint8_t out_data[512];
    uint8_t in_data[512];
    uint8_t loginout_data[512];

    // State machine counters
    int initial_response_bytes_read;
    int credentials_sent;
    int read_bytes_needed;
    int read_bytes_received;

    // Login data
    uint64_t server_seed;
    int32_t seed[4];
    struct Isaac* random_out;
    struct Isaac* random_in;

    // RSA keys (loaded from PEM files)
    RSA* rsa_public_key;
    RSA* rsa_private_key;

    // Client version and checksums
    int client_version;
    int32_t jag_checksum[9];
    bool low_memory;

    // Status messages
    char login_message0[256];
    char login_message1[256];

    // Game state (set on successful login)
    int staffmodlevel;
    bool ingame;
};

// Load RSA public key from PEM file
// Returns 0 on success, -1 on error
int
lclogin_load_rsa_public_key(
    struct LCLogin* login,
    const char* pem_file_path);

// Load RSA public key from modulus and exponent (as decimal strings)
// Returns 0 on success, -1 on error
int
lclogin_load_rsa_public_key_from_values(
    struct LCLogin* login,
    const char* modulus_str,
    const char* exponent_str);

// Load RSA public key from environment variables (LOGIN_RSAN, LOGIN_RSAE) with defaults
// Returns 0 on success, -1 on error
int
lclogin_load_rsa_public_key_from_env(struct LCLogin* login);

// Load RSA private key from PEM file
// Returns 0 on success, -1 on error
int
lclogin_load_rsa_private_key(
    struct LCLogin* login,
    const char* pem_file_path);

// Initialize login state machine
void
lclogin_init(
    struct LCLogin* login,
    int client_version,
    int32_t* jag_checksum,
    bool low_memory);

// Start login process
void
lclogin_start(
    struct LCLogin* login,
    const char* username,
    const char* password,
    bool reconnect);

// Process login state machine (call this repeatedly until state is SUCCESS or ERROR)
// Returns 0 if more processing needed, 1 if complete, -1 on error
int
lclogin_process(
    struct LCLogin* login);

// Cleanup
void
lclogin_cleanup(struct LCLogin* login);

// Get current state
lclogin_state_t
lclogin_get_state(const struct LCLogin* login);

// Get result
lclogin_result_t
lclogin_get_result(const struct LCLogin* login);

// Get status messages
const char*
lclogin_get_message0(const struct LCLogin* login);
const char*
lclogin_get_message1(const struct LCLogin* login);

// Set socket file descriptor (call this before starting login if socket is opened externally)
void
lclogin_set_socket(struct LCLogin* login, int socket_fd);

#endif // OSRS_LCLOGIN_H
