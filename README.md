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
- **界面语言**:中文 / English,默认跟随系统语言;`设置` 对话框可选语言
  (动态文本立即生效,窗口框架重启后完全生效)

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
   `aron566/HPLC_Wireshark` 仓库 `main` 分支的 `update.json`,版本 1.0.2)
5. **配置文件 `config.ini`**(exe 同目录,首次启动自动生成带注释模板):
   更新检查地址、语言、串口参数、过滤条件等均可在其中修改(也可在
   `设置` 对话框修改串口/语言/过滤器——自动写回)。删除该文件后下次
   启动重建默认配置。

## 帧封装格式(串口 0x3C/0x3E 与回放 .bin)

监控器对串口/设备输入与回放文件采用同一套帧封装,由
`src/io/serialreader.cpp`(读取/反转义)与 `src/io/playbackwriter.h`(导出)实现。

### 帧边界与转义

```
串口/文件帧: 0x3C <数据> 0x3E
```

- 以 `0x3C` 起始、`0x3E` 结束,读取端按这两个哨兵切帧
- 数据内的保留字符用 **0x3D 转义**:`0x3D` + `(0xFF ^ 原字节)`

| 原字节 | 转义后 | 反转义规则 |
|--------|--------|-----------|
| 0x3C   | `0x3D 0xC3` | 0x3D 后一字节 `0xFF - b` 还原 |
| 0x3E   | `0x3D 0xC1` | 同上 |
| 0x3D   | `0x3D 0xC2` | 同上 |

### 帧内数据布局(实时串口帧)

反转义后的数据(去掉 0x3C/0x3E)为:

```
[dlen 2B LE][ts 4B LE][phr_mcs 1B][option 1B][channel 1B][isRF 1B][MPDU...]
  offset 0-1     2-5        6           7            8          9      10..
```

| 字段 | 长度 | 含义 |
|------|------|------|
| dlen | 2B LE | 数据长度(**读取端不校验**,按 MPDU+6 填即可) |
| ts   | 4B LE | 时间戳(固件计数/ms;BEACON Info 列展示) |
| phr_mcs | 1B | 物理层 MCS/调制(HRF) |
| option | 1B | 物理层选项 |
| channel | 1B | 信道(HRF 信道号 / PLC 频段) |
| isRF | 1B | 0=PLC 载波,非 0=HRF 无线(media_id) |
| MPDU | 变长 | 完整 MPDU:MPDU_BASE(FCH 16B + PB...)或 BEACON/ACK/COORD |

固件侧输出示例(单块 SOF):`0x3C` + 上述字节 + `0x3E`。

### 回放 .bin 文件格式

导出(菜单 `捕获 → 导出...`)与回放使用**相同帧流**,差异仅在数据最前面
**多 8 字节 BCD 绝对时间标签**(起始时间):

```
[BCD 时间 8B][dlen 2B LE][ts 4B LE][phr_mcs][option][channel][isRF][MPDU...]
```

BCD 时间标签布局(本地时间,与解码器 `has_time_tag` 头同构):

| 字节 | 内容 | 取值(BCD) |
|------|------|-----------|
| 0 | 年-2000 | 00-99 |
| 1 | 月 | 01-12 |
| 2 | 日 | 01-31 |
| 3 | 时 | 00-23 |
| 4 | 分 | 00-59 |
| 5 | 秒 | 00-59 |
| 6 | 毫秒百位 | 0-9 |
| 7 | 毫秒低两位 | 00-99 |

回放时 `serialreader` 用 `looks_like_bcd_time()` **自动识别**该字段:带时间
标签的 bin 恢复每帧原始捕获时刻(Time 列/Delta/再次导出均以原始时间为准);
**无该字段的旧 bin 自动回退本地时间**,无需手动配置。串口实时帧不含此字段。

### 裸 hex 行(raw 模式)

`RawHex` 模式直接喂 16 进制文本,每行剥空白后按 2 字符一字节解析,内容为
**log hex 行同款**:`[isRF 1B][MPDU...]`(首字节非 0 视为 HRF)。

## 打包发布(安装包制作)

### 需要的工具软件

| 工具 | 用途 | 获取方式 |
|------|------|----------|
| Qt 6.10.1 mingw_64 + MinGW 13.1.0 | 编译(qmake/mingw32-make) | Qt 在线安装器 |
| `windeployqt`(Qt 自带) | 收集 Qt 运行库 DLL 到 release/ | 随 Qt 安装,`<Qt>\6.10.1\mingw_64\bin\windeployqt.exe` |
| NSIS 3.x | 安装包制作 | <https://nsis.sourceforge.io/Download>(便携 zip 解压到工程 `tools/nsis-3.09/` 即可,免安装) |
| Python 3 + Pillow | 重新生成应用图标(可选) | `pip install pillow` |
| GitHub 账号 + PAT | 发布 release / 上传安装包(仓库需公开才能被免鉴权拉取) | GitHub Settings → Developer settings |

> NSIS 脚本里含中文字符串,文件必须以 **UTF-8 BOM** 编码保存(否则报
> `Bad text encoding`)。脚本文件已被 `tools/` 目录加入 `.gitignore`,
> NSIS 便携包无需随仓库提交。

### 打包步骤

```bash
# 1.(可选)重新生成图标:icons/app.ico 与 icons/app.png
python scripts/make_icon.py

# 2.一键打包:全量构建 → windeployqt → NSIS 生成安装包
bash scripts/package.sh 1.0.2
#   产物:dist/BPLC_STA_Monitor_Setup_v1.0.2.exe

# 3.安装包验证(静默安装/升级,免 UAC)
dist/BPLC_STA_Monitor_Setup_v1.0.2.exe /S                 # 静默安装到默认目录
dist/BPLC_STA_Monitor_Setup_v1.0.2.exe /S /D=C:\my\dir     # 静默装到指定目录
"%LOCALAPPDATA%\Programs\BPLC_STA_Monitor\uninstall.exe" /S  # 静默卸载
```

安装器特性:per-user 安装(免 UAC)、升级前自动关闭运行中的程序、覆盖式更新
(旧的 exe/DLL 直接替换)、生成开始菜单/桌面快捷方式与卸载项。

### 发布与更新流程

1. **版本号**:改 `src/app/mainwindow.cpp` 顶部 `kAppVersion` 与
   `BPLC_STA_Monitor.pro` 的 `VERSION`(两者保持一致)
2. `bash scripts/package.sh <新版本>` 得到安装包
3. **推代码 + 建 release**:在 GitHub 仓库建 tag/release(如 `v1.0.2`),
   上传安装包为 release asset
4. **改 `update.json`**(仓库根,提交推送):
   `latest-version` 抬高新版本号,`download-url` 指向 release asset 地址
   (形如 `https://github.com/<owner>/<repo>/releases/download/<tag>/<文件名>.exe`)
5. 用户端:程序内 `帮助 → 检查更新` → 发现新版本 → 下载安装包 → 运行
   即覆盖安装(自动关闭旧进程,完成后即可用新版本)

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
├── icons/                      应用图标(app.ico / app.png)
├── scripts/                    打包与辅助脚本(package.sh / installer.nsi / make_icon.py)
├── dist/                       安装包产物(本地,不入库)
├── samples/                    回放转换/校验脚本、无头回归测试
└── BPLC_STA_Monitor.pro
```

## 第三方组件

- **QSimpleUpdater**(`src/updater/`,MIT License):Qt6 兼容的软件更新检查/
  下载库,来自 <https://github.com/alex-spataru/QSimpleUpdater>;许可副本
  见 `src/updater/LICENSE.md`,上游文件保持原样未改动。

## 许可

本仓库代码在显式授权前保留所有权利;第三方组件按其各自许可(见上)。
