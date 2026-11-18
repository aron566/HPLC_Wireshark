# Reader 信封必填探针(A-6)
#
# 断言:3C 回放 bin 与裸 hex 两处构造点必填信封头字段
# (epoch_ms/timestamp/phr_mcs/option/channel/is_rf),与 GW_2022 解析器
# decode_envelope 的复核值恒等(同字节同公式)。
#
# 做法:3 帧合成 BEACON(FCH CRC24 有效,NTB 步进 25000 tick = 1 ms)
# 走真实 ReaderWorker(文件回放 + 裸 hex),再经同一 GW_2022_Parser
# 解析,断言 reader 值 = 构造值(非默认值)且 = 解析器复核值。
# 断言进退出码(失败返回非 0)。
