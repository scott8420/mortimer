#pragma once
// core/ -- the GTK-free half of @APP@ (the two-layer seam, docs/two-layer-seam.md).
// No gtkmm include is possible here: @BIN@_core doesn't link it, so a stray UI
// include fails to compile. The model goes here, and the selftest proves it
// headless. This first file is just the version, so the seam exists on day one.
#include <string>

namespace @NS@::core {

std::string version();   // "0.1.0" -- from CMake's project(VERSION)

}  // namespace @NS@::core
