# NW_2026 OFDMA 类型 1：UL-OFDMA 数据帧构造说明

结论：类型 1 确实可能是 UL-OFDMA 数据帧（规约原文），parser 应当实现其 PB 解析。
构造物：trigger（类型 2）+ UL-OFDMA 数据帧（类型 1）+ DL-SACK 对照帧（类型 1），
均已用 38852670 树生产 parser + pb_assembler 实测。

## 规约依据（SPLC 第 4 部分，261023 md 版）

- 表 26 OFDMA FC：多站点标识 (4,0,1)；OFDMA 帧类型 (4,1,2)：
  0=DL-OFDMA，1=DL-OFDMA 的 SACK 帧 / UL-OFDMA 帧，2=UL-OFDMA 的 trigger 帧，
  3=UL-OFDMA 的 SACK 帧；站点数 (4,6,2)：0=1 站、1=2 站、2=3 站、3=4 站；
  eFC 符号个数 (5,0,2)；PL 符号数 (6,0,9)；帧长 (8,0,12)×10us；
  短网络标识高位 (12,0,1)；标准版本号 (12,4,4)=2 走 SPLC。
- 类型 1 不携带 eFC（原文）：
  - DL-OFDMA 的 SACK 子项：帧载荷承载 SACK 数据，单个物理块、固定 TMI 26 传输。
  - UL-OFDMA 子项：帧载荷承载数据帧数据，物理块的 TMI 由 trigger 帧的 eFC 中的 TMI 指示。
  即：UL-OFDMA 数据帧的 TMI 不在本帧内，必须取 trigger 上下文。
- 表 28 trigger eFC（16B）：TF 个数 (0,0,2)；每站点：PB 个数 1b（0→1 块、1→2 块）、
  TEI 12b、TMI 5b、RU 4b、Tx 功率回退 3b；CRC24 在 eFC 字节 13-15。
- 表 27 DL-OFDMA eFC（16B）：同表 28 几何，Tx 回退位置为 SACK RU；CRC 在 13-15。
- 表 29 UL-SACK eFC：4×[TEI 12b + 接收状态 4b + 保留 8b]，每站点 3B。
- PB 块（表 47 + §5.1.3.2.1）：头 4B（序列号 16b LE + 聚合标志 byte2 bit0 + 保留 15b）
  + 块体（pb_size−8）+ 保留 1B + CRC24 3B；CRC 覆盖块内前 pb_size−3 字节，末 3B 小端存储。
- SPLC TMI→PB 大小（nw_2026_pb_table.h）：0-8→520、9-15→264、16-20→136、
  21-25→72、26-27→16。
- 表 49/表 50（16B 块）：体首 nibble=报文 ID（0=DL-OFDMA SACK），内容=源 TEI 12b +
  接收状态 2b（bit0=第 1 块、bit1=第 2 块，1=通过）+ 保留。

## 三帧构成（MPDU，FCH byte0 起；from_raw 前加 isRF=0 一字节喂入）

### 1. trigger（类型 2，32B）— nw2026_trigger.bin
- FCH：01 01 f0 ff 45 00 64 00 00 02 00 00 20 c5 07 d3
  - byte0=0x01（SOF）；源 TEI 1（CCO）；目的 TEI 0xFFFF（广播，多站点）；
    byte4=0x45（multi=1、类型 2、频段 0、站点数码 1=2 站）；
    eFC 符号数码 0；PL 符号数 100；帧长 0x200（×10us）；byte12=0x20（版本 2）；
    字节 13-15=FCH FCS。
- eFC（16-31B）：10 00 59 38 00 d5 00 00 00 00 00 00 00 07 02 57
  - TF 个数码 0（实际 2）；站 0：PB 数 1、TEI 2、TMI 18（→136B）、RU 5、回退 0；
    站 1：PB 数 2、TEI 3、TMI 21（→72B）、RU 6、回退 0；站 2/3 未用补 0；
    字节 13-15=eFC CRC。
- 无数据 PB（符合规约：trigger 只带 eFC）。

### 2. UL-OFDMA 数据帧（类型 1，296B）— nw2026_ul_ofdma_data.bin
- FCH：01 ff 1f 00 43 00 64 00 00 02 00 00 20 7c 91 cc
  - 源 TEI 0xFFFF（广播：多站点同时上行，无单一发送方）；目的 TEI 1（CCO）；
    byte4=0x43（multi=1、类型 1、频段 0、站点数码 1=2 站）；
    byte5=0（无 eFC，该字段无效、默认填 0，符合表 26 注）；
    PL 符号数 100；帧长 0x200；byte12=0x20；字节 13-15=FCH FCS。
- 站载荷自 byte16 起（无 eFC）：
  - 站 0：1×136B PB（TMI 18，trigger 指定），@16，头 seq 0，体为 "UL-STA0-" 循环，PBCS 闭合。
  - 站 1：2×72B PB（TMI 21，trigger 指定），@152（seq 0）、@224（seq 1），
    体为 "UL-STA1-PB0-"/"UL-STA1-PB1-" 循环，PBCS 闭合。
- 总长 16+136+72+72=296B。

### 3. DL-SACK 对照帧（类型 1，32B）— nw2026_dl_sack.bin
- FCH：01 ff 1f 00 03 00 04 00 00 01 00 00 20 1c 32 e0
  - 源 TEI 0xFFFF；目的 TEI 1；byte4=0x03（multi=1、类型 1、站点数码 0=1 站）；
    PL 符号数 4（测试值）；帧长 0x100；byte12=0x20；字节 13-15=FCH FCS。
- 单个 16B PB（固定 TMI 26），@16：头 seq 0；体=20 00 03 00 00 00 00 00
  （MsgID 0、源 TEI 2、接收状态 0b11=两块皆通过）；保留 0；PBCS 闭合。

## C++ 实测（38852670 树，nw2026_ul_probe/，offscreen）

生产 parser（from_raw）：
- trigger：accept=1、FCH OK、ver=2、src=1、dst=4095、frame_len=512、
  pb_num=0（正确：trigger 本无数据 PB）。
- ul_data：accept=1、FCH OK、ver=2、src=4095、dst=1、frame_len=512，
  但 pb_num=0、msdu.present=0——有效 UL-OFDMA 数据帧的载荷现不可见（缺口实锤）。
- dl_sack：accept=1、FCH OK、src=4095、dst=1、frame_len=256，
  pb_num=0——SACK 内容同样不可见。

生产 pb_assembler 按 trigger 布局直跑 UL 帧：
- sta0：base=16、1×136B，all_crc_ok=1，体 128B。
- sta1：base=152、2×72B，all_crc_ok=1，体 128B。
- sack：base=16、1×16B，all_crc_ok=1，体 8B；体解码 MsgID=0、SrcTEI=2、RxStatus=0b11。
即：帧本身按规约无误，分段与 CRC 均可由现有 assembler 验证；缺的是 parser 的类型 1 分派。

## parser 实现形状（供拍板，不是已实现）

类型 1 必须先分两个子项，不能一律按 SACK 处理：
- DL-SACK 子项：可独立解析——单 16B PB、固定 TMI 26、基址 16、体按表 49/50 展示。
  判别：总长 32B（FCH+单 16B PB）且 PB CRC 通过且体首 MsgID=0。
- UL-OFDMA 数据子项：需 trigger 上下文——每站（PB 数、TMI）取自 trigger eFC，
  站载荷自 byte16 起按站依序分段。
  - 方案 A（规约正解）：parser/dispatcher 记住最近 trigger eFC（按 NID），类型 1 到达时套用。
  - 方案 B（无状态兜底）：按 SPLC 尺寸集 {16,72,136,264,520} 做 CRC 引导分段；
    但站归属仍需站点数（FCH 有）+ 每站块数（帧内无），只能启发式。
- 模型缺口（不止改分支）：现 MpduInfo 只有一组 (pb_num, pb_size)，装不下“每站不同尺寸”；
  UL 帧需逐站分段结构（站 TEI、块数、尺寸、基址）；树侧 PB 循环同样是均一尺寸模型，
  且多站点基址一律按 32 起（类型 1 正确基址是 16，无 eFC）。

## 树侧另有两处与类型 1 直接相关（代码已读，未改）

- eFC 分支 guard 含 oft==1（nw_2026_tree.cpp 约 356 行）：类型 1 无 eFC，但树会把
  站载荷 byte16 起按表 27 布局渲染成 eFC 字段——UL 帧和 DL-SACK 帧都会被误读一段 eFC。
  修法：guard 改为 oft==0/2（+ oft==3 走表 29 分支不变）。
- 无块原因标签（约 529-535 行）：ofdma_type 1 与 3 共用“OFDMA SACK 帧无数据物理块”。
  类型 1 的 UL-OFDMA 数据子项不是 SACK，应分标签（无 trigger 上下文时标“UL-OFDMA 数据，
  需 trigger 上下文”而非 SACK）。
- 顺带（3 站以上 eFC 显示）：表 27/28 的 SACK RU / Tx 回退 3b 是每站都有，
  但树只在站 0 处推进 3b（“仅首站”），3 站以上帧的站 2、站 3 eFC 字段会整体错位 3b 起；
  2 站帧的站 0、站 1 显示不受影响。

## 测试取值说明（合法范围内，非文档值）

SNID 0、TEI 1/2/3、RU 5/6、PL 符号数 100（UL/trigger）与 4（SACK）、帧长、
字母体填充、PB 序号。trigger 的 eFC 符号数码 0 为占位值（parser 不用该字段）；
DL-SACK 的站点数码 0（1 站）是按单 16B PB 体含单一源 TEI 取的，规约未明文规定
DL-SACK 的站点数取值。生成脚本：build_nw2026_ul_ofdma.py（重跑即再生三帧）。
