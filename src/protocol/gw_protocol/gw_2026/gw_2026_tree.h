/// @file gw_2026_tree.h
/// @brief 国网 GW_2026 协议字段树构建器(IProtocolTreeBuilder 实现)
/// @details 国网双模标准 2022(202203)的 MPDU Base + 各帧型 FC 字段树。
///          字段坐标/语义与 GW_2026_Parser 解析结果(MpduInfo)严格一致。
#ifndef BPLC_GW_2026_TREE_H
#define BPLC_GW_2026_TREE_H

#include "protocol_tree_builder.h"

/// 国网 GW_2026 定界符类型(FC byte0 bit0-2)
enum class GW_2026_FrameType : quint8 {
    BEACON = 0,  ///< 信标帧
    SOF    = 1,  ///< SOF 帧
    ACK    = 2,  ///< 选择确认帧
    COORD  = 3,  ///< 网间协调帧
};

/// 国网 GW_2026 SOF 帧子类型(载波表7 (7,0,3);无线 HDC2.0 4-2 表18 无此字段=数据帧)
enum class GW_2026_SofDataType : quint8 {
    DATA   = 0,  ///< 数据帧
    SOUND  = 1,  ///< 信道评估帧(表9)
    OFDMA  = 2,  ///< OFDMA 下行帧(表10/表11)
    TDA    = 3,  ///< TDA 下行帧(表12/表13)
};

/// 国网 GW_2026 TF 符号位置(载波表7 (7,3,2))
enum class GW_2026_TfSymbolPos : quint8 {
    NONE  = 0,  ///< 不携带 TF
    FRONT = 1,  ///< 数据载荷之前
    BACK  = 2,  ///< 数据载荷之后
};

/// 国网 GW_2026 FC 符号数(载波表7 (9,7,2) 跨字节 / 信标表5 (11,3,2))
enum class GW_2026_FchSymNum : quint8 {
    STANDARD= 0,  ///< 标准 FC(V6 表5/7/9/12:0=标准)
    LONG    = 1,  ///< 长 FC(1=长)
    SHORT   = 2,  ///< 短 FC(2=短)
};

/// 国网 GW_2026 载荷子载波加载模式标识(载波表7 (10,0-1),2bit)
enum class GW_2026_ModMode : quint8 {
    STATIC_LOAD   = 0,  ///< 静态加载模式(调制参数=TMI)
    DYNAMIC_POWER = 1,  ///< 动态功率加载模式(调制参数=TMI)
    DYNAMIC_BIT   = 2,  ///< 动态比特加载模式(调制参数=物理块规格编号,表8)
};

/// 国网 GW_2026 TF 与数据载荷频段标识(载波表7 (11,4,6),取值同表49)
enum class GW_2026_CarrierBand : quint8 {
    BAND_0 = 0,  ///< 1.953~11.96 MHz
    BAND_1 = 1,  ///< 2.441~5.615 MHz
    BAND_2 = 2,  ///< 0.781~2.930 MHz
    BAND_3 = 3,  ///< 保留(旧版 1.758-2.930,2.0 收回;混网互操作注意)
    BAND_4 = 4,  ///< 0.781~5.615 MHz
    BAND_5 = 5,  ///< 0.781~11.96 MHz
    BAND_6 = 6,  ///< 6.08~11.962 MHz
};

/// 国网 GW_2026 ACK 扩展帧类型(FC byte12 bit0-3,表25/表36)
/// @note 表25(载波)/表36(无线)均仅定义 0=选择确认帧,其余为保留;
///       国网无南网 NW_2021 的"网络搜索/同步"扩展类型。
enum class GW_2026_AckExtType : quint8 {
    Normal = 0,  ///< 选择确认帧
};

/// 国网 GW_2026 标准版本号(FC byte12 bit4-7,表4)
enum class GW_2026_Version : quint8 {
    HDC_1_0 = 0,  ///< HDC 1.0(旧版,字节序兼容帧可解析)
    HDC_2_0 = 1,  ///< HDC 2.0(本标准)
};

/// 国网 GW_2026 ACK 常规收包结果(RxRes;HDC 2.0 表14 该 4bit 拆为
/// BRU SNR/SAT ID/接收PB个数,无独立 RxRes 字段;枚举保留供序列化兼容)
enum class GW_2026_AckRxRes : quint8 {
    RECEIPT_OK   = 0,  ///< 接收成功(所有 PB CRC 通过)
    RECEIPT_FAIL = 1,  ///< 接收失败(≥1 块 CRC 未过)
};

/// 国网双模标准 2022 字段树构建器
class GW_2026_TreeBuilder : public IProtocolTreeBuilder {
public:
    ProtocolVariant variant() const override { return ProtocolVariant::GW_2026; }
    void build(QTreeWidgetItem* root, const PacketEntry& e) override;
};

#endif  // BPLC_GW_2026_TREE_H
