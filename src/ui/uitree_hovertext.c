#include "uitree_hovertext.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

void
UIHoverText_Reset(struct UIHoverText* hover)
{
    assert(hover);
    memset(hover, 0, sizeof(*hover));
    hover->font_id = -1;
    hover->h = UITREE_HOVERTEXT_BOX_H;
}

bool
UIHoverText_Compose(
    struct UIMinimenu const* menu,
    struct UIHoverText* out)
{
    assert(menu);
    assert(out);

    /* Every menu the builder produces starts with Cancel, and the priority
     * bubble sinks the >1000 rows (Cancel, Examine) toward index 0, so the
     * acting row is always the last one and `option_count - 1` is exactly the
     * reference's minimenu_numops (which excludes Cancel). A Cancel-only menu
     * means numops == 0: 4726 draws nothing. */
    int const num_ops = menu->option_count - 1;
    if( num_ops <= 0 )
    {
        out->visible = false;
        out->text[0] = '\0';
        return false;
    }

    {
        char const* const top = menu->options[menu->option_count - 1].text;
        if( num_ops > 1 )
            snprintf(
                out->text,
                sizeof(out->text),
                "%s</col> / %d more options",
                top,
                num_ops - 1);
        else
            snprintf(out->text, sizeof(out->text), "%s", top);
    }

    out->visible = true;
    return true;
}
