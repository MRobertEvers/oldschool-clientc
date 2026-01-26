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
 *   3. Start login: lclogin_start(&login, username, password, reconnect)
 *   4. Process in loop:
 *      - Call lclogin_process(&login) to advance state machine
 *      - Check lclogin_get_bytes_needed(&login) to see if data is needed
 *      - Provide received data via lclogin_provide_data(&login, data, size)
 *      - Check lclogin_get_data_to_send(&login, &data) to get data to send
 *      - Repeat until lclogin_process returns non-zero
 *   5. Check result: lclogin_get_state(&login) and lclogin_get_result(&login)
 *   6. Cleanup: lclogin_cleanup(&login)
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

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

    // Initialize pending send/receive buffers
    login->pending_send_data = NULL;
    login->pending_send_size = 0;
    login->pending_send_sent = 0;
    login->pending_receive_needed = 0;
    login->pending_receive_received = 0;
    login->first_byte_printed = false;
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

        // Set up pending send if not already set
        if( !login->pending_send_data )
        {
            login->pending_send_data = login->out.data;
            login->pending_send_size = 2;
            login->pending_send_sent = 0;
        }

        // Check if all data has been sent
        if( login->pending_send_sent >= login->pending_send_size )
        {
            login->state = LCLOGIN_STATE_READING_INITIAL_RESPONSE;
            login->initial_response_bytes_read = 0;
            login->pending_send_data = NULL;
            login->pending_send_size = 0;
            login->pending_send_sent = 0;
            login->pending_receive_needed = 8;
            login->pending_receive_received = 0;
            // Fall through
        }
        else
        {
            return 0; // Still waiting for data to be sent
        }
    }
        __attribute__((fallthrough));

    case LCLOGIN_STATE_READING_INITIAL_RESPONSE:
    {
        // Need 8 bytes
        if( login->initial_response_bytes_read < 8 )
        {
            // Set pending receive if not already set
            if( login->pending_receive_needed == 0 )
            {
                login->pending_receive_needed = 8;
                login->pending_receive_received = 0;
            }
            // Wait for data to be provided via lclogin_provide_data
            return 0;
        }

        // Have all 8 bytes, move to next state
        login->state = LCLOGIN_STATE_READING_REPLY;
        login->pending_receive_needed = 1;
        login->pending_receive_received = 0;
        // Fall through
    }
        __attribute__((fallthrough));

    case LCLOGIN_STATE_READING_REPLY:
    {
        // Need 1 byte for reply
        if( login->pending_receive_received < 1 )
        {
            // Wait for data to be provided via lclogin_provide_data
            return 0;
        }

        // Reply byte should be in login->in.data[8] (after initial 8 bytes)
        uint8_t reply_byte = login->in.data[8];
        int reply = reply_byte & 0xFF;

        if( reply == 0 )
        {
            // Need to read server seed and send credentials
            login->state = LCLOGIN_STATE_PROCESSING_REPLY_0;
            login->read_bytes_received = 0;
            login->pending_receive_needed = 8;
            login->pending_receive_received = 0;
        }
        else
        {
            // Handle other replies directly
            login->state = LCLOGIN_STATE_HANDLING_REPLY;
            login->result = (lclogin_result_t)reply;
            login->pending_receive_needed = 0;
            login->pending_receive_received = 0;
        }
        // Fall through
    }
        __attribute__((fallthrough));

    case LCLOGIN_STATE_PROCESSING_REPLY_0:
    {
        // Read 8 bytes for server seed
        if( login->read_bytes_received < 8 )
        {
            // Wait for data to be provided via lclogin_provide_data
            return 0;
        }

        // Parse server seed (from bytes 9-16, after initial 8 bytes and reply byte)
        login->in.position = 9;
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
        login->pending_receive_needed = 0;
        login->pending_receive_received = 0;
        // Fall through
    }
        __attribute__((fallthrough));

    case LCLOGIN_STATE_SENDING_CREDENTIALS:
    {
        int total_size = login->loginout.position;

        // Set up pending send if not already set
        if( !login->pending_send_data && login->credentials_sent < total_size )
        {
            login->pending_send_data = login->loginout.data + login->credentials_sent;
            login->pending_send_size = total_size - login->credentials_sent;
            login->pending_send_sent = 0;
        }

        // Check if all data has been sent
        if( login->credentials_sent >= total_size )
        {
            login->state = LCLOGIN_STATE_READING_FINAL_REPLY;
            login->read_bytes_received = 0;
            login->pending_send_data = NULL;
            login->pending_send_size = 0;
            login->pending_send_sent = 0;
            login->pending_receive_needed = 1;
            login->pending_receive_received = 0;
            // Fall through
        }
        else
        {
            return 0; // Still waiting for data to be sent
        }
    }
        __attribute__((fallthrough));

    case LCLOGIN_STATE_READING_FINAL_REPLY:
    {
        // Need 1 byte for final reply
        if( login->pending_receive_received < 1 )
        {
            // Wait for data to be provided via lclogin_provide_data
            return 0;
        }

        // Reply byte should be in the receive buffer
        uint8_t reply_byte = login->in.data[17]; // After initial 8 + reply 1 + seed 8
        int reply = reply_byte & 0xFF;
        login->state = LCLOGIN_STATE_HANDLING_REPLY;
        login->result = (lclogin_result_t)reply;
        login->pending_receive_needed = 0;
        login->pending_receive_received = 0;
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
            login->pending_receive_needed = 1; // Ready to receive data
            login->pending_receive_received = 0;
            login->first_byte_printed = false; // Reset flag for new login
            // Continue to receive data after login
            return 0;
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
            login->pending_receive_needed = 1; // Ready to receive data
            login->pending_receive_received = 0;
            login->first_byte_printed = false; // Reset flag for new login
            // Continue to receive data after login
            return 0;
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
    {
        // Continue receiving data after successful login
        // Set up to receive data if not already set
        if( login->pending_receive_needed == 0 )
        {
            login->pending_receive_needed = 1; // Ready to receive at least 1 byte
            login->pending_receive_received = 0;
        }
        return 0; // Continue processing to receive data
    }

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

    login->pending_send_data = NULL;
    login->pending_send_size = 0;
    login->pending_send_sent = 0;
    login->pending_receive_needed = 0;
    login->pending_receive_received = 0;
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

int
lclogin_provide_data(
    struct LCLogin* login,
    const uint8_t* data,
    int data_size)
{
    if( !login || !data || data_size <= 0 )
    {
        return -1;
    }

    if( login->pending_receive_needed <= 0 )
    {
        return 0; // Not expecting data
    }

    int needed = login->pending_receive_needed - login->pending_receive_received;
    if( needed <= 0 )
    {
        return 0; // Already have all needed data
    }

    int to_copy = (data_size < needed) ? data_size : needed;

    // Determine where to copy based on current state
    uint8_t* dest = NULL;
    if( login->state == LCLOGIN_STATE_READING_INITIAL_RESPONSE )
    {
        dest = login->in.data + login->initial_response_bytes_read;
    }
    else if( login->state == LCLOGIN_STATE_READING_REPLY )
    {
        dest = login->in.data + 8; // After initial 8 bytes
    }
    else if( login->state == LCLOGIN_STATE_PROCESSING_REPLY_0 )
    {
        dest = login->in.data + 9 + login->read_bytes_received; // After initial 8 + reply byte
    }
    else if( login->state == LCLOGIN_STATE_READING_FINAL_REPLY )
    {
        dest = login->in.data + 17; // After initial 8 + reply 1 + seed 8
    }
    else if( login->state == LCLOGIN_STATE_SUCCESS )
    {
        // In success state, receive data and decode first byte using ISAAC
        // Print decoded first byte only once
        if( !login->first_byte_printed && to_copy > 0 && login->random_in )
        {
            int encrypted_byte = data[0] & 0xff;
            int isaac_value = isaac_next(login->random_in);
            int decoded_byte = (encrypted_byte - isaac_value) & 0xff;
            printf("First byte after login (encrypted): 0x%02x (%d)\n", encrypted_byte, encrypted_byte);
            printf("First byte after login (decoded): 0x%02x (%d)\n", decoded_byte, decoded_byte);
            login->first_byte_printed = true;
        }

        // Update received count
        login->pending_receive_received += to_copy;

        // Reset pending_receive_needed to continue receiving
        login->pending_receive_needed = 1;
        login->pending_receive_received = 0; // Reset for next chunk

        return to_copy;
    }
    else
    {
        return -1; // Invalid state for receiving data
    }

    memcpy(dest, data, to_copy);
    login->pending_receive_received += to_copy;

    // Update state-specific counters
    if( login->state == LCLOGIN_STATE_READING_INITIAL_RESPONSE )
    {
        login->initial_response_bytes_read += to_copy;
    }
    else if( login->state == LCLOGIN_STATE_PROCESSING_REPLY_0 )
    {
        login->read_bytes_received += to_copy;
    }

    return to_copy;
}

int
lclogin_get_data_to_send(
    struct LCLogin* login,
    const uint8_t** data)
{
    if( !login || !data )
    {
        return 0;
    }

    if( !login->pending_send_data || login->pending_send_size <= 0 )
    {
        *data = NULL;
        return 0;
    }

    int remaining = login->pending_send_size - login->pending_send_sent;
    if( remaining <= 0 )
    {
        *data = NULL;
        return 0;
    }

    *data = login->pending_send_data + login->pending_send_sent;
    return remaining;
}

int
lclogin_get_bytes_needed(const struct LCLogin* login)
{
    if( !login )
    {
        return 0;
    }

    // In success state, always ready to receive at least 1 byte
    if( login->state == LCLOGIN_STATE_SUCCESS )
    {
        return 1;
    }

    return login->pending_receive_needed - login->pending_receive_received;
}

// Mark data as sent (call this after successfully sending data from lclogin_get_data_to_send)
void
lclogin_mark_data_sent(
    struct LCLogin* login,
    int bytes_sent)
{
    if( !login || bytes_sent <= 0 )
    {
        return;
    }

    login->pending_send_sent += bytes_sent;

    // Update state-specific counters
    if( login->state == LCLOGIN_STATE_SENDING_LOGIN_SERVER )
    {
        // All data sent, state machine will transition on next process call
    }
    else if( login->state == LCLOGIN_STATE_SENDING_CREDENTIALS )
    {
        login->credentials_sent += bytes_sent;
    }
}
