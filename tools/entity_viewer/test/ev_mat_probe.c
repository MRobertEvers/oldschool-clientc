#include "asset_access.h"
#include "tool_profile.h"
#include "dat2disk.h"
#include "datatypes/dat2_proctexture.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc,char**argv){
    struct RSCache p; struct Tool_Dat2Cache c;
    tool_resolve_profile(argv[2],NULL,NULL,NULL,NULL,&p);
    tool_dat2_open(argv[1],&p,&c);
    int t = RSCache_Dat2DiskTableId(c.disk, RSCACHE_DAT2_TABLE_MATERIALS);
    struct RSCache_Dat2DiskArchive* a = RSCache_Dat2DiskArchiveNewLoad(c.disk,t,0);
    struct RSCache_Dat2MaterialTable* mt =
        RSCache_Dat2MaterialTableNewDecode(a->data,a->data_size,RSCache_Dat2ProcTextureFlags(&c.profile));
    printf("materials=%d extended=%d\n", mt->count, mt->has_extended);
    for(int i=3;i<argc;i++){
        int id=atoi(argv[i]);
        if(id<0||id>=mt->count){printf("  %d out of range\n",id);continue;}
        struct RSCache_Dat2Material* m=&mt->materials[id];
        printf("  mat %-5d exists=%d bright=%-4d blanch=%-4d shader=%-3d param=%-4d param2=%-6d "
               "alpha_mode=%d combine=%d repeat_s=%d repeat_t=%d avg_hsl=%d\n",
               id, m->exists, m->brightness, m->blanch, m->shader_id, m->shader_param,
               m->shader_param2, m->alpha_mode, m->combine_mode, m->repeat_s, m->repeat_t, m->average_hsl);
    }
    return 0;
}
