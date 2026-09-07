/* Real-cache sea classification plus focused hull-motion regressions.
 * Run: make -C src test-sailing-collision */
#include "torirs_server.h"
#include "torirs_server_scene.h"
#include "torirs_server_mapinstance.h"
#include "torirs_server_vessel.h"
#include "world/wev.h"
#include "toridraw_math.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
static const char* cache_dir = "cache.osrs239";
static const int ocean_x = 3072;
static const int ocean_z = 3160;
#define CHECK(condition, message) do { \
    if( !(condition) ) { fprintf(stderr, "FAIL: %s (%d)\n", message, __LINE__); failures++; } \
} while(0)

/* Link seams outside this test's map/mover scope. */
int ToriRSServer_VarbitGet(const struct ToriRSServerPlayer* player, int id)
{ (void)player; (void)id; return 0; }
const char* ToriRSServer_WorldCacheDir(void) { return cache_dir; }
int ToriRSServer_WorldMapInstanceFree(struct ToriRSServer* srv, int handle)
{ (void)srv; return ToriRSServer_MapInstanceFree(handle); }
void ToriRSServer_WorldSetActive(struct ToriRSServer* srv, struct ToriRSServerPlayer* player)
{ srv->active_player = player; }
void ToriRSServer_WorldTeleport(struct ToriRSServer* srv, int level, int x, int z)
{ assert(srv->active_player); srv->active_player->level = level;
  srv->active_player->x = x; srv->active_player->z = z; }

static struct ToriRSServerVessel*
fixture(struct ToriRSServer* srv, int width, int length, int angle)
{
    struct ToriRSServerVessel* boat = &srv->vessels[0];
    memset(boat, 0, sizeof(*boat));
    boat->in_use = 1;
    boat->index = 1;
    boat->size_x_tiles = width;
    boat->size_z_tiles = length;
    boat->fine_x = ocean_x * 128 + 64;
    boat->fine_z = ocean_z * 128 + 64;
    boat->angle = angle;
    boat->heading = angle / 128;
    boat->speed_tier = 1;
    boat->turn_rate = 128;
    boat->sails_set = 1;
    boat->state = TORIRSSERVER_VESSEL_HEADING;
    srv->vessel_count = 1;
    return boat;
}

/* Geometry cases add only a boat obstacle; the real ocean/player map survives. */
static void
boat_obstacle(int dx, int dz, int blocked)
{
    struct CollisionMap* map = ToriRSServer_SceneBoatCollision(0);
    int x = ocean_x + dx - ToriRSServer_SceneBaseX();
    int z = ocean_z + dz - ToriRSServer_SceneBaseZ();
    if( blocked ) collision_map_add_floor(map, x, z);
    else collision_map_del_floor(map, x, z);
}

int main(int argc, char** argv)
{
    struct ToriRSServer* srv = calloc(1, sizeof(*srv));
    struct ToriRSServerVessel* boat;
    assert(srv);
    ToriDraw_InitMath();
    if( argc > 1 ) cache_dir = argv[1];
    CHECK(ToriRSServer_SceneBuild(cache_dir, ocean_x >> 3, ocean_z >> 3), "ocean cache scene builds");
    if( !ToriRSServer_SceneBoatCollision(0) ) return 1;
    CHECK(ToriRSServer_SceneBoatCollision(0) != ToriRSServer_SceneCollision(0),
          "player and boat maps have independent storage");
    for( int dx = -8; dx <= 8; dx++ )
        for( int dz = -8; dz <= 8; dz++ )
        {
            CHECK(ToriRSServer_VesselTileSailable(0, ocean_x + dx, ocean_z + dz),
                  "surveyed ocean permits a hull");
            CHECK(ToriRSServer_SceneWalkBlocked(0, ocean_x + dx, ocean_z + dz),
                  "surveyed ocean blocks a walker");
        }
    /* m48_49 tile 0,51,30 is a shaped shore overlay o442;4;0, not full sea. */
    CHECK((ToriRSServer_SceneBoatTileFlags(0, 3123, 3166) & COLL_FLAG_FLOOR) != 0,
          "partial shoreline has boat terrain collision");
    CHECK(!ToriRSServer_VesselTileSailable(1, ocean_x, ocean_z), "empty sky is not water");
    CHECK(!ToriRSServer_VesselTileSailable(0, 0, 0), "unknown map blocks a hull");
    {
        int x = ocean_x + 10, z = ocean_z + 10;
        CHECK(!ToriRSServer_SceneWalkBlocked(1, x + 1, z + 1),
              "empty template padding initially has no cache player block");
        ToriRSServer_SceneRestrictDeckWalk(x, z, 4, 4, x + 1, z + 1, x + 3, z + 3);
        CHECK(ToriRSServer_SceneWalkBlocked(1, x, z) &&
              !ToriRSServer_SceneWalkBlocked(1, x + 1, z + 1),
              "deck restriction blocks padding and preserves the native interior plane");
        int obstacle = ToriRSServer_SceneAddLoc(x, z, 1, 1276, 10, 0);
        CHECK(obstacle >= 0 && ToriRSServer_SceneRemoveLoc(obstacle),
              "facility removal exercises deck terrain restamping");
        CHECK(ToriRSServer_SceneWalkBlocked(1, x, z),
              "removing a facility cannot reopen deck padding");
        CHECK(ToriRSServer_VesselTileSailable(0, x, z),
              "player deck restriction never changes boat ocean collision");
    }
    CHECK(!ToriRSServer_SceneCanStep(0, ocean_x, ocean_z, 4), "player cannot step into open ocean");
    ToriRSServer_SceneChangeOccupancy(0, ocean_x, ocean_z, 1,
                                     COLL_FLAG_NPC_OCC | COLL_FLAG_PLAYER_OCC, 1);
    CHECK(ToriRSServer_VesselTileSailable(0, ocean_x, ocean_z), "player/NPC occupancy cannot block boat map");
    ToriRSServer_SceneChangeOccupancy(0, ocean_x, ocean_z, 1,
                                     COLL_FLAG_NPC_OCC | COLL_FLAG_PLAYER_OCC, 0);

    /* A real cache tree, then overlapping trees: removal must retain both the
     * remaining obstacle and player ocean floor, without poisoning boat water. */
    {
        int first = ToriRSServer_SceneAddLoc(ocean_x + 5, ocean_z + 5, 0, 1276, 10, 0);
        int second = ToriRSServer_SceneAddLoc(ocean_x + 6, ocean_z + 5, 0, 1276, 10, 0);
        CHECK(first >= 0 && second >= 0, "cache-backed obstacles can be added at sea");
        CHECK(!ToriRSServer_VesselTileSailable(0, ocean_x + 6, ocean_z + 5), "loc footprint blocks boat map");
        CHECK(ToriRSServer_SceneRemoveLoc(first), "first obstacle removes");
        CHECK(!ToriRSServer_VesselTileSailable(0, ocean_x + 6, ocean_z + 5), "overlapping obstacle survives removal");
        CHECK(ToriRSServer_SceneRemoveLoc(second), "second obstacle removes");
        CHECK(ToriRSServer_VesselTileSailable(0, ocean_x + 6, ocean_z + 5), "removing obstacles reopens boat water");
        CHECK(ToriRSServer_SceneWalkBlocked(0, ocean_x + 6, ocean_z + 5), "removing obstacles preserves player ocean floor");
    }

    {
        struct CollisionMap* map = ToriRSServer_SceneBoatCollision(0);
        int x = ocean_x + 4 - ToriRSServer_SceneBaseX();
        int z = ocean_z + 4 - ToriRSServer_SceneBaseZ();
        collision_map_add_wall(map, x, z, 0, COLL_ANGLE_WEST, 0);
        CHECK(!ToriRSServer_VesselTileSailable(0, ocean_x + 4, ocean_z + 4),
              "directional wall flags block hull occupancy");
        CHECK(!ToriRSServer_VesselTileSailable(0, ocean_x + 3, ocean_z + 4),
              "wall's complementary edge also blocks hull occupancy");
        collision_map_del_wall(map, x, z, 0, COLL_ANGLE_WEST, 0);
        CHECK(ToriRSServer_VesselTileSailable(0, ocean_x + 4, ocean_z + 4),
              "wall removal leaves boat ocean open");
    }

    /* The native config determines hull geometry, independently of the deck
     * reservation. The ten-tile sloop's -256 offset rotates with the vessel. */
    for( int config = 1; config <= 3; ++config )
    {
        int lengths[] = {0, 3, 5, 10};
        int bx = 0, bz = 0, x0 = 0, z0 = 0, x1 = 0, z1 = 0;
        boat = fixture(srv, config, lengths[config], 0);
        boat->config_id = config;
        boat->instance = ToriRSServer_MapInstanceAlloc(cache_dir, 1, (lengths[config] + 7) / 8);
        CHECK(boat->instance > 0 && ToriRSServer_MapInstanceBase(boat->instance, &bx, &bz),
              "native deck footprint fixture reserves pool tiles");
        CHECK(ToriRSServer_VesselDeckWalkBounds(boat, &x0, &z0, &x1, &z1) &&
              x1 - x0 == config && z1 - z0 == (config == 3 ? 8 : lengths[config]),
              "native raft/skiff/sloop walk rectangles exclude zone rounding padding");
        if( config == 3 )
            CHECK(z0 - bz == 3 && z1 - bz == 11,
                  "Sloop navigation bow buffer is excluded from player planking");
        fprintf(stderr, "deck walk config%d local %d,%d..%d,%d (exclusive)\n",
                config, x0 - bx, z0 - bz, x1 - bx, z1 - bz);
        ToriRSServer_MapInstanceFree(boat->instance);
    }
    boat = fixture(srv, 8, 8, 0);
    boat->config_id = 2;
    boat_obstacle(3, 0, 1);
    CHECK(ToriRSServer_VesselCanOccupy(boat, boat->fine_x, boat->fine_z, 0),
          "8x8 deck reservation keeps native skiff 2x5 collision");
    boat_obstacle(3, 0, 0);
    boat = fixture(srv, 1, 1, 0);
    boat->config_id = 3;
    boat_obstacle(0, -6, 1);
    CHECK(!ToriRSServer_VesselCanOccupy(boat, boat->fine_x, boat->fine_z, 0),
          "tiny deck reservation cannot shrink native sloop or discard bow offset");
    boat_obstacle(0, -6, 0);
    boat_obstacle(-6, 0, 1);
    CHECK(!ToriRSServer_VesselCanOccupy(boat, boat->fine_x, boat->fine_z, 512),
          "native negative Z bound offset turns west with the hull");
    CHECK(ToriRSServer_VesselCanOccupy(boat, boat->fine_x, boat->fine_z, 1536),
          "the same bound offset turns east on the opposite heading");
    boat_obstacle(-6, 0, 0);

    boat = fixture(srv, 1, 1, 0);
    boat->fine_x -= 32;
    boat_obstacle(-1, 0, 1);
    CHECK(!ToriRSServer_VesselCanOccupy(boat, boat->fine_x, boat->fine_z, 0),
          "full hull catches a 32-unit edge overlap missed by inset samples");
    boat_obstacle(-1, 0, 0);
    CHECK(ToriRSServer_VesselCanOccupy(boat, boat->fine_x, boat->fine_z, 0), "unobstructed offset hull fits");

    for( int target = 0; target < 2; target++ )
    {
        boat = fixture(srv, 1, 1, 1536);
        int original_x = boat->fine_x;
        boat->speed_tier = 4;
        if( target ) ToriRSServer_VesselSetTarget(boat, ocean_x + 2, ocean_z);
        boat_obstacle(1, 0, 1);
        CHECK(ToriRSServer_VesselCanOccupy(boat, original_x + 256, boat->fine_z, 1536), "fast-step endpoint itself is clear");
        ToriRSServer_VesselTickAll(srv);
        CHECK(boat->fine_x == original_x && boat->angle == 1536, "intervening obstacle refuses whole move and preserves pose");
        CHECK(boat->state == TORIRSSERVER_VESSEL_IDLE && ToriRSServer_VesselTakeBlocked(boat), "heading and target-arrival blockage both notify");
        CHECK(!ToriRSServer_VesselTakeBlocked(boat), "blocked notice drains once");
        boat_obstacle(1, 0, 0);
    }

    boat = fixture(srv, 1, 3, 0);
    boat->sails_set = 0;
    boat->heading = 4;
    boat->turn_rate = 512;
    boat_obstacle(1, 0, 1);
    ToriRSServer_VesselTickAll(srv);
    CHECK(boat->angle == 0 && boat->state == TORIRSSERVER_VESSEL_IDLE,
          "sails-down rotation into shore is refused without mutating yaw");
    boat_obstacle(1, 0, 0);

    boat = fixture(srv, 1, 5, 0);
    boat->sails_set = 0;
    boat->heading = 8;
    boat->turn_rate = 1024;
    boat_obstacle(2, 0, 1);
    CHECK(ToriRSServer_VesselCanOccupy(boat, boat->fine_x, boat->fine_z, 1024), "half-turn endpoint is clear");
    ToriRSServer_VesselTickAll(srv);
    CHECK(boat->angle == 0 && boat->blocked_notice, "swept turn catches shore between clear endpoints");
    boat_obstacle(2, 0, 0);

    boat = fixture(srv, 1, 3, 0);
    boat->sails_set = 0;
    boat->heading = 4;
    for( int i = 0; i < 4; i++ ) ToriRSServer_VesselTickAll(srv);
    CHECK(boat->angle == 512 && boat->fine_x == ocean_x * 128 + 64 &&
          boat->fine_z == ocean_z * 128 + 64, "clear stationary turn reaches heading without translation");

    boat = fixture(srv, 1, 3, 0);
    boat->anchored = 1;
    boat->heading = 4;
    for( int i = 0; i < 4; i++ ) ToriRSServer_VesselTickAll(srv);
    CHECK(boat->angle == 512 && boat->fine_x == ocean_x * 128 + 64 &&
          boat->fine_z == ocean_z * 128 + 64,
          "lowered anchor permits collision-checked heading changes but stops translation");
    boat->anchored = 0;
    ToriRSServer_VesselTickAll(srv);
    CHECK(boat->fine_x < ocean_x * 128 + 64 && boat->sails_set,
          "raising anchor resumes the retained sails and heading");

    for( int heading = 0; heading < 16; heading++ )
    {
        boat = fixture(srv, 1, 1, heading * 128);
        boat->speed_tier = 4;
        for( int i = 0; i < 3; i++ ) ToriRSServer_VesselTickAll(srv);
        CHECK(boat->state == TORIRSSERVER_VESSEL_HEADING, "all sixteen headings sail on real ocean");
        CHECK((boat->fine_x & 31) == 0 && (boat->fine_z & 31) == 0, "sailing retains quarter-tile quantum");
    }

    /* Boats may overlap and pass through one another; they never stamp the
     * map that decides whether another hull may move. */
    boat = fixture(srv, 2, 5, 0);
    boat->config_id = 2;
    srv->vessels[1] = *boat;
    srv->vessels[1].index = 2;
    srv->vessels[1].angle = 1024;
    srv->vessels[1].heading = 8;
    srv->vessel_count = 2;
    for( int i = 0; i < 4; ++i ) ToriRSServer_VesselTickAll(srv);
    CHECK(boat->fine_z == ocean_z * 128 + 64 - 256 &&
          srv->vessels[1].fine_z == ocean_z * 128 + 64 + 256 &&
          !boat->blocked_notice && !srv->vessels[1].blocked_notice,
          "fully overlapping native hulls move apart without boat-versus-boat collision");
    memset(&srv->vessels[1], 0, sizeof(srv->vessels[1]));

    {
        static const int starts[] = {2047, 1, 1920};
        static const int headings[] = {0, 15, 1};
        static const int expected[] = {0, 1921, 0};
        for( int i = 0; i < 3; ++i )
        {
            boat = fixture(srv, 1, 1, starts[i]);
            boat->anchored = 1;
            boat->heading = headings[i];
            ToriRSServer_VesselTickAll(srv);
            CHECK(boat->angle == expected[i], "heading turns follow the capped shortest arc across zero");
        }
    }
    /* Compare the independently implemented client transform as well as the
     * inverse: matching inverse mistakes must not make this test pass. */
    for( int heading = 0; heading < 16; ++heading )
    {
        static const int points[][2] = {{0,0},{64,64},{448,448},{1023,1023},{173,821}};
        boat = fixture(srv, 1, 3, heading * 128);
        boat->config_id = 1; /* Native raft pivot=-64,-64 in an8x8 view. */
        struct WevDeckBox client = {.pos_x=boat->fine_x,.pos_z=boat->fine_z,
            .angle=boat->angle,.recenter_x=-448,.recenter_z=-448,
            .size_x_tiles=8,.size_z_tiles=8};
        for( unsigned i = 0; i < sizeof(points)/sizeof(points[0]); ++i )
        {
            int x, z, cx, cz, back_x, back_z;
            ToriRSServer_VesselDeckToRoot(boat,points[i][0],points[i][1],&x,&z);
            Wev_ParentFromDeck(&client,points[i][0],points[i][1],&cx,&cz);
            CHECK(x==cx && z==cz,"server projection matches actual client at every heading");
            ToriRSServer_VesselRootToDeck(boat,x,z,&back_x,&back_z);
            Wev_DeckFromParent(&client,x,z,&cx,&cz);
            CHECK(back_x==cx && back_z==cz, "inverse projection matches the actual client at every heading");
            CHECK(abs(back_x-points[i][0])<=2 && abs(back_z-points[i][1])<=2,
                  "all sixteen inverse roundtrips stay within fixed-point rounding");
        }
    }

    boat = fixture(srv, 1, 1, 1536);
    boat->speed_cap_fine = 448;
    ToriRSServer_VesselSetSpeed(boat, 7);
    ToriRSServer_VesselTickAll(srv);
    CHECK(boat->fine_x == ocean_x * 128 + 64 + 448,
          "native 3.5 tile speed cap supports tier seven");
    boat = fixture(srv, 1, 1, 1536);
    boat->speed_cap_fine = 192;
    ToriRSServer_VesselSetSpeed(boat, 7);
    CHECK(boat->speed_tier == 3, "speed setter clamps throttle to native cap");
    boat->speed_tier = 7;
    ToriRSServer_VesselTickAll(srv);
    CHECK(boat->fine_x == ocean_x * 128 + 64 + 192,
          "movement also respects a reduced native speed cap");

    boat = fixture(srv, 1, 3, 1024);
    CHECK(ToriRSServer_SceneBuild(cache_dir, (ocean_x + 8) >> 3, ocean_z >> 3), "overlapping scene rebuild succeeds");
    ToriRSServer_VesselTickAll(srv);
    CHECK(boat->fine_z == ocean_z * 128 + 128 && boat->state == TORIRSSERVER_VESSEL_HEADING,
          "rebuild retains boat ocean without restamping");

    CHECK(ToriRSServer_SceneBuild(cache_dir, 3222 >> 3, 3218 >> 3), "inland scene builds");
    CHECK(!ToriRSServer_VesselTileSailable(0, 3222, 3218), "ordinary inland ground is not sailable");
    {
        struct CollisionMap* player_map = ToriRSServer_SceneCollision(0);
        int x = 3222 - ToriRSServer_SceneBaseX();
        int z = 3218 - ToriRSServer_SceneBaseZ();
        collision_map_add_floor(player_map, x, z);
        CHECK(!ToriRSServer_VesselTileSailable(0, 3222, 3218), "blocked inland ground cannot turn into sea");
    }
    ToriRSServer_SceneFree();
    free(srv);
    fprintf(stderr, "sailing_collision_test: %s (%d failures)\n", failures ? "FAILED" : "passed", failures);
    return failures ? 1 : 0;
}
