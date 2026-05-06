#ifndef OSRS_CORE_REVISION_H
#define OSRS_CORE_REVISION_H

#include "osrs/rscache/rsbuf.h"

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
revision_lua_cacherev_load_path(const struct Revision* rev);

const char*
revision_lua_init_ui_path(const struct Revision* rev);

int
revision_packetin_size(const struct Revision* rev, int opcode);

/** Route one inbound packet byte-buffer to the correct rev deserializer + enqueue. */
int
revision_serverprot_parse(
    const struct Revision* rev,
    struct GGame* game,
    int opcode,
    uint8_t* data,
    int n);

bool
revision_has_pending(const struct Revision* rev, struct GGame* game);

/** Drain the pending inbound queue, calling gamenet_exec for each packet. */
void
revision_gamenet_exec_drain(const struct Revision* rev, struct GGame* game);

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

#endif
