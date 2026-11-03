#include "core/Project.hpp"
#include "core/Catalog.hpp"
#include "core/Templates.hpp"

#include "json.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
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
    if (j.contains("models")) {   // s013
        try { pr.models = models_from_json(j["models"].dump()); }
        catch (const std::exception& e) { throw std::runtime_error(p.string() + ": " + e.what()); }
    }
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
    if (!pr.models.empty()) j["models"] = json::parse(models_to_json(pr.models));   // s013
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

// s022 -- an app made before s022 globs src/*.cpp only; its first promoted
// widget adds src/widgets/*.cpp to that line, once. A CMakeLists you've
// changed so the line isn't there is yours: the note says what to add.
static void widgets_cmake(const fs::path& root, GenerateResult& r) {
    const fs::path cm = root / "CMakeLists.txt";
    if (!fs::exists(cm)) return;
    std::string text = read_file(cm);
    if (text.find("src/widgets/*.cpp") != std::string::npos) return;
    const std::string line = "file(GLOB APP_SOURCES CONFIGURE_DEPENDS \"${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp\")";
    const auto at = text.find(line);
    if (at == std::string::npos) {
        r.notes.push_back("CMakeLists.txt: add \"${CMAKE_CURRENT_SOURCE_DIR}/src/widgets/*.cpp\" to the app's "
                          "sources -- your promoted widgets live there");
        return;
    }
    text.replace(at, line.size(),
                 "file(GLOB APP_SOURCES CONFIGURE_DEPENDS \"${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp\"\n"
                 "                                        \"${CMAKE_CURRENT_SOURCE_DIR}/src/widgets/*.cpp\")");
    write_if_changed(cm, text);
    r.notes.push_back("CMakeLists.txt: src/widgets/*.cpp added to the app's sources (your promoted widgets)");
}

// s024 m2 -- an app made before s024 m2 doesn't regenerate from its build:
// insert the template's block, once, before the first glob (so a new form's
// files are seen in the same build), and say so.
static void regenerate_cmake(const fs::path& root, GenerateResult& r) {
    const fs::path cm = root / "CMakeLists.txt";
    if (!fs::exists(cm)) return;
    std::string text = read_file(cm);
    if (text.find("MORTIMER_GEN") != std::string::npos) return;
    std::string block;
    for (const auto& t : project_templates())
        if (std::string(t.path) == "@BIN@/CMakeLists.txt") {
            const std::string all = t.content;
            auto from = all.find("forms/ -> generated/, by the build");
            const auto to = all.find("@BIN@_core -- the GTK-free half");
            if (from == std::string::npos || to == std::string::npos) break;
            from = all.rfind('\n', from) + 1;
            block = render_tokens(all.substr(from, all.rfind('\n', to) + 1 - from), load_project(root.string()).app);
        }
    const auto glob = text.find("file(GLOB");
    if (block.empty() || glob == std::string::npos) {
        r.notes.push_back("CMakeLists.txt: no glob found -- the regenerate block (see a new app's) wasn't added");
        return;
    }
    text.insert(text.rfind('\n', glob) + 1, block);
    write_if_changed(cm, text);
    r.notes.push_back("CMakeLists.txt: the build now regenerates generated/ when a form or mortimer.json is newer");
}

// s024 -- an app made before s024 has no undo gate target: append it once (the
// block the template has), and say so. Its build then runs the gate.
static void undo_gate_cmake(const fs::path& root, GenerateResult& r) {
    const fs::path cm = root / "CMakeLists.txt";
    if (!fs::exists(cm)) return;
    std::string text = read_file(cm);
    if (text.find("_undo_gate") != std::string::npos) return;
    std::string block;
    for (const auto& t : project_templates())
        if (std::string(t.path) == "@BIN@/CMakeLists.txt") {
            const std::string all = t.content;
            auto at = all.find("@BIN@_undo_gate -- ");   // the block's heading line, to the end
            if (at != std::string::npos) {
                at = all.rfind('\n', at) + 1;
                block = render_tokens(all.substr(at), load_project(root.string()).app);
            }
        }
    if (block.empty()) return;
    if (!text.empty() && text.back() != '\n') text += "\n";
    text += "\n" + block;
    write_if_changed(cm, text);
    r.notes.push_back("CMakeLists.txt: the undo gate added -- every build now walks the app's undo (" +
                      load_project(root.string()).app.bin + "_undo_gate)");
}

GenerateResult generate_project(const std::string& app_dir) {
    const fs::path root(app_dir);
    const Project pr = load_project(app_dir);
    GenerateResult r;
    std::set<std::string> produced;
    std::vector<Form> keyed;   // s010: the forms that generated, for the shortcuts window

    auto emit = [&](const OutFile& f) {
        produced.insert(f.path);
        if (write_if_changed(root / f.path, f.content, f.executable)) r.written.push_back(f.path);
        else r.unchanged.push_back(f.path);
    };

    for (const auto& f : generate_support(pr.app)) emit(f);
    // s014: generated/Undo needs the journal, include/core/Undo.hpp -- a new app has
    // it from the template; an older one gets it here, once (then it's yours).
    if (!fs::exists(root / "include" / "core" / "Undo.hpp"))
        for (const auto& t : project_templates())
            if (std::string(t.path) == "@BIN@/include/core/Undo.hpp") {
                write_if_changed(root / "include" / "core" / "Undo.hpp", render_tokens(t.content, pr.app));
                r.notes.push_back("include/core/Undo.hpp written (the undo journal generated/Undo uses)");
            }
    // s013: the shared models -- checked once, generated once (none -> pruned).
    for (const auto& i : validate_models(pr.models)) r.issues.push_back({"mortimer.json", i});
    for (const auto& f : generate_models(pr.models, pr.app)) emit(f);

    bool main_seen = false;
    for (const auto& stem : pr.forms) {
        main_seen = main_seen || stem == pr.main_form;
        const fs::path form_path = root / "forms" / (stem + ".mort");
        Form form;
        try {
            form = load_form(form_path.string());
        } catch (const std::exception& e) {
            r.issues.push_back({stem, {true, "form", e.what()}});
            for (const char* s : {"Ui.hpp", "Ui.cpp", "Items.cpp", ".hooks.txt", ".hooks.json"})
                produced.insert("generated/" + stem + s);   // keep the old ones; don't prune
            continue;
        }
        form.shared = pr.models;   // s013: the project's models, for its views
        if (form.name != stem)
            r.issues.push_back({stem, {true, "form", "the file is " + stem + ".mort but its name is \"" +
                                                         form.name + "\" -- they must match"}});
        for (const auto& i : validate(form)) r.issues.push_back({stem, i});
        // s010: a window an action opens must be another form of this project.
        for (const auto& a : form.actions) {
            if (a.opens.empty() || a.opens == kShortcutsTarget) continue;
            const std::string where = "action " + a.name;
            if (std::find(pr.forms.begin(), pr.forms.end(), a.opens) == pr.forms.end())
                r.issues.push_back({stem, {true, where, "opens " + a.opens + ", which isn't a form of this project"}});
            else if (a.opens == pr.main_form)
                r.issues.push_back({stem, {true, where, "opens " + a.opens +
                                                            ", the main window -- the app opens that one itself"}});
        }
        bool bad = false;
        for (const auto& [s, i] : r.issues) bad = bad || (s == stem && i.error);
        if (bad) {
            for (const char* s : {"Ui.hpp", "Ui.cpp", "Items.cpp", ".hooks.txt", ".hooks.json"})
                produced.insert("generated/" + stem + s);
            r.notes.push_back(stem + ": not generated (errors above); the last good files stay");
            continue;
        }

        for (const auto& f : generate_form(form, pr.app)) emit(f);
        (stem == pr.main_form ? keyed.insert(keyed.begin(), form) : keyed.insert(keyed.end(), form));

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

        // s022: a promoted widget's class -- written ONCE (both files missing), then yours.
        for (const auto& [cls, type] : promoted_classes(form)) {
            const fs::path ch = root / "include" / "widgets" / (cls + ".hpp");
            const fs::path cc = root / "src" / "widgets" / (cls + ".cpp");
            if (!fs::exists(ch) && !fs::exists(cc)) {
                for (const auto& f : custom_widget_stubs(cls, type, pr.app)) {
                    write_if_changed(root / f.path, f.content);
                    r.stubs.push_back(f.path);
                }
                widgets_cmake(root, r);
            } else if (const WidgetSpec* w = find_widget(type);
                       w && fs::exists(ch) && read_file(ch).find(w->gtk_class) == std::string::npos) {
                r.notes.push_back(stem + ": include/widgets/" + cls + ".hpp doesn't mention " + w->gtk_class +
                                  " -- the form has " + cls + " as a " + type + "; its base must be one");
            }
        }

        const fs::path manifest = root / "generated" / (stem + ".hooks.json");
        auto rep = build_report(form, fs::exists(uh), read_file(uh), read_file(manifest));
        emit({"generated/" + stem + ".hooks.txt", report_text(rep)});
        emit({"generated/" + stem + ".hooks.json", report_manifest(rep)});
        r.reports.push_back(std::move(rep));
    }
    // s010: the keyboard-shortcuts window, from every form that generated.
    for (const auto& f : generate_shortcuts(keyed, pr.app)) emit(f);
    // s024: the undo gate, from every form that generated and the shared models.
    for (const auto& f : generate_undo_gate(keyed, pr.models, pr.app)) emit(f);
    undo_gate_cmake(root, r);
    regenerate_cmake(root, r);   // s024 m2
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
               {"menu", Value(StringList{"Undo=win.undo", "Redo=win.redo", "-", "Say Hello=win.hello", "-",
                                         "Keyboard Shortcuts=win.shortcuts", "About " + app.name + "=win.about", "-",
                                         "Quit=win.quit"})}},
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
    // s010: two actions open windows -- no hooks, no code: the shortcuts window
    // (written from every form's keys) and the About dialog (forms/About.mort).
    f.actions = {{"hello", "<Control>h", false},
                 {"shortcuts", "<Control>question", false, "", kShortcutsTarget},
                 {"about", "", false, "", "About"},
                 {"quit", "<Control>q", false},
                 // s014: the app's undo -- every edit a user makes in a generated list.
                 {"undo", "<Control>z", false, "", "", "undo"},
                 {"redo", "<Control><Shift>z", false, "", "", "redo"}};
    // s023: the header's Open button opens a file on day one -- the form owns the
    // dialog (choose_open_doc(), async, one at a time), your half's clicked hook
    // calls it, and on_open_doc_chosen(path) is where your app reads the file.
    FileDef open_doc;
    open_doc.name = "open_doc";
    open_doc.mode = "open";
    f.files = {open_doc};
    f.root = {"ApplicationWindow", "main_window",
              {{"title", Value(app.name)}, {"default_width", I(800)}, {"default_height", I(560)},
               {"remember", Value(true)}},   // s008: a new app comes back as it was left
              {}, {}, {header, body}};
    return f;
}

Form about_form(const AppInfo& app) {
    auto S = [](const std::string& s) { return Value(s); };
    auto I = [](std::int64_t v) { return Value(v); };
    auto B = [](bool v) { return Value(v); };
    Node logo{"Image", "logo", {{"icon_name", S(app.bin + "-logo-symbolic")}, {"pixel_size", I(96)}}, {}, {}, {}};
    Node name{"Label", "app_name",
              {{"use_markup", B(true)}, {"label", S("<span size='xx-large' weight='bold'>" + app.name + "</span>")}},
              {}, {}, {}};
    // Filled in by your half (src/About.cpp) from core::version() -- CMake's
    // project(VERSION) -- so there is one place to change it.
    Node version{"Label", "version", {{"label", S("Version")}, {"css_classes", Value(StringList{"dim-label"})}},
                 {}, {}, {}};
    Node blurb{"Label", "blurb",
               {{"label", S("Made with Mortimer.")}, {"wrap", B(true)}, {"margin_top", I(6)}},
               {}, {}, {}};
    // GTK's own window.close action: no hook needed. It hides the dialog (hide on close).
    Node close{"Button", "close",
               {{"label", S("Close")}, {"action_name", S("window.close")}, {"halign", S("center")},
                {"margin_top", I(12)}, {"css_classes", Value(StringList{"suggested-action", "pill"})}},
               {}, {}, {}};
    Node page{"Box", "page",
              {{"orientation", S("vertical")}, {"spacing", I(6)}, {"margin", I(24)}, {"valign", S("center")}},
              {}, {}, {logo, name, version, blurb, close}};
    Form f;
    f.name = "About";
    f.root = {"Dialog", "about",
              {{"title", S("About " + app.name)}, {"default_width", I(360)}, {"resizable", B(false)}},
              {}, {}, {page}};
    return f;
}

std::string add_about(const std::string& app_dir) {
    const fs::path root(app_dir);
    Project pr = load_project(app_dir);
    const fs::path form = root / "forms" / "About.mort";
    if (fs::exists(form) || std::find(pr.forms.begin(), pr.forms.end(), "About") != pr.forms.end())
        throw std::runtime_error("this project already has an About form");
    fs::create_directories(root / "forms");
    const Form f = about_form(pr.app);
    save_form(f, form.string());
    // Your half, written once -- with the line that shows the version, so the
    // dialog never says a stale one. (generate writes nothing over it.)
    const fs::path uh = root / "include" / "About.hpp", uc = root / "src" / "About.cpp";
    if (!fs::exists(uh) && !fs::exists(uc)) {
        for (auto out : user_stubs(f, pr.app)) {
            if (out.path == "src/About.cpp") {
                out.content = replace_all(out.content, "#include \"Log.hpp\"\n",
                                          "#include \"Log.hpp\"\n#include \"core/Version.hpp\"\n");
                out.content = replace_all(out.content,
                    "    // wired. Fill in models, load state, set what the form can't express.\n",
                    "    // wired. Fill in models, load state, set what the form can't express.\n"
                    "    //\n"
                    "    // The version comes from CMake's project(VERSION) through core/ -- one place\n"
                    "    // to change it, and this line follows.\n"
                    "    m_version.set_label(\"Version \" + core::version());\n");
            }
            write_if_changed(root / out.path, out.content);
        }
    }
    pr.forms.push_back("About");
    save_project(pr, app_dir);
    return form.string();
}

Form load_project_form(const std::string& app_dir, const std::string& stem) {
    Form f = load_form((fs::path(app_dir) / "forms" / (stem + ".mort")).string());
    f.shared = load_project(app_dir).models;
    return f;
}

std::vector<std::string> share_model(const std::string& app_dir, const std::string& stem, const std::string& name) {
    Project pr = load_project(app_dir);
    for (const auto& m : pr.models)
        if (m.name == name) throw std::runtime_error("the project already shares a model " + name);
    const fs::path root(app_dir);
    std::map<std::string, Form> forms;
    for (const auto& s : pr.forms) forms[s] = load_form((root / "forms" / (s + ".mort")).string());
    if (!forms.count(stem)) throw std::runtime_error(stem + " isn't a form of this project");
    const ModelDef* mine = nullptr;
    for (const auto& m : forms[stem].models) if (m.name == name) mine = &m;
    if (!mine) throw std::runtime_error(stem + " has no model " + name);
    const ModelDef shared = *mine;
    // Another form's copy must be the same model -- else sharing would change it under that form.
    for (const auto& [s, f] : forms) {
        if (s == stem) continue;
        for (const auto& m : f.models)
            if (m.name == name && !(m == shared))
                throw std::runtime_error(s + " has its own " + name + " with other fields -- make them the same "
                                         "(or rename one) before sharing");
    }
    std::vector<std::string> changed;
    for (auto& [s, f] : forms) {
        const auto n = f.models.size();
        std::erase_if(f.models, [&](const ModelDef& m) { return m.name == name; });
        if (f.models.size() != n) {
            save_form(f, (root / "forms" / (s + ".mort")).string());
            changed.push_back(s);
        }
    }
    pr.models.push_back(shared);
    save_project(pr, app_dir);
    return changed;
}

std::vector<std::string> set_shared_model(const std::string& app_dir, const std::string& name, const ModelDef& now) {
    Project pr = load_project(app_dir);
    auto it = std::find_if(pr.models.begin(), pr.models.end(), [&](const ModelDef& m) { return m.name == name; });
    if (it == pr.models.end()) throw std::runtime_error("the project has no shared model " + name);
    const ModelDef was = *it;
    *it = now;
    for (const auto& i : validate_models(pr.models))
        if (i.error) throw std::runtime_error(i.where + ": " + i.what);
    const fs::path root(app_dir);
    std::vector<std::string> changed;
    for (const auto& s : pr.forms) {
        const auto p = root / "forms" / (s + ".mort");
        Form f = load_form(p.string());
        if (now.name != name)
            for (const auto& m : f.models)
                if (m.name == now.name) throw std::runtime_error(s + " has its own " + now.name + " -- pick another name");
        if (carry_model(f.root, was, now)) { save_form(f, p.string()); changed.push_back(s); }
    }
    save_project(pr, app_dir);
    return changed;
}

std::vector<std::string> unshare_model(const std::string& app_dir, const std::string& name) {
    Project pr = load_project(app_dir);
    auto it = std::find_if(pr.models.begin(), pr.models.end(), [&](const ModelDef& m) { return m.name == name; });
    if (it == pr.models.end()) throw std::runtime_error("the project has no shared model " + name);
    const ModelDef m = *it;
    const fs::path root(app_dir);
    std::vector<std::string> users;
    for (const auto& s : pr.forms) {
        const auto p = root / "forms" / (s + ".mort");
        Form f = load_form(p.string());
        bool shows = false;
        walk(f.root, [&](const Node& n, const Node*) {
            const auto x = n.props.find("model");
            shows = shows || (x != n.props.end() && std::holds_alternative<std::string>(x->second) &&
                              std::get<std::string>(x->second) == name);
        });
        if (!shows) continue;
        f.models.push_back(m);
        save_form(f, p.string());
        users.push_back(s);
    }
    pr.models.erase(it);
    save_project(pr, app_dir);
    return users;
}

std::string create_workspace(const AppInfo& app, const std::string& parent) {
    const fs::path ws = fs::path(parent) / app.bin;
    if (fs::exists(ws) && !fs::is_empty(ws))
        throw std::runtime_error(ws.string() + " already exists -- Mortimer won't write over it");
    fs::create_directories(ws / "backups" / "Old");
    fs::create_directories(ws / "fixes");
    fs::create_directories(ws / "docs");   // s010: filled only when Mortimer was built with seed docs

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
    const Form main = starter_form(app);
    save_form(main, (app_dir / "forms" / "MainWindow.mort").string());
    // s023: your half of the main window, written here (once) rather than by
    // generate -- its Open hook already calls the form's file dialog.
    for (auto out : user_stubs(main, app)) {
        if (out.path == "src/MainWindow.cpp") {
            out.content = replace_all(out.content,
                "void MainWindow::on_open_clicked() {\n"
                "    if (auto lg = log::get(log::Area::Ui)) lg->info(\"MainWindow: open clicked\");\n",
                "void MainWindow::on_open_clicked() {\n"
                "    // open_doc is the form's file dialog (forms/MainWindow.mort, \"files\").\n"
                "    // This opens it over the window and returns AT ONCE -- the dialog is\n"
                "    // asynchronous, so the answer comes later, to on_open_doc_chosen()\n"
                "    // below. Cancel delivers nothing. Clicked again while it's up: refused\n"
                "    // and logged, so there is never more than one.\n"
                "    choose_open_doc();\n");
            out.content = replace_all(out.content,
                "void MainWindow::on_open_doc_chosen(const std::string& path) {\n",
                "void MainWindow::on_open_doc_chosen(const std::string& path) {\n"
                "    // The file the user picked -- a full path. Read it here. (A file with\n"
                "    // no local path, from a network share, arrives as its URI.)\n");
            if (out.content.find("    choose_open_doc();") == std::string::npos ||
                out.content.find("Read it here.") == std::string::npos)
                throw std::runtime_error("starter stub: the Open hooks weren't where expected");
        }
        write_if_changed(app_dir / out.path, out.content);
    }
    add_about(app_dir.string());   // s010: the starter's menu opens it
    generate_project(app_dir.string());
    return app_dir.string();
}

}  // namespace mortimer::core
