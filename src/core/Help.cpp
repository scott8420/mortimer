#include "core/Help.hpp"
#include "core/Shortcuts.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace mortimer::core {

namespace {
std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r");
    if (b == std::string::npos) return {};
    return s.substr(b, s.find_last_not_of(" \t\r") - b + 1);
}
int count(const std::string& hay, const std::string& needle) {
    int n = 0;
    for (auto at = hay.find(needle); !needle.empty() && at != std::string::npos; at = hay.find(needle, at + needle.size())) ++n;
    return n;
}
}  // namespace

const std::vector<HelpPage>& help_pages() {
    static const std::string keys = [] {
        std::string t = "# Keyboard shortcuts\n\nThe designer's keys and gestures -- made from the list the keys are\n"
                        "wired from, so this page can't be wrong about them. (Ctrl+? shows them in a\n"
                        "window of their own.)\n";
        std::string section;
        for (const auto& r : shortcut_registry()) {
            if (r.section != section) {
                section = r.section;
                t += "\n## " + section + "\n\n| Keys | Does |\n|---|---|\n";
            }
            std::string k = r.display_keys(), d = r.description;
            for (auto* s : {&k, &d})
                for (std::size_t at = 0; (at = s->find('|', at)) != std::string::npos; at += 2) s->insert(at, "\\");
            t += "| " + k + " | " + d + " |\n";
        }
        return t;
    }();
    static const std::vector<HelpPage> all = [] {
        std::vector<HelpPage> v = embedded_help_pages();
        v.push_back({"99-keyboard-shortcuts.md", keys.c_str()});
        return v;
    }();
    return all;
}

std::string help_title(const HelpPage& p) {
    std::istringstream in(p.text);
    std::string line;
    while (std::getline(in, line))
        if (line.rfind("# ", 0) == 0) return trim(line.substr(2));
    return p.file;
}

std::vector<HelpBlock> help_blocks(const std::string& text) {
    using K = HelpBlock::Kind;
    std::vector<HelpBlock> out;
    std::istringstream in(text);
    std::string line;
    std::string para;
    auto flush = [&] {
        if (!para.empty()) out.push_back({K::Para, para, {}});
        para.clear();
    };
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::string t = trim(line);
        if (t.rfind("```", 0) == 0) {   // a code block, kept exactly
            flush();
            std::string code, l;
            while (std::getline(in, l) && trim(l).rfind("```", 0) != 0) code += (code.empty() ? "" : "\n") + l;
            out.push_back({K::Code, code, {}});
            continue;
        }
        if (t.empty()) { flush(); continue; }
        if (t.rfind("### ", 0) == 0) { flush(); out.push_back({K::H3, trim(t.substr(4)), {}}); continue; }
        if (t.rfind("## ", 0) == 0) { flush(); out.push_back({K::H2, trim(t.substr(3)), {}}); continue; }
        if (t.rfind("# ", 0) == 0) { flush(); out.push_back({K::H1, trim(t.substr(2)), {}}); continue; }
        if (t.rfind("- ", 0) == 0) { flush(); out.push_back({K::Bullet, trim(t.substr(2)), {}}); continue; }
        if (t.size() > 2 && std::isdigit(static_cast<unsigned char>(t[0])) && t.find(". ") != std::string::npos &&
            t.find(". ") < 4) {
            flush();
            out.push_back({K::Number, t, {}});
            continue;
        }
        if (t.front() == '|') {   // a table row; the |---| row is skipped
            flush();
            std::vector<std::string> cells;
            std::string cell;
            bool esc = false;
            for (std::size_t i = 1; i < t.size(); ++i) {
                if (esc) { cell += t[i]; esc = false; continue; }
                if (t[i] == '\\') { esc = true; continue; }
                if (t[i] == '|') { cells.push_back(trim(cell)); cell.clear(); continue; }
                cell += t[i];
            }
            const bool rule = std::all_of(cells.begin(), cells.end(), [](const std::string& c) {
                return !c.empty() && c.find_first_not_of("-: ") == std::string::npos;
            });
            if (rule) continue;
            if (out.empty() || out.back().kind != K::Table) out.push_back({K::Table, {}, {}});
            out.back().rows.push_back(cells);
            continue;
        }
        // A bullet's continuation line joins it; otherwise the paragraph grows.
        if (para.empty() && !out.empty() && (out.back().kind == K::Bullet || out.back().kind == K::Number) &&
            line.rfind("  ", 0) == 0) {
            out.back().text += " " + t;
            continue;
        }
        para += (para.empty() ? "" : " ") + t;
    }
    flush();
    return out;
}

std::vector<HelpSpan> help_spans(const std::string& line) {
    std::vector<HelpSpan> out;
    std::string cur;
    bool bold = false, italic = false;
    auto push = [&](bool code) {
        if (!cur.empty()) out.push_back({cur, bold, code, italic && !code});
        cur.clear();
    };
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '`') {
            const auto end = line.find('`', i + 1);
            if (end != std::string::npos) {
                push(false);
                cur = line.substr(i + 1, end - i - 1);
                push(true);
                i = end;
                continue;
            }
        }
        if (line.compare(i, 2, "**") == 0) {
            push(false);
            bold = !bold;
            ++i;
            continue;
        }
        // *italic*: a star that opens before a letter, or closes one that opened
        if (line[i] == '*' && ((!italic && i + 1 < line.size() && std::isalnum(static_cast<unsigned char>(line[i + 1]))) ||
                               (italic && i > 0 && line[i - 1] != ' '))) {
            push(false);
            italic = !italic;
            continue;
        }
        cur += line[i];
    }
    push(false);
    return out;
}

std::vector<HelpHit> help_search(const std::string& query) {
    const auto& pages = help_pages();
    std::vector<std::string> words;
    std::istringstream q(lower(query));
    for (std::string w; q >> w;) words.push_back(w);
    std::vector<HelpHit> out;
    if (words.empty()) return out;
    // A page named outright: its file ("02", "the-designer") or its title.
    for (std::size_t i = 0; i < pages.size(); ++i) {
        const std::string f = lower(pages[i].file), title = lower(help_title(pages[i]));
        if (lower(query) == title || f.rfind(lower(query), 0) == 0 || f.find("-" + lower(query) + ".md") != std::string::npos ||
            f == lower(query) + ".md")
            return {{i, 1000}};
    }
    for (std::size_t i = 0; i < pages.size(); ++i) {
        const std::string body = lower(pages[i].text);
        int total = 0;
        bool all = true;
        for (const auto& w : words) {
            const int n = count(body, w);
            if (n == 0) { all = false; break; }
            total += n;
        }
        if (all) out.push_back({i, total});
    }
    std::stable_sort(out.begin(), out.end(), [](const HelpHit& a, const HelpHit& b) { return a.hits > b.hits; });
    return out;
}

}  // namespace mortimer::core
