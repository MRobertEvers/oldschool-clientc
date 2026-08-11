#!/bin/sh
# Train the OSRS style classifier inside WSL.
# The dataset is copied to WSL-native disk first: DataLoader workers reading
# thousands of small PNGs through /mnt/c (9p) would bottleneck every epoch.
set -e
SRC=/mnt/c/Users/mrobe/Documents/git_repos/3d-raster/tools/osrs_stylizer
rm -rf /root/osrs_data
mkdir -p /root/osrs_data
cp -r "$SRC/data/osrs" /root/osrs_data/osrs
cp -r "$SRC/data/highpoly" /root/osrs_data/highpoly
echo "dataset staged: $(find /root/osrs_data -name '*.png' | wc -l) images"

cd "$SRC"
mkdir -p models
# 3 epochs: with ~40k images and augmentation the task converges in the first
# epoch or two on CPU; best-on-validation weights are kept regardless.
exec /root/osrs_venv/bin/python train_classifier.py \
    --data-root /root/osrs_data \
    --epochs 3 \
    --batch-size 64 \
    --out models/osrs_classifier.pt
