#ifndef OSRS_CORE_CLIENTPROT_MOVE_PATH_H
#define OSRS_CORE_CLIENTPROT_MOVE_PATH_H

struct RSBuffer;

/** Client.ts tryMove payload: p1(2*bufferSize+3), p1(run), p2(start+base), p2(startZ+base),
 * then (bufferSize-1) pairs of p1(dx) p1(dz) relative to start tile.
 * @param path_x path_z scene-local tiles; path[0] = first step from player, path[path_len-1] = dest.
 */
void
clientprot_move_path_write_subpacket(
    struct RSBuffer* b,
    int run,
    int map_build_base_x,
    int map_build_base_z,
    const int* path_x,
    const int* path_z,
    int path_len);

#endif
