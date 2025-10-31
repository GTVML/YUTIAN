#!/usr/bin/env bash
set -euo pipefail

# FFGLModelFX deploy script (macOS)
# - Copies build-make-release/ModelFXFFGL.bundle to common FFGL plugin folders
# - Backs up existing bundles with timestamp suffix
# - Removes quarantine attributes to avoid macOS Gatekeeper warnings

# Resolve directories
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/build-make-release"
BUNDLE_NAME="ModelFXFFGL.bundle"
SRC_BUNDLE="$BUILD_DIR/$BUNDLE_NAME"

if [[ ! -d "$SRC_BUNDLE" ]]; then
  echo "[ERROR] Source bundle not found: $SRC_BUNDLE"
  echo "        Build it first, e.g.: cmake --build '$BUILD_DIR' --parallel"
  exit 1
fi

# Defaults
DEST_A="$HOME/Documents/Resolume/Extra Effects"
DEST_B="$HOME/Library/Graphics/FreeFrame Plug-Ins"
CUSTOM_DEST=""

# Parse args
while [[ $# -gt 0 ]]; do
  case "$1" in
    --dest)
      shift
      CUSTOM_DEST="${1:-}"
      ;;
    --help|-h)
      cat <<EOF
Usage: $(basename "$0") [--dest <path>]

Without --dest, deploys to:
  - $DEST_A
  - $DEST_B

With --dest, deploys only to the given path.
EOF
      exit 0
      ;;
    *)
      echo "Unknown arg: $1" >&2
      exit 2
      ;;
  esac
  shift || true
done

# Helpers
backup_existing() {
  local target_dir="$1"; shift || true
  local target="$target_dir/$BUNDLE_NAME"
  if [[ -e "$target" ]]; then
    local ts
    ts="$(date +%Y%m%d-%H%M%S)"
    local backup="$target_dir/${BUNDLE_NAME%.bundle}-$ts.bundle"
    echo "Backing up existing: $target -> $backup"
    mv -f "$target" "$backup"
  fi
}

install_to() {
  local target_dir="$1"; shift || true
  echo "\n==> Installing to: $target_dir"
  mkdir -p "$target_dir"
  backup_existing "$target_dir"
  cp -R "$SRC_BUNDLE" "$target_dir/"
  # Remove quarantine
  xattr -dr com.apple.quarantine "$target_dir/$BUNDLE_NAME" 2>/dev/null || true
  echo "Installed: $target_dir/$BUNDLE_NAME"
}

if [[ -n "$CUSTOM_DEST" ]]; then
  install_to "$CUSTOM_DEST"
else
  install_to "$DEST_A"
  install_to "$DEST_B"
fi

echo "\nDeployment complete. Next steps:"
echo "  1) Launch your host (e.g., Resolume Arena)"
echo "  2) Add the Source: ModelFXFFGL (plugin version >= 1.1)"
echo "  3) In the UI Status text, confirm it shows 'import=Auto|SkipFallbacks|StaticBake'"
echo "  4) Use Import Mode = SkipFallbacks for problematic glb/fbx to get fast error text"
