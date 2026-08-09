#include "parity_exec.h"

#include "parity_sha256.h"

#include "osrs/rscache/dat2a/dat2a_sprites.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
parity_sprite_case(
    struct RSCacheDat2Disk* cache,
    struct ParitySpriteCase const* sp_case,
    char const* out_dir)
{
    if( !cache || !sp_case || !out_dir )
        return -1;

    struct RSCacheDat2A_SpritePack* pack =
        RSCacheDat2A_SpritePackNewFromCache(cache, sp_case->sprite_id);
    if( !pack || pack->count <= 0 || !pack->palette )
    {
        if( pack )
            RSCacheDat2A_SpritePackFree(pack);
        return -1;
    }

    int* spr_px = RSCacheDat2A_SpriteGetPixels(&pack->sprites[0], pack->palette, 0);
    int sw = pack->sprites[0].crop_width;
    int sh = pack->sprites[0].crop_height;
    int ox = pack->sprites[0].offset_x;
    int oy = pack->sprites[0].offset_y;
    int cw = pack->sprites[0].width > 0 ? pack->sprites[0].width : sw;
    int ch = pack->sprites[0].height > 0 ? pack->sprites[0].height : sh;
    if( cw <= 0 )
        cw = sw;
    if( ch <= 0 )
        ch = sh;

    unsigned char* rgba = calloc((size_t)cw * (size_t)ch, 4);
    if( !rgba )
    {
        free(spr_px);
        RSCacheDat2A_SpritePackFree(pack);
        return -1;
    }

    for( int y = 0; y < sh; y++ )
    {
        for( int x = 0; x < sw; x++ )
        {
            int p = spr_px[y * sw + x];
            if( (p & 0xFF000000) == 0 )
                continue;
            int dx = x + ox;
            int dy = y + oy;
            if( dx < 0 || dy < 0 || dx >= cw || dy >= ch )
                continue;
            int di = (dy * cw + dx) * 4;
            rgba[di] = (p >> 16) & 0xFF;
            rgba[di + 1] = (p >> 8) & 0xFF;
            rgba[di + 2] = p & 0xFF;
            rgba[di + 3] = (p >> 24) & 0xFF;
            if( rgba[di + 3] == 0 )
                rgba[di + 3] = 255;
        }
    }

    char hash[65];
    parity_sha256_hex(rgba, (size_t)cw * (size_t)ch * 4, hash);

    char meta_path[512];
    char rgba_path[512];
    snprintf(meta_path, sizeof(meta_path), "%s/sprite_%s.json", out_dir, sp_case->id);
    snprintf(rgba_path, sizeof(rgba_path), "%s/sprite_%s.rgba", out_dir, sp_case->id);

    FILE* meta = fopen(meta_path, "w");
    if( !meta )
    {
        free(rgba);
        free(spr_px);
        RSCacheDat2A_SpritePackFree(pack);
        return -1;
    }
    fprintf(meta, "{\n");
    fprintf(meta, "  \"caseId\": \"%s\",\n", sp_case->id);
    fprintf(meta, "  \"spriteId\": %d,\n", sp_case->sprite_id);
    fprintf(meta, "  \"width\": %d,\n", cw);
    fprintf(meta, "  \"height\": %d,\n", ch);
    fprintf(meta, "  \"subWidth\": %d,\n", pack->sprites[0].crop_width);
    fprintf(meta, "  \"subHeight\": %d,\n", pack->sprites[0].crop_height);
    fprintf(meta, "  \"xOffset\": %d,\n", ox);
    fprintf(meta, "  \"yOffset\": %d,\n", oy);
    fprintf(meta, "  \"pixelHash\": \"%s\"\n", hash);
    fprintf(meta, "}\n");
    fclose(meta);

    FILE* rgba_fp = fopen(rgba_path, "wb");
    if( !rgba_fp )
    {
        free(rgba);
        free(spr_px);
        RSCacheDat2A_SpritePackFree(pack);
        return -1;
    }
    unsigned char header[8];
    header[0] = (unsigned char)(cw);
    header[1] = (unsigned char)(cw >> 8);
    header[2] = (unsigned char)(cw >> 16);
    header[3] = (unsigned char)(cw >> 24);
    header[4] = (unsigned char)(ch);
    header[5] = (unsigned char)(ch >> 8);
    header[6] = (unsigned char)(ch >> 16);
    header[7] = (unsigned char)(ch >> 24);
    fwrite(header, 1, 8, rgba_fp);
    fwrite(rgba, 1, (size_t)cw * (size_t)ch * 4, rgba_fp);
    fclose(rgba_fp);

    free(rgba);
    free(spr_px);
    RSCacheDat2A_SpritePackFree(pack);
    return 0;
}
