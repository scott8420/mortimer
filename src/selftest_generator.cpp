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
                setters = setters && (has(p.setter, "{v}") || has(p.setter, "{item}") || has(p.setter, "{list}"));
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
                const std::string as = w.type == "ApplicationWindow" ? "Window" : w.type;
                if (!props.count(w.type) && !sigs.count(w.type) && w.type != "ApplicationWindow")
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
}
