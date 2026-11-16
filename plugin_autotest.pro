# plugin_autotest.pro - 插件面板自动化截图测试的独立构建入口(仓库根)
#
# 为什么放在仓库根:BPLC_STA_Monitor.pro 内部大量使用相对路径
# (main.cpp / BPLC_STA_Monitor.qrc / src/... ),qmake 按「顶层 .pro 所在目录」
# 解析它们。把本文件放在仓库根,include 后这些相对路径才指向正确位置。
#
# 该探针不是纯单元测试:它经 MainWindow 的公开测试接口驱动真实主窗口
# (加载插件 -> 回放 bin -> 等插件线程排空 -> 逐 tab 截图 -> 退出),
# 因此必须与主程序一起编译。本 .pro 只是把「主程序 + CONFIG+=plugin_autotest」
# 这一组合固定下来,使它可以单独 qmake/make,不必记参数。
#
# 用法:
#   mkdir -p build-autotest && cd build-autotest
#   qmake <repo>/plugin_autotest.pro
#   make -j2
#   QT_QPA_PLATFORM=offscreen ./BPLC_STA_Monitor \
#       --plugins-dir <repo>/src/plugin/examples \
#       --replay <repo>/unit_test/ci/roundtrip/roundtrip_test.bin \
#       --plugin-autotest <shot_dir>
#
# 断言(与 unit_test/ci/probe_suite.sh 的 p_app_plugin_autotest 一致):
#   exit 0;日志含 "[autotest] reader finished" 与 "[autotest] shoot start";
#   <shot_dir>/main.png 非空且至少 1 张 plugin_*.png。

# 与 CI 一致:release + 不启用 crash 后端(避免探针环境缺 crashpad 依赖)
CONFIG += release
CONFIG -= debug
CONFIG += crash_no_default
CONFIG += plugin_autotest

include($$PWD/BPLC_STA_Monitor.pro)
