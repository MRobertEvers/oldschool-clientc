#include "../src/osrs/rscache/rscache.u.c"
#include "../src2/toriauxlib/toriauxlib.h"
#include "toriauxlib/c/toriauxlibcache.h"

#include <stdio.h>

#define CS2VM_EXECNO_YIELD -2
#define CS2VM_EXECNO_ERROR -1
#define CS2VM_EXECNO_OK 0

#define CS2VM_STACK_MAX 1024

#define CACHE_PATH "/Users/matthewevers/Documents/git_repos/3draster/cache"

#define CS2VM_USER(vm) ((struct CS2VMX*)(vm))->user

struct CS2VMX
{
    void* user;
    int int_stack[CS2VM_STACK_MAX];
    int int_stack_top;
    int string_stack[CS2VM_STACK_MAX];
    int string_stack_top;
};

struct CS2VM_HostRequest
{
    int op;
};

int
CS2VMX_HostExec(
    struct CS2VMX* vm,
    struct CS2VM_HostRequest* request)
{
    void* user = CS2VM_USER(vm);

    switch( request->op )
    {
    default:
        return CS2VM_EXECNO_ERROR;
    }

    return CS2VM_EXECNO_ERROR;
}

int
CS2VMX_RunScript(
    struct CS2VMX* vm,
    struct CS2_Script* script)
{
    return 0;
}

int
main(
    int argc,
    char** argv)
{
    struct RSCacheDat2Disk* cache = NULL;
    struct RSCacheDat2Disk_Archive* archive = NULL;
    RSCacheDat2A_Component* component = NULL;
    struct RSCacheShared_RSBuffer buffer;
    struct ToriAuxLib* toriauxlib = NULL;
    struct ToriAuxLibCore_Component* component_core = NULL;

    cache = RSCacheDat2Disk_NewFromDirectory(CACHE_PATH);
    if( !cache )
    {
        fprintf(stderr, "failed to open cache: %s\n", CACHE_PATH);
        return 1;
    }

    struct CS2VMX vm;
    memset(&vm, 0, sizeof(vm));

#define BANK_INTERFACE 12
#define INTERFACE_ID BANK_INTERFACE

    archive = RSCacheDat2Disk_ArchiveNewLoad(cache, RSCacheDat2Disk_Table_Interfaces, INTERFACE_ID);
    if( !archive )
    {
        fprintf(stderr, "failed to load archive: %d\n", INTERFACE_ID);
        return 1;
    }

    component = calloc(1, sizeof(RSCacheDat2A_Component));
    RSCacheShared_RSBufferInit(&buffer, archive->data, archive->data_size);
    RSCacheDat2A_ComponentDecodeIf3(component, &buffer);

    struct ToriDraw_Scene* scene = ToriDraw_SceneNew(0);
    if( !scene )
    {
        fprintf(stderr, "failed to create scene: %d\n", INTERFACE_ID);
        return 1;
    }

    toriauxlib = ToriAuxLib_New(TORIAUXLIBCACHE_MODE_DAT2, NULL);
    if( !toriauxlib )
    {
        fprintf(stderr, "failed to create toriauxlib: %d\n", INTERFACE_ID);
        return 1;
    }

    component_core = ToriAuxLibCache_ComponentNewFromCacheDat2Component(component);
    if( !component_core )
    {
        fprintf(stderr, "failed to create component: %d\n", INTERFACE_ID);
        return 1;
    }

    return 0;
}