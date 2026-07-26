#ifndef SRC_BOOTMANIFEST_BOOTMANIFEST_H
#define SRC_BOOTMANIFEST_BOOTMANIFEST_H

#include <stdint.h>

/*
 * Boot manifest — one INI file collapsing the whole per-generation boot
 * parameterization (cache identity/dir, protocol rev, transport, host:port,
 * login RSA/CRCs/version, revconfig includes, ui logic). See manifest_rs254.ini
 * / manifest_xrsps.ini at the repo root and docs/MULTI_GENERATIONAL_PARITY.md.
 *
 * Schema (house style [type:name] sections, lowercase key=value, ; / # comments):
 *
 *   [cache:boot]  epoch=dat1|dat2  game=rs2|oldschool  revision=<n>
 *                 quirks=none|kronos|void_rs634_no_xteas  dir=<path>  spawn=<x>,<z>
 *   [net:boot]    rev=<name>  transport=tcp|ws  host=<h>  port=<n>
 *                 client_version=<n>  rsa_exp=<hex>  rsa_mod=<hex>
 *                 jag_crc=<9 comma-separated int32>
 *   [ui:boot]     logic=cs1|cs2  chrome=revconfig|cache
 *                 revconfig_ui=<path>  revconfig_cache=<path>  interface_id=<n>
 *   [spawn:hotkeys]  npc=<id>  obj=<id>  spotanim=<id>
 *                    spotanim_height=<n>  spotanim_delay=<n>
 *                    proj_model=<id>  proj_seq=<id>
 *                 Optional; -1/absent = use the built-in default. Env
 *                 TORIRS_SPAWN_* still overrides (same precedence as
 *                 TORIRS_WORLD_MAP vs [cache:boot] spawn).
 *
 * [cache:boot] epoch/game/revision/quirks are all required. A missing key fails
 * the load with a stated reason (user input, not an internal invariant).
 *
 * Relative path values (dir, revconfig_ui, revconfig_cache) resolve against the
 * directory containing the manifest file; absolute paths pass through.
 *
 * Lifetime: BootManifest_ApplyToConfig points AppConfig string fields straight
 * into this struct's buffers, so a BootManifest handed to App_Init must outlive
 * the App (declare it `static` in main, as main.c does).
 */

struct AppConfig; /* fwd; src/app.h */

struct BootManifest
{
    /* [cache:boot] — identity (all four required) */
    int cache_game;      /* enum RSCache_Game; UNSET until parsed */
    int cache_epoch;     /* enum RSCache_Epoch; UNSET until parsed */
    int cache_revision;  /* game revision; -1 = unset */
    uint32_t cache_quirks;
    int cache_quirks_set; /* 1 when quirks= was present */
    int cache_kind;      /* enum AppCacheKind derived from epoch; -1 = unset */
    char cache_dir[512]; /* resolved against manifest dir */
    /* Map square to spawn on, "x,z". Both -1 = unset (client default 50,50).
     * Needed because the default is not universally loadable: a keyed cache
     * ships XTEA keys only for the squares it was dumped with, and cache.643
     * has no key for 50,50. */
    int spawn_x;
    int spawn_z;

    /* [net:boot] */
    char rev_name[32];
    char transport[16]; /* "tcp" | "ws"; "" = unset */
    char host[128];
    int port;           /* 0 = unset */
    int client_version; /* 0 = unset; login-block only, not cache identity */
    char rsa_exp[512];
    char rsa_mod[512];
    int32_t jag_crc[9];
    int jag_crc_set;

    /* [ui:boot] */
    int ui_logic;  /* enum AppUiLogic; 0 = unset/default */
    int chrome;    /* 0 unset, 1 revconfig, 2 cache */
    char revconfig_ui[512];    /* resolved */
    char revconfig_cache[512]; /* resolved */
    int interface_id;          /* 0 = unset */

    /* [spawn:hotkeys] — debug spawn-hotkey ids. -1 = unset (built-in default).
     * TORIRS_SPAWN_* env vars still override. */
    int spawn_npc_id;
    int spawn_obj_id;
    int spawn_spotanim_id;
    int spawn_spotanim_height;
    int spawn_spotanim_delay;
    int spawn_proj_model_id;
    int spawn_proj_seq_id;
};

/* Zero the manifest and load `path`. Relative paths resolve against
 * dirname(path). Returns 0 on success, <0 on read/parse failure or a missing
 * required [cache:boot] identity key (a stderr line names the problem). */
int
BootManifest_LoadFile(struct BootManifest* bm, char const* path);

/* Copy the manifest's set fields into cfg. Only fields the manifest actually
 * provided are written, so calling this before CLI flag parsing lets explicit
 * flags override by plain assignment (precedence: CLI > manifest > defaults). */
void
BootManifest_ApplyToConfig(struct BootManifest const* bm, struct AppConfig* cfg);

#endif
