#ifndef SRC_EDITOR_PANELS_EDPANEL_PROBLEMS_API_H
#define SRC_EDITOR_PANELS_EDPANEL_PROBLEMS_API_H

/**
 * The Problems panel's way in (design section 9 / 10: "Problems lists lint
 * findings ... with a click-to-node"), as the dialogue builder reports to it.
 *
 * STAND-IN. Workbench 6.6 part 1 builds the Problems panel and part 2 (the
 * builders) reports to it; they were built in parallel, so part 2 declares
 * the seam here and links a WEAK default (builder_panels.c) that writes each
 * row to the console. The panel's own definitions, when they land, replace
 * the default at link time; the integrator joins the two (and may move this
 * header into the panel's own).
 *
 * A SOURCE is one reporter's set of rows ("npcbuilder:hans"): Report
 * replaces that source's rows, Clear drops them. `open`, when not NULL, is
 * what a click on row `index` runs (the builder selects the node); it is
 * called with `user` and must stay valid until the source is cleared or
 * reported again.
 */

struct EditorUi;

#define EDITOR_PROBLEM_SOURCE_MAX 96

enum Editor_ProblemSeverity
{
    EDITOR_PROBLEM_WARNING = 0,
    EDITOR_PROBLEM_ERROR,
};

struct Editor_Problem
{
    enum Editor_ProblemSeverity severity;
    /** "node n4", "op1 step 2". */
    char where[64];
    char message[192];
};

typedef void (*Editor_ProblemOpenFn)(
    struct EditorUi* ui,
    void* user,
    int index);

void
Editor_Problems_Report(
    struct EditorUi* ui,
    const char* source,
    const struct Editor_Problem* rows,
    int count,
    Editor_ProblemOpenFn open,
    void* user);

void
Editor_Problems_Clear(
    struct EditorUi* ui,
    const char* source);

#endif
