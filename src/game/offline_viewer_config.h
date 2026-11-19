#ifndef SRC_GAME_OFFLINE_VIEWER_CONFIG_H
#define SRC_GAME_OFFLINE_VIEWER_CONFIG_H

/*
 * The offline interface viewer's launch options: the boot with no connect
 * target, which roots the tree at one interface itself instead of waiting for
 * a server's IF_OPENTOP, stands in for the login burst with the manifest's
 * mounts and var seeds, and can photograph one headless preview frame.
 *
 * One member of struct AppConfig (app.h `viewer`). BootManifest_ApplyToConfig
 * fills the manifest half (`[ui:boot] interface_id`, `[ui:gameframe]`,
 * `[ui:varc]`); main.c fills the positional interface id, the revconfig's
 * `[iface:boot]` default, and the preview half from the environment, once,
 * where it settles its configuration. A zeroed one is "no viewer options".
 *
 * This header names no App: it is plain data.
 */

struct BootManifestGameframeMount;
struct BootManifestVarcSeed;

struct OfflineViewerConfig
{
    /** The interface an offline boot roots the tree at (INTERFACE_ID_UNSET
     *  until something states one). A networked boot ignores it. */
    int interface_id;
    /** `[ui:gameframe]` — sub-interfaces to mount into the root's component
     *  slots once the tree is built, in order.
     *
     *  A gameframe root is a set of empty holders: the panels that fill it arrive
     *  as an IF_OPENSUB burst at login, so an offline client shows chrome with
     *  nothing in it. This is that burst, stated in the manifest. Points into the
     *  BootManifest, which must outlive the App. NULL/0 = none. */
    struct BootManifestGameframeMount const* gameframe_mounts;
    int gameframe_mount_count;
    /** `[ui:varc]` — client vars to seed before the root's scripts run.
     *
     *  The IF_OPENSUB burst arrives alongside var writes; the gameframe's own
     *  scripts branch on them (which sidebar tab is selected, which chat filters
     *  are lit), so mounting without them shows every panel hidden. Points into
     *  the BootManifest. NULL/0 = none. */
    struct BootManifestVarcSeed const* varc_seeds;
    int varc_seed_count;
    /** TORIRS_PREVIEW_BMP, as the environment had it when main.c settled the
     *  configuration: the path of the one headless preview frame to write.
     *  NULL = no preview (an empty string is a preview request with a bad
     *  path, refused where the frame is written). */
    char const* preview_bmp;
    /** TORIRS_PREVIEW_STATE, read at the same point: the preview-state packet
     *  (game/preview_state.h) applied before the root's scripts run. Only
     *  honoured beside preview_bmp. NULL = none. */
    char const* preview_state;
};

#endif
