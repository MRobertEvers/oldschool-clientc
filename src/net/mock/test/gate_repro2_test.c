/*
 * SCRATCH — gate investigation, phase 2. Runs the scratch
 * `[debugproc,gatecheck]` (server/scripts/skill_combat/scripts/zzz_gate_debug.rs2,
 * itself scratch) through the REAL compiled script pack and the REAL SS VM,
 * to see whether the VM/compile path disagrees with the raw C-level
 * `mock230_obj_param` reproduction (gate_repro_test.c) that already showed
 * bronze_dagger and dragon_dagger both reading back correctly.
 *
 * Not part of the build; delete after use, along with the .rs2 scratch file.
 */
#include "mock230.h"
#include "mock230_boot.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
main(void)
{
    struct Mock230BootConfig config;
    struct Mock230Server srv;
    struct Mock230Player* player;
    struct Mock230Capture capture;
    int errors;

    memset(&srv, 0, sizeof(srv));
    setenv("MOCK230_SAVES", "/tmp/objdir-gate/gate_saves", 1);

    mock230_boot_defaults(&config);
    errors = mock230_boot_load(&config);
    fprintf(stderr, "boot errors: %d\n", errors);

    player = mock230_world_add_player(&srv, NULL);
    mock230_world_init(&srv, config.home_x, config.home_z);
    mock230_world_player_init(player);
    {
        int loaded = mock230_scripts_load(&srv, config.script_dir);

        fprintf(stderr, "scripts loaded: %d (dir=%s)\n", loaded, config.script_dir);
    }

    mock230_capture_begin(&srv, &capture);
    {
        int handled = mock230_scripts_run_debugproc(&srv, "gatecheck");

        fprintf(stderr, "debugproc handled: %d\n", handled);
    }
    mock230_capture_end(&srv);

    printf("--- captured MESSAGE_GAME packets ---\n");
    for( int i = mock230_capture_find(&capture, 90 /* MESSAGE_GAME */, 0); i >= 0;
         i = mock230_capture_find(&capture, 90, i + 1) )
    {
        const struct Mock230CapturedPacket* packet = &capture.packets[i];

        if( packet->len < 2 )
            continue;
        printf("%s\n", (const char*)packet->data + 1);
    }

    mock230_boot_free();
    return 0;
}
