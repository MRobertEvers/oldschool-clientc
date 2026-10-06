#include "rscache_register.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* One line of the file, trimmed, as [begin, end). */
struct RegisterLine
{
    const char* begin;
    const char* end;
};

static int
is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static void
trim(struct RegisterLine* line)
{
    while( line->begin < line->end && is_space(*line->begin) )
        line->begin++;
    while( line->end > line->begin && is_space(line->end[-1]) )
        line->end--;
}

/* Copy [begin, end) into `out`, truncated to fit. */
static void
copy_span(
    char* out,
    size_t out_size,
    const char* begin,
    const char* end)
{
    size_t n = (size_t)(end - begin);

    if( n >= out_size )
        n = out_size - 1;
    memcpy(out, begin, n);
    out[n] = '\0';
}

static struct RSCache_RegisterField*
find_mutable(
    struct RSCache_Register* reg,
    const char* name)
{
    for( int i = 0; i < reg->count; i++ )
    {
        if( strcmp(reg->entries[i].name, name) == 0 )
            return &reg->entries[i];
    }
    return NULL;
}

static void
parse_server(
    struct RSCache_Register* reg,
    struct RSCache_RegisterField* field,
    const char* value)
{
    char wire[16] = "u1";
    int opcode = 0;

    if( strcmp(value, "drop") == 0 )
    {
        field->opcode = 0;
        field->wire = RSCACHE_REGISTER_WIRE_NONE;
        return;
    }
    if( strncmp(value, "opcode:", 7) != 0 || sscanf(value + 7, "%d:%15s", &opcode, wire) < 1 )
    {
        fprintf(stderr, "fields/%s.ini: [%s.%s] server = `%s` is not `opcode:<n>:<wire>` or "
                        "`drop`\n",
                reg->type, reg->type, field->name, value);
        reg->rejected++;
        return;
    }
    /* Below 64 is the client cache's own opcode band: a server record that
     * overlapped it would stop being distinguishable from a client one. */
    if( opcode < 64 || opcode > 255 )
    {
        fprintf(stderr, "fields/%s.ini: [%s.%s] server opcode %d is outside 64..255\n", reg->type,
                reg->type, field->name, opcode);
        reg->rejected++;
        return;
    }
    if( strcmp(wire, "u1") == 0 )
        field->wire = RSCACHE_REGISTER_WIRE_U1;
    else if( strcmp(wire, "u2") == 0 )
        field->wire = RSCACHE_REGISTER_WIRE_U2;
    else if( strcmp(wire, "u4") == 0 )
        field->wire = RSCACHE_REGISTER_WIRE_U4;
    else if( strcmp(wire, "string") == 0 )
        field->wire = RSCACHE_REGISTER_WIRE_STRING;
    else if( strcmp(wire, "list") == 0 )
        field->wire = RSCACHE_REGISTER_WIRE_LIST;
    else
    {
        fprintf(stderr, "fields/%s.ini: [%s.%s] unknown wire width `%s`\n", reg->type, reg->type,
                field->name, wire);
        reg->rejected++;
        return;
    }
    field->opcode = opcode;
}

static void
parse_client(
    struct RSCache_Register* reg,
    struct RSCache_RegisterField* field,
    const char* value)
{
    if( strcmp(value, "native") == 0 )
        field->client = RSCACHE_REGISTER_CLIENT_NATIVE;
    else if( strcmp(value, "drop") == 0 )
        field->client = RSCACHE_REGISTER_CLIENT_DROP;
    else if( strcmp(value, "error") == 0 )
        field->client = RSCACHE_REGISTER_CLIENT_ERROR;
    else if( strncmp(value, "param:", 6) == 0 && value[6] )
    {
        field->client = RSCACHE_REGISTER_CLIENT_PARAM;
        snprintf(field->param_name, sizeof(field->param_name), "%s", value + 6);
    }
    else
    {
        fprintf(stderr, "fields/%s.ini: [%s.%s] client = `%s` is not native, drop, error or "
                        "param:<name>\n",
                reg->type, reg->type, field->name, value);
        reg->rejected++;
    }
}

static void
parse_types(
    struct RSCache_Register* reg,
    struct RSCache_RegisterField* field,
    const char* value)
{
    const char* at = value;

    field->type_count = 0;
    while( *at )
    {
        const char* comma = strchr(at, ',');
        const char* end = comma ? comma : at + strlen(at);
        struct RegisterLine name = { at, end };

        trim(&name);
        if( name.begin == name.end || field->type_count == RSCACHE_REGISTER_TYPES_MAX ||
            (size_t)(name.end - name.begin) >= sizeof(field->types[0]) )
        {
            fprintf(stderr, "fields/%s.ini: [%s.%s] type = `%s` is not 1..%d short type names\n",
                    reg->type, reg->type, field->name, value, RSCACHE_REGISTER_TYPES_MAX);
            reg->rejected++;
            field->type_count = 0;
            return;
        }
        copy_span(field->types[field->type_count++], sizeof(field->types[0]), name.begin,
                  name.end);
        if( !comma )
            break;
        at = comma + 1;
    }
}

static void
parse_words(
    struct RSCache_Register* reg,
    struct RSCache_RegisterField* field,
    const char* value)
{
    const char* at = value;

    field->word_count = 0;
    while( *at )
    {
        const char* comma = strchr(at, ',');
        const char* end = comma ? comma : at + strlen(at);
        const char* colon = memchr(at, ':', (size_t)(end - at));
        struct RegisterLine word = { at, colon ? colon : end };
        char number[16];
        char* number_end;
        long parsed = 0;
        int ok = colon != NULL;

        trim(&word);
        if( ok )
        {
            struct RegisterLine digits = { colon + 1, end };

            trim(&digits);
            ok = digits.begin != digits.end &&
                 (size_t)(digits.end - digits.begin) < sizeof(number);
            if( ok )
            {
                copy_span(number, sizeof(number), digits.begin, digits.end);
                parsed = strtol(number, &number_end, 10);
                ok = *number_end == '\0' && parsed >= INT32_MIN && parsed <= INT32_MAX;
            }
        }
        if( !ok || word.begin == word.end || field->word_count == RSCACHE_REGISTER_WORDS_MAX ||
            (size_t)(word.end - word.begin) >= sizeof(field->words[0]) )
        {
            fprintf(stderr, "fields/%s.ini: [%s.%s] values = `%s` is not 1..%d `<word>:<number>` "
                            "pairs\n",
                    reg->type, reg->type, field->name, value, RSCACHE_REGISTER_WORDS_MAX);
            reg->rejected++;
            field->word_count = 0;
            return;
        }
        copy_span(field->words[field->word_count], sizeof(field->words[0]), word.begin, word.end);
        field->word_values[field->word_count++] = (int)parsed;
        if( !comma )
            break;
        at = comma + 1;
    }
}

static void
parse_row(
    struct RSCache_Register* reg,
    struct RSCache_RegisterField* field,
    const char* key,
    const char* value)
{
    if( strcmp(key, "scope") == 0 )
    {
        if( strcmp(value, "server") == 0 )
            field->scope = RSCACHE_REGISTER_SCOPE_SERVER;
        else if( strcmp(value, "client") == 0 )
            field->scope = RSCACHE_REGISTER_SCOPE_CLIENT;
        else
        {
            fprintf(stderr, "fields/%s.ini: [%s.%s] scope = `%s` is not server or client\n",
                    reg->type, reg->type, field->name, value);
            reg->rejected++;
        }
    }
    else if( strcmp(key, "client") == 0 )
        parse_client(reg, field, value);
    else if( strcmp(key, "param") == 0 )
        snprintf(field->param_name, sizeof(field->param_name), "%s", value);
    else if( strcmp(key, "server") == 0 )
        parse_server(reg, field, value);
    else if( strcmp(key, "ref") == 0 )
        snprintf(field->ref, sizeof(field->ref), "%s", value);
    else if( strcmp(key, "text") == 0 )
    {
        if( strcmp(value, "key") == 0 )
            field->text = RSCACHE_REGISTER_TEXT_KEY;
        else if( strcmp(value, "param") == 0 )
            field->text = RSCACHE_REGISTER_TEXT_PARAM;
        else if( strcmp(value, "indexed") == 0 )
            field->text = RSCACHE_REGISTER_TEXT_INDEXED;
        else if( strcmp(value, "list") == 0 )
            field->text = RSCACHE_REGISTER_TEXT_LIST;
        else
        {
            fprintf(stderr, "fields/%s.ini: [%s.%s] text = `%s` is not key, param, indexed or "
                            "list\n",
                    reg->type, reg->type, field->name, value);
            reg->rejected++;
        }
    }
    else if( strcmp(key, "type") == 0 )
        parse_types(reg, field, value);
    else if( strcmp(key, "values") == 0 )
        parse_words(reg, field, value);
    /* Anything else is a row another reader owns. */
}

/* Band fields first, ascending by opcode; the rest after in file order. Stable,
 * so two loads of one file agree, and ascending so two packs of the same content
 * are byte-identical (RSCache_BandEncode walks this order). */
static void
sort_band_first(struct RSCache_Register* reg)
{
    for( int i = 1; i < reg->count; i++ )
    {
        struct RSCache_RegisterField key = reg->entries[i];
        int key_rank = key.opcode ? 0 : 1;
        int j = i - 1;

        while( j >= 0 )
        {
            int rank = reg->entries[j].opcode ? 0 : 1;

            if( rank < key_rank )
                break;
            if( rank == key_rank && (key_rank == 1 || reg->entries[j].opcode <= key.opcode) )
                break;
            reg->entries[j + 1] = reg->entries[j];
            j--;
        }
        reg->entries[j + 1] = key;
    }
    reg->band_count = 0;
    while( reg->band_count < reg->count && reg->entries[reg->band_count].opcode )
        reg->band_count++;
}

int
RSCache_RegisterParse(
    struct RSCache_Register* reg,
    const char* type,
    const char* text,
    size_t size)
{
    const char* at = text;
    const char* end = text + size;
    struct RSCache_RegisterField* current = NULL;
    int in_type_section = 0;
    size_t type_len;

    assert(reg);
    assert(type);
    assert(text || size == 0);
    memset(reg, 0, sizeof(*reg));
    snprintf(reg->type, sizeof(reg->type), "%s", type);
    type_len = strlen(type);

    while( at < end )
    {
        struct RegisterLine line = { at, at };
        char key[64];
        char value[256];
        const char* eq;

        while( line.end < end && *line.end != '\n' )
            line.end++;
        at = line.end < end ? line.end + 1 : end;
        trim(&line);
        if( line.begin == line.end || *line.begin == ';' || *line.begin == '#' )
            continue;

        if( *line.begin == '[' )
        {
            char section[128];

            current = NULL;
            in_type_section = 0;
            if( line.end[-1] != ']' )
            {
                fprintf(stderr, "fields/%s.ini: unterminated section header\n", type);
                reg->rejected++;
                continue;
            }
            copy_span(section, sizeof(section), line.begin + 1, line.end - 1);
            /* `[npc]` is about the type; `[npc.hitpoints]` declares a field. */
            if( strcmp(section, type) == 0 )
            {
                in_type_section = 1;
                continue;
            }
            if( strncmp(section, type, type_len) != 0 || section[type_len] != '.' )
                continue;
            current = find_mutable(reg, section + type_len + 1);
            if( !current )
            {
                if( reg->count == RSCACHE_REGISTER_MAX )
                {
                    fprintf(stderr, "fields/%s.ini: more than %d fields\n", type,
                            RSCACHE_REGISTER_MAX);
                    reg->rejected++;
                    continue;
                }
                current = &reg->entries[reg->count++];
                memset(current, 0, sizeof(*current));
                snprintf(current->name, sizeof(current->name), "%s", section + type_len + 1);
            }
            continue;
        }

        eq = memchr(line.begin, '=', (size_t)(line.end - line.begin));
        if( !eq )
        {
            fprintf(stderr, "fields/%s.ini: a row with no `=`\n", type);
            reg->rejected++;
            continue;
        }
        {
            struct RegisterLine k = { line.begin, eq };
            struct RegisterLine v = { eq + 1, line.end };

            trim(&k);
            trim(&v);
            copy_span(key, sizeof(key), k.begin, k.end);
            copy_span(value, sizeof(value), v.begin, v.end);
        }
        if( in_type_section )
        {
            if( strcmp(key, "records") == 0 )
                reg->records_client = strcmp(value, "client") == 0;
            continue;
        }
        if( current )
            parse_row(reg, current, key, value);
    }

    sort_band_first(reg);
    return reg->count;
}

int
RSCache_RegisterLoad(
    struct RSCache_Register* reg,
    const char* dir,
    const char* type)
{
    char path[1200];
    FILE* file;
    long size;
    char* data;
    int count;

    assert(reg);
    assert(dir);
    assert(type);
    snprintf(path, sizeof(path), "%s/fields/%s.ini", dir, type);
    file = fopen(path, "rb");
    if( !file )
    {
        /* No file: the tree declares nothing for this type. */
        RSCache_RegisterParse(reg, type, "", 0);
        return 0;
    }
    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);
    assert(size >= 0);
    data = malloc((size_t)size + 1);
    assert(data);
    if( fread(data, 1, (size_t)size, file) != (size_t)size )
    {
        fprintf(stderr, "%s: short read\n", path);
        fclose(file);
        free(data);
        RSCache_RegisterParse(reg, type, "", 0);
        reg->rejected++;
        return 0;
    }
    fclose(file);
    count = RSCache_RegisterParse(reg, type, data, (size_t)size);
    reg->from_file = 1;
    free(data);
    return count;
}

const struct RSCache_RegisterField*
RSCache_RegisterFind(
    const struct RSCache_Register* reg,
    const char* name)
{
    assert(reg);
    assert(name);
    for( int i = 0; i < reg->count; i++ )
    {
        if( strcmp(reg->entries[i].name, name) == 0 )
            return &reg->entries[i];
    }
    return NULL;
}

const struct RSCache_RegisterField*
RSCache_RegisterFindLine(
    const struct RSCache_Register* reg,
    const char* key)
{
    const struct RSCache_RegisterField* field;

    assert(reg);
    assert(key);
    field = RSCache_RegisterFind(reg, key);
    if( field )
        return field;
    for( int i = 0; i < reg->count; i++ )
    {
        size_t stem = strlen(reg->entries[i].name);
        const char* digit = key + stem;

        if( reg->entries[i].text != RSCACHE_REGISTER_TEXT_INDEXED ||
            strncmp(reg->entries[i].name, key, stem) != 0 || *digit < '0' || *digit > '9' )
            continue;
        while( *digit >= '0' && *digit <= '9' )
            digit++;
        if( *digit == '\0' )
            return &reg->entries[i];
    }
    return NULL;
}

int
RSCache_RegisterWordValue(
    const struct RSCache_RegisterField* field,
    const char* word,
    int* out)
{
    assert(field);
    assert(word);
    assert(out);
    for( int i = 0; i < field->word_count; i++ )
    {
        if( strcmp(field->words[i], word) == 0 )
        {
            *out = field->word_values[i];
            return 1;
        }
    }
    return 0;
}

const char*
RSCache_RegisterValueWord(
    const struct RSCache_RegisterField* field,
    int value)
{
    assert(field);
    for( int i = 0; i < field->word_count; i++ )
    {
        if( field->word_values[i] == value )
            return field->words[i];
    }
    return NULL;
}

int
RSCache_RegisterCheck(const struct RSCache_Register* reg)
{
    int problems;

    assert(reg);
    problems = reg->rejected;
    for( int i = 0; i < reg->band_count; i++ )
    {
        const struct RSCache_RegisterField* field = &reg->entries[i];

        if( field->wire == RSCACHE_REGISTER_WIRE_LIST && field->type_count == 0 )
        {
            fprintf(stderr, "fields/%s.ini: [%s.%s] a list field states no `type`\n", reg->type,
                    reg->type, field->name);
            problems++;
        }
        if( (field->wire == RSCACHE_REGISTER_WIRE_LIST) !=
            (field->text == RSCACHE_REGISTER_TEXT_INDEXED ||
             field->text == RSCACHE_REGISTER_TEXT_LIST ||
             (field->text == RSCACHE_REGISTER_TEXT_PARAM &&
              field->wire == RSCACHE_REGISTER_WIRE_LIST)) )
        {
            fprintf(stderr, "fields/%s.ini: [%s.%s] a list is spelled `text = indexed`, "
                            "`text = list` or (one tuple per line) `text = param`, and only a "
                            "list is spelled indexed or list\n",
                    reg->type, reg->type, field->name);
            problems++;
        }
        if( field->word_count > 0 && (field->wire == RSCACHE_REGISTER_WIRE_STRING ||
                                      field->wire == RSCACHE_REGISTER_WIRE_LIST) )
        {
            fprintf(stderr, "fields/%s.ini: [%s.%s] `values` names the numbers of an int field, "
                            "and this one is a %s\n",
                    reg->type, reg->type, field->name,
                    field->wire == RSCACHE_REGISTER_WIRE_STRING ? "string" : "list");
            problems++;
        }
        for( int w = 0; w < field->word_count; w++ )
        {
            int64_t v = field->word_values[w];
            int fits = field->wire == RSCACHE_REGISTER_WIRE_U1   ? v >= 0 && v <= 0xFF
                       : field->wire == RSCACHE_REGISTER_WIRE_U2 ? v >= 0 && v <= 0xFFFF
                                                                 : 1;

            if( !fits )
            {
                fprintf(stderr, "fields/%s.ini: [%s.%s] `values` word `%s` is %d, which its "
                                "server width cannot carry\n",
                        reg->type, reg->type, field->name, field->words[w],
                        field->word_values[w]);
                problems++;
            }
        }
        if( i > 0 && reg->entries[i - 1].opcode == field->opcode )
        {
            fprintf(stderr, "fields/%s.ini: [%s.%s] and [%s.%s] share server opcode %d\n",
                    reg->type, reg->type, reg->entries[i - 1].name, reg->type, field->name,
                    field->opcode);
            problems++;
        }
    }
    return problems;
}
