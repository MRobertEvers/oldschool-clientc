

#include "ini.c"

#include <stdio.h>

char const* test_no_newline_eof = //
    "[Section2]\n"
    "key5=value5";

char const* test_no_kv_newline_eof = //
    "[Section2]";

char const* test_newline_empty = //
    "[Section2]\n";

char const* test_multisection = //
    "[Section1]\n"
    "key1=value1\n"
    "[Section2]\n"
    "key2=value2\n"
    "\n";

char const* test_multi_comments = //
    "\n"
    "; Comment for section 1\n"
    "[Section1]\n"
    "key1=value1\n"
    "; Comment for section 2\n"
    "[Section2]\n"
    "key2=value2\n"
    "\n";

char const* test_invalid_section_initial = //
    "key1=value1\n"
    "[Section1]\n";

char const* test_invalid_kv = //
    "[Section1]\n"
    "invalidkeyval\n";

char const* test_unterminated_section = //
    "[Section1\n"
    "key1=value1\n";

int
run_test_no_newline_eof()
{
    struct INIReader reader;
    ini_reader_init(&reader);
    struct INIElement element;
    int err;

    err = ini_reader_next(
        &reader, (uint8_t*)test_no_newline_eof, strlen(test_no_newline_eof), &element);
    if( err != TORI_INI_ERR_OK || element.kind != INI_ELEMENT_SECTION ||
        strcmp(element._section.name, "Section2") != 0 )
    {
        printf(
            "Test failed: expected section element for Section2, got err=%d kind=%d name=%s\n",
            err,
            element.kind,
            element._section.name);
        return 1;
    }

    err = ini_reader_next(
        &reader, (uint8_t*)test_no_newline_eof, strlen(test_no_newline_eof), &element);
    if( err != TORI_INI_ERR_OK || element.kind != INI_ELEMENT_KEYVAL ||
        strcmp(element._keyval.name, "key5") != 0 || strcmp(element._keyval.value, "value5") != 0 )
    {
        printf(
            "Test failed: expected keyval element for key5=value5, got err=%d kind=%d name=%s "
            "value=%s\n",
            err,
            element.kind,
            element._keyval.name,
            element._keyval.value);
        return 1;
    }

    err = ini_reader_next(
        &reader, (uint8_t*)test_no_newline_eof, strlen(test_no_newline_eof), &element);
    if( err != TORI_INI_ERR_OK || element.kind != INI_ELEMENT_SECTION_END )
    {
        printf(
            "Test failed: expected section end element, got err=%d kind=%d\n", err, element.kind);
        return 1;
    }

    if( reader.state != INI_READER_STATE_DONE )
    {
        printf("Test failed: expected reader state DONE, got %d\n", reader.state);
        return 1;
    }

    return 0;
}

int
run_test_no_kv_newline_eof()
{
    struct INIReader reader;
    ini_reader_init(&reader);
    struct INIElement element;
    int err;

    err = ini_reader_next(
        &reader, (uint8_t*)test_no_kv_newline_eof, strlen(test_no_kv_newline_eof), &element);
    if( err != TORI_INI_ERR_OK || element.kind != INI_ELEMENT_SECTION ||
        strcmp(element._section.name, "Section2") != 0 )
    {
        printf(
            "Test failed: expected section element for Section2, got err=%d kind=%d name=%s\n",
            err,
            element.kind,
            element._section.name);
        return 1;
    }

    err = ini_reader_next(
        &reader, (uint8_t*)test_no_kv_newline_eof, strlen(test_no_kv_newline_eof), &element);
    if( err != TORI_INI_ERR_OK || element.kind != INI_ELEMENT_SECTION_END )
    {
        printf(
            "Test failed: expected section end element, got err=%d kind=%d\n", err, element.kind);
        return 1;
    }

    if( reader.state != INI_READER_STATE_DONE )
    {
        printf("Test failed: expected reader state DONE, got %d\n", reader.state);
        return 1;
    }

    return 0;
}

int
run_test_newline_empty()
{
    struct INIReader reader;
    ini_reader_init(&reader);
    struct INIElement element;
    int err;

    err = ini_reader_next(
        &reader, (uint8_t*)test_newline_empty, strlen(test_newline_empty), &element);
    if( err != TORI_INI_ERR_OK || element.kind != INI_ELEMENT_SECTION ||
        strcmp(element._section.name, "Section2") != 0 )
    {
        printf(
            "Test failed: expected section element for Section2, got err=%d kind=%d name=%s\n",
            err,
            element.kind,
            element._section.name);
        return 1;
    }

    err = ini_reader_next(
        &reader, (uint8_t*)test_newline_empty, strlen(test_newline_empty), &element);
    if( err != TORI_INI_ERR_OK || element.kind != INI_ELEMENT_SECTION_END )
    {
        printf(
            "Test failed: expected section end element, got err=%d kind=%d\n", err, element.kind);
        return 1;
    }

    if( reader.state != INI_READER_STATE_DONE )
    {
        printf("Test failed: expected reader state DONE, got %d\n", reader.state);
        return 1;
    }

    return 0;
}

int
run_test_multisection()
{
    struct INIReader reader;
    ini_reader_init(&reader);
    struct INIElement element;
    int err;

    err =
        ini_reader_next(&reader, (uint8_t*)test_multisection, strlen(test_multisection), &element);
    if( err != TORI_INI_ERR_OK || element.kind != INI_ELEMENT_SECTION ||
        strcmp(element._section.name, "Section1") != 0 )
    {
        printf(
            "Test failed: expected section element for Section1, got err=%d kind=%d name=%s\n",
            err,
            element.kind,
            element._section.name);
        return 1;
    }

    err =
        ini_reader_next(&reader, (uint8_t*)test_multisection, strlen(test_multisection), &element);
    if( err != TORI_INI_ERR_OK || element.kind != INI_ELEMENT_KEYVAL ||
        strcmp(element._keyval.name, "key1") != 0 || strcmp(element._keyval.value, "value1") != 0 )
    {
        printf(
            "Test failed: expected keyval element for key1=value1, got err=%d kind=%d name=%s "
            "value=%s\n",
            err,
            element.kind,
            element._keyval.name,
            element._keyval.value);
        return 1;
    }

    err =
        ini_reader_next(&reader, (uint8_t*)test_multisection, strlen(test_multisection), &element);
    if( err != TORI_INI_ERR_OK || element.kind != INI_ELEMENT_SECTION_END )
    {
        printf(
            "Test failed: expected section end element, got err=%d kind=%d\n", err, element.kind);
        return 1;
    }

    err =
        ini_reader_next(&reader, (uint8_t*)test_multisection, strlen(test_multisection), &element);
    if( err != TORI_INI_ERR_OK || element.kind != INI_ELEMENT_SECTION ||
        strcmp(element._section.name, "Section2") != 0 )
    {
        printf(
            "Test failed: expected section element for Section2, got err=%d kind=%d name=%s\n",
            err,
            element.kind,
            element._section.name);
        return 1;
    }

    err =
        ini_reader_next(&reader, (uint8_t*)test_multisection, strlen(test_multisection), &element);
    if( err != TORI_INI_ERR_OK || element.kind != INI_ELEMENT_KEYVAL ||
        strcmp(element._keyval.name, "key2") != 0 || strcmp(element._keyval.value, "value2") != 0 )
    {
        printf(
            "Test failed: expected keyval element for key2=value2, got err=%d kind=%d name=%s "
            "value=%s\n",
            err,
            element.kind,
            element._keyval.name,
            element._keyval.value);
        return 1;
    }

    err =
        ini_reader_next(&reader, (uint8_t*)test_multisection, strlen(test_multisection), &element);
    if( err != TORI_INI_ERR_OK || element.kind != INI_ELEMENT_SECTION_END )
    {
        printf(
            "Test failed: expected section end element, got err=%d kind=%d\n", err, element.kind);
        return 1;
    }

    if( reader.state != INI_READER_STATE_DONE )
    {
        printf("Test failed: expected reader state DONE, got %d\n", reader.state);
        return 1;
    }

    return 0;
}

int
run_test_multi_comments()
{
    struct INIReader reader;
    ini_reader_init(&reader);
    struct INIElement element;
    int err;

    err = ini_reader_next(
        &reader, (uint8_t*)test_multi_comments, strlen(test_multi_comments), &element);
    if( err != TORI_INI_ERR_OK || element.kind != INI_ELEMENT_SECTION ||
        strcmp(element._section.name, "Section1") != 0 )
    {
        printf(
            "Test failed: expected section element for Section1, got err=%d state=%d kind=%d "
            "name=%s\n",
            err,
            reader.state,
            element.kind,
            element._section.name);
        return 1;
    }

    err = ini_reader_next(
        &reader, (uint8_t*)test_multi_comments, strlen(test_multi_comments), &element);
    if( err != TORI_INI_ERR_OK || element.kind != INI_ELEMENT_KEYVAL ||
        strcmp(element._keyval.name, "key1") != 0 || strcmp(element._keyval.value, "value1") != 0 )
    {
        printf(
            "Test failed: expected keyval element for key1=value1, got err=%d kind=%d name=%s "
            "value=%s\n",
            err,
            element.kind,
            element._keyval.name,
            element._keyval.value);
        return 1;
    }

    err = ini_reader_next(
        &reader, (uint8_t*)test_multi_comments, strlen(test_multi_comments), &element);
    if( err != TORI_INI_ERR_OK || element.kind != INI_ELEMENT_SECTION_END )
    {
        printf(
            "Test failed: expected section end element, got err=%d kind=%d\n", err, element.kind);
        return 1;
    }

    err = ini_reader_next(
        &reader, (uint8_t*)test_multi_comments, strlen(test_multi_comments), &element);
    if( err != TORI_INI_ERR_OK || element.kind != INI_ELEMENT_SECTION ||
        strcmp(element._section.name, "Section2") != 0 )
    {
        printf(
            "Test failed: expected section element for Section2, got err=%d kind=%d name=%s\n",
            err,
            element.kind,
            element._section.name);
        return 1;
    }

    err = ini_reader_next(
        &reader, (uint8_t*)test_multi_comments, strlen(test_multi_comments), &element);
    if( err != TORI_INI_ERR_OK || element.kind != INI_ELEMENT_KEYVAL ||
        strcmp(element._keyval.name, "key2") != 0 || strcmp(element._keyval.value, "value2") != 0 )
    {
        printf(
            "Test failed: expected keyval element for key2=value2, got err=%d kind=%d name=%s "
            "value=%s\n",
            err,
            element.kind,
            element._keyval.name,
            element._keyval.value);
        return 1;
    }

    err = ini_reader_next(
        &reader, (uint8_t*)test_multi_comments, strlen(test_multi_comments), &element);
    if( err != TORI_INI_ERR_OK || element.kind != INI_ELEMENT_SECTION_END )
    {
        printf(
            "Test failed: expected section end element, got err=%d kind=%d\n", err, element.kind);
        return 1;
    }

    if( reader.state != INI_READER_STATE_DONE )
    {
        printf("Test failed: expected reader state DONE, got %d\n", reader.state);
        return 1;
    }

    return 0;
}

int
run_test_invalid_section_initial()
{
    struct INIReader reader;
    ini_reader_init(&reader);
    struct INIElement element;
    int err;

    err = ini_reader_next(
        &reader,
        (uint8_t*)test_invalid_section_initial,
        strlen(test_invalid_section_initial),
        &element);
    if( err != TORI_INI_ERR_PARSE_ERROR || reader.state != INI_READER_STATE_ERROR )
    {
        printf(
            "Test failed: expected parse error for invalid section header at initial state, got "
            "err=%d state=%d\n",
            err,
            reader.state);
        return 1;
    }

    return 0;
}

int
run_test_invalid_kv()
{
    struct INIReader reader;
    ini_reader_init(&reader);
    struct INIElement element;
    int err;

    err = ini_reader_next(&reader, (uint8_t*)test_invalid_kv, strlen(test_invalid_kv), &element);
    if( err != TORI_INI_ERR_OK || element.kind != INI_ELEMENT_SECTION ||
        strcmp(element._section.name, "Section1") != 0 )
    {
        printf(
            "Test failed: expected section element for Section1, got err=%d kind=%d name=%s\n",
            err,
            element.kind,
            element._section.name);
        return 1;
    }

    err = ini_reader_next(&reader, (uint8_t*)test_invalid_kv, strlen(test_invalid_kv), &element);
    if( err != TORI_INI_ERR_PARSE_ERROR || reader.state != INI_READER_STATE_ERROR )
    {
        printf(
            "Test failed: expected parse error for invalid key-value pair, got err=%d state=%d\n",
            err,
            reader.state);
        return 1;
    }

    return 0;
}

int
run_test_unterminated_section()
{
    struct INIReader reader;
    ini_reader_init(&reader);
    struct INIElement element;
    int err;

    err = ini_reader_next(
        &reader, (uint8_t*)test_unterminated_section, strlen(test_unterminated_section), &element);
    if( err != TORI_INI_ERR_PARSE_ERROR || reader.state != INI_READER_STATE_ERROR )
    {
        printf(
            "Test failed: expected parse error for unterminated section header, got err=%d "
            "state=%d\n",
            err,
            reader.state);
        return 1;
    }

    return 0;
}

int
main()
{
    struct INIReader reader;
    ini_reader_init(&reader);

    struct INIElement element;
    int err;

    err = run_test_no_newline_eof();
    if( err != 0 )
        return 1;

    err = run_test_no_kv_newline_eof();
    if( err != 0 )
        return 1;

    err = run_test_newline_empty();
    if( err != 0 )
        return 1;

    err = run_test_multisection();
    if( err != 0 )
        return 1;

    err = run_test_multi_comments();
    if( err != 0 )
        return 1;

    err = run_test_invalid_section_initial();
    if( err != 0 )
        return 1;

    err = run_test_invalid_kv();
    if( err != 0 )
        return 1;

    err = run_test_unterminated_section();
    if( err != 0 )
        return 1;

    return 0;
}