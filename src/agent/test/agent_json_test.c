/*
 * The agent control server's JSON (agent/agent_json.h): the writer's commas,
 * escapes and UTF-8 repair, and the reader's flat object, its arrays, its
 * escapes and every refusal a request can earn.
 *
 *   ./agent_json_test
 */

#include "agent/agent_json.h"
#include "mem/torirs_mem.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_checks;
static int g_failed;

#define CHECK(cond, ...)                                                                           \
    do                                                                                             \
    {                                                                                              \
        g_checks++;                                                                                \
        if( !(cond) )                                                                              \
        {                                                                                          \
            g_failed++;                                                                            \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while( 0 )

/** What the writer holds, as a C string (the buffer is not terminated). */
static const char*
written(struct AgentBuf* buf)
{
    static char text[1024];
    size_t const length = buf->length < sizeof(text) - 1 ? buf->length : sizeof(text) - 1;

    memcpy(text, buf->data, length);
    text[length] = '\0';
    return text;
}

static void
expect_written(
    struct AgentBuf* buf,
    const char* want,
    int line)
{
    const char* got = written(buf);

    g_checks++;
    if( strcmp(got, want) != 0 )
    {
        g_failed++;
        printf("FAIL %s:%d: wrote %s, want %s\n", __FILE__, line, got, want);
    }
}

static void
test_writer(void)
{
    struct AgentBuf buf;
    struct AgentJsonWriter w;
    int const pair[2] = { -3, 40 };

    AgentBuf_Init(&buf);

    /* Members and elements are separated; a value after its key is not. */
    AgentJsonW_Init(&w, &buf);
    AgentJsonW_ObjBegin(&w);
    AgentJsonW_Key(&w, "id");
    AgentJsonW_Raw(&w, "7");
    AgentJsonW_KBool(&w, "ok", 1);
    AgentJsonW_KStr(&w, "mode", "editor");
    AgentJsonW_Key(&w, "surface");
    AgentJsonW_ObjBegin(&w);
    AgentJsonW_KInt(&w, "w", 1600);
    AgentJsonW_KInt(&w, "h", 900);
    AgentJsonW_ObjEnd(&w);
    AgentJsonW_KRect(&w, "rect", 1, 2, 3, 4);
    AgentJsonW_KInts(&w, "at", pair, 2);
    AgentJsonW_Key(&w, "none");
    AgentJsonW_ArrBegin(&w);
    AgentJsonW_ArrEnd(&w);
    AgentJsonW_Key(&w, "rows");
    AgentJsonW_ArrBegin(&w);
    AgentJsonW_ObjBegin(&w);
    AgentJsonW_KInt(&w, "row", 0);
    AgentJsonW_ObjEnd(&w);
    AgentJsonW_ObjBegin(&w);
    AgentJsonW_KInt(&w, "row", 1);
    AgentJsonW_ObjEnd(&w);
    AgentJsonW_Null(&w);
    AgentJsonW_Bool(&w, 0);
    AgentJsonW_ArrEnd(&w);
    AgentJsonW_KDbl(&w, "scale", 1.25);
    AgentJsonW_ObjEnd(&w);
    CHECK(w.depth == 0, "the writer closed every container");
    expect_written(&buf,
        "{\"id\":7,\"ok\":true,\"mode\":\"editor\",\"surface\":{\"w\":1600,\"h\":900},\"rect\":[1,2,3,4],"
        "\"at\":[-3,40],\"none\":[],\"rows\":[{\"row\":0},{\"row\":1},null,false],\"scale\":1.25}",
        __LINE__);

    /* Quotes, backslashes and control characters are escaped; a cells row's
     * 0x1F included. */
    buf.length = 0;
    AgentJsonW_Init(&w, &buf);
    AgentJsonW_Str(&w, "a\"b\\c\nd\te\x1f" "f");
    expect_written(&buf, "\"a\\\"b\\\\c\\nd\\te\\u001ff\"", __LINE__);

    /* Well-formed UTF-8 passes through; a stray high byte (a CP1252 byte
     * typed into a field) is written as its Latin-1 code point, so the line
     * stays valid UTF-8 JSON. */
    buf.length = 0;
    AgentJsonW_Init(&w, &buf);
    AgentJsonW_Str(&w, "\xC3\xA9 \xE2\x8C\x98 \xE9 \xC3");
    expect_written(&buf, "\"\xC3\xA9 \xE2\x8C\x98 \\u00e9 \\u00c3\"", __LINE__);

    /* An overlong encoding and a surrogate are not UTF-8 either. */
    buf.length = 0;
    AgentJsonW_Init(&w, &buf);
    AgentJsonW_Str(&w, "\xC0\x80\xED\xA0\x80");
    expect_written(&buf, "\"\\u00c0\\u0080\\u00ed\\u00a0\\u0080\"", __LINE__);

    /* A slice, and a number JSON cannot spell (an infinity, made at run time
     * so the compiler has no constant to object to). */
    {
        volatile double big = 1e308;

        buf.length = 0;
        AgentJsonW_Init(&w, &buf);
        AgentJsonW_ArrBegin(&w);
        AgentJsonW_StrN(&w, "crates", 5);
        AgentJsonW_Dbl(&w, big * 10.0);
        AgentJsonW_Int(&w, -2147483648LL);
        AgentJsonW_ArrEnd(&w);
        expect_written(&buf, "[\"crate\",null,-2147483648]", __LINE__);
    }

    AgentBuf_Free(&buf);
}

static int
parse(
    struct AgentJsonDoc* doc,
    const char* text,
    char* error,
    size_t error_size)
{
    return AgentJson_Parse(doc, text, strlen(text), error, error_size);
}

static void
test_reader(void)
{
    struct AgentJsonDoc doc;
    const struct AgentJsonValue* value;
    char error[256];

    CHECK(parse(&doc, " { \"id\" : 7 , \"verb\":\"ui\", \"visible_only\":true, \"panel\":null,"
                      " \"from\":[10, -20], \"mods\":[\"shift\",\"ctrl\"], \"empty\":[] } ",
              error, sizeof(error)),
        "a request parses: %s", error);
    CHECK(doc.member_count == 7, "seven members, got %d", doc.member_count);
    value = AgentJson_Get(&doc, "id");
    CHECK(value && value->type == AGENT_JSON_NUMBER && value->number == 7.0, "id is the number 7");
    value = AgentJson_Get(&doc, "verb");
    CHECK(value && value->type == AGENT_JSON_STRING && strcmp(value->string, "ui") == 0, "verb is \"ui\"");
    value = AgentJson_Get(&doc, "visible_only");
    CHECK(value && value->type == AGENT_JSON_BOOL && value->boolean == 1, "visible_only is true");
    value = AgentJson_Get(&doc, "panel");
    CHECK(value && value->type == AGENT_JSON_NULL, "panel is null");
    value = AgentJson_Get(&doc, "from");
    CHECK(value && value->type == AGENT_JSON_ARRAY && value->count == 2, "from has two elements");
    CHECK(AgentJson_At(&doc, value, 0)->number == 10.0 && AgentJson_At(&doc, value, 1)->number == -20.0,
        "from is [10, -20]");
    value = AgentJson_Get(&doc, "mods");
    CHECK(value && value->count == 2 && strcmp(AgentJson_At(&doc, value, 1)->string, "ctrl") == 0,
        "mods' second element is \"ctrl\"");
    value = AgentJson_Get(&doc, "empty");
    CHECK(value && value->type == AGENT_JSON_ARRAY && value->count == 0, "an empty array has no elements");
    CHECK(AgentJson_Get(&doc, "absent") == NULL, "a member that is not there is NULL");
    AgentJson_Free(&doc);

    CHECK(parse(&doc, "{}", error, sizeof(error)) && doc.member_count == 0, "an empty object parses");
    AgentJson_Free(&doc);

    /* Escapes: the two-character ones, \u in the BMP, and a surrogate pair. */
    CHECK(parse(&doc, "{\"text\":\"a\\\"b\\\\c\\/d\\ne\\tf \\u00e9 \\u2318 \\ud83d\\ude00\"}", error, sizeof(error)),
        "escapes parse: %s", error);
    value = AgentJson_Get(&doc, "text");
    CHECK(value && strcmp(value->string, "a\"b\\c/d\ne\tf \xC3\xA9 \xE2\x8C\x98 \xF0\x9F\x98\x80") == 0,
        "escapes decode to UTF-8, got %s", value ? value->string : "(none)");
    AgentJson_Free(&doc);

    /* Numbers as JSON spells them. */
    CHECK(parse(&doc, "{\"a\":-0.5,\"b\":1e3,\"c\":12}", error, sizeof(error)), "numbers parse: %s", error);
    CHECK(AgentJson_Get(&doc, "a")->number == -0.5 && AgentJson_Get(&doc, "b")->number == 1000.0
              && AgentJson_Get(&doc, "c")->number == 12.0,
        "the numbers are -0.5, 1000 and 12");
    AgentJson_Free(&doc);

    /* Every refusal is a sentence, and leaves a document that can be freed. */
    {
        static const char* const k_bad[] = {
            "",
            "[1, 2]",
            "{\"verb\":\"ui\"",
            "{\"verb\" \"ui\"}",
            "{\"verb\":\"ui\",}",
            "{\"verb\":\"ui\"} trailing",
            "{\"args\":{\"x\":1}}",
            "{\"from\":[[1,2]]}",
            "{\"text\":\"unclosed}",
            "{\"text\":\"bad \\q escape\"}",
            "{\"text\":\"half \\ud83d pair\"}",
            "{\"x\":12abc}",
            "{\"x\":tru}",
            "{\"id\":7,\"verb\":\"action\",\"id\":\"file.save_all\"}",
            "{verb:\"ui\"}",
        };
        for( size_t i = 0; i < sizeof(k_bad) / sizeof(k_bad[0]); i++ )
        {
            error[0] = '\0';
            CHECK(!parse(&doc, k_bad[i], error, sizeof(error)), "'%s' is refused", k_bad[i]);
            CHECK(error[0] != '\0', "'%s' is refused with a sentence", k_bad[i]);
            AgentJson_Free(&doc);
        }
    }

    /* More members, and more array elements, than a request may carry. */
    {
        char many[2048];
        size_t used = 0;

        used += (size_t)snprintf(many + used, sizeof(many) - used, "{");
        for( int i = 0; i <= AGENT_JSON_MEMBERS_MAX; i++ )
            used += (size_t)snprintf(many + used, sizeof(many) - used, "%s\"k%d\":%d", i ? "," : "", i, i);
        snprintf(many + used, sizeof(many) - used, "}");
        CHECK(!parse(&doc, many, error, sizeof(error)), "%d members are refused", AGENT_JSON_MEMBERS_MAX + 1);
        AgentJson_Free(&doc);

        used = (size_t)snprintf(many, sizeof(many), "{\"a\":[");
        for( int i = 0; i <= AGENT_JSON_ELEMENTS_MAX; i++ )
            used += (size_t)snprintf(many + used, sizeof(many) - used, "%s%d", i ? "," : "", i);
        snprintf(many + used, sizeof(many) - used, "]}");
        CHECK(!parse(&doc, many, error, sizeof(error)), "%d array elements are refused", AGENT_JSON_ELEMENTS_MAX + 1);
        AgentJson_Free(&doc);
    }
}

static void
test_buffer(void)
{
    struct AgentBuf buf;

    AgentBuf_Init(&buf);
    for( int i = 0; i < 1000; i++ )
        AgentBuf_Append(&buf, "0123456789", 10);
    CHECK(buf.length == 10000, "ten thousand bytes appended, got %u", (unsigned)buf.length);
    AgentBuf_Consume(&buf, 9995);
    CHECK(buf.length == 5 && memcmp(buf.data, "56789", 5) == 0, "consuming keeps the tail");
    AgentBuf_Consume(&buf, 5);
    CHECK(buf.length == 0, "consuming everything leaves it empty");
    AgentBuf_Free(&buf);
    CHECK(buf.data == NULL && buf.capacity == 0, "a freed buffer holds nothing");
}

int
main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    ToriMem_Init();
    test_buffer();
    test_writer();
    test_reader();
    printf("agent_json_test: %d checks, %d failed\n", g_checks, g_failed);
    ToriMem_Shutdown();
    return g_failed ? 1 : 0;
}
