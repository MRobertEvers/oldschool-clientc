#!/usr/bin/env python3
"""Preserver component 3: the Siamese embedding network.

One ResNet-18 tower (so ~11.4M parameters total — "Siamese" means the anchor,
positive, and negative all pass through the SAME weights, not three copies)
with the ImageNet classification head replaced by a small projection head,
and every output embedding L2-normalized onto the unit sphere.

The unit sphere matters twice over: it makes cosine similarity and Euclidean
distance interchangeable (d^2 = 2 - 2*cos), and it bounds the distance range
to [0, 2] so the triplet margin and the scorer's 0..100 mapping both have a
fixed, checkpoint-independent geometry to work against.
"""

import os

import torch
import torch.nn as nn
import torch.nn.functional as F
from torchvision import models

DEFAULT_EMBED_DIM = 256


class ContentPreserver(nn.Module):
    """ResNet-18 backbone -> MLP projection head -> L2-normalized embedding."""

    def __init__(self, embed_dim: int = DEFAULT_EMBED_DIM,
                 pretrained: bool = True) -> None:
        super().__init__()
        weights = models.ResNet18_Weights.DEFAULT if pretrained else None
        backbone = models.resnet18(weights=weights)
        feat_dim = backbone.fc.in_features        # 512 for ResNet-18
        backbone.fc = nn.Identity()               # strip the ImageNet classifier
        self.backbone = backbone
        # A hidden layer (rather than a bare Linear) lets the head re-mix the
        # backbone features for the metric task instead of just rotating them.
        self.head = nn.Sequential(
            nn.Linear(feat_dim, feat_dim),
            nn.ReLU(inplace=True),
            nn.Linear(feat_dim, embed_dim),
        )
        self.embed_dim = embed_dim

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        """(B, 3, 224, 224) image batch -> (B, embed_dim) unit vectors."""
        z = self.head(self.backbone(x))
        return F.normalize(z, p=2, dim=-1)


def save_checkpoint(path: str, model: ContentPreserver, **metadata) -> None:
    """Checkpoint = weights + everything inference needs to rebuild the model
    without guessing (mirrors the osrs_classifier.pt convention)."""
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    torch.save({
        "state_dict": model.state_dict(),
        "arch": "resnet18-siamese",
        "embed_dim": model.embed_dim,
        **metadata,
    }, path)


def load_checkpoint(path: str, device: torch.device) -> ContentPreserver:
    """Rebuild the embedder from a train_preserver.py checkpoint, in eval
    mode on `device`. pretrained=False: every weight comes from the file, so
    there is no reason to download ImageNet weights first."""
    ckpt = torch.load(path, map_location=device)
    model = ContentPreserver(embed_dim=ckpt["embed_dim"], pretrained=False)
    model.load_state_dict(ckpt["state_dict"])
    return model.to(device).eval()
