#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <memory>
#include <string>

/// 全局日志初始化，调用一次即可
inline void InitLogger(const std::string& level = "info",
                       const std::string& file = "")
{
    std::vector<spdlog::sink_ptr> sinks;
    sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());

    if (!file.empty()) {
        sinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(file, true));
    }

    auto logger = std::make_shared<spdlog::logger>("backend", sinks.begin(), sinks.end());
    spdlog::set_default_logger(logger);

    // 设置日志级别
    if (level == "debug")       spdlog::set_level(spdlog::level::debug);
    else if (level == "warn")   spdlog::set_level(spdlog::level::warn);
    else if (level == "error")  spdlog::set_level(spdlog::level::err);
    else                        spdlog::set_level(spdlog::level::info); // default

    spdlog::flush_on(spdlog::level::warn);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%s:%#] %v");

    spdlog::info("Logger initialized. Level={}, File={}", level,
                 file.empty() ? "(stdout only)" : file);
}
