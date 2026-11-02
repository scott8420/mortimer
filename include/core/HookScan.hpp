#pragma once
// core::HookScan -- which hooks has YOUR class actually written?
//
// Reads include/<Form>.hpp (your half) as text and finds every
// `name(...) override` -- comments and string literals stripped first, so a
// commented-out override doesn't count. Against the form's hooks that gives:
//
//   hooked     you override it
//   unhooked   you don't; it logs when it fires          (fine, just reported)
//   MISSING    REQUIRED and you don't -- the build fails  (until you write it)
//   STALE      you override a hook the form no longer has -- the build fails
//              on `override`. Mortimer remembers retired hook names in
//              generated/<Form>.hooks.json, so it can name the culprit.
//
// It's a reader for the report, not a C++ parser: the compiler stays the
// authority. A hand-written override it can't see (a macro, say) just shows as
// unhooked here -- the build still knows better.
#include "core/Codegen.hpp"

#include <set>
#include <string>
#include <vector>

namespace mortimer::core {

std::string strip_comments_and_strings(const std::string& src);
std::set<std::string> scan_overrides(const std::string& header_text);

struct HookState {
    Hook hook;
    bool hooked = false;
};

struct HookReport {
    std::string form;
    bool header_found = false;           // include/<Form>.hpp exists
    std::vector<HookState> hooks;        // form order
    std::vector<std::string> missing;    // REQUIRED, not written
    std::vector<std::string> stale;      // overridden, but retired from the form
    std::vector<std::string> retired;    // every hook name the form used to have (memory)
    int unhooked_count() const;
};

// `previous_manifest` is the last generated/<Form>.hooks.json ("" if none).
HookReport build_report(const Form& f, bool header_found, const std::string& header_text,
                        const std::string& previous_manifest);

std::string report_text(const HookReport& r);      // generated/<Form>.hooks.txt
std::string report_manifest(const HookReport& r);  // generated/<Form>.hooks.json

}  // namespace mortimer::core
