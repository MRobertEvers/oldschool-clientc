#!/bin/sh
# How every PNG beside this file was authored, and from which cache.
#
# Run from the repo root. It rewrites the whole set, so it is also the way to
# re-cut the art after a cache is replaced -- the files are the plugin's now,
# but they are not hand-drawn, and a folder of pictures with no provenance is
# one nobody can correct.
#
#   make -C tools/dump_sprites
#   sh script/plugins/assets/gameframe-layout/SOURCES.sh
#
# Two caches, because the plugin offers two FAMILIES of frame and each one's
# art belongs to the era it came from:
#
#   classic_*  cache254.lostcity, the dat1 media jagfile -- the 2004 gameframe.
#   osrs_*     cache.osrs239 sprite table -- the OldSchool gameframe, both the
#              fixed (548) chrome and the resizable (161/164) tab strips.
#
# The osrs ids are the ones revconfig/osrs_static/osrs_static_ui_cache.ini
# already names symbolically; they are repeated here rather than read from it
# because that file addresses a cache at runtime and this one produces files.
set -e
cd "$(dirname "$0")/../../../.."
OUT=script/plugins/assets/gameframe-layout
DUMP=tools/dump_sprites/dump_sprites

"$DUMP" --dat2 --rev osrs239 cache.osrs239 --out "$OUT" \
  osrs_sideicon_0=774 osrs_sideicon_1=775 osrs_sideicon_2=776 osrs_sideicon_3=777 \
  osrs_sideicon_4=778 osrs_sideicon_5=779 osrs_sideicon_6=780 osrs_sideicon_7=781 \
  osrs_sideicon_8=782 osrs_sideicon_9=783 osrs_sideicon_10=784 osrs_sideicon_11=785 \
  osrs_sideicon_12=786 osrs_sideicon_13=787 \
  osrs_stone_tl=1026 osrs_stone_tr=1027 osrs_stone_bl=1028 osrs_stone_br=1029 \
  osrs_stone_mid=1030 osrs_stone_mid_r=1180 osrs_stone_mid_r2=1181 \
  osrs_invback=297 osrs_chatback=1017 osrs_chat_stones=1018 osrs_chat_button=3051 osrs_chat_button_hover=3052 osrs_chat_button_active=3053 \
  osrs_chat_button_active_hover=3054 osrs_chat_button_report=3056 \
  osrs_mapback=1182 osrs_compass=169 \
  osrs_side_panel=1031 osrs_side_panel_r=897 \
  osrs_tabs_top=1036 osrs_tabs_bottom=1032 osrs_tabs_top_r=1173 osrs_tabs_bottom_r=1174 \
  osrs_minimap_mask=1183 osrs_compass_mask=1184 \
  osrs_mapback_r=1177 osrs_minimap_mask_r=1178 osrs_compass_mask_r=1179 \
  osrs_side_column_l=1175 osrs_side_column_r=1176 \
  osrs_sb_trough=792 osrs_sb_dragger_top=789 osrs_sb_dragger_mid=790 \
  osrs_sb_dragger_bottom=791 osrs_sb_arrow_up=773 osrs_sb_arrow_down=788 \
  osrs_backleft1=4 osrs_backleft2=1034 osrs_backright1=1035 osrs_backtop1=1039 \
  osrs_backtop_right=1441 osrs_backright_top=1038 osrs_backvmid1=1037 \
  osrs_backvmid2=1033 osrs_backhmid1=1611

"$DUMP" --dat1 cache254.lostcity --out "$OUT" \
  classic_invback=invback.dat classic_chatback=chatback.dat classic_mapback=mapback.dat \
  classic_backbase1=backbase1.dat classic_backbase2=backbase2.dat \
  classic_backleft1=backleft1.dat classic_backleft2=backleft2.dat \
  classic_backright1=backright1.dat classic_backright2=backright2.dat \
  classic_backtop1=backtop1.dat \
  classic_backvmid1=backvmid1.dat classic_backvmid2=backvmid2.dat \
  classic_backvmid3=backvmid3.dat \
  classic_backhmid1=backhmid1.dat classic_backhmid2=backhmid2.dat \
  classic_compass=compass.dat \
  classic_redstone1=redstone1.dat classic_redstone2=redstone2.dat \
  classic_redstone3=redstone3.dat \
  classic_sideicon=sideicons.dat:0-12
