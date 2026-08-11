#!/bin/sh
# One-time WSL training-environment setup: venv on the WSL-native filesystem
# (fast I/O; /root persists, unlike tmpfs /tmp) with CPU PyTorch wheels.
set -e
python3 -m venv /root/osrs_venv
/root/osrs_venv/bin/pip install --quiet --upgrade pip
/root/osrs_venv/bin/pip install --quiet torch torchvision --index-url https://download.pytorch.org/whl/cpu
/root/osrs_venv/bin/pip install --quiet pillow
/root/osrs_venv/bin/python - <<'EOF'
import torch, torchvision
print("torch", torch.__version__, "| torchvision", torchvision.__version__,
      "| cuda", torch.cuda.is_available())
EOF
echo ENV_READY
