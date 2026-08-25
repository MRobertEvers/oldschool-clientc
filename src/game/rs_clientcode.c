#include "rs_clientcode.h"

#include "app.h"
#include "engine/cache_provider.h"
#include "engine/uitree_scene_bridge.h"
#include "rs_idk_design.h"
#include "rs_social.h"
#include "ui/uitree.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "log/torirs_log.h"

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

    return UITree_SetTextAt(tree, idx, text) ? 1 : 0;
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
    struct UITree* tree,
    int32_t idx,
    int count)
{
    struct UITreeComponent const* c = &tree->components[idx];
    int scroll_height = count * 15 + 20;
    int box_h = c->position.height;
    if( scroll_height <= box_h )
        scroll_height = box_h + 1;
    if( c->type != UIELEM_RS_LAYER || c->u.rs_layer.scroll_height == scroll_height )
        return 0;
    return UITree_SetScrollSizeAt(
               tree, idx, c->u.rs_layer.scroll_width, scroll_height)
               ? 1
               : 0;
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

/*
 * Reference CC_DESIGN_PREVIEW's idkDesignRedraw block: rebuild the composite
 * from the current kits/colours/gender. The reference first calls
 * IdkType.checkModel() on all seven kits and returns (retrying next frame) if
 * any model is still loading; here that gate is "every referenced model is
 * resident", with the missing ones queued on the async pipeline.
 *
 * Returns nonzero once the composite has been rebuilt.
 */
static int
design_preview_rebuild(struct App* app)
{
    struct RS_IdkDesign* design = &app->idk_design;
    int model_ids[RS_IDK_DESIGN_LOAD_TRACK_MAX];
    int model_count;
    int missing = 0;

    if( !app->provider )
        return 0;

    /* The kit table is only walkable once the idk configs are resident, and
     * the preview mount is what prefetches them (PackAssetsLoad needs_player). */
    if( !RS_IdkDesign_EnsureResolved(design, app->provider) )
        return 0;

    model_count = UITreeSceneBridge_CollectPlayerDesignModelIds(
        &app->bridge,
        design->parts,
        design->gender,
        model_ids,
        (int)(sizeof(model_ids) / sizeof(model_ids[0])));

    for( int i = 0; i < model_count; i++ )
    {
        if( CacheProvider_ModelHas(app->provider, model_ids[i]) )
            continue;
        missing = 1;
        if( RS_IdkDesign_LoadRequestAdd(design, model_ids[i]) )
        {
            struct ToriRS_Task* task = CreateTask_ModelLoad(app->provider, model_ids[i]);
            if( task )
                ToriRS_TaskQueue_Add(app->runner.queue, task);
        }
    }
    if( missing )
        return 0; /* retry next tick, like the reference's checkModel() gate */

    if( UITreeSceneBridge_BuildPlayerDesignModel(
            &app->bridge, design->parts, design->colours, design->gender) < 0 )
        return 0;

    design->redraw = 0;
    design->load_requested_count = 0;
    if( getenv("TORIRS_ANIM_DEBUG") )
        TORIRS_LOG("design_preview: rebuilt gender=%d kits=[%d,%d,%d,%d,%d,%d,%d] "
            "colours=[%d,%d,%d,%d,%d]\n",
            design->gender,
            design->parts[0], design->parts[1], design->parts[2], design->parts[3],
            design->parts[4], design->parts[5], design->parts[6],
            design->colours[0], design->colours[1], design->colours[2],
            design->colours[3], design->colours[4]);
    return 1;
}

/*
 * Reference CC_SWITCH_TO_MALE / CC_SWITCH_TO_FEMALE: both gender buttons carry
 * two sprites, and the pair is swapped so the selected gender wears the first
 * one. The reference captures graphic/graphic2 off whichever button it walks
 * first and applies that pair to both.
 */
static int
design_gender_button_tick(
    struct App* app,
    struct UITree* tree,
    int32_t idx,
    int client_code)
{
    struct UITreeComponent* c = &tree->components[idx];
    struct RS_IdkDesign* design = &app->idk_design;
    int want;

    if( c->type != UIELEM_RS_GRAPHIC )
        return 0;

    if( !design->button_scene_valid )
    {
        design->button_scene_id[0] = c->u.rs_graphic.scene_id;
        design->button_scene_id[1] = c->u.rs_graphic.scene_id_active;
        design->button_scene_valid = 1;
    }
    /* A cache whose gender buttons carry no second sprite has nothing to swap;
     * blanking the unselected button would be worse than leaving both alone. */
    if( design->button_scene_id[0] < 0 || design->button_scene_id[1] < 0 )
        return 0;

    want = ((client_code == RS_CC_SWITCH_TO_MALE) == (design->gender == 0))
               ? design->button_scene_id[0]
               : design->button_scene_id[1];
    if( c->u.rs_graphic.scene_id == want )
        return 0;
    c->u.rs_graphic.scene_id = want;
    UITree_MarkNodeDirty(tree, idx);
    return 1;
}

/*
 * Welcome screen (reference clientComponent, CC_LAST_LOGIN_INFO onwards). The
 * five rows below are the whole of what the client does with LAST_LOGIN_INFO,
 * and every one of them is a plain read of App::welcome -- the packet exec
 * stores, and nothing else in the client consults it.
 *
 * `last_ip == 0` means the server did not send one (and, at login, that no
 * LAST_LOGIN_INFO has arrived yet); the reference blanks the row rather than
 * printing an address of 0.0.0.0.
 */
static int
welcome_last_login_tick(
    struct App* app,
    struct UITree* tree,
    int32_t idx)
{
    char text[128];
    char when[32];
    uint32_t ip;

    assert(app);
    assert(tree);
    if( app->welcome.last_ip == 0 )
        return set_node_text(tree, idx, "");

    if( app->welcome.days_since_login == 0 )
        snprintf(when, sizeof(when), "earlier today");
    else if( app->welcome.days_since_login == 1 )
        snprintf(when, sizeof(when), "yesterday");
    else
        snprintf(when, sizeof(when), "%d days ago", app->welcome.days_since_login);

    ip = (uint32_t)app->welcome.last_ip;
    /* A loopback address is not information -- it is every account on a local
     * server -- so the reference drops the "from:" clause for it rather than
     * printing 127.0.0.1 to everyone. */
    if( ip == 0x7f000001u )
        snprintf(text, sizeof(text), "You last logged in %s.", when);
    else
        snprintf(
            text,
            sizeof(text),
            "You last logged in %s from: %u.%u.%u.%u",
            when,
            (ip >> 24) & 0xff,
            (ip >> 16) & 0xff,
            (ip >> 8) & 0xff,
            ip & 0xff);
    return set_node_text(tree, idx, text);
}

/* Reference recolours this row as well as retexting it: zero unread is yellow,
 * anything waiting is green. */
static int
welcome_unread_tick(
    struct App* app,
    struct UITree* tree,
    struct UITreeComponent* c,
    int32_t idx)
{
    char text[64];
    int colour;
    int changed = 0;

    assert(app);
    assert(tree);
    assert(c);
    if( app->welcome.unread_messages == 1 )
    {
        snprintf(text, sizeof(text), "1 unread message");
        colour = RS_CC_WELCOME_COLOUR_GREEN;
    }
    else if( app->welcome.unread_messages > 1 )
    {
        snprintf(text, sizeof(text), "%d unread messages", app->welcome.unread_messages);
        colour = RS_CC_WELCOME_COLOUR_GREEN;
    }
    else
    {
        snprintf(text, sizeof(text), "0 unread messages");
        colour = RS_CC_WELCOME_COLOUR_YELLOW;
    }

    changed |= set_node_text(tree, idx, text);
    if( c->type == UIELEM_RS_TEXT && c->u.rs_text.color != colour )
    {
        c->u.rs_text.color = colour;
        UITree_MarkNodeDirty(tree, idx);
        changed = 1;
    }
    return changed;
}

/*
 * The three recovery-question lines, which are one paragraph split across three
 * components -- so each row's text depends on which of the three paragraphs is
 * being told, not just on its own index. days_since_recovery carries two
 * non-day sentinels for that: 200 = never set, 201 = nothing to say (and then
 * member_warning may want the three rows for its own paragraph instead).
 */
static int
welcome_recovery_tick(
    struct App* app,
    struct UITree* tree,
    int32_t idx,
    int client_code)
{
    int const row = client_code - RS_CC_RECOVERY1;
    static char const* const k_members[3] = {
        "@yel@This is a non-members world: @whi@Since you are a member we",
        "@whi@recommend you use a members world instead. You may use",
        "@whi@this world but member benefits are unavailable whilst here.",
    };
    static char const* const k_never_set[3] = {
        "You have not yet set any password recovery questions.",
        "We strongly recommend you do so now to secure your account.",
        "Do this from the 'account management' area on our front webpage",
    };
    char text[128];

    assert(app);
    assert(tree);
    /* The caller keys on the 652..654 range, so a row outside 0..2 means that
     * range and this arithmetic have drifted apart -- and the two array reads
     * below would be the first thing to notice, silently. */
    assert(row >= 0 && row < 3);
    if( app->welcome.days_since_recovery == RS_CC_RECOVERY_DAYS_SILENT )
        return set_node_text(tree, idx, app->welcome.member_warning == 1 ? k_members[row] : "");
    if( app->welcome.days_since_recovery == RS_CC_RECOVERY_DAYS_NEVER_SET )
        return set_node_text(tree, idx, k_never_set[row]);

    if( row == 0 )
    {
        char when[32];
        if( app->welcome.days_since_recovery == 0 )
            snprintf(when, sizeof(when), "Earlier today");
        else if( app->welcome.days_since_recovery == 1 )
            snprintf(when, sizeof(when), "Yesterday");
        else
            snprintf(when, sizeof(when), "%d days ago", app->welcome.days_since_recovery);
        snprintf(text, sizeof(text), "%s you changed your recovery questions", when);
        return set_node_text(tree, idx, text);
    }
    if( row == 1 )
        return set_node_text(
            tree, idx, "If you do not remember making this change then cancel it immediately");
    return set_node_text(
        tree, idx, "Do this from the 'account management' area on our front webpage");
}

int
RS_ClientCode_Tick(
    struct App* app,
    struct UITree* tree,
    struct RS_Social const* social,
    uint64_t loop_cycle)
{
    int changed = 0;

    assert(app);
    assert(tree);
    assert(social);

    for( int ci = 0; ci < tree->client_code.count; ci++ )
    {
        int32_t i = tree->client_code.slots[ci];
        struct UITreeComponent* c;
        int cc;
        assert(i >= 0 && (uint32_t)i < tree->component_count);
        c = &tree->components[i];
        if( c->freed )
            continue;
        cc = c->behavior.client_code;
        if( cc <= 0 )
            continue;

        if( (cc >= RS_CC_FRIENDS_START && cc <= RS_CC_FRIENDS_END) ||
            (cc >= RS_CC_FRIENDS2_START && cc <= RS_CC_FRIENDS2_END) )
            changed |= friends_row_tick(tree, i, social, cc);
        else if(
            (cc >= RS_CC_FRIENDS_UPDATE_START && cc <= RS_CC_FRIENDS_UPDATE_END) ||
            (cc >= RS_CC_FRIENDS2_UPDATE_START && cc <= RS_CC_FRIENDS2_UPDATE_END) )
            changed |= friends_world_row_tick(tree, i, social, cc);
        else if( cc == RS_CC_FRIENDS_SIZE )
            changed |= set_list_scroll_height(
                tree,
                (int32_t)i,
                social->server_status == RS_SOCIAL_SERVER_CONNECTED ? social->friend_count : 0);
        else if( cc >= RS_CC_IGNORES_START && cc <= RS_CC_IGNORES_END )
            changed |= ignores_row_tick(tree, (int32_t)i, social, cc);
        else if( cc == RS_CC_IGNORES_SIZE )
            changed |= set_list_scroll_height(tree, (int32_t)i, social->ignore_count);
        else if( cc == RS_CC_DESIGN_PREVIEW && c->type == UIELEM_RS_MODEL )
        {
            /* Reference: modelXAn=150, modelYAn=sin(loop/40)*256 wrapped. */
            int yan = ((int)(sin((double)loop_cycle / 40.0) * 256.0)) & 0x7ff;
            if( c->u.rs_model.xan != 150 || c->u.rs_model.yan != yan )
            {
                (void)UITree_SetModelPoseAt(
                    tree,
                    (int32_t)i,
                    c->u.rs_model.x_offset,
                    c->u.rs_model.y_offset,
                    150,
                    yan,
                    c->u.rs_model.zan,
                    0);
                changed = 1;
            }
            if( app->idk_design.redraw && design_preview_rebuild(app) )
            {
                UITree_MarkNodeDirty(tree, (int32_t)i);
                changed = 1;
            }
        }
        else if( cc == RS_CC_SWITCH_TO_MALE || cc == RS_CC_SWITCH_TO_FEMALE )
            changed |= design_gender_button_tick(app, tree, (int32_t)i, cc);
        else if( cc == RS_CC_LAST_LOGIN_INFO || cc == RS_CC_LAST_LOGIN_INFO2 )
            changed |= welcome_last_login_tick(app, tree, (int32_t)i);
        else if( cc == RS_CC_UNREAD_MESSAGES )
            changed |= welcome_unread_tick(app, tree, c, (int32_t)i);
        else if( cc >= RS_CC_RECOVERY1 && cc <= RS_CC_RECOVERY3 )
            changed |= welcome_recovery_tick(app, tree, (int32_t)i, cc);
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
        TORIRS_LOG("clientcode: logout requested\n");
        return 1;
    case RS_CC_ACCEPT_DESIGN:
        /* Reference sends IDK_SAVEDESIGN and returns true, so the plain
         * IF_BUTTON follows it — the server needs both (the design, and the
         * button that advances the tutorial). */
        if( app->provider )
            RS_IdkDesign_EnsureResolved(&app->idk_design, app->provider);
        App_SendIdkDesign(
            app,
            app->idk_design.gender,
            app->idk_design.parts,
            app->idk_design.colours);
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
            /* Part/colour cycling and the gender switch are purely local: the
             * reference falls through to `return false`, so no IF_BUTTON goes
             * out until Accept. The preview picks the change up on the next
             * clientCode tick via idkDesignRedraw. */
            if( app->provider )
                RS_IdkDesign_Button(&app->idk_design, app->provider, cc);
            app->need_redraw = 1;
            return 0;
        }
        return 0;
    }
}
