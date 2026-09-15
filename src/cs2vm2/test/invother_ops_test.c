/* Native cargo reads a remote inventory namespace, including after updates.
 * Exercise real VM dispatch + host + storage, with conflicting local items. */
#include "cs2vm2/cs2vm2.h"
#include "cs2vm2/cs2vm2_script.h"
#include "game/rs_cs2_host.h"
#include "inv/inv_manager.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static int failures;
static int read_op(struct RS_CS2Host* host, int opcode, int inv, int arg)
{
    struct CS2VM2 vm;
    struct CS2VM2_Script script;
    uint16_t ops[] = { CS2_OP_PUSH_CONSTANT_INT, CS2_OP_PUSH_CONSTANT_INT,
                       (uint16_t)opcode, CS2_OP_RETURN };
    int operands[] = {inv,arg,0,0};
    char* strings[] = {NULL,NULL,NULL,NULL};
    CS2VM2_Init(&vm);
    CS2VM2_BindHost(&vm,host,RS_CS2Host_Exec);
    CS2VM2_ScriptInit(&script);
    script.script_id=9998; script.op_count=4;
    script.opcodes=ops; script.int_operands=operands; script.string_operands=strings;
    struct CS2VM2_Thread* thread=CS2VM2_ThreadMain(&vm);
    CS2VM2_PushCallScript(thread,&script);
    CS2VM2_RunScript(thread);
    int value=-999;
    if( CS2VM2_PopInt(thread,&value)!=CS2VM_EXECNO_OK ) failures++;
    CS2VM2_Free(&vm);
    return value;
}
#define CHECK(op,inv,arg,want) do { int got=read_op(host,op,inv,arg); \
    if(got!=(want)){fprintf(stderr,"opcode%d inv%d arg%d: got%d expected%d\n",op,inv,arg,got,want);failures++;} } while(0)
int main(void)
{
    struct InvManager invs;
    InvManager_Init(&invs);
    struct RS_CS2Host* host=calloc(1,sizeof(*host));
    assert(host);
    host->invs=&invs;
    int ids[]={995,1265,995}, counts[]={10,1,30};
    int own_ids[]={1731}, own_counts[]={1};
    InvManager_EnsureContainer(&invs,963,3,"local");
    InvManager_EnsureContainer(&invs,963+32768,3,"cargo");
    InvManager_ApplyFull(&invs,963,own_ids,own_counts,1);
    InvManager_ApplyFull(&invs,963+32768,ids,counts,3);
    CHECK(CS2_OP_INV_GETOBJ,963,0,1731);
    CHECK(CS2_OP_INVOTHER_GETOBJ,963,0,995);
    CHECK(CS2_OP_INVOTHER_GETNUM,963,2,30);
    CHECK(CS2_OP_INVOTHER_TOTAL,963,995,40);
    CHECK(CS2_OP_INVOTHER_GETOBJ,964,0,-1);
    CHECK(CS2_OP_INVOTHER_GETNUM,964,0,0);
    CHECK(CS2_OP_INVOTHER_TOTAL,964,995,0);
    CHECK(CS2_OP_INVOTHER_GETOBJ,963,99,-1);
    CHECK(CS2_OP_INVOTHER_GETNUM,963,-1,0);
    counts[0]=15;
    InvManager_ApplyFull(&invs,963+32768,ids,counts,3);
    CHECK(CS2_OP_INVOTHER_TOTAL,963,995,45);
    CHECK(CS2_OP_INV_GETOBJ,963,0,1731);
    InvManager_Free(&invs);
    free(host);
    printf("native cargo inventory: %d failures\n",failures);
    return failures ? 1:0;
}
