#!/bin/bash
# Build gw_topo_merge_probe against the parser sources in the repo root.
# Adapted from the original script: repo dir defaults to the checkout root
# (unit_test/protocol-gw/gw-topo-merge-probe/../../..), output defaults to
# ./build/gw_topo_merge_probe, and the QsLogDestFunctor moc file is generated
# at build time (never committed).
# Usage: ./build.sh [out-binary [repo-dir]]
set -e
cd "$(dirname "$0")"
REPO_ROOT="$(cd "../../.." && pwd)"
OUT="${1:-./build/gw_topo_merge_probe}"
R="${2:-$REPO_ROOT}"
QT="$HOME/qt/6.11.2/gcc_64"
mkdir -p build
"$QT/libexec/moc" "$R/3rdparty/qslog/QsLogDestFunctor.h" -o build/moc_QsLogDestFunctor.cpp
g++ -std=c++17 -fPIC \
  -I$R/src -I$R/src/protocol -I$R/src/protocol/common \
  -I$R/src/protocol/gw_protocol/gw_2026 -I$R/src/protocol/gw_protocol/gw_2026/beacon \
  -I$R/src/protocol/gw_protocol/gw_2026/sof -I$R/src/protocol/gw_protocol/gw_2026/ack \
  -I$R/src/protocol/gw_protocol/gw_2026/coord \
  -I$R/src/protocol/gw_protocol/gw_2022 -I$R/src/protocol/gw_protocol/gw_2022/beacon \
  -I$R/src/protocol/gw_protocol/gw_2022/sof -I$R/src/protocol/gw_protocol/gw_2022/ack \
  -I$R/src/protocol/gw_protocol/gw_2022/coord \
  -I$R/src/common -I$R/src/topo -I$R/3rdparty/qslog \
  -I$QT/include -I$QT/include/QtCore -I$QT/include/QtGui -I$QT/include/QtWidgets \
  gw_topo_merge_probe.cpp build/moc_QsLogDestFunctor.cpp \
  $R/src/common/i18n.cpp \
  $R/src/topo/topo_state.cpp \
  $R/3rdparty/qslog/QsLog.cpp $R/3rdparty/qslog/QsLogDest.cpp \
  $R/3rdparty/qslog/QsLogDestConsole.cpp $R/3rdparty/qslog/QsLogDestFile.cpp \
  $R/3rdparty/qslog/QsLogDestFunctor.cpp \
  $R/src/protocol/protocol_tree_builder.cpp \
  $R/src/protocol/gw_protocol/gw_2026/gw_2026_parser.cpp \
  $R/src/protocol/gw_protocol/gw_2026/gw_2026_msdu_parser.cpp \
  $R/src/protocol/gw_protocol/gw_2026/beacon/gw_2026_beacon_parser.cpp \
  $R/src/protocol/gw_protocol/gw_2026/sof/gw_2026_sof_parser.cpp \
  $R/src/protocol/gw_protocol/gw_2026/ack/gw_2026_ack_parser.cpp \
  $R/src/protocol/gw_protocol/gw_2026/coord/gw_2026_coord_parser.cpp \
  $R/src/protocol/gw_protocol/gw_2026/gw_2026_tree.cpp \
  $R/src/protocol/gw_protocol/gw_2022/gw_2022_parser.cpp \
  $R/src/protocol/gw_protocol/gw_2022/gw_2022_msdu_parser.cpp \
  $R/src/protocol/gw_protocol/gw_2022/beacon/gw_2022_beacon_parser.cpp \
  $R/src/protocol/gw_protocol/gw_2022/sof/gw_2022_sof_parser.cpp \
  $R/src/protocol/gw_protocol/gw_2022/ack/gw_2022_ack_parser.cpp \
  $R/src/protocol/gw_protocol/gw_2022/coord/gw_2022_coord_parser.cpp \
  $R/src/protocol/gw_protocol/gw_2022/gw_2022_tree.cpp \
  -L$QT/lib -L$HOME/workspace/build_deps/libfix \
  -lQt6Widgets -lQt6Gui -lQt6Core -lEGL -lGL -o "$OUT"
chmod +x "$OUT"
echo "built $OUT"
