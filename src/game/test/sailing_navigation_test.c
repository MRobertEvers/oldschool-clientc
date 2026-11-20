#include "game/sailing_navigation.h"
#include "render/torirs_world_projection.h"
#include "world/wev.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition) do { if( !(condition) ) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); exit(1); } } while(0)

int main(void)
{
    ToriDraw_InitMath();
    {
        struct Wevs wevs;
        struct WevConfig cfg = {0};
        Wevs_Init(&wevs);
        struct Wev* boat = Wevs_Spawn(&wevs, 1, WORLDVIEW_ROOT, &cfg, 2, 6400, 6400, 0, 0, 0);
        Wev_ApplyMove(boat, 256, 0, 128, 512, false, 0);
        struct WevDeckBox wire_box = {.pos_x = 6656, .pos_z = 6528, .angle = 512,
            .recenter_x = -512, .recenter_z = -512, .size_x_tiles = 8, .size_z_tiles = 8};
        int npc_x, npc_z;
        Wev_ParentFromDeck(&wire_box, 576, 320, &npc_x, &npc_z);
        int anchored_x = -1, anchored_z = -1;
        for( int cycle = 0; cycle <= 30; ++cycle )
        {
            Wev_Interpolate(boat, cycle);
            for( int observer_base = 0; observer_base <= 1024; observer_base += 1024 )
            {
                struct WevDeckBox visible = wire_box;
                visible.pos_x = boat->x - observer_base;
                visible.pos_z = boat->z - observer_base;
                visible.angle = boat->angle;
                int deck_x, deck_z;
                Wev_DeckFromWireTarget(boat, &visible, npc_x-observer_base, npc_z-observer_base, &deck_x, &deck_z);
                /* Native trig uses sin(i*0.0030679615)*65536 truncated to
                 * int: sin512 is65535, so forward+inverse loses one fine
                 * unit. The deck transform documents this quantization.
                 * Interpolating the hull must introduce NO additional drift. */
                CHECK(abs(deck_x - 576) <= 1 && abs(deck_z - 320) <= 1);
                if( anchored_x < 0 ) { anchored_x = deck_x; anchored_z = deck_z; }
                CHECK(deck_x == anchored_x && deck_z == anchored_z);
            }
        }
    }
    CHECK(SailingNavigation_Heading(0, -128) == 0);
    CHECK(SailingNavigation_Heading(-128, 0) == 4);
    CHECK(SailingNavigation_Heading(0, 128) == 8);
    CHECK(SailingNavigation_Heading(128, 0) == 12);
    for( int direction = 0; direction < 16; ++direction )
        for( int offset = -63; offset <= 63; offset += 63 )
        {
            double angle = (direction * 128 + offset) * 6.28318530717958647693 / 2048;
            CHECK(SailingNavigation_Heading(-sin(angle) * 1000, -cos(angle) * 1000) == direction);
        }
    for( int yaw = 0; yaw < 2048; yaw += 256 )
    {
        struct ToriDraw_Camera camera = { .yaw = yaw, .pitch = 256,
            .projection_mode = TORIDRAW_PROJECTION_MODE_SCALE, .projection_scale = 512 };
        struct ToriDraw_Position eye;
        ToriRS_OrbitCameraEye(6400, 0, 6400, camera.pitch, yaw, 1000, &eye);
        for( int dx = -128; dx <= 128; dx += 128 )
            for( int dz = -128; dz <= 128; dz += 128 )
            {
                int x, y;
                double rx, rz;
                CHECK(ToriRS_WorldProjectPoint(&camera, &eye, 5, 9, 765, 503, 50,
                                              6400 + dx, 0, 6400 + dz, &x, &y));
                CHECK(ToriRS_WorldUnprojectPlane(&camera, &eye, 5, 9, 765, 503, x, y, 0, &rx, &rz));
                CHECK(fabs(rx - 6400 - dx) < 6);
                CHECK(fabs(rz - 6400 - dz) < 6);
            }
    }
    {
        /* An eye thousands of tiles from the point: the replay back-step's
         * first frame has this one (fx-ub, 2026-11-20). The 32-bit rotation
         * overflowed int there, which is undefined; the projection is now
         * the geometry's, within the 16.16 tables' rounding. */
        struct ToriDraw_Camera far_camera = { .yaw = 0, .pitch = 128,
            .projection_mode = TORIDRAW_PROJECTION_MODE_SCALE, .projection_scale = 512 };
        struct ToriDraw_Position far_eye = { .x = -316352, .y = -768, .z = -411696 };
        double const pitch = 128 * 6.28318530717958647693 / 2048;
        double const dx = 2112 - far_eye.x, dy = -384 - far_eye.y, dz = 6464 - far_eye.z;
        double const y2 = dy * cos(pitch) - dz * sin(pitch);
        double const z2 = dy * sin(pitch) + dz * cos(pitch);
        int x, y;
        CHECK(ToriRS_WorldProjectPoint(&far_camera, &far_eye, 5, 9, 765, 503, 50,
                                      2112, -384, 6464, &x, &y));
        CHECK(fabs(x - (5 + 765 / 2 + dx * 512 / z2)) < 2);
        CHECK(fabs(y - (9 + 503 / 2 + y2 * 512 / z2)) < 2);
    }
    struct ToriDraw_Camera camera = { .pitch = 0, .yaw = 0,
        .projection_mode = TORIDRAW_PROJECTION_MODE_SCALE, .projection_scale = 512 };
    struct ToriDraw_Position eye = { .x = 6400, .y = -1000, .z = 6400 };
    double x, z;
    CHECK(!ToriRS_WorldUnprojectPlane(&camera, &eye, 0, 0, 800, 600, 400, 300, 0, &x, &z));
    CHECK(!ToriRS_WorldUnprojectPlane(&camera, &eye, 0, 0, 800, 600, 400, 200, 0, &x, &z));
    puts("PASS sailing compass sectors and camera-plane ray projection");
    return 0;
}
