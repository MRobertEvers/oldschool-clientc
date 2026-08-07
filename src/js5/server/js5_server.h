#ifndef SRC_JS5_SERVER_JS5_SERVER_H
#define SRC_JS5_SERVER_JS5_SERVER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Js5ServerConfig
{
    const char* cache_dir;
    const char* bind_address;
    uint16_t port;
    uint32_t revision;
    uint32_t max_clients;
    uint32_t backlog;
    uint32_t max_pending_per_lane;
    uint32_t handshake_timeout_ms;
    uint32_t idle_timeout_ms;
    uint32_t output_timeout_ms;
    bool verbose;
};

void
Js5ServerConfigInit(struct Js5ServerConfig* config);

/* Blocking multi-client reactor. Returns 0 after a requested stop, -1 on error. */
int
Js5ServerRun(const struct Js5ServerConfig* config);

/* Signal-safe request for the active Js5ServerRun loop to return. */
void
Js5ServerRequestStop(void);

#ifdef __cplusplus
}
#endif

#endif
