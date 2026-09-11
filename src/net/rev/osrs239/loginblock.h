#ifndef SRC_NET_REV_OSRS239_LOGINBLOCK_H
#define SRC_NET_REV_OSRS239_LOGINBLOCK_H

/*
 * The revision-239 GAMELOGIN block, stated once for both ends.
 *
 * The client driver (src/net/loginproto_osrs239.c) writes it and the mock
 * server (src/torirsserver/mock239_login.c) reads it, and they must agree byte for
 * byte or the ISAAC seeds come out wrong and every packet after the handshake
 * is noise. Stating the layout here rather than twice in prose is the whole
 * point of the file: the transcription source is RSProt's LoginBlockDecoder
 * (osrs-239-shared/.../loginprot/incoming/codec/shared/LoginBlockDecoder.kt),
 * and there is exactly one copy of it.
 *
 * WIRE
 *
 *   p1   opcode 16 (GAMELOGIN)          -- 18 is GAMERECONNECT, not built here
 *   p2   payload length
 *   --- header, plaintext -----------------------------------------------
 *   p4   version            must equal 239 or the server answers CLIENT_OUT_OF_DATE
 *   p4   subVersion
 *   p4   serverVersion      NEW at 237; absent from the 230 block
 *   p1   clientType         rsprot LoginClientType: 1 desktop, 2 android,
 *                           3 ios, 4/5/10 enhanced windows/mac/linux,
 *                           7/8 enhanced android/ios (the client writes the
 *                           clienttype the cache scripts are told)
 *   p1   platformType       rsprot LoginPlatformType: 0 default, 1 steam,
 *                           2 android, 3 apple, 5 jagex
 *   p1   externalAuthType
 *   --- RSA envelope ----------------------------------------------------
 *   p2   rsaSize
 *   [rsaSize] RSA(e,n) over:
 *        p1   encryptionCheck == 1   (a wrong RSA key shows up here and only here)
 *        p4x4 seed[4]                ISAAC seeds
 *        p8   sessionId              echoed from INIT_GAME_CONNECTION
 *        p1   otpType   0 token / 1 remember / 2 none / 3 forget
 *        p4   otp payload (identity for 0, key+pad for 1 and 3, skipped for 2)
 *        p1   authType  0 password / 2 token
 *        pjstr password (or token)
 *   --- XTEA body, key = seed[4], to the end of the payload --------------
 *        pjstr username
 *        p1   packedClientSettings   bit0 lowDetail, bit1 resizable
 *        p2   width
 *        p2   height
 *        [24] uuid
 *        pjstr siteSettings
 *        p4   affiliate
 *        p1   deepLinkCount, then p4 * count
 *        HostPlatformStats            (see below -- ~30 fields, mostly zeroable)
 *        p1   secondClientType        need not equal the header's since 237
 *        p4   reflectionCheckerConst
 *        23 archive CRCs, in a scrambled order and four byte orders
 *
 * THE CRC BLOCK is the part worth stating explicitly, because it is the one
 * place where "looks right" and "is right" diverge silently. The 23 CRCs are
 * NOT written in index order and NOT all big-endian: RSProt reads them as the
 * table below, where the byte order is one of four:
 *
 *   g4      B3 B2 B1 B0   (big endian)
 *   g4Alt1  B0 B1 B2 B3   (little endian)
 *   g4Alt2  B2 B3 B0 B1
 *   g4Alt3  B1 B0 B3 B2
 *
 * Getting the permutation wrong does not fail the login -- the server compares
 * CRCs it may not enforce -- so the mistake surfaces much later as "the client
 * re-downloads an archive every boot".
 */

#include <stdint.h>

#define OSRS239_LOGIN_CRC_COUNT 23
#define OSRS239_LOGIN_UUID_BYTES 24

/** Byte orders used by the CRC block. Named for RSProt's accessors. */
enum Osrs239CrcOrder
{
    OSRS239_CRC_BE = 0,   /* g4     */
    OSRS239_CRC_LE = 1,   /* g4Alt1 */
    OSRS239_CRC_ALT2 = 2, /* g4Alt2 */
    OSRS239_CRC_ALT3 = 3, /* g4Alt3 */
};

/**
 * The CRC block in wire order: entry i of this table is the i-th int on the
 * wire, `.index` is which archive's CRC it carries and `.order` is how its four
 * bytes are arranged. Transcribed from LoginBlockDecoder.decodeCrc.
 */
struct Osrs239CrcSlot
{
    uint8_t index;
    uint8_t order;
};

static const struct Osrs239CrcSlot g_osrs239_crc_order[OSRS239_LOGIN_CRC_COUNT] = {
    { 18, OSRS239_CRC_ALT2 }, { 19, OSRS239_CRC_ALT2 }, { 22, OSRS239_CRC_BE },
    { 12, OSRS239_CRC_LE },   { 9, OSRS239_CRC_LE },    { 1, OSRS239_CRC_ALT2 },
    { 17, OSRS239_CRC_ALT2 }, { 16, OSRS239_CRC_ALT3 }, { 7, OSRS239_CRC_ALT3 },
    { 13, OSRS239_CRC_LE },   { 3, OSRS239_CRC_BE },    { 5, OSRS239_CRC_ALT2 },
    { 10, OSRS239_CRC_ALT3 }, { 11, OSRS239_CRC_BE },   { 2, OSRS239_CRC_ALT2 },
    { 8, OSRS239_CRC_LE },    { 0, OSRS239_CRC_ALT3 },  { 21, OSRS239_CRC_BE },
    { 4, OSRS239_CRC_ALT3 },  { 20, OSRS239_CRC_ALT2 }, { 6, OSRS239_CRC_LE },
    { 14, OSRS239_CRC_ALT3 }, { 15, OSRS239_CRC_LE },
};

/** Everything the server learns from a login block. */
struct Osrs239LoginBlock
{
    int32_t version;
    int32_t sub_version;
    int32_t server_version;
    int client_type;
    int platform_type;
    int external_auth_type;

    int32_t seed[4];
    uint64_t session_id;
    int auth_type;
    int otp_type;

    char username[64];
    char password[64];

    int low_detail;
    int resizable;
    int width;
    int height;
    uint8_t uuid[OSRS239_LOGIN_UUID_BYTES];
    char site_settings[64];
    int32_t affiliate;
    int second_client_type;
    int32_t reflection_checker_const;
    int32_t crc[OSRS239_LOGIN_CRC_COUNT];
};

/* Login prot opcodes (RSProt LoginClientProtId / LoginServerProtId). */
enum
{
    OSRS239_LOGIN_INIT_GAME_CONNECTION = 14,
    OSRS239_LOGIN_INIT_JS5REMOTE_CONNECTION = 15,
    OSRS239_LOGIN_GAMELOGIN = 16,
    OSRS239_LOGIN_GAMERECONNECT = 18,
    OSRS239_LOGIN_POW_REPLY = 19,
    OSRS239_LOGIN_REMAINING_BETA_ARCHIVE_HASHES = 32,
};

enum
{
    OSRS239_LOGINRES_SUCCESSFUL = 0,
    OSRS239_LOGINRES_OK = 2,
    OSRS239_LOGINRES_INVALID_USER_OR_PASS = 3,
    OSRS239_LOGINRES_CLIENT_OUT_OF_DATE = 6,
    OSRS239_LOGINRES_BAD_SESSION_ID = 10,
    /* Var-short framed, and the payload is the player-info init block — the
     * one thing a re-established session cannot rebuild for itself, because
     * no REBUILD_LOGIN follows it (RSProt LoginServerProt.RECONNECT_OK). */
    OSRS239_LOGINRES_RECONNECT_OK = 15,
    OSRS239_LOGINRES_INVALID_LOGIN_PACKET = 22,
    OSRS239_LOGINRES_LOGIN_FAIL_1 = 65,
    OSRS239_LOGINRES_PROOF_OF_WORK = 69,
};

/* Authentication / OTP discriminators inside the RSA block. */
enum
{
    OSRS239_OTP_TOKEN = 0,
    OSRS239_OTP_REMEMBER = 1,
    OSRS239_OTP_NONE = 2,
    OSRS239_OTP_FORGET = 3,

    OSRS239_AUTH_PASSWORD = 0,
    OSRS239_AUTH_TOKEN = 2,
};

#endif
