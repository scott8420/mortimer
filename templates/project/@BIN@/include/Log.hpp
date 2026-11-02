#pragma once
#include <spdlog/spdlog.h>
#include <memory>

// Log -- per-area logging, runtime-toggleable (CANON). Every concern gets a
// named spdlog logger on one shared stdout sink. Default level INFO; to trace:
//
//   @BIN_UPPER@_DEBUG=debug ./build/@BIN@        every area at debug
//   @BIN_UPPER@_DEBUG=trace:ui ./build/@BIN@     one area at trace
//
// Areas are named by CONCERN, not by file. `ui` is where Mortimer's generated
// half speaks: an unhooked hook says so there, once.
namespace @NS@::log {

enum class Area { App, Ui, Registry, Io };

// May be null before init() -- callers guard: if (auto lg = log::get(area)) ...
std::shared_ptr<spdlog::logger> get(Area area);

// Stand up the area loggers (reads @BIN_UPPER@_DEBUG). Call once, first thing.
void init();

void set_level(Area area, spdlog::level::level_enum level);

}  // namespace @NS@::log
