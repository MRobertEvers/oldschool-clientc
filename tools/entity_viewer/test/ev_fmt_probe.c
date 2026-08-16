#include "asset_access.h"
#include "tool_profile.h"
#include "datatypes/dat2_config_npc.h"
#include "datatypes/model.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc,char**argv){
    struct RSCache p; struct Tool_Dat2Cache c;
    tool_resolve_profile(argv[2],NULL,NULL,NULL,NULL,&p);
    tool_dat2_open(argv[1],&p,&c);
    struct RSCache_Dat2ConfigNpc* n = tool_dat2_npc_load(&c, atoi(argv[3]));
    if(!n){printf("no npc\n");return 1;}
    for(int i=0;i<n->models_count;i++){
        struct RSCache_Model* m = tool_dat2_model_load(&c, n->models[i]);
        if(!m){printf("  model %d: none\n", n->models[i]);continue;}
        printf("  model %-6d texfaces=%-4d ", n->models[i], m->textured_face_count);
        int rt[4]={0,0,0,0};
        for(int t=0;t<m->textured_face_count;t++){
            int k=m->texture_render_types?m->texture_render_types[t]:0;
            if(k>=0&&k<4)rt[k]++;
        }
        printf("rt=[%d,%d,%d,%d] ", rt[0],rt[1],rt[2],rt[3]);
        printf("scale_x=%s ", m->texture_scale_x?"y":"NULL");
        if(m->texture_scale_x)
            for(int t=0;t<m->textured_face_count && t<4;t++)
                printf("[%d: sx=%d sy=%d sz=%d rot=%d]",
                    t, m->texture_scale_x[t], m->texture_scale_y[t], m->texture_scale_z[t],
                    m->texture_rotation?m->texture_rotation[t]:0);
        printf("\n");
        RSCache_ModelFree(m);
    }
    return 0;
}
