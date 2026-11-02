#include "core/CssScope.hpp"

#include <cctype>
#include <set>

namespace mortimer::core {

namespace {

// Copy a /* comment */ starting at i (s[i] == '/', s[i+1] == '*'); returns the index after it.
std::size_t skip_comment(const std::string& s, std::size_t i) {
    const auto end = s.find("*/", i + 2);
    return end == std::string::npos ? s.size() : end + 2;
}

// From an opening '{' at i, the index just after its matching '}' -- strings
// and comments inside don't count.
std::size_t skip_block(const std::string& s, std::size_t i) {
    int depth = 0;
    while (i < s.size()) {
        const char c = s[i];
        if (c == '/' && i + 1 < s.size() && s[i + 1] == '*') { i = skip_comment(s, i); continue; }
        if (c == '"' || c == '\'') {
            const char q = c;
            ++i;
            while (i < s.size() && s[i] != q) i += (s[i] == '\\') ? 2 : 1;
            ++i;
            continue;
        }
        if (c == '{') ++depth;
        if (c == '}' && --depth == 0) return i + 1;
        ++i;
    }
    return s.size();
}

// One selector (trimmed of the whitespace around it, which is kept outside).
std::string scope_one(const std::string& sel, const std::string& scope) {
    // A leading `window` type selector is the card itself.
    if (sel.rfind("window", 0) == 0) {
        const char next = sel.size() > 6 ? sel[6] : '\0';
        if (next == '\0' || next == '.' || next == ':' || next == '#' || next == '[' || next == '>' ||
            std::isspace(static_cast<unsigned char>(next)))
            return scope + sel.substr(6);
    }
    return scope + " " + sel;
}

// A selector list: split on top-level commas (not inside :not(a, b)), scope
// each, keep every byte of whitespace and comment where it was.
std::string scope_list(const std::string& list, const std::string& scope) {
    std::string out;
    std::size_t start = 0;
    int paren = 0;
    auto flush = [&](std::size_t end) {
        const std::string part = list.substr(start, end - start);
        // Leading whitespace / comments stay before the selector.
        std::size_t a = 0;
        while (a < part.size()) {
            if (std::isspace(static_cast<unsigned char>(part[a]))) { ++a; continue; }
            if (part.compare(a, 2, "/*") == 0) { a = skip_comment(part, a); continue; }
            break;
        }
        std::size_t b = part.size();
        while (b > a && std::isspace(static_cast<unsigned char>(part[b - 1]))) --b;
        out += part.substr(0, a);
        if (b > a) out += scope_one(part.substr(a, b - a), scope);
        out += part.substr(b);
    };
    for (std::size_t i = 0; i < list.size(); ++i) {
        const char c = list[i];
        if (c == '/' && i + 1 < list.size() && list[i + 1] == '*') { i = skip_comment(list, i) - 1; continue; }
        if (c == '(') ++paren;
        else if (c == ')') --paren;
        else if (c == ',' && paren == 0) {
            flush(i);
            out += ',';
            start = i + 1;
        }
    }
    flush(list.size());
    return out;
}

}  // namespace

std::string scope_css(const std::string& s, const std::string& scope) {
    std::string out;
    std::size_t i = 0;
    while (i < s.size()) {
        const char c = s[i];
        // Whitespace and comments between rules: as they are.
        if (std::isspace(static_cast<unsigned char>(c))) { out += c; ++i; continue; }
        if (c == '/' && i + 1 < s.size() && s[i + 1] == '*') {
            const auto e = skip_comment(s, i);
            out += s.substr(i, e - i);
            i = e;
            continue;
        }
        // The prelude: up to '{' (a rule) or ';' (a statement like @import).
        std::size_t j = i;
        while (j < s.size() && s[j] != '{' && s[j] != ';') {
            if (s[j] == '/' && j + 1 < s.size() && s[j + 1] == '*') { j = skip_comment(s, j); continue; }
            ++j;
        }
        const std::string prelude = s.substr(i, j - i);
        if (j >= s.size()) { out += prelude; break; }            // unterminated: GTK will say so
        if (s[j] == ';') { out += prelude + ';'; i = j + 1; continue; }   // @import ...; @define-color ...;
        const std::size_t end = skip_block(s, j);
        if (c == '@') out += prelude;                               // @keyframes name -- not a selector
        else out += scope_list(prelude, scope);
        out += s.substr(j, end - j);                                // the declarations, untouched
        i = end;
    }
    return out;
}

std::vector<std::string> css_class_names(const std::string& s) {
    std::set<std::string> names;
    auto from_prelude = [&](const std::string& p) {
        for (std::size_t i = 0; i < p.size(); ++i) {
            if (p[i] == '/' && i + 1 < p.size() && p[i + 1] == '*') { i = skip_comment(p, i) - 1; continue; }
            if (p[i] != '.') continue;
            std::size_t j = i + 1;
            auto ident = [](char c, bool first) {
                return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '-' ||
                       (!first && std::isdigit(static_cast<unsigned char>(c)));
            };
            if (j >= p.size() || !ident(p[j], true)) continue;
            while (j < p.size() && ident(p[j], false)) ++j;
            names.insert(p.substr(i + 1, j - i - 1));
            i = j - 1;
        }
    };
    std::size_t i = 0;
    while (i < s.size()) {
        if (std::isspace(static_cast<unsigned char>(s[i]))) { ++i; continue; }
        if (s[i] == '/' && i + 1 < s.size() && s[i + 1] == '*') { i = skip_comment(s, i); continue; }
        std::size_t j = i;
        while (j < s.size() && s[j] != '{' && s[j] != ';') {
            if (s[j] == '/' && j + 1 < s.size() && s[j + 1] == '*') { j = skip_comment(s, j); continue; }
            ++j;
        }
        if (j >= s.size()) break;
        if (s[j] == ';') { i = j + 1; continue; }
        if (s[i] != '@') from_prelude(s.substr(i, j - i));
        i = skip_block(s, j);
    }
    return {names.begin(), names.end()};
}

}  // namespace mortimer::core
