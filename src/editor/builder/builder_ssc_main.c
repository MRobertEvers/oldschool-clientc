/*
 * maped_script_check: the editor server's Check job (workbench 6.6,
 * torirsmaped/torirs_maped_build.h TORIRSMAPED_BUILD_CHECK), run by `make -C
 * src maped-script-check` (src/maped/records.mk) as its own process, so the
 * compile never runs on an editor's UI thread:
 *
 *   maped_script_check <sscompile> <content-root> <work-dir>
 *
 * Stages <content-root>/server/scripts under <work-dir>/scripts (the
 * builders' stage, builder_ssc.h: never in the content tree), writes every
 * file the editor server left under <work-dir>/puts (its unsaved script
 * documents, by their path under server/scripts) over the stage and deletes
 * it, runs sscompile, and prints the job's lines (builder_ssc.h "AS A BUILD
 * JOB"). Exits 0 when the scripts compile, 1 when they do not, 2 when the
 * compiler could not run.
 */

#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#include "builder_ssc.h"

#include "mem/torirs_mem.h"

#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK_PATH_MAX 2048

static char*
read_whole(
    const char* path,
    size_t* out_length)
{
    FILE* file = fopen(path, "rb");
    long size;
    char* text;

    if( !file )
        return NULL;
    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);
    text = malloc((size_t)(size > 0 ? size : 0) + 1);
    assert(text);
    if( size > 0 && fread(text, 1, (size_t)size, file) != (size_t)size )
        size = 0;
    text[size > 0 ? size : 0] = '\0';
    fclose(file);
    *out_length = (size_t)(size > 0 ? size : 0);
    return text;
}

/** Every file under `root`/`rel` onto the stage, then gone. Returns the
 *  count. */
static int
apply_puts(
    struct Builder_Ssc* ssc,
    const char* root,
    const char* rel)
{
    char dir_path[CHECK_PATH_MAX];
    DIR* dir;
    struct dirent* entry;
    int count = 0;

    snprintf(dir_path, sizeof(dir_path), "%s%s%s", root, rel[0] ? "/" : "", rel);
    dir = opendir(dir_path);
    if( !dir )
        return 0;
    while( (entry = readdir(dir)) != NULL )
    {
        char child_rel[CHECK_PATH_MAX];
        char child_path[CHECK_PATH_MAX * 2];
        struct stat st;

        if( entry->d_name[0] == '.' )
            continue;
        snprintf(child_rel, sizeof(child_rel), "%s%s%s", rel, rel[0] ? "/" : "", entry->d_name);
        snprintf(child_path, sizeof(child_path), "%s/%s", root, child_rel);
        if( stat(child_path, &st) != 0 )
            continue;
        if( S_ISDIR(st.st_mode) )
        {
            count += apply_puts(ssc, root, child_rel);
            continue;
        }
        {
            size_t length = 0;
            char* text = read_whole(child_path, &length);
            if( text && Builder_SscPut(ssc, child_rel, text, length) )
            {
                printf("check: unsaved %s over the stage\n", child_rel);
                count++;
            }
            else
                printf("warning: check: %s could not be put over the stage\n", child_rel);
            free(text);
            unlink(child_path);
        }
    }
    closedir(dir);
    return count;
}

int
main(
    int argc,
    char** argv)
{
    struct Builder_Ssc* ssc;
    struct Builder_SscResult* result;
    char puts_dir[CHECK_PATH_MAX];
    char line[1024];
    int puts_count;

    if( argc != 4 )
    {
        fprintf(stderr, "usage: %s <sscompile> <content-root> <work-dir>\n", argv[0]);
        return 2;
    }
    setvbuf(stdout, NULL, _IOLBF, 0);
    ToriMem_Init();
    ssc = calloc(1, sizeof(*ssc));
    assert(ssc);
    result = calloc(1, sizeof(*result));
    assert(result);
    if( !Builder_SscInit(ssc, argv[1], argv[2], argv[3]) )
    {
        printf("check: sscompile could not run (the work dir %s is inside the content tree, or a path is too long)\n",
            argv[3]);
        return 2;
    }
    printf("check: staging %s/server/scripts under %s\n", argv[2], ssc->stage);
    if( !Builder_SscStage(ssc) )
    {
        printf("check: sscompile could not run (no binary or stage)\n");
        return 2;
    }
    printf("check: staged (copied %d, dropped %d, kept %d)\n", ssc->last_copied, ssc->last_dropped, ssc->last_kept);
    snprintf(puts_dir, sizeof(puts_dir), "%s/puts", argv[3]);
    puts_count = apply_puts(ssc, puts_dir, "");
    if( puts_count )
        printf("check: %d unsaved script(s) over the stage\n", puts_count);
    printf("check: compiling (sscompile)\n");
    (void)Builder_SscRun(ssc, BUILDER_SSC_COMPILE, result);
    for( int i = 0; i < result->diag_count; i++ )
    {
        Builder_SscFormatDiag(&result->diags[i], line, sizeof(line));
        printf("%s\n", line);
    }
    if( result->diags_dropped )
        printf("check: %d more diagnostic(s) in %s\n", result->diags_dropped, result->log_path);
    if( result->summary[0] )
        printf("check: sscompile: %s\n", result->summary);
    Builder_SscFormatSummary(result, line, sizeof(line));
    printf("%s\n", line);
    return result->status == BUILDER_SSC_OK ? 0 : result->status == BUILDER_SSC_FAILED ? 1 : 2;
}
