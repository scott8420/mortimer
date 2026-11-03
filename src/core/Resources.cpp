#include "core/Resources.hpp"
#include "core/Form.hpp"
#include "core/Project.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace mortimer::core {

namespace {

std::string read_all(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) throw std::runtime_error("can't read " + p.string());
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

void write_all(const fs::path& p, const std::string& text) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("can't write " + p.string());
    out << text;
}

fs::path xml_of(const std::string& app_dir, const Project& pr) {
    return fs::path(app_dir) / "resources" / (pr.app.bin + ".gresource.xml");
}

std::string attr(const std::string& tag, const std::string& name) {
    const std::string key = name + "=\"";
    const auto at = tag.find(key);
    if (at == std::string::npos) return "";
    const auto end = tag.find('"', at + key.size());
    return end == std::string::npos ? "" : tag.substr(at + key.size(), end - at - key.size());
}

std::string ends_kind(const std::string& prefix, const std::string& file) {
    auto ends = [&](const std::string& t) { return prefix.size() >= t.size() && prefix.compare(prefix.size() - t.size(), t.size(), t) == 0; };
    if (ends("/icons/scalable/apps")) return "icon";
    if (ends("/images")) return "image";
    if (file.size() > 4 && file.compare(file.size() - 4, 4, ".css") == 0) return "css";
    return "data";
}

// One <file> entry with where it sits in the xml text (for removal).
struct Entry {
    Resource r;
    std::size_t line_begin = 0, line_end = 0;   // the whole line, newline included
};

std::vector<Entry> parse(const std::string& xml) {
    std::vector<Entry> out;
    std::size_t at = 0;
    while ((at = xml.find("<gresource ", at)) != std::string::npos) {
        const auto open_end = xml.find('>', at);
        const auto close = xml.find("</gresource>", at);
        if (open_end == std::string::npos || close == std::string::npos) break;
        const std::string prefix = attr(xml.substr(at, open_end - at), "prefix");
        std::size_t f = open_end;
        while ((f = xml.find("<file", f)) != std::string::npos && f < close) {
            const auto tag_end = xml.find('>', f);
            const auto fend = xml.find("</file>", f);
            if (tag_end == std::string::npos || fend == std::string::npos) break;
            const std::string tag = xml.substr(f, tag_end - f);
            const std::string text = xml.substr(tag_end + 1, fend - tag_end - 1);
            const std::string alias = attr(tag, "alias");
            Entry e;
            e.r.file = text;
            e.r.kind = ends_kind(prefix, text);
            e.r.path = prefix + "/" + (alias.empty() ? text : alias);
            const std::string name = fs::path(alias.empty() ? text : alias).filename().string();
            if (e.r.kind == "icon") e.r.use = fs::path(name).stem().string();
            else if (e.r.kind == "css") e.r.use = "loaded for every window";
            else e.r.use = e.r.path;
            e.line_begin = xml.rfind('\n', f);
            e.line_begin = e.line_begin == std::string::npos ? f : e.line_begin + 1;
            e.line_end = xml.find('\n', fend);
            e.line_end = e.line_end == std::string::npos ? fend + 7 : e.line_end + 1;
            out.push_back(e);
            f = fend + 7;
        }
        at = close + 12;
    }
    return out;
}

// Insert a <file> line into the block with `prefix`, making the block if absent.
std::string insert(std::string xml, const std::string& prefix, const std::string& line) {
    const std::string open = "<gresource prefix=\"" + prefix + "\">";
    if (auto at = xml.find(open); at != std::string::npos) {
        const auto close = xml.find("</gresource>", at);
        auto ls = xml.rfind('\n', close);   // before the closing tag's own line
        ls = ls == std::string::npos ? close : ls + 1;
        xml.insert(ls, "    " + line + "\n");
        return xml;
    }
    const auto end = xml.rfind("</gresources>");
    if (end == std::string::npos) throw std::runtime_error("the bundle's xml has no </gresources>");
    xml.insert(end, "  " + open + "\n    " + line + "\n  </gresource>\n");
    return xml;
}

bool plain_name(const std::string& n) {
    if (n.empty() || n.front() == '.') return false;
    for (unsigned char c : n)
        if (!std::isalnum(c) && c != '.' && c != '_' && c != '-') return false;
    return true;
}

// removed/<rel>, or removed/<rel>.2, .3 ... when that's taken.
fs::path free_spot(const fs::path& base) {
    if (!fs::exists(base)) return base;
    for (int i = 2;; ++i) {
        fs::path p = base;
        p += "." + std::to_string(i);
        if (!fs::exists(p)) return p;
    }
}

}  // namespace

std::string id_path(const std::string& app_id) {
    std::string s = "/" + app_id;
    for (auto& c : s) if (c == '.') c = '/';
    return s;
}

std::vector<Resource> list_resources(const std::string& app_dir) {
    const Project pr = load_project(app_dir);
    const fs::path xml = xml_of(app_dir, pr);
    if (!fs::exists(xml)) return {};
    std::vector<Resource> out;
    for (auto& e : parse(read_all(xml))) out.push_back(e.r);
    return out;
}

Resource add_resource(const std::string& app_dir, const std::string& kind, const std::string& src) {
    const Project pr = load_project(app_dir);
    std::string folder, sub;
    if (kind == "icon") { folder = "icons/scalable/apps"; sub = "/icons/scalable/apps"; }
    else if (kind == "image") { folder = "images"; sub = "/images"; }
    else if (kind == "data") { folder = "data"; sub = "/data"; }
    else throw std::runtime_error("\"" + kind + "\" isn't a kind of resource (icon, image, data)");
    if (!fs::is_regular_file(src)) throw std::runtime_error(src + " isn't a file");
    const std::string name = fs::path(src).filename().string();
    if (!plain_name(name))
        throw std::runtime_error("\"" + name + "\": a bundled name is letters, digits, . _ - (rename the file first)");
    if (kind == "icon" && fs::path(name).extension() != ".svg")
        throw std::runtime_error("an icon is an .svg (\"" + name + "\" isn't) -- add it as an image instead");
    const fs::path xml = xml_of(app_dir, pr);
    std::string text = read_all(xml);
    const std::string rel = folder + "/" + name;
    const std::string bundled = id_path(pr.app.app_id) + sub + "/" + name;
    for (const auto& e : parse(text))
        if (e.r.file == rel || e.r.path == bundled) throw std::runtime_error("the bundle already has " + name);
    const fs::path dest = fs::path(app_dir) / "resources" / rel;
    if (fs::exists(dest)) throw std::runtime_error("resources/" + rel + " is already there -- remove it, or rename the new one");
    fs::create_directories(dest.parent_path());
    fs::copy_file(src, dest);
    const std::string pre = kind == "icon" ? "preprocess=\"xml-stripblanks\" " : "";
    text = insert(text, id_path(pr.app.app_id) + sub, "<file " + pre + "alias=\"" + name + "\">" + rel + "</file>");
    write_all(xml, text);
    for (const auto& e : parse(text))
        if (e.r.file == rel) return e.r;
    throw std::runtime_error("added " + rel + " but can't read it back from the xml");
}

Resource add_stylesheet(const std::string& app_dir) {
    const Project pr = load_project(app_dir);
    const fs::path xml = xml_of(app_dir, pr);
    std::string text = read_all(xml);
    const std::string rel = pr.app.bin + ".css";
    for (const auto& e : parse(text))
        if (e.r.file == rel) throw std::runtime_error("the bundle already has " + rel);
    const fs::path css = fs::path(app_dir) / "resources" / rel;
    if (!fs::exists(css))
        write_all(css,
                  "/* " + pr.app.name + "'s stylesheet -- bundled into the binary and loaded for every\n"
                  " * window (generated/Style.cpp), above the theme. Style by widget name\n"
                  " * (#<root id>.<id>, as the registry names it) or by class (.my-class, set in\n"
                  " * the form's css_classes). */\n");
    text = insert(text, id_path(pr.app.app_id), "<file>" + rel + "</file>");
    write_all(xml, text);
    for (const auto& e : parse(text))
        if (e.r.file == rel) return e.r;
    throw std::runtime_error("added " + rel + " but can't read it back from the xml");
}

std::string remove_resource(const std::string& app_dir, const std::string& file) {
    const Project pr = load_project(app_dir);
    const fs::path xml = xml_of(app_dir, pr);
    std::string text = read_all(xml);
    if (fs::path(file).filename() == pr.app.bin + "-logo-symbolic.svg")
        throw std::runtime_error("the logo stays: the .desktop file and the About window use it (edit the .svg instead)");
    for (const auto& e : parse(text)) {
        if (e.r.file != file) continue;
        text.erase(e.line_begin, e.line_end - e.line_begin);
        const fs::path src = fs::path(app_dir) / "resources" / file;
        std::string went = "(the file was already gone)";
        if (fs::exists(src)) {
            const fs::path dest = free_spot(fs::path(app_dir) / "removed" / "resources" / file);
            fs::create_directories(dest.parent_path());
            fs::rename(src, dest);
            went = fs::relative(dest, app_dir).string();
        }
        write_all(xml, text);
        return went;
    }
    throw std::runtime_error("the bundle has no " + file);
}

std::string remove_form(const std::string& app_dir, const std::string& stem) {
    Project pr = load_project(app_dir);
    if (stem == pr.main_form)
        throw std::runtime_error(stem + " is the main form -- the app opens it; it can't go");
    const auto it = std::find(pr.forms.begin(), pr.forms.end(), stem);
    if (it == pr.forms.end()) throw std::runtime_error("the project has no form " + stem);
    const fs::path root(app_dir);
    const fs::path dest = free_spot(root / "removed" / stem);
    fs::create_directories(dest);
    for (const fs::path rel : {fs::path("forms") / (stem + ".mort"), fs::path("include") / (stem + ".hpp"),
                               fs::path("src") / (stem + ".cpp")})
        if (fs::exists(root / rel)) fs::rename(root / rel, dest / rel.filename());
    pr.forms.erase(it);
    save_project(pr, app_dir);
    return fs::relative(dest, root).string();
}

// ── s021: rename a form ────────────────────────────────────────────────────
//
// Why a little scanner and not a find-and-replace: "About" is a class in code,
// but also a word in a menu row ("About Notes") and maybe in a comment about
// something else. A string the user sees must never change behind their back,
// so the scanner knows where strings, chars and comments begin and end, and
// renames only whole identifiers outside them.

namespace {

bool ident_char(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

// One identifier: old -> now, oldUi -> nowUi, anything else as it was.
std::string swap_word(const std::string& w, const std::string& old, const std::string& now) {
    if (w == old) return now;
    if (w == old + "Ui") return now + "Ui";
    return w;
}

// Words in a run of plain text (a comment): rename the whole ones.
std::string swap_words(const std::string& s, const std::string& old, const std::string& now) {
    std::string out;
    std::size_t i = 0;
    while (i < s.size()) {
        if (ident_char(s[i])) {
            std::size_t j = i;
            while (j < s.size() && ident_char(s[j])) ++j;
            out += swap_word(s.substr(i, j - i), old, now);
            i = j;
        } else {
            out += s[i++];
        }
    }
    return out;
}

// The path of an #include: only its last part, and only "<old>.hpp" / "<old>Ui.hpp".
std::string swap_include(const std::string& p, const std::string& old, const std::string& now) {
    const auto slash = p.rfind('/');
    const std::string dir = slash == std::string::npos ? "" : p.substr(0, slash + 1);
    const std::string file = slash == std::string::npos ? p : p.substr(slash + 1);
    if (file == old + ".hpp") return dir + now + ".hpp";
    if (file == old + "Ui.hpp") return dir + now + "Ui.hpp";
    return p;
}

}  // namespace

std::string rename_class_in(const std::string& text, const std::string& old, const std::string& now, bool comments) {
    std::string out;
    out.reserve(text.size() + 64);
    const std::size_t n = text.size();
    std::size_t i = 0;
    bool line_is_include = false;   // this line began with #include
    std::size_t line_start = 0;
    while (i < n) {
        const char c = text[i];
        if (i == line_start) {
            std::size_t k = i;
            while (k < n && (text[k] == ' ' || text[k] == '\t')) ++k;
            line_is_include = text.compare(k, 1, "#") == 0 &&
                              text.find_first_not_of(" \t", k + 1) != std::string::npos &&
                              text.compare(text.find_first_not_of(" \t", k + 1), 7, "include") == 0;
        }
        if (c == '\n') {
            out += c;
            line_start = ++i;
            continue;
        }
        // // comment, to the end of the line
        if (c == '/' && i + 1 < n && text[i + 1] == '/') {
            const std::size_t e = std::min(text.find('\n', i), n);
            const std::string body = text.substr(i, e - i);
            out += comments ? swap_words(body, old, now) : body;
            i = e;
            continue;
        }
        // /* comment */ (may span lines -- keep line_start honest after it)
        if (c == '/' && i + 1 < n && text[i + 1] == '*') {
            const std::size_t close = text.find("*/", i + 2);
            const std::size_t e = close == std::string::npos ? n : close + 2;
            const std::string body = text.substr(i, e - i);
            out += comments ? swap_words(body, old, now) : body;
            if (body.find('\n') != std::string::npos) line_start = n + 1;   // mid-line now: no #include here
            i = e;
            continue;
        }
        // "string" -- untouched, unless it's the path of this line's #include
        if (c == '"') {
            // A raw string: the identifier just written ends in R (R"x( ... )x").
            std::size_t b = out.size();
            while (b > 0 && ident_char(out[b - 1])) --b;
            const std::string pre = out.substr(b);
            const bool raw = pre == "R" || pre == "LR" || pre == "uR" || pre == "UR" || pre == "u8R";
            if (raw) {
                const std::size_t open = text.find('(', i + 1);
                if (open != std::string::npos) {
                    const std::string close = ")" + text.substr(i + 1, open - i - 1) + "\"";
                    const std::size_t at = text.find(close, open + 1);
                    const std::size_t e = at == std::string::npos ? n : at + close.size();
                    out += text.substr(i, e - i);
                    if (text.substr(i, e - i).find('\n') != std::string::npos) line_start = n + 1;
                    i = e;
                    continue;
                }
            }
            std::size_t j = i + 1;
            while (j < n && text[j] != '"' && text[j] != '\n') j += text[j] == '\\' ? 2 : 1;
            const std::size_t e = std::min(j + 1, n);
            if (line_is_include) out += "\"" + swap_include(text.substr(i + 1, j - i - 1), old, now) + "\"";
            else out += text.substr(i, e - i);
            i = e;
            continue;
        }
        // 'c' -- untouched
        if (c == '\'') {
            std::size_t j = i + 1;
            while (j < n && text[j] != '\'' && text[j] != '\n') j += text[j] == '\\' ? 2 : 1;
            const std::size_t e = std::min(j + 1, n);
            out += text.substr(i, e - i);
            i = e;
            continue;
        }
        // a number (1'000 has a quote that isn't a char) -- as it was
        if (std::isdigit(static_cast<unsigned char>(c))) {
            std::size_t j = i;
            while (j < n && (ident_char(text[j]) || text[j] == '\'' || text[j] == '.')) ++j;
            out += text.substr(i, j - i);
            i = j;
            continue;
        }
        // an identifier -- the one thing we rename
        if (ident_char(c)) {
            std::size_t j = i;
            while (j < n && ident_char(text[j])) ++j;
            out += swap_word(text.substr(i, j - i), old, now);
            i = j;
            continue;
        }
        out += c;
        ++i;
    }
    return out;
}

RenamedForm rename_project_form(const std::string& app_dir, const std::string& old, const std::string& now) {
    if (!is_identifier(now) || is_cpp_keyword(now)) throw std::runtime_error("\"" + now + "\" isn't a C++ class name");
    if (now == old) throw std::runtime_error("the form is already " + now);
    Project pr = load_project(app_dir);
    const auto it = std::find(pr.forms.begin(), pr.forms.end(), old);
    if (it == pr.forms.end()) throw std::runtime_error("the project has no form " + old);
    if (std::find(pr.forms.begin(), pr.forms.end(), now) != pr.forms.end())
        throw std::runtime_error("the project already has a form " + now);
    const fs::path root(app_dir);
    // A file of the new name already there would be overwritten -- or, for
    // include/App.hpp / Log.hpp / Registry.hpp, it's the app's own: refuse.
    for (const fs::path rel : {fs::path("forms") / (now + ".mort"), fs::path("include") / (now + ".hpp"),
                               fs::path("src") / (now + ".cpp")})
        if (fs::exists(root / rel)) throw std::runtime_error(rel.string() + " is already there -- pick another name");

    RenamedForm r;
    // Plan every rewrite of yours first (read, rewrite in memory); nothing on
    // disk moves until all of it has been read.
    struct Edit { fs::path from, to; std::string before, after; };
    std::vector<Edit> edits;
    const fs::path own_h = fs::path("include") / (old + ".hpp"), own_c = fs::path("src") / (old + ".cpp");
    for (const auto& [rel, to] : {std::pair{own_h, fs::path("include") / (now + ".hpp")},
                                  std::pair{own_c, fs::path("src") / (now + ".cpp")}})
        if (fs::exists(root / rel)) {
            const std::string t = read_all(root / rel);
            edits.push_back({rel, to, t, rename_class_in(t, old, now, true)});
        }
    for (const char* dir : {"include", "src"}) {
        if (!fs::is_directory(root / dir)) continue;
        std::vector<fs::path> files;
        for (const auto& e : fs::recursive_directory_iterator(root / dir)) {
            if (!e.is_regular_file()) continue;
            const std::string ext = e.path().extension().string();
            if (ext != ".hpp" && ext != ".cpp" && ext != ".h" && ext != ".cc" && ext != ".hh" && ext != ".cxx") continue;
            const fs::path rel = fs::relative(e.path(), root);
            if (rel == own_h || rel == own_c) continue;
            files.push_back(rel);
        }
        std::sort(files.begin(), files.end());
        for (const auto& rel : files) {
            const std::string t = read_all(root / rel);
            std::string a = rename_class_in(t, old, now, false);
            if (a != t) edits.push_back({rel, rel, t, std::move(a)});
        }
    }
    // Other forms whose actions open this one.
    std::vector<std::pair<std::string, Form>> openers;
    for (const auto& stem : pr.forms) {
        if (stem == old) continue;
        Form f = load_form((root / "forms" / (stem + ".mort")).string());
        bool hit = false;
        for (auto& a : f.actions)
            if (a.opens == old) { a.opens = now; hit = true; }
        if (hit) openers.emplace_back(stem, std::move(f));
    }

    // Now the disk. The originals first (only the files that change).
    if (std::any_of(edits.begin(), edits.end(), [](const Edit& e) { return e.before != e.after; })) {
        const fs::path keep = free_spot(root / "removed" / (old + ".renamed"));
        for (const auto& e : edits) {
            if (e.before == e.after) continue;   // only moved: nothing to keep
            fs::create_directories((keep / e.from).parent_path());
            write_all(keep / e.from, e.before);
        }
        r.kept = fs::relative(keep, root).string();
    }
    Form me = load_form((root / "forms" / (old + ".mort")).string());
    me.name = now;
    save_form(me, (root / "forms" / (now + ".mort")).string());
    fs::remove(root / "forms" / (old + ".mort"));
    for (const auto& e : edits) {
        write_all(root / e.to, e.after);
        if (e.from != e.to) fs::remove(root / e.from);
        r.edited.push_back(e.to.string());
    }
    for (const char* s : {".hooks.json", ".hooks.txt"})
        if (fs::exists(root / "generated" / (old + s)) && !fs::exists(root / "generated" / (now + s)))
            fs::rename(root / "generated" / (old + s), root / "generated" / (now + s));
    for (auto& [stem, f] : openers) {
        save_form(f, (root / "forms" / (stem + ".mort")).string());
        r.forms.push_back(stem);
    }
    *std::find(pr.forms.begin(), pr.forms.end(), old) = now;
    if (pr.main_form == old) pr.main_form = now;
    save_project(pr, app_dir);
    return r;
}

// ── s021 m2: rename a resource ─────────────────────────────────────────────

namespace {

// Every icon_name (a property, or a Stack page's pack) equal to `old` -> `now`.
bool rename_icon_in(Node& n, const std::string& old, const std::string& now) {
    bool hit = false;
    for (auto* m : {&n.props, &n.pack})
        if (const auto it = m->find("icon_name"); it != m->end())
            if (const auto* v = std::get_if<std::string>(&it->second); v && *v == old) { it->second = Value(now); hit = true; }
    for (auto& c : n.children) hit |= rename_icon_in(c, old, now);
    return hit;
}

// `needle` inside a string literal of this text? (a cheap look: quoted whole)
bool names_in_string(const std::string& text, const std::string& needle) {
    for (std::size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + 1)) {
        const bool open = at > 0 && (text[at - 1] == '"' || text[at - 1] == '/' || text[at - 1] == '\'');
        const std::size_t e = at + needle.size();
        const bool close = e < text.size() && (text[e] == '"' || text[e] == '\'' || text[e] == ')');
        if (open && close) return true;
    }
    return false;
}

}  // namespace

RenamedResource rename_resource(const std::string& app_dir, const std::string& file, const std::string& name_in) {
    const Project pr = load_project(app_dir);
    const fs::path root(app_dir);
    const fs::path xml = xml_of(app_dir, pr);
    std::string text = read_all(xml);
    const auto entries = parse(text);
    const auto it = std::find_if(entries.begin(), entries.end(), [&](const Entry& e) { return e.r.file == file; });
    if (it == entries.end()) throw std::runtime_error("the bundle has no " + file);
    const Entry e = *it;
    if (e.r.kind == "css")
        throw std::runtime_error("the stylesheet is " + pr.app.bin + ".css by name -- the generated code loads it by that name");
    const std::string old_name = fs::path(file).filename().string();
    if (old_name == pr.app.bin + "-logo-symbolic.svg")
        throw std::runtime_error("the logo keeps its name: the .desktop file and the About window use it");
    std::string name = name_in;
    if (fs::path(name).extension().empty()) name += fs::path(old_name).extension().string();
    if (name == old_name) throw std::runtime_error("it's already " + name);
    if (!plain_name(name)) throw std::runtime_error("\"" + name + "\": a bundled name is letters, digits, . _ -");
    if (e.r.kind == "icon" && fs::path(name).extension() != ".svg")
        throw std::runtime_error("an icon is an .svg (\"" + name + "\" isn't)");
    const fs::path folder = fs::path(file).parent_path();
    const std::string rel_now = (folder / name).string();
    const fs::path dest = root / "resources" / rel_now;
    if (fs::exists(dest)) throw std::runtime_error("resources/" + rel_now + " is already there");
    const std::string path_now = e.r.path.substr(0, e.r.path.rfind('/') + 1) + name;
    for (const auto& x : entries)
        if (x.r.file == rel_now || x.r.path == path_now) throw std::runtime_error("the bundle already has " + name);

    RenamedResource r;
    r.was = e.r.use;
    // The xml line: its alias and its text, nothing else (preprocess stays).
    std::string line = text.substr(e.line_begin, e.line_end - e.line_begin);
    auto swap = [&](const std::string& a, const std::string& b) {
        if (const auto at = line.find(a); at != std::string::npos) line.replace(at, a.size(), b);
    };
    swap("alias=\"" + old_name + "\"", "alias=\"" + name + "\"");
    swap(">" + file + "</file>", ">" + rel_now + "</file>");
    text.replace(e.line_begin, e.line_end - e.line_begin, line);

    // Forms that name the icon (read them all before anything moves).
    std::vector<std::pair<std::string, Form>> forms;
    const std::string use_now = e.r.kind == "icon" ? fs::path(name).stem().string() : path_now;
    if (e.r.kind == "icon")
        for (const auto& stem : pr.forms) {
            Form f = load_form((root / "forms" / (stem + ".mort")).string());
            if (rename_icon_in(f.root, r.was, use_now)) forms.emplace_back(stem, std::move(f));
        }

    if (fs::exists(root / "resources" / file)) fs::rename(root / "resources" / file, dest);
    write_all(xml, text);
    for (auto& [stem, f] : forms) {
        save_form(f, (root / "forms" / (stem + ".mort")).string());
        r.forms.push_back(stem);
    }
    // Strings are yours: say where the old name still is.
    const std::string needle = e.r.kind == "icon" ? r.was : e.r.path;
    for (const char* dir : {"include", "src", "resources"}) {
        if (!fs::is_directory(root / dir)) continue;
        std::vector<std::string> hits;
        for (const auto& f : fs::recursive_directory_iterator(root / dir)) {
            if (!f.is_regular_file()) continue;
            const std::string ext = f.path().extension().string();
            if (ext != ".hpp" && ext != ".cpp" && ext != ".h" && ext != ".cc" && ext != ".css") continue;
            if (names_in_string(read_all(f.path()), needle)) hits.push_back(fs::relative(f.path(), root).string());
        }
        std::sort(hits.begin(), hits.end());
        r.mentions.insert(r.mentions.end(), hits.begin(), hits.end());
    }
    for (const auto& x : parse(text))
        if (x.r.file == rel_now) { r.now = x.r; return r; }
    throw std::runtime_error("renamed " + rel_now + " but can't read it back from the xml");
}

std::string form_kind(const std::string& app_dir, const std::string& stem) {
    try {
        return load_form((fs::path(app_dir) / "forms" / (stem + ".mort")).string()).root.type;
    } catch (...) {
        return "";
    }
}

}  // namespace mortimer::core
