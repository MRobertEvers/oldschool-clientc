#ifndef GAMEPROTO_LC254_U_C
#define GAMEPROTO_LC254_U_C

static void
lc254_process(
    struct GGame* game,
    int packet_type,
    uint8_t* data,
    int data_size)
{
    switch( packet_type )
    {
    case PKTIN_LC254_REBUILD_NORMAL:
    {
        int zonex, zonez;
        serverprot_core_parse_maprebuild_v1(data, data_size, &zonex, &zonez);
        break;
    }
    default:
        break;
    }
}

#endif