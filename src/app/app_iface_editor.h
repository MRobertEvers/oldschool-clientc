#ifndef SRC_APP_APP_IFACE_EDITOR_H
#define SRC_APP_APP_IFACE_EDITOR_H

/**
 * The interface editor in the client (docs/MAPED_DESIGN.html #cs2): the App
 * half of editor/iface/iface_panels.h, run by the registered module's tick
 * (editor/iface/iface_register.c).
 *
 *   MOUNT    the edited interface the way the server would:
 *            App_OpenRootInterface for "root", App_OpenSubInterface(slot,
 *            id, 0 modal / 1 overlay / 3 tab) for a sub.
 *   PICK     with picking armed, a click in the view hit-tests the uitree
 *            (UITree_HitTest) and selects the edited interface's component
 *            under it — the nearest ancestor that belongs to the interface.
 *   REFILL   the mirror's IFACE text is compared with what was last
 *            mounted every frame; a block whose lines changed and whose node
 *            keeps its kind is re-read through the typed layer
 *            (Editor_IfBlockReadComponent -> ToriRS_ComponentFromRSCacheDat2
 *            -> UITree_FillBuildFromToriRS) and written onto that one node; a
 *            structure change, a kind change or a graphic change remounts.
 *            Driven by the mirror, so another view's edit refills here too.
 *   RUN      CreateTask_CS2RunMixed with the selected component as the
 *            active one, on the app's serial runner; the trace (when asked)
 *            is captured around it and handed to the panel when it ends.
 *   WATCH    every watched varp / varbit / varc read from VarPManager /
 *            VarCManager each frame; a set writes it optimistically.
 */

#include <stddef.h>

struct App;
struct Editor;
struct EditorIfacePanels;
struct ToriRSChrome;
struct UIBuildComponent;
struct UITree;

/** The registered interface editor's panels (NULL when not registered). */
struct EditorIfacePanels*
AppIfaceEditor_Panels(struct App* app);

/**
 * Point the CS2 tools at the App's trees: the content root
 * (`[editor:boot] content_dir`), the cache, the compiler under
 * `[editor:boot] repo_root`, RuneStar's names when present
 * (TORIRS_CS2_NAMES, else ~/Documents/git_repos/cs2/...), and a scratch
 * directory under TMPDIR.
 */
void
AppIfaceEditor_Configure(
    struct App* app,
    struct EditorIfacePanels* panels);

/** One frame of the App half (`app` is the registry's opaque App). */
void
AppIfaceEditor_Service(
    void* app,
    struct EditorIfacePanels* panels,
    struct ToriRSChrome* ui,
    struct Editor* editor);

/** Release the App half's state. NULL-tolerant. */
void
AppIfaceEditor_ServiceFree(struct EditorIfacePanels* panels);

/** Show the interface editor and open `interface_name` (NULL: only show). */
void
AppIfaceEditor_Open(
    struct App* app,
    const char* interface_name);

/** Whether picking is armed (the host routes view clicks here then). */
int
AppIfaceEditor_PickArmed(struct App* app);

/** A primary click at canvas (x, y): with picking armed, select the
 *  component under it and return 1 (the click is consumed). */
int
AppIfaceEditor_PickAt(
    struct App* app,
    int x,
    int y);

/**
 * Write `build` onto tree node `index` without rebuilding the tree: its
 * position and size with their modes, hide, scroll size, transparency, and
 * the kind's own fields (text and colour, rect colour, model and pose).
 * Returns 0 when the node is of another kind or the build changes something
 * only a remount can (a graphic needs its sprite resolved).
 */
int
AppIfaceEditor_RefillNode(
    struct UITree* tree,
    int index,
    const struct UIBuildComponent* build);

#endif
