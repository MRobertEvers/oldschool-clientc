#include "js5_server.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <signal.h>
#endif

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int
parse_u32(
    const char* flag,
    const char* value,
    uint32_t minimum,
    uint32_t maximum,
    uint32_t* out)
{
    char* end = NULL;
    unsigned long parsed;

    errno = 0;
    parsed = strtoul(value, &end, 10);
    if( errno != 0 || end == value || *end != '\0' || parsed < minimum || parsed > maximum )
    {
        fprintf(stderr, "js5_server: %s requires %u..%u\n", flag, minimum, maximum);
        return -1;
    }
    *out = (uint32_t)parsed;
    return 0;
}

static void
print_usage(const char* program)
{
    fprintf(
        stderr,
        "usage: %s --cache DIR [--revision 239] [--bind 127.0.0.1] "
        "[--port 43594] [--max-clients 48] [--backlog 64] "
        "[--max-pending 200] [--handshake-timeout-ms 10000] "
        "[--idle-timeout-ms 300000] [--output-timeout-ms 30000] [--verbose]\n",
        program);
}

#ifdef _WIN32
static BOOL WINAPI
console_control(DWORD event)
{
    if( event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT || event == CTRL_CLOSE_EVENT ||
        event == CTRL_SHUTDOWN_EVENT )
    {
        Js5ServerRequestStop();
        return TRUE;
    }
    return FALSE;
}
#else
static void
signal_stop(int signal_number)
{
    (void)signal_number;
    Js5ServerRequestStop();
}
#endif

int
main(int argc, char** argv)
{
    struct Js5ServerConfig config;
    uint32_t value;

    Js5ServerConfigInit(&config);
    for( int index = 1; index < argc; index++ )
    {
        const char* flag = argv[index];
        if( strcmp(flag, "--cache") == 0 && index + 1 < argc )
            config.cache_dir = argv[++index];
        else if( strcmp(flag, "--bind") == 0 && index + 1 < argc )
            config.bind_address = argv[++index];
        else if( strcmp(flag, "--revision") == 0 && index + 1 < argc )
        {
            if( parse_u32(flag, argv[++index], 1u, INT_MAX, &config.revision) != 0 )
                return 2;
        }
        else if( strcmp(flag, "--port") == 0 && index + 1 < argc )
        {
            if( parse_u32(flag, argv[++index], 1u, 65535u, &value) != 0 )
                return 2;
            config.port = (uint16_t)value;
        }
        else if( strcmp(flag, "--max-clients") == 0 && index + 1 < argc )
        {
            if( parse_u32(flag, argv[++index], 1u, 10000u, &config.max_clients) != 0 )
                return 2;
        }
        else if( strcmp(flag, "--backlog") == 0 && index + 1 < argc )
        {
            if( parse_u32(flag, argv[++index], 1u, INT_MAX, &config.backlog) != 0 )
                return 2;
        }
        else if( strcmp(flag, "--max-pending") == 0 && index + 1 < argc )
        {
            if( parse_u32(flag, argv[++index], 1u, 65535u, &config.max_pending_per_lane) != 0 )
                return 2;
        }
        else if( strcmp(flag, "--handshake-timeout-ms") == 0 && index + 1 < argc )
        {
            if( parse_u32(flag, argv[++index], 0u, UINT_MAX, &config.handshake_timeout_ms) != 0 )
                return 2;
        }
        else if( strcmp(flag, "--idle-timeout-ms") == 0 && index + 1 < argc )
        {
            if( parse_u32(flag, argv[++index], 0u, UINT_MAX, &config.idle_timeout_ms) != 0 )
                return 2;
        }
        else if( strcmp(flag, "--output-timeout-ms") == 0 && index + 1 < argc )
        {
            if( parse_u32(flag, argv[++index], 0u, UINT_MAX, &config.output_timeout_ms) != 0 )
                return 2;
        }
        else if( strcmp(flag, "--verbose") == 0 )
            config.verbose = true;
        else if( strcmp(flag, "--help") == 0 || strcmp(flag, "-h") == 0 )
        {
            print_usage(argv[0]);
            return 0;
        }
        else
        {
            fprintf(stderr, "js5_server: unknown or incomplete option '%s'\n", flag);
            print_usage(argv[0]);
            return 2;
        }
    }
    if( !config.cache_dir )
    {
        fprintf(stderr, "js5_server: --cache is required\n");
        print_usage(argv[0]);
        return 2;
    }

#ifdef _WIN32
    SetConsoleCtrlHandler(console_control, TRUE);
#else
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, signal_stop);
    signal(SIGTERM, signal_stop);
#endif
    return Js5ServerRun(&config) == 0 ? 0 : 1;
}
