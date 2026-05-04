#include "osrs/core/revision.h"

#include "osrs/game.h"
#include "osrs/revs/lc245_2/loginproto_rev245_2.h"
#include "osrs/packetin.h"
#include "osrs/revs/lc245_2/gameproto_rev245_2_parse.h"
#include "osrs/revs/lc245_2/revision_lc245_2.h"

#include <assert.h>

static struct Revision g_revision = { REVISION_KIND_INVALID, NULL };

const struct Revision*
revision_active(void)
{
    return &g_revision;
}

void
revision_set_active(struct Revision rev)
{
    g_revision = rev;
}

const char*
revision_name(const struct Revision* rev)
{
    if( !rev )
        return "invalid";
    switch( rev->kind )
    {
    case REVISION_KIND_LC245_2:
        return "lc245_2";
    case REVISION_KIND_LC254:
        return "lc254";
    case REVISION_KIND_OS217:
        return "os217";
    case REVISION_KIND_INVALID:
    default:
        return "invalid";
    }
}

const char*
revision_lua_pkt_dispatch_path(const struct Revision* rev)
{
    switch( rev->kind )
    {
    case REVISION_KIND_LC245_2:
        return "rev245_2/pkt_dispatch.lua";
    case REVISION_KIND_LC254:
        return "rev_lc254/pkt_dispatch.lua";
    case REVISION_KIND_OS217:
        assert(0 && "revision_lua_pkt_dispatch_path: OS217 not wired");
        return "empty.lua";
    case REVISION_KIND_INVALID:
    default:
        assert(0 && "revision_lua_pkt_dispatch_path: invalid revision");
        return "empty.lua";
    }
}

const char*
revision_lua_init_ui_path(const struct Revision* rev)
{
    switch( rev->kind )
    {
    case REVISION_KIND_LC245_2:
        return "rev245_2/init_ui.lua";
    case REVISION_KIND_LC254:
        return "rev_lc254/init_ui.lua";
    case REVISION_KIND_OS217:
        assert(0 && "revision_lua_init_ui_path: OS217 not wired");
        return "empty.lua";
    case REVISION_KIND_INVALID:
    default:
        assert(0 && "revision_lua_init_ui_path: invalid revision");
        return "empty.lua";
    }
}

int
revision_packetin_size(const struct Revision* rev, int opcode)
{
    switch( rev->kind )
    {
    case REVISION_KIND_LC245_2:
        return packetin_size_lc245_2(opcode);
    case REVISION_KIND_LC254:
        return packetin_size_lc254(opcode);
    case REVISION_KIND_OS217:
        (void)opcode;
        assert(0 && "revision_packetin_size: OS217 not wired");
        return 0;
    case REVISION_KIND_INVALID:
    default:
        assert(0 && "revision_packetin_size: invalid revision");
        return 0;
    }
}

int
revision_parse_and_enqueue(
    const struct Revision* rev,
    struct GGame* game,
    int opcode,
    uint8_t* data,
    int n)
{
    switch( rev->kind )
    {
    case REVISION_KIND_LC245_2:
        return gameproto_rev245_2_parse_and_enqueue(game, opcode, data, n);
    case REVISION_KIND_LC254:
        (void)game;
        (void)opcode;
        (void)data;
        (void)n;
        return 0;
    case REVISION_KIND_OS217:
        assert(0 && "revision_parse_and_enqueue: OS217 not wired");
        return 0;
    case REVISION_KIND_INVALID:
    default:
        assert(0 && "revision_parse_and_enqueue: invalid revision");
        return 0;
    }
}

bool
revision_has_pending(const struct Revision* rev, struct GGame* game)
{
    (void)rev;
    if( !game )
        return false;
    switch( game->revision.kind )
    {
    case REVISION_KIND_LC245_2:
        return game->revision.impl
            && gameproto_rev245_2_has_pending((struct RevisionLC245_2*)game->revision.impl);
    case REVISION_KIND_LC254:
        return false;
    case REVISION_KIND_OS217:
        return false;
    case REVISION_KIND_INVALID:
    default:
        return false;
    }
}

void
revision_drain_pending(const struct Revision* rev, struct GGame* game)
{
    (void)rev;
    if( !game )
        return;
    switch( game->revision.kind )
    {
    case REVISION_KIND_LC245_2:
        if( game->revision.impl )
            gameproto_rev245_2_drain_pending((struct RevisionLC245_2*)game->revision.impl, game);
        break;
    case REVISION_KIND_LC254:
    case REVISION_KIND_OS217:
    case REVISION_KIND_INVALID:
    default:
        break;
    }
}

struct LoginProto*
revision_loginproto_new(
    const struct Revision* rev,
    struct Isaac* random_in,
    struct Isaac* random_out,
    struct rsa* rsa,
    char* username,
    char* password,
    int32_t* jag_checksum)
{
    switch( rev->kind )
    {
    case REVISION_KIND_LC245_2:
        return loginproto_new(random_in, random_out, rsa, username, password, jag_checksum);
    case REVISION_KIND_LC254:
        (void)random_in;
        (void)random_out;
        (void)rsa;
        (void)username;
        (void)password;
        (void)jag_checksum;
        assert(0 && "revision_loginproto_new: LC254 not wired");
        return NULL;
    case REVISION_KIND_OS217:
        assert(0 && "revision_loginproto_new: OS217 not wired");
        return NULL;
    case REVISION_KIND_INVALID:
    default:
        assert(0 && "revision_loginproto_new: invalid revision");
        return NULL;
    }
}

void
revision_loginproto_free(const struct Revision* rev, struct LoginProto* lp)
{
    switch( rev->kind )
    {
    case REVISION_KIND_LC245_2:
        loginproto_free(lp);
        break;
    case REVISION_KIND_LC254:
    case REVISION_KIND_OS217:
    case REVISION_KIND_INVALID:
        if( lp )
            assert(0 && "revision_loginproto_free: unexpected revision");
        break;
    }
}

int
revision_loginproto_recv(
    const struct Revision* rev,
    struct LoginProto* lp,
    uint8_t* data,
    int size)
{
    switch( rev->kind )
    {
    case REVISION_KIND_LC245_2:
        return loginproto_recv(lp, data, size);
    case REVISION_KIND_LC254:
    case REVISION_KIND_OS217:
    case REVISION_KIND_INVALID:
    default:
        (void)lp;
        (void)data;
        (void)size;
        assert(0 && "revision_loginproto_recv: revision not wired");
        return 0;
    }
}

int
revision_loginproto_send(
    const struct Revision* rev,
    struct LoginProto* lp,
    uint8_t* out,
    int out_size)
{
    switch( rev->kind )
    {
    case REVISION_KIND_LC245_2:
        return loginproto_send(lp, out, out_size);
    case REVISION_KIND_LC254:
    case REVISION_KIND_OS217:
    case REVISION_KIND_INVALID:
    default:
        (void)lp;
        (void)out;
        (void)out_size;
        assert(0 && "revision_loginproto_send: revision not wired");
        return 0;
    }
}

int
revision_loginproto_poll(const struct Revision* rev, struct LoginProto* lp)
{
    switch( rev->kind )
    {
    case REVISION_KIND_LC245_2:
        return loginproto_poll(lp);
    case REVISION_KIND_LC254:
    case REVISION_KIND_OS217:
    case REVISION_KIND_INVALID:
    default:
        (void)lp;
        assert(0 && "revision_loginproto_poll: revision not wired");
        return 0;
    }
}

int
revision_write_move_gameclick(
    const struct Revision* rev,
    uint8_t* out,
    int cap,
    int x,
    int z,
    int run)
{
    (void)out;
    (void)cap;
    (void)x;
    (void)z;
    (void)run;
    switch( rev->kind )
    {
    case REVISION_KIND_LC245_2:
        assert(0 && "revision_write_move_gameclick: wired in split-outbound");
        return 0;
    case REVISION_KIND_LC254:
    case REVISION_KIND_OS217:
    case REVISION_KIND_INVALID:
    default:
        assert(0 && "revision_write_move_gameclick: revision not wired");
        return 0;
    }
}

int
revision_write_if_button(
    const struct Revision* rev,
    uint8_t* out,
    int cap,
    int component_id)
{
    (void)out;
    (void)cap;
    (void)component_id;
    switch( rev->kind )
    {
    case REVISION_KIND_LC245_2:
        assert(0 && "revision_write_if_button: wired in split-outbound");
        return 0;
    case REVISION_KIND_LC254:
    case REVISION_KIND_OS217:
    case REVISION_KIND_INVALID:
    default:
        assert(0 && "revision_write_if_button: revision not wired");
        return 0;
    }
}

int
revision_write_inv_action(
    const struct Revision* rev,
    uint8_t* out,
    int cap,
    int opcode_byte,
    int component_id,
    int slot,
    int obj_id)
{
    (void)out;
    (void)cap;
    (void)opcode_byte;
    (void)component_id;
    (void)slot;
    (void)obj_id;
    switch( rev->kind )
    {
    case REVISION_KIND_LC245_2:
        assert(0 && "revision_write_inv_action: wired in split-outbound");
        return 0;
    case REVISION_KIND_LC254:
    case REVISION_KIND_OS217:
    case REVISION_KIND_INVALID:
    default:
        assert(0 && "revision_write_inv_action: revision not wired");
        return 0;
    }
}
