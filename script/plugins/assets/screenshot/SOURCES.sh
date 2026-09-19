#!/bin/sh
# How the PNGs beside this file were authored.
#
# camera.png / camera_small.png are hand-authored: no OSRS sprite table
# carries a front-view camera. The pixels are in camera.txt and
# camera_small.txt, in the options_icons family's own colour ramp, and baked
# here. Two sizes, one drawing -- @see camera_small.txt.
#
# chat_button.png is NOT hand-authored and must not become so. It is the
# chatbox filter's own plate, cache sprite 3051, the picture the lane draws
# under every one of its eight filters -- the same bake the gameframe layout
# takes (osrs_chat_button, @see ../gameframe-layout/SOURCES.sh). It is copied
# rather than re-baked so the two cannot drift: one sprite id, one picture.
#
#   sh script/plugins/assets/screenshot/SOURCES.sh
#
set -e
cd "$(dirname "$0")"
python3 bake.py camera.txt camera.png
python3 bake.py camera_small.txt camera_small.png
cp ../gameframe-layout/osrs_chat_button.png chat_button.png
