/* Included from main.c — cooperative interface open / CS2 script tasks. */

struct InterfaceX_AwaitPendingAssets
{
    struct pt thread;
    struct InterfaceX_VMHost* host;
    int index;
    int scene_id;
    struct InterfaceX_TaskSpriteLoad* sprite_child;
    struct InterfaceX_TaskFontLoad* font_child;
    struct InterfaceX_TaskModelLoad* model_child;
    struct Task_Dat2ConfigEntryLoad* obj_config_child;
};

struct Task_InterfaceXLoadGroup
{
    struct pt thread;
    struct InterfaceX_VMHost* host;
    int group_id;
    struct InterfaceX_TaskInterfaceLoad* iface_child;
};

struct Task_InterfaceXRunScript
{
    struct pt thread;
    struct InterfaceX_VMHost* host;
    struct CS2VMX* vm;
    int script_id;
    int component_id;
    int int_args[TORIAUXLIBCORE_COMPONENT_HOOK_ARG_MAX];
    int int_arg_count;
    char* string_args[TORIAUXLIBCORE_COMPONENT_HOOK_ARG_MAX];
    int string_arg_count;
    struct CS2VM_HostRequest pending;
    int (*sub_run)(void*, struct LibToriRS_IOContext*);
    void* sub_state;
    bool sub_heap;
    struct InterfaceX_TaskSpriteLoad* sprite_child;
    struct InterfaceX_TaskFontLoad* font_child;
    struct InterfaceX_TaskModelLoad* model_child;
    struct Task_ClientScriptLoad* scripts_child;
    struct Task_Dat2ConfigEntryLoad* config_child;
    struct Task_ToriAuxLibCache_NpcAdd* npc_child;
    struct Task_ToriAuxLibCache_PlayerAdd* player_child;
    struct Task_InterfaceXLoadGroup load_group;
    int scene_id;
};

struct Task_InterfaceXOpen
{
    struct pt thread;
    struct InterfaceX_VMHost* host;
    struct CS2VMX* vm;
    int group_id;
    bool is_root;
    struct InterfaceX_LoadedInterface* loaded_out;
    struct InterfaceX_LoadedInterface loaded_scratch;
    bool ui_ready;
    bool failed;
    struct InterfaceX_TaskInterfaceLoad* iface_child;
    struct Task_ClientScriptLoad* scripts_child;
    struct InterfaceX_AwaitPendingAssets* assets_child;
    struct Task_InterfaceXRunScript run_script;
    struct InterfaceX_BatchModelLoad* model_batch;
    int* prefetch_script_ids;
    int prefetch_script_count;
    int prefetch_script_cap;
    struct InterfaceX_ScriptQueueEntry script_entry;
};

static void
InterfaceX_ResolvePendingAssets(
    struct InterfaceX_VMHost* host,
    struct UITreeXBuilder* builder)
{
    assert(host);
    assert(builder);

    for( int i = 0; i < builder->pending_asset_count; i++ )
    {
        struct InterfaceX_PendingAsset const* pending = &builder->pending_assets[i];
        if( pending->node_idx < 0 || pending->node_idx >= builder->tree->node_count )
            continue;

        struct UITreeXNode* node = &builder->tree->nodes[pending->node_idx];

        switch( pending->kind )
        {
        case INTERFACEX_PENDING_GRAPHIC:
        {
            int scene_id = -1;
            if( InterfaceX_HostIO_GraphicSceneId(&host->host_io, pending->id, &scene_id) &&
                scene_id >= 0 && node->kind == UITreeXNodeKind_RSGraphic )
                UITreeX_NodeRSGraphicMut(node)->scene_id = scene_id;
            break;
        }
        case INTERFACEX_PENDING_FONT:
            break;
        case INTERFACEX_PENDING_OBJ_ICON:
        {
            int scene_id = -1;
            if( InterfaceX_HostIO_ObjIconSceneId(
                    &host->host_io, pending->id, pending->id2, &scene_id) &&
                scene_id >= 0 )
            {
                if( node->kind == UITreeXNodeKind_RSObj )
                    UITreeX_NodeRSObjMut(node)->scene_id = scene_id;
                else if( node->kind == UITreeXNodeKind_RSGraphic )
                    UITreeX_NodeRSGraphicMut(node)->scene_id = scene_id;
            }
            break;
        }
        }
    }
}

static int
InterfaceX_AwaitPendingAssets_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx)
{
    struct InterfaceX_AwaitPendingAssets* task = task_state;
    struct InterfaceX_VMHost* host = task->host;
    struct UITreeXBuilder* builder = host->builder;
    struct Dat2BuildCache* bc = dat2(InterfaceX_HostIO_Cache(&host->host_io));

    PT_BEGIN(&task->thread);

    for( task->index = 0; task->index < builder->pending_asset_count; task->index++ )
    {
        struct InterfaceX_PendingAsset const* pending = &builder->pending_assets[task->index];
        if( pending->node_idx < 0 || pending->node_idx >= builder->tree->node_count )
            continue;

        switch( pending->kind )
        {
        case INTERFACEX_PENDING_GRAPHIC:
        {
            task->scene_id = -1;
            if( InterfaceX_HostIO_GraphicSceneId(&host->host_io, pending->id, &task->scene_id) )
                break;
            InterfaceX_TaskSpriteLoad_Free(task->sprite_child);
            task->sprite_child =
                InterfaceX_TaskSpriteLoad_New(InterfaceX_HostIO_Cache(&host->host_io), pending->id);
            if( task->sprite_child )
                TASK_AWAIT(&task->thread, InterfaceX_TaskSpriteLoad_Run(task->sprite_child, ctx));
            pending = &builder->pending_assets[task->index];
            InterfaceX_HostIO_FinalizeGraphicScene(
                &host->host_io, pending->id, &task->scene_id);
            break;
        }
        case INTERFACEX_PENDING_FONT:
            if( InterfaceX_HostIO_SceneFontGet(&host->host_io, pending->id) )
                break;
            InterfaceX_TaskFontLoad_Free(task->font_child);
            task->font_child =
                InterfaceX_TaskFontLoad_New(InterfaceX_HostIO_Cache(&host->host_io), pending->id);
            if( task->font_child )
                TASK_AWAIT(&task->thread, InterfaceX_TaskFontLoad_Run(task->font_child, ctx));
            pending = &builder->pending_assets[task->index];
            InterfaceX_HostIO_FinalizeSceneFont(&host->host_io, pending->id);
            break;
        case INTERFACEX_PENDING_OBJ_ICON:
        {
            task->scene_id = -1;
            if( InterfaceX_HostIO_ObjIconSceneId(
                    &host->host_io, pending->id, pending->id2, &task->scene_id) )
                break;
            if( !InterfaceX_HostIO_ConfigEntryReady(
                    &host->host_io, RSCacheDat2A_ConfigKind_Object, pending->id) )
            {
                if( !task->obj_config_child )
                    task->obj_config_child = Task_Dat2ConfigEntryLoad_New(
                        InterfaceX_HostIO_Cache(&host->host_io),
                        RSCacheDat2A_ConfigKind_Object,
                        &pending->id,
                        1);
                if( task->obj_config_child )
                {
                    PT_INIT(&task->obj_config_child->thread);
                    TASK_AWAIT(
                        &task->thread,
                        Task_Dat2ConfigEntryLoad_Run(task->obj_config_child, ctx));
                }
            }
            pending = &builder->pending_assets[task->index];
            {
                struct RSCacheDat2A_ConfigObject* obj =
                    dat2_buildcache_object_get(bc, pending->id);
                if( obj && obj->inventory_model_id > 0 &&
                    !dat2_buildcache_model_get(bc, obj->inventory_model_id) )
                {
                    InterfaceX_TaskModelLoad_Free(task->model_child);
                    task->model_child = InterfaceX_TaskModelLoad_New(
                        InterfaceX_HostIO_Cache(&host->host_io), obj->inventory_model_id);
                    if( task->model_child )
                        TASK_AWAIT(
                            &task->thread, InterfaceX_TaskModelLoad_Run(task->model_child, ctx));
                }
            }
            pending = &builder->pending_assets[task->index];
            InterfaceX_HostIO_FinalizeObjIconScene(
                &host->host_io, pending->id, pending->id2, &task->scene_id);
            break;
        }
        }
    }

    InterfaceX_ResolvePendingAssets(host, builder);
    PT_END(&task->thread);
}

static void
InterfaceX_AwaitPendingAssets_Free(void* state)
{
    struct InterfaceX_AwaitPendingAssets* task = state;
    if( !task )
        return;
    InterfaceX_TaskSpriteLoad_Free(task->sprite_child);
    InterfaceX_TaskFontLoad_Free(task->font_child);
    InterfaceX_TaskModelLoad_Free(task->model_child);
    if( task->obj_config_child )
        Task_Dat2ConfigEntryLoad_Free(task->obj_config_child);
    free(task);
}

static int
InterfaceX_LoadGroupIdFromHostRequest(struct CS2VM_HostRequest const* request)
{
    switch( request->kind )
    {
    case CS2VM_HOST_REQUEST_CC_CREATE:
        return (request->u.cc_create.parent_id >> 16) & 0xffff;
    case CS2VM_HOST_REQUEST_CC_FIND:
        return (request->u.cc_find.parent_id >> 16) & 0xffff;
    case CS2VM_HOST_REQUEST_IF_FIND:
        return (request->u.if_find.component_id >> 16) & 0xffff;
    case CS2VM_HOST_REQUEST_CC_CHILDREN_FIND:
        return (request->u.cc_children_find.parent_id >> 16) & 0xffff;
    case CS2VM_HOST_REQUEST_IF_CHILDREN_FIND:
        return (request->u.if_children_find.uid >> 16) & 0xffff;
    default:
        return -1;
    }
}

static bool
InterfaceX_ProcessInterfacePack(
    struct InterfaceX_VMHost* host,
    int group_id,
    struct InterfaceX_LoadedInterface* loaded_out)
{
    struct ToriAuxLibCache* aux_cache;
    struct Dat2BuildCache* bc;
    struct Dat2BuildCache_InterfaceArchive* arch;
    struct ToriAuxLibCore* core;
    int i;
    int integrated = 0;
    bool is_primary;

    assert(host);
    assert(host->builder);
    assert(host->tree);

    if( group_id <= 0 )
        return false;
    if( InterfaceX_IsGroupLoaded(host, group_id) )
        return true;

    is_primary = host->interface_id == group_id;
    if( !is_primary && host->extra_group_count >= INTERFACEX_MAX_LOADED_GROUPS )
    {
        fprintf(stderr, "interface group %d: loaded group cap reached\n", group_id);
        return false;
    }

    aux_cache = InterfaceX_HostIO_Cache(&host->host_io);
    if( !aux_cache )
    {
        fprintf(stderr, "interface group %d: aux cache unavailable\n", group_id);
        return false;
    }

    bc = dat2(aux_cache);
    arch = dat2_buildcache_interface_archive_get(bc, group_id);
    if( !arch || arch->component_count <= 0 )
    {
        fprintf(stderr, "interface group %d: buildcache archive missing\n", group_id);
        return false;
    }

    if( loaded_out )
    {
        memset(loaded_out, 0, sizeof(*loaded_out));
        loaded_out->interface_id = group_id;
        loaded_out->component_count = arch->component_count;
        loaded_out->components =
            calloc((size_t)arch->component_count, sizeof(struct ToriAuxLibCore_Component*));
        loaded_out->owns_components = false;
        if( !loaded_out->components )
            return false;
    }

    core = InterfaceX_HostIO_Core(&host->host_io);
    for( i = 0; i < arch->component_count; i++ )
    {
        int packed_id = (group_id << 16) | (i & 0xFFFF);
        struct ToriAuxLibCore_Component* component_core =
            core ? ToriAuxLibCore_ComponentGet(core, packed_id) : NULL;
        if( !component_core && arch->components[i] )
        {
            component_core =
                ToriAuxLibCache_ComponentNewFromCacheDat2Component(arch->components[i]);
            if( component_core && core )
                ToriAuxLibCore_ComponentAdd(core, packed_id, component_core);
        }
        if( !component_core )
        {
            fprintf(stderr, "interface group %d: missing component file %d\n", group_id, i);
            if( loaded_out && loaded_out->components )
            {
                free(loaded_out->components);
                memset(loaded_out, 0, sizeof(*loaded_out));
            }
            return false;
        }

        if( loaded_out && loaded_out->components )
            loaded_out->components[i] = component_core;
        process_component(host, host->builder, component_core);
        integrated++;
    }

    UITreeXBuilder_ResolvePendingParents(host->builder);
    if( !is_primary )
    {
        InterfaceX_HideInterfaceRoots(host->tree, group_id);
        host->extra_group_ids[host->extra_group_count] = group_id;
        memset(&host->extra_groups[host->extra_group_count], 0, sizeof(host->extra_groups[0]));
        host->extra_groups[host->extra_group_count].interface_id = group_id;
        host->extra_group_count++;
    }
    else
        host->primary_pack_integrated = true;

    fprintf(stderr, "loaded interface group %d (%d components)\n", group_id, integrated);
    return true;
}

static int
Task_InterfaceXLoadGroup_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_InterfaceXLoadGroup* task = task_state;
    struct InterfaceX_VMHost* host = task->host;
    struct Dat2BuildCache* bc = dat2(InterfaceX_HostIO_Cache(&host->host_io));
    int group_id = task->group_id;

    PT_BEGIN(&task->thread);

    if( group_id <= 0 )
        PT_EXIT(&task->thread);

    if( !dat2_buildcache_interface_archive_has(bc, group_id) )
    {
        if( !task->iface_child )
            task->iface_child =
                InterfaceX_TaskInterfaceLoad_New(InterfaceX_HostIO_Cache(&host->host_io), group_id);
        if( task->iface_child )
        {
            TASK_AWAIT(&task->thread, InterfaceX_TaskInterfaceLoad_Run(task->iface_child, ctx));
        }
    }

    InterfaceX_HostIO_InterfaceGroupSubmit(&host->host_io, group_id);
    if( !InterfaceX_ProcessInterfacePack(host, group_id, NULL) )
        PT_EXIT(&task->thread);

    PT_END(&task->thread);
}

static void
Task_InterfaceXRunScript_FreeChild(struct Task_InterfaceXRunScript* task)
{
    if( !task )
        return;
    if( task->sub_heap )
    {
        if( task->sub_run == Task_ClientScriptLoad_Run )
            Task_ClientScriptLoad_Free(task->scripts_child);
        else if( task->sub_run == Task_Dat2ConfigEntryLoad_Run )
            Task_Dat2ConfigEntryLoad_Free(task->config_child);
        else if( task->sub_run == Task_ToriAuxLibCache_NpcAdd_Run )
            Task_ToriAuxLibCache_NpcAdd_Free(task->npc_child);
        else if( task->sub_run == Task_ToriAuxLibCache_PlayerAdd_Run )
            Task_ToriAuxLibCache_PlayerAdd_Free(task->player_child);
    }
    else
    {
        InterfaceX_TaskSpriteLoad_Free(task->sprite_child);
        InterfaceX_TaskFontLoad_Free(task->font_child);
        InterfaceX_TaskModelLoad_Free(task->model_child);
    }
    task->sprite_child = NULL;
    task->font_child = NULL;
    task->model_child = NULL;
    task->scripts_child = NULL;
    task->config_child = NULL;
    task->npc_child = NULL;
    task->player_child = NULL;
    task->sub_run = NULL;
    task->sub_state = NULL;
    task->sub_heap = false;
}

static void
Task_InterfaceXRunScript_SelectLoadChild(struct Task_InterfaceXRunScript* task)
{
    struct InterfaceX_VMHost* host = task->host;
    struct CS2VM_HostRequest const* request = &task->pending;

    task->sub_run = NULL;
    task->sub_state = NULL;
    task->sub_heap = false;

    switch( request->kind )
    {
    case CS2VM_HOST_REQUEST_PUSHSCRIPT:
        if( !InterfaceX_HostIO_ClientScriptGet(
                &host->host_io, request->u.push_script.script_id) )
        {
            task->sub_heap = true;
            task->scripts_child = Task_ClientScriptLoad_New(
                InterfaceX_HostIO_Cache(&host->host_io),
                &request->u.push_script.script_id,
                1);
            task->sub_state = task->scripts_child;
            task->sub_run = Task_ClientScriptLoad_Run;
        }
        return;
    case CS2VM_HOST_REQUEST_PARAHEIGHT:
    case CS2VM_HOST_REQUEST_PARAWIDTH:
        task->font_child = InterfaceX_TaskFontLoad_New(
            InterfaceX_HostIO_Cache(&host->host_io), request->u.para_height.font_id);
        task->sub_state = task->font_child;
        task->sub_run = InterfaceX_TaskFontLoad_Run;
        return;
    case CS2VM_HOST_REQUEST_CC_SETTEXTFONT:
        task->font_child = InterfaceX_TaskFontLoad_New(
            InterfaceX_HostIO_Cache(&host->host_io), request->u.cc_set_text_font.font_id);
        task->sub_state = task->font_child;
        task->sub_run = InterfaceX_TaskFontLoad_Run;
        return;
    case CS2VM_HOST_REQUEST_CC_SETGRAPHIC:
    case CS2VM_HOST_REQUEST_IF_SETGRAPHIC:
        task->sprite_child = InterfaceX_TaskSpriteLoad_New(
            InterfaceX_HostIO_Cache(&host->host_io), request->u.cc_set_graphic.graphic_id);
        task->sub_state = task->sprite_child;
        task->sub_run = InterfaceX_TaskSpriteLoad_Run;
        return;
    case CS2VM_HOST_REQUEST_ENUM_LOOKUP:
        task->sub_heap = true;
        task->config_child = Task_Dat2ConfigEntryLoad_New(
            InterfaceX_HostIO_Cache(&host->host_io),
            RSCacheDat2A_ConfigKind_Enum,
            &request->u.enum_lookup.enum_id,
            1);
        task->sub_state = task->config_child;
        task->sub_run = Task_Dat2ConfigEntryLoad_Run;
        return;
    case CS2VM_HOST_REQUEST_ENUM_GETOUTPUTCOUNT:
        task->sub_heap = true;
        task->config_child = Task_Dat2ConfigEntryLoad_New(
            InterfaceX_HostIO_Cache(&host->host_io),
            RSCacheDat2A_ConfigKind_Enum,
            &request->u.enum_get_output_count.enum_id,
            1);
        task->sub_state = task->config_child;
        task->sub_run = Task_Dat2ConfigEntryLoad_Run;
        return;
    case CS2VM_HOST_REQUEST_STRUCT_PARAM:
        task->sub_heap = true;
        task->config_child = Task_Dat2ConfigEntryLoad_New(
            InterfaceX_HostIO_Cache(&host->host_io),
            RSCacheDat2A_ConfigKind_Struct,
            &request->u.struct_param.struct_id,
            1);
        task->sub_state = task->config_child;
        task->sub_run = Task_Dat2ConfigEntryLoad_Run;
        return;
    case CS2VM_HOST_REQUEST_OC_PARAM:
    case CS2VM_HOST_REQUEST_OC_NAME:
    case CS2VM_HOST_REQUEST_OC_UNPLACEHOLDER:
    case CS2VM_HOST_REQUEST_OC_INT_PARAM:
        task->sub_heap = true;
        task->config_child = Task_Dat2ConfigEntryLoad_New(
            InterfaceX_HostIO_Cache(&host->host_io),
            RSCacheDat2A_ConfigKind_Object,
            &request->u.oc_param.item_id,
            1);
        task->sub_state = task->config_child;
        task->sub_run = Task_Dat2ConfigEntryLoad_Run;
        return;
    case CS2VM_HOST_REQUEST_WIDGET_SET_MODEL:
        if( request->u.widget_set_model.model_id >= 0 )
        {
            task->model_child = InterfaceX_TaskModelLoad_New(
                InterfaceX_HostIO_Cache(&host->host_io), request->u.widget_set_model.model_id);
            task->sub_state = task->model_child;
            task->sub_run = InterfaceX_TaskModelLoad_Run;
        }
        return;
    case CS2VM_HOST_REQUEST_WIDGET_SET_MODEL_KIND:
    {
        enum InterfaceX_ModelKind kind = request->u.widget_set_model_kind.model_kind;
        int model_id = request->u.widget_set_model_kind.model_id;
        if( kind == INTERFACEX_MODEL_KIND_PLAIN && model_id >= 0 )
        {
            task->model_child =
                InterfaceX_TaskModelLoad_New(InterfaceX_HostIO_Cache(&host->host_io), model_id);
            task->sub_state = task->model_child;
            task->sub_run = InterfaceX_TaskModelLoad_Run;
        }
        else if( kind == INTERFACEX_MODEL_KIND_NPC_HEAD && model_id >= 0 )
        {
            task->sub_heap = true;
            task->npc_child =
                Task_ToriAuxLibCache_NpcAdd_New(InterfaceX_HostIO_Cache(&host->host_io), model_id);
            task->sub_state = task->npc_child;
            task->sub_run = Task_ToriAuxLibCache_NpcAdd_Run;
        }
        else if(
            kind == INTERFACEX_MODEL_KIND_PLAYER_HEAD ||
            kind == INTERFACEX_MODEL_KIND_PLAYER_SELF ||
            kind == INTERFACEX_MODEL_KIND_PLAYER_CHATHEAD )
        {
            task->sub_heap = true;
            task->player_child = Task_ToriAuxLibCache_PlayerAdd_New(
                InterfaceX_HostIO_Cache(&host->host_io),
                host->player_appearance,
                RUNESCAPE_EXAMPLE_PLAYER_READYANIM,
                RUNESCAPE_EXAMPLE_PLAYER_WALKANIM,
                RUNESCAPE_EXAMPLE_PLAYER_TURNANIM,
                RUNESCAPE_EXAMPLE_PLAYER_RUNANIM,
                RUNESCAPE_EXAMPLE_PLAYER_WALKANIM_B,
                RUNESCAPE_EXAMPLE_PLAYER_WALKANIM_R,
                RUNESCAPE_EXAMPLE_PLAYER_WALKANIM_L);
            task->sub_state = task->player_child;
            task->sub_run = Task_ToriAuxLibCache_PlayerAdd_Run;
        }
        return;
    }
    case CS2VM_HOST_REQUEST_CC_CREATE:
    case CS2VM_HOST_REQUEST_CC_FIND:
    case CS2VM_HOST_REQUEST_IF_FIND:
    case CS2VM_HOST_REQUEST_CC_CHILDREN_FIND:
    case CS2VM_HOST_REQUEST_IF_CHILDREN_FIND:
        memset(&task->load_group, 0, sizeof(task->load_group));
        PT_INIT(&task->load_group.thread);
        task->load_group.host = host;
        task->load_group.group_id = InterfaceX_LoadGroupIdFromHostRequest(request);
        task->sub_state = &task->load_group;
        task->sub_run = Task_InterfaceXLoadGroup_Run;
        return;
    default:
        fprintf(stderr, "Task_InterfaceXRunScript: unhandled load kind %d\n", (int)request->kind);
        return;
    }
}

static void
Task_InterfaceXRunScript_Reset(
    struct Task_InterfaceXRunScript* task,
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    int script_id,
    int component_id,
    int const* int_args,
    int int_arg_count,
    char const* const* string_args,
    int string_arg_count)
{
    int i;

    assert(task);
    for( i = 0; i < task->string_arg_count; i++ )
        free(task->string_args[i]);
    Task_InterfaceXRunScript_FreeChild(task);
    if( task->load_group.iface_child )
    {
        InterfaceX_TaskInterfaceLoad_Free(task->load_group.iface_child);
        task->load_group.iface_child = NULL;
    }
    memset(task, 0, sizeof(*task));
    PT_INIT(&task->thread);
    task->host = host;
    task->vm = vm;
    task->script_id = script_id;
    task->component_id = component_id;
    task->int_arg_count = int_arg_count;
    if( int_arg_count > TORIAUXLIBCORE_COMPONENT_HOOK_ARG_MAX )
        task->int_arg_count = TORIAUXLIBCORE_COMPONENT_HOOK_ARG_MAX;
    if( int_arg_count > 0 && int_args )
        memcpy(task->int_args, int_args, (size_t)task->int_arg_count * sizeof(task->int_args[0]));
    if( string_arg_count > TORIAUXLIBCORE_COMPONENT_HOOK_ARG_MAX )
        string_arg_count = TORIAUXLIBCORE_COMPONENT_HOOK_ARG_MAX;
    for( i = 0; i < string_arg_count; i++ )
    {
        char const* src = string_args && string_args[i] ? string_args[i] : "";
        task->string_args[i] = strdup(src);
        if( task->string_args[i] )
            task->string_arg_count++;
    }
}

static int
Task_InterfaceXRunScript_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_InterfaceXRunScript* task = task_state;
    struct InterfaceX_VMHost* host = task->host;
    struct CS2VMX* vm = task->vm;
    int res;

    PT_BEGIN(&task->thread);

    if( task->script_id <= 0 )
        PT_EXIT(&task->thread);

    if( !InterfaceX_HostIO_ClientScriptGet(&host->host_io, task->script_id) )
    {
        task->pending.kind = CS2VM_HOST_REQUEST_PUSHSCRIPT;
        task->pending.u.push_script.script_id = task->script_id;
        Task_InterfaceXRunScript_SelectLoadChild(task);
        if( task->sub_run )
            TASK_AWAIT(&task->thread, task->sub_run(task->sub_state, ctx));
        Task_InterfaceXRunScript_FreeChild(task);
        if( !InterfaceX_HostIO_ClientScriptGet(&host->host_io, task->script_id) )
        {
            fprintf(stderr, "failed to resolve script: %d\n", task->script_id);
            PT_EXIT(&task->thread);
        }
    }

    CS2VMX_ResetRuntime(vm);
    CS2VMX_PushCallScript(
        vm, &InterfaceX_HostIO_ClientScriptGet(&host->host_io, task->script_id)->script);
    {
        struct CS2VMX_Frame* frame = CS2VM_FRAME(vm);
        for( int j = 0; j < task->string_arg_count; j++ )
        {
            char const* src = task->string_args[j] ? task->string_args[j] : "";
            frame->str_locals[j] = strdup(src);
            if( !frame->str_locals[j] )
                PT_EXIT(&task->thread);
        }
    }
    for( int j = 0; j < task->int_arg_count; j++ )
        InterfaceX_SetHookIntLocal(host, vm, task->component_id, j, task->int_args[j]);

    CS2VMX_SetActiveAndDotComponentId(vm, task->component_id);

    for( ;; )
    {
        res = CS2VMX_RunScript(vm);
        if( res == CS2VM_EXECNO_DONE || res == 0 )
            break;
        if( res == CS2VM_EXECNO_ERROR )
        {
            fprintf(
                stderr,
                "script %d failed at opcode %d pc %d (invoked as script %d for component 0x%x)\n",
                vm->last_error_script_id,
                vm->last_error_opcode,
                vm->last_error_pc,
                task->script_id,
                (unsigned)task->component_id);
            CS2VMX_ResetRuntime(vm);
            break;
        }
        if( res != CS2VM_EXECNO_YIELD )
            break;
        if( !host->has_pending_host_request )
        {
            fprintf(stderr, "CS2VM: yield without pending host request\n");
            CS2VMX_ResetRuntime(vm);
            break;
        }
        task->pending = host->pending_host_request;
        host->has_pending_host_request = false;
        Task_InterfaceXRunScript_SelectLoadChild(task);
        if( task->sub_run )
            TASK_AWAIT(&task->thread, task->sub_run(task->sub_state, ctx));
        Task_InterfaceXRunScript_FreeChild(task);

        if( task->pending.kind == CS2VM_HOST_REQUEST_CC_SETGRAPHIC ||
            task->pending.kind == CS2VM_HOST_REQUEST_IF_SETGRAPHIC )
        {
            task->scene_id = -1;
            InterfaceX_HostIO_FinalizeGraphicScene(
                &host->host_io, task->pending.u.cc_set_graphic.graphic_id, &task->scene_id);
        }
        else if(
            task->pending.kind == CS2VM_HOST_REQUEST_PARAHEIGHT ||
            task->pending.kind == CS2VM_HOST_REQUEST_PARAWIDTH )
        {
            InterfaceX_HostIO_FinalizeSceneFont(
                &host->host_io, task->pending.u.para_height.font_id);
        }
        else if( task->pending.kind == CS2VM_HOST_REQUEST_CC_SETTEXTFONT )
        {
            InterfaceX_HostIO_FinalizeSceneFont(
                &host->host_io, task->pending.u.cc_set_text_font.font_id);
        }
        else if(
            task->pending.kind == CS2VM_HOST_REQUEST_STRUCT_PARAM &&
            !InterfaceX_HostIO_ConfigEntryReady(
                &host->host_io,
                RSCacheDat2A_ConfigKind_Params,
                task->pending.u.struct_param.param_id) )
        {
            task->sub_heap = true;
            task->config_child = Task_Dat2ConfigEntryLoad_New(
                InterfaceX_HostIO_Cache(&host->host_io),
                RSCacheDat2A_ConfigKind_Params,
                &task->pending.u.struct_param.param_id,
                1);
            if( task->config_child )
                TASK_AWAIT(
                    &task->thread, Task_Dat2ConfigEntryLoad_Run(task->config_child, ctx));
            Task_Dat2ConfigEntryLoad_Free(task->config_child);
            task->config_child = NULL;
            task->sub_heap = false;
        }
        else if(
            task->pending.kind == CS2VM_HOST_REQUEST_OC_PARAM &&
            !InterfaceX_HostIO_ConfigEntryReady(
                &host->host_io,
                RSCacheDat2A_ConfigKind_Params,
                task->pending.u.oc_param.param_id) )
        {
            task->sub_heap = true;
            task->config_child = Task_Dat2ConfigEntryLoad_New(
                InterfaceX_HostIO_Cache(&host->host_io),
                RSCacheDat2A_ConfigKind_Params,
                &task->pending.u.oc_param.param_id,
                1);
            if( task->config_child )
                TASK_AWAIT(
                    &task->thread, Task_Dat2ConfigEntryLoad_Run(task->config_child, ctx));
            Task_Dat2ConfigEntryLoad_Free(task->config_child);
            task->config_child = NULL;
            task->sub_heap = false;
        }
        else if(
            task->pending.kind == CS2VM_HOST_REQUEST_CC_SETOBJECT ||
            task->pending.kind == CS2VM_HOST_REQUEST_IF_SETOBJECT )
        {
            task->scene_id = -1;
            InterfaceX_HostIO_FinalizeObjIconScene(
                &host->host_io,
                task->pending.u.cc_set_object.obj_id,
                task->pending.u.cc_set_object.count,
                &task->scene_id);
        }
    }

    PT_END(&task->thread);
}

static void
InterfaceX_PrefetchCollectInterfaceScripts(
    struct InterfaceX_LoadedInterface const* loaded,
    int** script_ids,
    int* script_count,
    int* script_cap)
{
    assert(loaded);
    assert(script_ids);
    assert(script_count);
    assert(script_cap);

    for( int i = 0; i < loaded->component_count; i++ )
    {
        struct ToriAuxLibCore_Component* component = loaded->components[i];
        if( !component )
            continue;
        prefetch_collect_core_hook_script_id(
            &component->on_load, script_ids, script_count, script_cap);
        if( component->varp_triggers_count > 0 )
            prefetch_collect_core_hook_script_id(
                &component->on_varp_transmit, script_ids, script_count, script_cap);
        prefetch_collect_core_hook_script_id(
            &component->on_inv_transmit, script_ids, script_count, script_cap);
    }
}


struct Task_InterfaceXOpen*
Task_InterfaceXOpen_New(
    struct InterfaceX_VMHost* host,
    struct CS2VMX* vm,
    int group_id,
    bool is_root,
    struct InterfaceX_LoadedInterface* loaded_out)
{
    struct Task_InterfaceXOpen* task = calloc(1, sizeof(*task));
    if( !task )
        return NULL;
    PT_INIT(&task->thread);
    task->host = host;
    task->vm = vm;
    task->group_id = group_id;
    task->is_root = is_root;
    task->loaded_out = loaded_out;
    return task;
}

void
Task_InterfaceXOpen_Free(void* state)
{
    struct Task_InterfaceXOpen* task = state;
    if( !task )
        return;
    InterfaceX_TaskInterfaceLoad_Free(task->iface_child);
    if( task->scripts_child )
        Task_ClientScriptLoad_Free(task->scripts_child);
    if( task->assets_child )
        InterfaceX_AwaitPendingAssets_Free(task->assets_child);
    if( task->model_batch )
        InterfaceX_BatchModelLoad_Destroy(task->model_batch);
    free(task->prefetch_script_ids);
    free(task);
}

int
Task_InterfaceXOpen_Run(
    void* task_state,
    struct LibToriRS_IOContext* ctx)
{
    struct Task_InterfaceXOpen* task = task_state;
    struct InterfaceX_VMHost* host = task->host;
    struct CS2VMX* vm = task->vm;
    struct Dat2BuildCache* bc = dat2(InterfaceX_HostIO_Cache(&host->host_io));
    struct InterfaceX_LoadedInterface* loaded_ptr =
        task->loaded_out ? task->loaded_out : &task->loaded_scratch;

    PT_BEGIN(&task->thread);

    if( !dat2_buildcache_interface_archive_has(bc, task->group_id) )
    {
        if( !task->iface_child )
            task->iface_child = InterfaceX_TaskInterfaceLoad_New(
                InterfaceX_HostIO_Cache(&host->host_io), task->group_id);
        if( task->iface_child )
        {
            TASK_AWAIT(&task->thread, InterfaceX_TaskInterfaceLoad_Run(task->iface_child, ctx));
        }
    }

    InterfaceX_HostIO_InterfaceGroupSubmit(&host->host_io, task->group_id);
    if( !InterfaceX_ProcessInterfacePack(host, task->group_id, loaded_ptr) )
    {
        task->failed = true;
        PT_EXIT(&task->thread);
    }

    if( !task->assets_child )
    {
        task->assets_child = calloc(1, sizeof(*task->assets_child));
        if( task->assets_child )
        {
            PT_INIT(&task->assets_child->thread);
            task->assets_child->host = host;
        }
    }
    if( task->assets_child )
        TASK_AWAIT(&task->thread, InterfaceX_AwaitPendingAssets_Run(task->assets_child, ctx));

    {
        task->prefetch_script_count = 0;
        InterfaceX_PrefetchCollectInterfaceScripts(
            loaded_ptr,
            &task->prefetch_script_ids,
            &task->prefetch_script_count,
            &task->prefetch_script_cap);
        if( task->prefetch_script_count > 0 )
        {
            task->scripts_child = Task_ClientScriptLoad_New(
                InterfaceX_HostIO_Cache(&host->host_io),
                task->prefetch_script_ids,
                task->prefetch_script_count);
            if( task->scripts_child )
                TASK_AWAIT(
                    &task->thread, Task_ClientScriptLoad_Run(task->scripts_child, ctx));
        }
    }

    InterfaceX_QueueInterfaceOnLoadScripts(host, loaded_ptr);
    host->client_clock++;
    while( host->script_queue_count > 0 )
    {
        task->script_entry = host->script_queue[host->script_queue_head];
        host->script_queue_head = (host->script_queue_head + 1) % INTERFACEX_SCRIPT_QUEUE_MAX;
        host->script_queue_count--;
        Task_InterfaceXRunScript_Reset(
            &task->run_script,
            host,
            vm,
            task->script_entry.script_id,
            task->script_entry.component_id,
            task->script_entry.int_args,
            task->script_entry.int_arg_count,
            (char const* const*)task->script_entry.string_args,
            task->script_entry.string_arg_count);
        for( int i = 0; i < task->script_entry.string_arg_count; i++ )
        {
            free(task->script_entry.string_args[i]);
            task->script_entry.string_args[i] = NULL;
        }
        TASK_AWAIT(&task->thread, Task_InterfaceXRunScript_Run(&task->run_script, ctx));
    }

    if( task->is_root )
    {
        InterfaceX_VMHost_FireVarTransmitHooks(host, vm);
        host->client_clock++;
        while( host->script_queue_count > 0 )
        {
            task->script_entry = host->script_queue[host->script_queue_head];
            host->script_queue_head = (host->script_queue_head + 1) % INTERFACEX_SCRIPT_QUEUE_MAX;
            host->script_queue_count--;
            Task_InterfaceXRunScript_Reset(
                &task->run_script,
                host,
                vm,
                task->script_entry.script_id,
                task->script_entry.component_id,
                task->script_entry.int_args,
                task->script_entry.int_arg_count,
                (char const* const*)task->script_entry.string_args,
                task->script_entry.string_arg_count);
            for( int i = 0; i < task->script_entry.string_arg_count; i++ )
            {
                free(task->script_entry.string_args[i]);
                task->script_entry.string_args[i] = NULL;
            }
            TASK_AWAIT(&task->thread, Task_InterfaceXRunScript_Run(&task->run_script, ctx));
        }

        InterfaceX_VMHost_FireCacheVarTransmitHooks(
            host, vm, loaded_ptr->components, loaded_ptr->component_count);
        host->client_clock++;
        while( host->script_queue_count > 0 )
        {
            task->script_entry = host->script_queue[host->script_queue_head];
            host->script_queue_head = (host->script_queue_head + 1) % INTERFACEX_SCRIPT_QUEUE_MAX;
            host->script_queue_count--;
            Task_InterfaceXRunScript_Reset(
                &task->run_script,
                host,
                vm,
                task->script_entry.script_id,
                task->script_entry.component_id,
                task->script_entry.int_args,
                task->script_entry.int_arg_count,
                (char const* const*)task->script_entry.string_args,
                task->script_entry.string_arg_count);
            for( int i = 0; i < task->script_entry.string_arg_count; i++ )
            {
                free(task->script_entry.string_args[i]);
                task->script_entry.string_args[i] = NULL;
            }
            TASK_AWAIT(&task->thread, Task_InterfaceXRunScript_Run(&task->run_script, ctx));
        }

        InterfaceX_VMHost_FireInvTransmitHooks(host, vm);
        host->client_clock++;
        while( host->script_queue_count > 0 )
        {
            task->script_entry = host->script_queue[host->script_queue_head];
            host->script_queue_head = (host->script_queue_head + 1) % INTERFACEX_SCRIPT_QUEUE_MAX;
            host->script_queue_count--;
            Task_InterfaceXRunScript_Reset(
                &task->run_script,
                host,
                vm,
                task->script_entry.script_id,
                task->script_entry.component_id,
                task->script_entry.int_args,
                task->script_entry.int_arg_count,
                (char const* const*)task->script_entry.string_args,
                task->script_entry.string_arg_count);
            for( int i = 0; i < task->script_entry.string_arg_count; i++ )
            {
                free(task->script_entry.string_args[i]);
                task->script_entry.string_args[i] = NULL;
            }
            TASK_AWAIT(&task->thread, Task_InterfaceXRunScript_Run(&task->run_script, ctx));
        }

        InterfaceX_VMHost_FireCacheInvTransmitHooks(
            host, vm, loaded_ptr->components, loaded_ptr->component_count);
        host->client_clock++;
        while( host->script_queue_count > 0 )
        {
            task->script_entry = host->script_queue[host->script_queue_head];
            host->script_queue_head = (host->script_queue_head + 1) % INTERFACEX_SCRIPT_QUEUE_MAX;
            host->script_queue_count--;
            Task_InterfaceXRunScript_Reset(
                &task->run_script,
                host,
                vm,
                task->script_entry.script_id,
                task->script_entry.component_id,
                task->script_entry.int_args,
                task->script_entry.int_arg_count,
                (char const* const*)task->script_entry.string_args,
                task->script_entry.string_arg_count);
            for( int i = 0; i < task->script_entry.string_arg_count; i++ )
            {
                free(task->script_entry.string_args[i]);
                task->script_entry.string_args[i] = NULL;
            }
            TASK_AWAIT(&task->thread, Task_InterfaceXRunScript_Run(&task->run_script, ctx));
        }
    }

    if( host->model_load_count > 0 )
    {
        struct InterfaceX_ModelLoadArg args[MAX_NODES];
        for( int i = 0; i < host->model_load_count; i++ )
            args[i] = host->model_load_queue[i].arg;
        int arg_count = host->model_load_count;
        host->model_load_count = 0;
        task->model_batch = InterfaceX_BatchModelLoad_New(&host->host_io, args, arg_count);
        if( task->model_batch )
        {
            TASK_AWAIT(&task->thread, InterfaceX_BatchModelLoad_RunAwait(task->model_batch, ctx));
            UITreeX_ResolveModelSceneIds(host->tree, task->model_batch);
            InterfaceX_BatchModelLoad_Destroy(task->model_batch);
            task->model_batch = NULL;
        }
    }

    if( loaded_ptr == &task->loaded_scratch )
        InterfaceX_FreeLoadedInterface(&task->loaded_scratch);

    task->ui_ready = true;
    PT_END(&task->thread);
}

bool
Task_InterfaceXOpen_IsReady(struct Task_InterfaceXOpen const* task)
{
    return task && task->ui_ready;
}

bool
Task_InterfaceXOpen_Failed(struct Task_InterfaceXOpen const* task)
{
    return task && task->failed;
}
