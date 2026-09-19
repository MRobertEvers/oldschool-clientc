/* Native privacy apply script must reach the existing server setting mirror. */
#include "cs2vm2/cs2vm2.h"
#include "cs2vm2/cs2vm2_script.h"
#include "game/rs_cs2_host.h"
#include "game/rs_worldmap.h"
#include "game/sailing_settings.h"
#include "engine/cache_provider.h"
#include "inv/inv_manager.h"
#include "ui/uitree.h"
#include "varp/varp_manager.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void write_setting(struct RS_CS2Host* host, int script_id, int bit, int value)
{
    struct CS2VM2 vm;
    struct CS2VM2_Script script;
    uint16_t ops[]={CS2_OP_PUSH_CONSTANT_INT,CS2_OP_POP_VARBIT,CS2_OP_RETURN};
    int operands[]={value,bit,0};
    char* strings[]={NULL,NULL,NULL};
    CS2VM2_Init(&vm);
    CS2VM2_BindHost(&vm,host,RS_CS2Host_Exec);
    CS2VM2_ScriptInit(&script);
    script.script_id=script_id;
    script.op_count=3;
    script.opcodes=ops;
    script.int_operands=operands;
    script.string_operands=strings;
    struct CS2VM2_Thread* thread=CS2VM2_ThreadMain(&vm);
    assert(CS2VM2_PushCallScript(thread,&script)==CS2VM_EXECNO_OK);
    assert(CS2VM2_RunScript(thread)==CS2VM_EXECNO_DONE);
    CS2VM2_Free(&vm);
}

static void apply_dropdown(struct RS_CS2Host* host, int setting, int choice)
{
    struct CS2VM2 vm;
    struct CS2VM2_Script script;
    uint16_t ops[]={CS2_OP_PUSH_INT_LOCAL,CS2_OP_POP_VARBIT,CS2_OP_RETURN};
    int operands[]={0,9657,0};
    char* strings[]={NULL,NULL,NULL};
    CS2VM2_Init(&vm);
    CS2VM2_BindHost(&vm,host,RS_CS2Host_Exec);
    CS2VM2_ScriptInit(&script);
    script.script_id=3967;
    script.int_argument_count=3;
    script.local_int_count=3;
    script.op_count=3;
    script.opcodes=ops;
    script.int_operands=operands;
    script.string_operands=strings;
    struct CS2VM2_Thread* thread=CS2VM2_ThreadMain(&vm);
    assert(CS2VM2_PushCallScript(thread,&script)==CS2VM_EXECNO_OK);
    assert(CS2VM2_SetIntCurrentFrameLocal(thread,0,setting)==CS2VM_EXECNO_OK);
    assert(CS2VM2_SetIntCurrentFrameLocal(thread,1,choice)==CS2VM_EXECNO_OK);
    assert(CS2VM2_SetIntCurrentFrameLocal(thread,2,-1)==CS2VM_EXECNO_OK);
    assert(CS2VM2_RunScript(thread)==CS2VM_EXECNO_DONE);
    CS2VM2_Free(&vm);
}


/*
 * The real path: `setting_dropdown_entry_op`, script3852.
 *
 * The entry script is what a click on a dropdown choice runs. Its whole
 * contribution to a server-applied row is `cc_settext($text0)` -- the label --
 * so this drives exactly that: fourteen integer parameters in the cache's
 * declaration order, one string parameter, an active component set the way
 * `cc_find` sets it, and a real CC_SETTEXT through the real VM into the real
 * host dispatch. Nothing here reaches past the host boundary the client uses.
 */
#define PRIVACY_LABEL_COMPONENT ((944 << 16) | 11)

static int32_t push_text_component(struct UITree* tree, int component_id)
{
    struct UITreeNodeSpec spec;
    int32_t idx;

    memset(&spec, 0, sizeof(spec));
    spec.type = UIELEM_RS_TEXT;
    spec.component_id = component_id;
    spec.width = 74;
    spec.height = 14;
    idx = UITree_Push(tree, -1, &spec);
    assert(idx >= 0);
    return idx;
}

/* Locals in the cache's declaration order; see sailing_settings.h. */
static void dropdown_entry_locals(int* locals, int kind, int choice, int setting,
    int server_applied, int struct_id)
{
    for( int i = 0; i < SAILING_CARGO_PRIVACY_DROPDOWN_ARG_COUNT; ++i )
        locals[i] = 0;
    locals[SAILING_CARGO_PRIVACY_LOCAL_KIND] = kind;
    locals[SAILING_CARGO_PRIVACY_LOCAL_CHOICE] = choice;
    locals[SAILING_CARGO_PRIVACY_LOCAL_SETTING] = setting;
    locals[SAILING_CARGO_PRIVACY_LOCAL_SERVER_APPLIED] = server_applied;
    locals[SAILING_CARGO_PRIVACY_LOCAL_STRUCT] = struct_id;
    /* $component3 is the container cc_find succeeded on; it is not read by the
     * bridge, but a zero there would be a component id the tree really has. */
    locals[3] = 944 << 16;
}

static void dropdown_entry_op(
    struct RS_CS2Host* host, int component_id, const int* locals, char const* label)
{
    struct CS2VM2 vm;
    struct CS2VM2_Script script;
    uint16_t ops[] = { CS2_OP_PUSH_CONSTANT_STRING, CS2_OP_CC_SETTEXT, CS2_OP_RETURN };
    int operands[] = { 0, 0, 0 };
    char* strings[] = { (char*)label, NULL, NULL };

    CS2VM2_Init(&vm);
    CS2VM2_BindHost(&vm, host, RS_CS2Host_Exec);
    CS2VM2_ScriptInit(&script);
    script.script_id = SAILING_CARGO_PRIVACY_DROPDOWN_SCRIPT;
    script.int_argument_count = SAILING_CARGO_PRIVACY_DROPDOWN_ARG_COUNT;
    script.local_int_count = SAILING_CARGO_PRIVACY_DROPDOWN_ARG_COUNT;
    script.string_argument_count = 1;
    script.local_string_count = 1;
    script.op_count = 3;
    script.opcodes = ops;
    script.int_operands = operands;
    script.string_operands = strings;
    struct CS2VM2_Thread* thread = CS2VM2_ThreadMain(&vm);
    assert(CS2VM2_PushCallScript(thread, &script) == CS2VM_EXECNO_OK);
    for( int i = 0; i < SAILING_CARGO_PRIVACY_DROPDOWN_ARG_COUNT; ++i )
        assert(CS2VM2_SetIntCurrentFrameLocal(thread, i, locals[i]) == CS2VM_EXECNO_OK);
    /* What a successful cc_find leaves behind. */
    CS2VM2_SetTargetComponentId(thread, 0, component_id);
    assert(CS2VM2_RunScript(thread) == CS2VM_EXECNO_DONE);
    CS2VM2_Free(&vm);
}

int main(void)
{
    struct RS_CS2Host* host=calloc(1,sizeof(*host));
    assert(host);
    struct UITree* tree=UITree_New(16);
    assert(tree);
    push_text_component(tree,PRIVACY_LABEL_COMPONENT);
    struct CacheProvider provider={0};
    struct InvManager invs;
    struct VarPManager varps;
    struct VarPType type={0};
    struct VarBitType* bits=calloc(SAILING_CARGO_PRIVACY_VARBIT+1,sizeof(*bits));
    assert(bits);
    bits[SAILING_CARGO_PRIVACY_VARBIT]=(struct VarBitType){0,22,23};
    bits[1]=(struct VarBitType){0,0,0};
    bits[9657]=(struct VarBitType){0,1,11};
    VarPManager_Init(&varps);
    assert(VarPManager_SetVarpTypes(&varps,&type,1));
    assert(VarPManager_SetVarbitTypes(&varps,bits,SAILING_CARGO_PRIVACY_VARBIT+1));
    free(bits);
    InvManager_Init(&invs);
    RS_CS2Host_Init(host,tree,&provider,&invs,&varps,NULL,NULL);
    int bit,value;
    for( int choice=0; choice<=2; ++choice )
    {
        write_setting(host,SAILING_CARGO_PRIVACY_SCRIPT,SAILING_CARGO_PRIVACY_VARBIT,choice);
        assert(RS_CS2Host_TakeSettingsMirror(host,&bit,&value));
        assert(bit==SAILING_CARGO_PRIVACY_VARBIT);
        assert(value==choice);
        assert(VarPManager_GetVarbit(&varps,bit)==choice);
        assert(!RS_CS2Host_TakeSettingsMirror(host,&bit,&value));
    }
    write_setting(host,SAILING_CARGO_PRIVACY_SCRIPT,SAILING_CARGO_PRIVACY_VARBIT,3);
    assert(!RS_CS2Host_TakeSettingsMirror(host,&bit,&value));
    write_setting(host,9997,SAILING_CARGO_PRIVACY_VARBIT,1);
    write_setting(host,SAILING_CARGO_PRIVACY_SCRIPT,1,1);
    assert(!RS_CS2Host_TakeSettingsMirror(host,&bit,&value));
    write_setting(host,SAILING_CARGO_PRIVACY_SCRIPT,SAILING_CARGO_PRIVACY_VARBIT,0);
    write_setting(host,SAILING_CARGO_PRIVACY_SCRIPT,SAILING_CARGO_PRIVACY_VARBIT,2);
    assert(RS_CS2Host_TakeSettingsMirror(host,&bit,&value));
    assert(value==2);
    assert(!RS_CS2Host_TakeSettingsMirror(host,&bit,&value));
    host->varbit_settings_last_changed=9657;
    host->script_settings_client_apply=3967;
    apply_dropdown(host,SAILING_CARGO_PRIVACY_SETTING,1);
    assert(VarPManager_GetVarbit(&varps,SAILING_CARGO_PRIVACY_VARBIT)==1);
    assert(RS_CS2Host_TakeSettingsMirror(host,&bit,&value));
    assert(bit==SAILING_CARGO_PRIVACY_VARBIT);
    assert(value==1);
    apply_dropdown(host,471,2);
    apply_dropdown(host,SAILING_CARGO_PRIVACY_SETTING,3);
    assert(!RS_CS2Host_TakeSettingsMirror(host,&bit,&value));
    assert(VarPManager_GetVarbit(&varps,SAILING_CARGO_PRIVACY_VARBIT)==1);
    assert(!SailingCargoPrivacy_Valid(-1));
    assert(!SailingCargoPrivacy_Valid(3));

    /*
     * The actual shipped path: script3852's label write, with struct6372's
     * `param1085=1` in local12, which is the branch that never calls3967 or
     * 8830. Before the bridge below the mirror queue existed, every one of
     * these three clicks left the varbit at its previous value.
     *
     * Each choice is driven from a different starting value so a stuck varbit
     * cannot be mistaken for a correct one.
     */
    {
        static char const* const labels[3] = { "Navigators", "All players", "No players" };
        int locals[SAILING_CARGO_PRIVACY_DROPDOWN_ARG_COUNT];

        for( int choice = 0; choice <= 2; ++choice )
        {
            int const start = (choice + 1) % 3;
            RS_CS2Host_ScriptWriteVarbit(host, SAILING_CARGO_PRIVACY_VARBIT, start);
            while( RS_CS2Host_TakeSettingsMirror(host, &bit, &value) )
                ;
            assert(VarPManager_GetVarbit(&varps, SAILING_CARGO_PRIVACY_VARBIT) == start);

            dropdown_entry_locals(locals, SAILING_CARGO_PRIVACY_DROPDOWN_KIND, choice,
                SAILING_CARGO_PRIVACY_SETTING, 1, SAILING_CARGO_PRIVACY_STRUCT);
            dropdown_entry_op(host, PRIVACY_LABEL_COMPONENT, locals, labels[choice]);

            assert(VarPManager_GetVarbit(&varps, SAILING_CARGO_PRIVACY_VARBIT) == choice);
            assert(RS_CS2Host_TakeSettingsMirror(host, &bit, &value));
            assert(bit == SAILING_CARGO_PRIVACY_VARBIT);
            assert(value == choice);
            assert(!RS_CS2Host_TakeSettingsMirror(host, &bit, &value));
            /* The label really was applied, so the seam is a real text write. */
            {
                int32_t idx = UITree_FindByComponentId(tree, PRIVACY_LABEL_COMPONENT);
                assert(idx >= 0);
                assert(tree->components[idx].u.rs_text.text);
                assert(strcmp(tree->components[idx].u.rs_text.text, labels[choice]) == 0);
            }
        }

        /* Negative rows: every one of these is some OTHER settings row running
         * the same shared entry script, and none of them may touch19614. */
        RS_CS2Host_ScriptWriteVarbit(host, SAILING_CARGO_PRIVACY_VARBIT, 1);
        while( RS_CS2Host_TakeSettingsMirror(host, &bit, &value) )
            ;

        /* Not the cargo row: another setting id, another struct. */
        dropdown_entry_locals(locals, SAILING_CARGO_PRIVACY_DROPDOWN_KIND, 2, 471, 1, 6373);
        dropdown_entry_op(host, PRIVACY_LABEL_COMPONENT, locals, "Something else");
        assert(!RS_CS2Host_TakeSettingsMirror(host, &bit, &value));
        assert(VarPManager_GetVarbit(&varps, SAILING_CARGO_PRIVACY_VARBIT) == 1);

        /* The cargo row id with a different struct is still not the cargo row. */
        dropdown_entry_locals(locals, SAILING_CARGO_PRIVACY_DROPDOWN_KIND, 2,
            SAILING_CARGO_PRIVACY_SETTING, 1, 6373);
        dropdown_entry_op(host, PRIVACY_LABEL_COMPONENT, locals, "Wrong struct");
        assert(!RS_CS2Host_TakeSettingsMirror(host, &bit, &value));
        assert(VarPManager_GetVarbit(&varps, SAILING_CARGO_PRIVACY_VARBIT) == 1);

        /* A keybind entry, not a dropdown one. */
        dropdown_entry_locals(locals, 1, 2, SAILING_CARGO_PRIVACY_SETTING, 1,
            SAILING_CARGO_PRIVACY_STRUCT);
        dropdown_entry_op(host, PRIVACY_LABEL_COMPONENT, locals, "Keybind");
        assert(!RS_CS2Host_TakeSettingsMirror(host, &bit, &value));
        assert(VarPManager_GetVarbit(&varps, SAILING_CARGO_PRIVACY_VARBIT) == 1);

        /* $int12 == 0 is the client-applied branch: it calls3967, whose hub
         * the other mirror path owns, so this seam must stay out of it. */
        dropdown_entry_locals(locals, SAILING_CARGO_PRIVACY_DROPDOWN_KIND, 2,
            SAILING_CARGO_PRIVACY_SETTING, 0, SAILING_CARGO_PRIVACY_STRUCT);
        dropdown_entry_op(host, PRIVACY_LABEL_COMPONENT, locals, "Hub applies");
        assert(!RS_CS2Host_TakeSettingsMirror(host, &bit, &value));
        assert(VarPManager_GetVarbit(&varps, SAILING_CARGO_PRIVACY_VARBIT) == 1);

        /* Out-of-range choice on the right row: refused, varbit untouched. */
        dropdown_entry_locals(locals, SAILING_CARGO_PRIVACY_DROPDOWN_KIND, 3,
            SAILING_CARGO_PRIVACY_SETTING, 1, SAILING_CARGO_PRIVACY_STRUCT);
        dropdown_entry_op(host, PRIVACY_LABEL_COMPONENT, locals, "Out of range");
        assert(!RS_CS2Host_TakeSettingsMirror(host, &bit, &value));
        assert(VarPManager_GetVarbit(&varps, SAILING_CARGO_PRIVACY_VARBIT) == 1);

        /* A CC_SETTEXT that lands on no component is not an applied choice. */
        dropdown_entry_locals(locals, SAILING_CARGO_PRIVACY_DROPDOWN_KIND, 0,
            SAILING_CARGO_PRIVACY_SETTING, 1, SAILING_CARGO_PRIVACY_STRUCT);
        dropdown_entry_op(host, PRIVACY_LABEL_COMPONENT + 4096, locals, "No such node");
        assert(!RS_CS2Host_TakeSettingsMirror(host, &bit, &value));
        assert(VarPManager_GetVarbit(&varps, SAILING_CARGO_PRIVACY_VARBIT) == 1);
    }

    RS_WorldMap_Free(host->worldmap);
    InvManager_Free(&invs);
    VarPManager_Free(&varps);
    UITree_Free(tree);
    free(host);
    puts("PASS native cargo privacy0..2 through script3852 label writes, the8830/3967 paths, isolated script/varbit/struct scope and last-choice coalescing");
    return 0;
}
