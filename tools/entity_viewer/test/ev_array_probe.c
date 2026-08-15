#include "asset_access.h"
#include "tool_profile.h"
#include "engine/torirs_model_from_rscache.h"
#include "engine/torirs_types.h"
#include "datatypes/model.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc,char**argv){
    struct RSCache p; struct Tool_Dat2Cache c;
    tool_resolve_profile(argv[2],NULL,NULL,NULL,NULL,&p);
    tool_dat2_open(argv[1],&p,&c);
    for(int a=3;a<argc;a++){
        int id=atoi(argv[a]);
        struct RSCache_Model* rs=tool_dat2_model_load(&c,id);
        if(!rs){printf("model %d: none\n",id);continue;}
        printf("model %-6d faces=%-5d texfaces=%-4d | rs: p=%s m=%s n=%s rt=%s coords=%s\n",
            id, rs->face_count, rs->textured_face_count,
            rs->textured_p_coordinate?"y":"NULL", rs->textured_m_coordinate?"y":"NULL",
            rs->textured_n_coordinate?"y":"NULL", rs->texture_render_types?"y":"NULL",
            rs->face_texture_coords?"y":"NULL");
        struct ToriRS_Model* m=ToriRS_ModelFromRSCache(rs);
        RSCache_ModelFree(rs);
        if(!m){printf("   torirs: NULL\n");continue;}
        printf("   torirs: texfaces=%-4d p=%s m=%s n=%s rt=%s coords=%s colors_a=%s infos=%s alphas=%s\n",
            m->textured_face_count,
            m->textured_p_coordinate?"y":"NULL", m->textured_m_coordinate?"y":"NULL",
            m->textured_n_coordinate?"y":"NULL", m->texture_render_types?"y":"NULL",
            m->face_texture_coords?"y":"NULL", m->face_colors_a?"y":"NULL",
            m->face_infos?"y":"NULL", m->face_alphas?"y":"NULL");
        ToriRS_ModelFree(m);
    }
    return 0;
}
