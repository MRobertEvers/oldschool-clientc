{
  "targets": [
    {
      "target_name": "cachelib_js",
      "sources": [
        "cachelib_addon.c",
        "../../platforms/platform_x/cachelib.c",
        "../../platforms/platform_x/cachelib_platform.c",
        "../../platforms/platform_x/cachelib_serialized.c",
        "../../../src/osrs/rscache/cache.c",
        "../../../src/osrs/rscache/cache_dat.c",
        "../../../src/osrs/rscache/xtea_config.c",
        "../../../src/osrs/rscache/reference_table.c",
        "../../../src/osrs/rscache/filelist.c",
        "../../../src/osrs/rscache/disk.c",
        "../../../src/osrs/rscache/rsbuf.c",
        "../../../src/osrs/rscache/archive.c",
        "../../../src/osrs/rscache/archive_decompress.c",
        "../../../src/osrs/rscache/compression.c",
        "../../../src/osrs/rscache/xtea.c",
        "../../../src/3rd/bzip/bzip.c",
        "../../../src/3rd/miniz/miniz.c",
        "../../../src/osrs/rscache/tables/string_utils.c",
        "../../../src/osrs/rscache/tables_dat/pix8.c",
        "../../../src/osrs/rscache/tables_dat/pix32.c",
        "../../../src/osrs/rscache/tables_dat/config_versionlist_mapsquare.c",
        "../../../src/osrs/rscache/tables_dat/config_textures.c",
        "../../../src/osrs/rscache/tables_dat/config_idk.c",
        "../../../src/osrs/rscache/tables_dat/config_npc.c",
        "../../../src/osrs/rscache/tables_dat/config_obj.c",
        "../../../src/osrs/rscache/tables_dat/config_spotanim.c",
        "../../../src/osrs/rscache/tables_dat/config_varp.c",
        "../../../src/osrs/rscache/tables_dat/animframe.c",
        "../../../src/osrs/rscache/tables/model.c"
      ],
      "include_dirs": [
        "../../..",
        "../../../src",
        "../../",
        "../../platforms"
      ],
      "cflags": [
        "-Wall",
        "-Wextra",
        "-std=c99"
      ],
      "conditions": [
        ["OS=='mac'", {
          "xcode_settings": {
            "OTHER_CFLAGS": [
              "-Wall",
              "-Wextra",
              "-std=c99"
            ]
          }
        }]
      ]
    }
  ]
}
