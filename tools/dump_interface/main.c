/*
 * Human-readable dump of dat1/dat2 interface widgets.
 *
 * Usage:
 *   dump_interface <cache_dir> --iface 387
 *   dump_interface <cache_dir> --componentno 1644 --dat1
 *   dump_interface <cache_dir> --iface 387 --child 0
 *   dump_interface <cache_dir> --iface 387 --json [--out file.json]
 *
 * Cache format is auto-detected (main_file_cache.dat vs .dat2) unless --dat1/--dat2 is set.
 * Dat2 caches use --iface (archive id). Dat1 caches use --componentno (or --iface alias).
 */

#include "../dump_interface_common.h"
#include <assert.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
json_escape(
    FILE* fp,
    char const* s)
{
    assert(s);
    for( char const* p = s; *p; p++ )
    {
        if( *p == '"' || *p == '\\' )
            fputc('\\', fp);
        if( *p == '\n' )
            fputs("\\n", fp);
        else if( *p == '\r' )
            fputs("\\r", fp);
        else if( *p == '\t' )
            fputs("\\t", fp);
        else
            fputc(*p, fp);
    }
}

static void
print_inv_slots(
    struct DumpIfaceLoaded const* li,
    int i,
    FILE* fp,
    char const* indent)
{
    RSCacheDat2A_Component const* c = &li->comps[i];
    if( !c || c->type != COMPONENT_TYPE_INV )
        return;

    if( li->cache_mode == DUMP_IFACE_CACHE_DAT1 && li->dat1_src && li->dat1_src[i] )
    {
        struct RSCacheDat1A_ConfigComponent const* src = li->dat1_src[i];
        for( int si = 0; si < DUMP_IFACE_INV_SLOT_MAX; si++ )
        {
            char const* gfx = src->invSlotGraphic ? src->invSlotGraphic[si] : NULL;
            if( !gfx || gfx[0] == '\0' )
                continue;
            fprintf(
                fp,
                "%sslot[%2d]  off=(%d,%d)  sprite='%s'\n",
                indent,
                si,
                src->invSlotOffsetX ? src->invSlotOffsetX[si] : 0,
                src->invSlotOffsetY ? src->invSlotOffsetY[si] : 0,
                gfx);
        }
        return;
    }

    if( !c->invSlotGraphicId )
        return;

    for( int si = 0; si < DUMP_IFACE_INV_SLOT_MAX; si++ )
    {
        if( c->invSlotGraphicId[si] < 0 )
            continue;
        fprintf(
            fp,
            "%sslot[%2d]  off=(%d,%d)  graphicId=%d\n",
            indent,
            si,
            c->invSlotOffsetX ? c->invSlotOffsetX[si] : 0,
            c->invSlotOffsetY ? c->invSlotOffsetY[si] : 0,
            c->invSlotGraphicId[si]);
    }
}

static void
print_ops(
    RSCacheDat2A_Component const* c,
    FILE* fp,
    char const* indent)
{
    char const* const* ops = NULL;
    int ops_len = 0;
    if( c->type == COMPONENT_TYPE_INV && c->objOps )
    {
        ops = c->objOps;
        ops_len = 5;
    }
    else if( c->ops && c->opsLen > 0 )
    {
        ops = c->ops;
        ops_len = c->opsLen;
    }

    if( !ops )
        return;

    int printed = 0;
    for( int i = 0; i < ops_len; i++ )
    {
        if( !ops[i] || ops[i][0] == '\0' )
            continue;
        if( !printed )
            fprintf(fp, "%sops:", indent);
        fprintf(fp, " [%d]='%s'", i, ops[i]);
        printed = 1;
    }
    if( printed )
        fputc('\n', fp);
}

static void
print_one_hook(
    char const* label,
    ComponentScriptVar const* vars,
    int len,
    FILE* fp,
    char const* indent)
{
    if( !vars || len <= 0 )
        return;
    fprintf(fp, "%s%s:", indent, label);
    for( int i = 0; i < len; i++ )
    {
        if( vars[i].type == SCRIPT_VAR_STRING )
            fprintf(fp, " \"%s\"", vars[i].value.s ? vars[i].value.s : "");
        else
            fprintf(fp, " %d", vars[i].value.i);
    }
    fputc('\n', fp);
}

static void
print_hooks(
    RSCacheDat2A_Component const* c,
    FILE* fp,
    char const* indent)
{
    print_one_hook("onLoad", c->onLoad, c->onLoadLen, fp, indent);
    print_one_hook("onMouseOver", c->onMouseOver, c->onMouseOverLen, fp, indent);
    print_one_hook("onMouseLeave", c->onMouseLeave, c->onMouseLeaveLen, fp, indent);
    print_one_hook("onVarpTransmit", c->onVarpTransmit, c->onVarpTransmitLen, fp, indent);
    print_one_hook("onInvTransmit", c->onInvTransmit, c->onInvTransmitLen, fp, indent);
    print_one_hook("onStatTransmit", c->onStatTransmit, c->onStatTransmitLen, fp, indent);
    print_one_hook("onTimer", c->onTimer, c->onTimerLen, fp, indent);
    print_one_hook("onOp", c->onOp, c->onOpLen, fp, indent);
    print_one_hook("onMouseRepeat", c->onMouseRepeat, c->onMouseRepeatLen, fp, indent);
    print_one_hook("onClick", c->onClick, c->onClickLen, fp, indent);
    print_one_hook("onClickRepeat", c->onClickRepeat, c->onClickRepeatLen, fp, indent);
    print_one_hook("onRelease", c->onRelease, c->onReleaseLen, fp, indent);
    print_one_hook("onHold", c->onHold, c->onHoldLen, fp, indent);
    print_one_hook("onDrag", c->onDrag, c->onDragLen, fp, indent);
    print_one_hook("onDragComplete", c->onDragComplete, c->onDragCompleteLen, fp, indent);
    print_one_hook("onScrollWheel", c->onScrollWheel, c->onScrollWheelLen, fp, indent);
    print_one_hook("onVarcTransmit", c->onVarcTransmit, c->onVarcTransmitLen, fp, indent);
    print_one_hook("onVarcstrTransmit", c->onVarcstrTransmit, c->onVarcstrTransmitLen, fp, indent);
}

static void
print_component_details(
    struct DumpIfaceLoaded const* li,
    int i,
    FILE* fp)
{
    RSCacheDat2A_Component const* c = &li->comps[i];
    if( c->type < 0 )
        return;

    int parent = dump_iface_parent_file_index(li, i);
    char const* tname = dump_iface_component_type_name(c);
    char id_buf[48];
    char layer_buf[48];

    if( li->cache_mode == DUMP_IFACE_CACHE_DAT2 )
        dump_iface_format_packed_id(id_buf, sizeof(id_buf), c->id);
    else
        snprintf(id_buf, sizeof(id_buf), "%d", c->id);

    if( c->layer >= 0 && li->cache_mode == DUMP_IFACE_CACHE_DAT2 )
        dump_iface_format_packed_id(layer_buf, sizeof(layer_buf), c->layer);
    else if( c->layer >= 0 )
        snprintf(layer_buf, sizeof(layer_buf), "%d", c->layer);
    else
        snprintf(layer_buf, sizeof(layer_buf), "-1");

    fprintf(
        fp,
        "[%02d] id=%s  %s  parent=%02d  layer=%s  type=%s(%d)  x=%d y=%d w=%d h=%d  "
        "hidden=%d  button=%d  clientCode=%d  clickMask=0x%08x",
        i,
        id_buf,
        c->if3 ? "if3" : "if1",
        parent,
        layer_buf,
        tname,
        c->type,
        li->lay_x[i],
        li->lay_y[i],
        li->lay_w[i],
        li->lay_h[i],
        c->hidden ? 1 : 0,
        c->buttonType,
        c->clientCode,
        (unsigned)c->clickMask);

    if( c->name && c->name[0] != '\0' )
        fprintf(fp, "  name='%s'", c->name);
    fputc('\n', fp);

    /* IF1 "active" scripts: comparator[k] + operand[k] vs the value script
     * scripts[k] decides whether the widget draws its active variant
     * (activeModel / activeText / activeColour). Reference getIfActive. */
    if( li->cache_mode == DUMP_IFACE_CACHE_DAT1 && li->dat1_src && li->dat1_src[i] )
    {
        struct RSCacheDat1A_ConfigComponent const* src = li->dat1_src[i];
        /* This struct does not keep the comparator count (it is decoded
         * independently of scripts_count in the cache format); in practice the
         * two match for the widgets that use them. */
        for( int k = 0; src->scriptComparator && k < src->scripts_count; k++ )
        {
            fprintf(
                fp,
                "      if1script[%d] cmp=%d operand=%d code=",
                k,
                src->scriptComparator ? src->scriptComparator[k] : -1,
                src->scriptOperand ? src->scriptOperand[k] : -1);
            if( src->scripts && k < src->scripts_count && src->scripts[k] )
                for( int o = 0; o < src->scripts_lengths[k]; o++ )
                    fprintf(fp, "%d ", src->scripts[k][o]);
            fputc('\n', fp);
        }
    }

    if( c->type == COMPONENT_TYPE_INV )
    {
        fprintf(
            fp,
            "      grid=%dx%d  margin=%d,%d\n",
            c->baseWidth,
            c->baseHeight,
            c->marginX,
            c->marginY);
        print_inv_slots(li, i, fp, "      ");
    }
    else if( c->type == COMPONENT_TYPE_INV_TEXT )
    {
        fprintf(
            fp,
            "      grid=%dx%d  margin=%d,%d  font=%d\n",
            c->baseWidth,
            c->baseHeight,
            c->marginX,
            c->marginY,
            c->textFont);
    }
    else if( c->type == COMPONENT_TYPE_GRAPHIC || (c->if3 && c->type == 5) )
    {
        if( c->name && c->name[0] != '\0' )
        {
            fprintf(
                fp,
                "      sprite='%s'  tiled=%d  transparency=%d\n",
                c->name,
                c->tiled ? 1 : 0,
                c->transparency);
        }
        else
        {
            fprintf(
                fp,
                "      graphic=%d  activeGraphic=%d  textureId=%d  tiled=%d  transparency=%d\n",
                c->graphic,
                c->activeGraphic,
                c->textureId,
                c->tiled ? 1 : 0,
                c->transparency);
        }
    }
    else if( c->type == COMPONENT_TYPE_TEXT || (c->if3 && c->type == 4) )
    {
        fprintf(
            fp,
            "      font=%d  text='%s'  halign=%d valign=%d lineheight=%d",
            c->textFont,
            c->text ? c->text : "",
            c->textHorizontalAlignment,
            c->textVerticalAlignment,
            c->textLineHeight);
        if( c->activeText && c->activeText[0] != '\0' )
            fprintf(fp, "  activeText='%s'", c->activeText);
        fputc('\n', fp);
    }
    else if( c->type == COMPONENT_TYPE_MODEL || (c->if3 && c->type == 6) )
    {
        fprintf(
            fp,
            "      modelType=%d  modelId=%d  zoom=%d  angles=(%d,%d,%d)\n",
            c->modelType,
            c->modelId,
            c->modelZoom,
            c->modelXAngle,
            c->modelYAngle,
            c->modelZAngle);
        /* The active (if1script-selected) variant is not carried on the shared
         * dump struct; read it off the dat1 source. */
        if( li->cache_mode == DUMP_IFACE_CACHE_DAT1 && li->dat1_src && li->dat1_src[i] )
            fprintf(
                fp,
                "      activeModelType=%d  activeModelId=%d\n",
                li->dat1_src[i]->activeModelType,
                li->dat1_src[i]->activeModel);
    }
    else if( c->type == COMPONENT_TYPE_RECT || (c->if3 && c->type == 3) )
    {
        fprintf(
            fp,
            "      color=0x%06x  fill=%d  transparency=%d\n",
            (unsigned)(c->color & 0xFFFFFF),
            c->fill ? 1 : 0,
            c->transparency);
    }
    else if( c->type == COMPONENT_TYPE_LINE || (c->if3 && c->type == 9) )
    {
        fprintf(
            fp,
            "      lineWidth=%d  color=0x%06x  horizontal=%d\n",
            c->lineWidth,
            (unsigned)(c->color & 0xFFFFFF),
            c->lineDirection ? 1 : 0);
    }
    else if( c->type == COMPONENT_TYPE_LAYER || (c->if3 && c->type == 0) )
    {
        fprintf(
            fp,
            "      scroll=%dx%d  noClickThrough=%d\n",
            c->scrollWidth,
            c->scrollHeight,
            c->noClickThrough ? 1 : 0);
    }
    else if( c->type == COMPONENT_TYPE_UNUSED )
    {
        fprintf(fp, "      WARNING: IF1 type 1 (unused) — not supported by game bake path\n");
    }

    print_ops(c, fp, "      ");
    print_hooks(c, fp, "      ");
}

static void
print_summary(
    struct DumpIfaceLoaded const* li,
    int iface_id,
    int root_w,
    int root_h,
    FILE* fp)
{
    int type_counts[16] = { 0 };
    int inv_children = 0;
    int inv_slots_with_gfx = 0;
    int unsupported = 0;

    for( int i = 0; i < li->count; i++ )
    {
        RSCacheDat2A_Component const* c = &li->comps[i];
        if( c->type < 0 )
            continue;
        if( c->type >= 0 && c->type < 16 )
            type_counts[c->type]++;
        if( c->type == COMPONENT_TYPE_INV )
        {
            inv_children++;
            if( c->invSlotGraphicId )
            {
                for( int si = 0; si < DUMP_IFACE_INV_SLOT_MAX; si++ )
                {
                    if( c->invSlotGraphicId[si] >= 0 )
                        inv_slots_with_gfx++;
                }
            }
        }
        if( c->type == COMPONENT_TYPE_UNUSED )
            unsupported++;
        if( c->if3 && c->type == 2 )
            unsupported++;
    }

    fprintf(
        fp,
        "%s %d  files=%d  root=%dx%d\n",
        li->cache_mode == DUMP_IFACE_CACHE_DAT1 ? "RSCacheDat2A_Component" : "Interface",
        iface_id,
        li->count,
        root_w,
        root_h);
    if( li->cache_mode == DUMP_IFACE_CACHE_DAT2 )
        fprintf(fp, "id encoding: packed_id = (iface_archive << 16) | file_index\n");
    fprintf(
        fp,
        "summary: inv_children=%d  inv_slots_with_graphicId=%d  unsupported_type1=%d\n",
        inv_children,
        inv_slots_with_gfx,
        unsupported);
    fprintf(
        fp,
        "type_counts(if1_raw): layer=%d unused=%d inv=%d rect=%d text=%d graphic=%d model=%d "
        "inv_text=%d line=%d\n",
        type_counts[0],
        type_counts[1],
        type_counts[2],
        type_counts[3],
        type_counts[4],
        type_counts[5],
        type_counts[6],
        type_counts[7],
        type_counts[9]);
    fputc('\n', fp);
}

static void
print_all(
    struct DumpIfaceLoaded const* li,
    int iface_id,
    int root_w,
    int root_h,
    FILE* fp)
{
    print_summary(li, iface_id, root_w, root_h, fp);
    for( int i = 0; i < li->count; i++ )
        print_component_details(li, i, fp);
}

static void
print_json_child(
    struct DumpIfaceLoaded const* li,
    int i,
    FILE* fp)
{
    RSCacheDat2A_Component const* c = &li->comps[i];
    int parent = dump_iface_parent_file_index(li, i);
    char const* tname = dump_iface_component_type_name(c);

    fprintf(fp, "    {\n");
    fprintf(fp, "      \"child_id\": %d,\n", i);
    fprintf(fp, "      \"id\": %d,\n", c->id);
    if( li->cache_mode == DUMP_IFACE_CACHE_DAT2 )
    {
        fprintf(fp, "      \"id_iface\": %d,\n", dump_iface_packed_id_iface(c->id));
        fprintf(fp, "      \"id_file\": %d,\n", dump_iface_packed_id_file(c->id));
    }
    fprintf(fp, "      \"if3\": %s,\n", c->if3 ? "true" : "false");
    fprintf(fp, "      \"parent\": %d,\n", parent);
    if( c->layer >= 0 )
    {
        fprintf(fp, "      \"layer\": %d,\n", c->layer);
        if( li->cache_mode == DUMP_IFACE_CACHE_DAT2 )
        {
            fprintf(fp, "      \"layer_iface\": %d,\n", dump_iface_packed_id_iface(c->layer));
            fprintf(fp, "      \"layer_file\": %d,\n", dump_iface_packed_id_file(c->layer));
        }
    }
    else
        fprintf(fp, "      \"layer\": -1,\n");
    fprintf(fp, "      \"type\": %d,\n", c->type);
    fprintf(fp, "      \"type_name\": \"%s\",\n", tname);
    fprintf(fp, "      \"x\": %d, \"y\": %d, \"w\": %d, \"h\": %d,\n", li->lay_x[i], li->lay_y[i], li->lay_w[i], li->lay_h[i]);
    fprintf(fp, "      \"hidden\": %s,\n", c->hidden ? "true" : "false");
    fprintf(fp, "      \"button_type\": %d,\n", c->buttonType);
    fprintf(fp, "      \"client_code\": %d,\n", c->clientCode);
    fprintf(fp, "      \"click_mask\": %u,\n", (unsigned)c->clickMask);
    fprintf(fp, "      \"graphic\": %d,\n", c->graphic);
    fprintf(fp, "      \"name\": \"");
    json_escape(fp, c->name);
    fprintf(fp, "\"");

    if( c->type == COMPONENT_TYPE_INV )
    {
        fprintf(fp, ",\n      \"inv_cols\": %d, \"inv_rows\": %d,\n", c->baseWidth, c->baseHeight);
        fprintf(fp, "      \"margin_x\": %d, \"margin_y\": %d,\n", c->marginX, c->marginY);
        fprintf(fp, "      \"inv_slots\": [");
        int first_slot = 1;
        if( li->cache_mode == DUMP_IFACE_CACHE_DAT1 && li->dat1_src && li->dat1_src[i] )
        {
            struct RSCacheDat1A_ConfigComponent const* src = li->dat1_src[i];
            for( int si = 0; si < DUMP_IFACE_INV_SLOT_MAX; si++ )
            {
                char const* gfx = src->invSlotGraphic ? src->invSlotGraphic[si] : NULL;
                if( !gfx || gfx[0] == '\0' )
                    continue;
                if( !first_slot )
                    fprintf(fp, ",");
                first_slot = 0;
                fprintf(
                    fp,
                    "\n        {\"slot\": %d, \"offset_x\": %d, \"offset_y\": %d, \"sprite\": \"",
                    si,
                    src->invSlotOffsetX ? src->invSlotOffsetX[si] : 0,
                    src->invSlotOffsetY ? src->invSlotOffsetY[si] : 0);
                json_escape(fp, gfx);
                fprintf(fp, "\"}");
            }
        }
        else if( c->invSlotGraphicId )
        {
            for( int si = 0; si < DUMP_IFACE_INV_SLOT_MAX; si++ )
            {
                if( c->invSlotGraphicId[si] < 0 )
                    continue;
                if( !first_slot )
                    fprintf(fp, ",");
                first_slot = 0;
                fprintf(
                    fp,
                    "\n        {\"slot\": %d, \"offset_x\": %d, \"offset_y\": %d, \"graphic_id\": %d}",
                    si,
                    c->invSlotOffsetX ? c->invSlotOffsetX[si] : 0,
                    c->invSlotOffsetY ? c->invSlotOffsetY[si] : 0,
                    c->invSlotGraphicId[si]);
            }
        }
        fprintf(fp, "\n      ]");
    }
    else if( c->type == COMPONENT_TYPE_TEXT || (c->if3 && c->type == 4) )
    {
        fprintf(fp, ",\n      \"text\": \"");
        json_escape(fp, c->text);
        fprintf(
            fp,
            "\",\n      \"font\": %d,\n"
            "      \"text_halign\": %d, \"text_valign\": %d, \"line_height\": %d",
            c->textFont,
            c->textHorizontalAlignment,
            c->textVerticalAlignment,
            c->textLineHeight);
    }
    else if( c->type == COMPONENT_TYPE_MODEL || (c->if3 && c->type == 6) )
    {
        fprintf(
            fp,
            ",\n      \"model_type\": %d, \"model_id\": %d, \"model_zoom\": %d",
            c->modelType,
            c->modelId,
            c->modelZoom);
    }

    fprintf(fp, "\n    }");
}

static void
print_json(
    struct DumpIfaceLoaded const* li,
    int iface_id,
    int root_w,
    int root_h,
    FILE* fp)
{
    fprintf(
        fp,
        "{\n  \"iface\": %d,\n  \"root_w\": %d,\n  \"root_h\": %d,\n  \"children\": [\n",
        iface_id,
        root_w,
        root_h);
    int first = 1;
    for( int i = 0; i < li->count; i++ )
    {
        if( li->comps[i].type < 0 )
            continue;
        if( !first )
            fprintf(fp, ",\n");
        first = 0;
        print_json_child(li, i, fp);
    }
    fprintf(fp, "\n  ]\n}\n");
}

static void
usage(void)
{
    fprintf(
        stderr,
        "usage: dump_interface <cache_dir> [--dat1 | --dat2]\n"
        "       (--iface N | --componentno N) [--child N | --json | --parents]\n"
        "       [--root-w W] [--root-h H] [--out path]\n");
}

int
main(
    int argc,
    char** argv)
{
    const char* cache_dir = NULL;
    int iface = 387;
    int componentno = -1;
    int child_id = -1;
    int json_mode = 0;
    int parents_mode = 0;
    int root_w = DUMP_IFACE_FIXED_ROOT_W;
    int root_h = DUMP_IFACE_FIXED_ROOT_H;
    const char* out_path = NULL;
    int cache_mode = -1;

    for( int i = 1; i < argc; i++ )
    {
        if( strcmp(argv[i], "--iface") == 0 && i + 1 < argc )
            iface = atoi(argv[++i]);
        else if( strcmp(argv[i], "--componentno") == 0 && i + 1 < argc )
            componentno = atoi(argv[++i]);
        else if( strcmp(argv[i], "--child") == 0 && i + 1 < argc )
            child_id = atoi(argv[++i]);
        else if( strcmp(argv[i], "--json") == 0 )
            json_mode = 1;
        else if( strcmp(argv[i], "--parents") == 0 )
            parents_mode = 1;
        else if( strcmp(argv[i], "--dat1") == 0 )
            cache_mode = DUMP_IFACE_CACHE_DAT1;
        else if( strcmp(argv[i], "--dat2") == 0 )
            cache_mode = DUMP_IFACE_CACHE_DAT2;
        else if( strcmp(argv[i], "--root-w") == 0 && i + 1 < argc )
            root_w = atoi(argv[++i]);
        else if( strcmp(argv[i], "--root-h") == 0 && i + 1 < argc )
            root_h = atoi(argv[++i]);
        else if( strcmp(argv[i], "--out") == 0 && i + 1 < argc )
            out_path = argv[++i];
        else if( argv[i][0] != '-' && !cache_dir )
            cache_dir = argv[i];
    }

    if( !cache_dir )
    {
        usage();
        return 1;
    }

    if( cache_mode < 0 )
        cache_mode = dump_iface_detect_cache_mode(cache_dir);
    if( cache_mode < 0 )
    {
        fprintf(stderr, "failed to detect cache format in: %s\n", cache_dir);
        return 1;
    }

    int root_id = componentno >= 0 ? componentno : iface;
    if( iface == 161 || iface == 164 || root_id == 161 || root_id == 164 )
    {
        root_w = 800;
        root_h = 600;
    }

    struct DumpIfaceLoaded li;
    if( cache_mode == DUMP_IFACE_CACHE_DAT1 )
    {
        if( parents_mode )
        {
            fprintf(stderr, "--parents requires dat2 cache\n");
            return 1;
        }
        if( dump_iface_load_dat1(cache_dir, root_id, root_w, root_h, &li) != 0 )
        {
            fprintf(stderr, "failed to load dat1 component %d\n", root_id);
            return 1;
        }
    }
    else
    {
        struct RSCacheDat2Disk* cache = RSCacheDat2Disk_NewFromDirectory(cache_dir);
        if( !cache )
        {
            fprintf(stderr, "failed to open dat2 cache: %s\n", cache_dir);
            return 1;
        }

        if( parents_mode )
        {
            FILE* fp = stdout;
            if( out_path )
            {
                fp = fopen(out_path, "w");
                if( !fp )
                {
                    fprintf(stderr, "failed to open %s\n", out_path);
                    RSCacheDat2Disk_Free(cache);
                    return 1;
                }
            }
            int rc = dump_iface_scan_parents(cache, root_id, fp);
            if( fp != stdout )
                fclose(fp);
            RSCacheDat2Disk_Free(cache);
            return rc != 0;
        }

        if( dump_iface_load_dat2(cache, root_id, root_w, root_h, &li) != 0 )
        {
            fprintf(stderr, "failed to load dat2 interface %d\n", root_id);
            RSCacheDat2Disk_Free(cache);
            return 1;
        }
        RSCacheDat2Disk_Free(cache);
    }

    FILE* fp = stdout;
    if( out_path )
    {
        fp = fopen(out_path, "w");
        if( !fp )
        {
            fprintf(stderr, "failed to open %s\n", out_path);
            dump_iface_free(&li);
            return 1;
        }
    }

    if( json_mode )
        print_json(&li, root_id, root_w, root_h, fp);
    else if( child_id >= 0 )
    {
        if( child_id >= li.count )
            fprintf(fp, "child %d: not found (count=%d)\n", child_id, li.count);
        else
            print_component_details(&li, child_id, fp);
    }
    else
        print_all(&li, root_id, root_w, root_h, fp);

    if( fp != stdout )
        fclose(fp);

    dump_iface_free(&li);
    return 0;
}
