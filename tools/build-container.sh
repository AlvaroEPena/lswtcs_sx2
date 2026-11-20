#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
docker run --rm --mount "type=bind,src=$root,dst=/work" --workdir /work \
  "${LSWTCS_DEVKIT_IMAGE:-ounce-devkit}" bash -lc '
    source /opt/devkitpro/devkita64.sh
    make -j6
  '
