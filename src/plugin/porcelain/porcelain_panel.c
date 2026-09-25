/*
 * Porcelain's row model: a reconciler over `api->panel`.
 *
 * The element reconciler in porcelain.c owns CONTROLS -- things Porcelain
 * created and can address by reference for the rest of their lives. This one
 * owns a DECLARATION the host holds, and that is a different problem with a
 * different failure mode.
 *
 * The host's page reconciler validates by IDENTITY ONLY -- serial, kind, id,
 * count. That is deliberate: three separate shipped bugs came from a property
 * check inside its validate loop, because a property check there turns a
 * caption into a rebuild and a rebuild into a flash with the scroll thrown
 * away and every retained custom run retired. So the rule here is the same
 * rule stated from the plugin's side:
 *
 *   - the ordered sequence of (key, kind, identity label) IS the declaration,
 *     and a change to it is the page's ONE legitimate rebuild;
 *   - every other difference is a setter on the row it names;
 *   - an unchanged property hash costs one 64-bit compare and no engine call;
 *   - a changed y-to-item mapping is neither: it is a new INPUT identity for
 *     that one row, which is what panel.reidentify exists for.
 *
 * The identity is (key, kind). Nothing else -- not even the row's name.
 *
 * `label` WAS part of it for KEY_VALUE, TOGGLE, SELECT and ACTION_ROW, and
 * that was the host's constraint rather than a choice: those four are built
 * from `label` and the patch path had no arm that restated one, so renaming
 * one cost the page. It has `set_label` now, so a rename is a setter on the
 * row it names like every other property. The four kinds whose one string
 * travels as `text` -- HEADING, PARAGRAPH, LABEL and BUTTON -- still have it
 * routed into the row's text, which is what makes either spelling work.
 *
 * And one drift the diff cannot see: the host commits a result BEFORE it
 * dispatches, so a refused pick leaves the host's copy moved and the
 * description unmoved. @see Porcelain_Restate.
 */

#include "plugin/porcelain/porcelain_internal.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

/* One pool for the process, claimed by Porcelain_Panel. @see PORCELAIN_PANELS_MAX */
static struct PorcelainPanel g_panels[PORCELAIN_PANELS_MAX];

/* ------------------------------------------------------------------------ */
/* Row shape: what the host builds from, and what it can patch               */
/* ------------------------------------------------------------------------ */

/**
 * Does this kind carry a LABEL as a second string beside its value?
 *
 * These four are built from `label` and hold their reading, their chosen
 * entry or their summary in `text`. `label` used to be part of their
 * DECLARATION IDENTITY, because the host's patch path had no arm for it and
 * the only way to change one was to declare the row again -- so renaming a
 * key/value row cost a page rebuild, which is a flash with the scroll thrown
 * away and every retained custom run on the page retired. The host has
 * `set_label` now, so a rename is a setter and the identity is just (key,
 * kind).
 */
static bool
panel_kind_has_label(enum PorcelainRowKind kind)
{
    return kind == PORCELAIN_ROW_KEY_VALUE || kind == PORCELAIN_ROW_TOGGLE ||
           kind == PORCELAIN_ROW_SELECT || kind == PORCELAIN_ROW_ACTION_ROW;
}

/**
 * Does this kind carry its one string as the row's TEXT?
 *
 * HEADING, PARAGRAPH and LABEL are drawn from a readout over (label, text),
 * and BUTTON's caption is built from `label` but patched from `text`. Putting
 * the string in `text` for all four makes both halves agree AND makes the
 * string patchable, which is the whole of host fix H1 from the plugin's side.
 */
static bool
panel_text_carries_the_string(enum PorcelainRowKind kind)
{
    return kind == PORCELAIN_ROW_HEADING || kind == PORCELAIN_ROW_PARAGRAPH ||
           kind == PORCELAIN_ROW_LABEL || kind == PORCELAIN_ROW_BUTTON;
}

/** The host's node kind for a row kind. Every row goes through `node`. */
static int
panel_node_kind(enum PorcelainRowKind kind)
{
    switch( kind )
    {
    case PORCELAIN_ROW_HEADING: return TORIRS_PANEL_HEADING;
    case PORCELAIN_ROW_PARAGRAPH: return TORIRS_PANEL_PARAGRAPH;
    case PORCELAIN_ROW_LABEL: return TORIRS_PANEL_LABEL;
    case PORCELAIN_ROW_KEY_VALUE: return TORIRS_PANEL_KEY_VALUE;
    case PORCELAIN_ROW_TOGGLE: return TORIRS_PANEL_TOGGLE;
    case PORCELAIN_ROW_SELECT: return TORIRS_PANEL_SELECT;
    case PORCELAIN_ROW_BUTTON: return TORIRS_PANEL_BUTTON;
    case PORCELAIN_ROW_ACTION_ROW: return TORIRS_PANEL_ACTION_ROW;
    case PORCELAIN_ROW_SEPARATOR: return TORIRS_PANEL_SEPARATOR;
    case PORCELAIN_ROW_PROGRESS: return TORIRS_PANEL_PROGRESS;
    case PORCELAIN_ROW_CUSTOM: return TORIRS_PANEL_CUSTOM;
    case PORCELAIN_ROW_KIND_COUNT: break;
    }
    /* Every kind is named above. A value outside the enum reached this
     * through a cast, and guessing a node kind for it would declare a row
     * nothing can address. */
    assert(0);
    return TORIRS_PANEL_LABEL;
}

/* ------------------------------------------------------------------------ */
/* Hashing                                                                  */
/* ------------------------------------------------------------------------ */

/*
 * (key, kind). Nothing else.
 *
 * The label was in here until the host grew a patch arm for it, and that is
 * the whole of what the row model's declaration identity is now: the ordered
 * sequence of these IS the declaration, and every difference that is not one
 * of these is a setter on the row it names.
 */
static uint64_t
panel_identity_hash(struct PorcelainNormalRow const* row)
{
    uint64_t hash = Porcelain_HashString(0, row->key.text);
    hash = Porcelain_HashBytes(hash, &row->kind, sizeof(row->kind));
    return hash;
}

/**
 * The int this kind pushes through set_value.
 *
 * A BUTTON's `value` IS its availability -- what the builder's `enabled`
 * argument writes and what the patch reads to draw it dim. Everything else
 * pushes the row's own value.
 */
static int
panel_pushed_value(struct PorcelainNormalRow const* row)
{
    if( row->kind == PORCELAIN_ROW_BUTTON )
        return row->disabled ? 0 : 1;
    return row->value;
}

static uint64_t
panel_options_hash(struct PorcelainPanel const* panel, struct PorcelainNormalRow const* row)
{
    uint64_t hash = Porcelain_HashBytes(0, &row->option_count, sizeof(row->option_count));
    for( int i = 0; i < row->option_count; i++ )
    {
        struct PorcelainOption const* option = &panel->options[row->option_first + i];
        hash = Porcelain_HashString(hash, option->value);
        hash = Porcelain_HashString(hash, option->label);
        hash = Porcelain_HashString(hash, option->detail);
        hash = Porcelain_HashBytes(hash, &option->enabled, sizeof(option->enabled));
    }
    return hash;
}

static uint64_t
panel_property_hash(struct PorcelainNormalRow const* row)
{
    uint64_t hash = Porcelain_HashBytes(0, &row->text_hash, sizeof(row->text_hash));
    hash = Porcelain_HashBytes(hash, &row->label_hash, sizeof(row->label_hash));
    hash = Porcelain_HashBytes(hash, &row->options_hash, sizeof(row->options_hash));
    hash = Porcelain_HashBytes(hash, &row->pushed_value, sizeof(row->pushed_value));
    hash = Porcelain_HashBytes(hash, &row->height, sizeof(row->height));
    /* hit_key and paint_key are deliberately NOT here. They are not
     * properties the host can set: one is an INPUT identity answered by a
     * reidentify, the other a picture answered by a redraw. Folding hit_key
     * in would make every readout that happens to move a box remint a serial,
     * and leaving those two questions conflated is what made a kill that
     * added a source rebuild the whole page. */
    return hash;
}

/* ------------------------------------------------------------------------ */
/* The describe-side verbs                                                  */
/* ------------------------------------------------------------------------ */

static struct Porcelain*
panel_of_describe(struct ToriRS_PorcelainDescribe* describe)
{
    struct Porcelain* porcelain;

    assert(describe);
    porcelain = describe->porcelain;
    assert(porcelain);
    assert(porcelain->used);
    /* Every builder verb is legal only inside the run it was handed to. */
    assert(porcelain->describing);
    return porcelain;
}

/**
 * Refuse this describe run's rows and keep the last good declaration.
 *
 * ONLY for a CAPACITY overrun -- a full row table, an exhausted option pool.
 * There, every row after the one that overran is dropped too, so what would
 * reach the host is half a description: the page would lose the rows the
 * overrun cut, the sequence would differ, and the reconciler would call that
 * a rebuild. So an overrun leaves the applied declaration exactly as it was,
 * and says so once.
 *
 * A row the LAYER refused -- an over-long string, a duplicate key, an option
 * with no stable value -- is not that, and must not come here. @see
 * panel_refuse_row: the rest of the description is intact, and discarding it
 * meant one bad provider id blanked an entire settings page.
 */
static void
panel_poison(struct Porcelain* porcelain, int result, char const* detail)
{
    assert(porcelain);
    assert(porcelain->panel);
    porcelain->panel->row_scratch_poisoned = true;
    Porcelain_RecordFinding(porcelain, "row", PORCELAIN_EL(NONE), result, detail);
}

/**
 * Drop ONE row and tell the plugin which. The description stands.
 *
 * The row being built is at panel->rows[panel->row_count] and has not been
 * counted yet, so dropping it is a matter of not counting it -- but its
 * options are already in the shared pool, and leaving them there would make
 * every later row's option_first wrong by however many this one wrote. The
 * pool is rewound to where the row started, which is why option_first is
 * assigned before the first string is copied rather than after.
 */
static void
panel_refuse_row(struct Porcelain* porcelain, struct PorcelainNormalRow const* row,
                 char const* detail)
{
    struct PorcelainPanel* panel;

    assert(porcelain);
    assert(porcelain->panel);
    assert(row);
    assert(detail);
    panel = porcelain->panel;
    panel->option_count = row->option_first;
    panel->counters.dropped_rows++;
    Porcelain_RecordFinding(porcelain, "row", PORCELAIN_EL(NONE), PORCELAIN_FINDING_REFUSED,
                            detail);
}

/**
 * Copy a borrowed string, or refuse the row if it does not fit.
 *
 * Never truncates. A truncated stable value names a different option and
 * reads back as though the person had picked it; a truncated key aliases
 * another row's identity. Both are the silent-wrong-answer class, so the
 * ceiling is a refusal with a finding and not a memcpy with a shorter length.
 *
 * The refusal is the CALLER's to act on, because only the caller knows which
 * row is being built and therefore what has to be rewound. This used to
 * poison the whole run from in here.
 */
static bool
panel_copy_or_refuse(char* destination, size_t capacity, char const* source)
{
    assert(destination);
    assert(capacity > 0);
    if( !source )
    {
        destination[0] = '\0';
        return true;
    }
    if( strlen(source) >= capacity )
        return false;
    memcpy(destination, source, strlen(source) + 1);
    return true;
}

void
Porcelain_Row(struct ToriRS_PorcelainDescribe* describe, struct PorcelainRow const* row)
{
    struct Porcelain* porcelain = panel_of_describe(describe);
    struct PorcelainPanel* panel;
    struct PorcelainNormalRow* normal;
    char const* string;

    assert(row);
    assert(row->key);
    assert(row->key[0]);
    assert(row->kind >= PORCELAIN_ROW_HEADING);
    assert(row->kind < PORCELAIN_ROW_KIND_COUNT);
    if( row->option_count > 0 )
        assert(row->options);
    /* A row described by a plugin that never registered a pane is a plugin
     * that believes it has a page. Nothing downstream can make that true. */
    assert(porcelain->panel);

    panel = porcelain->panel;
    if( panel->row_scratch_poisoned )
        return;
    if( panel->row_count >= PORCELAIN_ROWS_MAX )
    {
        /* A CAPACITY overrun: every row after this one is lost too, so what
         * would reach the host is half a description. @see panel_poison. */
        panel_poison(porcelain, PORCELAIN_FINDING_BUDGET, row->key);
        return;
    }

    normal = &panel->rows[panel->row_count];
    memset(normal, 0, sizeof(*normal));
    /* FIRST, before a single string is copied, because every refusal below
     * rewinds the option pool to it. @see panel_refuse_row. */
    normal->option_first = panel->option_count;
    normal->option_count = 0;

    for( int i = 0; i < panel->row_count; i++ )
        if( strcmp(panel->rows[i].key.text, row->key) == 0 )
        {
            /* Two rows under one key: the host's declaration is idempotent on
             * a repeated id, so the second would vanish and every setter
             * aimed at it would land on the first. Dropping the second is what
             * the host would do anyway; discarding the PAGE for it was the
             * layer punishing every other row for one bad key. */
            panel_refuse_row(porcelain, normal, row->key);
            return;
        }

    /* The HOST's ceiling, which is narrower than Porcelain's own key. */
    if( !panel_copy_or_refuse(normal->key.text, PORCELAIN_ROW_KEY_MAX, row->key) )
    {
        panel_refuse_row(porcelain, normal, row->key);
        return;
    }
    normal->kind = row->kind;
    normal->value = row->value;
    normal->height = row->height;
    normal->disabled = row->disabled;
    normal->hit_key = row->hit_key;
    normal->paint_key = row->paint_key;
    normal->on_action = row->on_action;
    normal->on_menu = row->on_menu;
    normal->paint = row->paint;
    normal->user = row->user;

    /*
     * One string, two spellings. A caller writes .label on a HEADING and
     * .text on a LABEL and means the same thing; routing both into the field
     * the host can patch means neither spelling costs a rebuild.
     */
    if( panel_text_carries_the_string(row->kind) )
    {
        string = row->text && row->text[0] ? row->text : row->label;
        if( !panel_copy_or_refuse(normal->text, sizeof(normal->text), string) )
        {
            panel_refuse_row(porcelain, normal, row->key);
            return;
        }
    }
    else
    {
        if( !panel_copy_or_refuse(normal->label, sizeof(normal->label), row->label) ||
            !panel_copy_or_refuse(normal->text, sizeof(normal->text), row->text) )
        {
            panel_refuse_row(porcelain, normal, row->key);
            return;
        }
    }

    for( int i = 0; i < row->option_count; i++ )
    {
        struct ToriRS_SelectOption const* wanted = &row->options[i];
        struct PorcelainOption* option;

        if( panel->option_count >= PORCELAIN_ROW_OPTIONS_MAX )
        {
            /* The shared pool, not this row's own ceiling: the rows after
             * this one have nowhere to put their options either. */
            panel_poison(porcelain, PORCELAIN_FINDING_BUDGET, row->key);
            return;
        }
        option = &panel->options[panel->option_count];
        memset(option, 0, sizeof(*option));
        /* The host refuses an option with no stable value, and a row that
         * silently lost one of its choices is a row whose selection moved --
         * so the ROW goes, and the page it was on does not. */
        if( !wanted->value || !wanted->value[0] || !wanted->label ||
            !panel_copy_or_refuse(option->value, sizeof(option->value), wanted->value) ||
            !panel_copy_or_refuse(option->label, sizeof(option->label), wanted->label) ||
            !panel_copy_or_refuse(option->detail, sizeof(option->detail), wanted->detail) )
        {
            panel_refuse_row(porcelain, normal, row->key);
            return;
        }
        option->enabled = wanted->enabled;
        panel->option_count++;
        normal->option_count++;
    }

    normal->text_hash = Porcelain_HashString(0, normal->text);
    normal->label_hash = Porcelain_HashString(0, normal->label);
    normal->options_hash = panel_options_hash(panel, normal);
    normal->pushed_value = panel_pushed_value(normal);
    normal->identity = panel_identity_hash(normal);
    normal->properties = panel_property_hash(normal);
    panel->row_count++;
}

void
Porcelain_Reidentify(struct ToriRS_PorcelainDescribe* describe, char const* key)
{
    struct Porcelain* porcelain = panel_of_describe(describe);
    struct PorcelainPanel* panel;

    assert(key);
    assert(key[0]);
    assert(porcelain->panel);

    panel = porcelain->panel;
    if( panel->row_scratch_poisoned )
        return;
    for( int i = 0; i < panel->row_count; i++ )
        if( strcmp(panel->rows[i].key.text, key) == 0 )
        {
            panel->rows[i].force_reidentify = true;
            return;
        }
    /*
     * A key this run has not described. Not an assert: the order of the two
     * calls is the plugin's business and a row described AFTER its reidentify
     * would abort a legal description. It is a finding because the identity
     * the plugin asked to retire is then not retired, and a click authored
     * against the old picture would be delivered.
     */
    Porcelain_RecordFinding(porcelain, "reidentify", PORCELAIN_EL(NONE),
                            PORCELAIN_FINDING_REFUSED, key);
}

/**
 * Say one row again, because the HOST moved it and the description did not.
 *
 * This is the one drift the reconciler cannot see. The host commits a result
 * to its widget model and bumps its model revision BEFORE it dispatches the
 * action, so a plugin that REFUSES a pick the host accepted is looking at a
 * control which already shows the value nobody could honour -- while its own
 * description, which is the thing the reconciler diffs, has not moved at all.
 * Every hash matches, no setter fires, and the row keeps showing a choice
 * that was never saved.
 *
 * The two verbs that existed are both the wrong instrument:
 * Porcelain_Invalidate says it by rebuilding the whole page, which is the
 * flash this family exists to remove, and Porcelain_Reidentify mints a fresh
 * serial and leaves the wrong value in place.
 *
 * It is worth being clear about why most rows never need this, because the
 * difference IS the design. A feature pick writes to CONFIG, and config is an
 * INPUT to the layer: the next describe reads the value back, sees the
 * refusal, and describes the row differently, so the description genuinely
 * moves and the ordinary per-property compare carries it. A refusal that
 * writes nothing anywhere moves no input, and there is nothing for a describe
 * to notice. So this verb is not "push the row again" -- it is "the host's
 * copy of this row is not what I last described, and my description is the
 * one that is right".
 *
 * Callable from anywhere, and an action handler is where it belongs, because
 * that is the frame that KNOWS a refusal happened. It costs the whole of that
 * kind's setters at the next reconcile and nothing else: no rebuild, no
 * serial, no scroll, no retired custom run.
 *
 * A key the HOST does not hold is a finding and not an assert, for the same
 * reason Porcelain_Reidentify's is: a page rebuilt between the action and the
 * next fence legitimately holds no such row, and aborting there would kill a
 * plugin for the host's timing.
 */
void
Porcelain_Restate(struct Porcelain* porcelain, char const* key)
{
    struct PorcelainPanel* panel;

    assert(porcelain);
    assert(porcelain->used);
    assert(key);
    assert(key[0]);
    /* A restate from a plugin that never registered a pane is a plugin that
     * believes it has a page. @see Porcelain_Row's own assert. */
    assert(porcelain->panel);

    panel = porcelain->panel;
    for( int i = 0; i < panel->declared_count; i++ )
        if( strcmp(panel->declared[i].key.text, key) == 0 )
        {
            panel->declared[i].restate = true;
            /*
             * The row reconciler runs inside the element reconcile, which the
             * fence runs only when an input moved -- and a refusal that wrote
             * nothing moved none, which is the whole reason this verb exists.
             * So say one moved. It costs a describe, which is pure, and NOT a
             * page: the row sequence is unchanged, so the reconcile below
             * takes the setter arm and this one row's setters are all that
             * fire.
             */
            Porcelain_Invalidate(porcelain);
            return;
        }
    Porcelain_RecordFinding(porcelain, "restate", PORCELAIN_EL(NONE),
                            PORCELAIN_FINDING_REFUSED, key);
}

/**
 * Where the page is scrolled to, or -1 when no page of ours is up.
 *
 * -1 and not 0, because 0 IS an answer -- the top -- and a plugin that could
 * not tell it from "there is no page" would restore a place nobody took.
 */
int
Porcelain_PanelScroll(struct Porcelain* porcelain)
{
    assert(porcelain);
    assert(porcelain->used);
    assert(porcelain->panel);
    porcelain->counters.engine_calls++;
    return porcelain->api->panel.scroll(porcelain->api);
}

/**
 * Put the reader somewhere. Clamped by the presenter's next layout.
 *
 * The host already carries the place across the one legitimate rebuild by
 * itself, so this is for MOVING it -- scrolling a row that has just arrived
 * into view -- which is a different intent and a rarer one.
 */
void
Porcelain_PanelScrollTo(struct Porcelain* porcelain, int scroll)
{
    assert(porcelain);
    assert(porcelain->used);
    assert(porcelain->panel);
    porcelain->counters.engine_calls++;
    if( porcelain->api->panel.scroll_to(porcelain->api, scroll) != TORIRS_RESULT_OK )
        /* No page of ours is up. Not an assert: the page can close between the
         * frame that decided to scroll and the one that says so. */
        Porcelain_RecordFinding(porcelain, "scroll_to", PORCELAIN_EL(NONE),
                                PORCELAIN_FINDING_REFUSED, porcelain->plugin_id);
}

/* ------------------------------------------------------------------------ */
/* Registration                                                             */
/* ------------------------------------------------------------------------ */

void
Porcelain_Panel(struct Porcelain* porcelain, char const* icon_asset, int width, unsigned faces)
{
    struct ToriRS_PanelDescriptor descriptor;
    struct PorcelainPanel* panel = NULL;

    assert(porcelain);
    assert(porcelain->used);
    assert(width > 0);
    /* Zero faces is a page nothing can ever open. */
    assert(faces != 0);
    assert((faces & ~(unsigned)PORCELAIN_FACE_BOTH) == 0);
    /* Twice is a plugin that does not know which registration won. */
    assert(!porcelain->panel);

    for( int i = 0; i < PORCELAIN_PANELS_MAX; i++ )
        if( !g_panels[i].used )
        {
            panel = &g_panels[i];
            break;
        }
    /* A full table is a build with more panel plugins than the layer was
     * sized for; carrying on would give one of them a page it cannot see. */
    assert(panel);

    memset(panel, 0, sizeof(*panel));
    panel->used = true;
    panel->owner = porcelain;
    panel->faces = faces;
    panel->width = width;
    panel->built_view = -1;
    /* NULL is the documented ask for the baked wrench, not an absent icon. */
    if( icon_asset )
        Porcelain_CopyString(panel->icon, sizeof(panel->icon), icon_asset);
    porcelain->panel = panel;

    memset(&descriptor, 0, sizeof(descriptor));
    descriptor.icon_asset = panel->icon[0] ? panel->icon : NULL;
    descriptor.preferred_width = width;
    porcelain->counters.engine_calls++;
    if( porcelain->api->panel.request(porcelain->api, &descriptor) != TORIRS_RESULT_OK )
        /* Registration is on_start only; a refusal means the pane does not
         * exist and every row this plugin describes is going nowhere. */
        Porcelain_RecordFinding(porcelain, "panel", PORCELAIN_EL(NONE),
                                PORCELAIN_FINDING_REFUSED, porcelain->plugin_id);
    else
        panel->registered = true;
}

/* ------------------------------------------------------------------------ */
/* The build half: emitting a declaration                                   */
/* ------------------------------------------------------------------------ */

static unsigned
panel_face_of_view(int view)
{
    return view == TORIRS_PANEL_VIEW_SETTINGS ? (unsigned)PORCELAIN_FACE_SETTINGS
                                              : (unsigned)PORCELAIN_FACE_PAGE;
}

static void
panel_emit_row(struct Porcelain* porcelain, struct ToriRS_PanelBuilder* builder,
               struct PorcelainNormalRow const* row)
{
    struct PorcelainPanel const* panel = porcelain->panel;
    struct ToriRS_SelectOption options[PORCELAIN_ROW_OPTIONS_MAX];
    struct ToriRS_PanelNode node;

    memset(&node, 0, sizeof(node));
    node.struct_size = sizeof(node);
    node.kind = panel_node_kind(row->kind);
    node.id = row->key.text;
    node.label = row->label[0] ? row->label : NULL;
    node.value = row->value;
    node.preferred_height = row->height;

    switch( row->kind )
    {
    case PORCELAIN_ROW_BUTTON:
        /* The caption twice, because the build reads `label` and the patch
         * reads `text`: one of them alone would revert the other's answer the
         * next time the page was declared. `value` IS the button's
         * availability. @see host fix H1, H2. */
        node.label = row->text;
        node.text = row->text;
        node.value = row->disabled ? 0 : 1;
        break;
    case PORCELAIN_ROW_SELECT:
        /* The host requires a selected value even when no option carries it:
         * an unselected row is a row whose pick nothing can answer. */
        node.text = row->text;
        for( int i = 0; i < row->option_count; i++ )
        {
            struct PorcelainOption const* copied = &panel->options[row->option_first + i];
            memset(&options[i], 0, sizeof(options[i]));
            options[i].struct_size = sizeof(options[i]);
            options[i].value = copied->value;
            options[i].label = copied->label;
            options[i].detail = copied->detail;
            options[i].enabled = copied->enabled;
        }
        node.options = options;
        node.option_count = row->option_count;
        break;
    case PORCELAIN_ROW_SEPARATOR:
    case PORCELAIN_ROW_CUSTOM:
        /* Neither reads a string, and passing an empty one would journal a
         * text change on a row that has nowhere to put it. */
        node.label = NULL;
        node.text = NULL;
        break;
    case PORCELAIN_ROW_TOGGLE:
        node.text = NULL;
        break;
    default:
        node.text = row->text[0] ? row->text : NULL;
        break;
    }

    porcelain->counters.engine_calls++;
    if( builder->node(builder, &node) != TORIRS_RESULT_OK )
        Porcelain_RecordFinding(porcelain, "row", PORCELAIN_EL(NONE), PORCELAIN_FINDING_REFUSED,
                                row->key.text);
}

static void
panel_record_declaration(struct PorcelainPanel* panel)
{
    panel->declared_count = panel->row_count;
    for( int i = 0; i < panel->row_count; i++ )
    {
        struct PorcelainDeclaredRow* declared = &panel->declared[i];
        memset(declared, 0, sizeof(*declared));
        declared->key = panel->rows[i].key;
        declared->kind = panel->rows[i].kind;
        declared->identity = panel->rows[i].identity;
        declared->properties = panel->rows[i].properties;
        declared->hit_key = panel->rows[i].hit_key;
        declared->paint_key = panel->rows[i].paint_key;
        declared->text_hash = panel->rows[i].text_hash;
        declared->label_hash = panel->rows[i].label_hash;
        declared->options_hash = panel->rows[i].options_hash;
        declared->pushed_value = panel->rows[i].pushed_value;
        declared->height = panel->rows[i].height;
    }
}

void
Porcelain_PanelBuild(struct Porcelain* porcelain, struct ToriRS_PanelBuilder* builder, int view)
{
    struct PorcelainPanel* panel;

    assert(porcelain);
    assert(porcelain->used);
    assert(builder);
    /* `node` is the one builder verb Porcelain uses, because it is the only
     * one that takes an id for every kind: `heading` and `paragraph` mint a
     * generated one, and a row whose id Porcelain did not choose is a row it
     * can never patch, reidentify, or route an action to. */
    assert(builder->node);
    /* A build for a plugin that never called Porcelain_Panel is the host and
     * the plugin disagreeing about whether this plugin has a page at all. */
    assert(porcelain->panel);

    panel = porcelain->panel;
    /* Whatever the host held, it does not hold any more: a build callback IS
     * the page having been cleared. */
    panel->built = false;
    panel->declared_count = 0;
    panel->built_view = view;
    /* A build callback IS the host asking, so at this moment nothing is owed.
     * Clearing it here and not after the emit matters: a build for a face we
     * do not cover returns before the emit, and a flag left standing from an
     * earlier refused build would make the next fence ask for a rebuild of a
     * page that is not even ours. */
    panel->declaration_owed = false;
    if( !(panel->faces & panel_face_of_view(view)) )
        /* Not a face this description covers. Declaring nothing is what makes
         * the host present the form generated from the config schema, which
         * is exactly what a plugin with no settings handler has always got. */
        return;

    /*
     * The page can be opened before this plugin has ever fenced. Declaring an
     * empty page here and filling it at the next fence would be a rebuild and
     * a flash for a page that was simply new.
     */
    if( !panel->row_set_ready )
        Porcelain_DescribeNow(porcelain);
    if( panel->row_scratch_poisoned )
    {
        /* Nothing good to declare and nothing retained to keep. Say so once,
         * and remember that the page is empty: nothing else will ask for a
         * declaration again, so the first describe that fits owes one. */
        Porcelain_RecordFinding(porcelain, "panel_build", PORCELAIN_EL(NONE),
                                PORCELAIN_FINDING_BUDGET, porcelain->plugin_id);
        panel->declaration_owed = true;
        return;
    }

    for( int i = 0; i < panel->row_count; i++ )
        panel_emit_row(porcelain, builder, &panel->rows[i]);
    panel_record_declaration(panel);
    panel->built = true;
    panel->counters.builds++;
}

/* ------------------------------------------------------------------------ */
/* The patch half: the reconcile                                            */
/* ------------------------------------------------------------------------ */

static bool
panel_sequence_matches(struct PorcelainPanel const* panel)
{
    if( panel->row_count != panel->declared_count )
        return false;
    for( int i = 0; i < panel->row_count; i++ )
        if( panel->rows[i].identity != panel->declared[i].identity )
            return false;
    return true;
}

static void
panel_note_result(struct Porcelain* porcelain, char const* verb, char const* key,
                  enum ToriRS_Result result)
{
    porcelain->panel->counters.setters++;
    porcelain->counters.engine_calls++;
    if( result != TORIRS_RESULT_OK && result != TORIRS_RESULT_PENDING )
        Porcelain_RecordFinding(porcelain, verb, PORCELAIN_EL(NONE), PORCELAIN_FINDING_REFUSED,
                                key);
}

static void
panel_apply_text(struct Porcelain* porcelain, struct PorcelainNormalRow const* row)
{
    struct ToriRS_Api* api = porcelain->api;
    panel_note_result(porcelain, "set_text", row->key.text,
                      api->panel.set_text(api, row->key.text, row->text));
}

static void
panel_apply_value(struct Porcelain* porcelain, struct PorcelainNormalRow const* row)
{
    struct ToriRS_Api* api = porcelain->api;
    panel_note_result(porcelain, "set_value", row->key.text,
                      api->panel.set_value(api, row->key.text, row->pushed_value));
}

static void
panel_apply_options(struct Porcelain* porcelain, struct PorcelainNormalRow const* row)
{
    struct PorcelainPanel const* panel = porcelain->panel;
    struct ToriRS_Api* api = porcelain->api;
    struct ToriRS_SelectOption options[PORCELAIN_ROW_OPTIONS_MAX];

    for( int i = 0; i < row->option_count; i++ )
    {
        struct PorcelainOption const* copied = &panel->options[row->option_first + i];
        memset(&options[i], 0, sizeof(options[i]));
        options[i].struct_size = sizeof(options[i]);
        options[i].value = copied->value;
        options[i].label = copied->label;
        options[i].detail = copied->detail;
        options[i].enabled = copied->enabled;
    }
    /* A changed option COUNT is a setter here and not a rebuild. It used to
     * be refused, and both settings pages answered the refusal by
     * invalidating the whole page for a one-row edit. @see host fix H3. */
    panel_note_result(porcelain, "set_options", row->key.text,
                      api->panel.set_options(api, row->key.text, row->text, options,
                                             row->option_count));
}

static void
panel_apply_label(struct Porcelain* porcelain, struct PorcelainNormalRow const* row)
{
    struct ToriRS_Api* api = porcelain->api;
    panel_note_result(porcelain, "set_label", row->key.text,
                      api->panel.set_label(api, row->key.text, row->label));
}

static void
panel_apply_height(struct Porcelain* porcelain, struct PorcelainNormalRow const* row)
{
    struct ToriRS_Api* api = porcelain->api;
    panel_note_result(porcelain, "set_height", row->key.text,
                      api->panel.set_height(api, row->key.text, row->height));
}

/**
 * Push exactly the properties this kind's patch path can apply, and only the
 * ones that moved.
 *
 * Per-property and not per-row, because a BUTTON whose caption changed would
 * otherwise also restate its availability -- a call that says nothing, on the
 * path the whole family exists to keep cheap.
 */
static void
panel_apply_properties(struct Porcelain* porcelain, struct PorcelainNormalRow const* row,
                       struct PorcelainDeclaredRow const* have, bool force)
{
    /*
     * `force` is a restate: the host's copy of this row moved without the
     * description moving, so every per-property compare below would match and
     * push nothing, which is the exact shape of the defect Porcelain_Restate
     * exists to fix. A restate therefore states the whole kind, unconditionally.
     */
    bool const text_moved = force || row->text_hash != have->text_hash;
    bool const value_moved = force || row->pushed_value != have->pushed_value;

    porcelain->panel->counters.row_applies++;
    /* The NAME, on the four kinds that carry one, and separately from every
     * value below: a key/value row whose reading moved must not also restate
     * its key, which is a call that says nothing on the path this family
     * exists to keep free. */
    if( panel_kind_has_label(row->kind) && (force || row->label_hash != have->label_hash) )
        panel_apply_label(porcelain, row);
    switch( row->kind )
    {
    case PORCELAIN_ROW_HEADING:
    case PORCELAIN_ROW_PARAGRAPH:
    case PORCELAIN_ROW_LABEL:
    case PORCELAIN_ROW_KEY_VALUE:
    case PORCELAIN_ROW_ACTION_ROW:
        if( text_moved )
            panel_apply_text(porcelain, row);
        break;
    case PORCELAIN_ROW_BUTTON:
        if( text_moved )
            panel_apply_text(porcelain, row);
        if( value_moved )
            panel_apply_value(porcelain, row);
        break;
    case PORCELAIN_ROW_TOGGLE:
        if( value_moved )
            panel_apply_value(porcelain, row);
        break;
    case PORCELAIN_ROW_PROGRESS:
        if( text_moved )
            panel_apply_text(porcelain, row);
        if( value_moved )
            panel_apply_value(porcelain, row);
        break;
    case PORCELAIN_ROW_SELECT:
        /* The selected value travels WITH the list, in one call: the host
         * takes both and a set_text on a dropdown would land on its caption. */
        if( text_moved || row->options_hash != have->options_hash )
            panel_apply_options(porcelain, row);
        break;
    case PORCELAIN_ROW_CUSTOM:
        if( force || row->height != have->height )
            panel_apply_height(porcelain, row);
        break;
    case PORCELAIN_ROW_SEPARATOR:
        /* It has no properties. Reaching here means its hash moved, which it
         * cannot; the row_applies counter above still counts the walk. */
        break;
    case PORCELAIN_ROW_KIND_COUNT:
        assert(0);
        break;
    }
}

void
Porcelain_PanelRunBegin(struct Porcelain* porcelain)
{
    struct PorcelainPanel* panel;

    assert(porcelain);
    if( !porcelain->panel )
        return;
    panel = porcelain->panel;
    panel->row_count = 0;
    panel->option_count = 0;
    panel->row_scratch_poisoned = false;
    panel->row_set_ready = true;
}

void
Porcelain_PanelReconcile(struct Porcelain* porcelain)
{
    struct PorcelainPanel* panel;
    struct ToriRS_Api* api;

    assert(porcelain);
    if( !porcelain->panel )
        return;
    panel = porcelain->panel;
    api = porcelain->api;

    /* An overrun leaves the declaration the host holds exactly as it was. */
    if( panel->row_scratch_poisoned )
        return;
    if( !panel->built )
    {
        /* A build on a face of ours declared nothing because that run
         * overran. Ask for one more, once. Everywhere else `built` false
         * means no face of ours is up and the next build takes the
         * description as it stands. */
        if( panel->declaration_owed )
        {
            porcelain->counters.engine_calls++;
            api->panel.invalidate(api);
            panel->counters.rebuilds++;
            panel->declaration_owed = false;
        }
        return;
    }

    if( !panel_sequence_matches(panel) )
    {
        /*
         * The row SET changed. This is the page's one legitimate rebuild:
         * a detail block opened, a flag list grew, a heading arrived. The
         * host clears the page and calls back for a fresh declaration.
         */
        porcelain->counters.engine_calls++;
        api->panel.invalidate(api);
        panel->counters.rebuilds++;
        panel->built = false;
        panel->declared_count = 0;
        return;
    }

    for( int i = 0; i < panel->row_count; i++ )
    {
        struct PorcelainNormalRow const* wanted = &panel->rows[i];
        struct PorcelainDeclaredRow* have = &panel->declared[i];

        /*
         * The y-to-item mapping first, and separately. A well that grew a
         * band has to refuse a click queued against the old bitmap, and it
         * has to do so without the page, the scroll, or any other row's
         * retained run moving. @see host fix H5.
         */
        if( wanted->hit_key != have->hit_key || wanted->force_reidentify )
        {
            porcelain->counters.engine_calls++;
            if( api->panel.reidentify(api, wanted->key.text) != TORIRS_RESULT_OK )
                Porcelain_RecordFinding(porcelain, "reidentify", PORCELAIN_EL(NONE),
                                        PORCELAIN_FINDING_REFUSED, wanted->key.text);
            panel->counters.reidentifies++;
            have->hit_key = wanted->hit_key;
            /* Taking the new picture as already asked for, which it is: the
             * host marks a re-identified well dirty by construction, because
             * the old bitmap belonged to the old identity. Without this line
             * the redraw below would be a second request for the same
             * repaint on every band a tracker gains. */
            have->paint_key = wanted->paint_key;
        }
        if( wanted->paint_key != have->paint_key )
        {
            /* Same rows, same identity, different picture. Nothing the host
             * can see moved, so without this the well keeps the bitmap it
             * staged and a readout inside it never changes. */
            porcelain->counters.engine_calls++;
            api->panel.redraw(api, wanted->key.text);
            panel->counters.redraws++;
            have->paint_key = wanted->paint_key;
        }
        /* One 64-bit compare, and nothing else. Not "every per-field compare
         * happened to match" -- the row_applies counter tells the two apart.
         * A restate is the one thing that gets past an equal hash, because an
         * equal hash is exactly its symptom. @see Porcelain_Restate. */
        if( wanted->properties == have->properties && !have->restate )
            continue;
        panel_apply_properties(porcelain, wanted, have, have->restate);
        if( have->restate )
        {
            have->restate = false;
            panel->counters.restates++;
        }
        have->properties = wanted->properties;
        have->text_hash = wanted->text_hash;
        have->label_hash = wanted->label_hash;
        have->options_hash = wanted->options_hash;
        have->pushed_value = wanted->pushed_value;
        have->height = wanted->height;
    }
}

/* ------------------------------------------------------------------------ */
/* Routing                                                                  */
/* ------------------------------------------------------------------------ */

static struct PorcelainNormalRow const*
panel_row_by_key(struct PorcelainPanel const* panel, char const* key)
{
    for( int i = 0; i < panel->row_count; i++ )
        if( strcmp(panel->rows[i].key.text, key) == 0 )
            return &panel->rows[i];
    return NULL;
}

bool
Porcelain_PanelAction(struct Porcelain* porcelain, struct ToriRS_PanelActionEvent const* event)
{
    struct PorcelainNormalRow const* row;
    struct PorcelainRowAction action;
    PorcelainRowActionFn handler;

    assert(porcelain);
    assert(porcelain->used);
    assert(event);
    assert(event->id);
    assert(porcelain->panel);

    row = panel_row_by_key(porcelain->panel, event->id);
    /*
     * An id Porcelain does not know is NOT a finding. A description declared
     * on the settings face is ADDED to the form the host generates from the
     * config schema, so the rows of that form arrive here under ids Porcelain
     * never chose; calling each of them a refusal would fill the findings
     * table with the host's own correct behaviour.
     */
    if( !row )
        return false;
    /*
     * A secondary click is its OWN callback and never falls through to
     * on_action. A row written before this channel existed handles only
     * clicks, and handing it a right click as though it were one would make
     * every such row do the wrong thing at once -- which is the failure a
     * kind-flag on a shared callback guarantees and a separate slot cannot
     * have. A row with no on_menu declines it; the click is already swallowed
     * by the presenter, so nothing under the page sees it either way.
     */
    handler = event->action == TORIRS_PANEL_ACTION_MENU ? row->on_menu : row->on_action;
    if( !handler )
        return true;

    memset(&action, 0, sizeof(action));
    action.key = row->key.text;
    action.kind = event->action;
    action.value = event->value;
    action.text = event->text ? event->text : "";
    action.x = event->x;
    action.y = event->y;
    porcelain->panel->counters.actions++;
    handler(porcelain->api, row->user, &action);
    return true;
}

bool
Porcelain_PanelDraw(struct Porcelain* porcelain, char const* node, struct ToriRS_Graphics* draw)
{
    struct PorcelainNormalRow const* row;

    assert(porcelain);
    assert(porcelain->used);
    assert(node);
    assert(draw);
    assert(porcelain->panel);

    row = panel_row_by_key(porcelain->panel, node);
    if( !row || !row->paint )
        return false;
    assert(row->kind == PORCELAIN_ROW_CUSTOM);
    porcelain->panel->counters.paints++;
    row->paint(porcelain->api, row->user, row->key.text, draw);
    return true;
}

/* ------------------------------------------------------------------------ */
/* Lifecycle and test seams                                                 */
/* ------------------------------------------------------------------------ */

void
Porcelain_PanelClose(struct Porcelain* porcelain)
{
    assert(porcelain);
    if( !porcelain->panel )
        return;
    memset(porcelain->panel, 0, sizeof(*porcelain->panel));
    porcelain->panel = NULL;
}

void
Porcelain_PanelResetForTesting(void)
{
    memset(g_panels, 0, sizeof(g_panels));
}

void
Porcelain_PanelCountersReset(struct Porcelain* porcelain)
{
    assert(porcelain);
    /* A handle with no panel is the common case: every chrome and overlay
     * plugin resets its counters and has no page. */
    if( !porcelain->panel )
        return;
    memset(&porcelain->panel->counters, 0, sizeof(porcelain->panel->counters));
}

void
Porcelain_PanelCountersRead(struct Porcelain* porcelain, struct PorcelainPanelCounters* out)
{
    assert(porcelain);
    assert(porcelain->used);
    assert(out);
    assert(porcelain->panel);
    *out = porcelain->panel->counters;
}
