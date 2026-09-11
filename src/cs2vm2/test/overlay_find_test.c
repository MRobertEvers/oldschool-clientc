#include "cs2vm2/cs2vm2.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { ROOT_ID=0x12340001, CHILD_ID=0x12340002, OVERLAY_ID=41 };
struct Host {int creates,finds,timers,children,texts,errors;int dot;};

static int
host_exec(struct CS2VM2_Thread* thread,struct CS2VM_HostRequest* request)
{
    struct Host* host=CS2VM_USER(thread);
    assert(host);
    switch( request->kind )
    {
    case CS2VM_HOST_REQUEST_OVERLAY_NPC_CREATE:
        if( request->u.OVERLAY_NPC_CREATE.arg_count!=5 ) ++host->errors;
        ++host->creates;
        return CS2VM2_PushInt(thread,OVERLAY_ID);
    case CS2VM_HOST_REQUEST_OVERLAY_CC_FIND:
    case CS2VM_HOST_REQUEST_OVERLAY_FIND:
    {
        int count,index,dot;
        if( request->kind==CS2VM_HOST_REQUEST_OVERLAY_CC_FIND )
        {count=request->u.OVERLAY_CC_FIND.arg_count;index=request->u.OVERLAY_CC_FIND.args[0];dot=request->u.OVERLAY_CC_FIND.dot_operand;}
        else
        {count=request->u.OVERLAY_FIND.arg_count;index=request->u.OVERLAY_FIND.args[0];dot=request->u.OVERLAY_FIND.dot_operand;}
        int child=request->kind==CS2VM_HOST_REQUEST_OVERLAY_CC_FIND;
        if( count!=(child?2:1) || index!=OVERLAY_ID || dot!=host->dot ) ++host->errors;
        if( child && request->u.OVERLAY_CC_FIND.args[1]!=0 ) ++host->errors;
        ++host->finds;
        int found=host->creates && (!child || host->children);
        if( found ) CS2VM2_SetTargetComponentId(thread,dot,child?CHILD_ID:ROOT_ID);
        return CS2VM2_PushInt(thread,found);
    }
    case CS2VM_HOST_REQUEST_CC_SETONTIMER:
        if( request->u.CC_SETONTIMER.component_id!=ROOT_ID ||
            request->u.CC_SETONTIMER.script_id!=6696 ) ++host->errors;
        ++host->timers;
        return CS2VM_EXECNO_OK;
    case CS2VM_HOST_REQUEST_OVERLAY_CC_CREATE:
        if( request->u.OVERLAY_CC_CREATE.arg_count!=3 ||
            request->u.OVERLAY_CC_CREATE.args[0]!=OVERLAY_ID ||
            request->u.OVERLAY_CC_CREATE.args[1]!=4 ||
            request->u.OVERLAY_CC_CREATE.args[2]!=0 ) ++host->errors;
        ++host->children;
        CS2VM2_SetTargetComponentId(thread,request->u.OVERLAY_CC_CREATE.dot_operand,CHILD_ID);
        return CS2VM_EXECNO_OK;
    case CS2VM_HOST_REQUEST_CC_SETTEXT:
        if( request->u.CC_SETTEXT.component_id!=CHILD_ID ||
            strcmp(request->u.CC_SETTEXT.text,"NPC indicator")!=0 ) ++host->errors;
        ++host->texts;
        return CS2VM_EXECNO_OK;
    default:
        ++host->errors;return CS2VM_EXECNO_ERROR;
    }
}

int main(void)
{
    int failures=0;
    for( int dot=0;dot<=1;++dot )
    {
        /* Exact native6695 create/store/load/find sequence; the continuation
         * verifies root listeners before a child has even been created. */
        uint16_t ops[]={0,0,0,0,0,7200,34,33,202,38,0,3,1408,33,0,0,103,33,202,38,33,0,203,38,3,1112,21};
        int operands[]={2,1,1,1,1,0,3,3,0,0,6696,0,0,3,4,0,0,3,0,0,3,0,0,0,0,0,0};
        char* strings[27]={0};strings[11]="";strings[24]="NPC indicator";
        operands[8]=operands[12]=operands[16]=operands[18]=operands[22]=operands[25]=dot;
        struct CS2VM2_Script script={.script_id=6695,.local_int_count=4,.op_count=27,
            .opcodes=ops,.int_operands=operands,.string_operands=strings};
        struct CS2VM2 vm;struct CS2VM2_ThreadError error={0};struct Host host={.dot=dot};
        CS2VM2_Init(&vm);CS2VM2_BindHost(&vm,&host,host_exec);
        struct CS2VM2_Thread* thread=CS2VM2_ThreadMain(&vm);
        CS2VM2_ThreadStart(thread,&script);
        enum CS2VM2_ThreadStatus status=CS2VM2_ThreadRun(thread,&error);
        if( status!=CS2VM2_THREAD_DONE || host.errors || host.creates!=1 || host.finds!=3 ||
            host.timers!=1 || host.children!=1 || host.texts!=1 || thread->ints_stack_top!=0 ||
            thread->strs_stack_top!=0 )
        {++failures;fprintf(stderr,"FAIL native overlay create/find continuation dot=%d status=%d calls=%d/%d/%d/%d/%d errors=%d\n",
            dot,status,host.creates,host.finds,host.timers,host.children,host.texts,host.errors);}
        CS2VM2_Free(&vm);
    }
    for( int opcode=202;opcode<=203;++opcode )
    {
        uint16_t ops[]={0,0,(uint16_t)opcode,21};int operands[]={OVERLAY_ID,0,0,0};char* strings[4]={0};
        int start=opcode==202?1:0;
        if( start ) operands[1]=OVERLAY_ID;
        struct CS2VM2_Script script={.script_id=6677,.op_count=4-start,.opcodes=ops+start,
            .int_operands=operands+start,.string_operands=strings+start};
        struct CS2VM2 vm;struct CS2VM2_ThreadError error={0};struct Host host={0};
        CS2VM2_Init(&vm);CS2VM2_BindHost(&vm,&host,host_exec);
        struct CS2VM2_Thread* thread=CS2VM2_ThreadMain(&vm);
        CS2VM2_ThreadStart(thread,&script);
        enum CS2VM2_ThreadStatus status=CS2VM2_ThreadRun(thread,&error);
        if( status!=CS2VM2_THREAD_DONE || host.finds!=1 || host.errors ||
            thread->ints_stack_top!=1 || thread->ints_stack[0]!=0 )
        {++failures;fprintf(stderr,"FAIL missing overlay control opcode=%d\n",opcode);}
        CS2VM2_Free(&vm);
    }
    printf("native overlay find: create/find/timer/child/text and missing controls, %d failures\n",failures);
    return failures?1:0;
}
