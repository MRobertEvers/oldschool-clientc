#ifndef SRC_EXECUTOR_CONFIG_H
#define SRC_EXECUTOR_CONFIG_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct AppConfig;

#define TORIRS_EXECUTOR_JS5_HOST_MAX 128

/*
 * Host-owned settings which must not leak into AppConfig. The App continues to
 * describe the already-openable cache and game connection; the executor may
 * use this separate block to populate that cache before App_Init, or attach a
 * JS5 producer beside the frame loop later.
 */
struct ToriRS_ExecutorConfig
{
    int js5_enabled;
    char js5_host[TORIRS_EXECUTOR_JS5_HOST_MAX];
    int js5_port;          /* 0 = inherit the effective game port */
    int js5_fallback_port; /* 0 = disabled; meaningful after ResolveJs5 */
    int js5_fallback_port_set;
    int js5_revision; /* 0 = inherit AppConfig cache revision */
    int js5_revision_explicit;
};

void
ToriRS_ExecutorConfig_Init(struct ToriRS_ExecutorConfig* config);

/*
 * Resolve inherited JS5 values after manifest and CLI parsing have finalized
 * AppConfig. Disabled JS5 is always valid. Enabled JS5 requires a dat2 cache,
 * a host and a positive cache identity revision.
 *
 * The normal update port is 43594. Only that primary gets the historical 443
 * fallback by default; a custom primary has no fallback unless one was stated.
 * A non-explicit JS5 revision must agree with the cache identity. An explicit
 * manifest/CLI revision is the opt-out for test servers which intentionally
 * announce a different revision.
 *
 * Returns 0 on success and -1 on invalid configuration. When error/error_cap
 * are supplied, a user-facing reason is written there.
 */
int
ToriRS_ExecutorConfig_ResolveJs5(
    struct ToriRS_ExecutorConfig* config,
    struct AppConfig const* app_config,
    char* error,
    size_t error_cap);

#ifdef __cplusplus
}
#endif

#endif
