#!/bin/sh
# Train the OSRS style classifier inside WSL.
#
# Only needed when the training environment lives in WSL. With a native
# PyTorch install (Windows, macOS, Linux) just run train_classifier.py
# directly against ./data — there is nothing to stage:
#
#     python train_classifier.py --data-root data --epochs 3 \
#         --batch-size 64 --out models/osrs_classifier.pt
#
# The dataset is copied to WSL-native disk first: DataLoader workers reading
# tens of thousands of small PNGs through /mnt/c (9p) would bottleneck every
# epoch.
set -e
# Derived from this script's own location, not hardcoded — the tool has to
# keep working when the repo is cloned somewhere else.
SRC=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
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
