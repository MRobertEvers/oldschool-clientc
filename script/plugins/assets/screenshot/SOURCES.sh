#!/bin/sh
# How the PNG beside this file was authored.
#
# Unlike the gameframe layout's art, this one is not cut from a cache: no OSRS
# sprite table carries a front-view camera. The pixels are hand-authored in
# camera.txt, in the options_icons family's own colour ramp, and baked here.
#
#   sh script/plugins/assets/screenshot/SOURCES.sh
#
set -e
cd "$(dirname "$0")"
python3 bake.py camera.txt camera.png
