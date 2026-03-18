#include "tori_rs.h"

#include <emscripten.h>

EMSCRIPTEN_KEEPALIVE
struct ToriRSNetSharedBuffer*
LibToriRS_GetBuffer(struct GGame* game)
{
    return game->net_shared;
}

// Ensure all offset getters are exported
EMSCRIPTEN_KEEPALIVE uint32_t
Export_NetGetSharedBufferSize()
{
    return LibToriRS_NetGetSharedBufferSize();
}
EMSCRIPTEN_KEEPALIVE uint32_t
Export_NetGetOffset_G2P_Data()
{
    return LibToriRS_NetGetOffset_G2P_Data();
}
EMSCRIPTEN_KEEPALIVE uint32_t
Export_NetGetOffset_G2P_Head()
{
    return LibToriRS_NetGetOffset_G2P_Head();
}
EMSCRIPTEN_KEEPALIVE uint32_t
Export_NetGetOffset_G2P_Tail()
{
    return LibToriRS_NetGetOffset_G2P_Tail();
}
EMSCRIPTEN_KEEPALIVE uint32_t
Export_NetGetOffset_P2G_Data()
{
    return LibToriRS_NetGetOffset_P2G_Data();
}
EMSCRIPTEN_KEEPALIVE uint32_t
Export_NetGetOffset_P2G_Head()
{
    return LibToriRS_NetGetOffset_P2G_Head();
}
EMSCRIPTEN_KEEPALIVE uint32_t
Export_NetGetOffset_P2G_Tail()
{
    return LibToriRS_NetGetOffset_P2G_Tail();
}
EMSCRIPTEN_KEEPALIVE uint32_t
Export_NetGetOffset_Status()
{
    return LibToriRS_NetGetOffset_Status();
}