/*
 * Standalone TCP mock server for the OSRS rev-230 protocol. Accepts one client,
 * performs the 230 login handshake (RSA-decrypts the login block with the mock
 * private key, arms ISAAC), then sends REBUILD_NORMAL + a small on-login burst
 * and idles — no other interaction. Lets the real client
 *   src/torirs --manifest manifest_osrs230.ini --connect localhost:PORT
 * log in and build the world offline.
 *
 * Build:  make -C src mock230     Run:  src/build/mock230 [port]
 *
 * The RSA keypair is fixed (see mock230_rsa scratch / manifest_osrs230.ini): the
 * client encrypts the login block with (E=10001, N); the mock decrypts with
 * (D, N). Both are the same modexp (rsa_crypt).
 */
#include "net/isaac.h"
#include "net/rsa.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <rsbuffer.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Mock private key (D) + modulus (N); client uses (10001, N). */
static const char* MOCK_RSA_D =
    "795ae97353fb30c7d069891454220c40d0c006732e9180cd3b74f0fcd02a74980d363ffc0a19e8cfe13b7d556"
    "b8874e22576d0e3bb214b8bbfdcdee7295036a63ad32111d525f65546d3bae015c190654a52424f1a946d4135"
    "1f17a48e80fe0a33a0118c8c436f810f2585cf874828890d486c26ac1cd29a824d2fdac6032305";
static const char* MOCK_RSA_N =
    "c30fcbc01e071ff224ea1a6508052d1140f87abaf8f40f7004efa59926708e5d99e2bc832fdca8276482dd0d6"
    "90f644156850f47886f8032b3e9aa52508d24e8c9b7c50b8d8b8716fb8c3993bb6ce15e2124883edb7aaa7241"
    "a8b530f806c61cd1345879413fc105980a4f5fcdb3f0d743b14b16228b4d1496c83d3755a78a19";

#define SESSION_ID 0x0102030405060708ULL

/* Lumbridge-ish login spot. zone = tile>>3; scene base = (zone-6)*8. */
// #define LOGIN_ZONE_X 402
// #define LOGIN_ZONE_Z 402
#define LOGIN_ZONE_X 426
#define LOGIN_ZONE_Z 408

/* 230 server->client opcode -> name (RSProt GameServerProt), for debug. */
static const char*
opcode_name(int op)
{
    switch( op )
    {
    case 68:
        return "REBUILD_NORMAL";
    case 23:
        return "PLAYER_INFO";
    case 104:
        return "NPC_INFO_SMALL";
    case 0:
        return "SET_NPC_UPDATE_ORIGIN";
    case 35:
        return "VARP_SMALL";
    case 82:
        return "VARP_LARGE";
    case 7:
        return "VARP_RESET";
    case 88:
        return "VARP_SYNC";
    case 77:
        return "UPDATE_RUNENERGY";
    case 27:
        return "UPDATE_RUNWEIGHT";
    case 114:
        return "UPDATE_STAT_V2";
    case 10:
        return "UPDATE_INV_FULL";
    case 37:
        return "UPDATE_INV_PARTIAL";
    case 60:
        return "IF_OPENTOP";
    case 6:
        return "IF_OPENSUB";
    case 36:
        return "IF_CLOSESUB";
    case 94:
        return "IF_SETTEXT";
    case 47:
        return "IF_SETEVENTS";
    case 84:
        return "RUNCLIENTSCRIPT";
    case 90:
        return "MESSAGE_GAME";
    case 2:
        return "SET_MAP_FLAG";
    case 108:
        return "SERVER_TICK_END";
    default:
        return "?";
    }
}

static void
hex_preview(
    char* out,
    int out_cap,
    uint8_t const* data,
    int len)
{
    int n = len < 12 ? len : 12;
    int pos = 0;
    for( int i = 0; i < n && pos + 3 < out_cap; i++ )
        pos += snprintf(out + pos, (size_t)(out_cap - pos), "%02x ", data[i]);
    if( len > n && pos + 4 < out_cap )
        snprintf(out + pos, (size_t)(out_cap - pos), "...");
}

static int
read_full(
    int fd,
    uint8_t* buf,
    int n)
{
    int got = 0;
    while( got < n )
    {
        int r = (int)read(fd, buf + got, (size_t)(n - got));
        if( r <= 0 )
            return got;
        got += r;
    }
    return got;
}

/* Frame + ISAAC-scramble one server->client packet and send it. */
static void
send_packet(
    int fd,
    struct Isaac* enc,
    int opcode,
    uint8_t const* payload,
    int len,
    int var)
{
    uint8_t frame[8192];
    struct RSCache_Buffer buf;
    RSCache_BufferInit(&buf, frame, sizeof(frame));
    p1(&buf, (opcode + isaac_next(enc)) & 0xff);
    if( var == 1 )
        p1(&buf, len);
    else if( var == 2 )
        p2(&buf, len);
    if( len > 0 )
        pbuf(&buf, (uint8_t*)payload, len);
    write(fd, frame, (size_t)buf.position);

    /* Outbound debug: every server->client packet. */
    char hex[48];
    hex_preview(hex, sizeof(hex), payload, len);
    fprintf(
        stderr,
        "mock230: -> %-18s op=%-3d %-9s payload=%3d framed=%d  [%s]\n",
        opcode_name(opcode),
        opcode,
        var == 1   ? "var-u8"
        : var == 2 ? "var-u16"
                   : "fixed",
        len,
        (int)buf.position,
        hex);
}

static void
send_rebuild_normal(
    int fd,
    struct Isaac* enc)
{
    uint8_t body[4096];
    struct RSCache_Buffer buf;
    RSCache_BufferInit(&buf, body, sizeof(body));

    /* RSProt RebuildNormalEncoder: worldArea, zoneX(p2Alt2), zoneZ, keyCount,
     * keyCount*4 XTEA ints. Map squares span the 104-tile scene: base=(zone-6)*8. */
    p2(&buf, 0); /* worldArea */
    /* p2Alt2 zoneX: writeByte(v>>8); writeByte(v+128) */
    p1(&buf, (LOGIN_ZONE_X >> 8) & 0xff);
    p1(&buf, (LOGIN_ZONE_X + 128) & 0xff);
    p2(&buf, LOGIN_ZONE_Z);

    int base_x = (LOGIN_ZONE_X - 6) * 8;
    int base_z = (LOGIN_ZONE_Z - 6) * 8;
    int sq_x0 = base_x >> 6, sq_x1 = (base_x + 103) >> 6;
    int sq_z0 = base_z >> 6, sq_z1 = (base_z + 103) >> 6;
    int count = (sq_x1 - sq_x0 + 1) * (sq_z1 - sq_z0 + 1);
    p2(&buf, count);
    /* Zero keys for now (unencrypted regions load; real keys wired later). */
    for( int i = 0; i < count * 4; i++ )
        p4(&buf, 0);

    send_packet(fd, enc, 68, body, buf.position, 2);
    fprintf(
        stderr,
        "mock230: sent REBUILD_NORMAL zoneX=%d zoneZ=%d squares=%d\n",
        LOGIN_ZONE_X,
        LOGIN_ZONE_Z,
        count);
}

/* Payload builder scratch. */
static uint8_t g_body[8192];

/* RSProt Jagex byte-order alt transforms (see JagexByteBufExtensions.kt). We
 * only have big-endian p1/p2/p4 in rsbuffer, so spell the alts out by byte. */
static void
p1alt2(
    struct RSCache_Buffer* b,
    int v) /* writeByte(-v) */
{
    p1(b, (-v) & 0xff);
}
static void
p2alt1(
    struct RSCache_Buffer* b,
    int v) /* writeByte(v); writeByte(v>>8) : [lo,hi] */
{
    p1(b, v & 0xff);
    p1(b, (v >> 8) & 0xff);
}
static void
p2alt2(
    struct RSCache_Buffer* b,
    int v) /* writeByte(v>>8); writeByte(v+128) : [hi,lo+128] */
{
    p1(b, (v >> 8) & 0xff);
    p1(b, (v + 128) & 0xff);
}
static void
p4alt1(
    struct RSCache_Buffer* b,
    int v) /* writeIntLE : [b0,b1,b2,b3] */
{
    p1(b, v & 0xff);
    p1(b, (v >> 8) & 0xff);
    p1(b, (v >> 16) & 0xff);
    p1(b, (v >> 24) & 0xff);
}
static void
p4alt3(
    struct RSCache_Buffer* b,
    int v) /* [b2,b3,b0,b1] : (v>>16),(v>>24),v,(v>>8) */
{
    p1(b, (v >> 16) & 0xff);
    p1(b, (v >> 24) & 0xff);
    p1(b, v & 0xff);
    p1(b, (v >> 8) & 0xff);
}

static void
send_if_opentop(
    int fd,
    struct Isaac* enc,
    int root_group)
{
    /* RSProt IfOpenTopEncoder: interfaceId as p2Alt1. */
    struct RSCache_Buffer buf;
    RSCache_BufferInit(&buf, g_body, sizeof(g_body));
    p2alt1(&buf, root_group);
    send_packet(fd, enc, 60, g_body, buf.position, 0); /* fixed 2 */
}

/* Mount sub-interface `group` (type: 0 modal, 1 overlay, 3 tab) into component
 * `child` of root `parent`. RSProt IfOpenSubEncoder wire order:
 *   p1 type, p2Alt2 interfaceId, p4Alt3 destinationCombinedId(parent<<16|child). */
static void
send_if_opensub(
    int fd,
    struct Isaac* enc,
    int parent,
    int child,
    int group,
    int type)
{
    struct RSCache_Buffer buf;
    RSCache_BufferInit(&buf, g_body, sizeof(g_body));
    p1(&buf, type);
    p2alt2(&buf, group);
    p4alt3(&buf, (parent << 16) | child);
    send_packet(fd, enc, 6, g_body, buf.position, 0); /* fixed 7 */
}

/* RSProt IfSetEventsEncoder wire order:
 *   p4Alt3 combinedId, p2Alt2 start, p4Alt1 events, p2 end. */
static void
send_if_setevents(
    int fd,
    struct Isaac* enc,
    int uid,
    int from,
    int to,
    int events)
{
    struct RSCache_Buffer buf;
    RSCache_BufferInit(&buf, g_body, sizeof(g_body));
    p4alt3(&buf, uid);
    p2alt2(&buf, from);
    p4alt1(&buf, events);
    p2(&buf, to);
    send_packet(fd, enc, 47, g_body, buf.position, 0); /* fixed 12 */
}

static void
send_varp_small(
    int fd,
    struct Isaac* enc,
    int id,
    int val)
{
    struct RSCache_Buffer buf;
    RSCache_BufferInit(&buf, g_body, sizeof(g_body));
    p2(&buf, id);
    p1(&buf, val & 0xff);
    send_packet(fd, enc, 35, g_body, buf.position, 0); /* fixed 3 */
}

static void
send_stat(
    int fd,
    struct Isaac* enc,
    int skill,
    int level,
    int xp)
{
    struct RSCache_Buffer buf;
    RSCache_BufferInit(&buf, g_body, sizeof(g_body));
    p1(&buf, skill);
    p1(&buf, level);
    p4(&buf, xp);
    p1(&buf, level);                                    /* boosted level */
    send_packet(fd, enc, 114, g_body, buf.position, 0); /* fixed 7 */
}

/* One slot in an UPDATE_INV_FULL payload. obj_id < 0 means empty. */
struct MockInvSlot
{
    int obj_id;
    int count;
};

/* RSProt UpdateInvFullEncoder: pCombinedId, p2 inventoryId, p2 capacity, then
 * per slot p1Alt2(count) [+ p4Alt1 if count>=255] + p2(id+1). Empty = Alt2(0),
 * p2(0). capacity is the slot count written (backpack = 28). */
static void
send_inv_full(
    int fd,
    struct Isaac* enc,
    int component,
    int container,
    struct MockInvSlot const* slots,
    int capacity)
{
    struct RSCache_Buffer buf;
    RSCache_BufferInit(&buf, g_body, sizeof(g_body));
    p4(&buf, component); /* pCombinedId */
    p2(&buf, container);
    p2(&buf, capacity);
    for( int i = 0; i < capacity; i++ )
    {
        int obj_id = slots ? slots[i].obj_id : -1;
        int count = slots ? slots[i].count : 0;
        if( obj_id < 0 || count <= 0 )
        {
            p1alt2(&buf, 0);
            p2(&buf, 0);
            continue;
        }
        int wire_count = count > 0xff ? 0xff : count;
        p1alt2(&buf, wire_count);
        if( count >= 255 )
            p4alt1(&buf, count);
        p2(&buf, obj_id + 1);
    }
    send_packet(fd, enc, 10, g_body, buf.position, 2); /* var-short */
}

static void
send_message_game(
    int fd,
    struct Isaac* enc,
    const char* text)
{
    struct RSCache_Buffer buf;
    RSCache_BufferInit(&buf, g_body, sizeof(g_body));
    p1(&buf, 0); /* type */
    pjstr(&buf, text, 0);
    send_packet(fd, enc, 90, g_body, buf.position, 1); /* var-byte */
}

/* --- PLAYER_INFO (GPI) : classic/Kronos-style bitstream the client's
 * pkt_player_info_reader_read decodes. Places the local player + appearance. */

static uint8_t g_gpi[1024];
static int g_gpi_bit;

static void
gpi_reset(void)
{
    memset(g_gpi, 0, sizeof(g_gpi));
    g_gpi_bit = 0;
}

/* Write `count` bits of `val`, MSB-first (matches Net_BitBufferGbits). */
static void
gpi_bits(
    int count,
    int val)
{
    for( int i = count - 1; i >= 0; i-- )
    {
        int byte = g_gpi_bit >> 3;
        int bit = 7 - (g_gpi_bit & 7);
        if( val & (1 << i) )
            g_gpi[byte] |= (uint8_t)(1 << bit);
        g_gpi_bit++;
    }
}

/* Default naked-male appearance blob (lc254 layout PktPlayerAppearance_Decode
 * reads): gender, headicon, 12 slots, 5 colours, 7 anim shorts, name37, combat. */
static int
build_appearance(uint8_t* out)
{
    struct RSCache_Buffer buf;
    RSCache_BufferInit(&buf, out, 256);
    p1(&buf, 0); /* gender: male */
    p1(&buf, 0); /* head icon */

    /* 12 slots [hat,cape,amulet,weapon,torso,shield,arms,legs,hair,hands,feet,jaw].
     * Empty -> 0x00; body part -> 0x100 + idk (two bytes). */
    static const int slots[12] = {
        0,          0,          0,         0,          0x100 + 18, 0,
        0x100 + 26, 0x100 + 36, 0x100 + 0, 0x100 + 33, 0x100 + 42, 0x100 + 10,
    };
    for( int i = 0; i < 12; i++ )
    {
        if( slots[i] == 0 )
            p1(&buf, 0);
        else
            p2(&buf, slots[i]);
    }

    for( int i = 0; i < 5; i++ )
        p1(&buf, 0); /* body colours */

    /* 7 animation ids (idle/turn/walk/walkb/walkl/walkr/run) — standard human. */
    static const int anims[7] = { 808, 823, 819, 820, 821, 822, 824 };
    for( int i = 0; i < 7; i++ )
        p2(&buf, anims[i]);

    p4(&buf, 0);
    p4(&buf, 0); /* name37 = 0 (empty) */
    p1(&buf, 3); /* combat level */
    return buf.position;
}

static void
send_player_info(
    int fd,
    struct Isaac* enc)
{
    gpi_reset();

    /* Local player: info=1, moveType=3 (teleport), level, scene-local x/z, jump,
     * then hasExtended=1 so appearance follows. scene_off for zone 402 is 32, so
     * scene tile = 32 + 20 = 52 (scene centre, on the loaded terrain). */
    gpi_bits(1, 1);  /* info */
    gpi_bits(2, 3);  /* moveType = teleport */
    gpi_bits(2, 0);  /* level */
    gpi_bits(7, 20); /* scene-local x */
    gpi_bits(7, 20); /* scene-local z */
    gpi_bits(1, 1);  /* jump */
    gpi_bits(1, 1);  /* has extended info (appearance) */

    gpi_bits(8, 0);     /* tracked player count = 0 */
    gpi_bits(11, 2047); /* new-player list terminator */

    /* Byte-align, then the extended-info section for the local player. */
    int pos = (g_gpi_bit + 7) >> 3;
    uint8_t app[256];
    int app_len = build_appearance(app);
    g_gpi[pos++] = 0x01; /* mask = APPEARANCE */
    g_gpi[pos++] = (uint8_t)app_len;
    memcpy(g_gpi + pos, app, (size_t)app_len);
    pos += app_len;

    send_packet(fd, enc, 23, g_gpi, pos, 2); /* PLAYER_INFO, var-short */
}

/*
 * Full on-login burst, matching Kronos's order (rebuild -> interfaces ->
 * masks -> vars/energy/weight -> containers -> stats -> message -> player/npc
 * info -> tick-end), with the real rev-230 opcodes (RSProt GameServerProt).
 * Only REBUILD_NORMAL is decoded by the client today; the rest frame cleanly
 * and drop, so payloads are plausible fillers with table-correct sizes.
 */
static void
send_login_burst(
    int fd,
    struct Isaac* enc)
{
    /* 1. Map region. */
    send_rebuild_normal(fd, enc);

    /* 2. Root gameframe + sidebar tab panels. The client boots the resizable
     *    gameframe root 161 (manifest ui:boot interface_id=161), so open 161 as
     *    the top and mount each tab's cache interface into 161's tab-content
     *    slots. Those slots are components 76..89 of interface 161 (14 layers
     *    under component 75 — one per sidebar tab, index 0..13), confirmed from
     *    the baked 161 tree (TORIRS_DUMP_TREE). Tab->panel-archive mapping is the
     *    standard OSRS gameframe layout (Kronos DisplayHandler order). type=1
     *    (overlay), matching the display-burst mounts Kronos sends. */
    send_if_opentop(fd, enc, 161);

    /* {sidebar-tab index (0=combat..13=music) -> panel interface archive}.
     *
     * All 14 slots must be sent: the gameframe shows a tab's icon and stone only
     * when if_hassub() reports something mounted in its slot (see
     * [proc,toplevel_sidebuttons_enable]), so a slot we never open is a tab that
     * simply does not exist on screen — no varbit gates them. Slots 83/84/86 were
     * missing, which is exactly why the clan, account and logout tabs never
     * appeared.
     *
     * Group ids are the rev-230 set. Two were previously wrong: 78 was 720, which
     * is a *warning/confirmation dialogue* interface ("Warning!", "Be careful!
     * It doesn't...") and not the quest journal at all — that is why the quest tab
     * rendered unrelated content; and 87 was 261, a 3-file stub, where rev 230
     * puts the real 141-file settings interface at 116. */
    static const struct
    {
        int slot;  /* 161 tab-content component id */
        int group; /* panel interface archive */
        const char* name;
    } tabs[] = {
        { 76, 593, "combat"    },
        { 77, 320, "stats"     },
        { 78, 629, "quest"     },
        { 79, 149, "inventory" },
        { 80, 387, "equipment" },
        { 81, 541, "prayer"    },
        { 82, 218, "magic"     },
        { 83, 7,   "clan"      },
        { 84, 109, "account"   },
        { 85, 429, "friends"   },
        { 86, 182, "logout"    },
        { 87, 116, "settings"  },
        { 88, 216, "emotes"    },
        { 89, 239, "music"     },
    };
    for( size_t i = 0; i < sizeof(tabs) / sizeof(tabs[0]); i++ )
        send_if_opensub(fd, enc, 161, tabs[i].slot, tabs[i].group, 1);

    /* 2b. TODO(quest journal): 629 is only a tab strip plus an EMPTY content
     *     container at 629|43 — the journal body is a separate interface and the
     *     CS2 never mounts it (script 2800, the tab switcher, only swaps the
     *     tab-button sprites), so the server owns that mount:
     *
     *         send_if_opensub(fd, enc, 629, 43, 399, 1);
     *         send_varp_small(fd, enc, 1141, 1 << 4);  // varbit 8168 = 1
     *
     *     Sub-tab -> content group: 0=712 summary, 1=399 quest list, 2=259
     *     diaries, 3=187 paths, 4=656 leagues; the selector is varbit 8168 =
     *     varp 1141 bits 4..6, whose unset value 0 means "character summary".
     *
     *     Left disabled for now because it does not render yet and it trips a
     *     stack overflow in the quest-list builder (script 1352 via 1350). Root
     *     cause to chase first: the client resolves 629|0 to 500x1 at x=724
     *     (off-screen) where the cache has 765x503 — the panel geometry is being
     *     mis-resolved, so mounting the body into it cannot work regardless. */

    /* 3. Access mask (IF_SETEVENTS) — unlock inventory slot clicks. The inventory
     *    container component lives inside interface 149. */
    send_if_setevents(fd, enc, (149 << 16) | 0, 0, 27, 0x3fe);

    /* 4. A few config varps. */
    send_varp_small(fd, enc, 1737, 0);
    send_varp_small(fd, enc, 261, 10);

    /* 5. Run energy + weight. */
    {
        uint8_t b[2] = { 100, 0 };
        send_packet(fd, enc, 77, b, 2, 0);
    }
    {
        uint8_t b[2] = { 0, 0 };
        send_packet(fd, enc, 27, b, 2, 0);
    }

    /* 6. Inventory (container 93) + equipment (container 94). Backpack gets an
     *    abyssal whip (obj 4151) in slot 0; remaining 27 slots + equipment empty. */
    {
        struct MockInvSlot backpack[28];
        for( int i = 0; i < 28; i++ )
        {
            backpack[i].obj_id = -1;
            backpack[i].count = 0;
        }
        backpack[0].obj_id = 4151;
        backpack[0].count = 1;
        send_inv_full(fd, enc, (149 << 16) | 0, 93, backpack, 28);
    }
    send_inv_full(fd, enc, (387 << 16) | 0, 94, NULL, 0);

    /* 7. All 23 skills at level 1. */
    for( int skill = 0; skill < 23; skill++ )
        send_stat(fd, enc, skill, 1, 121);

    /* 8. Welcome message. */
    send_message_game(fd, enc, "Welcome to the mock 230 world.");

    /* 9. Player info (GPI) — real classic bitstream placing the local player
     *    with appearance. The client's serial packet queue guarantees REBUILD
     *    (and its async world load) fully completes before PLAYER_INFO is
     *    applied, so no send delay is needed. NPC info stays a placeholder. */
    send_player_info(fd, enc);
    {
        uint8_t b[1] = { 0 };
        send_packet(fd, enc, 104, b, 1, 2);
    }

    /* 10. End-of-tick marker. */
    send_packet(fd, enc, 108, NULL, 0, 0);

    fprintf(stderr, "mock230: on-login burst complete\n");
}

static int
handle_client(int fd)
{
    uint8_t buf[2048];

    /* 1. INIT_GAME_CONNECTION (opcode 14). */
    if( read_full(fd, buf, 1) != 1 || buf[0] != 14 )
    {
        fprintf(stderr, "mock230: expected opcode 14, got %d\n", buf[0]);
        return -1;
    }
    /* Reply [status 0x00][sessionId i64]. */
    {
        struct RSCache_Buffer o;
        RSCache_BufferInit(&o, buf, sizeof(buf));
        p1(&o, 0);
        p4(&o, (int32_t)(SESSION_ID >> 32));
        p4(&o, (int32_t)(SESSION_ID & 0xffffffff));
        write(fd, buf, (size_t)o.position);
    }

    /* 2. GAMELOGIN (opcode 16) + u16 len + block. */
    if( read_full(fd, buf, 3) != 3 || buf[0] != 16 )
    {
        fprintf(stderr, "mock230: expected opcode 16\n");
        return -1;
    }
    int payload_len = (buf[1] << 8) | buf[2];
    if( payload_len < 13 || read_full(fd, buf, payload_len) != payload_len )
    {
        fprintf(stderr, "mock230: short login block (%d)\n", payload_len);
        return -1;
    }
    {
        struct RSCache_Buffer in;
        in.data = buf;
        in.size = (uint32_t)payload_len;
        in.position = 0;
        (void)g4(&in); /* version */
        (void)g4(&in); /* subVersion */
        (void)g1(&in); /* clientType */
        (void)g1(&in); /* platformType */
        (void)g1(&in); /* externalAuth */
        int rsa_size = g2(&in);

        /* RSA-decrypt the block with the private key. */
        struct rsa rsa;
        if( rsa_init(&rsa, MOCK_RSA_D, MOCK_RSA_N) != 0 )
        {
            fprintf(stderr, "mock230: rsa_init failed\n");
            return -1;
        }
        uint8_t plain[512];
        int plen = rsa_crypt(&rsa, buf + in.position, (size_t)rsa_size, plain, sizeof(plain));
        if( plen <= 0 )
        {
            fprintf(stderr, "mock230: rsa decrypt failed\n");
            return -1;
        }

        struct RSCache_Buffer rb;
        rb.data = plain;
        rb.size = (uint32_t)plen;
        rb.position = 0;
        int enc_check = g1(&rb);
        int32_t seed[4];
        for( int i = 0; i < 4; i++ )
            seed[i] = (int32_t)g4(&rb);
        uint64_t sid = (uint64_t)g8(&rb);
        int auth_type = g1(&rb);
        char const* user = gcstring(&rb);
        char const* pass = gcstring(&rb);
        fprintf(
            stderr,
            "mock230: login encCheck=%d authType=%d sid=%s user='%s' pass-len=%zu\n",
            enc_check,
            auth_type,
            sid == SESSION_ID ? "ok" : "MISMATCH",
            user ? user : "",
            pass ? strlen(pass) : 0);

        /* 3. Success response (plaintext) then GAME stream. */
        uint8_t ok = 2;
        write(fd, &ok, 1);

        /* Arm the server out-cipher = seed+50 (client in-cipher matches). */
        int32_t sin[4];
        for( int i = 0; i < 4; i++ )
            sin[i] = seed[i] + 50;
        struct Isaac* enc = isaac_new(sin, 4);

        /* 4. On-login burst. */
        send_login_burst(fd, enc);

        /* 5. Idle: drain client bytes so the socket stays healthy. */
        fprintf(stderr, "mock230: idling (client logged in)\n");
        for( ;; )
        {
            int r = (int)read(fd, buf, sizeof(buf));
            if( r <= 0 )
                break;
        }
        isaac_free(enc);
    }
    return 0;
}

int
main(
    int argc,
    char** argv)
{
    int port = argc > 1 ? atoi(argv[1]) : 43595;

    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if( srv < 0 )
    {
        perror("socket");
        return 1;
    }
    int one = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((uint16_t)port);
    if( bind(srv, (struct sockaddr*)&addr, sizeof(addr)) != 0 )
    {
        perror("bind");
        return 1;
    }
    listen(srv, 1);
    fprintf(stderr, "mock230: listening on 127.0.0.1:%d\n", port);

    for( ;; )
    {
        int fd = accept(srv, NULL, NULL);
        if( fd < 0 )
            continue;
        fprintf(stderr, "mock230: client connected\n");
        handle_client(fd);
        close(fd);
        fprintf(stderr, "mock230: client disconnected\n");
    }
    return 0;
}
