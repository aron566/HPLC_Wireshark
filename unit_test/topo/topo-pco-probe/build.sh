#!/bin/bash
# Build topo-pco-probe (neighbour-detail PCO row marking) from the repo root.
# Reproduces the original probe link; moc outputs are generated at build time
# (never committed).
# Usage: ./build.sh   (produces ./build/pco_probe)
set -e
cd "$(dirname "$0")"
REPO_ROOT="$(cd "../../.." && pwd)"
QT="$HOME/qt/6.11.2/gcc_64"
LIBFIX="$HOME/workspace/build_deps/libfix"
mkdir -p build
"$QT/libexec/moc" "$REPO_ROOT/src/topo/topo_window.h" -o build/moc_topo_window.cpp
"$QT/libexec/moc" "$REPO_ROOT/3rdparty/qslog/QsLogDestFunctor.h" -o build/moc_QsLogDestFunctor.cpp
g++ -std=c++17 -fPIC \
  -I"$REPO_ROOT/src" -I"$REPO_ROOT/src/topo" -I"$REPO_ROOT/src/common" \
  -I"$REPO_ROOT/3rdparty/qslog" \
  -I"$QT/include" -I"$QT/include/QtCore" -I"$QT/include/QtGui" -I"$QT/include/QtWidgets" \
  pco_probe.cpp build/moc_topo_window.cpp build/moc_QsLogDestFunctor.cpp \
  "$REPO_ROOT/src/topo/topo_window.cpp" "$REPO_ROOT/src/topo/topo_state.cpp" \
  "$REPO_ROOT/src/common/i18n.cpp" \
  "$REPO_ROOT/3rdparty/qslog/QsLog.cpp" "$REPO_ROOT/3rdparty/qslog/QsLogDest.cpp" \
  "$REPO_ROOT/3rdparty/qslog/QsLogDestConsole.cpp" "$REPO_ROOT/3rdparty/qslog/QsLogDestFile.cpp" \
  "$REPO_ROOT/3rdparty/qslog/QsLogDestFunctor.cpp" \
  -L"$QT/lib" -L"$LIBFIX" -lQt6Widgets -lQt6Gui -lQt6Core -lEGL -lGL \
  -o build/pco_probe
chmod +x build/pco_probe
echo "built build/pco_probe"
