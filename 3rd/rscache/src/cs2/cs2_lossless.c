#include "cs2_lossless.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define CS2_LOSSLESS_MAGIC UINT32_C(0x43533231) /* CS21 */
#define CS2_LOSSLESS_MAX_OPS 1000000u
#define CS2_LOSSLESS_MAX_STRING 16777216u
#define CS2_LOSSLESS_MAX_SWITCHES 65536u
#define CS2_LOSSLESS_MAX_CASES 1000000u

uint64_t
RSCache_CS2_LosslessHash(const char* data, size_t length)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    for( size_t i = 0; i < length; i++ )
    {
        hash ^= (unsigned char)data[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static bool
cs2_lossless_strings_equal(const char* left, const char* right)
{
    if( !left || !right )
        return left == right;
    return strcmp(left, right) == 0;
}

bool
RSCache_CS2_LosslessEqual(
    const struct RSCache_CS2_Script* left,
    const struct RSCache_CS2_Script* right)
{
    if( !left || !right || left->script_id != right->script_id ||
        left->local_int_count != right->local_int_count ||
        left->local_string_count != right->local_string_count ||
        left->local_long_count != right->local_long_count ||
        left->int_argument_count != right->int_argument_count ||
        left->string_argument_count != right->string_argument_count ||
        left->long_argument_count != right->long_argument_count || left->op_count != right->op_count ||
        left->switch_table_count != right->switch_table_count ||
        !cs2_lossless_strings_equal(left->signature, right->signature) )
        return false;

    for( int i = 0; i < left->op_count; i++ )
    {
        int64_t left_long = left->long_operands ? left->long_operands[i] : 0;
        int64_t right_long = right->long_operands ? right->long_operands[i] : 0;
        const char* left_string = left->string_operands ? left->string_operands[i] : NULL;
        const char* right_string = right->string_operands ? right->string_operands[i] : NULL;
        if( left->opcodes[i] != right->opcodes[i] ||
            left->int_operands[i] != right->int_operands[i] || left_long != right_long ||
            !cs2_lossless_strings_equal(left_string, right_string) )
            return false;
    }
    for( int i = 0; i < left->switch_table_count; i++ )
    {
        const struct RSCache_CS2_ScriptSwitch* a = &left->switch_tables[i];
        const struct RSCache_CS2_ScriptSwitch* b = &right->switch_tables[i];
        if( a->case_count != b->case_count )
            return false;
        for( int j = 0; j < a->case_count; j++ )
        {
            if( a->cases[j].key != b->cases[j].key ||
                a->cases[j].target_pc != b->cases[j].target_pc )
                return false;
        }
    }
    return true;
}

static void
cs2_lossless_put_byte(struct RSCache_CS2_StrBuf* out, uint8_t value)
{
    static const char hex[] = "0123456789abcdef";
    RSCache_CS2_StrBufAppendChar(out, hex[value >> 4]);
    RSCache_CS2_StrBufAppendChar(out, hex[value & 15]);
}

static void
cs2_lossless_put_u16(struct RSCache_CS2_StrBuf* out, uint16_t value)
{
    cs2_lossless_put_byte(out, (uint8_t)(value >> 8));
    cs2_lossless_put_byte(out, (uint8_t)value);
}

static void
cs2_lossless_put_u32(struct RSCache_CS2_StrBuf* out, uint32_t value)
{
    for( int shift = 24; shift >= 0; shift -= 8 )
        cs2_lossless_put_byte(out, (uint8_t)(value >> shift));
}

static void
cs2_lossless_put_u64(struct RSCache_CS2_StrBuf* out, uint64_t value)
{
    for( int shift = 56; shift >= 0; shift -= 8 )
        cs2_lossless_put_byte(out, (uint8_t)(value >> shift));
}

static bool
cs2_lossless_put_string(struct RSCache_CS2_StrBuf* out, const char* text)
{
    if( !text )
    {
        cs2_lossless_put_u32(out, UINT32_MAX);
        return true;
    }
    size_t length = strlen(text);
    if( length > UINT32_MAX )
        return false;
    cs2_lossless_put_u32(out, (uint32_t)length);
    for( size_t i = 0; i < length; i++ )
        cs2_lossless_put_byte(out, (uint8_t)text[i]);
    return true;
}

bool
RSCache_CS2_LosslessEncode(
    const struct RSCache_CS2_Script* script,
    struct RSCache_CS2_StrBuf* out)
{
    if( !script || !out || script->op_count < 0 || script->switch_table_count < 0 )
        return false;
    cs2_lossless_put_u32(out, CS2_LOSSLESS_MAGIC);
    cs2_lossless_put_u32(out, (uint32_t)script->script_id);
    if( !cs2_lossless_put_string(out, script->signature) )
        return false;
    cs2_lossless_put_u32(out, (uint32_t)script->local_int_count);
    cs2_lossless_put_u32(out, (uint32_t)script->local_string_count);
    cs2_lossless_put_u32(out, (uint32_t)script->local_long_count);
    cs2_lossless_put_u32(out, (uint32_t)script->int_argument_count);
    cs2_lossless_put_u32(out, (uint32_t)script->string_argument_count);
    cs2_lossless_put_u32(out, (uint32_t)script->long_argument_count);
    cs2_lossless_put_u32(out, (uint32_t)script->op_count);
    for( int i = 0; i < script->op_count; i++ )
    {
        cs2_lossless_put_u16(out, script->opcodes[i]);
        cs2_lossless_put_u32(out, (uint32_t)script->int_operands[i]);
        cs2_lossless_put_u64(
            out, (uint64_t)(script->long_operands ? script->long_operands[i] : 0));
        if( !cs2_lossless_put_string(
                out, script->string_operands ? script->string_operands[i] : NULL) )
            return false;
    }
    cs2_lossless_put_u32(out, (uint32_t)script->switch_table_count);
    for( int i = 0; i < script->switch_table_count; i++ )
    {
        const struct RSCache_CS2_ScriptSwitch* table = &script->switch_tables[i];
        if( table->case_count < 0 )
            return false;
        cs2_lossless_put_u32(out, (uint32_t)table->case_count);
        for( int j = 0; j < table->case_count; j++ )
        {
            cs2_lossless_put_u32(out, (uint32_t)table->cases[j].key);
            cs2_lossless_put_u32(out, (uint32_t)table->cases[j].target_pc);
        }
    }
    return true;
}

struct cs2_lossless_reader
{
    const char* cursor;
    bool failed;
};

static int
cs2_lossless_nibble(char ch)
{
    if( ch >= '0' && ch <= '9' )
        return ch - '0';
    if( ch >= 'a' && ch <= 'f' )
        return ch - 'a' + 10;
    if( ch >= 'A' && ch <= 'F' )
        return ch - 'A' + 10;
    return -1;
}

static uint8_t
cs2_lossless_get_byte(struct cs2_lossless_reader* reader)
{
    int high = cs2_lossless_nibble(reader->cursor[0]);
    int low = high >= 0 ? cs2_lossless_nibble(reader->cursor[1]) : -1;
    if( high < 0 || low < 0 )
    {
        reader->failed = true;
        return 0;
    }
    reader->cursor += 2;
    return (uint8_t)((high << 4) | low);
}

static uint16_t
cs2_lossless_get_u16(struct cs2_lossless_reader* reader)
{
    uint16_t value = 0;
    for( int i = 0; i < 2; i++ )
        value = (uint16_t)((value << 8) | cs2_lossless_get_byte(reader));
    return value;
}

static uint32_t
cs2_lossless_get_u32(struct cs2_lossless_reader* reader)
{
    uint32_t value = 0;
    for( int i = 0; i < 4; i++ )
        value = (value << 8) | cs2_lossless_get_byte(reader);
    return value;
}

static uint64_t
cs2_lossless_get_u64(struct cs2_lossless_reader* reader)
{
    uint64_t value = 0;
    for( int i = 0; i < 8; i++ )
        value = (value << 8) | cs2_lossless_get_byte(reader);
    return value;
}

static char*
cs2_lossless_get_string(struct cs2_lossless_reader* reader)
{
    uint32_t length = cs2_lossless_get_u32(reader);
    if( reader->failed || length == UINT32_MAX )
        return NULL;
    if( length > CS2_LOSSLESS_MAX_STRING )
    {
        reader->failed = true;
        return NULL;
    }
    char* text = (char*)malloc((size_t)length + 1);
    if( !text )
    {
        reader->failed = true;
        return NULL;
    }
    for( uint32_t i = 0; i < length; i++ )
        text[i] = (char)cs2_lossless_get_byte(reader);
    text[length] = '\0';
    if( reader->failed )
    {
        free(text);
        return NULL;
    }
    return text;
}

bool
RSCache_CS2_LosslessDecode(
    const char* text,
    struct RSCache_ClientScript* out,
    const char** end)
{
    if( end )
        *end = text;
    if( !text || !out )
        return false;
    memset(out, 0, sizeof(*out));
    struct RSCache_CS2_Script* script = &out->script;
    RSCache_CS2_ScriptInit(script);
    struct cs2_lossless_reader reader = { text, false };
    if( cs2_lossless_get_u32(&reader) != CS2_LOSSLESS_MAGIC )
        reader.failed = true;
    script->script_id = (int)cs2_lossless_get_u32(&reader);
    script->signature = cs2_lossless_get_string(&reader);
    if( !script->signature )
        reader.failed = true;
    script->local_int_count = (int)cs2_lossless_get_u32(&reader);
    script->local_string_count = (int)cs2_lossless_get_u32(&reader);
    script->local_long_count = (int)cs2_lossless_get_u32(&reader);
    script->int_argument_count = (int)cs2_lossless_get_u32(&reader);
    script->string_argument_count = (int)cs2_lossless_get_u32(&reader);
    script->long_argument_count = (int)cs2_lossless_get_u32(&reader);
    uint32_t op_count = cs2_lossless_get_u32(&reader);
    if( op_count == 0 || op_count > CS2_LOSSLESS_MAX_OPS )
        reader.failed = true;
    if( !reader.failed )
    {
        script->op_count = (int)op_count;
        script->opcodes = (uint16_t*)calloc(op_count, sizeof(*script->opcodes));
        script->int_operands = (int*)calloc(op_count, sizeof(*script->int_operands));
        script->long_operands = (int64_t*)calloc(op_count, sizeof(*script->long_operands));
        script->string_operands = (char**)calloc(op_count, sizeof(*script->string_operands));
        if( !script->opcodes || !script->int_operands || !script->long_operands ||
            !script->string_operands )
            reader.failed = true;
    }
    for( uint32_t i = 0; !reader.failed && i < op_count; i++ )
    {
        script->opcodes[i] = cs2_lossless_get_u16(&reader);
        script->int_operands[i] = (int)cs2_lossless_get_u32(&reader);
        script->long_operands[i] = (int64_t)cs2_lossless_get_u64(&reader);
        script->string_operands[i] = cs2_lossless_get_string(&reader);
        /* A NULL string is represented explicitly and is not a decode failure. */
        if( reader.failed )
            break;
    }
    uint32_t switch_count = reader.failed ? 0 : cs2_lossless_get_u32(&reader);
    if( switch_count > CS2_LOSSLESS_MAX_SWITCHES ||
        (!reader.failed && !RSCache_CS2_ScriptAllocSwitches(script, (int)switch_count)) )
        reader.failed = true;
    uint64_t total_cases = 0;
    for( uint32_t i = 0; !reader.failed && i < switch_count; i++ )
    {
        uint32_t case_count = cs2_lossless_get_u32(&reader);
        total_cases += case_count;
        if( total_cases > CS2_LOSSLESS_MAX_CASES ||
            !RSCache_CS2_ScriptAllocSwitchCases(script, (int)i, (int)case_count) )
        {
            reader.failed = true;
            break;
        }
        for( uint32_t j = 0; !reader.failed && j < case_count; j++ )
        {
            script->switch_tables[i].cases[j].key = (int)cs2_lossless_get_u32(&reader);
            script->switch_tables[i].cases[j].target_pc = (int)cs2_lossless_get_u32(&reader);
        }
    }
    if( end )
        *end = reader.cursor;
    if( reader.failed )
    {
        RSCache_ClientScriptFreeInplace(out);
        return false;
    }
    return true;
}
