#include "rs_clientcode.h"

#include "app.h"
#include "rs_social.h"
#include "ui/uitree.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Set a node's text in place (ownership matches UITree_ApplyText: heap copy).
 * Compares first so a per-tick pass does not churn allocations or force
 * redraws for unchanged rows. Returns nonzero when the text changed. */
static int
set_node_text(
    struct UITree* tree,
    int32_t idx,
    char const* text)
{
    struct UITreeComponent* c = &tree->components[idx];
    char const* current;

    if( c->type != UIELEM_RS_TEXT )
        return 0;
    current = c->u.rs_text.text ? c->u.rs_text.text : "";
    if( strcmp(current, text) == 0 )
        return 0;

    {
        char* copy = strdup(text);
        if( !copy )
            return 0;
        free((void*)c->u.rs_text.text);
        c->u.rs_text.text = copy;
    }
    UITree_MarkNodeDirty(tree, idx);
    return 1;
}

static int
set_button_type(
    struct UITreeComponent* c,
    int button_type)
{
    if( c->behavior.button_type == button_type )
        return 0;
    c->behavior.button_type = button_type;
    return 1;
}

/* List scroll height = rows * 15 + 20, floored to box height + 1 (reference
 * CC_FRIENDS_SIZE / CC_IGNORES_SIZE). */
static int
set_list_scroll_height(
    struct UITreeComponent* c,
    int count)
{
    int scroll_height = count * 15 + 20;
    int box_h = c->position.height;
    if( scroll_height <= box_h )
        scroll_height = box_h + 1;
    if( c->type != UIELEM_RS_LAYER || c->u.rs_layer.scroll_height == scroll_height )
        return 0;
    c->u.rs_layer.scroll_height = scroll_height;
    return 1;
}

static int
friends_row_tick(
    struct UITree* tree,
    int32_t idx,
    struct RS_Social const* social,
    int client_code)
{
    struct UITreeComponent* c = &tree->components[idx];
    int changed = 0;
    int count = social->server_status == RS_SOCIAL_SERVER_CONNECTED ? social->friend_count : 0;

    if( client_code == RS_CC_FRIENDS_START && social->server_status == RS_SOCIAL_SERVER_LOADING )
    {
        changed |= set_node_text(tree, idx, "Loading friend list");
        changed |= set_button_type(c, 0);
        return changed;
    }
    if( client_code == RS_CC_FRIENDS_START &&
        social->server_status == RS_SOCIAL_SERVER_CONNECTING )
    {
        changed |= set_node_text(tree, idx, "Connecting to friendserver");
        changed |= set_button_type(c, 0);
        return changed;
    }

    {
        int row = client_code > 700 ? client_code - RS_CC_FRIENDS2_START
                                    : client_code - RS_CC_FRIENDS_START;
        if( row >= count )
        {
            changed |= set_node_text(tree, idx, "");
            changed |= set_button_type(c, 0);
        }
        else
        {
            changed |= set_node_text(tree, idx, social->friend_name[row]);
            changed |= set_button_type(c, 1);
        }
    }
    return changed;
}

static int
friends_world_row_tick(
    struct UITree* tree,
    int32_t idx,
    struct RS_Social const* social,
    int client_code)
{
    struct UITreeComponent* c = &tree->components[idx];
    int changed = 0;
    int count = social->server_status == RS_SOCIAL_SERVER_CONNECTED ? social->friend_count : 0;
    int row = client_code > 800 ? client_code - RS_CC_FRIENDS2_UPDATE_START
                                : client_code - RS_CC_FRIENDS_UPDATE_START;

    if( row >= count )
    {
        changed |= set_node_text(tree, idx, "");
        changed |= set_button_type(c, 0);
        return changed;
    }

    {
        char text[32];
        int world = social->friend_world[row];
        if( world == 0 )
            snprintf(text, sizeof(text), "@red@Offline");
        else if( world == social->node_id )
            snprintf(text, sizeof(text), "@gre@World-%d", world);
        else
            snprintf(text, sizeof(text), "@yel@World-%d", world);
        changed |= set_node_text(tree, idx, text);
    }
    changed |= set_button_type(c, 1);
    return changed;
}

static int
ignores_row_tick(
    struct UITree* tree,
    int32_t idx,
    struct RS_Social const* social,
    int client_code)
{
    struct UITreeComponent* c = &tree->components[idx];
    int changed = 0;
    int row = client_code - RS_CC_IGNORES_START;

    if( row >= social->ignore_count )
    {
        changed |= set_node_text(tree, idx, "");
        changed |= set_button_type(c, 0);
    }
    else
    {
        changed |= set_node_text(tree, idx, social->ignore_name[row]);
        changed |= set_button_type(c, 1);
    }
    return changed;
}

int
RS_ClientCode_Tick(
    struct UITree* tree,
    struct RS_Social const* social,
    uint64_t loop_cycle)
{
    int changed = 0;

    assert(tree);
    assert(social);

    for( uint32_t i = 0; i < tree->component_count; i++ )
    {
        struct UITreeComponent* c = &tree->components[i];
        int cc;
        if( c->freed )
            continue;
        cc = c->behavior.client_code;
        if( cc <= 0 )
            continue;

        if( (cc >= RS_CC_FRIENDS_START && cc <= RS_CC_FRIENDS_END) ||
            (cc >= RS_CC_FRIENDS2_START && cc <= RS_CC_FRIENDS2_END) )
            changed |= friends_row_tick(tree, (int32_t)i, social, cc);
        else if(
            (cc >= RS_CC_FRIENDS_UPDATE_START && cc <= RS_CC_FRIENDS_UPDATE_END) ||
            (cc >= RS_CC_FRIENDS2_UPDATE_START && cc <= RS_CC_FRIENDS2_UPDATE_END) )
            changed |= friends_world_row_tick(tree, (int32_t)i, social, cc);
        else if( cc == RS_CC_FRIENDS_SIZE )
            changed |= set_list_scroll_height(
                c,
                social->server_status == RS_SOCIAL_SERVER_CONNECTED ? social->friend_count : 0);
        else if( cc >= RS_CC_IGNORES_START && cc <= RS_CC_IGNORES_END )
            changed |= ignores_row_tick(tree, (int32_t)i, social, cc);
        else if( cc == RS_CC_IGNORES_SIZE )
            changed |= set_list_scroll_height(c, social->ignore_count);
        else if( cc == RS_CC_DESIGN_PREVIEW && c->type == UIELEM_RS_MODEL )
        {
            /* Reference: modelXAn=150, modelYAn=sin(loop/40)*256 wrapped. */
            int yan = ((int)(sin((double)loop_cycle / 40.0) * 256.0)) & 0x7ff;
            c->u.rs_model.xan = 150;
            if( c->u.rs_model.yan != yan )
            {
                c->u.rs_model.yan = yan;
                UITree_MarkNodeDirty(tree, (int32_t)i);
                changed = 1;
            }
        }
        else if( cc == RS_CC_LAST_LOGIN_INFO || cc == RS_CC_LAST_LOGIN_INFO2 )
            changed |= set_node_text(
                tree, (int32_t)i, "You last logged in @yel@earlier today@whi@.");
        else if( cc == RS_CC_UNREAD_MESSAGES )
            changed |= set_node_text(tree, (int32_t)i, "0 unread messages");
    }

    return changed;
}

int
RS_ClientCode_Button(
    struct App* app,
    struct UITreeComponent const* component)
{
    int cc;

    assert(app);
    assert(component);
    cc = component->behavior.client_code;

    switch( cc )
    {
    case RS_CC_LOGOUT:
        /* Reference sets logoutTimer=250 and notifies the server. */
        fprintf(stderr, "clientcode: logout requested\n");
        return 1;
    case RS_CC_ACCEPT_DESIGN:
        return 1;
    case RS_CC_ADD_FRIEND:
    case RS_CC_DEL_FRIEND:
    case RS_CC_ADD_IGNORE:
    case RS_CC_DEL_IGNORE:
    {
        /* Open the chat social-input line (reference clientButton). */
        static struct
        {
            int cc;
            int type;
            char const* header;
        } const k_social[] = {
            { RS_CC_ADD_FRIEND,
              RS_CHAT_SOCIAL_ADD_FRIEND,
              "Enter name of friend to add to list" },
            { RS_CC_DEL_FRIEND,
              RS_CHAT_SOCIAL_DEL_FRIEND,
              "Enter name of friend to delete from list" },
            { RS_CC_ADD_IGNORE,
              RS_CHAT_SOCIAL_ADD_IGNORE,
              "Enter name of player to add to list" },
            { RS_CC_DEL_IGNORE,
              RS_CHAT_SOCIAL_DEL_IGNORE,
              "Enter name of player to delete from list" },
        };
        for( int i = 0; i < 4; i++ )
        {
            if( k_social[i].cc != cc )
                continue;
            app->chat.dialog_input_open = 0;
            app->chat.social_input_open = 1;
            app->chat.social_input_type = k_social[i].type;
            app->chat.social_input[0] = '\0';
            strncpy(
                app->chat.social_header,
                k_social[i].header,
                sizeof(app->chat.social_header) - 1);
            app->need_redraw = 1;
            break;
        }
        return 0;
    }
    default:
        if( cc >= RS_CC_CHANGE_HEAD_L && cc <= RS_CC_SWITCH_TO_FEMALE )
        {
            /* Player-design part/colour cycling needs the idk design store. */
            fprintf(stderr, "clientcode: design button %d (not implemented)\n", cc);
            return 0;
        }
        return 0;
    }
}
