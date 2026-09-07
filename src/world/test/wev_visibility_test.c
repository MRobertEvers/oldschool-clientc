#include "world/wev.h"
#include <math.h>
#include <stdio.h>

static int failures;
#define CHECK(c, text) do { if( !(c) ) { fprintf(stderr, "FAIL: %s\n", text); ++failures; } } while(0)

static void config(struct WevConfig* cfg, int width, int height)
{
    uint8_t bytes[] = {8, width >> 8, width, 9, height >> 8, height, 0};
    CHECK(WevConfig_Decode(cfg, 1, bytes, sizeof(bytes)), "synthetic native bounds decode");
}

static struct Wev* spawn(struct Wevs* all, struct WevConfig* cfg, int id, int group, int x, int z)
{
    return Wevs_Spawn(all, id, 0, cfg, 1, x, z, 0, group, 31);
}

static bool actor(void* user, const struct Wev* wev)
{
    int* point = user;
    return Wev_OverlapsActor(wev, point[0], point[1], 1);
}

int main(void)
{
    struct WevConfig square, longboat;
    config(&square, 128, 128);
    config(&longboat, 128, 768);
    struct Wevs all;
    Wevs_Init(&all);
    struct Wev* a = spawn(&all, &square, 1, 0, 64, 64);
    int x, z, w, h;
    Wev_PainterFootprint(a, &x, &z, &w, &h);
    CHECK(x == 0 && z == 0 && w == 1 && h == 1, "radius60 inside a tile");
    a->x = 0; a->z = 128;
    Wev_PainterFootprint(a, &x, &z, &w, &h);
    CHECK(x == -1 && z == 0 && w == 2 && h == 2, "radius60 crosses tile boundaries");
    a->x = a->z = 64;
    struct Wev* b = spawn(&all, &square, 2, 1, 193, 64);
    Wevs_SelectRenderStates(&all, 0, 30, NULL, NULL);
    CHECK(b->render_visible && !b->flattened, "one-fine-unit gap must not use rounded tile overlap");
    b->x = 192;
    Wevs_SelectRenderStates(&all, 0, 30, NULL, NULL);
    CHECK(b->flattened, "native fine AABB touching boundary intersects");

    Wevs_Init(&all);
    a = spawn(&all, &longboat, 1, 0, 4096, 4096);
    b = spawn(&all, &longboat, 2, 1, 4396, 3796);
    a->angle = b->angle = 256;
    CHECK(!Wev_OverlapsActor(a, 4396, 3796, 1), "actor outside diagonal OBB is not an overlap");
    CHECK(Wev_OverlapsActor(a, 4310, 4310, 1), "actor inside diagonal OBB overlaps");
    Wevs_SelectRenderStates(&all, 0, 30, NULL, NULL);
    CHECK(b->flattened, "boat overlap uses native transformed AABB, not full SAT");
    a->angle = 63;
    CHECK(!Wev_OverlapsActor(a, 4246, 4396, 1), "actor bucket below64 is zero");
    a->angle = 64;
    CHECK(Wev_OverlapsActor(a, 4246, 4396, 1), "actor bucket64 rounds to the next128 heading");

    struct WevConfig skiff; config(&skiff,256,640);
    Wevs_Init(&all);
    a=spawn(&all,&skiff,1,0,4096,4096);a->angle=368;
    int native_bounds[4];Wev_RenderBounds(a,native_bounds);
    CHECK(native_bounds[0]==3753 && native_bounds[2]==4439,
          "native Q16 trig, before component truncation, keeps the actual skiff edge at343 fine units");

    Wevs_Init(&all);
    for( int id = 1; id <= 6; ++id ) spawn(&all, &square, id, id <= 2 ? 0 : id <= 4 ? 2 : 1, id * 1000, 0);
    Wevs_SelectRenderStates(&all, 2, 1, NULL, NULL);
    CHECK(all.wevs[1].render_visible && all.wevs[2].render_visible && all.wevs[3].render_visible && all.wevs[5].render_visible,
          "aboard is uncapped and each group receives its own slot");
    CHECK(!all.wevs[4].render_visible && !all.wevs[6].render_visible, "over-budget boats are skipped");
    Wevs_SelectRenderStates(&all, 2, 0, NULL, NULL);
    for( int id = 1; id <= 6; ++id ) CHECK(all.wevs[id].render_visible == (id == 2), "zero limit preserves only aboard");
    Wevs_SelectRenderStates(&all, 0, 30, NULL, NULL);
    for( int id = 1; id <= 6; ++id ) CHECK(all.wevs[id].render_visible, "selection resets previous frame visibility");

    struct WevConfig wide;
    config(&wide, 256, 256);
    Wevs_Init(&all);
    spawn(&all, &wide, 1, 0, 0, 0);
    spawn(&all, &wide, 2, 1, 200, 0);
    spawn(&all, &wide, 3, 1, 440, 0);
    Wevs_SelectRenderStates(&all, 0, 30, NULL, NULL);
    CHECK(!all.wevs[1].flattened && all.wevs[2].flattened && all.wevs[3].flattened,
          "a flattened but placed boat still stamps overlap for the next boat");
    Wevs_SelectRenderStates(&all, 0, 1, NULL, NULL);
    CHECK(all.wevs[2].flattened && !all.wevs[3].render_visible, "flatten consumes its group draw slot");
    Wevs_Init(&all);
    a = spawn(&all, &square, 1, 1, 1000, 1000);
    int point[] = {1000, 1000};
    Wevs_SelectRenderStates(&all, 0, 30, actor, point);
    CHECK(a->flattened, "group1 yields to an overlapping actor");
    Wevs_SelectRenderStates(&all, 1, 0, actor, point);
    CHECK(a->render_visible && !a->flattened, "aboard never flattens under actor or budget");

    float fx = 0, fz = 0;
    Wev_SmoothCameraFocus(&fx, &fz, 500, -500);
    CHECK(fx == 31.25f && fz == -31.25f, "camera smooths exactly at inclusive500 boundary");
    fx = fz = 0; Wev_SmoothCameraFocus(&fx, &fz, 10, 501);
    CHECK(fx == 10 && fz == 501, "over-threshold axis snaps both camera axes");
    fx = fz = 0; Wev_SmoothCameraFocus(&fx, &fz, -501, 0);
    CHECK(fx == -501 && fz == 0, "negative camera threshold snaps");
    fx = fz = 0;
    for( int i = 0; i < 256; ++i ) Wev_SmoothCameraFocus(&fx, &fz, 15, -15);
    CHECK(fabsf(fx - 15) < .001f && fabsf(fz + 15) < .001f, "sub16 camera distances converge instead of stalling");
    WevConfig_FreeContents(&wide);
    WevConfig_FreeContents(&longboat);
    WevConfig_FreeContents(&square);
    printf("native world-entity visibility/camera: %d failures\n", failures);
    return failures != 0;
}
