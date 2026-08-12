#!/usr/bin/env python3
"""Preserver component 2: the triplet Dataset + the shared image transforms.

Reads the dataset.csv written by gen_preserver_triplets.py (columns
anchor_path, positive_path, negative_path; paths relative to the CSV) and
serves (anchor, positive, negative) tensor triplets.

The transforms live here — not in the training script — because they are the
model's input contract: preserver_scorer.py must preprocess inference images
exactly like the validation pipeline did, or the scores silently drift.
"""

import csv
import os

import torch
from PIL import Image
from torch.utils.data import Dataset
from torchvision import transforms

# ImageNet normalization constants — required because the embedder starts
# from ImageNet-pretrained ResNet-18 weights.
IMAGENET_MEAN = [0.485, 0.456, 0.406]
IMAGENET_STD = [0.229, 0.224, 0.225]


def train_transform() -> transforms.Compose:
    """Training-time augmentation. Deliberately MILD, and each triplet member
    is augmented independently: the network must tolerate small framing and
    color differences (they occur between baseline and trial renders), but
    aggressive crops/flips applied to only one member would break the
    correspondence the triplet is supposed to teach. No horizontal flip for
    the same reason — a flipped anchor vs unflipped positive is a harder
    match than anything inference ever produces."""
    return transforms.Compose([
        transforms.Resize(256),
        transforms.RandomResizedCrop(224, scale=(0.85, 1.0)),
        transforms.ColorJitter(brightness=0.15, contrast=0.15, saturation=0.05),
        transforms.ToTensor(),
        transforms.Normalize(IMAGENET_MEAN, IMAGENET_STD),
    ])


def eval_transform() -> transforms.Compose:
    """Deterministic pipeline used for validation AND inference (the scorer)."""
    return transforms.Compose([
        transforms.Resize(256),
        transforms.CenterCrop(224),
        transforms.ToTensor(),
        transforms.Normalize(IMAGENET_MEAN, IMAGENET_STD),
    ])


class TripletDataset(Dataset):
    """(anchor, positive, negative) image triplets from dataset.csv.

    Exposes `rows` (resolved absolute paths) and `model_names` (the per-row
    source-model directory name) so the training script can split train/val
    BY MODEL — splitting by row would leak the other two views of every
    validation model into training.
    """

    def __init__(self, csv_path: str,
                 transform: transforms.Compose | None = None) -> None:
        self.transform = transform if transform is not None else eval_transform()
        base = os.path.dirname(os.path.abspath(csv_path))
        self.rows: list[tuple[str, str, str]] = []
        with open(csv_path, encoding="utf-8", newline="") as fh:
            reader = csv.DictReader(fh)
            for row in reader:
                self.rows.append(tuple(
                    os.path.normpath(os.path.join(base, row[col]))
                    for col in ("anchor_path", "positive_path", "negative_path")))
        if not self.rows:
            raise SystemExit(f"no triplets in {csv_path} — "
                             "run gen_preserver_triplets.py first")
        # renders/<model>/anchor_front.png -> "<model>"
        self.model_names = [os.path.basename(os.path.dirname(r[0]))
                            for r in self.rows]

    def __len__(self) -> int:
        return len(self.rows)

    def _load(self, path: str) -> torch.Tensor:
        with Image.open(path) as im:
            return self.transform(im.convert("RGB"))

    def __getitem__(self, idx: int) -> tuple[torch.Tensor, torch.Tensor, torch.Tensor]:
        anchor, positive, negative = self.rows[idx]
        return self._load(anchor), self._load(positive), self._load(negative)


def split_indices_by_model(model_names: list[str], val_frac: float,
                           seed: int) -> tuple[list[int], list[int]]:
    """Seeded train/val split at MODEL granularity: all views of one source
    model land on the same side, so validation measures generalization to
    unseen objects rather than unseen camera angles of memorized ones."""
    unique = sorted(set(model_names))
    gen = torch.Generator().manual_seed(seed)
    order = torch.randperm(len(unique), generator=gen).tolist()
    n_val = max(1, int(len(unique) * val_frac))
    val_models = {unique[i] for i in order[:n_val]}
    train_idx = [i for i, m in enumerate(model_names) if m not in val_models]
    val_idx = [i for i, m in enumerate(model_names) if m in val_models]
    return train_idx, val_idx
