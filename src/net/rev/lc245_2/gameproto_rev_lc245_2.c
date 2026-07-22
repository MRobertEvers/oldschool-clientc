#include "net/rev/gameproto_revisions.h"
#include "packetin.h"

static int
rev_packetin_size(int packet_type)
{
    return packetin_size_lc245_2(packet_type);
}

static int
rev_packetin_code(int packet_type)
{
    return packetin_code_lc245_2(packet_type);
}

/* Jag-archive CRCs of the rev 245_2 Lost City build (v0 loginproto values;
 * "TODO get from server" carried over — the CRC handshake fetch is future
 * work, so a different server build rejects login until then). */
static struct GameProtoRevTable const k_rev_lc245_2 = {
    .revision = GAMEPROTO_REVISION_LC245_2,
    .name = "lc245_2",
    .client_version = 245,
    .jag_checksum = { 0, -945108033, -323580723, 1539972921, -259567598,
                      260912122, -1840622973, -87627495, -1625923170 },
    .packetin_size = rev_packetin_size,
    .packetin_code = rev_packetin_code,
};

struct GameProtoRevTable const*
GameProtoRev_LC245_2(void)
{
    return &k_rev_lc245_2;
}
