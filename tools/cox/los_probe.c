/*
 * The server's own collision answers for one built scene, a line a query on
 * stdin -- for an RSProx analysis that has to ask "could that npc see that
 * raider" (tools/cox/rsprox_mystics.py --probe). Same scene builder and
 * collision map the server routes and hunts on (ToriRSServer_SceneBuild, the
 * loc configs from the server pack).
 *
 *   los_probe <cache_dir> <x> <z> <pack_dir>
 *   F l x z                          raw collision flags
 *   S|W|A l sx sz dx dz sw sh dw dh  line of sight / line of walk / approached
 *   T l x z ox oz size               can travel
 * Each answers one integer line.
 *
 * Build it with the walkable-probe link line plus the server pack reader:
 *   cd src && eval "$(make -n walkable-probe | grep -A6 -- '-o .*walkable_probe' | tr -d '\\\n' \
 *     | sed 's#-o [^ ]*walkable_probe#-o /tmp/los_probe#; s#torirsserver/test/walkable_probe.c#../tools/cox/los_probe.c torirsserver/torirs_server_servpack.c#; s#echo.*##')"
 *   /tmp/los_probe cache.osrs239 3280 5264 OSRS-Content/osrs239-content/server/pack   (from the repo root)
 */
#include "torirs_server_scene.h"
#include "torirs_server_servpack.h"
#include "engine/world_builder/collision_map.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int ToriRSServer_VarbitGet(struct ToriRSServerPlayer* p, int v) { (void)p; (void)v; return 0; }
int ToriRSServer_SceneTileFlags(int level, int x, int z);
int main(int argc, char** argv)
{
    static struct RSCache_ServerPack pack;
    char kind; int a[9]; char line[256];
    if( argc < 5 ) return 2;
    if( !ToriRSServer_ServPackOpen(&pack, argv[4]) ) { fprintf(stderr, "no pack\n"); return 1; }
    if( ToriRSServer_SceneLocConfigsLoad(&pack) < 0 ) { fprintf(stderr, "no loc configs\n"); return 1; }
    if( !ToriRSServer_SceneBuild(argv[1], atoi(argv[2]) >> 3, atoi(argv[3]) >> 3) ) { fprintf(stderr, "no scene\n"); return 1; }
    while( fgets(line, sizeof line, stdin) )
    {
        int n = sscanf(line, " %c %d %d %d %d %d %d %d %d %d", &kind, &a[0], &a[1], &a[2], &a[3], &a[4], &a[5], &a[6], &a[7], &a[8]);
        int r = -1;
        if( kind == 'F' && n >= 4 ) r = ToriRSServer_SceneTileFlags(a[0], a[1], a[2]);
        else if( kind == 'S' && n >= 10 ) r = ToriRSServer_SceneLineOfSight(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], 0);
        else if( kind == 'W' && n >= 10 ) r = ToriRSServer_SceneLineOfWalk(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], 0);
        else if( kind == 'A' && n >= 10 ) r = ToriRSServer_SceneApproached(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8]);
        else if( kind == 'T' && n >= 7 ) r = ToriRSServer_SceneCanTravel(a[0], a[1], a[2], a[3], a[4], a[5], 0);
        printf("%d\n", r);
        fflush(stdout);
    }
    return 0;
}
