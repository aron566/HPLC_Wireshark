#!/bin/bash
# Build this case with qmake (Qt 6.11.2) in shadow build dirs under ./build/.
# Usage: ./build.sh
set -e
cd "$(dirname "$0")"
QT="$HOME/qt/6.11.2/gcc_64"
export LIBRARY_PATH="$HOME/workspace/build_deps/libfix${LIBRARY_PATH:+:$LIBRARY_PATH}"
export LD_LIBRARY_PATH="$HOME/workspace/build_deps/libfix:$QT/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
for pro in nw_trunc_pbcrc_probe.pro; do
  name="$(basename "$pro" .pro)"
  mkdir -p "build/$name"
  (cd "build/$name" && "$QT/bin/qmake" "../../$pro" && make)
  echo "built build/$name/"
done
