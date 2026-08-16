#include "executor_config.h"

#include "app.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TORIRS_JS5_DEFAULT_PORT 43594
#define TORIRS_JS5_DEFAULT_FALLBACK_PORT 443
#define TORIRS_PORT_MAX 65535

static int
executor_config_error(char* error, size_t error_cap, char const* message)
{
    if( error && error_cap > 0 )
        snprintf(error, error_cap, "%s", message);
    return -1;
}

void
ToriRS_ExecutorConfig_Init(struct ToriRS_ExecutorConfig* config)
{
    assert(config);
    memset(config, 0, sizeof(*config));
}

int
ToriRS_ExecutorConfig_ResolveJs5(
    struct ToriRS_ExecutorConfig* config,
    struct AppConfig const* app_config,
    char* error,
    size_t error_cap)
{
    assert(config);
    assert(app_config);

    if( error && error_cap > 0 )
        error[0] = '\0';
    if( !config->js5_enabled )
        return 0;

#if defined(TORIRS_PLATFORM_WEB)
    return executor_config_error(
        error, error_cap, "JS5 incremental cache loading is not available in the web executor");
#endif

    if( app_config->cache_kind != APP_CACHE_DAT2 )
        return executor_config_error(
            error, error_cap, "JS5 incremental cache loading requires a dat2 cache");

    if( config->js5_host[0] == '\0' && app_config->connect_target &&
        app_config->connect_target[0] )
    {
        char const* target = app_config->connect_target;
        char const* colon = strrchr(target, ':');
        size_t host_len = colon ? (size_t)(colon - target) : strlen(target);

        if( colon )
        {
            char* end = NULL;
            long endpoint_port = strtol(colon + 1, &end, 10);
            if( host_len == 0 || end == colon + 1 || *end != '\0' || endpoint_port <= 0 ||
                endpoint_port > TORIRS_PORT_MAX )
                return executor_config_error(
                    error, error_cap, "inherited JS5 host:port endpoint is invalid");
            if( config->js5_port == 0 )
                config->js5_port = (int)endpoint_port;
        }
        if( host_len >= sizeof(config->js5_host) )
            return executor_config_error(
                error, error_cap, "inherited JS5 host is longer than 127 characters");
        memcpy(config->js5_host, target, host_len);
        config->js5_host[host_len] = '\0';
    }
    if( config->js5_host[0] == '\0' )
        return executor_config_error(
            error, error_cap, "JS5 is enabled but no host was provided or inherited");

    if( config->js5_port == 0 )
        config->js5_port =
            app_config->connect_port > 0 ? app_config->connect_port : TORIRS_JS5_DEFAULT_PORT;
    if( config->js5_port <= 0 || config->js5_port > TORIRS_PORT_MAX )
        return executor_config_error(error, error_cap, "JS5 primary port must be in 1..65535");

    if( !config->js5_fallback_port_set )
        config->js5_fallback_port = config->js5_port == TORIRS_JS5_DEFAULT_PORT
                                        ? TORIRS_JS5_DEFAULT_FALLBACK_PORT
                                        : 0;
    if( config->js5_fallback_port < 0 || config->js5_fallback_port > TORIRS_PORT_MAX )
        return executor_config_error(error, error_cap, "JS5 fallback port must be in 0..65535");

    if( !app_config->cache_identity_set || app_config->cache_revision <= 0 )
        return executor_config_error(
            error, error_cap, "JS5 requires a positive cache identity revision");
    if( config->js5_revision <= 0 )
        config->js5_revision = app_config->cache_revision;
    else if( !config->js5_revision_explicit &&
             config->js5_revision != app_config->cache_revision )
        return executor_config_error(
            error, error_cap, "JS5 revision disagrees with the cache identity");

    return 0;
}
