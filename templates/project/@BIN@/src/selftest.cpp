// @BIN@_selftest -- the headless core harness. Runs the GTK-free core with no
// display. Every model verb you add earns a check here (CANON: a claim isn't a
// fact until a consumer exercises it).
//
//   ./build.sh && ./build/@BIN@_selftest
//
// The app's UNDO has a gate of its own: @BIN@_undo_gate (generated/UndoGate.cpp,
// rewritten from your forms' models), which every build runs.
#include "core/Version.hpp"

#include <iostream>
#include <string>

namespace {
int g_pass = 0, g_fail = 0;
void check(const std::string& name, bool ok, const std::string& detail = "") {
    if (ok) { ++g_pass; std::cout << "  PASS  " << name; }
    else    { ++g_fail; std::cout << "  FAIL  " << name; }
    if (!detail.empty()) std::cout << "  (" << detail << ")";
    std::cout << "\n";
}
}  // namespace

int main() {
    std::cout << "@BIN@_selftest\n";
    check("version is set", !@NS@::core::version().empty(), @NS@::core::version());

    std::cout << "-----------------------------------------------\n"
              << g_pass << " pass / " << g_fail << " fail\n";
    return g_fail == 0 ? 0 : 1;
}
