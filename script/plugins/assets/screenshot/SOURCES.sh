#!/bin/sh
# How the PNG beside this file was authored.
#
# Unlike the gameframe layout's art, these are not cut from a cache: no OSRS
# sprite table carries a front-view camera. The pixels are hand-authored in
# camera.txt and camera_small.txt, in the options_icons family's own colour
# ramp, and baked here. Two sizes, one drawing -- @see camera_small.txt.
#
#   sh script/plugins/assets/screenshot/SOURCES.sh
#
set -e
cd "$(dirname "$0")"
python3 bake.py camera.txt camera.png
python3 bake.py camera_small.txt camera_small.png
