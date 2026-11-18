/**
 * The `.spawn` loader's suppression row, through its public entry.
 *
 *   spawn_suppress_test <scratch-dir>
 *
 * Writes a two-lane spawn tree under the scratch directory — a "generated"
 * file and an "edited" one beside it, the way areas/world and areas/edited
 * sit — loads it with ToriRSServer_ContentLoadSpawns, and checks that each
 * `-name x z level` row removed exactly the spawns it names (whichever file
 * they came from), kept every other spawn in merge order, and that a tree
 * with no `-` rows loads unchanged. Then a content lane's own rows
 * (`ported/<lane>/configs`), which ToriRSServer_ContentLoadSpawnsWithLanes
 * reads for a lane the build names and for no other. Rows use the `#<id>`
 * escape, so no pack is needed.
 */

#include "torirsserver/torirs_server.h"
#include "torirsserver/torirs_server_content.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int g_checks;
static int g_failures;

static void
check(
    int condition,
    const char* what)
{
    g_checks++;
    if( !condition )
    {
        fprintf(stderr, "FAIL: %s\n", what);
        g_failures++;
    }
}

static void
make_dir(const char* path)
{
    if( mkdir(path, 0755) != 0 && errno != EEXIST )
    {
        fprintf(stderr, "cannot create %s\n", path);
        exit(2);
    }
}

static void
write_text(
    const char* path,
    const char* text)
{
    FILE* f = fopen(path, "wb");
    if( !f || fwrite(text, 1, strlen(text), f) != strlen(text) )
    {
        fprintf(stderr, "cannot write %s\n", path);
        exit(2);
    }
    fclose(f);
}

static int
has_npc(
    int id,
    int x,
    int z,
    int level)
{
    int count = 0;
    const struct ToriRSServerMapNpcSpawn* spawns = ToriRSServer_ContentNpcSpawns(&count);
    int found = 0;

    for( int i = 0; i < count; i++ )
    {
        if( spawns[i].npc_id == id && spawns[i].x == x && spawns[i].z == z
            && spawns[i].level == level )
            found++;
    }
    return found;
}

static const char* k_generated = "// generated: do not hand-edit\n"
                                 "==== NPC ====\n"
                                 "#11                 3200  3200 0\n"
                                 "#11                 3200  3200 0\n"
                                 "#11                 3201  3200 0\n"
                                 "#12                 3200  3200 0\n"
                                 "#11                 3200  3200 1\n"
                                 "\n"
                                 "==== OBJ ====\n"
                                 "#50                 3205  3205 0 3\n"
                                 "#50                 3206  3205 0 1\n";

int
main(
    int argc,
    char** argv)
{
    char root[1024];
    char path[1200];
    int removed;
    int npc_count = 0;
    int obj_count = 0;
    const struct ToriRSServerMapObjSpawn* objs;
    const struct ToriRSServerMapNpcSpawn* npcs;

    if( argc != 2 )
    {
        fprintf(stderr, "usage: spawn_suppress_test <scratch-dir>\n");
        return 2;
    }

    /* ---- a tree with no suppression rows loads unchanged ------------------ */

    snprintf(root, sizeof(root), "%s/plain", argv[1]);
    make_dir(root);
    snprintf(path, sizeof(path), "%s/m50_50.spawn", root);
    write_text(path, k_generated);
    removed = ToriRSServer_ContentLoadSpawns(root);
    npcs = ToriRSServer_ContentNpcSpawns(&npc_count);
    ToriRSServer_ContentObjSpawns(&obj_count);
    check(removed == 0, "no `-` rows: nothing suppressed");
    check(npc_count == 5 && obj_count == 2, "no `-` rows: every spawn loads");
    check(
        npc_count == 5 && npcs[0].npc_id == 11 && npcs[2].x == 3201 && npcs[3].npc_id == 12,
        "no `-` rows: merge order unchanged");
    ToriRSServer_ContentFree();

    /* ---- the edited lane suppresses generated spawns ---------------------- */

    snprintf(root, sizeof(root), "%s/lanes", argv[1]);
    make_dir(root);
    snprintf(path, sizeof(path), "%s/world", root);
    make_dir(path);
    snprintf(path, sizeof(path), "%s/edited", root);
    make_dir(path);
    snprintf(path, sizeof(path), "%s/world/m50_50.spawn", root);
    write_text(path, k_generated);
    snprintf(path, sizeof(path), "%s/edited/m50_50.spawn", root);
    write_text(path,
        "==== NPC ====\n"
        "-#11                3200  3200 0      // both stacked copies go\n"
        "#13                 3210  3210 0\n"
        "-#13                3299  3299 0      // stale: matches nothing\n"
        "\n"
        "==== OBJ ====\n"
        "-#50                3205  3205 0\n");

    removed = ToriRSServer_ContentLoadSpawns(root);
    npcs = ToriRSServer_ContentNpcSpawns(&npc_count);
    objs = ToriRSServer_ContentObjSpawns(&obj_count);
    check(removed == 3, "three spawns suppressed (two npcs on one tile, one obj)");
    check(has_npc(11, 3200, 3200, 0) == 0, "the suppressed npc is gone from its tile");
    check(has_npc(11, 3201, 3200, 0) == 1, "the same npc on another tile stays");
    check(has_npc(11, 3200, 3200, 1) == 1, "the same npc on another level stays");
    check(has_npc(12, 3200, 3200, 0) == 1, "another npc on the same tile stays");
    check(has_npc(13, 3210, 3210, 0) == 1, "the edited lane's own spawn loads");
    check(npc_count == 4, "four npc spawns survive");
    check(
        obj_count == 1 && objs[0].obj_id == 50 && objs[0].x == 3206,
        "the obj suppression removed only its tile's stack");
    check(ToriRSServer_ContentErrorCount() == 0, "suppression rows are not errors");
    (void)npcs;
    ToriRSServer_ContentFree();

    /* ---- a content lane's own rows: read for a lane in the build only ------ */

    {
        char content[1024];
        char scripts[1100];
        const char* in_build[] = { "halloween" };

        snprintf(content, sizeof(content), "%s/content", argv[1]);
        make_dir(content);
        snprintf(scripts, sizeof(scripts), "%s/scripts", content);
        make_dir(scripts);
        snprintf(path, sizeof(path), "%s/m50_50.spawn", scripts);
        write_text(path, k_generated);
        snprintf(path, sizeof(path), "%s/ported", content);
        make_dir(path);
        snprintf(path, sizeof(path), "%s/ported/halloween", content);
        make_dir(path);
        snprintf(path, sizeof(path), "%s/ported/halloween/configs", content);
        make_dir(path);
        snprintf(path, sizeof(path), "%s/ported/halloween/configs/halloween.spawn", content);
        write_text(path,
            "==== NPC ====\n"
            "-#12                3200  3200 0      // the lane takes a base npc away\n"
            "#21                 3220  3220 0\n");
        /* A lane the build does not name, and one with no configs directory. */
        snprintf(path, sizeof(path), "%s/ported/xmas", content);
        make_dir(path);
        snprintf(path, sizeof(path), "%s/ported/xmas/configs", content);
        make_dir(path);
        snprintf(path, sizeof(path), "%s/ported/xmas/configs/xmas.spawn", content);
        write_text(path, "==== NPC ====\n#22                 3230  3230 0\n");
        snprintf(path, sizeof(path), "%s/ported/bare", content);
        make_dir(path);

        removed = ToriRSServer_ContentLoadSpawnsWithLanes(scripts, content, NULL, 0);
        ToriRSServer_ContentNpcSpawns(&npc_count);
        check(removed == 0 && npc_count == 5, "no lane in the build: only the scripts tree's rows");
        check(has_npc(21, 3220, 3220, 0) == 0, "no lane in the build: the lane's npc does not stand");
        check(has_npc(12, 3200, 3200, 0) == 1, "no lane in the build: its suppression does not apply");
        ToriRSServer_ContentFree();

        removed = ToriRSServer_ContentLoadSpawnsWithLanes(scripts, content, in_build, 1);
        ToriRSServer_ContentNpcSpawns(&npc_count);
        check(has_npc(21, 3220, 3220, 0) == 1, "lane in the build: its npc stands");
        check(removed == 1 && has_npc(12, 3200, 3200, 0) == 0, "lane in the build: its suppression applies");
        check(has_npc(22, 3230, 3230, 0) == 0, "a lane the build does not name contributes nothing");
        check(npc_count == 5, "lane in the build: five npc spawns (five, one gone, one added)");
        check(ToriRSServer_ContentErrorCount() == 0, "lane rows are not errors");
        ToriRSServer_ContentFree();

        {
            const char* bare[] = { "bare" };

            removed = ToriRSServer_ContentLoadSpawnsWithLanes(scripts, content, bare, 1);
            ToriRSServer_ContentNpcSpawns(&npc_count);
            check(removed == 0 && npc_count == 5, "a lane with no configs directory contributes nothing");
            ToriRSServer_ContentFree();
        }
    }

    printf("spawn_suppress_test: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
