#ifndef SRC_BOOTMANIFEST_BOOTMANIFEST_H
#define SRC_BOOTMANIFEST_BOOTMANIFEST_H

#include <stdint.h>

/*
 * Boot manifest — one INI file collapsing the whole per-generation boot
 * parameterization (cache kind/dir, protocol rev, transport, host:port, login
 * RSA/CRCs/version, revconfig includes, ui logic). See manifest_rs254.ini /
 * manifest_xrsps.ini at the repo root and docs/MULTI_GENERATIONAL_PARITY.md.
 *
 * Schema (house style [type:name] sections, lowercase key=value, ; / # comments):
 *
 *   [cache:boot]  kind=dat1|dat2   dir=<path>
 *   [net:boot]    rev=<name>  transport=tcp|ws  host=<h>  port=<n>
 *                 client_version=<n>  rsa_exp=<hex>  rsa_mod=<hex>
 *                 jag_crc=<9 comma-separated int32>
 *   [ui:boot]     logic=cs1|cs2  chrome=revconfig|cache
 *                 revconfig_ui=<path>  revconfig_cache=<path>  interface_id=<n>
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
    /* [cache:boot] */
    int cache_kind;      /* enum AppCacheKind; -1 = unset */
    char cache_dir[512]; /* resolved against manifest dir */

    /* [net:boot] */
    char rev_name[32];
    char transport[16]; /* "tcp" | "ws"; "" = unset */
    char host[128];
    int port;           /* 0 = unset */
    int client_version; /* 0 = unset */
    char rsa_exp[256];
    char rsa_mod[256];
    int32_t jag_crc[9];
    int jag_crc_set;

    /* [ui:boot] */
    int ui_logic;  /* enum AppUiLogic; 0 = unset/default */
    int chrome;    /* 0 unset, 1 revconfig, 2 cache */
    char revconfig_ui[512];    /* resolved */
    char revconfig_cache[512]; /* resolved */
    int interface_id;          /* 0 = unset */
};

/* Zero the manifest and load `path`. Relative paths resolve against
 * dirname(path). Returns 0 on success, <0 on read/parse failure (a stderr line
 * names the offending section/key). Unknown keys warn but do not fail. */
int
BootManifest_LoadFile(struct BootManifest* bm, char const* path);

/* Copy the manifest's set fields into cfg. Only fields the manifest actually
 * provided are written, so calling this before CLI flag parsing lets explicit
 * flags override by plain assignment (precedence: CLI > manifest > defaults). */
void
BootManifest_ApplyToConfig(struct BootManifest const* bm, struct AppConfig* cfg);

#endif
