#ifndef TORIRS_PLUGIN_FRAME_H
#define TORIRS_PLUGIN_FRAME_H

#include "plugin/torirs_plugin_v2.h"

#define TORIRS_PLUGIN_FRAME_OFFERS_MAX 32

enum PluginFrameCatalogResult
{
    PLUGIN_FRAME_CATALOG_OK = 0,
    PLUGIN_FRAME_CATALOG_INVALID,
    PLUGIN_FRAME_CATALOG_DUPLICATE,
    PLUGIN_FRAME_CATALOG_FULL,
};

struct PluginFrameCatalogEntry
{
    int plugin;
    char id[TORIRS_PLUGIN_FRAME_ID_MAX];
    char local_id[TORIRS_PLUGIN_FRAME_LOCAL_ID_MAX];
    char title[TORIRS_PLUGIN_TITLE_MAX];
    char provider[TORIRS_PLUGIN_NAME_MAX];
    int canvas;
    int width;
    int height;
    int available;
};

struct PluginFrameCatalog
{
    struct PluginFrameCatalogEntry entries[TORIRS_PLUGIN_FRAME_OFFERS_MAX];
    int count;
};

void PluginFrameCatalog_Init(struct PluginFrameCatalog* catalog);

/** Add every offer in a NULL-id-terminated provider array atomically. */
enum PluginFrameCatalogResult PluginFrameCatalog_Add(
    struct PluginFrameCatalog* catalog,
    int plugin,
    char const* provider,
    struct ToriRS_FrameOffer const* offers);

int PluginFrameCatalog_Count(struct PluginFrameCatalog const* catalog);

struct PluginFrameCatalogEntry const* PluginFrameCatalog_At(
    struct PluginFrameCatalog const* catalog,
    int index);

int PluginFrameCatalog_Find(
    struct PluginFrameCatalog const* catalog,
    char const* canonical_id);

void PluginFrameCatalog_SetAvailable(
    struct PluginFrameCatalog* catalog,
    int plugin,
    int available);

/** Remove a provider during an otherwise-failed atomic plugin registration. */
void PluginFrameCatalog_RemovePlugin(
    struct PluginFrameCatalog* catalog,
    int plugin);

#endif
