#include "cs2vm2/cs2vm2.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check(int type,int value,int offset,int count,const char* text,
                  const int* expected,int error_expected)
{
    uint16_t ops[]={CS2_OP_PUSH_CONSTANT_INT,CS2_OP_DEFINE_ARRAY,
        CS2_OP_PUSH_STRING_LOCAL,CS2_OP_PUSH_CONSTANT_INT,
        CS2_OP_PUSH_CONSTANT_INT,CS2_OP_PUSH_CONSTANT_INT,CS2_OP_PUSH_CONSTANT_INT,
        CS2_OP_ARRAY_FILL,CS2_OP_PUSH_CONSTANT_INT,CS2_OP_PUSH_ARRAY_INT,
        CS2_OP_PUSH_CONSTANT_INT,CS2_OP_PUSH_ARRAY_INT,
        CS2_OP_PUSH_CONSTANT_INT,CS2_OP_PUSH_ARRAY_INT,
        CS2_OP_PUSH_CONSTANT_INT,CS2_OP_PUSH_ARRAY_INT,CS2_OP_RETURN};
    int values[]={4,type==0 ? (value==-1 ? 208:'i'):'s',0,value,offset,count,type,0,0,0,1,0,2,0,3,0,0};
    char* strings[17]={0};
    if(type==2){ops[3]=CS2_OP_PUSH_CONSTANT_STRING;strings[3]=(char*)text;}
    /* A null typed value consumes no value stack. */
    if(type==-1) {ops[3]=CS2_OP_PUSH_CONSTANT_INT;values[3]=99;}
    struct CS2VM2 vm; CS2VM2_Init(&vm);
    struct CS2VM2_Script script={.script_id=998010,.local_string_count=1,.op_count=17,
        .opcodes=ops,.int_operands=values,.string_operands=strings};
    struct CS2VM2_Thread* t=CS2VM2_ThreadMain(&vm);
    struct CS2VM2_ThreadError err={0};
    if(CS2VM2_ThreadStart(t,&script)!=CS2VM_EXECNO_OK)exit(1);
    int state=CS2VM2_ThreadRun(t,&err);
    if(error_expected){if(state==CS2VM2_THREAD_DONE)exit(1);CS2VM2_Free(&vm);return;}
    if(state!=CS2VM2_THREAD_DONE || t->ints_stack_top!=(type==0 ? 4:type==-1 ? 1:0) ||
       t->strs_stack_top!=(type==0 ? 0:4))exit(1);
    for(int i=0;i<4;++i)
        if(type==0 ? t->ints_stack[i]!=expected[i] :
           strcmp(t->strs_stack[i] ? t->strs_stack[i]:"", expected[i] ? text:""))
        {fprintf(stderr,"fill type%d offset%d count%d slot%d mismatch\n",type,offset,count,i);exit(1);}
    CS2VM2_Free(&vm);
}
int main(void)
{
    int all[]={-1,-1,-1,-1},part[]={-1,7,7,-1},ones[]={1,1,1,1},strpart[]={0,1,1,0},empty[]={0,0,0,0};
    check(0,-1,0,-1,NULL,all,0); /* native facilities call */
    check(0,7,1,2,NULL,part,0);
    check(0,1,-20,INT_MAX,NULL,ones,0);
    check(0,9,4,-1,NULL,all,0);
    check(0,9,0,0,NULL,all,0);
    check(0,9,5,0,NULL,all,1);
    check(2,0,1,2,"crew",strpart,0);
    check(2,0,-1,-1,"cargo",ones,0);
    check(-1,0,0,-1,"",empty,0);
    puts("native ARRAY_FILL int/string/null, partial/clamped/overflow ranges and stack balance PASS");
    return 0;
}
