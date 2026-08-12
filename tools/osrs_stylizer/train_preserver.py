#!/usr/bin/env python3
"""Preserver component 4: train the Content Preserver with triplet loss.

    python train_preserver.py --csv data/preserver/dataset.csv \
        --epochs 15 --out models/content_preserver.pt

Loss is nn.TripletMarginLoss(margin=1.0, p=2) over L2-normalized embeddings.
On the unit sphere the anchor-negative distance tops out at 2.0, so a margin
of 1.0 is a demanding target: destroyed meshes must land at least half the
sphere's diameter further from the anchor than safe stylizations do. That
aggression is intentional — the optimizer consumes this metric, and a wide
trained gap is what keeps the 0..100 score discriminative.

Model selection tracks TRIPLET ACCURACY on held-out models (the fraction of
validation triplets with d(a,p) < d(a,n)) rather than raw loss: accuracy is
exactly the ranking property the optimizer relies on, while loss can shrink
by widening already-correct triplets without fixing broken ones.
"""

import argparse
import copy

import torch
import torch.nn as nn
from torch.utils.data import DataLoader, Subset

from preserver_dataset import (TripletDataset, eval_transform,
                               split_indices_by_model, train_transform)
from preserver_model import ContentPreserver, save_checkpoint


def build_loaders(csv_path: str, batch_size: int, val_frac: float, seed: int,
                  num_workers: int) -> tuple[DataLoader, DataLoader]:
    """Train/val DataLoaders over the same CSV, split by source model (no
    view leakage), each side wearing its own transform — the same two-
    instances pattern train_classifier.py uses."""
    train_ds = TripletDataset(csv_path, transform=train_transform())
    val_ds = TripletDataset(csv_path, transform=eval_transform())
    train_idx, val_idx = split_indices_by_model(train_ds.model_names,
                                                val_frac, seed)
    train_loader = DataLoader(Subset(train_ds, train_idx),
                              batch_size=batch_size, shuffle=True,
                              num_workers=num_workers, pin_memory=True)
    val_loader = DataLoader(Subset(val_ds, val_idx),
                            batch_size=batch_size, shuffle=False,
                            num_workers=num_workers, pin_memory=True)
    return train_loader, val_loader


@torch.no_grad()
def evaluate(model: ContentPreserver, loader: DataLoader,
             criterion: nn.TripletMarginLoss,
             device: torch.device) -> tuple[float, float]:
    """Return (mean triplet loss, triplet accuracy) over a loader."""
    model.eval()
    total_loss = correct = total = 0
    for anchor, positive, negative in loader:
        anchor = anchor.to(device, non_blocking=True)
        positive = positive.to(device, non_blocking=True)
        negative = negative.to(device, non_blocking=True)
        za, zp, zn = model(anchor), model(positive), model(negative)
        total_loss += criterion(za, zp, zn).item() * len(anchor)
        d_ap = (za - zp).norm(dim=-1)
        d_an = (za - zn).norm(dim=-1)
        correct += (d_ap < d_an).sum().item()
        total += len(anchor)
    return total_loss / max(total, 1), correct / max(total, 1)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--csv", default="data/preserver/dataset.csv",
                    help="triplet manifest from gen_preserver_triplets.py")
    ap.add_argument("--out", default="models/content_preserver.pt")
    ap.add_argument("--epochs", type=int, default=15)
    ap.add_argument("--batch-size", type=int, default=32)
    ap.add_argument("--lr", type=float, default=1e-4)
    ap.add_argument("--margin", type=float, default=1.0)
    ap.add_argument("--embed-dim", type=int, default=256)
    ap.add_argument("--val-frac", type=float, default=0.1)
    ap.add_argument("--num-workers", type=int, default=4)
    ap.add_argument("--seed", type=int, default=1337)
    args = ap.parse_args()

    torch.manual_seed(args.seed)
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    print(f"Training on {device}")

    train_loader, val_loader = build_loaders(
        args.csv, args.batch_size, args.val_frac, args.seed, args.num_workers)
    print(f"{len(train_loader.dataset)} train / {len(val_loader.dataset)} val "
          f"triplets (split by source model)", flush=True)

    model = ContentPreserver(embed_dim=args.embed_dim, pretrained=True).to(device)
    criterion = nn.TripletMarginLoss(margin=args.margin, p=2)
    optimizer = torch.optim.Adam(model.parameters(), lr=args.lr)
    scheduler = torch.optim.lr_scheduler.CosineAnnealingLR(optimizer,
                                                           T_max=args.epochs)

    best_acc = -1.0
    best_loss = float("inf")
    best_state = copy.deepcopy(model.state_dict())
    for epoch in range(1, args.epochs + 1):
        model.train()
        running_loss = 0.0
        for anchor, positive, negative in train_loader:
            anchor = anchor.to(device, non_blocking=True)
            positive = positive.to(device, non_blocking=True)
            negative = negative.to(device, non_blocking=True)
            optimizer.zero_grad(set_to_none=True)
            # One shared tower, three passes — that weight sharing IS the
            # Siamese architecture; the loss only ever compares embeddings.
            loss = criterion(model(anchor), model(positive), model(negative))
            loss.backward()
            optimizer.step()
            running_loss += loss.item() * len(anchor)
        scheduler.step()

        train_loss = running_loss / max(len(train_loader.dataset), 1)
        val_loss, val_acc = evaluate(model, val_loader, criterion, device)
        print(f"epoch {epoch:2d}/{args.epochs}  train_loss={train_loss:.4f}  "
              f"val_loss={val_loss:.4f}  val_triplet_acc={val_acc:.3%}",
              flush=True)

        # Best-on-validation, accuracy first, loss as the tie-break (accuracy
        # saturates quickly on an easy corpus; loss keeps separating margins).
        if (val_acc, -val_loss) >= (best_acc, -best_loss):
            best_acc, best_loss = val_acc, val_loss
            best_state = copy.deepcopy(model.state_dict())

    model.load_state_dict(best_state)
    save_checkpoint(args.out, model, margin=args.margin,
                    val_triplet_acc=best_acc, val_loss=best_loss)
    print(f"Saved best checkpoint (val_triplet_acc={best_acc:.3%}) "
          f"-> {args.out}")


if __name__ == "__main__":
    main()
