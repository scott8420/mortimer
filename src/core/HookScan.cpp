#include "core/HookScan.hpp"

#include "json.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace mortimer::core {

using json = nlohmann::ordered_json;

std::string strip_comments_and_strings(const std::string& s) {
    // Blank (not delete) what we drop, so nothing else shifts. Newlines kept.
    std::string o = s;
    enum { Code, Line, Block, Str, Chr } st = Code;
    for (std::size_t i = 0; i < o.size(); ++i) {
        const char c = o[i], n = i + 1 < o.size() ? o[i + 1] : '\0';
        switch (st) {
            case Code:
                if (c == '/' && n == '/') { st = Line; o[i] = ' '; }
                else if (c == '/' && n == '*') { st = Block; o[i] = ' '; o[++i] = ' '; }
                else if (c == '"') st = Str;
                else if (c == '\'') st = Chr;
                break;
            case Line:
                if (c == '\n') st = Code; else o[i] = ' ';
                break;
            case Block:
                if (c == '*' && n == '/') { o[i] = ' '; o[++i] = ' '; st = Code; }
                else if (c != '\n') o[i] = ' ';
                break;
            case Str:
            case Chr:
                if (c == '\\') { o[i] = ' '; if (i + 1 < o.size()) o[++i] = ' '; }
                else if ((st == Str && c == '"') || (st == Chr && c == '\'')) st = Code;
                else if (c != '\n') o[i] = ' ';
                break;
        }
    }
    return o;
}

namespace {
bool word(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }
}

std::set<std::string> scan_overrides(const std::string& header_text) {
    const std::string s = strip_comments_and_strings(header_text);
    std::set<std::string> out;
    for (std::size_t at = s.find("override"); at != std::string::npos; at = s.find("override", at + 8)) {
        if ((at > 0 && word(s[at - 1])) || (at + 8 < s.size() && word(s[at + 8]))) continue;
        // Walk back over qualifiers to the ')' that closes the parameter list.
        long i = static_cast<long>(at) - 1;
        for (;;) {
            while (i >= 0 && std::isspace(static_cast<unsigned char>(s[i]))) --i;
            if (i < 0) break;
            if (s[i] == '&') { --i; continue; }
            if (word(s[i])) {
                long e = i;
                while (i >= 0 && word(s[i])) --i;
                const std::string q = s.substr(i + 1, e - i);
                if (q == "const" || q == "noexcept" || q == "volatile" || q == "final") continue;
                i = -1;   // some other word: not a function declarator
            }
            break;
        }
        if (i < 0 || s[i] != ')') continue;
        int depth = 0;
        for (; i >= 0; --i) {
            if (s[i] == ')') ++depth;
            else if (s[i] == '(' && --depth == 0) break;
        }
        if (i < 0) continue;
        --i;
        while (i >= 0 && std::isspace(static_cast<unsigned char>(s[i]))) --i;
        long e = i;
        while (i >= 0 && word(s[i])) --i;
        if (e > i) out.insert(s.substr(i + 1, e - i));
    }
    return out;
}

int HookReport::unhooked_count() const {
    int n = 0;
    for (const auto& h : hooks) n += !h.hooked && !h.hook.required;
    return n;
}

HookReport build_report(const Form& f, bool header_found, const std::string& header_text,
                        const std::string& previous_manifest) {
    HookReport r;
    r.form = f.name;
    r.header_found = header_found;
    const auto overrides = scan_overrides(header_text);
    std::set<std::string> now;
    for (const auto& h : hooks_of(f)) {
        now.insert(h.name);
        const bool hooked = overrides.count(h.name) > 0;
        r.hooks.push_back({h, hooked});
        if (h.required && !hooked) r.missing.push_back(h.name);
    }
    // Memory: every hook the form ever had and has no longer.
    std::set<std::string> retired;
    if (!previous_manifest.empty()) {
        try {
            auto j = json::parse(previous_manifest);
            for (const auto& h : j.value("hooks", json::array()))
                if (h.is_object()) retired.insert(h.value("name", ""));
            for (const auto& n : j.value("retired", json::array()))
                if (n.is_string()) retired.insert(n.get<std::string>());
        } catch (...) {
            // A damaged manifest only costs the stale names; the compiler still catches them.
        }
    }
    retired.erase("");
    for (const auto& n : now) retired.erase(n);
    for (const auto& n : retired) {
        r.retired.push_back(n);
        if (overrides.count(n)) r.stale.push_back(n);
    }
    return r;
}

std::string report_text(const HookReport& r) {
    std::ostringstream o;
    o << "# " << kGeneratedMarker << " -- DO NOT EDIT. Hooks of " << r.form << ".\n"
      << "# Your class: include/" << r.form << ".hpp + src/" << r.form << ".cpp\n\n";
    if (!r.header_found)
        o << "!! include/" << r.form << ".hpp is missing -- nothing can be hooked until it exists.\n\n";
    if (r.hooks.empty()) {
        o << "No signals are ticked in the form.\n";
    } else {
        std::size_t width = 0;
        for (const auto& h : r.hooks) width = std::max(width, h.hook.decl().size());
        for (const auto& h : r.hooks) {
            const char* tag = h.hooked ? "hooked    " : h.hook.required ? "MISSING   " : "unhooked  ";
            std::string d = h.hook.decl();
            d.append(width - d.size(), ' ');
            o << "  " << tag << d << "   " << h.hook.widget << " (" << h.hook.type << ") "
              << h.hook.signal << (h.hook.required ? "  [required]" : "") << "\n";
        }
    }
    if (!r.stale.empty()) {
        o << "\nSTALE -- include/" << r.form << ".hpp overrides hooks the form no longer has.\n"
          << "The build rejects their `override`. Delete them (or rename to the new hook):\n";
        for (const auto& s : r.stale) o << "  " << s << "\n";
    }
    std::vector<const HookState*> todo;
    for (const auto& h : r.hooks)
        if (!h.hooked) todo.push_back(&h);
    if (!todo.empty()) {
        o << "\n-- paste into include/" << r.form << ".hpp (class " << r.form << ", protected:) --\n";
        for (auto* h : todo) o << override_decl(h->hook) << "\n";
        o << "\n-- paste into src/" << r.form << ".cpp --\n";
        for (auto* h : todo) o << "\n" << override_def(h->hook, r.form);
    }
    return o.str();
}

std::string report_manifest(const HookReport& r) {
    json j;
    j["generated"] = kGeneratedMarker;
    j["form"] = r.form;
    json hooks = json::array();
    for (const auto& h : r.hooks) {
        json x;
        x["name"] = h.hook.name;
        x["widget"] = h.hook.widget;
        x["signal"] = h.hook.signal;
        x["required"] = h.hook.required;
        x["state"] = h.hooked ? "hooked" : h.hook.required ? "missing" : "unhooked";
        hooks.push_back(x);
    }
    j["hooks"] = hooks;
    j["stale"] = r.stale;
    j["retired"] = r.retired;
    return j.dump(2) + "\n";
}

}  // namespace mortimer::core
