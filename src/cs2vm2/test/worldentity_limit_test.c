/* Real host + bytecode tests for native extension opcodes 7900/7901. */
#include "cs2vm2/cs2vm2.h"
#include "cs2vm2/cs2vm2_script.h"
#include "game/rs_cs2_host.h"
#include "game/rs_worldmap.h"
#include "engine/cache_provider.h"
#include "inv/inv_manager.h"
#include "ui/uitree.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

static int run(struct RS_CS2Host* host, int set, int value)
{
    struct CS2VM2 vm; struct CS2VM2_Script script;
    uint16_t ops[]={CS2_OP_PUSH_CONSTANT_INT,CS2_OP_WORLDENTITY_SETDRAWLIMIT,
                    CS2_OP_WORLDENTITY_GETDRAWLIMIT,CS2_OP_RETURN};
    int operands[]={value,0,0,0}; char* strings[]={NULL,NULL,NULL,NULL};
    CS2VM2_Init(&vm); CS2VM2_BindHost(&vm,host,RS_CS2Host_Exec);
    CS2VM2_ScriptInit(&script); script.script_id=9997;
    script.op_count=set ? 4:2; script.opcodes=ops+(set ? 0:2);
    script.int_operands=operands+(set ? 0:2); script.string_operands=strings+(set ? 0:2);
    struct CS2VM2_Thread* thread=CS2VM2_ThreadMain(&vm);
    CS2VM2_PushCallScript(thread,&script); CS2VM2_RunScript(thread);
    int result=-1; assert(CS2VM2_PopInt(thread,&result)==CS2VM_EXECNO_OK);
    CS2VM2_Free(&vm); return result;
}
int main(void)
{
    struct RS_CS2Host* host=calloc(1,sizeof(*host)); assert(host);
    struct UITree tree={0}; struct CacheProvider provider={0}; struct InvManager invs;
    InvManager_Init(&invs);
    RS_CS2Host_Init(host,&tree,&provider,&invs,NULL,NULL,NULL);
    assert(run(host,0,0)==30);
    assert(run(host,1,0)==0);
    assert(run(host,1,-1)==0);
    assert(run(host,1,INT_MIN)==0);
    assert(run(host,1,2)==2);
    assert(run(host,1,INT_MAX)==INT_MAX);
    assert(run(host,0,0)==INT_MAX);
    RS_WorldMap_Free(host->worldmap); InvManager_Free(&invs); free(host);
    puts("native 7900/7901 default30, setter/getter, zero and nonnegative clamp PASS");
    return 0;
}
