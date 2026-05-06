-- Rev245_2 cache loading helpers.
--
-- Wraps the load -> cache_add -> convert pipeline for asset types that appear
-- repeatedly across packet handlers (models, animation base frames, spotanims).
-- Each function is idempotent: already-cached assets are skipped, and the
-- paired GameCache convert is always called so the caller does not need to.
--
-- Config-table loads (jagfile init + convert_*_from_buildcachedat for npcs,
-- objs, idks, sequences, floortypes, etc.) are intentionally NOT abstracted
-- here; those have packet-specific ordering constraints and belong inline.

local CacheDat = require("cachedat")
local M = {}

-- Load, add, and convert a batch of model IDs.
-- Deduplicates across calls and skips IDs already in the model cache.
-- model_ids: array of integer model IDs; nil and 0 entries are ignored.
function M.load_models(model_ids)
    local seen = {}
    local requests = {}
    local needed = {}
    for _, id in ipairs(model_ids) do
        if id and id > 0 and not seen[id] then
            seen[id] = true
            if not Game.BuildCacheDat.model_cache_has(id) then
                requests[#requests + 1] = {
                    table_id = CacheDat.Tables.CACHE_DAT_MODELS,
                    archive_id = id,
                    flags = 0,
                }
                needed[#needed + 1] = id
            end
        end
    end
    if #requests > 0 then
        local archives = CacheDat.load_archives(requests)
        for i, id in ipairs(needed) do
            Game.BuildCacheDat.model_cache_add(archives[i], id)
        end
    end
    Game.GameCache.convert_models_chunk_from_buildcachedat(0, 0)
end

-- Collect model IDs from multiple getter call descriptors into one
-- deduplicated batch, then call load_models.
-- sources: array of { getter_fn, arg1, arg2, ... } where
--          getter_fn(arg1, arg2, ...) returns an array of model IDs.
function M.load_models_from_sources(sources)
    local all_ids = {}
    for _, src in ipairs(sources) do
        local getter = src[1]
        for _, mid in ipairs(getter(table.unpack(src, 2))) do
            all_ids[#all_ids + 1] = mid
        end
    end
    M.load_models(all_ids)
end

-- Load animation base frames for a single sequence and convert.
-- Skips frame IDs already in the anim cache.
function M.load_anims_for_seq(seq_id)
    if not seq_id or seq_id < 0 then
        return
    end
    local frame_ids = Game.BuildCacheDat.get_sequence_animbaseframes_ids(seq_id)
    local requests = {}
    for _, fid in ipairs(frame_ids) do
        if not Game.BuildCacheDat.animbaseframes_cache_has(fid) then
            requests[#requests + 1] = {
                table_id = CacheDat.Tables.CACHE_DAT_ANIMATIONS,
                archive_id = fid,
                flags = 0,
            }
        end
    end
    if #requests > 0 then
        local archives = CacheDat.load_archives(requests)
        for _, archive in ipairs(archives) do
            Game.BuildCacheDat.animbaseframes_cache_add(archive)
        end
    end
    Game.GameCache.convert_animbaseframes_from_buildcachedat()
end

-- Load and convert all assets needed for a spotanim:
--   its model, its sequence's animation base frames, and the spotanim configs.
function M.load_spotanim(spotanim_id)
    local model_id = Game.BuildCacheDat.get_spotanim_model_id(spotanim_id)
    if model_id and model_id > 0 and not Game.BuildCacheDat.model_cache_has(model_id) then
        local archive = CacheDat.load_archive(CacheDat.Tables.CACHE_DAT_MODELS, model_id, 0)
        Game.BuildCacheDat.model_cache_add(archive, model_id)
    end
    Game.GameCache.convert_models_chunk_from_buildcachedat(0, 0)

    M.load_anims_for_seq(Game.BuildCacheDat.get_spotanim_seq_id(spotanim_id))
    Game.GameCache.convert_spotanims_from_buildcachedat()
end

return M
