#ifndef TORI_INI_H
#define TORI_INI_H

#include <stdbool.h>
#include <stdint.h>

#define TORI_INI_ERR_OK 1
#define TORI_INI_ERR_NONE 0
#define TORI_INI_ERR_KEY_NAME_TOO_LONG -1
#define TORI_INI_ERR_KEY_VALUE_TOO_LONG -2
#define TORI_INI_ERR_SECTION_NAME_TOO_LONG -3
#define TORI_INI_ERR_PARSE_ERROR -4

enum INIReaderStateKind
{
    INI_READER_STATE_ERROR,
    INI_READER_STATE_INITIAL,
    INI_READER_STATE_DONE,
    INI_READER_STATE_SECTION,
    INI_READER_STATE_KEYVAL,
    INI_READER_STATE_NEXTLINE
};

struct INIReader
{
    int offset;
    enum INIReaderStateKind state;
};

enum INIElementKind
{
    INI_ELEMENT_UNDEFINED,
    INI_ELEMENT_SECTION,
    // Emitted at the start of a new section or end of file to flush the previous section
    INI_ELEMENT_SECTION_END,
    INI_ELEMENT_KEYVAL
};

struct INIElement
{
    enum INIElementKind kind;

    union
    {
        struct
        {
            char name[64];
        } _section;

        struct
        {
            char name[24];
            char value[128];
        } _keyval;
    };
};

int
ini_reader_init(struct INIReader* reader);

int
ini_reader_next(
    struct INIReader* reader,
    uint8_t* data,
    uint32_t data_size,
    struct INIElement* element);

#endif