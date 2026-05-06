/* Compatibility header — gameproto_core_parse has been renamed.
 * Include serverprot_core_parse.h instead.
 * Kept to allow old code to compile without touching every include at once.
 */
#include "osrs/core/serverprot_core_parse.h"

/* Old name shim (inline). */
static inline int
gameproto_core_parse_maprebuild8_z16_x16_v1(
    uint8_t* data, int n, int* out_zonex, int* out_zonez)
{
    return serverprot_core_parse_maprebuild_v1(data, n, out_zonex, out_zonez);
}
