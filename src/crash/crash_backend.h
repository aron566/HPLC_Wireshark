// crash_backend.h - 后端实现内部接口(不对外,只给 crash_handler.cpp 用)
//
// 每个 backend_*.cpp 实现对应的一对函数;未编译的后端由 backend_stub.cpp
// 提供空实现,保证链接通过。
#pragma once

#include "crash_handler.h"

#include <string>

std::string backend_sentry_install(const CrashHandler::Options& opts);
std::string backend_crashpad_install(const CrashHandler::Options& opts);
void backend_sentry_shutdown();
void backend_crashpad_shutdown();
