#include "ini.h"

#include <string.h>

int
ini_reader_init(struct INIReader* reader)
{
    memset(reader, 0, sizeof(struct INIReader));
    reader->state = INI_READER_STATE_INITIAL;
    return 0;
}

static inline void
skip_whitespace_and_comments(
    struct INIReader* reader,
    uint8_t* data,
    uint32_t data_size)
{
    uint8_t* p = data + reader->offset;
    while( reader->offset < data_size )
    {
        // Skip comments and whitespace
        if( *p == ';' || *p == '#' )
        {
            // Skip to end of line
            while( reader->offset < data_size && *p != '\n' )
            {
                reader->offset++;
                p++;
            }
        }
        else if( *p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' )
        {
            reader->offset++;
            p++;
        }
        else
            break;
    }
}

int
ini_reader_next(
    struct INIReader* reader,
    uint8_t* data,
    uint32_t data_size,
    struct INIElement* element)
{
    memset(element, 0, sizeof(struct INIElement));

    skip_whitespace_and_comments(reader, data, data_size);

    uint8_t* p = data + reader->offset;

continue_reading:;
    switch( reader->state )
    {
    case INI_READER_STATE_INITIAL:
    {
        if( reader->offset >= data_size )
            goto done;

        if( *p == '[' )
        {
            reader->state = INI_READER_STATE_SECTION;
            goto continue_reading;
        }
        else
        {
            reader->state = INI_READER_STATE_ERROR;
            return TORI_INI_ERR_PARSE_ERROR; // Expected section header
        }
    }
    break;
    case INI_READER_STATE_NEXTLINE:
    {
        if( reader->offset >= data_size )
            goto done;

        if( *p == '[' )
        {
            reader->state = INI_READER_STATE_SECTION;

            // Emit a SECTION_END element to flush the previous section
            element->kind = INI_ELEMENT_SECTION_END;
            return TORI_INI_ERR_OK; // Flush previous section
        }
        else if( reader->offset < data_size )
        {
            reader->state = INI_READER_STATE_KEYVAL;
            goto continue_reading;
        }
        else
            goto done;
    }
    break;
    case INI_READER_STATE_SECTION:
    {
        if( reader->offset >= data_size )
        {
            reader->state = INI_READER_STATE_ERROR;
            return TORI_INI_ERR_PARSE_ERROR; // Expected section header
        }

        if( *p != '[' )
        {
            reader->state = INI_READER_STATE_ERROR;
            return TORI_INI_ERR_PARSE_ERROR; // Expected section header
        }

        p++;
        reader->offset++;
        char* name_start = (char*)p;
        while( reader->offset < data_size && *p != ']' && *p != '\n' && *p != '\r' )
        {
            p++;
            reader->offset++;
        }
        if( reader->offset >= data_size || *p != ']' )
        {
            reader->state = INI_READER_STATE_ERROR;
            return TORI_INI_ERR_PARSE_ERROR; // Unterminated section header
        }

        uint32_t name_len = (uint32_t)(p - (uint8_t*)name_start);
        if( name_len >= sizeof(element->_section.name) )
            return TORI_INI_ERR_SECTION_NAME_TOO_LONG; // Section name too long

        memcpy(element->_section.name, name_start, name_len);
        element->_section.name[name_len] = '\0';
        element->kind = INI_ELEMENT_SECTION;
        p++;
        reader->offset++;

        reader->state = INI_READER_STATE_NEXTLINE;

        return TORI_INI_ERR_OK;
    }
    break;
    case INI_READER_STATE_KEYVAL:
    {
        if( reader->offset >= data_size )
        {
            reader->state = INI_READER_STATE_ERROR;
            return TORI_INI_ERR_PARSE_ERROR; // Expected section header
        }

        // Key-value pair
        char* key_start = (char*)p;
        while( reader->offset < data_size && *p != '=' && *p != '\n' && *p != '\r' )
        {
            p++;
            reader->offset++;
        }
        if( reader->offset >= data_size || *p != '=' )
        {
            reader->state = INI_READER_STATE_ERROR;
            return TORI_INI_ERR_PARSE_ERROR; // Invalid key-value pair
        }

        uint32_t key_len = (uint32_t)(p - (uint8_t*)key_start);
        if( key_len >= sizeof(element->_keyval.name) )
            return TORI_INI_ERR_KEY_NAME_TOO_LONG; // Key too long
        memcpy(element->_keyval.name, key_start, key_len);
        element->_keyval.name[key_len] = '\0';
        p++;
        reader->offset++;

        char* value_start = (char*)p;
        while( reader->offset < data_size && *p != '\n' && *p != '\r' )
        {
            p++;
            reader->offset++;
        }

        uint32_t value_len = (uint32_t)(p - (uint8_t*)value_start);
        if( value_len >= sizeof(element->_keyval.value) )
            return TORI_INI_ERR_KEY_VALUE_TOO_LONG; // Value too long
        memcpy(element->_keyval.value, value_start, value_len);
        element->_keyval.value[value_len] = '\0';
        element->kind = INI_ELEMENT_KEYVAL;

        reader->state = INI_READER_STATE_NEXTLINE;

        return TORI_INI_ERR_OK;
    }
    break;
    default:
        reader->state = INI_READER_STATE_ERROR;
        return TORI_INI_ERR_PARSE_ERROR; // Invalid state
    }

    if( reader->offset >= data_size )
    {
    done:;
        reader->state = INI_READER_STATE_DONE;
        element->kind = INI_ELEMENT_SECTION_END;
        return TORI_INI_ERR_NONE; // End of file, flush last section
    }

    return TORI_INI_ERR_NONE;
}