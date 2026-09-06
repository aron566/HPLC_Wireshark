# BPLC STA Monitor

BPLC/HRF(HPLC) 协议 STA 报文监控上位机(Windows,Qt 6 / C++17)。

原为 Python 版 `BPLCMonitor`,本项目为 Qt6/C++ 重写版:解析 STA 固件经调试串口
输出的 `0x3C … 0x3E` 封装帧,并以字段树 + 十六进制字节联动高亮展示各层协议
结构。

## 功能特性

- **四类帧解析**:SOF(数据)、BEACON、ACK、COORD,含 FCH 位域逐字段拆分、
  FCH CRC24 / PB CRC24 / MSDU CRC32 / BEACON 载荷 CRC 校验与 OK/FAIL 显示
- **多 PB 块重组**:支持分片 MSDU 跨多物理块(PB)重组,树中逐块展示
  `PB Header / PB Body(+Padding) / PB CRC24`,字段可点选高亮到原始字节
- **MSDU 消息解析**:标准头 / 简头 MSDU_BASE,MMe 头(MMType 2B + RSV 2B,
  实测 16bit 宽)、8 类 MMe 消息 + APP EventPacket 事件上报,含
  DiscoveryNodeList(位图 TEI + 发现计数)、UpRouteEntryList(路由类型含义)、
  SuccessRateReport、Beacon Load(STA/PCO/CCO 信标载荷)等字段级展示
- **字段语义注解**:MSDUType / SendType / 路由类型 / Link ID / 报文优先级等
  枚举中文含义,周期/超时(s/ms)、信道质量(dB)、成功率(%)等单位齐全,
  保留字段按协议补全并标注位宽
- **帧列表**:`# | Time | Delta | Source | Destination | Protocol | Frame Type |
  MSDU Type | MSDU Seq | Length | Info`,支持表达式过滤(协议/类型/源目地址)
- **数据来源**:串口捕获或离线日志/二进制回放(log 行 `isRF+MPDU` 与
  `0x3C…0x3E` bin 两种)

## 环境与构建

| 组件   | 版本 |
|--------|------|
| Qt     | 6.10.1(mingw_64) |
| 编译器 | MinGW 13.1.0 |
| 构建   | qmake + mingw32-make(非 shadow 构建) |

```bash
export PATH="/c/Qt/Tools/mingw1310_64/bin:/c/Qt/6.10.1/mingw_64/bin:$PATH"
qmake BPLC_STA_Monitor.pro
mingw32-make -j4
# 产物:release/BPLC_STA_Monitor.exe
```

> 注:工程代码采用 clangd/LSP 时对 Qt 头会有假阳性报错,以本机
> mingw32-make 编译结果为准。

## 使用

1. **串口模式**:工具栏选择 COM 口与波特率后开始捕获(停止/暂停/清空)
2. **回放模式**:打开 log/bin 回放文件离线分析;`samples/log2bin.py` 可把
   Python 上位机 log(hex 行)转成 bin
3. 点帧列表行 → 上方协议字段树 → 点字段行 → 下方十六进制区对应字节高亮;
   多 PB 报文每块 Header/Body/CRC24 均逐块可点
4. **检查更新**:菜单 `帮助 → 检查更新`,更新清单地址见
   `src/app/mainwindow.cpp` 顶部 `kUpdateUrl`(当前指向 GitHub
   `aron566/HPLC_Wireshark` 仓库 `main` 分支的 `update.json`,版本 1.0.0)

## 更新清单格式

`update.json`(放仓库根或发布服务器):

```json
{
  "updates": {
    "windows": {
      "latest-version": "1.0.1",
      "download-url": "https://example.com/BPLC_STA_Monitor.exe",
      "changelog": "修复 xxx",
      "mandatory-update": false
    }
  }
}
```

程序发现远端版本高于本地(`1.0.0`)即弹窗询问下载。

## 目录结构

```
├── main.cpp                    入口(qMain)
├── src/
│   ├── common/                 帧/消息数据结构(含位域节点、高亮坐标)
│   ├── protocol/               帧解析器(BPLC MPDU/MSDU/BEACON)+ 统计
│   ├── io/                     串口读取与帧分发
│   ├── ui/                     帧列表模型、协议字段树、十六进制视图
│   ├── app/                    主窗口、配置持久化
│   └── updater/                QSimpleUpdater(第三方,MIT)
├── samples/                    回放转换/校验脚本、无头回归测试
└── BPLC_STA_Monitor.pro
```

## 第三方组件

- **QSimpleUpdater**(`src/updater/`,MIT License):Qt6 兼容的软件更新检查/
  下载库,来自 <https://github.com/alex-spataru/QSimpleUpdater>;许可副本
  见 `src/updater/LICENSE.md`,上游文件保持原样未改动。

## 许可

本仓库代码在显式授权前保留所有权利;第三方组件按其各自许可(见上)。
