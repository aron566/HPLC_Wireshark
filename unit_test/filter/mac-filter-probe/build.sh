#!/bin/bash
# Build mac-filter-probe (main-interface MAC search filter) from the repo root.
# Reproduces the original probe link; moc outputs are generated at build time
# (never committed). NOTE: the MAC-filter feature was merged into main
# (12d1cbc); against main sources the probe passes 47/47
# (RESULT: 0 failures). Before the merge, main sources gave 28/47.
# Usage: ./build.sh   (produces ./build/mac_filter_probe)
set -e
cd "$(dirname "$0")"
REPO_ROOT="$(cd "../../.." && pwd)"
QT="$HOME/qt/6.11.2/gcc_64"
LIBFIX="$HOME/workspace/build_deps/libfix"
mkdir -p build
"$QT/libexec/moc" "$REPO_ROOT/src/ui/packetlistmodel.h" -o build/moc_packetlistmodel.cpp
"$QT/libexec/moc" "$REPO_ROOT/3rdparty/qslog/QsLogDestFunctor.h" -o build/moc_QsLogDestFunctor.cpp
g++ -std=c++17 -fPIC \
  -I"$REPO_ROOT/src" -I"$REPO_ROOT/src/ui" -I"$REPO_ROOT/src/common" \
  -I"$REPO_ROOT/3rdparty/qslog" \
  -I"$QT/include" -I"$QT/include/QtCore" -I"$QT/include/QtGui" -I"$QT/include/QtWidgets" \
  -I"$QT/include/QtConcurrent" \
  mac_filter_probe.cpp build/moc_packetlistmodel.cpp build/moc_QsLogDestFunctor.cpp \
  "$REPO_ROOT/src/ui/packetlistmodel.cpp" \
  "$REPO_ROOT/src/common/i18n.cpp" \
  "$REPO_ROOT/3rdparty/qslog/QsLog.cpp" "$REPO_ROOT/3rdparty/qslog/QsLogDest.cpp" \
  "$REPO_ROOT/3rdparty/qslog/QsLogDestConsole.cpp" "$REPO_ROOT/3rdparty/qslog/QsLogDestFile.cpp" \
  "$REPO_ROOT/3rdparty/qslog/QsLogDestFunctor.cpp" \
  -L"$QT/lib" -L"$LIBFIX" -lQt6Widgets -lQt6Gui -lQt6Core -lQt6Concurrent -lEGL -lGL \
  -o build/mac_filter_probe
chmod +x build/mac_filter_probe
echo "built build/mac_filter_probe"
