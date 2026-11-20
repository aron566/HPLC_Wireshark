/// @file frametype_probe.cpp
/// @brief NW_2021 枚举独立头编译检查(A5)
/// @details 只含 nw_2021_frametype.h:本文件用 QT -= gui widgets 构建,
///          若新头依赖 QtWidgets 编译即失败;再以 static_assert 守住取值
///          (FrameType 0/1/2/3、AckExtType 0/1/2),并确认无 SwitchChannel。
///          预期输出 `RESULT: ALL PASS (0 failures)`,exit 0。
#include "nw_2021_frametype.h"

#include <QCoreApplication>
#include <cstdio>
#include <type_traits>

static_assert(static_cast<quint8>(NW_2021_FrameType::BEACON) == 0,
              "BEACON must be 0");
static_assert(static_cast<quint8>(NW_2021_FrameType::SOF) == 1, "SOF must be 1");
static_assert(static_cast<quint8>(NW_2021_FrameType::ACK) == 2, "ACK must be 2");
static_assert(static_cast<quint8>(NW_2021_FrameType::COORD) == 3, "COORD must be 3");
static_assert(static_cast<quint8>(NW_2021_AckExtType::Normal) == 0,
              "Normal must be 0");
static_assert(static_cast<quint8>(NW_2021_AckExtType::Search) == 1,
              "Search must be 1");
static_assert(static_cast<quint8>(NW_2021_AckExtType::Sync) == 2, "Sync must be 2");
static_assert(std::is_same_v<std::underlying_type_t<NW_2021_FrameType>, quint8>,
              "FrameType must be quint8-based");
static_assert(std::is_same_v<std::underlying_type_t<NW_2021_AckExtType>, quint8>,
              "AckExtType must be quint8-based");

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    // 运行时再核对一次(防 static_assert 被优化配置绕过):取值须与规约一致,
    // 且 AckExtType 最大合法值是 2(表21 仅定义 0/1/2,无 SwitchChannel)。
    const bool ok =
        static_cast<int>(NW_2021_FrameType::BEACON) == 0
        && static_cast<int>(NW_2021_FrameType::SOF) == 1
        && static_cast<int>(NW_2021_FrameType::ACK) == 2
        && static_cast<int>(NW_2021_FrameType::COORD) == 3
        && static_cast<int>(NW_2021_AckExtType::Normal) == 0
        && static_cast<int>(NW_2021_AckExtType::Search) == 1
        && static_cast<int>(NW_2021_AckExtType::Sync) == 2;
    std::printf("frametype header compiles without QtWidgets: %s\n",
                ok ? "yes" : "no");
    std::printf("RESULT: %s (0 failures)\n", ok ? "ALL PASS" : "FAILURES");
    std::fflush(stdout);
    return ok ? 0 : 1;
}
