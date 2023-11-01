#pragma once

#include <fmt/core.h>

namespace Motorino::Logger {

template<typename ...Args>
inline auto info(
    fmt::format_string<Args...> format,
    Args&&... args
) -> void {
    fmt::print("[INFO] ");
    fmt::print(format, std::forward<Args>(args)...);
}

template<typename ...Args>
inline auto warn(
    fmt::format_string<Args...> format,
    Args&&... args
) -> void {
    fmt::print("\x1b[1;33m[WARN]\x1b[0m ");
    fmt::print(format, std::forward<Args>(args)...);
}

template<typename ...Args>
inline auto error(
    fmt::format_string<Args...> format,
    Args&&... args
) -> void {
    fmt::print("\x1b[1;31m[ERROR]\x1b[0m ");
    fmt::print(format, std::forward<Args>(args)...);
}

}