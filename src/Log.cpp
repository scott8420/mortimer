#include "Log.hpp"

#include <spdlog/sinks/stdout_color_sinks.h>

#include <array>
#include <cstdlib>
#include <string>

namespace mortimer::log {

namespace {
constexpr int kAreaCount = 7;

const char* area_name(Area a) {
    switch (a) {
        case Area::App:      return "app";
        case Area::Shell:    return "shell";
        case Area::Registry: return "registry";
        case Area::Recents:  return "recents";
        case Area::Io:       return "io";
        case Area::Design:   return "design";
        case Area::Canvas:   return "canvas";
    }
    return "?";
}

std::array<std::shared_ptr<spdlog::logger>, kAreaCount>& loggers() {
    static std::array<std::shared_ptr<spdlog::logger>, kAreaCount> a{};
    return a;
}
}  // namespace

void init() {
    auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto& a = loggers();
    for (int i = 0; i < kAreaCount; ++i) {
        const auto area = static_cast<Area>(i);
        auto lg = std::make_shared<spdlog::logger>(area_name(area), sink);
        lg->set_level(spdlog::level::info);   // default INFO; raise one area to TRACE to debug
        a[static_cast<std::size_t>(i)] = lg;
    }
    // MORTIMER_DEBUG=level[:area]  e.g. "debug", "trace:canvas" (as the apps it makes do).
    if (const char* env = std::getenv("MORTIMER_DEBUG")) {
        std::string spec = env, area;
        if (const auto c = spec.find(':'); c != std::string::npos) { area = spec.substr(c + 1); spec.resize(c); }
        const auto lvl = spdlog::level::from_str(spec);
        for (int i = 0; i < kAreaCount; ++i)
            if (area.empty() || area == area_name(static_cast<Area>(i))) a[static_cast<std::size_t>(i)]->set_level(lvl);
    }
}

std::shared_ptr<spdlog::logger> get(Area area) {
    return loggers()[static_cast<std::size_t>(area)];
}

void set_level(Area area, spdlog::level::level_enum level) {
    if (auto lg = get(area)) lg->set_level(level);
}

}  // namespace mortimer::log
