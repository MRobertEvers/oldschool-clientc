#include "cp_fields.h"

#include "checksum.h"
#include "ini.h"
#include "rsbuffer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Trim in place and drop an inline comment.
 *
 * The same four lines `cp_register.c` needs, and for the same reason: 3rd/ini
 * takes a key as everything up to `=` and a value as everything to end of line, so
 * `server = opcode:77:u2` arrives as key `"server "` and value `" opcode:77:u2"`.
 * Every consumer has to do this, and forgetting it parses a valid file into no
 * settings at all while reporting success.
 *
 * Duplicated rather than shared because sharing it would mean a header the two
 * tools' ini readers both include, and the whole point of both files is that they
 * are small enough to be read straight through.
 */
static void
trim(char* text)
{
    char* start = text;
    size_t length;

    for( char* scan = text; *scan; scan++ )
    {
        if( *scan == ';' || *scan == '#' )
        {
            *scan = '\0';
            break;
        }
    }
    while( *start == ' ' || *start == '\t' )
        start++;
    if( start != text )
        memmove(text, start, strlen(start) + 1);
    length = strlen(text);
    while( length > 0 && (text[length - 1] == ' ' || text[length - 1] == '\t' ||
                          text[length - 1] == '\r' || text[length - 1] == '\n') )
        text[--length] = '\0';
}

static struct CP_Field*
find_mutable(struct CP_Fields* fields, const char* name)
{
    for( int i = 0; i < fields->count; i++ )
    {
        if( strcmp(fields->entries[i].name, name) == 0 )
            return &fields->entries[i];
    }
    return NULL;
}

const struct CP_Field*
cp_fields_find(const struct CP_Fields* fields, const char* name)
{
    if( !fields || !name )
        return NULL;
    for( int i = 0; i < fields->count; i++ )
    {
        if( strcmp(fields->entries[i].name, name) == 0 )
            return &fields->entries[i];
    }
    return NULL;
}

static enum CP_FieldWire
parse_wire(const char* text)
{
    if( strcmp(text, "u1") == 0 )
        return CP_FIELD_WIRE_U1;
    if( strcmp(text, "u2") == 0 )
        return CP_FIELD_WIRE_U2;
    if( strcmp(text, "u4") == 0 )
        return CP_FIELD_WIRE_U4;
    if( strcmp(text, "string") == 0 )
        return CP_FIELD_WIRE_STRING;
    return CP_FIELD_WIRE_NONE;
}

/**
 * `server = opcode:<n>:<wire>`.
 *
 * A row that names an opcode outside 64..255 is *counted and dropped*, not
 * clamped. `content_fields.c` prints and continues there because it still has a
 * usable record without that field; a writer does not — emitting it into the
 * client band is the corruption the reserved band exists to prevent — so here it
 * becomes a `cp_fields_check` failure.
 */
static void
parse_server(
    struct CP_Fields* fields,
    struct CP_Field* field,
    const char* value)
{
    char wire_text[16] = "u1";
    int opcode = 0;

    if( strcmp(value, "drop") == 0 )
    {
        field->opcode = 0;
        field->wire = CP_FIELD_WIRE_NONE;
        return;
    }
    if( strncmp(value, "opcode:", 7) != 0 )
    {
        fprintf(stderr, "cachepack: fields/%s.ini: [%s.%s] server = `%s` is not `opcode:<n>:<wire>` "
                        "or `drop`\n",
                fields->type, fields->type, field->name, value);
        fields->rejected++;
        return;
    }
    if( sscanf(value + 7, "%d:%15s", &opcode, wire_text) < 1 )
    {
        fprintf(stderr, "cachepack: fields/%s.ini: [%s.%s] server = `%s` has no opcode number\n",
                fields->type, fields->type, field->name, value);
        fields->rejected++;
        return;
    }
    if( opcode < 64 || opcode > 255 )
    {
        fprintf(stderr,
                "cachepack: fields/%s.ini: [%s.%s] server opcode %d is outside 64..255 — below 64 "
                "is the client band, and a server record that overlaps it stops being "
                "distinguishable from a client one\n",
                fields->type, fields->type, field->name, opcode);
        fields->rejected++;
        return;
    }
    field->opcode = opcode;
    field->wire = parse_wire(wire_text);
    if( field->wire == CP_FIELD_WIRE_NONE )
    {
        fprintf(stderr, "cachepack: fields/%s.ini: [%s.%s] unknown wire width `%s`\n",
                fields->type, fields->type, field->name, wire_text);
        fields->rejected++;
        field->opcode = 0;
    }
}

/** `native` | `drop` | `error` | `param:<name>`. */
static enum CP_FieldClient
parse_client(
    const char* value,
    char* out_param,
    size_t out_param_size,
    enum CP_FieldClient fallback)
{
    if( strcmp(value, "native") == 0 )
        return CP_FIELD_CLIENT_NATIVE;
    if( strcmp(value, "drop") == 0 )
        return CP_FIELD_CLIENT_DROP;
    if( strcmp(value, "error") == 0 )
        return CP_FIELD_CLIENT_ERROR;
    if( strncmp(value, "param:", 6) == 0 && value[6] )
    {
        snprintf(out_param, out_param_size, "%s", value + 6);
        return CP_FIELD_CLIENT_PARAM;
    }
    return fallback;
}

/**
 * Band fields first and ascending, everything else after.
 *
 * A stable insertion sort on the key `(has-no-opcode, opcode)`. Ascending among
 * the band is a *choice* — the reader dispatches per opcode — made so two packs
 * of the same content are byte-identical; the split is what lets the band walk
 * stop at `band_count` instead of testing every entry.
 */
static void
sort_by_opcode(struct CP_Fields* fields)
{
    for( int i = 1; i < fields->count; i++ )
    {
        struct CP_Field key = fields->entries[i];
        int key_rank = key.wire == CP_FIELD_WIRE_NONE ? 1 : 0;
        int j = i - 1;

        while( j >= 0 )
        {
            int rank = fields->entries[j].wire == CP_FIELD_WIRE_NONE ? 1 : 0;

            if( rank < key_rank )
                break;
            if( rank == key_rank && (key_rank == 1 || fields->entries[j].opcode <= key.opcode) )
                break;
            fields->entries[j + 1] = fields->entries[j];
            j--;
        }
        fields->entries[j + 1] = key;
    }
    fields->band_count = 0;
    for( int i = 0; i < fields->count; i++ )
    {
        if( fields->entries[i].wire != CP_FIELD_WIRE_NONE )
            fields->band_count++;
    }
}

int
cp_fields_load(
    struct CP_Fields* fields,
    const char* srcdir,
    const char* type)
{
    char path[1200];
    char prefix[64];
    FILE* file;
    long size;
    uint8_t* data;
    struct INIReader reader;
    struct INIElement element;
    struct CP_Field* current = NULL;
    int in_type_section = 0;

    memset(fields, 0, sizeof(*fields));
    snprintf(fields->type, sizeof(fields->type), "%s", type);

    snprintf(path, sizeof(path), "%s/fields/%s.ini", srcdir, type);
    file = fopen(path, "rb");
    if( !file )
        return 0;

    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if( size <= 0 )
    {
        fclose(file);
        return 0;
    }
    data = (uint8_t*)malloc((size_t)size);
    if( !data )
    {
        fclose(file);
        return 0;
    }
    if( fread(data, 1, (size_t)size, file) != (size_t)size )
    {
        free(data);
        fclose(file);
        return 0;
    }
    fclose(file);

    /* `[npc.hitpoints]`, so one file could describe several types without the
     * sections colliding — and so a field can be declared in more than one block,
     * which this tree does: `fields/npc.ini` states the projection at the top and
     * the server opcodes at the bottom, under the same section names. Sections are
     * therefore *merged* by name rather than appended. */
    snprintf(prefix, sizeof(prefix), "%s.", type);

    ini_reader_init(&reader);
    while( ini_reader_next(&reader, data, (uint32_t)size, &element) == TORI_INI_ERR_OK )
    {
        if( element.kind == INI_ELEMENT_SECTION )
        {
            const char* name = element._section.name;

            trim(element._section.name);
            current = NULL;
            /* A bare `[param]` is about the *type*, not a field of it. Kept
             * distinct from `[param.foo]` by the absent dot, so the two grammars
             * cannot collide. */
            if( strcmp(name, type) == 0 )
            {
                in_type_section = 1;
                continue;
            }
            in_type_section = 0;
            if( strncmp(name, prefix, strlen(prefix)) != 0 )
                continue;
            name += strlen(prefix);

            current = find_mutable(fields, name);
            if( !current && fields->count < CP_FIELDS_MAX )
            {
                current = &fields->entries[fields->count++];
                memset(current, 0, sizeof(*current));
                snprintf(current->name, sizeof(current->name), "%s", name);
            }
            continue;
        }
        if( element.kind != INI_ELEMENT_KEYVAL )
            continue;
        if( in_type_section )
        {
            trim(element._keyval.name);
            trim(element._keyval.value);
            if( strcmp(element._keyval.name, "records") == 0 )
                fields->records_client = strcmp(element._keyval.value, "client") == 0;
            continue;
        }
        if( !current )
            continue;

        trim(element._keyval.name);
        trim(element._keyval.value);

        if( strcmp(element._keyval.name, "server") == 0 )
            parse_server(fields, current, element._keyval.value);
        else if( strcmp(element._keyval.name, "ref") == 0 )
            snprintf(current->ref, sizeof(current->ref), "%s", element._keyval.value);
        else if( strcmp(element._keyval.name, "client") == 0 )
            current->client = parse_client(element._keyval.value, current->param_name,
                                           sizeof(current->param_name), current->client);
        /* `scope` is the reader's half and means nothing to a writer: who reads a
         * field does not change how it is written. Skipped rather than rejected —
         * an unknown key is how the two halves of one file stay independent. */
    }

    free(data);
    fields->from_file = 1;

    /*
     * Every declaration is kept, including the ones with no server opcode.
     *
     * `[npc.name]` (`client = native`) and `[npc.magic]` (`client = drop`) carry
     * no opcode and are still load-bearing: they are how the client-side filter
     * tells a key the encoder handles from a key it must never see. An earlier cut
     * of this file dropped them, which left `hitpoints` and `name`
     * indistinguishable to everything downstream.
     */
    sort_by_opcode(fields);
    return fields->band_count;
}

int
cp_fields_check(const struct CP_Fields* fields)
{
    int violations = fields->rejected;

    /* `band_count`, not `count`: a declaration with no `server =` row has opcode
     * 0 by construction and is not in the band at all. Checking it against the
     * reserved range would fail every `[npc.name]` in the file. */
    for( int i = 0; i < fields->band_count; i++ )
    {
        const struct CP_Field* field = &fields->entries[i];

        if( field->opcode < 64 || field->opcode > 255 )
        {
            fprintf(stderr, "cachepack: fields/%s.ini: [%s.%s] opcode %d is outside 64..255\n",
                    fields->type, fields->type, field->name, field->opcode);
            violations++;
        }
        if( field->wire == CP_FIELD_WIRE_STRING )
        {
            /* The reader's `enum ServerWire` has no string case, so the decode
             * would stop at this opcode and every field after it in the record
             * would be silently absent. */
            fprintf(stderr,
                    "cachepack: fields/%s.ini: [%s.%s] declares `wire = string`, which the server "
                    "codec cannot decode — the stream would terminate at opcode %d\n",
                    fields->type, fields->type, field->name, field->opcode);
            violations++;
        }
        for( int j = i + 1; j < fields->count; j++ )
        {
            if( fields->entries[j].opcode != field->opcode )
                continue;
            fprintf(stderr,
                    "cachepack: fields/%s.ini: `%s` and `%s` both claim opcode %d — one of them "
                    "would be written and never read back\n",
                    fields->type, field->name, fields->entries[j].name, field->opcode);
            violations++;
        }
    }
    return violations;
}

/* ---- the band ----------------------------------------------------------- */

void
cp_server_band_init(struct CP_ServerBand* band)
{
    memset(band, 0, sizeof(*band));
}

/*
 * The bytes go through RSCache_Buffer, for the reason its reader does.
 *
 * `torirs_server_servercodec.c` decodes this band with `g1/g2/g4`, and `rsbuffer.h`
 * states the one property that makes the pair safe: *"every `g` below has a `p`
 * that is its exact inverse"*. Writing the shifts out here instead would put the
 * two directions in two files that agree only by inspection — and a u2 written
 * little-endian and read big-endian is still a perfectly well-formed stream, so
 * nothing would report the disagreement.
 */
static void
band_cursor(
    struct CP_ServerBand* band,
    struct RSCache_Buffer* buffer)
{
    RSCache_BufferInit(buffer, band->bytes, CP_SERVER_BAND_MAX);
    buffer->position = (uint32_t)band->at;
}

int
cp_server_band_put(
    struct CP_ServerBand* band,
    const struct CP_Field* field,
    int value)
{
    struct RSCache_Buffer buffer;

    if( !band || !field )
        return 0;
    /* One byte held back for the terminator, which `finish` must always fit. */
    if( band->at + 1 + (int)field->wire > CP_SERVER_BAND_MAX - 1 )
        return 0;

    /*
     * Range-checked before a single byte is written, and refused rather than
     * masked. `p1` would assert and `p2` would truncate silently, and a truncated
     * id is a perfectly valid id belonging to some other record — which is the
     * exact failure this whole register exists to make impossible.
     */
    switch( field->wire )
    {
    case CP_FIELD_WIRE_U1:
        if( value < 0 || value > 0xFF )
            return 0;
        break;
    case CP_FIELD_WIRE_U2:
        if( value < 0 || value > 0xFFFF )
            return 0;
        break;
    case CP_FIELD_WIRE_U4:
        /* Every 32-bit pattern reads back as the `int` the reader assembles, so
         * -1 is representable here and only here — which is why `death_drop` is
         * declared u4. */
        break;
    default:
        return 0;
    }

    band_cursor(band, &buffer);
    RSCache_BufferP1(&buffer, field->opcode);
    switch( field->wire )
    {
    case CP_FIELD_WIRE_U1:
        RSCache_BufferP1(&buffer, value);
        break;
    case CP_FIELD_WIRE_U2:
        RSCache_BufferP2(&buffer, value);
        break;
    default:
        RSCache_BufferP4(&buffer, value);
        break;
    }
    band->at = (int)RSCache_BufferLength(&buffer);
    band->stated++;
    return 1;
}

int
cp_server_band_finish(struct CP_ServerBand* band)
{
    struct RSCache_Buffer buffer;

    if( band->at >= CP_SERVER_BAND_MAX )
        return 0;
    band_cursor(band, &buffer);
    RSCache_BufferP1(&buffer, 0);
    band->at = (int)RSCache_BufferLength(&buffer);
    return band->at;
}

/* ---- the archive -------------------------------------------------------- */

/** The shared framing: magic, version, kind, CRC, payload. */
uint32_t
cp_server_archive_build_payload(
    int kind,
    const uint8_t* payload,
    uint32_t payload_size,
    uint8_t* out,
    uint32_t out_capacity)
{
    struct RSCache_Buffer buffer;
    uint32_t crc;

    if( !payload || !out || payload_size == 0 )
        return 0;
    if( out_capacity < payload_size + CP_SERVER_PACK_HEADER )
        return 0;

    crc = RSCache_Crc32Buffer(payload, (size_t)payload_size);

    RSCache_BufferInit(&buffer, out, out_capacity);
    RSCache_BufferP1(&buffer, 'S');
    RSCache_BufferP1(&buffer, 'P');
    RSCache_BufferP1(&buffer, CP_SERVER_PACK_VERSION);
    RSCache_BufferP1(&buffer, kind);
    RSCache_BufferP4(&buffer, (int)crc);
    memcpy(out + CP_SERVER_PACK_HEADER, payload, (size_t)payload_size);
    return payload_size + CP_SERVER_PACK_HEADER;
}

uint32_t
cp_server_archive_build(
    const struct CP_ServerBand* band,
    uint8_t* out,
    uint32_t out_capacity)
{
    if( !band || band->at <= 0 )
        return 0;
    return cp_server_archive_build_payload(
        CP_SERVER_PAYLOAD_BAND, band->bytes, (uint32_t)band->at, out, out_capacity);
}

int
cp_server_archive_open(
    const uint8_t* data,
    int size,
    int* out_version,
    int* out_kind,
    const uint8_t** out_payload,
    int* out_payload_size)
{
    struct RSCache_Buffer buffer;
    uint32_t stated;
    uint32_t actual;
    int payload_size;
    int version;
    int kind;

    if( !data || size < CP_SERVER_PACK_HEADER )
        return 0;

    RSCache_BufferInit(&buffer, (uint8_t*)data, (uint32_t)size);
    if( RSCache_BufferG1(&buffer) != 'S' || RSCache_BufferG1(&buffer) != 'P' )
        return 0;
    version = RSCache_BufferG1(&buffer);
    kind = RSCache_BufferG1(&buffer);
    stated = (uint32_t)RSCache_BufferG4(&buffer);

    payload_size = size - CP_SERVER_PACK_HEADER;
    actual = RSCache_Crc32Buffer(data + CP_SERVER_PACK_HEADER, (size_t)payload_size);
    if( stated != actual )
        return 0;

    if( out_version )
        *out_version = version;
    if( out_kind )
        *out_kind = kind;
    if( out_payload )
        *out_payload = data + CP_SERVER_PACK_HEADER;
    if( out_payload_size )
        *out_payload_size = payload_size;
    return 1;
}

/* ---- server-only namespaces ---------------------------------------------- */

/*
 * One group per server-only namespace, from 128 up. See cp_fields.h for why the
 * base is 128 and why the space is one byte wide.
 *
 * Assigned here and nowhere else. The numbers are ours, so nothing outside this
 * project breaks if they move — but they must move *together* with whatever reads
 * the pack, which is why they are in one table rather than spelled at each use.
 */
static const struct CP_ServerGroup k_server_groups[] = {
    { "stat", CP_SERVER_GROUP_BASE + 0 },
    { "category", CP_SERVER_GROUP_BASE + 1 },
};

#define SERVER_GROUP_COUNT ((int)(sizeof(k_server_groups) / sizeof(k_server_groups[0])))

const struct CP_ServerGroup*
cp_server_groups(int* out_count)
{
    if( out_count )
        *out_count = SERVER_GROUP_COUNT;
    return k_server_groups;
}

int
cp_server_group_for(const char* ns)
{
    if( !ns )
        return -1;
    for( int i = 0; i < SERVER_GROUP_COUNT; i++ )
    {
        if( strcmp(k_server_groups[i].name, ns) == 0 )
            return k_server_groups[i].group;
    }
    return -1;
}

uint32_t
cp_server_names_encode(
    const int* ids,
    const char* const* names,
    int count,
    uint8_t* out,
    uint32_t out_capacity)
{
    struct RSCache_Buffer buffer;

    if( !ids || !names || !out || count < 0 || count > 0xFFFF )
        return 0;

    /* Sized before anything is written, because the cursor is borrowed and `p`
     * asserts rather than growing. */
    {
        uint32_t need = 2;

        for( int i = 0; i < count; i++ )
            need += 4 + (uint32_t)strlen(names[i] ? names[i] : "") + 1;
        if( out_capacity < need )
            return 0;
    }

    RSCache_BufferInit(&buffer, out, out_capacity);
    RSCache_BufferP2(&buffer, count);
    for( int i = 0; i < count; i++ )
    {
        const char* name = names[i] ? names[i] : "";
        size_t length = strlen(name) + 1;

        RSCache_BufferP4(&buffer, ids[i]);
        memcpy(buffer.data + buffer.position, name, length);
        buffer.position += (uint32_t)length;
    }
    return RSCache_BufferLength(&buffer);
}

int
cp_server_names_decode(
    const uint8_t* data,
    int size,
    int* out_ids,
    const char** out_names,
    int max)
{
    struct RSCache_Buffer buffer;
    int count;

    if( !data || size < 2 )
        return -1;
    RSCache_BufferInit(&buffer, (uint8_t*)data, (uint32_t)size);
    count = RSCache_BufferG2(&buffer);

    for( int i = 0; i < count; i++ )
    {
        int id;
        const char* name;
        uint32_t at;

        if( RSCache_BufferRemaining(&buffer) < 5 )
            return -1;
        id = RSCache_BufferG4(&buffer);
        name = (const char*)data + buffer.position;
        at = buffer.position;
        while( at < (uint32_t)size && data[at] )
            at++;
        /* A name must be terminated *inside* the payload: a table whose last
         * string runs to the end without a NUL would hand the caller a pointer
         * into whatever follows the archive. */
        if( at >= (uint32_t)size )
            return -1;
        buffer.position = at + 1;
        if( i < max )
        {
            if( out_ids )
                out_ids[i] = id;
            if( out_names )
                out_names[i] = name;
        }
    }
    return count;
}
