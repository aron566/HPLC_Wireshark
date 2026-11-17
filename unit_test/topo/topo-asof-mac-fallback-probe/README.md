# topo-asof-mac-fallback-probe

回归探针：路由变更表 `event_mac_text` 的 `mac_to_tei` 回退
（修法 `fix/topo-asof-mac-fallback`，main 6fd98bc 之后）。

背景：`ChangeProxyCnf` 体里无 MAC，事件不学 TEI=1；信标/逐帧路径
`note_restart_origin` 学到的 TEI→MAC 只进 `TopoState::mac_to_tei`，
从不进渲染用的时点映射 asof，故路由表里 TEI=1 显示“未知”。
修法：asof 查不到时回退查 `mac_to_tei`，查到写回 asof 一次。

覆盖（见 main.cpp 注释 T0–T8）：逐帧学习只进反向表不建 nodes、
回退显示 MAC、写回 asof、未知仍显示未知、帧内直带优先、
写回条目参与后续 leaves 反查、st 为空不崩溃；同一 NID 下同一 TEI
对应多个 MAC 时返回未知且结果稳定、不同 NID 隔离、唯一映射仍可回退。
该探针已注册到 `unit_test/ci/probe_suite.sh`，并由
`.github/workflows/protocol-regression.yml` 的 `frames-gw-storage` job 执行。
修前代码跑本探针时 T1 失败（反向验证见提交说明）。

用法：

```bash
./build.sh
QT_QPA_PLATFORM=offscreen ./build/asof_mac_fallback_probe  # exit 0 = 全部通过
```
