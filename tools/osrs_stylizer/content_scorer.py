#!/usr/bin/env python3
"""Component 3: content preservation via CLIP image-embedding similarity.

get_content_score(original, modified) returns the cosine similarity between
the CLIP embeddings of the two renders. This is the guard rail: the style
classifier alone is happiest with a flat brown blob (the mathematical extreme
of "retro low poly"), so the optimizer must ALSO keep this score high, which
forces the stylized model to remain recognizably the same object.

CLIP is a good fit here because its embeddings capture semantic/structural
content while being fairly tolerant of exactly the changes we WANT to allow
(fewer polygons, coarser colors, flat shading).

Usage as a module (from optimize.py) or standalone:

    python content_scorer.py baseline_front.png trial_front.png
"""

import sys
from functools import lru_cache

import torch
from PIL import Image
from transformers import CLIPModel, CLIPProcessor

# ViT-B/32: the smallest standard CLIP — plenty for same-object similarity,
# and fast enough to sit inside an optimization loop.
MODEL_NAME = "openai/clip-vit-base-patch32"


@lru_cache(maxsize=1)
def _load(device_str: str):
    """Load CLIP once per process; cached so the Optuna loop pays the
    (multi-second) load cost only on the first trial."""
    device = torch.device(device_str)
    model = CLIPModel.from_pretrained(MODEL_NAME).to(device).eval()
    processor = CLIPProcessor.from_pretrained(MODEL_NAME)
    return model, processor, device


def _embed(image_path: str, device: str) -> torch.Tensor:
    """L2-normalized CLIP image embedding for a single image."""
    model, processor, dev = _load(device)
    image = Image.open(image_path).convert("RGB")
    inputs = processor(images=image, return_tensors="pt").to(dev)
    with torch.no_grad():
        features = model.get_image_features(**inputs)
    return features / features.norm(dim=-1, keepdim=True)


def get_content_score(original_render_path: str,
                      modified_render_path: str,
                      device: str | None = None) -> float:
    """Cosine similarity between the CLIP embeddings of the original and the
    stylized render. Range is [-1, 1] in theory; in practice two renders of
    the same object land around 0.85-0.99 and unrelated blobs around 0.5-0.7,
    so treat it as a relative signal, not an absolute percentage."""
    if device is None:
        device = "cuda" if torch.cuda.is_available() else "cpu"
    a = _embed(original_render_path, device)
    b = _embed(modified_render_path, device)
    return float((a @ b.T).item())


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit(f"usage: {sys.argv[0]} <original.png> <modified.png>")
    print(f"{get_content_score(sys.argv[1], sys.argv[2]):.4f}")
