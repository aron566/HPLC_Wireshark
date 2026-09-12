/* packet-hplc_rf.c
 *
 * 双模通信(高速载波+无线) 数据链路层协议 dissector
 * 依据: 《双模通信互联互通技术规范 第4-2部分：数据链路层通信协议》2021-03-26
 *
 * 覆盖范围:
 *   - MPDU 帧控制(16B, 5.1.2)          [已实现解析]
 *   - MAC 标准/单跳帧头(5.1.1)          [字段已注册]
 *   - 信标/SOF/SACK/网间协调 可变区域    [字段已注册]
 *   - 管理消息(MMTYPE 分流, 5.1.3)      [字段已注册, 分支待实现]
 *
 * 编译: 放入 Wireshark 源码树 epan/dissectors/, 在 CMakeLists.txt
 *       的 DISSECTOR_SRC 列表追加 packet-hplc_rf.c, 与 Wireshark 一同编译。
 *       (Wireshark 4.x C 插件无独立编译路径, 必须随源码树构建)
 */

#include "config.h"

#include <epan/packet.h>
#include <epan/proto.h>
#include <epan/prefs.h>

void proto_register_hplc_rf(void);
void proto_reg_handoff_hplc_rf(void);

/* =========================================================================
 * 协议句柄 / 子树句柄
 * ========================================================================= */
static int proto_hplc_rf = -1;

static gint ett_hplc_rf            = -1;
static gint ett_hplc_rf_mpdu       = -1;
static gint ett_hplc_rf_fc         = -1;
static gint ett_hplc_rf_mac        = -1;
static gint ett_hplc_rf_mac_hdr    = -1;
static gint ett_hplc_rf_mgmt       = -1;
static gint ett_hplc_rf_vf         = -1;
static gint ett_hplc_rf_vf_beacon  = -1;
static gint ett_hplc_rf_vf_sof     = -1;
static gint ett_hplc_rf_vf_sack    = -1;
static gint ett_hplc_rf_vf_coord   = -1;

/* =========================================================================
 * value_string 映射表
 * ========================================================================= */

/* 表14 定界符类型 */
static const value_string dt_vals[] = {
    { 0, "Beacon 信标帧" },
    { 1, "SOF 数据帧" },
    { 2, "SACK 选择确认帧" },
    { 3, "Inter-network Coordination 网间协调帧" },
    { 0, NULL }
};

/* 表15 网络类型 */
static const value_string network_type_vals[] = {
    { 0, "用电信息采集系统" },
    { 0, NULL }
};

/* 表16 标准版本号 */
static const value_string std_ver_vals[] = {
    { 0, "本标准" },
    { 0, NULL }
};

/* 表5 发送类型 */
static const value_string send_type_vals[] = {
    { 0, "单播" },
    { 1, "全网广播" },
    { 2, "本地广播" },
    { 3, "代理广播" },
    { 0, NULL }
};

/* 表7 广播方向 */
static const value_string broadcast_dir_vals[] = {
    { 0, "双向广播" },
    { 1, "下行广播(CCO->STA)" },
    { 2, "上行广播(STA->CCO)" },
    { 0, NULL }
};

/* 表10 MSDU 类型 */
static const value_string msdu_type_vals[] = {
    { 0,  "网络管理消息" },
    { 48, "应用层报文" },
    { 49, "IP 报文" },
    { 0, NULL }
};

/* 表12 单跳帧 消息类型 */
static const value_string msg_type_vals[] = {
    { 0,   "发现列表消息" },
    { 128, "应用层报文" },
    { 129, "IPv4 报文" },
    { 0, NULL }
};

/* 表39 信标类型 */
static const value_string beacon_type_vals[] = {
    { 0, "发现信标" },
    { 1, "代理信标" },
    { 2, "中央信标" },
    { 0, NULL }
};

/* 表61/72/80/86/90 链路类型 */
static const value_string link_type_vals[] = {
    { 0, "高速载波链路" },
    { 1, "无线链路" },
    { 0, NULL }
};

/* 表62/83 相线 */
static const value_string phase_vals[] = {
    { 0, "未知相线" },
    { 1, "A相线" },
    { 2, "B相线" },
    { 3, "C相线" },
    { 0, NULL }
};

/* 表63 设备类型 */
static const value_string device_type_vals[] = {
    { 1, "抄控器" },
    { 2, "集中器本地通信单元" },
    { 3, "电表通信单元" },
    { 4, "中继器" },
    { 5, "II型采集器" },
    { 6, "I型采集器单元" },
    { 7, "三相电表通信单元" },
    { 0, NULL }
};

/* 表64 MAC 地址类型 */
static const value_string mac_addr_type_vals[] = {
    { 0, "电能表地址作为入网MAC" },
    { 1, "通信模块本身MAC作为入网MAC" },
    { 0, NULL }
};

/* 表65 模块类型 */
static const value_string module_type_vals[] = {
    { 0, "高速载波单模模块" },
    { 1, "双模(高速载波+无线)模块" },
    { 2, "无线单模模块" },
    { 0, NULL }
};

/* 表71 关联确认结果 */
static const value_string assoc_result_vals[] = {
    { 0x00, "关联请求成功" },
    { 0x01, "站点不在白名单中" },
    { 0x02, "站点在黑名单中" },
    { 0x03, "站点个数超过上限" },
    { 0x04, "没有设置白名单列表" },
    { 0x05, "代理站点个数超过上限" },
    { 0x06, "子站点个数超过上限" },
    { 0x08, "重复的MAC地址" },
    { 0x09, "超过拓扑层级" },
    { 0x0A, "站点再次关联请求入网成功" },
    { 0x0B, "新站点试图以自己子站点为代理入网" },
    { 0x0C, "组网拓扑中存在环路" },
    { 0x0D, "CCO端未知原因出错" },
    { 0x0E, "无线代理达到上限" },
    { 0, NULL }
};

/* 表73/77 载波频段 */
static const value_string carrier_band_vals[] = {
    { 0, "1.953~11.96 MHz" },
    { 1, "2.441~5.615 MHz" },
    { 2, "0.781~2.930 MHz" },
    { 3, "1.758~2.930 MHz" },
    { 0, NULL }
};

/* 表69/81 代理类型 */
static const value_string proxy_type_vals[] = {
    { 0, "站点动态选择的代理" },
    { 0, NULL }
};

/* 表98 路由类型 */
static const value_string route_type_vals[] = {
    { 0, "错误的路由类型" },
    { 1, "同级路由" },
    { 2, "上级路由" },
    { 3, "代理主路径路由" },
    { 4, "上上级路由" },
    { 0, NULL }
};

/* 表59 管理消息类型 (MMTYPE) */
static const value_string mgmt_type_vals[] = {
    { 0x0000, "关联请求 AssocReq" },
    { 0x0001, "关联确认 AssocCnf" },
    { 0x0002, "关联汇总指示 AssocGatherInd" },
    { 0x0003, "代理变更请求 ChangeProxyReq" },
    { 0x0004, "代理变更确认 ChangeProxyCnf" },
    { 0x0005, "代理变更确认(位图版) ChangeProxyBitMapCnf" },
    { 0x0006, "离线指示 LeaveInd" },
    { 0x0007, "心跳检测 HeartBeatCheck" },
    { 0x0008, "发现列表 DiscoverNodeList" },
    { 0x0009, "通信成功率上报 SuccessRateReport" },
    { 0x000A, "网络冲突上报 NetworkConflictReport" },
    { 0x000B, "过零NTB采集指示 ZeroCrossNTBCollectInd" },
    { 0x000C, "过零NTB上报 ZeroCrossNTBReport" },
    { 0x004F, "网络诊断报文 Diagnose" },
    { 0x0050, "路由请求 RouteRequest" },
    { 0x0051, "路由回复 RouteReply" },
    { 0x0052, "路由错误 RouteError" },
    { 0x0053, "路由应答 RouteAck" },
    { 0x0054, "链路确认请求 LinkConfirmRequest" },
    { 0x0055, "链路确认回应 LinkConfirmResponse" },
    { 0x0080, "无线信道冲突上报 RFChannelConflictReport" },
    { 0, NULL }
};

/* =========================================================================
 * hf 字段变量声明
 * ========================================================================= */

/* --- MPDU 帧控制(表13) --- */
static int hf_hplc_rf_fc_dt          = -1;  /* 定界符类型 */
static int hf_hplc_rf_fc_net_type    = -1;  /* 网络类型 */
static int hf_hplc_rf_fc_nid         = -1;  /* 网络标识 NID */
static int hf_hplc_rf_fc_vf          = -1;  /* 可变区域(占位) */
static int hf_hplc_rf_fc_std_ver     = -1;  /* 标准版本号 */
static int hf_hplc_rf_fc_fccs        = -1;  /* 帧控制校验序列 CRC24 */

/* --- 标准 MAC 帧头(表4) --- */
static int hf_hplc_rf_mac_version     = -1;
static int hf_hplc_rf_mac_ostei       = -1;  /* 原始源 TEI */
static int hf_hplc_rf_mac_odtei       = -1;  /* 原始目的 TEI */
static int hf_hplc_rf_mac_send_type   = -1;  /* 发送类型 */
static int hf_hplc_rf_mac_retry_limit = -1;  /* 发送次数限值 */
static int hf_hplc_rf_mac_msdu_seq    = -1;  /* MSDU 序列号 */
static int hf_hplc_rf_mac_msdu_type   = -1;  /* MSDU 类型 */
static int hf_hplc_rf_mac_msdu_len    = -1;  /* MSDU 长度 */
static int hf_hplc_rf_mac_restart_cnt = -1;  /* 重启次数 */
static int hf_hplc_rf_mac_proxy_main  = -1;  /* 代理主路径标识 */
static int hf_hplc_rf_mac_route_total = -1;  /* 路由总跳数 */
static int hf_hplc_rf_mac_route_left  = -1;  /* 路由剩余跳数 */
static int hf_hplc_rf_mac_bcast_dir   = -1;  /* 广播方向 */
static int hf_hplc_rf_mac_path_repair = -1;  /* 路径修复标志 */
static int hf_hplc_rf_mac_addr_flag   = -1;  /* MAC 地址标志 */
static int hf_hplc_rf_mac_net_seq     = -1;  /* 组网序列号 */
static int hf_hplc_rf_mac_osmac       = -1;  /* 原始源 MAC 地址 */
static int hf_hplc_rf_mac_odmac       = -1;  /* 原始目的 MAC 地址 */

/* --- 单跳 MAC 帧头(表11) --- */
static int hf_hplc_rf_sh_version = -1;
static int hf_hplc_rf_sh_msgtype = -1;  /* 消息类型 */
static int hf_hplc_rf_sh_msdulen = -1;  /* MSDU 长度 */

/* --- 信标帧可变区域(表17 载波 / 表27 无线) --- */
static int hf_hplc_rf_beacon_bts         = -1;  /* 信标时间戳 */
static int hf_hplc_rf_beacon_src_tei     = -1;  /* 源 TEI */
static int hf_hplc_rf_beacon_div_mode    = -1;  /* 分集拷贝基本模式 */
static int hf_hplc_rf_beacon_symbol_cnt  = -1;  /* 符号数 */
static int hf_hplc_rf_beacon_phase       = -1;  /* 相线 */
static int hf_hplc_rf_beacon_mcs         = -1;  /* MCS(无线) */
static int hf_hplc_rf_beacon_pb_size     = -1;  /* 载荷PB块大小 */

/* --- 信标帧载荷(表38 标准) --- */
static int hf_hplc_rf_beacon_type        = -1;
static int hf_hplc_rf_beacon_net_cplt    = -1;  /* 组网标志位 */
static int hf_hplc_rf_beacon_simple      = -1;  /* 精简信标标志 */
static int hf_hplc_rf_beacon_start_assoc = -1;  /* 开始关联标志 */
static int hf_hplc_rf_beacon_use_flag    = -1;  /* 信标使用标志 */
static int hf_hplc_rf_beacon_net_seq     = -1;  /* 组网序列号 */
static int hf_hplc_rf_beacon_cco_mac     = -1;  /* CCO MAC 地址 */
static int hf_hplc_rf_beacon_bpc         = -1;  /* 信标周期计数 */
static int hf_hplc_rf_beacon_rf_ch       = -1;  /* 本网络无线信道编号 */
static int hf_hplc_rf_beacon_entry_cnt   = -1;  /* 信标条目数 */
static int hf_hplc_rf_beacon_bpcs        = -1;  /* 帧载荷校验序列 CRC32 */

/* --- SOF 帧可变区域(表19 载波 / 表30 无线) --- */
static int hf_hplc_rf_sof_src_tei    = -1;
static int hf_hplc_rf_sof_dst_tei    = -1;
static int hf_hplc_rf_sof_lid        = -1;  /* 链路标识符 */
static int hf_hplc_rf_sof_frame_len  = -1;  /* 帧长 */
static int hf_hplc_rf_sof_pb_count   = -1;  /* 物理块个数(载波) */
static int hf_hplc_rf_sof_symbol_cnt = -1;  /* 符号数(载波) */
static int hf_hplc_rf_sof_bcast      = -1;  /* 广播标志 */
static int hf_hplc_rf_sof_retrans    = -1;  /* 重传标志 */
static int hf_hplc_rf_sof_encrypt    = -1;  /* 加密标志 */
static int hf_hplc_rf_sof_div_mode   = -1;  /* 分集拷贝基本模式(载波) */
static int hf_hplc_rf_sof_div_ext    = -1;  /* 分集拷贝扩展模式(载波) */
static int hf_hplc_rf_sof_pb_size    = -1;  /* 载荷PB块大小(无线) */
static int hf_hplc_rf_sof_mcs        = -1;  /* MCS(无线) */

/* --- SACK 帧可变区域(表23 载波 / 表34 无线) --- */
static int hf_hplc_rf_sack_recv_result = -1;  /* 接收结果 */
static int hf_hplc_rf_sack_recv_status = -1;  /* 接收状态(载波) */
static int hf_hplc_rf_sack_src_tei     = -1;
static int hf_hplc_rf_sack_dst_tei     = -1;
static int hf_hplc_rf_sack_pb_count    = -1;  /* 接收物理块个数(载波) */
static int hf_hplc_rf_sack_chan_qual   = -1;  /* 信道质量 */
static int hf_hplc_rf_sack_site_load   = -1;  /* 站点负载 */
static int hf_hplc_rf_sack_ext_type    = -1;  /* 扩展帧类型 */

/* --- 网间协调帧可变区域(表26) --- */
static int hf_hplc_rf_coord_duration  = -1;  /* 持续时间 */
static int hf_hplc_rf_coord_bw_offset = -1;  /* 带宽开始偏移 */
static int hf_hplc_rf_coord_nbr_nid   = -1;  /* 接收到的邻居网络号 */
static int hf_hplc_rf_coord_rf_ch     = -1;  /* 本网络无线信道编号 */

/* --- 管理消息头(表58) --- */
static int hf_hplc_rf_mgmt_mmtype = -1;  /* 管理消息类型 */
static int hf_hplc_rf_mgmt_resv   = -1;  /* 保留 */

/* =========================================================================
 * 解析入口
 *
 * 说明: 本协议数据源为串口/监控器捕获的 0x3C 帧流(raw_wire), 封装采用
 * WTAP_ENCAP_USERx。MPDU 帧控制 16B 为定长, 已做完整解析; MAC 帧头与管理
 * 消息字段已注册, 解析逻辑按 TODO 锚点逐段补充。
 * ========================================================================= */
static int
dissect_hplc_rf(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tree,
                void *data _U_)
{
    proto_item *ti;
    proto_tree *hplc_tree;
    guint8      dt;

    col_set_str(pinfo->cinfo, COL_PROTOCOL, "HPLC_RF");
    col_clear(pinfo->cinfo, COL_INFO);

    ti = proto_tree_add_item(tree, proto_hplc_rf, tvb, 0, -1, ENC_NA);
    hplc_tree = proto_item_add_subtree(ti, ett_hplc_rf);

    /* ---- MPDU 帧控制 16B (5.1.2, 表13) ---- */
    {
        proto_item *mpdu_item = proto_tree_add_item(hplc_tree,
            proto_hplc_rf, tvb, 0, 16, ENC_NA);
        proto_tree *mpdu_tree = proto_item_add_subtree(mpdu_item,
            ett_hplc_rf_mpdu);

        dt = tvb_get_guint8(tvb, 0) & 0x07;

        proto_tree_add_item(mpdu_tree, hf_hplc_rf_fc_dt,       tvb, 0, 1, ENC_NA);
        proto_tree_add_item(mpdu_tree, hf_hplc_rf_fc_net_type, tvb, 0, 1, ENC_NA);
        proto_tree_add_item(mpdu_tree, hf_hplc_rf_fc_nid,      tvb, 1, 3, ENC_NA);

        /* 可变区域 68bit = 字节4..11 + 字节12低半字节, 按 DT 分流 */
        {
            proto_item *vf_item = proto_tree_add_item(mpdu_tree,
                hf_hplc_rf_fc_vf, tvb, 4, 9, ENC_NA);
            proto_tree *vf_tree = proto_item_add_subtree(vf_item,
                ett_hplc_rf_vf);

            /* TODO(5.1.2.4): dt==0 信标帧可变区域 解析 */
            /* TODO(5.1.2.3): dt==1 SOF 帧可变区域 解析 */
            /* TODO(5.1.2.7): dt==2 SACK 帧可变区域 解析 */
            /* TODO(5.1.2.2): dt==3 网间协调帧可变区域 解析 */
        }

        proto_tree_add_item(mpdu_tree, hf_hplc_rf_fc_std_ver, tvb, 12, 1, ENC_NA);
        proto_tree_add_item(mpdu_tree, hf_hplc_rf_fc_fccs,    tvb, 13, 3, ENC_NA);
    }

    col_add_fstr(pinfo->cinfo, COL_INFO, "DT=%s NID=%u",
                 val_to_str(dt, dt_vals, "Reserved(%u)"),
                 tvb_get_ntoh24(tvb, 1));

    /* ---- 载荷区域: MAC 帧头 + MSDU + ICV, 按 DT 决定是否存在 ---- */
    if (dt == 1) {  /* SOF 帧才携带 MAC 帧 */
        /* TODO(5.1.1): 解析标准 MAC 帧头(版本=0, 表4)
         * TODO(5.1.1.4): 版本=1 单跳 MAC 帧头(仅无线, 表11)
         * TODO(5.1.3): MSDU 类型=0 时按 MMTYPE 分流管理消息
         * TODO(5.1.1.6): ICV CRC32 完整性校验
         */
    }

    return tvb_captured_length(tvb);
}

/* =========================================================================
 * 协议注册
 * ========================================================================= */
void
proto_register_hplc_rf(void)
{
    static hf_register_info hf[] = {
        /* ---- MPDU 帧控制(表13) ---- */
        { &hf_hplc_rf_fc_dt,
          { "定界符类型", "hplc_rf.fc.dt", FT_UINT8, BASE_DEC,
            VALS(dt_vals), 0x07, NULL, HFILL } },
        { &hf_hplc_rf_fc_net_type,
          { "网络类型", "hplc_rf.fc.net_type", FT_UINT8, BASE_DEC,
            VALS(network_type_vals), 0xF8, NULL, HFILL } },
        { &hf_hplc_rf_fc_nid,
          { "网络标识(NID)", "hplc_rf.fc.nid", FT_UINT24, BASE_DEC,
            NULL, 0x0, NULL, HFILL } },
        { &hf_hplc_rf_fc_vf,
          { "可变区域", "hplc_rf.fc.vf", FT_BYTES, BASE_NONE,
            NULL, 0x0, NULL, HFILL } },
        { &hf_hplc_rf_fc_std_ver,
          { "标准版本号", "hplc_rf.fc.std_ver", FT_UINT8, BASE_DEC,
            VALS(std_ver_vals), 0xF0, NULL, HFILL } },
        { &hf_hplc_rf_fc_fccs,
          { "帧控制校验序列(FCCS,CRC24)", "hplc_rf.fc.fccs",
            FT_UINT24, BASE_HEX, NULL, 0x0, NULL, HFILL } },

        /* ---- 标准 MAC 帧头(表4) ---- */
        { &hf_hplc_rf_mac_version,
          { "版本", "hplc_rf.mac.version", FT_UINT8, BASE_DEC,
            NULL, 0xF0, NULL, HFILL } },
        { &hf_hplc_rf_mac_ostei,
          { "原始源TEI", "hplc_rf.mac.ostei", FT_UINT16, BASE_DEC,
            NULL, 0x0, NULL, HFILL } },
        { &hf_hplc_rf_mac_odtei,
          { "原始目的TEI", "hplc_rf.mac.odtei", FT_UINT16, BASE_DEC,
            NULL, 0x0, NULL, HFILL } },
        { &hf_hplc_rf_mac_send_type,
          { "发送类型", "hplc_rf.mac.send_type", FT_UINT8, BASE_DEC,
            VALS(send_type_vals), 0x0F, NULL, HFILL } },
        { &hf_hplc_rf_mac_retry_limit,
          { "发送次数限值", "hplc_rf.mac.retry_limit", FT_UINT8, BASE_DEC,
            NULL, 0x1F, NULL, HFILL } },
        { &hf_hplc_rf_mac_msdu_seq,
          { "MSDU序列号", "hplc_rf.mac.msdu_seq", FT_UINT16, BASE_DEC,
            NULL, 0x0, NULL, HFILL } },
        { &hf_hplc_rf_mac_msdu_type,
          { "MSDU类型", "hplc_rf.mac.msdu_type", FT_UINT8, BASE_DEC,
            VALS(msdu_type_vals), 0x0, NULL, HFILL } },
        { &hf_hplc_rf_mac_msdu_len,
          { "MSDU长度", "hplc_rf.mac.msdu_len", FT_UINT16, BASE_DEC,
            NULL, 0x0, NULL, HFILL } },
        { &hf_hplc_rf_mac_restart_cnt,
          { "重启次数", "hplc_rf.mac.restart_cnt", FT_UINT8, BASE_DEC,
            NULL, 0x78, NULL, HFILL } },
        { &hf_hplc_rf_mac_proxy_main,
          { "代理主路径标识", "hplc_rf.mac.proxy_main", FT_BOOLEAN, 8,
            NULL, 0x80, NULL, HFILL } },
        { &hf_hplc_rf_mac_route_total,
          { "路由总跳数", "hplc_rf.mac.route_total", FT_UINT8, BASE_DEC,
            NULL, 0x0F, NULL, HFILL } },
        { &hf_hplc_rf_mac_route_left,
          { "路由剩余跳数", "hplc_rf.mac.route_left", FT_UINT8, BASE_DEC,
            NULL, 0xF0, NULL, HFILL } },
        { &hf_hplc_rf_mac_bcast_dir,
          { "广播方向", "hplc_rf.mac.bcast_dir", FT_UINT8, BASE_DEC,
            VALS(broadcast_dir_vals), 0x03, NULL, HFILL } },
        { &hf_hplc_rf_mac_path_repair,
          { "路径修复标志", "hplc_rf.mac.path_repair", FT_BOOLEAN, 8,
            NULL, 0x04, NULL, HFILL } },
        { &hf_hplc_rf_mac_addr_flag,
          { "MAC地址标志", "hplc_rf.mac.addr_flag", FT_BOOLEAN, 8,
            NULL, 0x08, NULL, HFILL } },
        { &hf_hplc_rf_mac_net_seq,
          { "组网序列号", "hplc_rf.mac.net_seq", FT_UINT8, BASE_DEC,
            NULL, 0x0, NULL, HFILL } },
        { &hf_hplc_rf_mac_osmac,
          { "原始源MAC地址", "hplc_rf.mac.osmac", FT_ETHER, BASE_NONE,
            NULL, 0x0, NULL, HFILL } },
        { &hf_hplc_rf_mac_odmac,
          { "原始目的MAC地址", "hplc_rf.mac.odmac", FT_ETHER, BASE_NONE,
            NULL, 0x0, NULL, HFILL } },

        /* ---- 单跳 MAC 帧头(表11) ---- */
        { &hf_hplc_rf_sh_version,
          { "版本", "hplc_rf.sh.version", FT_UINT8, BASE_DEC,
            NULL, 0xF0, NULL, HFILL } },
        { &hf_hplc_rf_sh_msgtype,
          { "消息类型", "hplc_rf.sh.msg_type", FT_UINT8, BASE_DEC,
            VALS(msg_type_vals), 0x0, NULL, HFILL } },
        { &hf_hplc_rf_sh_msdulen,
          { "MSDU长度", "hplc_rf.sh.msdu_len", FT_UINT16, BASE_DEC,
            NULL, 0x0, NULL, HFILL } },

        /* ---- 信标帧可变区域 ---- */
        { &hf_hplc_rf_beacon_bts,
          { "信标时间戳(BTS)", "hplc_rf.beacon.bts", FT_UINT32, BASE_DEC,
            NULL, 0x0, NULL, HFILL } },
        { &hf_hplc_rf_beacon_src_tei,
          { "源TEI", "hplc_rf.beacon.src_tei", FT_UINT16, BASE_DEC,
            NULL, 0x0, NULL, HFILL } },
        { &hf_hplc_rf_beacon_div_mode,
          { "分集拷贝基本模式", "hplc_rf.beacon.div_mode", FT_UINT8, BASE_DEC,
            NULL, 0x0, NULL, HFILL } },
        { &hf_hplc_rf_beacon_symbol_cnt,
          { "符号数", "hplc_rf.beacon.symbol_cnt", FT_UINT16, BASE_DEC,
            NULL, 0x0, NULL, HFILL } },
        { &hf_hplc_rf_beacon_phase,
          { "相线", "hplc_rf.beacon.phase", FT_UINT8, BASE_DEC,
            VALS(phase_vals), 0x06, NULL, HFILL } },
        { &hf_hplc_rf_beacon_mcs,
          { "MCS", "hplc_rf.beacon.mcs", FT_UINT8, BASE_DEC,
            NULL, 0x0, NULL, HFILL } },
        { &hf_hplc_rf_beacon_pb_size,
          { "载荷PB块大小", "hplc_rf.beacon.pb_size", FT_UINT8, BASE_DEC,
            NULL, 0x0, NULL, HFILL } },

        /* ---- 信标帧载荷(表38) ---- */
        { &hf_hplc_rf_beacon_type,
          { "信标类型", "hplc_rf.beacon.type", FT_UINT8, BASE_DEC,
            VALS(beacon_type_vals), 0x07, NULL, HFILL } },
        { &hf_hplc_rf_beacon_net_cplt,
          { "组网标志位", "hplc_rf.beacon.net_cplt", FT_BOOLEAN, 8,
            NULL, 0x08, NULL, HFILL } },
        { &hf_hplc_rf_beacon_simple,
          { "精简信标标志", "hplc_rf.beacon.simple", FT_BOOLEAN, 8,
            NULL, 0x10, NULL, HFILL } },
        { &hf_hplc_rf_beacon_start_assoc,
          { "开始关联标志", "hplc_rf.beacon.start_assoc", FT_BOOLEAN, 8,
            NULL, 0x40, NULL, HFILL } },
        { &hf_hplc_rf_beacon_use_flag,
          { "信标使用标志", "hplc_rf.beacon.use_flag", FT_BOOLEAN, 8,
            NULL, 0x80, NULL, HFILL } },
        { &hf_hplc_rf_beacon_net_seq,
          { "组网序列号", "hplc_rf.beacon.net_seq", FT_UINT8, BASE_DEC,
            NULL, 0x0, NULL, HFILL } },
        { &hf_hplc_rf_beacon_cco_mac,
          { "CCO MAC地址", "hplc_rf.beacon.cco_mac", FT_ETHER, BASE_NONE,
            NULL, 0x0, NULL, HFILL } },
        { &hf_hplc_rf_beacon_bpc,
          { "信标周期计数(BPC)", "hplc_rf.beacon.bpc", FT_UINT32, BASE_DEC,
            NULL, 0x0, NULL, HFILL } },
        { &hf_hplc_rf_beacon_rf_ch,
          { "本网络无线信道编号", "hplc_rf.beacon.rf_ch", FT_UINT8, BASE_DEC,
            NULL, 0x0, NULL, HFILL } },
        { &hf_hplc_rf_beacon_entry_cnt,
          { "信标条目数", "hplc_rf.beacon.entry_cnt", FT_UINT8, BASE_DEC,
            NULL, 0x0, NULL, HFILL } },
        { &hf_hplc_rf_beacon_bpcs,
          { "帧载荷校验序列(BPCS,CRC32)", "hplc_rf.beacon.bpcs",
            FT_UINT32, BASE_HEX, NULL, 0x0, NULL, HFILL } },

        /* ---- SOF 帧可变区域(表19/30) ---- */
        { &hf_hplc_rf_sof_src_tei,
          { "源TEI", "hplc_rf.sof.src_tei", FT_UINT16, BASE_DEC,
            NULL, 0x0, NULL, HFILL } },
        { &hf_hplc_rf_sof_dst_tei,
          { "目的TEI", "hplc_rf.sof.dst_tei", FT_UINT16, BASE_DEC,
            NULL, 0x0, NULL, HFILL } },
        { &hf_hplc_rf_sof_lid,
          { "链路标识符(LID)", "hplc_rf.sof.lid", FT_UINT8, BASE_DEC,
            NULL, 0x0, NULL, HFILL } },
        { &hf_hplc_rf_sof_frame_len,
          { "帧长", "hplc_rf.sof.frame_len", FT_UINT16, BASE_DEC,
            NULL, 0x0, NULL, HFILL } },
        { &hf_hplc_rf_sof_pb_count,
          { "物理块个数", "hplc_rf.sof.pb_count", FT_UINT8, BASE_DEC,
            NULL, 0xF0, NULL, HFILL } },
        { &hf_hplc_rf_sof_symbol_cnt,
          { "符号数", "hplc_rf.sof.symbol_cnt", FT_UINT16, BASE_DEC,
            NULL, 0x0, NULL, HFILL } },
        { &hf_hplc_rf_sof_bcast,
          { "广播标志", "hplc_rf.sof.bcast", FT_BOOLEAN, 8,
            NULL, 0x02, NULL, HFILL } },
        { &hf_hplc_rf_sof_retrans,
          { "重传标志", "hplc_rf.sof.retrans", FT_BOOLEAN, 8,
            NULL, 0x04, NULL, HFILL } },
        { &hf_hplc_rf_sof_encrypt,
          { "加密标志(预留)", "hplc_rf.sof.encrypt", FT_BOOLEAN, 8,
            NULL, 0x08, NULL, HFILL } },
        { &hf_hplc_rf_sof_div_mode,
          { "分集拷贝基本模式", "hplc_rf.sof.div_mode", FT_UINT8, BASE_DEC,
            NULL, 0xF0, NULL, HFILL } },
        { &hf_hplc_rf_sof_div_ext,
          { "分集拷贝扩展模式", "hplc_rf.sof.div_ext", FT_UINT8, BASE_DEC,
            NULL, 0x0F, NULL, HFILL } },
        { &hf_hplc_rf_sof_pb_size,
          { "载荷PB块大小", "hplc_rf.sof.pb_size", FT_UINT8, BASE_DEC,
            NULL, 0xF0, NULL, HFILL } },
        { &hf_hplc_rf_sof_mcs,
          { "MCS", "hplc_rf.sof.mcs", FT_UINT8, BASE_DEC,
            NULL, 0x0F, NULL, HFILL } },

        /* ---- SACK 帧可变区域(表23/34) ---- */
        { &hf_hplc_rf_sack_recv_result,
          { "接收结果", "hplc_rf.sack.recv_result", FT_UINT8, BASE_DEC,
            NULL, 0x0F, NULL, HFILL } },
        { &hf_hplc_rf_sack_recv_status,
          { "接收状态", "hplc_rf.sack.recv_status", FT_UINT8, BASE_HEX,
            NULL, 0xF0, NULL, HFILL } },
        { &hf_hplc_rf_sack_src_tei,
          { "源TEI", "hplc_rf.sack.src_tei", FT_UINT16, BASE_DEC,
            NULL, 0x0, NULL, HFILL } },
        { &hf_hplc_rf_sack_dst_tei,
          { "目的TEI", "hplc_rf.sack.dst_tei", FT_UINT16, BASE_DEC,
            NULL, 0x0, NULL, HFILL } },
        { &hf_hplc_rf_sack_pb_count,
          { "接收物理块个数", "hplc_rf.sack.pb_count", FT_UINT8, BASE_DEC,
            NULL, 0x07, NULL, HFILL } },
        { &hf_hplc_rf_sack_chan_qual,
          { "信道质量(SNR)", "hplc_rf.sack.chan_qual", FT_UINT8, BASE_DEC,
            NULL, 0x0, NULL, HFILL } },
        { &hf_hplc_rf_sack_site_load,
          { "站点负载", "hplc_rf.sack.site_load", FT_UINT8, BASE_DEC,
            NULL, 0x0, NULL, HFILL } },
        { &hf_hplc_rf_sack_ext_type,
          { "扩展帧类型", "hplc_rf.sack.ext_type", FT_UINT8, BASE_DEC,
            NULL, 0x0F, NULL, HFILL } },

        /* ---- 网间协调帧可变区域(表26) ---- */
        { &hf_hplc_rf_coord_duration,
          { "持续时间", "hplc_rf.coord.duration", FT_UINT16, BASE_DEC,
            NULL, 0x0, NULL, HFILL } },
        { &hf_hplc_rf_coord_bw_offset,
          { "带宽开始偏移", "hplc_rf.coord.bw_offset", FT_UINT16, BASE_DEC,
            NULL, 0x0, NULL, HFILL } },
        { &hf_hplc_rf_coord_nbr_nid,
          { "接收到的邻居网络号", "hplc_rf.coord.nbr_nid", FT_UINT24, BASE_HEX,
            NULL, 0x0, NULL, HFILL } },
        { &hf_hplc_rf_coord_rf_ch,
          { "本网络无线信道编号", "hplc_rf.coord.rf_ch", FT_UINT8, BASE_DEC,
            NULL, 0x0, NULL, HFILL } },

        /* ---- 管理消息头(表58) ---- */
        { &hf_hplc_rf_mgmt_mmtype,
          { "管理消息类型(MMTYPE)", "hplc_rf.mgmt.mmtype", FT_UINT16, BASE_HEX,
            VALS(mgmt_type_vals), 0x0, NULL, HFILL } },
        { &hf_hplc_rf_mgmt_resv,
          { "保留", "hplc_rf.mgmt.reserved", FT_UINT16, BASE_HEX,
            NULL, 0x0, NULL, HFILL } },
    };

    static gint *ett[] = {
        &ett_hplc_rf,
        &ett_hplc_rf_mpdu,
        &ett_hplc_rf_fc,
        &ett_hplc_rf_mac,
        &ett_hplc_rf_mac_hdr,
        &ett_hplc_rf_mgmt,
        &ett_hplc_rf_vf,
        &ett_hplc_rf_vf_beacon,
        &ett_hplc_rf_vf_sof,
        &ett_hplc_rf_vf_sack,
        &ett_hplc_rf_vf_coord,
    };

    proto_hplc_rf = proto_register_protocol(
        "双模通信(高速载波+无线) 数据链路层协议",
        "HPLC_RF",
        "hplc_rf");

    proto_register_field_array(proto_hplc_rf, hf, array_length(hf));
    proto_register_subtree_array(ett, array_length(ett));
}

/* =========================================================================
 * 注册握手指向(handoff)
 * ========================================================================= */
void
proto_reg_handoff_hplc_rf(void)
{
    static gboolean initialized = FALSE;
    dissector_handle_t hplc_rf_handle;

    if (!initialized) {
        hplc_rf_handle = create_dissector_handle(dissect_hplc_rf, proto_hplc_rf);

        /* 数据源为串口 raw_wire 捕获, 挂到 USER0..USER3 封装类型。
         * 实际使用需按抓包文件/监控器导出的 encap 类型调整。 */
        dissector_add_uint("wtap_encap", WTAP_ENCAP_USER0, hplc_rf_handle);
        dissector_add_uint("wtap_encap", WTAP_ENCAP_USER1, hplc_rf_handle);
        dissector_add_uint("wtap_encap", WTAP_ENCAP_USER2, hplc_rf_handle);
        dissector_add_uint("wtap_encap", WTAP_ENCAP_USER3, hplc_rf_handle);

        initialized = TRUE;
    }
}

/*
 * Editor modelines
 *
 * Local Variables:
 * c-basic-offset: 4
 * tab-width: 8
 * indent-tabs-mode: nil
 * End:
 *
 * ex: set shiftwidth=4 tabstop=8 expandtab:
 * :indentSize=4:tabSize=8:noTabs=true:
 */
