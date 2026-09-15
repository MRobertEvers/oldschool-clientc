#include "cs2vm2/cs2vm2.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

static void run_case(int initial,int offset,int count,const int* expected)
{
    uint16_t ops[]={CS2_OP_PUSH_CONSTANT_INT,CS2_OP_DEFINE_ARRAY,
        CS2_OP_PUSH_CONSTANT_INT,CS2_OP_PUSH_CONSTANT_INT,CS2_OP_POP_ARRAY_INT,
        CS2_OP_PUSH_CONSTANT_INT,CS2_OP_PUSH_CONSTANT_INT,CS2_OP_POP_ARRAY_INT,
        CS2_OP_PUSH_CONSTANT_INT,CS2_OP_PUSH_CONSTANT_INT,CS2_OP_POP_ARRAY_INT,
        CS2_OP_PUSH_CONSTANT_INT,CS2_OP_PUSH_CONSTANT_INT,CS2_OP_POP_ARRAY_INT,
        CS2_OP_PUSH_STRING_LOCAL,CS2_OP_PUSH_CONSTANT_INT,CS2_OP_PUSH_CONSTANT_INT,
        CS2_OP_PUSH_CONSTANT_INT,CS2_OP_ARRAY_FILL_SEQUENCE,
        CS2_OP_PUSH_CONSTANT_INT,CS2_OP_PUSH_ARRAY_INT,
        CS2_OP_PUSH_CONSTANT_INT,CS2_OP_PUSH_ARRAY_INT,
        CS2_OP_PUSH_CONSTANT_INT,CS2_OP_PUSH_ARRAY_INT,
        CS2_OP_PUSH_CONSTANT_INT,CS2_OP_PUSH_ARRAY_INT,CS2_OP_RETURN};
    int values[]={4,'i',0,99,0,1,99,0,2,99,0,3,99,0,0,initial,offset,count,0,0,0,1,0,2,0,3,0,0};
    char* strings[28]={0};
    struct CS2VM2 vm;
    struct CS2VM2_Script script={0};
    struct CS2VM2_ThreadError error={0};
    script.script_id=998011; script.local_string_count=1; script.op_count=28;
    script.opcodes=ops;script.int_operands=values;script.string_operands=strings;
    CS2VM2_Init(&vm);
    struct CS2VM2_Thread* thread=CS2VM2_ThreadMain(&vm);
    if(CS2VM2_ThreadStart(thread,&script)!=CS2VM_EXECNO_OK ||
       CS2VM2_ThreadRun(thread,&error)!=CS2VM2_THREAD_DONE ||
       thread->ints_stack_top!=4 || thread->strs_stack_top!=0) exit(1);
    for(int i=0;i<4;++i) if(thread->ints_stack[i]!=expected[i])
    {
        fprintf(stderr,"sequence(%d,%d,%d) slot%d got%d expected%d\n",
                initial,offset,count,i,thread->ints_stack[i],expected[i]);exit(1);
    }
    CS2VM2_Free(&vm);
}
int main(void)
{
    int identity[]={0,1,2,3};
    int range[]={99,7,8,99};
    int clamp[]={9,10,11,12};
    int wrap[]={INT_MAX,INT_MIN,INT_MIN+1,INT_MIN+2};
    int empty[]={99,99,99,99};
    run_case(0,-1,-1,identity);run_case(7,1,2,range);
    run_case(9,-5,100,clamp);run_case(INT_MAX,0,-1,wrap);
    run_case(99,7,2,empty);run_case(99,0,0,empty);
    puts("PASS native cargo slot sequence, partial/clamped ranges, integer wrap and stack balance");
    return 0;
}
