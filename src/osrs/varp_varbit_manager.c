#include "varp_varbit_manager.h"

#include "rscache/filelist.h"
#include "rscache/rsbuf.h"
#include "rscache/tables_dat/config_varp.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
decode_varbit_type(
    struct VarBitType* varbit,
    struct RSBuffer* buffer)
{
    memset(varbit, 0, sizeof(struct VarBitType));
    varbit->basevar = -1;

    while( 1 )
    {
        int code = g1(buffer);
        if( code == 0 )
            break;

        switch( code )
        {
        case 1:
            varbit->basevar = g2(buffer);
            varbit->startbit = g1(buffer);
            varbit->endbit = g1(buffer);
            break;
        case 10:
        {
            char* s = gstringnewline(buffer);
            free(s);
            break;
        }
        default:
            break;
        }
    }
}

void
varp_varbit_init(struct VarPVarBitManager* mgr)
{
    memset(mgr, 0, sizeof(struct VarPVarBitManager));

    for( int i = 0; i < VARP_VARBIT_READBIT_MAX; i++ )
    {
        mgr->readbit[i] = (1 << i) - 1;
    }
}

void
varp_varbit_free(struct VarPVarBitManager* mgr)
{
    if( !mgr )
        return;

    free(mgr->varp_types);
    mgr->varp_types = NULL;
    mgr->varp_count = 0;

    free(mgr->varbit_types);
    mgr->varbit_types = NULL;
    mgr->varbit_count = 0;

    free(mgr->var);
    mgr->var = NULL;

    free(mgr->var_serv);
    mgr->var_serv = NULL;
}

bool
varp_varbit_load_from_config_jagfile(
    struct VarPVarBitManager* mgr,
    struct FileListDat* config_jagfile)
{
    if( !mgr || !config_jagfile )
        return false;

    int varp_idx = filelist_dat_find_file_by_name(config_jagfile, "varp.dat");
    if( varp_idx == -1 )
        return false;

    struct CacheDatConfigVarpList* list = cache_dat_config_varp_list_new_decode(
        config_jagfile->files[varp_idx], config_jagfile->file_sizes[varp_idx]);
    if( !list )
        return false;

    int varp_count = list->varps_count;
    mgr->varp_types = malloc((size_t)varp_count * sizeof(struct VarPType));
    if( !mgr->varp_types )
    {
        cache_dat_config_varp_list_free(list);
        return false;
    }

    mgr->varp_count = varp_count;
    for( int i = 0; i < varp_count; i++ )
    {
        mgr->varp_types[i].clientcode = list->varps[i]->clientcode;
    }
    cache_dat_config_varp_list_free(list);

    /* Allocate var arrays */
    mgr->var = calloc((size_t)varp_count, sizeof(int));
    mgr->var_serv = calloc((size_t)varp_count, sizeof(int));
    if( !mgr->var || !mgr->var_serv )
    {
        varp_varbit_free(mgr);
        return false;
    }

    return true;
}

int
varp_varbit_get_varp(
    const struct VarPVarBitManager* mgr,
    int id)
{
    if( !mgr || id < 0 || id >= mgr->varp_count )
        return 0;
    return mgr->var[id];
}

int
varp_varbit_get_varbit(
    const struct VarPVarBitManager* mgr,
    int id)
{
    if( !mgr || id < 0 || id >= mgr->varbit_count )
        return 0;

    const struct VarBitType* vb = &mgr->varbit_types[id];
    if( vb->basevar < 0 || vb->basevar >= mgr->varp_count )
        return 0;

    int base_val = mgr->var[vb->basevar];
    int bit_count = vb->endbit - vb->startbit;
    if( bit_count <= 0 || bit_count >= VARP_VARBIT_READBIT_MAX )
        return 0;

    int mask = mgr->readbit[bit_count];
    return (base_val >> vb->startbit) & mask;
}

void
varp_varbit_set_client_var_callback(
    struct VarPVarBitManager* mgr,
    VarPVarBitClientVarFn fn,
    void* userdata)
{
    if( !mgr )
        return;
    mgr->client_var_fn = fn;
    mgr->client_var_userdata = userdata;
}

static void
notify_client_var(
    struct VarPVarBitManager* mgr,
    int varp_id)
{
    if( mgr->client_var_fn )
        mgr->client_var_fn(mgr->client_var_userdata, varp_id);
}

static void
apply_varp_value(
    struct VarPVarBitManager* mgr,
    int variable,
    int value)
{
    if( variable < 0 || variable >= mgr->varp_count )
        return;

    mgr->var_serv[variable] = value;

    if( mgr->var[variable] != value )
    {
        mgr->var[variable] = value;
        printf("varp set: id=%d value=%d\n", variable, value);
        notify_client_var(mgr, variable);
    }
}

void
varp_varbit_apply_small(
    struct VarPVarBitManager* mgr,
    int variable,
    int value)
{
    if( !mgr )
        return;
    /* g1b = signed byte, -128..127 */
    apply_varp_value(mgr, variable, value);
}

void
varp_varbit_apply_large(
    struct VarPVarBitManager* mgr,
    int variable,
    int value)
{
    if( !mgr )
        return;
    apply_varp_value(mgr, variable, value);
}

void
varp_varbit_set_varp_optimistic(
    struct VarPVarBitManager* mgr,
    int variable,
    int value)
{
    if( !mgr || variable < 0 || variable >= mgr->varp_count )
        return;

    if( mgr->var[variable] != value )
    {
        mgr->var[variable] = value;
        notify_client_var(mgr, variable);
    }
}

void
varp_varbit_apply_sync(struct VarPVarBitManager* mgr)
{
    if( !mgr )
        return;

    for( int i = 0; i < mgr->varp_count; i++ )
    {
        if( mgr->var[i] != mgr->var_serv[i] )
        {
            mgr->var[i] = mgr->var_serv[i];
            printf("varp sync: id=%d value=%d\n", i, mgr->var[i]);
            notify_client_var(mgr, i);
        }
    }
}
