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

std::string form_kind(const std::string& app_dir, const std::string& stem) {
    try {
        return load_form((fs::path(app_dir) / "forms" / (stem + ".mort")).string()).root.type;
    } catch (...) {
        return "";
    }
}

}  // namespace mortimer::core
