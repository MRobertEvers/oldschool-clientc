#include "builder_text.h"

#include "mem/torirs_mem.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* ---- buffer -------------------------------------------------------------- */

void
Builder_BufInit(struct Builder_Buf* buf)
{
    assert(buf);
    memset(buf, 0, sizeof(*buf));
}

void
Builder_BufFree(struct Builder_Buf* buf)
{
    if( !buf )
        return;
    if( buf->data )
        ToriMem_Free(ToriMem_Editor(), buf->data);
    memset(buf, 0, sizeof(*buf));
}

static void
buf_reserve(
    struct Builder_Buf* buf,
    size_t extra)
{
    size_t need = buf->length + extra + 1;
    size_t capacity;

    if( need <= buf->capacity )
        return;
    capacity = buf->capacity ? buf->capacity : 256;
    while( capacity < need )
        capacity *= 2;
    buf->data = ToriMem_Realloc(ToriMem_Editor(), buf->data, capacity);
    assert(buf->data);
    buf->capacity = capacity;
}

void
Builder_BufPut(
    struct Builder_Buf* buf,
    const char* data,
    size_t length)
{
    assert(buf);
    assert(data || length == 0);

    buf_reserve(buf, length);
    if( length > 0 )
        memcpy(buf->data + buf->length, data, length);
    buf->length += length;
    buf->data[buf->length] = '\0';
}

void
Builder_BufPuts(
    struct Builder_Buf* buf,
    const char* text)
{
    assert(text);
    Builder_BufPut(buf, text, strlen(text));
}

void
Builder_BufPrintf(
    struct Builder_Buf* buf,
    const char* format,
    ...)
{
    va_list args;
    va_list copy;
    int need;

    assert(buf);
    assert(format);

    va_start(args, format);
    va_copy(copy, args);
    need = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    assert(need >= 0);
    buf_reserve(buf, (size_t)need);
    vsnprintf(buf->data + buf->length, (size_t)need + 1, format, args);
    va_end(args);
    buf->length += (size_t)need;
}

const char*
Builder_BufText(const struct Builder_Buf* buf)
{
    assert(buf);
    return buf->data ? buf->data : "";
}

/* ---- splices ------------------------------------------------------------- */

static void
splice_init(
    struct Editor_Cmd* command,
    enum Editor_DocKind kind,
    const char* key,
    size_t offset,
    const char* before,
    size_t before_len,
    const char* after,
    size_t after_len)
{
    assert(before_len + after_len <= EDITOR_CMD_SPLICE_MAX);

    Editor_CmdInit(command, kind, key, EDITOR_CMD_TEXT_SPLICE);
    command->has_before = 1;
    command->has_after = 1;
    command->u.text_splice.offset = (uint32_t)offset;
    command->u.text_splice.before_len = (uint32_t)before_len;
    command->u.text_splice.after_len = (uint32_t)after_len;
    if( before_len > 0 )
        memcpy(command->u.text_splice.text, before, before_len);
    if( after_len > 0 )
        memcpy(command->u.text_splice.text + before_len, after, after_len);
}

int
Builder_TextSplices(
    enum Editor_DocKind kind,
    const char* key,
    const char* old_text,
    size_t old_length,
    const char* new_text,
    size_t new_length,
    struct Editor_Cmd* out_commands,
    int max)
{
    size_t prefix = 0;
    size_t suffix = 0;
    size_t old_mid;
    size_t new_mid;
    size_t at;
    size_t old_done = 0;
    size_t new_done = 0;
    int count = 0;
    const size_t half = EDITOR_CMD_SPLICE_MAX / 2;

    assert(key);
    assert(old_text || old_length == 0);
    assert(new_text || new_length == 0);
    assert(out_commands || max == 0);

    while( prefix < old_length && prefix < new_length && old_text[prefix] == new_text[prefix] )
        prefix++;
    while( suffix < old_length - prefix && suffix < new_length - prefix
           && old_text[old_length - 1 - suffix] == new_text[new_length - 1 - suffix] )
        suffix++;
    old_mid = old_length - prefix - suffix;
    new_mid = new_length - prefix - suffix;
    if( old_mid == 0 && new_mid == 0 )
        return 0;

    /* Each piece replaces the next run of the old middle with the next run
     * of the new one, at the point the earlier pieces have reached: the old
     * text from there on has not been touched yet, so its `before` is still
     * exactly the old bytes. */
    at = prefix;
    while( old_done < old_mid || new_done < new_mid )
    {
        size_t take_old = old_mid - old_done;
        size_t take_new = new_mid - new_done;

        if( take_old + take_new > EDITOR_CMD_SPLICE_MAX )
        {
            if( take_old > half && take_new > half )
            {
                take_old = half;
                take_new = half;
            }
            else if( take_old > half )
                take_old = EDITOR_CMD_SPLICE_MAX - take_new;
            else
                take_new = EDITOR_CMD_SPLICE_MAX - take_old;
        }
        if( count < max )
            splice_init(&out_commands[count], kind, key, at, old_text + prefix + old_done,
                        take_old, new_text + prefix + new_done, take_new);
        count++;
        old_done += take_old;
        new_done += take_new;
        at += take_new;
    }
    return count;
}

int
Builder_TextSpliceApply(
    struct Builder_Buf* text,
    const struct Editor_Cmd* command)
{
    size_t offset;
    size_t before_len;
    size_t after_len;
    const char* before;
    const char* after;
    char* data;

    assert(text);
    assert(command);
    assert(command->kind == EDITOR_CMD_TEXT_SPLICE);

    offset = command->u.text_splice.offset;
    before_len = command->u.text_splice.before_len;
    after_len = command->u.text_splice.after_len;
    before = command->u.text_splice.text;
    after = command->u.text_splice.text + before_len;
    if( offset > text->length || before_len > text->length - offset )
        return 0;
    if( before_len > 0 && memcmp(text->data + offset, before, before_len) != 0 )
        return 0;

    data = ToriMem_Alloc(ToriMem_Editor(), text->length - before_len + after_len + 1);
    assert(data);
    if( offset > 0 )
        memcpy(data, text->data, offset);
    if( after_len > 0 )
        memcpy(data + offset, after, after_len);
    if( text->length > offset + before_len )
        memcpy(data + offset + after_len, text->data + offset + before_len,
               text->length - offset - before_len);
    data[text->length - before_len + after_len] = '\0';
    if( text->data )
        ToriMem_Free(ToriMem_Editor(), text->data);
    text->length = text->length - before_len + after_len;
    text->capacity = text->length + 1;
    text->data = data;
    return 1;
}

/* ---- submitting ---------------------------------------------------------- */

enum Builder_SyncResult
Builder_TextSync(
    const struct Builder_Submit* submit,
    const char* key,
    const char* text,
    size_t length,
    int in_stroke)
{
    const char* current;
    size_t current_length = 0;
    struct Editor_Cmd* commands;
    int count;
    enum Builder_SyncResult result = BUILDER_SYNC_APPLIED;

    assert(submit);
    assert(submit->apply);
    assert(submit->stroke);
    assert(submit->current);
    assert(key);
    assert(text || length == 0);

    current = submit->current(submit->user_data, EDITOR_DOC_TEXT, key, &current_length);
    count = Builder_TextSplices(EDITOR_DOC_TEXT, key, current ? current : "", current_length,
                                text ? text : "", length, NULL, 0);
    if( count == 0 )
        return BUILDER_SYNC_UNCHANGED;

    commands = ToriMem_Alloc(ToriMem_Editor(), sizeof(*commands) * (size_t)count);
    assert(commands);
    Builder_TextSplices(EDITOR_DOC_TEXT, key, current ? current : "", current_length,
                        text ? text : "", length, commands, count);
    if( !in_stroke )
        submit->stroke(submit->user_data, 1);
    for( int i = 0; i < count; i++ )
    {
        if( !submit->apply(submit->user_data, &commands[i]) )
        {
            result = BUILDER_SYNC_REFUSED;
            break;
        }
    }
    if( !in_stroke )
        submit->stroke(submit->user_data, 0);
    ToriMem_Free(ToriMem_Editor(), commands);
    return result;
}
