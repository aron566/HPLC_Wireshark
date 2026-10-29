#!/bin/bash
# Build all topo_restart_probe programs from the repo root.
# Per-probe source sets follow each probe's #include lines:
#   cpc37_probe    NW_2026 + NW_2021 MSDU parsers (crc is header-only)
#   cpc_auth_probe TopoState only
#   diversity_probe gw_2026_pb_table.h only (inline functions, no extra link)
#   dual/m2/m2m7/module/main  TopoState only (main = restart-detection probe)
#   m6beacon_probe GW_2026 beacon parser
#   model_probe    packet-list model (+ QtConcurrent, moc generated)
#   pb0_gate_probe full NW_2021 group + protocol_tree_builder
# The QsLogDestFunctor moc file is generated at build time (never committed).
# Usage: ./build.sh   (produces ./build/<probe> for all 11 probes)
# Run:   ./build/<probe>   (each prints ok/FAIL lines, exit 0 on pass)
set -e
cd "$(dirname "$0")"
REPO_ROOT="$(cd "../../.." && pwd)"
QT="$HOME/qt/6.11.2/gcc_64"
LIBFIX="$HOME/workspace/build_deps/libfix"
NW26="$REPO_ROOT/src/protocol/nw_protocol/nw_2026"
NW21="$REPO_ROOT/src/protocol/nw_protocol/nw_2021"
GW26="$REPO_ROOT/src/protocol/gw_protocol/gw_2026"
mkdir -p build
"$QT/libexec/moc" "$REPO_ROOT/3rdparty/qslog/QsLogDestFunctor.h" -o build/moc_QsLogDestFunctor.cpp
"$QT/libexec/moc" "$REPO_ROOT/src/ui/packetlistmodel.h" -o build/moc_packetlistmodel.cpp
INC="-I$REPO_ROOT/src -I$REPO_ROOT/src/protocol -I$REPO_ROOT/src/protocol/common \
  -I$NW26 -I$NW21 -I$GW26 -I$GW26/beacon \
  -I$REPO_ROOT/src/common -I$REPO_ROOT/src/topo -I$REPO_ROOT/src/ui \
  -I$REPO_ROOT/3rdparty/qslog \
  -I$QT/include -I$QT/include/QtCore -I$QT/include/QtGui -I$QT/include/QtWidgets \
  -I$QT/include/QtConcurrent"
QSLOG="$REPO_ROOT/3rdparty/qslog/QsLog.cpp $REPO_ROOT/3rdparty/qslog/QsLogDest.cpp \
  $REPO_ROOT/3rdparty/qslog/QsLogDestConsole.cpp $REPO_ROOT/3rdparty/qslog/QsLogDestFile.cpp \
  $REPO_ROOT/3rdparty/qslog/QsLogDestFunctor.cpp"
BASE="$REPO_ROOT/src/common/i18n.cpp $QSLOG build/moc_QsLogDestFunctor.cpp"
TOPO="$REPO_ROOT/src/topo/topo_state.cpp"
LIBS="-L$QT/lib -L$LIBFIX -lQt6Widgets -lQt6Gui -lQt6Core -lEGL -lGL"
# shellcheck disable=SC2086
build_one() {
  local name="$1"; shift
  # shellcheck disable=SC2086
  g++ -std=c++17 -fPIC $INC "$@" $BASE $LIBS -o "build/$name"
  chmod +x "build/$name"
  echo "built build/$name"
}
build_one cpc37_probe    cpc37_probe.cpp    "$NW26/nw_2026_msdu_parser.cpp" "$NW21/nw_2021_msdu_parser.cpp"
build_one cpc_auth_probe cpc_auth_probe.cpp "$TOPO"
build_one diversity_probe diversity_probe.cpp
build_one dual_probe     dual_probe.cpp     "$TOPO"
build_one m2_probe       m2_probe.cpp       "$TOPO"
build_one m2m7_probe     m2m7_probe.cpp     "$TOPO"
build_one m6beacon_probe m6beacon_probe.cpp "$GW26/beacon/gw_2026_beacon_parser.cpp"
build_one main_probe     main.cpp           "$TOPO"
build_one module_probe   module_probe.cpp   "$TOPO"
build_one pb0_gate_probe pb0_gate_probe.cpp "$NW21/nw_2021_parser.cpp" "$NW21/nw_2021_msdu_parser.cpp" \
  "$NW21/nw_2021_beacon_parser.cpp" "$NW21/nw_2021_tree.cpp" "$REPO_ROOT/src/protocol/protocol_tree_builder.cpp"
# model_probe needs QtConcurrent and the packet-list model moc file.
g++ -std=c++17 -fPIC $INC model_probe.cpp build/moc_packetlistmodel.cpp \
  "$REPO_ROOT/src/ui/packetlistmodel.cpp" $BASE \
  -L$QT/lib -L$LIBFIX -lQt6Widgets -lQt6Gui -lQt6Core -lQt6Concurrent -lEGL -lGL \
  -o build/model_probe
chmod +x build/model_probe
echo "built build/model_probe"
