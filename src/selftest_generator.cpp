// selftest_generator.cpp -- s001: the generator's half of mortimer_selftest.
//
// Headless, like the rest: the catalog, the form model, validation, the code
// the generator writes (as text), the hook scanner, the hooks report, and a
// whole new workspace made in a temp dir. What it can't do is compile the
// output -- tests/proof.sh does that, on a real compiler.
//
// THE GATE: every catalog entry -- each widget type, each of its properties,
// each signal, and every common property -- must be used by
// tests/KitchenSink.mort (or the starter form). proof.sh compiles that form, so
// an entry added here without being exercised fails HERE, and an entry spelled
// wrong fails THERE. (CANON: a claim isn't a fact until a consumer exercises it.)
#include "core/Catalog.hpp"
#include "core/Codegen.hpp"
#include "core/Form.hpp"
#include "core/HookScan.hpp"
#include "core/Project.hpp"
#include "core/Templates.hpp"

#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>

#include <unistd.h>

namespace core = mortimer::core;
namespace fs = std::filesystem;

using Check = std::function<void(const std::string&, bool, const std::string&)>;

namespace {

bool has(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }

std::string slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

core::Form small_form() {
    using core::Value;
    core::Form f;
    f.name = "Probe";
    core::Node ok{"Button", "ok", {{"label", Value(std::string("O\"K"))}}, {}, {{"clicked", false, ""}}, {}};
    core::Node must{"Button", "must", {}, {}, {{"clicked", true, ""}}, {}};
    core::Node box{"Box", "box", {{"orientation", Value(std::string("vertical"))}}, {}, {}, {ok, must}};
    f.root = {"Window", "probe", {}, {}, {{"close_request", false, ""}}, {box}};
    return f;
}

std::vector<std::string> errors_of(const core::Form& f) {
    std::vector<std::string> out;
    for (const auto& i : core::validate(f))
        if (i.error) out.push_back(i.what);
    return out;
}

bool hooked(const core::HookReport& r, const std::string& name) {
    for (const auto& h : r.hooks)
        if (h.hook.name == name) return h.hooked;
    return false;
}

bool any_error_has(const core::Form& f, const std::string& s) {
    for (const auto& e : errors_of(f))
        if (has(e, s)) return true;
    return false;
}

}  // namespace

void selftest_generator(const Check& check) {
    // ── the catalog itself ──────────────────────────────────────────────────
    {
        std::set<std::string> types;
        bool unique = true, setters = true, signals = true;
        for (const auto& w : core::catalog()) {
            unique = unique && types.insert(w.type).second;
            for (const auto& p : w.props)
                setters = setters && (has(p.setter, "{v}") || has(p.setter, "{item}") || has(p.setter, "{list}") ||
                                      has(p.setter, "{menu}") || has(p.setter, "{model}"));
            for (const auto& s : w.signals)
                signals = signals && (s.ret == "void" || !s.ret_default.empty());
        }
        for (const auto& p : core::common_props())
            setters = setters && (has(p.setter, "{v}") || has(p.setter, "{item}"));
        check("catalog: type names unique", unique, std::to_string(types.size()) + " types");
        check("catalog: every setter has a value slot", setters, "");
        check("catalog: every non-void signal has an unhooked default", signals, "");
        check("catalog: Scale's range is always emitted (GTK's default is 0..0)",
              core::find_prop(*core::find_widget("Scale"), "upper")->always, "");
    }

    // ── THE GATE: the kitchen sink exercises the whole catalog ──────────────
    {
        const fs::path ks = fs::path(MORTIMER_SOURCE_DIR) / "tests" / "KitchenSink.mort";
        core::Form sink;
        bool loaded = true;
        try {
            sink = core::load_form(ks.string());
        } catch (const std::exception& e) {
            loaded = false;
            check("gate: tests/KitchenSink.mort loads", false, e.what());
        }
        if (loaded) {
            check("gate: the kitchen sink validates", !core::has_errors(core::validate(sink)), "");
            const core::AppInfo app = core::derive_app("Gate");
            const core::Form starter = core::starter_form(app);
            // type -> props used / signals used, across both forms.
            std::map<std::string, std::set<std::string>> props, sigs;
            std::set<std::string> common;
            for (const core::Form* f : {static_cast<const core::Form*>(&sink), &starter})
                core::walk(f->root, [&](const core::Node& n, const core::Node*) {
                    const auto* w = core::find_widget(n.type);
                    for (const auto& [k, v] : n.props) {
                        props[n.type].insert(k);
                        for (const auto& c : core::common_props())
                            if (c.name == k) common.insert(k);
                    }
                    for (const auto& h : n.signals) sigs[n.type].insert(h.signal);
                    (void)w;
                });
            std::string missing;
            for (const auto& w : core::catalog()) {
                // ApplicationWindow shares Window's setters (same table entries).
                const std::string as = (w.type == "ApplicationWindow" || w.type == "Dialog") ? "Window" : w.type;
                if (!props.count(w.type) && !sigs.count(w.type) && w.type != "ApplicationWindow" &&
                    w.type != "Dialog")
                    missing += " " + w.type;
                for (const auto& p : w.props)
                    if (!props[w.type].count(p.name) && !props[as].count(p.name))
                        missing += " " + w.type + "." + p.name;
                for (const auto& s : w.signals)
                    if (!sigs[w.type].count(s.name) && !sigs[as].count(s.name))
                        missing += " " + w.type + "~" + s.name;
            }
            for (const auto& c : core::common_props())
                if (!common.count(c.name)) missing += " *." + c.name;
            check("gate: the kitchen sink uses every type, property and signal", missing.empty(),
                  missing.empty() ? "" : "unexercised:" + missing);
            // and every container's packing
            std::set<core::ChildKind> kinds;
            for (const core::Form* f : {static_cast<const core::Form*>(&sink), &starter})
                core::walk(f->root, [&](const core::Node& n, const core::Node*) {
                    if (!n.children.empty()) kinds.insert(core::find_widget(n.type)->children);
                });
            std::string unpacked;
            for (const auto& w : core::catalog())
                if (w.children != core::ChildKind::None && !kinds.count(w.children))
                    unpacked += std::string(" ") + core::child_kind_name(w.children);
            check("gate: every kind of container holds a child", unpacked.empty(), unpacked);
        }
    }

    // ── the form model: JSON round trip ─────────────────────────────────────
    {
        const auto f = small_form();
        const std::string a = core::form_to_json(f);
        const auto g = core::form_from_json(a);
        check("form: JSON round-trips byte-exact", core::form_to_json(g) == a, "");
        check("form: required survives the trip", g.root.children[0].children[1].signals[0].required, "");
        bool threw = false;
        try { core::form_from_json("{ nope"); } catch (const std::exception&) { threw = true; }
        check("form: bad JSON is an error, not a crash", threw, "");
        threw = false;
        try { core::form_from_json(R"({"name":"X"})"); } catch (const std::exception&) { threw = true; }
        check("form: no root is an error", threw, "");
        const auto s = core::form_from_json(R"({"name":"S","root":{"type":"Window","id":"w","signals":["close_request"]}})");
        check("form: a bare signal name is allowed", s.root.signals.size() == 1 && !s.root.signals[0].required, "");
    }

    // ── validate: each mistake names itself ─────────────────────────────────
    {
        check("validate: the probe form is clean", errors_of(small_form()).empty(), "");
        auto f = small_form();
        f.root.children[0].children[0].id = "class";
        check("validate: a C++ keyword id", any_error_has(f, "C++ identifier"), "");
        f = small_form();
        f.root.children[0].children[1].id = "ok";
        check("validate: a duplicate id", any_error_has(f, "used twice"), "");
        f = small_form();
        f.root.children[0].children[0].type = "Buton";
        check("validate: an unknown type", any_error_has(f, "unknown widget type"), "");
        f = small_form();
        f.root.children[0].props["colour"] = std::string("red");
        check("validate: an unknown property", any_error_has(f, "no property \"colour\""), "");
        f = small_form();
        f.root.children[0].props["spacing"] = std::string("wide");
        check("validate: a property of the wrong kind", any_error_has(f, "takes a int"), "");
        f = small_form();
        f.root.children[0].props["orientation"] = std::string("diagonal");
        check("validate: an enum value not in the list", any_error_has(f, "one of: horizontal, vertical"), "");
        f = small_form();
        f.root.children[0].children[0].signals.push_back({"toggled", false, ""});
        check("validate: a signal the type doesn't have", any_error_has(f, "no signal \"toggled\""), "");
        f = small_form();
        f.root.children[0].children[0].children.push_back({"Label", "inner", {}, {}, {}, {}});
        check("validate: children in a leaf", any_error_has(f, "cannot hold children"), "");
        f = small_form();
        f.root.children.push_back({"Label", "second", {}, {}, {}, {}});
        check("validate: two children in a window", any_error_has(f, "holds ONE child"), "");
        f = small_form();
        f.root.props["visible"] = true;
        check("validate: visible on the window (found driving s001)", any_error_has(f, "present()"), "");
        f = small_form();
        f.root.type = "Box";
        check("validate: a root that isn't a window", any_error_has(f, "must be a window"), "");
        f = small_form();
        f.root.children[0].type = "Grid";
        f.root.children[0].props.clear();
        check("validate: a Grid child without column/row", any_error_has(f, "column and row"), "");
        f = small_form();
        f.root.children[0].children.push_back({"StackSidebar", "side", {{"stack", std::string("box")}}, {}, {}, {}});
        check("validate: a reference to the wrong type", any_error_has(f, "must point at a Stack"), "");
        f = small_form();
        f.root.children[0].children.push_back({"StackSidebar", "side", {{"stack", std::string("nope")}}, {}, {}, {}});
        check("validate: a reference to nothing", any_error_has(f, "isn't in the form"), "");
        f = small_form();
        f.root.children[0].children[0].signals[0].handler = "on_must_clicked";
        check("validate: two signals, one hook name", any_error_has(f, "make the hook"), "");
    }

    // ── codegen: the text it writes ─────────────────────────────────────────
    {
        const core::AppInfo app = core::derive_app("Probe Tool");
        const auto f = small_form();
        const auto out = core::generate_form(f, app);
        const std::string& h = out[0].content;
        const std::string& c = out[1].content;
        check("codegen: two files, named for the form",
              out.size() == 2 && out[0].path == "generated/ProbeUi.hpp" && out[1].path == "generated/ProbeUi.cpp", "");
        check("codegen: the marker on line one", h.rfind(std::string("// ") + core::kGeneratedMarker, 0) == 0, "");
        check("codegen: base class of the root type", has(h, "class ProbeUi : public Gtk::Window"), "");
        check("codegen: a hook is virtual", has(h, "virtual void on_ok_clicked();"), "");
        check("codegen: a REQUIRED hook is pure", has(h, "virtual void on_must_clicked() = 0;"), "");
        check("codegen: a root hook carries the root id", has(h, "virtual bool on_probe_close_request();"), "");
        check("codegen: members are Named, by id", has(h, "widgets::Button m_ok;"), "");
        check("codegen: registry names <root>.<id>", has(c, "m_ok(\"probe.ok\")"), "");
        check("codegen: strings are escaped", has(c, "m_ok.set_label(\"O\\\"K\");"), "");
        check("codegen: enums spelled for C++", has(c, "m_box.set_orientation(Gtk::Orientation::VERTICAL);"), "");
        check("codegen: packing", has(c, "m_box.append(m_ok);") && has(c, "set_child(m_box);"), "");
        check("codegen: close_request runs before GTK's default",
              has(c, "&ProbeUi::on_probe_close_request), false));"), "");
        check("codegen: the destructor cuts hooks first",
              c.find("c.disconnect()") < c.find("registry::remove(this)"), "");
        check("codegen: bind_hooks after build_ui", c.find("    build_ui();") < c.find("    bind_hooks();"), "");
        check("codegen: an unhooked default logs", has(c, "hooks::unhooked(\"Probe\", \"on_ok_clicked\""), "");
        check("codegen: a bool default returns", has(c, "return false;"), "");
        check("codegen: no default body for a REQUIRED hook", !has(c, "ProbeUi::on_must_clicked("), "");
        check("codegen: namespace from the app", has(h, "namespace probetool::ui {"), "");
        check("codegen: cpp_string escapes control bytes",
              core::cpp_string("a\tb\x01\\") == "\"a\\tb\\001\\\\\"", core::cpp_string("a\tb\x01\\"));
        check("codegen: UTF-8 kept as is", core::cpp_string("ünï") == "\"ünï\"", "");

        // every container's packing, rendered
        core::Form k = core::load_form((fs::path(MORTIMER_SOURCE_DIR) / "tests" / "KitchenSink.mort").string());
        const std::string kc = core::generate_form(k, app)[1].content;
        check("codegen: titlebar slot", has(kc, "set_titlebar(m_header);"), "");
        check("codegen: header bar slots",
              has(kc, "m_header.pack_start(m_start_btn);") && has(kc, "m_header.set_title_widget(m_title);") &&
                  has(kc, "m_header.pack_end(m_menu);"), "");
        check("codegen: paned sides + resize/shrink",
              has(kc, "m_split.set_start_child(m_sidebar);") && has(kc, "m_split.set_resize_start_child(false);") &&
                  has(kc, "m_split.set_shrink_end_child(true);"), "");
        check("codegen: stack pages with icon",
              has(kc, "m_pages.add(m_form_grid, \"inputs\", \"Inputs\")->set_icon_name(\"document-edit-symbolic\");"), "");
        check("codegen: grid attach with span", has(kc, "m_form_grid.attach(m_name, 1, 0, 2, 1);"), "");
        check("codegen: a reference becomes the member", has(kc, "m_sidebar.set_stack(m_pages);"), "");
        check("codegen: a string list, whole", has(kc, "Gtk::StringList::create(std::vector<Glib::ustring>{\"Vanilla\", \"Chocolate\", \"Strawberry\"})"), "");
        check("codegen: a string list, per item", has(kc, "m_go.add_css_class(\"suggested-action\");"), "");
        check("codegen: a double from an integer", has(kc, "m_volume.get_adjustment()->set_upper(11.0);"), "");
        check("codegen: a double as written", has(kc, "m_volume.set_value(7.25);"), "");
        check("codegen: range before value",
              kc.find("m_volume.get_adjustment()->set_upper") < kc.find("m_volume.set_value"), "");
        check("codegen: a custom handler name", has(kc, "&KitchenSinkUi::on_name_edited"), "");
        check("codegen: a hook with a parameter",
              has(core::generate_form(k, app)[0].content, "virtual void on_rows_row_activated(Gtk::ListBoxRow* row);"), "");
    }

    // ── the hook scanner ────────────────────────────────────────────────────
    {
        const std::string hdr = R"(
class X : public ui::XUi {
    void on_a_clicked() override;
    void on_b_row_activated(Gtk::ListBoxRow* row,
                            int extra) override;
    bool on_c_close_request() const noexcept override;
    // void on_commented_clicked() override;
    /* void on_blocked_clicked() override; */
    const char* s = "void on_string_clicked() override";
    void on_d_clicked() final override;
    void not_an_override();
    int override_count = 0;
};)";
        const auto o = core::scan_overrides(hdr);
        check("scan: a plain override", o.count("on_a_clicked") == 1, "");
        check("scan: parameters across lines", o.count("on_b_row_activated") == 1, "");
        check("scan: const noexcept qualifiers", o.count("on_c_close_request") == 1, "");
        check("scan: final override", o.count("on_d_clicked") == 1, "");
        check("scan: commented out doesn't count",
              !o.count("on_commented_clicked") && !o.count("on_blocked_clicked"), "");
        check("scan: inside a string doesn't count", !o.count("on_string_clicked"), "");
        check("scan: exactly four", o.size() == 4, std::to_string(o.size()));
    }

    // ── the report: hooked / unhooked / missing / stale, across generations ─
    {
        auto f = small_form();
        auto r1 = core::build_report(f, true, "void on_ok_clicked() override;", "");
        check("report: hooked", hooked(r1, "on_ok_clicked"), "");
        check("report: REQUIRED not written is missing", r1.missing == std::vector<std::string>{"on_must_clicked"}, "");
        check("report: unhooked counted", r1.unhooked_count() == 1, std::to_string(r1.unhooked_count()));
        const std::string m1 = core::report_manifest(r1);
        // rename ok -> fine: on_ok_clicked retires; the header still overrides it
        f.root.children[0].children[0].id = "fine";
        auto r2 = core::build_report(f, true, "void on_ok_clicked() override;", m1);
        check("report: a retired hook still overridden is STALE", r2.stale == std::vector<std::string>{"on_ok_clicked"}, "");
        // a third generation still remembers -- the memory is kept, not just the last diff
        auto r3 = core::build_report(f, true, "void on_ok_clicked() override;", core::report_manifest(r2));
        check("report: STALE survives another generate", r3.stale == std::vector<std::string>{"on_ok_clicked"}, "");
        // it comes back: no longer retired
        f.root.children[0].children[0].id = "ok";
        auto r4 = core::build_report(f, true, "void on_ok_clicked() override;", core::report_manifest(r3));
        check("report: a hook that comes back isn't stale", r4.stale.empty() && hooked(r4, "on_ok_clicked"), "");
        const std::string t = core::report_text(r2);
        check("report text: paste lines for what isn't hooked",
              has(t, "    void on_fine_clicked() override;") && has(t, "void Probe::on_fine_clicked() {"), "");
        check("report text: names the stale one", has(t, "STALE") && has(t, "  on_ok_clicked"), "");
        check("report: a damaged manifest is survivable", core::build_report(f, true, "", "{ junk").hooks.size() == 3, "");
    }

    // ── names ───────────────────────────────────────────────────────────────
    {
        const auto a = core::derive_app("Yatzé!");
        check("derive: binary from the name", a.bin == "yatz", a.bin);
        check("derive: default id", a.app_id == "io.github.scott8420.Yatz", a.app_id);
        check("derive: an explicit id wins", core::derive_app("Notes", "x", "org.example.N").app_id == "org.example.N", "");
        bool threw = false;
        try { core::derive_app("3D"); } catch (const std::exception&) { threw = true; }
        check("derive: must start with a letter", threw, "");
        check("derive: a keyword namespace is moved aside", core::derive_app("Class").ns == "class_app", "");
        check("snake: MainWindow -> main_window", core::snake("MainWindow") == "main_window", "");
        const std::string r = core::render_tokens("@APP@ @BIN@ @BIN_UPPER@ @APP_ID@ @APP_ID_PATH@ @NS@ @BINDIR@",
                                                  core::derive_app("Notes"));
        check("tokens: all of ours, and @BINDIR@ left for install.sh",
              r == "Notes notes NOTES io.github.scott8420.Notes /io/github/scott8420/Notes notes @BINDIR@", r);
    }

    // ── a whole new workspace, on disk ──────────────────────────────────────
    {
        const fs::path tmp = fs::temp_directory_path() / ("mortimer_selftest_" + std::to_string(::getpid()));
        fs::remove_all(tmp);
        fs::create_directories(tmp);
        const auto app = core::derive_app("Sample");
        std::string dir;
        try {
            dir = core::create_workspace(app, tmp.string());
        } catch (const std::exception& e) {
            check("workspace: created", false, e.what());
        }
        if (!dir.empty()) {
            const fs::path ws = tmp / "sample", ad = ws / "sample";
            bool tree = fs::is_directory(ws / "backups" / "Old") && fs::is_directory(ws / "fixes") &&
                        fs::is_regular_file(ws / "docs" / "HANDOFF.md") &&
                        fs::is_regular_file(ws / "scripts" / "sample_backup.sh") &&
                        fs::is_regular_file(ad / "data" / "io.github.scott8420.Sample.desktop.in") &&
                        fs::is_regular_file(ad / "data" / "io.github.scott8420.Sample.svg") &&
                        fs::is_regular_file(ad / "resources" / "icons" / "scalable" / "apps" / "sample-logo-symbolic.svg");
            check("workspace: the house tree", tree, "");
            check("workspace: scripts are executable",
                  (fs::status(ad / "install.sh").permissions() & fs::perms::owner_exec) != fs::perms::none &&
                      (fs::status(ws / "scripts" / "sample_backup.sh").permissions() & fs::perms::owner_exec) != fs::perms::none,
                  "");
            // No token survived anywhere (but @BINDIR@, which is install.sh's).
            std::string leftovers;
            for (const auto& e : fs::recursive_directory_iterator(ws)) {
                if (!e.is_regular_file()) continue;
                std::string s = slurp(e.path());
                for (std::size_t at = s.find('@'); at != std::string::npos; at = s.find('@', at + 1)) {
                    const auto end = s.find('@', at + 1);
                    if (end == std::string::npos || end - at > 14) continue;
                    const std::string tok = s.substr(at, end - at + 1);
                    bool upper = tok.size() > 2;
                    for (char ch : tok.substr(1, tok.size() - 2)) upper = upper && ((ch >= 'A' && ch <= 'Z') || ch == '_');
                    if (upper && tok != "@BINDIR@") leftovers += " " + e.path().filename().string() + ":" + tok;
                }
            }
            check("workspace: no @TOKEN@ left unrendered", leftovers.empty(), leftovers);
            const std::string handoff = slurp(ws / "docs" / "HANDOFF.md");
            check("workspace: HANDOFF opens with the next session + files block",
                  handoff.find("## Next session") < handoff.find("## Files to upload") &&
                      has(handoff, "**`s001_first_form`**"), "");
            check("workspace: HANDOFF carries RULES verbatim", has(handoff, "# Sample working rules") &&
                  has(handoff, "## Milestones are the checkpoint boundary"), "");
            check("workspace: your half written", fs::is_regular_file(ad / "include" / "MainWindow.hpp") &&
                  fs::is_regular_file(ad / "src" / "MainWindow.cpp"), "");

            // A second generate writes nothing; your half is never rewritten.
            const std::string mine = "// mine\n";
            { std::ofstream(ad / "src" / "MainWindow.cpp", std::ios::trunc) << mine; }
            auto g = core::generate_project(ad.string());
            check("generate: nothing changed, nothing written", g.written.empty() && g.ok(),
                  std::to_string(g.written.size()) + " written");
            check("generate: your half left alone", slurp(ad / "src" / "MainWindow.cpp") == mine, "");

            // Prune: a stale generated file with our marker goes; a stranger's stays.
            { std::ofstream(ad / "generated" / "GoneUi.cpp") << "// " << core::kGeneratedMarker << "\n"; }
            { std::ofstream(ad / "generated" / "notes.txt") << "mine\n"; }
            g = core::generate_project(ad.string());
            check("generate: prunes what no form makes", !fs::exists(ad / "generated" / "GoneUi.cpp") &&
                  g.pruned.size() == 1, "");
            check("generate: leaves a file that isn't ours", fs::exists(ad / "generated" / "notes.txt"), "");

            // A broken form: reported, not generated, and its last good files kept.
            { std::ofstream(ad / "forms" / "MainWindow.mort", std::ios::trunc) << "{ broken"; }
            g = core::generate_project(ad.string());
            check("generate: a broken form is an error", !g.ok(), "");
            check("generate: ...and its last good files stay", fs::exists(ad / "generated" / "MainWindowUi.cpp"), "");

            bool refused = false;
            try { core::create_workspace(app, tmp.string()); } catch (const std::exception&) { refused = true; }
            check("workspace: never written over", refused, "");
        }
        fs::remove_all(tmp);
    }

    // ── the embedded template ───────────────────────────────────────────────
    {
        bool cmake = false, rules = false;
        for (const auto& t : core::project_templates()) {
            cmake = cmake || std::string(t.path) == "@BIN@/CMakeLists.txt";
            rules = rules || std::string(t.path) == "docs/RULES.md";
        }
        check("templates: embedded in the binary", cmake && rules,
              std::to_string(core::project_templates().size()) + " files");
        // XML forbids "--" inside a comment; GLib's parser forgives it, librsvg
        // doesn't (found s001: the icon wouldn't render).
        std::string bad;
        for (const auto& t : core::project_templates()) {
            const std::string p = t.path, c = t.content;
            if (!(p.ends_with(".svg") || p.ends_with(".xml"))) continue;
            for (std::size_t a = c.find("<!--"); a != std::string::npos; a = c.find("<!--", a + 4)) {
                const std::size_t e = c.find("-->", a + 4);
                if (e == std::string::npos || c.substr(a + 4, e - a - 4).find("--") != std::string::npos) {
                    bad += " " + p;
                    break;
                }
            }
        }
        check("templates: XML comments are valid XML", bad.empty(), bad);
    }

    // ── s005: file dialogs ──────────────────────────────────────────────────
    {
        auto with = [](std::vector<core::FileDef> files) {
            auto f = small_form();
            f.files = std::move(files);
            return f;
        };
        core::FileDef open;
        open.name = "open_doc";
        open.title = "Open \"it\"";
        open.accept = "Open";
        open.filters = {"Text=*.txt; *.md", "Images=image/*"};
        open.cancel = true;
        core::FileDef many;  many.name = "open_many";  many.mode = "open_multiple";
        core::FileDef save;  save.name = "save_doc";   save.mode = "save";  save.required = true;
        core::FileDef dir;   dir.name = "pick_dir";    dir.mode = "folder";

        const auto f = with({open, many, save, dir});
        const std::string text = core::form_to_json(f);
        const auto back = core::form_from_json(text);
        check("files: JSON round-trips", back.files == f.files && core::form_to_json(back) == text, "");
        check("files: none -> nothing on disk (old forms byte-identical)",
              !has(core::form_to_json(small_form()), "\"files\""), "");
        check("files: defaults stay off disk", !has(text, "\"cancel\": false") && !has(text, "\"title\": \"\""), "");
        check("files: the probe with four dialogs validates", errors_of(f).empty(),
              errors_of(f).empty() ? "" : errors_of(f)[0]);

        const auto r = core::parse_file_filter("Text=*.txt; *.md;");
        check("files: a filter row parses (trims, skips empties)",
              r.ok && r.label == "Text" && r.patterns == core::StringList{"*.txt", "*.md"}, "");
        check("files: a filter with no label or no pattern is not ok",
              !core::parse_file_filter("=*.txt").ok && !core::parse_file_filter("Text=").ok &&
              !core::parse_file_filter("*.txt").ok, "");

        { auto x = open; x.mode = "load";
          check("validate files: an unknown mode", any_error_has(with({x}), "open, open_multiple, save or folder"), ""); }
        { auto x = open; x.filters = {"*.txt"};
          check("validate files: a filter without a label", any_error_has(with({x}), "Label=*.txt"), ""); }
        { check("validate files: declared twice", any_error_has(with({open, open}), "declared twice"), ""); }
        { auto x = open; x.name = "class";
          check("validate files: a keyword name", any_error_has(with({x}), "C++ identifier"), ""); }
        { auto x = dir; x.filters = {"Text=*.txt"};
          const auto is = core::validate(with({x}));
          bool warned = false;
          for (const auto& i : is) warned = warned || (!i.error && has(i.what, "filters are ignored"));
          check("validate files: filters on a folder dialog warn", warned && !core::has_errors(is), ""); }
        { // action "y_chosen" -> on_action_y_chosen; dialog "action_y" -> on_action_y_chosen
          auto g = small_form();
          g.actions.push_back({"y_chosen", "", false});
          core::FileDef y; y.name = "action_y";
          g.files.push_back(y);
          check("validate files: a hook an action also makes", any_error_has(g, "make the hook on_action_y_chosen"), ""); }
        { auto g = small_form();
          g.root.children[0].children.push_back({"Label", "files", {}, {}, {}, {}});
          check("validate: an id the generated class owns (m_files)", any_error_has(g, "is taken"), ""); }

        const core::AppInfo app = core::derive_app("Probe Tool");
        const auto out = core::generate_form(f, app);
        const std::string& h = out[0].content;
        const std::string& c = out[1].content;
        check("codegen files: a public chooser per dialog",
              has(h, "void choose_open_doc(const std::string& initial = {});") &&
              has(h, "void choose_pick_dir(const std::string& initial = {});"), "");
        check("codegen files: chosen is a hook (path)", has(h, "virtual void on_open_doc_chosen(const std::string& path);"), "");
        check("codegen files: open_multiple's hook takes paths",
              has(h, "virtual void on_open_many_chosen(const std::vector<std::string>& paths);"), "");
        check("codegen files: REQUIRED chosen is pure", has(h, "virtual void on_save_doc_chosen(const std::string& path) = 0;"), "");
        check("codegen files: cancel ticked -> a hook; unticked -> none",
              has(h, "virtual void on_open_doc_cancelled();") && !has(h, "on_open_many_cancelled"), "");
        check("codegen files: the token and the in-flight map",
              has(h, "std::shared_ptr<bool> m_alive") && has(h, "m_files;"), "");
        check("codegen files: each mode calls its own GTK pair",
              has(c, "dialog->open(*this,") && has(c, "open_finish(result)") &&
              has(c, "dialog->open_multiple(*this,") && has(c, "open_multiple_finish(result)") &&
              has(c, "dialog->save(*this,") && has(c, "save_finish(result)") &&
              has(c, "dialog->select_folder(*this,") && has(c, "select_folder_finish(result)"), "");
        check("codegen files: the answer checks the token first", has(c, "if (alive.expired()) return;"), "");
        check("codegen files: the destructor drops the token and cancels",
              has(c, "m_alive.reset();") && has(c, "cancel->cancel();"), "");
        check("codegen files: one at a time", has(c, "already open"), "");
        check("codegen files: a '/' is a MIME type, else a pattern",
              has(c, "ff->add_mime_type(\"image/*\");") && has(c, "ff->add_pattern(\"*.md\");"), "");
        check("codegen files: strings escaped", has(c, "set_title(\"Open \\\"it\\\"\");"), "");
        check("codegen files: save takes a bare name", has(c, "set_initial_name(initial)"), "");
        check("codegen files: no file dialogs -> no file-dialog code",
              !has(core::generate_form(small_form(), app)[1].content, "FileDialog"), "");
        const auto stubs = core::user_stubs(f, app);
        check("stubs files: the override logs what was chosen",
              has(stubs[1].content, "open_doc chosen {}\", path") &&
              has(stubs[1].content, "std::to_string(paths.size())"), "");
    }

    // ── s006: models + ColumnView ───────────────────────────────────────────
    {
        using core::Value;
        auto table = [](bool with_model = true) {
            auto f = small_form();
            core::Node cv{"ColumnView", "notes", {}, {}, {{"activated", false, ""}, {"selected", false, ""}}, {}};
            if (with_model) cv.props["model"] = Value(std::string("Note"));
            f.root.children[0].children.push_back(cv);
            f.models = {{"Note", {{"title", "string", ""}, {"due_date", "string", "Due"}, {"count", "int", ""},
                                  {"weight", "double", ""}, {"done", "bool", ""}}}};
            return f;
        };
        const auto f = table();
        check("models: the probe table validates", errors_of(f).empty(), errors_of(f).empty() ? "" : errors_of(f)[0]);
        const std::string text = core::form_to_json(f);
        const auto back = core::form_from_json(text);
        check("models: JSON round-trips", back.models == f.models && core::form_to_json(back) == text, "");
        check("models: none -> nothing on disk", !has(core::form_to_json(small_form()), "\"models\""), "");
        check("models: a header from the name, or the title",
              core::field_title({"due_date", "string", ""}) == "Due date" &&
              core::field_title({"due_date", "string", "Due"}) == "Due", "");

        { auto g = f; g.models[0].name = "class";
          check("validate models: a keyword name", any_error_has(g, "C++ class name"), ""); }
        { auto g = f; g.models.push_back(g.models[0]);
          check("validate models: declared twice", any_error_has(g, "declared twice"), ""); }
        { auto g = f; g.models[0].fields.push_back({"id", "int", ""});
          check("validate models: a field named id (every item has id())", any_error_has(g, "is taken"), ""); }
        { auto g = f; g.models[0].fields[1].name = "title";
          check("validate models: a field twice", any_error_has(g, "declared twice"), ""); }
        { auto g = f; g.models[0].fields[0].type = "date";
          check("validate models: an unknown field type", any_error_has(g, "string, int, double or bool"), ""); }
        { auto g = f; g.models[0].fields.clear();
          check("validate models: no fields", any_error_has(g, "at least one field"), ""); }
        { auto g = f; g.models[0].name = "Other";
          check("validate ColumnView: a model the form doesn't have", any_error_has(g, "no such model"), ""); }
        { check("validate ColumnView: hooks but no model", any_error_has(table(false), "pick a model first"), ""); }
        { auto g = f; g.root.children[0].children.push_back({"Label", "notes_store", {}, {}, {}, {}});
          check("validate ColumnView: a widget named like its store", any_error_has(g, "m_notes_store"), ""); }

        const core::AppInfo app = core::derive_app("Probe Tool");
        const auto out = core::generate_form(f, app);
        const std::string& h = out[0].content;
        const std::string& c = out[1].content;
        check("codegen models: a nested item class", has(h, "class NoteItem : public Glib::Object {"), "");
        check("codegen models: create(fields..., id = 0)",
              has(h, "static Glib::RefPtr<NoteItem> create(const std::string& title = {}, const std::string& due_date = {}, "
                     "std::int64_t count = {}, double weight = {}, bool done = {}, std::uint64_t id = 0);"), "");
        check("codegen models: plain C++ fields", has(h, "std::int64_t count = 0;") && has(h, "bool         done = false;"), "");
        check("codegen models: the id is freed with the item", has(h, "~NoteItem() override { ids().erase(m_id); }"), "");
        check("codegen models: a taken id is refused (null + error)",
              has(c, "if (id != 0 && ids().count(id))") && has(c, "is taken -- not created") && has(c, "return {};"), "");
        check("codegen models: the counter goes on past yours", has(c, "next() = id + 1;"), "");
        check("codegen table: the store and selection are members",
              has(h, "Glib::RefPtr<Gio::ListStore<NoteItem>> m_notes_store;") &&
              has(h, "Glib::RefPtr<Gtk::SingleSelection> m_notes_selection;"), "");
        check("codegen table: sorted by the clicked header",
              has(c, "Gtk::SortListModel::create(m_notes_store, m_notes.get_sorter())"), "");
        check("codegen table: a column per field, titled",
              has(c, "Gtk::ColumnViewColumn::create(\"Title\", factory)") &&
              has(c, "Gtk::ColumnViewColumn::create(\"Due\", factory)") &&
              has(c, "Gtk::ColumnViewColumn::create(\"Done\", factory)"), "");
        check("codegen table: strings sort as strings, the rest as numbers",
              has(c, "Gtk::StringSorter::create(") && has(c, "Gtk::NumericSorter<double>::create("), "");
        check("codegen table: a bool is a read-only check", has(c, "w->set_active(item->done);") &&
                                                            has(c, "set_can_target(false)"), "");
        check("codegen table: no setter for model (the block does it)", !has(c, "{model}"), "");
        check("codegen table: hooks take the row",
              has(h, "virtual void on_notes_activated(const Glib::RefPtr<NoteItem>& item);") &&
              has(h, "virtual void on_notes_selected(const Glib::RefPtr<NoteItem>& item);"), "");
        check("codegen table: activated -> the SORTED row at that position",
              has(c, "m_notes_selection->get_object(position)"), "");
        check("codegen table: selected -> the selected row (null for none)",
              has(c, "m_notes_selection->get_selected_item()"), "");
    }

    // ── s007: editable cells, multi-selection ───────────────────────────────
    {
        using core::Value;
        auto table = [] {
            auto f = small_form();
            core::Node cv{"ColumnView", "notes", {}, {}, {{"selected", false, ""}, {"edited", false, ""}}, {}};
            cv.props["model"] = Value(std::string("Note"));
            cv.props["selection"] = Value(std::string("multiple"));
            cv.props["editable"] = Value(core::StringList{"title", "count", "done"});
            f.root.children[0].children.push_back(cv);
            f.models = {{"Note", {{"title", "string", ""}, {"count", "int", ""}, {"weight", "double", ""},
                                  {"done", "bool", ""}}}};
            return f;
        };
        const auto f = table();
        check("edit: the probe validates", errors_of(f).empty(), errors_of(f).empty() ? "" : errors_of(f)[0]);
        check("edit: helpers read the node", core::view_selection(f.root.children[0].children.back()) == "multiple" &&
                                             core::view_edits(f.root.children[0].children.back(), "count") &&
                                             !core::view_edits(f.root.children[0].children.back(), "weight"), "");
        { auto g = f; g.root.children[0].children.back().props["editable"] = Value(core::StringList{"nope"});
          check("validate edit: a field the model lacks", any_error_has(g, "Note has no such field"), ""); }
        { auto g = f; g.root.children[0].children.back().props["editable"] = Value(core::StringList{"title", "title"});
          check("validate edit: a field twice", any_error_has(g, "names title twice"), ""); }
        { auto g = f; g.root.children[0].children.back().props.erase("model");
          g.root.children[0].children.back().signals.clear();
          check("validate edit: editable without a model", any_error_has(g, "pick a model first"), ""); }
        { auto g = f; g.root.children[0].children.back().props["selection"] = Value(std::string("none"));
          check("validate edit: selected hooked, selection none", any_error_has(g, "nothing can be picked"), ""); }
        { auto g = f; g.root.children[0].children.back().props["selection"] = Value(std::string("some"));
          check("validate edit: an unknown selection", any_error_has(g, "single, multiple, none"), ""); }
        { auto g = f; g.root.children[0].children.back().props.erase("editable");
          bool warned = false;
          for (const auto& i : core::validate(g)) warned = warned || (!i.error && i.what.find("no cell is editable") != std::string::npos);
          check("validate edit: edited hooked, nothing editable -> a warning", warned && errors_of(g).empty(), ""); }
        { auto g = f; g.root.children[0].children.push_back({"Label", "notes_edited", {}, {}, {}, {}});
          check("validate edit: a widget named like its signal", any_error_has(g, "m_notes_edited"), ""); }

        const core::AppInfo app = core::derive_app("Probe Tool");
        const auto out = core::generate_form(f, app);
        const std::string& h = out[0].content;
        const std::string& c = out[1].content;
        check("codegen edit: multiple -> a MultiSelection member",
              has(h, "Glib::RefPtr<Gtk::MultiSelection> m_notes_selection;") &&
              has(c, "m_notes_selection = Gtk::MultiSelection::create("), "");
        check("codegen edit: selected hands you every picked row",
              has(h, "virtual void on_notes_selected(const std::vector<Glib::RefPtr<NoteItem>>& items);") &&
              has(c, "m_notes_selection->get_selection();") && has(c, "picked->get_nth(i)"), "");
        check("codegen edit: edited hook takes (row, field)",
              has(h, "virtual void on_notes_edited(const Glib::RefPtr<NoteItem>& item, const std::string& field);"), "");
        check("codegen edit: the cells' signal is a member, connected in m_hooks",
              has(h, "sigc::signal<void(const Glib::RefPtr<NoteItem>&, const std::string&)> m_notes_edited;") &&
              has(c, "m_hooks.push_back(m_notes_edited.connect("), "");
        check("codegen edit: text / number cells are EditableLabels, double-click to edit",
              has(c, "Gtk::make_managed<Gtk::EditableLabel>()") && has(c, "presses != 2") &&
              has(c, "w->start_editing();") && has(c, "EventSequenceState::CLAIMED"), "");
        check("codegen edit: written only when it differs, then emitted",
              has(c, "if (now == item->title) return;") && has(c, "item->title = now;") &&
              has(c, "m_notes_edited.emit(item, \"title\");"), "");
        check("codegen edit: a number is parsed; not a number is put back",
              has(c, "template <class T> bool parse_cell(") && has(c, "if (!parse_cell(w->get_text(), now))") &&
              has(c, "isn't a whole number"), "");
        check("codegen edit: an editable bool is a live check",
              has(c, "w->signal_toggled().connect(") && has(c, "item->done = w->get_active();"), "");
        check("codegen edit: a click outside commits the open cell",
              has(c, "auto notes_open = std::make_shared<Gtk::EditableLabel*>(nullptr);") &&
              has(c, "set_propagation_phase(Gtk::PropagationPhase::CAPTURE)") && has(c, "open->stop_editing(true);"), "");
        check("codegen edit: a non-editable column stays a Label",
              has(c, "column weight (double)\n") || has(c, "column weight (double)"), "");
        { auto g = f; g.root.children[0].children.back().props["editable"] = Value(core::StringList{"title"});
          check("codegen edit: no number edits -> no parse helper",
                !has(core::generate_form(g, app)[1].content, "parse_cell"), ""); }
        { auto g = f; auto& v = g.root.children[0].children.back();
          v.props["selection"] = Value(std::string("none")); v.signals = {{"activated", false, ""}};
          const auto o = core::generate_form(g, app);
          check("codegen edit: none -> a NoSelection, activate still finds the row",
                has(o[0].content, "Glib::RefPtr<Gtk::NoSelection> m_notes_selection;") &&
                has(o[1].content, "Gtk::NoSelection::create(") &&
                has(o[1].content, "m_notes_selection->get_object(position)"), ""); }
        { auto g = f; g.root.children[0].children.back().props.erase("editable");
          const auto o = core::generate_form(g, app);
          check("codegen edit: nothing editable -> no signal member, no EditableLabel",
                !has(o[0].content, "m_notes_edited") && !has(o[1].content, "EditableLabel"), ""); }
    }
}
