#!/usr/bin/env python3
"""Component 1 (inference half): get_osrs_score(image_path) -> 0..100.

Loads the checkpoint produced by train_classifier.py once (lazily, cached at
module level) and returns the softmax probability of the 'osrs' class as a
percentage. Import this from the optimizer; it is also runnable directly:

    python osrs_scorer.py render.png [more.png ...]
"""

import sys
from functools import lru_cache

import torch
import torch.nn as nn
from PIL import Image
from torchvision import models, transforms

DEFAULT_CHECKPOINT = "models/osrs_classifier.pt"

# Must match the *validation* transform used in train_classifier.py exactly —
# a train/inference preprocessing mismatch silently wrecks calibration.
_PREPROCESS = transforms.Compose([
    transforms.Resize(256),
    transforms.CenterCrop(224),
    transforms.ToTensor(),
    transforms.Normalize([0.485, 0.456, 0.406], [0.229, 0.224, 0.225]),
])


@lru_cache(maxsize=2)
def _load(checkpoint_path: str, device_str: str):
    """Build the model once per (checkpoint, device) pair. lru_cache makes
    repeated calls from the Optuna loop free after the first."""
    device = torch.device(device_str)
    ckpt = torch.load(checkpoint_path, map_location=device)

    model = models.resnet18(weights=None)  # weights come from the checkpoint
    model.fc = nn.Linear(model.fc.in_features, 2)
    model.load_state_dict(ckpt["state_dict"])
    model.to(device).eval()

    osrs_idx = ckpt["class_to_idx"]["osrs"]  # which logit means "looks OSRS"
    return model, osrs_idx, device


def get_osrs_score(image_path: str,
                   checkpoint_path: str = DEFAULT_CHECKPOINT,
                   device: str | None = None) -> float:
    """Return the classifier's confidence (0..100) that `image_path` matches
    the OSRS art style."""
    if device is None:
        device = "cuda" if torch.cuda.is_available() else "cpu"
    model, osrs_idx, dev = _load(checkpoint_path, device)

    # Convert to RGB so grayscale/palette/alpha PNGs all normalize the same way.
    image = Image.open(image_path).convert("RGB")
    batch = _PREPROCESS(image).unsqueeze(0).to(dev)

    with torch.no_grad():
        probs = torch.softmax(model(batch), dim=1)[0]
    return float(probs[osrs_idx].item()) * 100.0


if __name__ == "__main__":
    if len(sys.argv) < 2:
        raise SystemExit(f"usage: {sys.argv[0]} <image> [<image> ...]")
    for path in sys.argv[1:]:
        print(f"{path}: {get_osrs_score(path):.2f}")
