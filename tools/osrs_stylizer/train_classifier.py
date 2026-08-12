#!/usr/bin/env python3
"""Component 1 (training half): fine-tune a ResNet-18 to recognize OSRS style.

Expects a two-class ImageFolder layout:

    <data-root>/
        osrs/       renders of real OSRS assets   (positive class)
        highpoly/   renders of standard 3D assets (negative class)

Any image formats PIL can read are fine. Produces a checkpoint containing both
the model weights and the class-name -> index mapping so inference
(osrs_scorer.py) never has to guess which logit means "OSRS".

Usage:
    python train_classifier.py --data-root data --epochs 10 \
        --out models/osrs_classifier.pt
"""

import argparse
import copy
import os

import torch
import torch.nn as nn
from torch.utils.data import DataLoader, random_split
from torchvision import datasets, models, transforms

# ImageNet normalization constants — required because we start from
# ImageNet-pretrained ResNet-18 weights.
IMAGENET_MEAN = [0.485, 0.456, 0.406]
IMAGENET_STD = [0.229, 0.224, 0.225]


def build_model() -> nn.Module:
    """ResNet-18 pretrained on ImageNet, with the final FC swapped for a
    2-class head. We fine-tune the whole network: the dataset (game renders)
    is far from ImageNet's distribution, so frozen features underperform."""
    model = models.resnet18(weights=models.ResNet18_Weights.DEFAULT)
    model.fc = nn.Linear(model.fc.in_features, 2)
    return model


def build_loaders(data_root: str, batch_size: int, val_frac: float, seed: int):
    """Create train/val DataLoaders from a single ImageFolder root.

    Train gets augmentation (flips, small color jitter, random crop) to make
    the classifier robust to viewpoint/lighting differences between your OSRS
    asset viewer and the Blender renders it will judge at inference time.
    Val gets the deterministic resize used at inference.
    """
    train_tf = transforms.Compose([
        transforms.Resize(256),
        transforms.RandomResizedCrop(224, scale=(0.7, 1.0)),
        transforms.RandomHorizontalFlip(),
        transforms.ColorJitter(brightness=0.2, contrast=0.2, saturation=0.1),
        transforms.ToTensor(),
        transforms.Normalize(IMAGENET_MEAN, IMAGENET_STD),
    ])
    val_tf = transforms.Compose([
        transforms.Resize(256),
        transforms.CenterCrop(224),
        transforms.ToTensor(),
        transforms.Normalize(IMAGENET_MEAN, IMAGENET_STD),
    ])

    # Two ImageFolder instances over the same files so each split gets its own
    # transform; the split indices are shared via a seeded generator.
    train_ds_full = datasets.ImageFolder(data_root, transform=train_tf)
    val_ds_full = datasets.ImageFolder(data_root, transform=val_tf)
    if set(train_ds_full.class_to_idx) != {"osrs", "highpoly"}:
        raise SystemExit(
            f"Expected class subfolders 'osrs' and 'highpoly' under {data_root}, "
            f"found: {list(train_ds_full.class_to_idx)}"
        )

    n_total = len(train_ds_full)
    n_val = max(1, int(n_total * val_frac))
    gen = torch.Generator().manual_seed(seed)
    train_split, _ = random_split(train_ds_full, [n_total - n_val, n_val], generator=gen)
    gen = torch.Generator().manual_seed(seed)  # same seed -> same partition
    _, val_split = random_split(val_ds_full, [n_total - n_val, n_val], generator=gen)

    train_loader = DataLoader(train_split, batch_size=batch_size, shuffle=True,
                              num_workers=8, pin_memory=True)
    val_loader = DataLoader(val_split, batch_size=batch_size, shuffle=False,
                            num_workers=8, pin_memory=True)

    # Inverse-frequency class weights for the loss: the corpus is imbalanced
    # (more OSRS tiles than high-poly ones), and without weighting the network
    # could buy loss cheaply by favoring the majority class.
    targets = torch.tensor(train_ds_full.targets)
    counts = torch.bincount(targets, minlength=2).float()
    class_weights = counts.sum() / (2.0 * counts)
    return train_loader, val_loader, train_ds_full.class_to_idx, class_weights


def evaluate(model: nn.Module, loader: DataLoader, device: torch.device):
    """Return (overall top-1 accuracy, per-class accuracy dict by class index).
    Per-class matters here: the corpus is imbalanced (~2:1 OSRS:highpoly), so
    overall accuracy alone could hide a weak minority class."""
    model.eval()
    correct = total = 0
    per_class = {0: [0, 0], 1: [0, 0]}  # idx -> [correct, total]
    with torch.no_grad():
        for images, labels in loader:
            images, labels = images.to(device), labels.to(device)
            preds = model(images).argmax(dim=1)
            correct += (preds == labels).sum().item()
            total += labels.numel()
            for cls in (0, 1):
                mask = labels == cls
                per_class[cls][0] += (preds[mask] == cls).sum().item()
                per_class[cls][1] += int(mask.sum().item())
    acc = correct / max(total, 1)
    cls_acc = {c: per_class[c][0] / max(per_class[c][1], 1) for c in per_class}
    return acc, cls_acc


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--data-root", default="data",
                    help="folder containing osrs/ and highpoly/ subfolders")
    ap.add_argument("--out", default="models/osrs_classifier.pt")
    ap.add_argument("--epochs", type=int, default=10)
    ap.add_argument("--batch-size", type=int, default=32)
    ap.add_argument("--lr", type=float, default=3e-4)
    ap.add_argument("--val-frac", type=float, default=0.1)
    ap.add_argument("--seed", type=int, default=1337)
    args = ap.parse_args()

    torch.manual_seed(args.seed)
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    print(f"Training on {device}")

    train_loader, val_loader, class_to_idx, class_weights = build_loaders(
        args.data_root, args.batch_size, args.val_frac, args.seed)
    print(f"Classes: {class_to_idx} | "
          f"{len(train_loader.dataset)} train / {len(val_loader.dataset)} val images | "
          f"loss weights {class_weights.tolist()}", flush=True)

    model = build_model().to(device)
    criterion = nn.CrossEntropyLoss(weight=class_weights.to(device))
    optimizer = torch.optim.AdamW(model.parameters(), lr=args.lr, weight_decay=1e-4)
    scheduler = torch.optim.lr_scheduler.CosineAnnealingLR(optimizer, T_max=args.epochs)

    best_acc = 0.0
    best_state = copy.deepcopy(model.state_dict())
    for epoch in range(1, args.epochs + 1):
        model.train()
        running_loss = 0.0
        for images, labels in train_loader:
            images, labels = images.to(device), labels.to(device)
            optimizer.zero_grad(set_to_none=True)
            loss = criterion(model(images), labels)
            loss.backward()
            optimizer.step()
            running_loss += loss.item() * labels.numel()
        scheduler.step()

        val_acc, cls_acc = evaluate(model, val_loader, device)
        idx_to_class = {v: k for k, v in class_to_idx.items()}
        per_cls = "  ".join(f"{idx_to_class[c]}={a:.3%}" for c, a in cls_acc.items())
        print(f"epoch {epoch:2d}/{args.epochs}  "
              f"loss={running_loss / len(train_loader.dataset):.4f}  "
              f"val_acc={val_acc:.3%}  ({per_cls})", flush=True)
        # Keep the best-on-val weights, not the last epoch — small datasets
        # overfit fast and the evaluator's calibration matters downstream.
        if val_acc >= best_acc:
            best_acc = val_acc
            best_state = copy.deepcopy(model.state_dict())

    os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)
    torch.save({
        "state_dict": best_state,
        "class_to_idx": class_to_idx,   # lets osrs_scorer find the OSRS logit
        "arch": "resnet18",
        "val_acc": best_acc,
    }, args.out)
    print(f"Saved best checkpoint (val_acc={best_acc:.3%}) -> {args.out}")


if __name__ == "__main__":
    main()
