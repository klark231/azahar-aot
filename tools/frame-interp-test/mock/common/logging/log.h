// Test stand-in for Azahar's logging: keeps the formatted messages.
#pragma once
#include <format>
#include <string>
#include <vector>
inline std::vector<std::string>& MockLog() {
    static std::vector<std::string> log;
    return log;
}
#define LOG_INFO(cls, ...) MockLog().push_back(std::format(__VA_ARGS__))
