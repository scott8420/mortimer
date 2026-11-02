#include "core/Project.hpp"
#include "core/Templates.hpp"

#include "json.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace mortimer::core {

using json = nlohmann::ordered_json;

namespace {

std::string read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Write only when the bytes differ: an unchanged generated file keeps its
// mtime, so the build recompiles only what the form change touched.
bool write_if_changed(const fs::path& p, const std::string& content, bool executable = false) {
    if (fs::exists(p) && read_file(p) == content) return false;
    fs::create_directories(p.parent_path());
    {
        std::ofstream out(p, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("cannot write " + p.string());
        out << content;
    }
    if (executable)
        fs::permissions(p, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                        fs::perm_options::add);
    return true;
}

std::string replace_all(std::string s, const std::string& from, const std::string& to) {
    for (std::size_t at = s.find(from); at != std::string::npos; at = s.find(from, at + to.size()))
        s.replace(at, from.size(), to);
    return s;
}

}  // namespace

bool GenerateResult::ok() const {
    for (const auto& [f, i] : issues)
        if (i.error) return false;
    return true;
}

// ── mortimer.json ──────────────────────────────────────────────────────────

Project load_project(const std::string& app_dir) {
    const fs::path p = fs::path(app_dir) / "mortimer.json";
    const std::string text = read_file(p);
    if (text.empty()) throw std::runtime_error("no mortimer.json in " + app_dir + " -- not a Mortimer app");
    json j;
    try {
        j = json::parse(text);
    } catch (const json::parse_error& e) {
        throw std::runtime_error(p.string() + ": " + e.what());
    }
    Project pr;
    pr.app.name = j.value("name", "");
    pr.app.bin = j.value("binary", "");
    pr.app.app_id = j.value("app_id", "");
    pr.app.ns = j.value("namespace", pr.app.bin);
    pr.main_form = j.value("main_form", "MainWindow");
    for (const auto& f : j.value("forms", json::array()))
        if (f.is_string()) pr.forms.push_back(f.get<std::string>());
    if (pr.app.name.empty() || pr.app.bin.empty() || pr.app.app_id.empty())
        throw std::runtime_error(p.string() + ": needs name, binary and app_id");
    return pr;
}

void save_project(const Project& pr, const std::string& app_dir) {
    json j;
    j["mortimer"] = 1;
    j["name"] = pr.app.name;
    j["binary"] = pr.app.bin;
    j["app_id"] = pr.app.app_id;
    j["namespace"] = pr.app.ns;
    j["main_form"] = pr.main_form;
    j["forms"] = pr.forms;
    write_if_changed(fs::path(app_dir) / "mortimer.json", j.dump(2) + "\n");
}

AppInfo derive_app(const std::string& name, const std::string& id_prefix, const std::string& app_id) {
    AppInfo a;
    a.name = name;
    std::string stem;   // ASCII letters + digits of the name, case kept
    for (unsigned char c : name)
        if (std::isalnum(c) && c < 0x80) stem += static_cast<char>(c);
    if (stem.empty() || !std::isalpha(static_cast<unsigned char>(stem[0])))
        throw std::runtime_error("\"" + name + "\": an app name needs to start with a letter (A-Z)");
    for (char c : stem) a.bin += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    a.ns = a.bin;
    if (is_cpp_keyword(a.ns) || a.ns == "std" || a.ns == "gtk" || a.ns == "glib" || a.ns == "sigc")
        a.ns += "_app";
    std::string pascal = stem;
    pascal[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(pascal[0])));
    a.app_id = app_id.empty() ? id_prefix + "." + pascal : app_id;
    return a;
}

std::string render_tokens(const std::string& text, const AppInfo& app) {
    std::string upper;
    for (char c : app.bin) upper += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    std::string id_path = "/" + replace_all(app.app_id, ".", "/");
    std::string s = text;
    s = replace_all(s, "@APP_ID_PATH@", id_path);
    s = replace_all(s, "@APP_ID@", app.app_id);
    s = replace_all(s, "@BIN_UPPER@", upper);
    s = replace_all(s, "@BIN@", app.bin);
    s = replace_all(s, "@APP@", app.name);
    s = replace_all(s, "@NS@", app.ns);
    return s;
}

// ── generate ───────────────────────────────────────────────────────────────

GenerateResult generate_project(const std::string& app_dir) {
    const fs::path root(app_dir);
    const Project pr = load_project(app_dir);
    GenerateResult r;
    std::set<std::string> produced;

    auto emit = [&](const OutFile& f) {
        produced.insert(f.path);
        if (write_if_changed(root / f.path, f.content, f.executable)) r.written.push_back(f.path);
        else r.unchanged.push_back(f.path);
    };

    for (const auto& f : generate_support(pr.app)) emit(f);

    bool main_seen = false;
    for (const auto& stem : pr.forms) {
        main_seen = main_seen || stem == pr.main_form;
        const fs::path form_path = root / "forms" / (stem + ".mort");
        Form form;
        try {
            form = load_form(form_path.string());
        } catch (const std::exception& e) {
            r.issues.push_back({stem, {true, "form", e.what()}});
            for (const char* s : {"Ui.hpp", "Ui.cpp", ".hooks.txt", ".hooks.json"})
                produced.insert("generated/" + stem + s);   // keep the old ones; don't prune
            continue;
        }
        if (form.name != stem)
            r.issues.push_back({stem, {true, "form", "the file is " + stem + ".mort but its name is \"" +
                                                         form.name + "\" -- they must match"}});
        for (const auto& i : validate(form)) r.issues.push_back({stem, i});
        bool bad = false;
        for (const auto& [s, i] : r.issues) bad = bad || (s == stem && i.error);
        if (bad) {
            for (const char* s : {"Ui.hpp", "Ui.cpp", ".hooks.txt", ".hooks.json"})
                produced.insert("generated/" + stem + s);
            r.notes.push_back(stem + ": not generated (errors above); the last good files stay");
            continue;
        }

        for (const auto& f : generate_form(form, pr.app)) emit(f);

        // Your half: written ONCE, for a form that has neither file yet.
        const fs::path uh = root / "include" / (stem + ".hpp");
        const fs::path uc = root / "src" / (stem + ".cpp");
        if (!fs::exists(uh) && !fs::exists(uc)) {
            for (const auto& f : user_stubs(form, pr.app)) {
                write_if_changed(root / f.path, f.content);
                r.stubs.push_back(f.path);
            }
        } else if (!fs::exists(uh) || !fs::exists(uc)) {
            r.notes.push_back(stem + ": only one of include/" + stem + ".hpp and src/" + stem +
                              ".cpp exists -- not writing the other (it may have been renamed)");
        }

        const fs::path manifest = root / "generated" / (stem + ".hooks.json");
        auto rep = build_report(form, fs::exists(uh), read_file(uh), read_file(manifest));
        emit({"generated/" + stem + ".hooks.txt", report_text(rep)});
        emit({"generated/" + stem + ".hooks.json", report_manifest(rep)});
        r.reports.push_back(std::move(rep));
    }
    if (!main_seen)
        r.issues.push_back({pr.main_form, {true, "project", "main_form \"" + pr.main_form +
                                                                "\" isn't in the forms list"}});

    // Prune: a generated/ file no form makes any more -- but only one that
    // carries our marker. Anything else in generated/ is somebody's, and stays.
    const fs::path gen = root / "generated";
    if (fs::exists(gen)) {
        for (const auto& e : fs::directory_iterator(gen)) {
            if (!e.is_regular_file()) continue;
            const std::string rel = "generated/" + e.path().filename().string();
            if (produced.count(rel)) continue;
            const std::string head = read_file(e.path()).substr(0, 300);
            if (head.find(kGeneratedMarker) == std::string::npos) {
                r.notes.push_back(rel + ": not Mortimer's (no marker) -- left alone");
                continue;
            }
            fs::remove(e.path());
            r.pruned.push_back(rel);
        }
    }
    // Forms on disk the project doesn't list -- say so; never guess.
    if (fs::exists(root / "forms"))
        for (const auto& e : fs::directory_iterator(root / "forms"))
            if (e.path().extension() == ".mort") {
                const std::string stem = e.path().stem().string();
                if (std::find(pr.forms.begin(), pr.forms.end(), stem) == pr.forms.end())
                    r.notes.push_back("forms/" + stem + ".mort isn't in mortimer.json's forms -- ignored");
            }
    return r;
}

std::vector<HookReport> hook_reports(const std::string& app_dir) {
    const fs::path root(app_dir);
    const Project pr = load_project(app_dir);
    std::vector<HookReport> out;
    for (const auto& stem : pr.forms) {
        const Form form = load_form((root / "forms" / (stem + ".mort")).string());
        const fs::path uh = root / "include" / (stem + ".hpp");
        out.push_back(build_report(form, fs::exists(uh), read_file(uh),
                                   read_file(root / "generated" / (stem + ".hooks.json"))));
    }
    return out;
}

// ── a new app ──────────────────────────────────────────────────────────────

std::string project_of(const std::string& form_path) {
    const fs::path fp = fs::absolute(fs::path(form_path)).lexically_normal();
    if (fp.extension() != ".mort" || fp.parent_path().filename() != "forms") return {};
    const fs::path app = fp.parent_path().parent_path();
    if (!fs::exists(app / "mortimer.json")) return {};
    try {
        const Project pr = load_project(app.string());
        const std::string stem = fp.stem().string();
        for (const auto& f : pr.forms)
            if (f == stem) return app.string();
    } catch (const std::exception&) {
    }
    return {};
}

std::string add_form(const std::string& app_dir, const std::string& name, const std::string& root_type) {
    if (root_type != "Window" && root_type != "Dialog")
        throw std::runtime_error("a form's window is a Window or a Dialog, not " + root_type);
    if (!is_identifier(name) || is_cpp_keyword(name))
        throw std::runtime_error("\"" + name + "\" isn't a C++ class name");
    Project pr = load_project(app_dir);
    for (const auto& f : pr.forms)
        if (f == name) throw std::runtime_error("the project already has a form " + name);
    const fs::path path = fs::path(app_dir) / "forms" / (name + ".mort");
    if (fs::exists(path)) throw std::runtime_error(path.string() + " already exists");
    Form f;
    f.name = name;
    Node body{"Box", "body", {{"orientation", Value(std::string("vertical"))}, {"spacing", Value(std::int64_t{12})},
                              {"margin", Value(std::int64_t{18})}}, {}, {}, {}};
    f.root = {root_type, snake(name), {{"title", Value(name)}, {"default_width", Value(std::int64_t{480})},
                                      {"default_height", Value(std::int64_t{320})}},
              {}, {}, {body}};
    save_form(f, path.string());
    pr.forms.push_back(name);
    save_project(pr, app_dir);
    return path.string();
}

HookReport live_report(const std::string& app_dir, const Form& f) {
    const fs::path root(app_dir);
    const fs::path uh = root / "include" / (f.name + ".hpp");
    const fs::path manifest = root / "generated" / (f.name + ".hooks.json");
    return build_report(f, !app_dir.empty() && fs::exists(uh), app_dir.empty() ? "" : read_file(uh),
                        app_dir.empty() ? "" : read_file(manifest));
}

Form starter_form(const AppInfo& app) {
    auto S = [](const char* s) { return Value(std::string(s)); };
    auto I = [](std::int64_t v) { return Value(v); };
    auto B = [](bool v) { return Value(v); };

    Node open{"Button", "open", {{"icon_name", S("document-open-symbolic")}, {"tooltip", S("Open")}},
              {{"slot", S("start")}}, {{"clicked", false, ""}}, {}};
    // s003: the menu's rows run the form's actions -- the key, the menu row and
    // the hook are one fact (Form::actions).
    Node menu{"MenuButton", "menu",
              {{"icon_name", S("open-menu-symbolic")},
               {"menu", Value(StringList{"Say Hello=win.hello", "-", "Quit=win.quit"})}},
              {{"slot", S("end")}}, {}, {}};
    Node header{"HeaderBar", "header", {}, {{"slot", S("titlebar")}}, {}, {open, menu}};

    Node greeting{"Label", "greeting",
                  {{"use_markup", B(true)},
                   {"label", Value("<span size='xx-large' weight='bold'>" + app.name + "</span>")}},
                  {}, {}, {}};
    Node hint{"Label", "hint",
              {{"label", S("Made by Mortimer. Change forms/MainWindow.mort, run mortimer-gen generate, "
                           "and this window follows -- your code in src/ is never touched.")},
               {"wrap", B(true)},
               {"css_classes", Value(StringList{"dim-label"})}},
              {}, {}, {}};
    Node hello{"Button", "hello",
               {{"label", S("Say hello")},
                {"halign", S("center")},
                {"css_classes", Value(StringList{"suggested-action", "pill"})}},
               {}, {{"clicked", false, ""}}, {}};
    Node body{"Box", "body",
              {{"orientation", S("vertical")}, {"spacing", I(12)}, {"margin", I(24)}, {"valign", S("center")}},
              {}, {}, {greeting, hint, hello}};

    Form f;
    f.name = "MainWindow";
    f.actions = {{"hello", "<Control>h", false}, {"quit", "<Control>q", false}};
    f.root = {"ApplicationWindow", "main_window",
              {{"title", Value(app.name)}, {"default_width", I(800)}, {"default_height", I(560)}},
              {}, {}, {header, body}};
    return f;
}

std::string create_workspace(const AppInfo& app, const std::string& parent) {
    const fs::path ws = fs::path(parent) / app.bin;
    if (fs::exists(ws) && !fs::is_empty(ws))
        throw std::runtime_error(ws.string() + " already exists -- Mortimer won't write over it");
    fs::create_directories(ws / "backups" / "Old");
    fs::create_directories(ws / "fixes");

    std::string rules;
    for (const auto& t : project_templates())
        if (std::string(t.path) == "docs/RULES.md") rules = render_tokens(t.content, app);

    for (const auto& t : project_templates()) {
        const std::string rel = render_tokens(t.path, app);
        std::string content = render_tokens(t.content, app);
        content = replace_all(content, "@RULES@", rules);
        const bool exe = rel.size() > 3 && rel.compare(rel.size() - 3, 3, ".sh") == 0;
        write_if_changed(ws / rel, content, exe);
    }

    const fs::path app_dir = ws / app.bin;
    Project pr;
    pr.app = app;
    pr.forms = {"MainWindow"};
    save_project(pr, app_dir.string());
    fs::create_directories(app_dir / "forms");
    save_form(starter_form(app), (app_dir / "forms" / "MainWindow.mort").string());
    generate_project(app_dir.string());
    return app_dir.string();
}

}  // namespace mortimer::core
