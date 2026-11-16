# plugin_autotest.pro - 本目录探针的构建入口说明(占位,不产生目标)
#
# 该探针不是纯单元测试:它经 MainWindow 的公开测试接口驱动真实主窗口
# (加载插件 -> 回放 bin -> 等插件线程排空 -> 逐 tab 截图 -> 退出),
# 因此必须与主程序一起编译。
#
# 为什么入口不在这里:BPLC_STA_Monitor.pro 内部大量使用相对路径
# (main.cpp / BPLC_STA_Monitor.qrc / src/... ),qmake 按「顶层 .pro 所在目录」
# 解析它们。若以本文件为顶层,这些路径会指向 unit_test/app/plugin-autotest/,
# 构建必然失败。故可用的独立入口放在仓库根:
#
#   <repo>/plugin_autotest.pro
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
#
# 本文件保持为空目标,使误以本目录为顶层时 qmake/make 静默成功而非报错,
# 真实入口见上方路径。

TEMPLATE = subdirs
SUBDIRS =
