#!/bin/bash
# Gets SuperTux 0.1.3 ("Milestone 1", 2004) - the version this port is made from - into
# SuperTux-0.1.3/ next to build.sh (git, tag v0.1.3 of github.com/SuperTux/supertux, 16 MB).
# Nothing of SuperTux is stored in this repository; build.sh reads it from there.
set -e
HERE="$(cd "$(dirname "$0")/.." && pwd)"
DEST="${1:-$HERE/SuperTux-0.1.3}"
if [ -f "$DEST/data/images/tilesets/supertux.stgt" ]; then
  echo "==> SuperTux 0.1.3 already in $DEST"
else
  git -c advice.detachedHead=false clone --depth 1 --branch v0.1.3 https://github.com/SuperTux/supertux "$DEST"
fi
