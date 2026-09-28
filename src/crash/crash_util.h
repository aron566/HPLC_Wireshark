// crash_util.h - 内部小工具(无 Qt 依赖)
#pragma once

#include <string>

namespace crash_util {

// 当前可执行文件所在目录(末尾不带分隔符)。失败返回空串。
std::string exe_dir();

// 确保目录存在(递归创建)。成功返回 true。
bool ensure_dir(const std::string& path);

// 路径拼接
std::string join(const std::string& a, const std::string& b);

} // namespace crash_util
