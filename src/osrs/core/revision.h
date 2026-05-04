#ifndef OSRS_CORE_REVISION_H
#define OSRS_CORE_REVISION_H

#include <stdbool.h>
#include <stdint.h>

struct GGame;
struct LoginProto;
struct Isaac;
struct rsa;

enum RevisionKind
{
    REVISION_KIND_INVALID = 0,
    REVISION_KIND_LC245_2 = 1,
    REVISION_KIND_LC254 = 2,
    REVISION_KIND_OS217 = 3,
};

struct Revision
{
    enum RevisionKind kind;
    void* impl;
};

const struct Revision*
revision_active(void);

void
revision_set_active(struct Revision rev);

const char*
revision_name(const struct Revision* rev);

const char*
revision_lua_pkt_dispatch_path(const struct Revision* rev);

const char*
revision_lua_init_ui_path(const struct Revision* rev);

int
revision_packetin_size(const struct Revision* rev, int opcode);

int
revision_parse_and_enqueue(
    const struct Revision* rev,
    struct GGame* game,
    int opcode,
    uint8_t* data,
    int n);

bool
revision_has_pending(const struct Revision* rev, struct GGame* game);

void
revision_drain_pending(const struct Revision* rev, struct GGame* game);

struct LoginProto*
revision_loginproto_new(
    const struct Revision* rev,
    struct Isaac* random_in,
    struct Isaac* random_out,
    struct rsa* rsa,
    char* username,
    char* password,
    int32_t* jag_checksum);

void
revision_loginproto_free(const struct Revision* rev, struct LoginProto* lp);

int
revision_loginproto_recv(
    const struct Revision* rev,
    struct LoginProto* lp,
    uint8_t* data,
    int size);

int
revision_loginproto_send(
    const struct Revision* rev,
    struct LoginProto* lp,
    uint8_t* out,
    int out_size);

int
revision_loginproto_poll(const struct Revision* rev, struct LoginProto* lp);

int
revision_write_move_gameclick(
    const struct Revision* rev,
    uint8_t* out,
    int cap,
    int x,
    int z,
    int run);

int
revision_write_if_button(
    const struct Revision* rev,
    uint8_t* out,
    int cap,
    int component_id);

int
revision_write_inv_action(
    const struct Revision* rev,
    uint8_t* out,
    int cap,
    int opcode_byte,
    int component_id,
    int slot,
    int obj_id);

#endif
