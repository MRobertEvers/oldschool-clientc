#!/usr/bin/env python3
"""Preserver component 5: get_identity_score() — the trained content judge.

Drop-in replacement for content_scorer.get_content_score, but purpose-trained:
where CLIP scores generic semantic similarity, this network was taught on
this exact pipeline's renders that palette baking and moderate decimation are
FINE while mesh collapse, tearing, and inverted-normal soup are NOT.

The function is built to sit inside the optimizer's hot loop (hundreds of
calls per study):
- the checkpoint is loaded once per process and cached;
- embeddings are memoized by (path, mtime), which makes the repeated anchor
  side of every comparison free after the first call.

Score semantics: embeddings live on the unit sphere, so the Euclidean
distance d between two of them is bounded to [0, 2]. The score is the linear
map (1 - d/2) * 100 — 100.0 means identical embeddings (perfect identity
preservation), 0.0 means antipodal (complete topological destruction). With
the default training margin of 1.0, a mesh the network considers destroyed
scores <= ~50 while safe stylizations sit near 100.

Usage as a module (see optimize.py --content-scorer preserver) or standalone:

    python preserver_scorer.py baseline_front.png trial_front.png
"""

import os
import sys
from collections import OrderedDict
from functools import lru_cache

import torch
from PIL import Image

from preserver_dataset import eval_transform
from preserver_model import ContentPreserver, load_checkpoint

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_CHECKPOINT = os.path.join(HERE, "models", "content_preserver.pt")

# Memoized embeddings, keyed by (abspath, mtime, model id). Bounded: trial
# renders are one-shot, only the handful of baseline anchors actually repeat.
_EMBED_CACHE: OrderedDict[tuple, torch.Tensor] = OrderedDict()
_EMBED_CACHE_MAX = 64


@lru_cache(maxsize=2)
def _load(checkpoint_path: str, device_str: str) -> tuple[ContentPreserver, torch.device]:
    """Load the trained embedder once per process (same pattern as the CLIP
    and style scorers — the Optuna loop pays the load cost on trial 0 only)."""
    device = torch.device(device_str)
    return load_checkpoint(checkpoint_path, device), device


_TRANSFORM = eval_transform()   # deterministic; identical to validation


def _embed(image_path: str, model: ContentPreserver,
           device: torch.device) -> torch.Tensor:
    """L2-normalized embedding of one image, memoized by (path, mtime)."""
    path = os.path.abspath(image_path)
    key = (path, os.path.getmtime(path), id(model))
    cached = _EMBED_CACHE.get(key)
    if cached is not None:
        _EMBED_CACHE.move_to_end(key)
        return cached
    with Image.open(path) as im:
        batch = _TRANSFORM(im.convert("RGB")).unsqueeze(0).to(device)
    with torch.no_grad():
        embedding = model(batch).squeeze(0)   # forward() already normalizes
    _EMBED_CACHE[key] = embedding
    while len(_EMBED_CACHE) > _EMBED_CACHE_MAX:
        _EMBED_CACHE.popitem(last=False)
    return embedding


def get_identity_score(anchor_img_path: str, candidate_img_path: str,
                       model: ContentPreserver | None = None,
                       device: torch.device | str | None = None,
                       checkpoint_path: str = DEFAULT_CHECKPOINT) -> float:
    """Structural-identity preservation score in [0.0, 100.0].

    100.0 = the candidate render is embedding-identical to the anchor
    (perfect preservation); 0.0 = maximally far on the unit sphere (complete
    destruction). Pass `model`/`device` to reuse an embedder you already
    loaded; omit them and the default checkpoint is loaded and cached
    automatically, so the optimizer can call this exactly like
    get_content_score(baseline, render).
    """
    if model is None:
        if device is None:
            device = "cuda" if torch.cuda.is_available() else "cpu"
        model, device = _load(checkpoint_path, str(device))
    elif device is None:
        device = next(model.parameters()).device
    device = torch.device(device)

    a = _embed(anchor_img_path, model, device)
    b = _embed(candidate_img_path, model, device)
    distance = float((a - b).norm())          # in [0, 2] on the unit sphere
    score = (1.0 - distance / 2.0) * 100.0
    return max(0.0, min(100.0, score))        # guard float round-off drift


if __name__ == "__main__":
    if len(sys.argv) not in (3, 4):
        raise SystemExit(
            f"usage: {sys.argv[0]} <anchor.png> <candidate.png> [checkpoint.pt]")
    ckpt = sys.argv[3] if len(sys.argv) == 4 else DEFAULT_CHECKPOINT
    print(f"{get_identity_score(sys.argv[1], sys.argv[2], checkpoint_path=ckpt):.2f}")
