#include "asset_access.h"
#include "tool_profile.h"
#include "dat2disk.h"
#include "reference_table.h"
#include <stdio.h>
int main(int argc, char** argv){
    struct RSCache p; struct Tool_Dat2Cache c;
    tool_resolve_profile(argv[2],NULL,NULL,NULL,NULL,&p);
    if(!tool_dat2_open(argv[1],&p,&c)){printf("open fail\n");return 1;}
    int t = RSCache_Dat2DiskTableId(c.disk, RSCACHE_DAT2_TABLE_TEXTURES);
    printf("textures logical->physical table = %d\n", t);
    struct RSCache_Dat2DiskArchive* ref = RSCache_Dat2DiskArchiveNewReferenceTableLoad(c.disk,t);
    if(!ref){printf("no reference table\n");return 1;}
    struct RSCache_ReferenceTable* rt = RSCache_ReferenceTableNewDecode(ref->data, ref->data_size);
    printf("archives in table: %d  first ids:", rt->id_count);
    for(int i=0;i<5 && i<rt->id_count;i++) printf(" %d", rt->ids[i]);
    printf("\n");
    for(int i=0;i<3 && i<rt->id_count;i++){
        struct RSCache_Dat2DiskArchive* a = RSCache_Dat2DiskArchiveNewLoad(c.disk,t,rt->ids[i]);
        if(!a){printf("  archive %d: LOAD FAILED\n", rt->ids[i]); continue;}
        printf("  archive %d: %d bytes, file_count=%d, file_ids=%s\n",
               rt->ids[i], a->data_size, a->file_count, a->file_ids?"yes":"NULL");
        RSCache_Dat2DiskArchiveFree(a);
    }
    return 0;
}
