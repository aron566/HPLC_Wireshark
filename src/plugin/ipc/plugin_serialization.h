/// @file plugin_serialization.h
/// @brief 插件 IPC 用 QDataStream 序列化(BplcFrame/ParseResult 等)
/// @details 主程序与 bplc-plugin-host 共享。所有算子按固定顺序读写,
///          新增字段必须追加在末尾(保证跨版本兼容)。
#ifndef BPLC_PLUGIN_SERIALIZATION_H
#define BPLC_PLUGIN_SERIALIZATION_H

#include <QDataStream>

#include "bplcframe.h"        // PhysicalMeta/BplcFrame/MsduState/MpduInfo/MsduInfo/...
#include "iprotocolparser.h"  // ParseResult/ParseFilter

// ---- PhysicalMeta ----
QDataStream& operator<<(QDataStream& out, const PhysicalMeta& m);
QDataStream& operator>>(QDataStream& in, PhysicalMeta& m);

// ---- BplcFrame ----
QDataStream& operator<<(QDataStream& out, const BplcFrame& f);
QDataStream& operator>>(QDataStream& in, BplcFrame& f);

// ---- MsduState ----
QDataStream& operator<<(QDataStream& out, const MsduState& s);
QDataStream& operator>>(QDataStream& in, MsduState& s);

// ---- ParseFilter ----
QDataStream& operator<<(QDataStream& out, const ParseFilter& f);
QDataStream& operator>>(QDataStream& in, ParseFilter& f);

// ---- MpduInfo ----
QDataStream& operator<<(QDataStream& out, const MpduInfo& m);
QDataStream& operator>>(QDataStream& in, MpduInfo& m);

// ---- MsduFieldNode (递归) ----
QDataStream& operator<<(QDataStream& out, const MsduFieldNode& n);
QDataStream& operator>>(QDataStream& in, MsduFieldNode& n);

// ---- TeiMacPair ----
QDataStream& operator<<(QDataStream& out, const TeiMacPair& p);
QDataStream& operator>>(QDataStream& in, TeiMacPair& p);

// ---- CommRateInfo ----
QDataStream& operator<<(QDataStream& out, const CommRateInfo& c);
QDataStream& operator>>(QDataStream& in, CommRateInfo& c);

// ---- TopoEvent ----
QDataStream& operator<<(QDataStream& out, const TopoEvent& e);
QDataStream& operator>>(QDataStream& in, TopoEvent& e);

// ---- MsduInfo ----
QDataStream& operator<<(QDataStream& out, const MsduInfo& m);
QDataStream& operator>>(QDataStream& in, MsduInfo& m);

// ---- ParseResult ----
QDataStream& operator<<(QDataStream& out, const ParseResult& r);
QDataStream& operator>>(QDataStream& in, ParseResult& r);

#endif // BPLC_PLUGIN_SERIALIZATION_H
