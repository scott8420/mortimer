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
#include "core/CssScope.hpp"
#include "core/Design.hpp"
#include "core/Form.hpp"
#include "core/HookScan.hpp"
#include "core/Project.hpp"
#include "core/Templates.hpp"

#include <algorithm>
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
                                      has(p.setter, "{menu}") || has(p.setter, "{model}") ||
                                      has(p.setter, "{state}"));
            for (const auto& s : w.signals)
                signals = signals && (s.ret == "void" || !s.ret_default.empty());
        }
        for (const auto& p : core::common_props())
            setters = setters && (has(p.setter, "{v}") || has(p.setter, "{item}") || has(p.setter, "{bind}"));
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
            bool seeded = false;
            for (const auto& t : core::project_templates()) seeded = seeded || std::string(t.path) == "docs/RULES.md";
            bool tree = fs::is_directory(ws / "backups" / "Old") && fs::is_directory(ws / "fixes") &&
                        (!seeded || fs::is_regular_file(ws / "docs" / "HANDOFF.md")) &&
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
            if (seeded) {   // s010: only a build with the private seed docs writes them
                const std::string handoff = slurp(ws / "docs" / "HANDOFF.md");
                check("workspace: HANDOFF opens with the next session + files block",
                      handoff.find("## Next session") < handoff.find("## Files to upload") &&
                          has(handoff, "**`s001_first_form`**"), "");
                check("workspace: HANDOFF carries RULES verbatim", has(handoff, "# Sample working rules") &&
                      has(handoff, "## Milestones are the checkpoint boundary"), "");
            } else {
                check("workspace: no seed docs -> an empty docs/", fs::is_directory(ws / "docs") &&
                      fs::is_empty(ws / "docs"), "");
            }
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
        bool cmake = false;
        for (const auto& t : core::project_templates()) cmake = cmake || std::string(t.path) == "@BIN@/CMakeLists.txt";
        check("templates: embedded in the binary", cmake,
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
        // s024: the item definitions are in generated/<Form>Items.cpp, the third file.
        const std::string& c = out[1].content;
        const std::string& ic = out.size() > 2 ? out[2].content : out[1].content;
        check("codegen models: a nested item class", has(h, "class NoteItem : public Glib::Object {"), "");
        check("codegen models: create(fields..., id = 0)",
              has(h, "static Glib::RefPtr<NoteItem> create(const std::string& title = {}, const std::string& due_date = {}, "
                     "std::int64_t count = {}, double weight = {}, bool done = {}, std::uint64_t id = 0);"), "");
        check("codegen models: plain C++ fields", has(h, "std::int64_t count = 0;") && has(h, "bool         done = false;"), "");
        check("codegen models: the id is freed with the item", has(h, "~NoteItem() override { ids().erase(m_id); }"), "");
        check("codegen models: a taken id is refused (null + error)",
              has(ic, "if (id != 0 && ids().count(id))") && has(ic, "is taken -- not created") && has(ic, "return {};"), "");
        check("codegen models: the counter goes on past yours", has(ic, "next() = id + 1;"), "");
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

    // ── s008: window state ──────────────────────────────────────────────────
    {
        using core::Value;
        auto f = small_form();
        f.root.props["remember"] = Value(true);
        core::Node a{"Label", "a", {}, {{"slot", Value(std::string("start"))}}, {}, {}};
        core::Node pg{"Label", "pg", {}, {{"name", Value(std::string("first"))}}, {}, {}};
        core::Node st{"Stack", "pages", {}, {{"slot", Value(std::string("end"))}}, {}, {pg}};
        f.root.children[0].children.push_back({"Paned", "split", {}, {}, {}, {a, st}});
        check("state: the probe validates", errors_of(f).empty(), errors_of(f).empty() ? "" : errors_of(f)[0]);
        check("state: remembers() reads the root", core::remembers(f) && !core::remembers(small_form()), "");
        const core::AppInfo app = core::derive_app("Probe Tool");
        const auto out = core::generate_form(f, app);
        const std::string& h = out[0].content;
        const std::string& c = out[1].content;
        check("codegen state: save is public, restore private",
              has(h, "    void save_window_state();") && has(h, "    void restore_window_state();"), "");
        check("codegen state: restored after build_ui, before the hooks",
              has(c, "    build_ui();\n    restore_window_state();\n    bind_hooks();"), "");
        check("codegen state: saved on UNMAP (a destroyed window never hides)",
              has(c, "signal_unmap().connect(sigc::mem_fun(*this, &ProbeUi::save_window_state))") &&
              !has(c, "signal_hide()"), "");
        check("codegen state: deleted while on screen still saves",
              has(c, "if (get_visible()) save_window_state();"), "");
        check("codegen state: size + maximized, sane sizes only",
              has(c, "set_default_size(") && has(c, "*w < 30000") && has(c, "maximize();") &&
              has(c, "get_default_size(w, h);") && has(c, "is_maximized()"), "");
        check("codegen state: a Paned's divider, a Stack's page (only a page it has)",
              has(c, "state::put_int(k + \"split.position\", m_split.get_position());") &&
              has(c, "m_split.set_position(") && has(c, "m_pages.get_child_by_name(*page)") &&
              has(c, "state::put_string(k + \"pages.page\", page);"), "");
        check("codegen state: keys under the form's name", has(c, "const std::string k = \"Probe.\";"), "");
        check("codegen state: no setter for remember", !has(c, "{state}"), "");
        const auto plain = core::generate_form(small_form(), app);
        check("codegen state: not remembering -> no state code",
              !has(plain[0].content, "window_state") && !has(plain[1].content, "WindowState"), "");
        const auto sup = core::generate_support(app);
        std::string wsh, wsc;
        for (const auto& o : sup) {
            if (o.path == "generated/WindowState.hpp") wsh = o.content;
            if (o.path == "generated/WindowState.cpp") wsc = o.content;
        }
        check("support state: WindowState.hpp/.cpp, the app's namespace",
              has(wsh, "namespace probetool::ui::state {") && has(wsc, "namespace probetool::ui::state {"),
              wsh.substr(0, 0));
        check("support state: ~/.config/<bin>/window-state.json, written atomically",
              has(wsc, "\"/probetool/window-state.json\"") && has(wsc, "g_file_set_contents(") &&
              has(wsc, "g_get_user_config_dir()"), "");
        check("support state: a file it can't read starts fresh (warns, never throws)",
              has(wsc, "isn't readable -- starting fresh") && !has(wsc, "throw"), "");
        check("support state: no token left unfilled", !has(wsh + wsc, "@NS@") && !has(wsh + wsc, "@BIN@") &&
                                                        !has(wsh + wsc, "@MARK@"), "");
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

    // ── s010: actions that open windows; the shortcuts window; About ────────
    {
        using core::Value;
        const auto app = core::derive_app("Opener");
        core::Form f;
        f.name = "MainWindow";
        core::Node menu{"MenuButton", "menu", {{"menu", Value(core::StringList{"Save the Thing=win.save-it"})}}, {}, {}, {}};
        core::Node box{"Box", "box", {}, {}, {}, {menu}};
        f.root = {"ApplicationWindow", "main_window", {{"title", Value(std::string("Opener"))}}, {}, {}, {box}};
        f.actions = {{"save_it", "<Control>s", false},
                     {"show_keys", "<Control>question", false, "Show the keys", core::kShortcutsTarget},
                     {"about", "", false, "", "About"},
                     {"save_as", "<Control><Shift>s", false}};

        // JSON: on disk only when set, and back exactly.
        const std::string js = core::form_to_json(f);
        check("s010 json: title / opens written when set", has(js, "\"opens\": \"About\"") &&
              has(js, "\"title\": \"Show the keys\"") && has(js, "\"opens\": \"@shortcuts\""), "");
        { core::Form g; g.name = "X"; g.root = {"Window", "x", {}, {}, {}, {}}; g.actions = {{"a", "", false}};
          check("s010 json: unset -> not on disk", !has(core::form_to_json(g), "opens") &&
                !has(core::form_to_json(g), "title"), ""); }
        check("s010 json: round trip", core::form_from_json(js).actions == f.actions, "");

        // The row a key shows.
        check("s010 title: its own title first", core::action_title(f, f.actions[1]) == "Show the keys", "");
        check("s010 title: else its menu row's label", core::action_title(f, f.actions[0]) == "Save the Thing", "");
        check("s010 title: else the name, readable", core::action_title(f, f.actions[3]) == "Save as" &&
              core::readable("MainWindow") == "Main window", core::action_title(f, f.actions[3]));

        // Validate: opens names a form (or @shortcuts), never its own; no hook to require.
        check("s010 validate: the form is valid", errors_of(f).empty(),
              errors_of(f).empty() ? "" : errors_of(f)[0]);
        { auto g = f; g.actions[2].opens = "not a form";
          check("s010 validate: opens a non-name -> error", !errors_of(g).empty(), ""); }
        { auto g = f; g.actions[2].opens = "MainWindow";
          check("s010 validate: opens itself -> error", !errors_of(g).empty(), ""); }
        { auto g = f; g.actions[2].required = true; bool w = false;
          for (const auto& i : core::validate(g)) w = w || (!i.error && has(i.what, "no hook to require"));
          check("s010 validate: required + opens warns", w && errors_of(g).empty(), ""); }
        { auto g = f; g.root.children[0].children.push_back({"Label", "opens_about", {}, {}, {}, {}});
          check("s010 validate: a widget named opens_about collides", !errors_of(g).empty(), ""); }
        { auto g = f; g.actions.push_back({"x", "", false, "", ""});   // a hook-action named like the
          g.actions.push_back({"about2", "", false, "", "About"});        // opener is still fine
          check("s010 validate: two actions may open the same form", errors_of(g).empty(), ""); }

        // Codegen: no hook; a kept window; forward declarations only in the header.
        bool hook = false;
        for (const auto& k : core::hooks_of(f)) hook = hook || k.name == "on_action_about" || k.name == "on_action_show_keys";
        check("s010 codegen: an opener has no hook", !hook && core::hooks_of(f).size() == 2, "");
        const auto o = core::generate_form(f, app);
        check("s010 codegen: header forward-declares, keeps a unique_ptr",
              has(o[0].content, "namespace opener {\nclass About;\n}") &&
              has(o[0].content, "namespace opener::ui {\nclass ShortcutsWindow;\n}") &&
              has(o[0].content, "std::unique_ptr<::opener::About> m_opens_about;") &&
              has(o[0].content, "#include <memory>") && !has(o[0].content, "#include \"About.hpp\""), "");
        check("s010 codegen: the source includes them whole",
              has(o[1].content, "#include \"About.hpp\"") && has(o[1].content, "#include \"generated/Shortcuts.hpp\""), "");
        check("s010 codegen: built on first ask, hides on close, presented over this window",
              has(o[1].content, "if (!m_opens_about) {") &&
              has(o[1].content, "m_opens_about = std::make_unique<::opener::About>();") &&
              has(o[1].content, "m_opens_about->set_hide_on_close(true);") &&
              has(o[1].content, "m_opens_about->set_transient_for(*this);") &&
              !has(o[1].content, "on_action_about"), "");
        check("s010 codegen: an opener's key is still wired",
              has(o[1].content, "parse_string(\"<Control>question\")") &&
              has(o[1].content, "NamedAction::create(\"win.show-keys\")"), "");
        { auto g = f; g.actions = {{"a", "", false}};
          const auto p = core::generate_form(g, app);
          check("s010 codegen: no openers -> no <memory>, no forward block",
                !has(p[0].content, "#include <memory>") && !has(p[0].content, "s010"), ""); }

        // The shortcuts window: a row per key, in form order, words escaped.
        core::Form d;
        d.name = "Prefs";
        d.root = {"Dialog", "prefs", {}, {}, {}, {}};
        d.actions = {{"reset", "<Control>r", false, "Reset \"all\"", ""}, {"nokey", "", false}};
        const auto sc = core::generate_shortcuts({f, d}, app);
        const std::string scc = sc.size() == 2 ? sc[1].content : "";
        check("s010 shortcuts: two files", sc.size() == 2 && sc[0].path == "generated/Shortcuts.hpp" &&
              sc[1].path == "generated/Shortcuts.cpp" && has(sc[0].content, "class ShortcutsWindow : public Gtk::Window"), "");
        const auto r1 = scc.find("{\"Opener\", \"<Control>s\", \"Save the Thing\", \"\"}");
        const auto r2 = scc.find("{\"Opener\", \"<Control>question\", \"Show the keys\", \"\"}");
        const auto r3 = scc.find("{\"Opener\", \"<Control><Shift>s\", \"Save as\", \"\"}");
        const auto r4 = scc.find("{\"Prefs\", \"<Control>r\", \"Reset \\\"all\\\"\", \"\"}");
        check("s010 shortcuts: each key a row, sectioned by window title / name, in order",
              r1 != std::string::npos && r1 < r2 && r2 < r3 && r3 < r4 && r4 != std::string::npos, "");
        check("s010 shortcuts: an action without a key has no row", !has(scc, "nokey") && !has(scc, "\"About\""), "");
        check("s010 shortcuts: GTK writes the key, hides on close, Escape closes",
              has(scc, "->to_label(get_display())") && has(scc, "set_hide_on_close(true)") &&
              has(scc, "NamedAction::create(\"window.close\")"), "");
        check("s010 shortcuts: none -> still compiles (a sentinel row) and says so",
              has(core::generate_shortcuts({}, app)[1].content, "{nullptr, nullptr, nullptr, nullptr}") &&
              has(core::generate_shortcuts({}, app)[1].content, "No keys yet"), "");

        // The project: a new one has About + both openers; add_about; bad targets refused.
        const fs::path tmp = fs::temp_directory_path() / ("mortimer_s010_" + std::to_string(::getpid()));
        fs::remove_all(tmp);
        fs::create_directories(tmp);
        std::string dir;
        try { dir = core::create_workspace(app, tmp.string()); } catch (const std::exception& e) {
            check("s010 project: created", false, e.what()); }
        if (!dir.empty()) {
            const fs::path ad(dir);
            const auto pr = core::load_project(dir);
            check("s010 project: a new app lists About", pr.forms.size() == 2 && pr.forms[1] == "About", "");
            const auto about = core::load_form((ad / "forms" / "About.mort").string());
            check("s010 project: About is a valid Dialog with the logo and window.close",
                  about.root.type == "Dialog" && errors_of(about).empty() &&
                  has(core::form_to_json(about), "opener-logo-symbolic") &&
                  has(core::form_to_json(about), "window.close"), "");
            check("s010 project: your About shows core::version()",
                  has(slurp(ad / "src" / "About.cpp"), "m_version.set_label(\"Version \" + core::version());") &&
                  has(slurp(ad / "src" / "About.cpp"), "#include \"core/Version.hpp\""), "");
            check("s010 project: the starter's menu opens both",
                  has(slurp(ad / "generated" / "MainWindowUi.cpp"), "std::make_unique<::opener::About>()") &&
                  has(slurp(ad / "generated" / "MainWindowUi.cpp"), "std::make_unique<::opener::ui::ShortcutsWindow>()") &&
                  has(slurp(ad / "generated" / "MainWindowUi.cpp"), "\"win.shortcuts\"") &&
                  has(slurp(ad / "generated" / "MainWindowUi.cpp"), "\"win.about\""), "");
            // s023: the starter's Open button opens a file -- the form's dialog, your
            // half's clicked hook calls it, the chosen hook is declared and logs.
            {
                const auto mf = core::load_form((ad / "forms" / "MainWindow.mort").string());
                const std::string uc = slurp(ad / "src" / "MainWindow.cpp");
                const auto at = uc.find("void MainWindow::on_open_clicked() {");
                check("s023 starter: the form has file dialog open_doc (open)",
                      mf.files.size() == 1 && mf.files[0].name == "open_doc" && mf.files[0].mode == "open" &&
                      errors_of(mf).empty(), "");
                check("s023 starter: your Open hook calls choose_open_doc()",
                      at != std::string::npos && uc.find("    choose_open_doc();", at) != std::string::npos &&
                      uc.find("    choose_open_doc();", at) < uc.find("\n}\n", at), "");
                check("s023 starter: on_open_doc_chosen declared, logs the path",
                      has(slurp(ad / "include" / "MainWindow.hpp"),
                          "void on_open_doc_chosen(const std::string& path) override;") &&
                      has(uc, "lg->info(\"MainWindow: open_doc chosen {}\", path);") &&
                      has(slurp(ad / "generated" / "MainWindowUi.hpp"), "choose_open_doc("), "");
            }
            check("s010 project: generated/Shortcuts lists the starter's keys",
                  has(slurp(ad / "generated" / "Shortcuts.cpp"), "{\"Opener\", \"<Control>h\", \"Say Hello\", \"\"}") &&
                  has(slurp(ad / "generated" / "Shortcuts.cpp"), "\"Keyboard Shortcuts\", \"\"}"), "");
            bool twice = false;
            try { core::add_about(dir); } catch (const std::exception&) { twice = true; }
            check("s010 project: a second About refused", twice, "");

            // A target that isn't a form, or is the main window -> error, not generated.
            auto mw = core::load_form((ad / "forms" / "MainWindow.mort").string());
            const std::string good = slurp(ad / "generated" / "MainWindowUi.cpp");
            mw.actions.push_back({"prefs", "", false, "", "Prefs"});
            core::save_form(mw, (ad / "forms" / "MainWindow.mort").string());
            auto g = core::generate_project(dir);
            bool said = false;
            for (const auto& [st, i] : g.issues) said = said || (i.error && has(i.what, "isn't a form of this project"));
            check("s010 project: opens an unknown form -> error, last good files kept",
                  !g.ok() && said && slurp(ad / "generated" / "MainWindowUi.cpp") == good, "");
            auto ab = core::load_form((ad / "forms" / "About.mort").string());
            ab.actions = {{"home", "", false, "", "MainWindow"}};
            core::save_form(ab, (ad / "forms" / "About.mort").string());
            g = core::generate_project(dir);
            said = false;
            for (const auto& [st, i] : g.issues) said = said || (st == "About" && i.error && has(i.what, "the main window"));
            check("s010 project: opens the main window -> error", said, "");

            // add_about on a project without one.
            fs::remove(ad / "forms" / "About.mort");
            fs::remove(ad / "include" / "About.hpp");
            fs::remove(ad / "src" / "About.cpp");
            auto pj = core::load_project(dir);
            pj.forms = {"MainWindow"};
            core::save_project(pj, dir);
            bool added = false;
            try { core::add_about(dir); added = true; } catch (const std::exception&) {}
            check("s010 project: add_about writes the form, your half, the listing",
                  added && fs::exists(ad / "forms" / "About.mort") && fs::exists(ad / "src" / "About.cpp") &&
                  core::load_project(dir).forms.back() == "About", "");
        }
        fs::remove_all(tmp);
    }

    // ── s011: ListView with a designed row ─────────────────────────────────
    {
        using core::Value;
        auto S = [](const char* x) { return Value(std::string(x)); };
        const auto app = core::derive_app("Lister");
        core::Form f;
        f.name = "MainWindow";
        f.models = {{"Task", {{"title", "string", ""}, {"icon", "string", ""}, {"done", "bool", ""}, {"n", "int", ""}}}};
        core::Node title{"Label", "t_title", {{"bind", S("title")}}, {}, {}, {}};
        core::Node icon{"Image", "t_icon", {{"bind", S("icon")}}, {}, {}, {}};
        core::Node done{"CheckButton", "t_done", {{"bind", S("done")}}, {}, {}, {}};
        core::Node n{"Label", "t_n", {{"bind", S("n")}}, {}, {}, {}};
        core::Node row{"Box", "t_row", {{"spacing", Value(std::int64_t{6})}}, {}, {}, {icon, title, n, done}};
        core::Node view{"ListView", "task_list", {{"model", S("Task")}}, {}, {{"activated", false, ""}, {"selected", false, ""}}, {row}};
        core::Node sc{"ScrolledWindow", "sc", {}, {}, {}, {view}};
        f.root = {"Window", "main_window", {}, {}, {}, {sc}};
        check("s011 validate: a list with a designed row is valid", errors_of(f).empty(),
              errors_of(f).empty() ? "" : errors_of(f)[0]);
        auto at = [](core::Form& g) -> core::Node& { return g.root.children[0].children[0]; };   // the view
        { auto g = f; at(g).children.clear();
          check("s011 validate: a model and no row -> error", !errors_of(g).empty(), ""); }
        { auto g = f; at(g).children.push_back(core::Node{"Label", "extra", {}, {}, {}, {}});
          check("s011 validate: two rows -> error", !errors_of(g).empty(), ""); }
        { auto g = f; at(g).children[0].children[1].props["bind"] = S("nope");
          check("s011 validate: bind names an unknown field -> error", !errors_of(g).empty(), ""); }
        { auto g = f; at(g).children[0].children[3].props["bind"] = S("title");
          check("s011 validate: a check bound to a string -> error", !errors_of(g).empty(), ""); }
        { auto g = f; at(g).children[0].children[0].props["bind"] = S("n");
          check("s011 validate: an Image bound to an int -> error", !errors_of(g).empty(), ""); }
        { auto g = f; at(g).children[0].children.push_back(core::Node{"ListBox", "lb", {}, {}, {}, {}});
          check("s011 validate: a ListBox can't be in a row", !errors_of(g).empty(), ""); }
        { auto g = f; at(g).children[0].children[3].signals = {{"toggled", false, ""}};
          check("s011 m2 validate: a row widget's hook is fine", errors_of(g).empty(), ""); }
        { auto g = f; g.root.children[0].children.push_back(core::Node{"Label", "loose", {{"bind", S("title")}}, {}, {}, {}});
          check("s011 validate: bind outside a row -> error", !errors_of(g).empty(), ""); }
        { auto g = f; g.root.children.clear();
          g.root.children.push_back({"Box", "b", {}, {}, {}, {f.root.children[0].children[0], f.root.children[0].children[0]}});
          g.root.children[0].children[1].id = "Task_list";   // ids differ, the row class doesn't
          g.root.children[0].children[1].children[0].id = "r2";
          for (auto& c : g.root.children[0].children[1].children[0].children) c.id += "2";
          check("s011 validate: two views making one row class -> error", !errors_of(g).empty(), ""); }

        check("s011 names: TaskListRow", core::row_class(at(f)) == "TaskListRow" && core::pascal("a_b_c") == "ABC", "");
        bool member = false;
        core::walk_members(f.root, [&](const core::Node& x, const core::Node*) { member = member || x.id == "t_title"; });
        check("s011 walk_members: a row's widgets aren't the form's", !member, "");

        const auto o = core::generate_form(f, app);
        const std::string& h = o[0].content;
        const std::string& c = o[1].content;
        check("s011 codegen: the row class, nested, plain Gtk:: members",
              has(h, "class TaskListRow : public Gtk::Box {") &&
              has(h, "void bind(const Glib::RefPtr<TaskItem>& item);") &&
              has(h, "Gtk::Label       m_t_title;") && !has(h, "widgets::Label m_t_title"), "");
        check("s011 codegen: the form doesn't own the row's widgets",
              !has(c, "m_t_title(\"main_window.t_title\")") && !has(h, "    widgets::Box            m_t_row;"), "");
        check("s011 codegen: the row is built in its constructor",
              has(c, "MainWindowUi::TaskListRow::TaskListRow(MainWindowUi& ui) : m_ui(&ui), m_alive(ui.m_rows_alive) {") &&
              has(c, "    set_spacing(6);") &&
              has(c, "    append(m_t_title);"), "");
        check("s011 codegen: bind shows each field the right way",
              has(c, "m_t_icon.set_from_icon_name(item->icon);") && has(c, "m_t_title.set_label(item->title);") &&
              has(c, "m_t_n.set_label(std::to_string(item->n));") && has(c, "m_t_done.set_active(item->done);"), "");
        check("s011 m2 codegen: a bound check writes back (only when it differs), then edited",
              has(c, "m_t_done.signal_toggled().connect([this] {") &&
              has(c, "if (m_item->done != m_t_done.get_active()) {") &&
              has(c, "undo::edit(m_item, \"Edit \u201CDone\u201D\", [&] { m_item->done = now; }, {}, false);") &&
              !has(c, "set_can_target(false)"), "");
        check("s011 codegen: store, selection (no sorter), factory setup + bind",
              has(c, "m_task_list_store = Gio::ListStore<TaskItem>::create();") &&
              has(c, "Gtk::SingleSelection::create(m_task_list_store);") &&
              has(c, "cell->set_child(*Gtk::make_managed<TaskListRow>(*this));") && has(c, "row->unbind();") &&
              has(c, "if (row && item) row->bind(item);") && has(c, "m_task_list.set_factory(factory);"), "");
        check("s011 codegen: hooks hand you the row",
              has(c, "on_task_list_activated(std::dynamic_pointer_cast<TaskItem>(m_task_list_selection->get_object(position)))"), "");
        check("s011 codegen: build_ui doesn't build the row", !has(c, "m_task_list.append("), "");

        // m2: a row's hooks hand you its item; inputs write back; edited fires.
        auto g = f;
        at(g).signals.push_back({"edited", false, ""});
        core::Node del{"Button", "t_del", {}, {}, {{"clicked", true, ""}}, {}};
        core::Node sp{"SpinButton", "t_spin", {{"bind", S("n")}}, {}, {}, {}};
        core::Node en{"Entry", "t_en", {{"bind", S("n")}}, {}, {}, {}};
        at(g).children[0].children.push_back(del);
        at(g).children[0].children.push_back(sp);
        at(g).children[0].children.push_back(en);
        check("s011 m2 validate: hooked row widgets + edited are valid", errors_of(g).empty(),
              errors_of(g).empty() ? "" : errors_of(g)[0]);
        bool row_hook = false;
        for (const auto& k : core::hooks_of(g))
            row_hook = row_hook || (k.name == "on_t_del_clicked" && k.params == "const Glib::RefPtr<TaskItem>& item" && k.required);
        check("s011 m2 hooks: a row button's hook takes the item (and can be required)", row_hook, "");
        const auto o2 = core::generate_form(g, app);
        const std::string& h2 = o2[0].content;
        const std::string& c2 = o2[1].content;
        check("s011 m2 codegen: the hook is the form's (pure virtual when required)",
              has(h2, "virtual void on_t_del_clicked(const Glib::RefPtr<TaskItem>& item) = 0;"), "");
        check("s011 m2 codegen: the row calls it with its item, guarded",
              has(c2, "m_t_del.signal_clicked().connect([this] {") &&
              has(c2, "if (m_binding || !m_item || !*m_alive) return;") && has(c2, "m_ui->on_t_del_clicked(m_item);"), "");
        check("s011 m2 codegen: edited is a member signal, emitted by the row, hooked by bind_hooks",
              has(h2, "m_task_list_edited;") && has(c2, "m_ui->m_task_list_edited.emit(m_item, \"done\");") &&
              has(c2, "m_task_list_edited.connect("), "");
        check("s011 m2 codegen: an int from a spin is rounded; an Entry over an int only shows",
              has(c2, "static_cast<std::int64_t>(std::llround(m_t_spin.get_value()))") &&
              has(c2, "m_t_en.set_editable(false);   // shows n (an int); not written back"), "");
        check("s011 m2 codegen: bind() isn't the user; unbind lets go; the token dies first",
              has(c2, "    m_binding = true;") && has(c2, "    m_binding = false;") &&
              has(c2, "::unbind() {\n    undo::follow(*this, {});") && has(c2, "*m_rows_alive = false;") &&
              has(h2, "std::shared_ptr<bool> m_rows_alive = std::make_shared<bool>(true);"), "");
        { auto u = core::user_stubs(g, app);
          check("s011 m2 stubs: your half overrides the row hook", has(u[0].content, "void on_t_del_clicked(const Glib::RefPtr<TaskItem>& item) override;"), ""); }
    }

    // ── s012: the app's stylesheet, scoped to the canvas card ──────────────
    {
        const std::string S = ".mortimer-window";
        auto sc = [&](const std::string& in) { return core::scope_css(in, S); };
        check("s012 css: a type selector gets the scope as ancestor",
              sc("label { color: red; }") == ".mortimer-window label { color: red; }", sc("label { color: red; }"));
        check("s012 css: a list -- each one scoped, the spacing kept",
              sc("label, .title,\nbutton:hover { a: b; }") ==
                  ".mortimer-window label, .mortimer-window .title,\n.mortimer-window button:hover { a: b; }",
              sc("label, .title,\nbutton:hover { a: b; }"));
        check("s012 css: `window` is the card itself",
              sc("window { x: y; } window.big > box { x: y; } windowish { x: y; }") ==
                  ".mortimer-window { x: y; } .mortimer-window.big > box { x: y; } .mortimer-window windowish { x: y; }",
              sc("window { x: y; } window.big > box { x: y; } windowish { x: y; }"));
        check("s012 css: a comma inside :not() isn't a split",
              sc("button:not(.a, .b) { x: y; }") == ".mortimer-window button:not(.a, .b) { x: y; }",
              sc("button:not(.a, .b) { x: y; }"));
        const std::string at = "@define-color accent #c00;\n@import url(\"x.css\");\n@keyframes spin { from { a: b; } to { a: c; } }\n";
        check("s012 css: @-statements and @keyframes are copied as they are", sc(at) == at, sc(at));
        const std::string cm = "/* label { } */\nlabel /* the labels */ { color: /* } */ red; }\n";
        check("s012 css: comments (even with braces) stay; the selector is still scoped",
              sc(cm) == "/* label { } */\n.mortimer-window label /* the labels */ { color: /* } */ red; }\n", sc(cm));
        check("s012 css: strings with braces in a declaration don't end the block",
              sc("label { content: \"}\"; } box { a: b; }") ==
                  ".mortimer-window label { content: \"}\"; } .mortimer-window box { a: b; }",
              sc("label { content: \"}\"; } box { a: b; }"));
        const std::string many = "a { }\n\n/* x\ny */\nb,\nc { d: e;\n f: g; }\n";
        auto lines = [](const std::string& t) { return std::count(t.begin(), t.end(), '\n'); };
        check("s012 css: line numbers survive (GTK's error line = your line)", lines(sc(many)) == lines(many), sc(many));
        check("s012 css: unterminated input is copied, not lost", has(sc("label { color: red;"), "color: red;"), "");
        check("s012 css: empty in, empty out", sc("").empty() && sc("  \n").size() == 3, "");
        const auto names = core::css_class_names(
            "/* .ignored */ .card, button.big:hover > .x-1 { margin: 0.5em; background: url(\"a.png\"); }\n"
            "@keyframes k { from { a: b; } }\n@define-color c #fff;\nlabel:not(.dim) { }\n.card { }\n");
        check("s012 m3 css: class names from selectors only, sorted, once",
              names == std::vector<std::string>{"big", "card", "dim", "x-1"},
              [&] { std::string o; for (const auto& n : names) o += n + " "; return o; }());
    }

    // ── s013: models shared across forms ───────────────────────────────────
    {
        using core::Value;
        const auto app = core::derive_app("Sharer");
        const core::ModelDef task{"Task", {{"title", "string", ""}, {"done", "bool", ""}}};
        core::Form f;
        f.name = "MainWindow";
        core::Node cv{"ColumnView", "tasks", {{"model", Value(std::string("Task"))}}, {}, {{"activated", false, ""}}, {}};
        f.root = {"Window", "main_window", {}, {}, {}, {cv}};
        check("s013 validate: an unknown model without the project's -> error", !errors_of(f).empty(), "");
        f.shared = {task};
        check("s013 validate: a shared model is a model", errors_of(f).empty(), errors_of(f).empty() ? "" : errors_of(f)[0]);
        { auto g = f; g.models = {task};
          check("s013 validate: the form's own Task can't hide the shared one", !errors_of(g).empty(), ""); }
        check("s013 json: shared models aren't written with the form", !has(core::form_to_json(f), "\"models\""), "");
        const auto o = core::generate_form(f, app);
        check("s013 codegen: a using, not a nested class; Models.hpp included",
              has(o[0].content, "using TaskItem = ::sharer::model::TaskItem;") &&
              !has(o[0].content, "class TaskItem") && has(o[0].content, "#include \"generated/Models.hpp\"") &&
              !has(o[1].content, "TaskItem::create("), "");
        check("s013 codegen: the hook still says TaskItem",
              has(o[0].content, "virtual void on_tasks_activated(const Glib::RefPtr<TaskItem>& item);"), "");
        const auto mo = core::generate_models({task}, app);
        check("s013 Models: one class at namespace level, with its create()",
              mo.size() == 2 && has(mo[0].content, "namespace sharer::model {") &&
              has(mo[0].content, "class TaskItem : public Glib::Object {") &&
              has(mo[1].content, "Glib::RefPtr<TaskItem> TaskItem::create(const std::string& title, bool done, std::uint64_t id) {"), "");
        check("s013 Models: none -> no files", core::generate_models({}, app).empty(), "");
        bool bad = false;
        for (const auto& i : core::validate_models({{"task", {}}})) bad = bad || i.error;
        check("s013 validate_models: a model with no fields -> error", bad, "");
        check("s013 json: models round-trip as text",
              core::models_from_json(core::models_to_json({task})) == std::vector<core::ModelDef>{task}, "");

        // The project: share a form's model; another form's identical copy goes too.
        const fs::path tmp = fs::temp_directory_path() / ("mortimer_s013_" + std::to_string(::getpid()));
        fs::remove_all(tmp);
        fs::create_directories(tmp);
        std::string dir;
        try { dir = core::create_workspace(app, tmp.string()); } catch (const std::exception& e) { check("s013 project", false, e.what()); }
        if (!dir.empty()) {
            const fs::path ad(dir);
            auto mw = core::load_form((ad / "forms" / "MainWindow.mort").string());
            mw.models = {task};
            core::Node mcv{"ColumnView", "tasks", {{"model", Value(std::string("Task"))}}, {}, {}, {}};
            mw.root.children.back().children.push_back(mcv);
            core::save_form(mw, (ad / "forms" / "MainWindow.mort").string());
            core::add_form(dir, "Pick", "Dialog");
            auto pk = core::load_form((ad / "forms" / "Pick.mort").string());
            pk.models = {task};
            pk.root.children.front().children.push_back(mcv);
            core::save_form(pk, (ad / "forms" / "Pick.mort").string());
            bool refused = false;
            try { core::share_model(dir, "MainWindow", "Nope"); } catch (const std::exception&) { refused = true; }
            check("s013 share: an unknown model refused", refused, "");
            { auto pk2 = pk; pk2.models[0].fields.push_back({"extra", "int", ""});
              core::save_form(pk2, (ad / "forms" / "Pick.mort").string());
              std::string why;
              try { core::share_model(dir, "MainWindow", "Task"); } catch (const std::exception& e) { why = e.what(); }
              check("s013 share: another form's DIFFERENT copy refuses, naming it", has(why, "Pick has its own Task"), why);
              core::save_form(pk, (ad / "forms" / "Pick.mort").string()); }
            std::vector<std::string> changed;
            try { changed = core::share_model(dir, "MainWindow", "Task"); } catch (const std::exception& e) { check("s013 share", false, e.what()); }
            check("s013 share: both forms lose their copy, the project has it",
                  changed.size() == 2 && core::load_project(dir).models.size() == 1 &&
                  core::load_form((ad / "forms" / "Pick.mort").string()).models.empty() &&
                  core::load_project_form(dir, "Pick").shared.size() == 1, "");
            const auto g = core::generate_project(dir);
            check("s013 generate: valid, Models.* written", g.ok() && fs::exists(ad / "generated" / "Models.hpp"),
                  g.issues.empty() ? "" : g.issues.front().second.what);
            bool again = false;
            try { core::share_model(dir, "MainWindow", "Task"); } catch (const std::exception&) { again = true; }
            check("s013 share: twice refused", again, "");
            // m3: edit the shared model -- every form follows; then unshare.
            {
                auto lv = core::load_form((ad / "forms" / "Pick.mort").string());
                core::Node lbl{"Label", "r_title", {{"bind", Value(std::string("title"))}}, {}, {}, {}};
                core::Node rowb{"Box", "r_row", {}, {}, {}, {lbl}};
                lv.root.children.front().children.push_back(
                    core::Node{"ListView", "rows", {{"model", Value(std::string("Task"))}}, {}, {}, {rowb}});
                auto& cvn = lv.root.children.front().children[lv.root.children.front().children.size() - 2];
                cvn.props["editable"] = Value(core::StringList{"title"});
                core::save_form(lv, (ad / "forms" / "Pick.mort").string());
                core::ModelDef now{"Job", {{"name", "string", ""}, {"done", "bool", ""}}};
                std::vector<std::string> ch;
                try { ch = core::set_shared_model(dir, "Task", now); } catch (const std::exception& e) { check("s013 m3 edit", false, e.what()); }
                const auto pk2 = core::load_form((ad / "forms" / "Pick.mort").string());
                const std::string js = core::form_to_json(pk2);
                check("s013 m3 edit shared: renamed + a field renamed -> every form follows (model, editable, bind)",
                      ch.size() == 2 && core::load_project(dir).models.front().name == "Job" &&
                      has(js, "\"model\": \"Job\"") && !has(js, "\"model\": \"Task\"") &&
                      has(js, "\"bind\": \"name\""), "");
                core::StringList ed;
                core::walk(pk2.root, [&](const core::Node& n, const core::Node*) {
                    if (n.type == "ColumnView") ed = core::view_editable(n);
                });
                check("s013 m3 edit shared: editable followed the field rename",
                      ed == core::StringList{"name"}, ed.empty() ? "(empty)" : ed.front());
                check("s013 m3 edit shared: the project still generates", core::generate_project(dir).ok(), "");
                bool bad = false;
                try { core::set_shared_model(dir, "Job", {"Job", {}}); } catch (const std::exception&) { bad = true; }
                check("s013 m3 edit shared: an empty model refused", bad, "");
                const auto users = core::unshare_model(dir, "Job");
                check("s013 m3 unshare: each form that shows it gets its own copy back; the project's goes",
                      users.size() == 2 && core::load_project(dir).models.empty() &&
                      core::load_form((ad / "forms" / "Pick.mort").string()).models.size() == 1 &&
                      core::generate_project(dir).ok(), "");
                core::share_model(dir, "MainWindow", "Job");   // back to shared for the prune check below
            }
            auto pj = core::load_project(dir);
            pj.models.clear();
            core::save_project(pj, dir);
            const auto g2 = core::generate_project(dir);
            check("s013 generate: no shared models -> Models.* pruned, the views' forms refused",
                  !fs::exists(ad / "generated" / "Models.hpp") && !g2.ok(), "");
        }
        fs::remove_all(tmp);
    }

    // ── s014: the app's journal is Mortimer's, word for word ───────────────
    {
        std::string tmpl;
        for (const auto& t : core::project_templates())
            if (std::string(t.path) == "@BIN@/include/core/Undo.hpp") tmpl = t.content;
        std::string mine = slurp(fs::path(MORTIMER_SOURCE_DIR) / "include" / "core" / "Undo.hpp");
        for (std::size_t at = 0; (at = mine.find("namespace mortimer::core", at)) != std::string::npos;)
            mine.replace(at, 24, "namespace @NS@::core");
        check("s014 template: include/core/Undo.hpp is Mortimer's journal (namespace aside)",
              !tmpl.empty() && tmpl == mine, tmpl.empty() ? "missing" : "differs");
        const auto sup = core::generate_support(core::derive_app("Undoer"));
        std::string uh, uc;
        for (const auto& o : sup) { if (o.path == "generated/Undo.hpp") uh = o.content; if (o.path == "generated/Undo.cpp") uc = o.content; }
        check("s014 support: generated/Undo -- the journal over items, the door, follow",
              has(uh, "using Journal = ::undoer::core::Journal<Records>;") &&
              has(uh, "bool edit(const std::string& type, std::uint64_t id") &&
              has(uh, "void follow(Glib::ObjectBase& w, sigc::connection c);") &&
              has(uc, "t->changed(item);") && !has(uh, "@NS@"), "");
        core::ModelDef m{"Note", {{"title", "string", ""}, {"n", "int", ""}, {"w", "double", ""}, {"done", "bool", ""}}};
        core::Form f;
        f.name = "MainWindow";
        f.models = {m};
        f.root = {"Window", "main_window", {}, {}, {}, {}};
        const auto o = core::generate_form(f, core::derive_app("Undoer"));
        const std::string items = o.size() > 2 ? o[2].content : std::string();   // s024: <Form>Items.cpp
        check("s014 items: find / snapshot / restore / undo_type / signal_changed, and they register",
              has(o[0].content, "static Glib::RefPtr<NoteItem> find(std::uint64_t id);") &&
              has(o[0].content, "sigc::signal<void()>& signal_changed() { return m_changed; }") &&
              has(items, "::undoer::ui::undo::register_type(undo_type(), {") &&
              has(items, "out_.str(title);") && has(items, "out_.num(n);") &&
              has(items, "out_.dbl(w);") && has(items, "done = in_.flag();"), "");
        check("s014 items: the undo type names the class where it lives",
              has(items, "return \"MainWindowUi::NoteItem\";"), "");
        core::Form a;
        a.name = "X";
        a.root = {"Window", "x", {}, {}, {}, {}};
        a.actions = {{"undo", "<Control>z", false, "", "", "undo"}, {"bad", "", false, "", "", "frob"}};
        bool frob = false, hook = false;
        for (const auto& i : core::validate(a)) frob = frob || (i.error && has(i.what, "does \"frob\""));
        for (const auto& k : core::hooks_of(a)) hook = hook || k.name == "on_action_undo";
        check("s014 actions: does undo has no hook; an unknown does is refused", frob && !hook, "");
        a.actions.pop_back();
        const auto ao = core::generate_form(a, core::derive_app("Undoer"));
        check("s014 actions: undo runs the journal, greyed while there's nothing",
              has(ao[1].content, "undo::undo(); }));") && has(ao[1].content, "a->set_enabled(undo::can_undo());"), "");
        check("s014 starter: Undo / Redo keys and menu rows",
              has(core::form_to_json(core::starter_form(core::derive_app("Undoer"))), "\"does\": \"undo\"") &&
              has(core::form_to_json(core::starter_form(core::derive_app("Undoer"))), "Undo=win.undo"), "");
    }

    // ── s014 m2: rows added / removed undoably ─────────────────────────────
    {
        using core::Value;
        core::Form f;
        f.name = "MainWindow";
        f.models = {{"Note", {{"title", "string", ""}, {"done", "bool", ""}}}};
        core::Node lbl{"Label", "r_t", {{"bind", Value(std::string("title"))}}, {}, {}, {}};
        core::Node row{"Box", "r", {}, {}, {}, {lbl}};
        core::Node lv{"ListView", "notes", {{"model", Value(std::string("Note"))}}, {}, {}, {row}};
        f.root = {"Window", "main_window", {}, {}, {}, {lv}};
        const auto o = core::generate_form(f, core::derive_app("Rows"));
        check("s014 m2: add_<view> / remove_<view> declared, through the undo",
              has(o[0].content, "bool add_notes(const Glib::RefPtr<NoteItem>& item, int position = -1);") &&
              has(o[0].content, "bool remove_notes(const Glib::RefPtr<NoteItem>& item);") &&
              has(o[1].content, "undo::remove(\"MainWindow.notes\", NoteItem::undo_type(), item->id(), \"Delete Note\")"), "");
        check("s014 m2: the store is registered by name, and dropped going",
              has(o[1].content, "undo::register_list(\"MainWindow.notes\", {") &&
              has(o[1].content, "undo::unregister_list(\"MainWindow.notes\");"), "");
        check("s014 m2: an item can be made again (an undone delete): that id, those fields",
              o.size() > 2 && has(o[2].content, "auto x = create({}, {}, n);") && has(o[2].content, "if (x) x->restore(s);"), "");
        std::string uc;
        for (const auto& x : core::generate_support(core::derive_app("Rows"))) if (x.path == "generated/Undo.cpp") uc = x.content;
        check("s014 m2 runtime: a row record is (position, fields); absent = out of the list; made again on undo",
              has(uc, "row_get(") && has(uc, "if (!it) it = t->make(n, fields);   // an undone delete") &&
              has(uc, "if (!row_get(list, type + \"#\" + std::to_string(id))) return false;   // not in it: not a step"), "");
    }

    // ── s016: the shortcuts window's other rows ────────────────────────────
    {
        using core::Value;
        core::Form f;
        f.name = "MainWindow";
        f.models = {{"Note", {{"title", "string", ""}}}};
        core::Node cv{"ColumnView", "notes", {{"model", Value(std::string("Note"))},
                                              {"selection", Value(std::string("multiple"))},
                                              {"editable", Value(core::StringList{"title"})}},
                      {}, {{"activated", false, ""}}, {}};
        f.root = {"Window", "main_window", {{"title", Value(std::string("Notes"))}}, {}, {}, {cv}};
        f.keys = {"Ctrl+scroll=Zoom"};
        const auto autos = core::automatic_key_notes(f);
        check("s016 automatic rows: open, pick several, edit, Escape",
              autos == core::StringList{"Double-click, Enter=Open a row", "Ctrl+click, Shift+click=Pick several rows",
                                        "Double-click a cell, Enter=Edit it", "Escape=Put an edited cell back"},
              autos.empty() ? "(none)" : autos.front());
        check("s016 json: keys round-trip, absent when none",
              core::form_from_json(core::form_to_json(f)).keys == f.keys &&
              !has(core::form_to_json(core::Form{"X", {"Window", "x", {}, {}, {}, {}}, {}, {}, {}, {}, {}}), "\"keys\""), "");
        { auto g = f; g.keys = {"no equals"};
          check("s016 validate: a row without Keys=What -> error", !errors_of(g).empty(), ""); }
        const auto sc = core::generate_shortcuts({f}, core::derive_app("Keys"));
        check("s016 shortcuts: automatic and own rows, written out (no accel)",
              sc.size() == 2 && has(sc[1].content, "{\"Notes\", \"\", \"Open a row\", \"Double-click, Enter\"}") &&
              has(sc[1].content, "{\"Notes\", \"\", \"Zoom\", \"Ctrl+scroll\"}") &&
              has(sc[1].content, ": Glib::ustring(*r.accel ? r.accel : r.keys)"), "");
    }

    // ── s016 m2: the menu names the undo step ──────────────────────────────
    {
        using core::Value;
        core::Form f;
        f.name = "MainWindow";
        f.actions = {{"undo", "<Control>z", false, "", "", "undo"}, {"hello", "", false}};
        core::Node mb{"MenuButton", "menu", {{"menu", Value(core::StringList{"Undo=win.undo", "-", "Hi=win.hello"})}}, {}, {}, {}};
        core::Node plain{"MenuButton", "other", {{"menu", Value(core::StringList{"Hi=win.hello"})}}, {}, {}, {}};
        core::Node box{"Box", "box", {}, {}, {}, {mb, plain}};
        f.root = {"Window", "main_window", {}, {}, {}, {box}};
        const auto o = core::generate_form(f, core::derive_app("Named"));
        check("s016 m2: a menu with an Undo row is built by fill_<id>_menu(), the row naming the step",
              has(o[0].content, "void fill_menu_menu();") && has(o[1].content, "    fill_menu_menu();") &&
              has(o[1].content, "section->append(undo::can_undo() ? \"Undo \" + undo::undo_label() : std::string(\"Undo\"), \"win.undo\");"), "");
        check("s016 m2: rebuilt at idle when the undo changes; a menu without one stays inline",
              has(o[1].content, "Glib::signal_idle().connect_once(sigc::mem_fun(*this, &MainWindowUi::fill_menu_menu));") &&
              !has(o[0].content, "fill_other_menu") && has(o[1].content, "#include <glibmm/main.h>"), "");
    }

    // ── s016 m3: a table's columns remembered ──────────────────────────────
    {
        using core::Value;
        core::Form f;
        f.name = "MainWindow";
        f.models = {{"Note", {{"title", "string", ""}, {"n", "int", ""}}}};
        core::Node cv{"ColumnView", "notes", {{"model", Value(std::string("Note"))}}, {}, {}, {}};
        f.root = {"Window", "main_window", {{"remember", Value(true)}}, {}, {}, {cv}};
        const auto o = core::generate_form(f, core::derive_app("Cols"));
        const std::string& c = o[1].content;
        check("s016 m3: each column has its field as id",
              has(c, "column->set_id(\"title\");") && has(c, "column->set_id(\"n\");"), "");
        check("s016 m3: restore -- order, widths, sort",
              has(c, "state::get_string(k + \"notes.order\")") && has(c, "m_notes.insert_column(at++, c);") &&
              has(c, "state::get_int(k + \"notes.title.width\")") &&
              has(c, "m_notes.sort_by_column(c, state::get_bool(k + \"notes.sort_desc\")"), "");
        check("s016 m3: save -- order, dragged widths, the sort",
              has(c, "state::put_string(k + \"notes.order\", order);") &&
              has(c, "std::dynamic_pointer_cast<Gtk::ColumnViewSorter>(m_notes.get_sorter())") &&
              has(c, "c->get_id().raw() == id") && has(c, "#include <gtkmm/columnviewsorter.h>"), "");
        { auto g = f; g.root.props.erase("remember");
          check("s016 m3: a form that doesn't remember says nothing of it",
                !has(core::generate_form(g, core::derive_app("Cols"))[1].content, "notes.order"), ""); }
    }

    // ── s022: promote to a class of yours ─────────────────────────────────
    {
        using core::Value;
        core::Form f;
        f.name = "MainWindow";
        core::Node chart{"Box", "chart", {}, {}, {}, {}};
        chart.cls = "MyChart";
        core::Node hint{"Label", "hint", {{"label", Value(std::string("hi"))}}, {}, {}, {}};
        f.root = {"Window", "main_window", {}, {}, {}, {core::Node{"Box", "body", {}, {}, {}, {chart, hint}}}};
        const std::string js = core::form_to_json(f);
        check("s022 json: \"class\" on disk, round-trips",
              has(js, "\"class\": \"MyChart\"") && core::form_from_json(js).root.children[0].children[0].cls == "MyChart" &&
              !has(js.substr(js.find("\"hint\"")), "\"class\""), "");
        { std::string e; for (const auto& i : core::validate(f)) if (i.error) e += i.where + ": " + i.what + "; ";
          check("s022 validate: a promoted Box is fine", e.empty(), e); }
        { auto g = f; g.root.cls = "Fancy";
          check("s022 validate: the window can't be promoted", any_error_has(g, "can't be promoted"), ""); }
        { auto g = f; g.root.children[0].children[0].cls = "Button";
          check("s022 validate: a catalog type is taken", any_error_has(g, "widgets::Button is the plain Button"), ""); }
        { auto g = f; g.root.children[0].children[0].cls = "Named";
          check("s022 validate: Named is taken", any_error_has(g, "Named"), ""); }
        { auto g = f; g.root.children[0].children[0].cls = "my chart";
          check("s022 validate: not a class name", any_error_has(g, "isn't a C++ class name"), ""); }
        { auto g = f; g.root.children[0].children[1].cls = "MyChart";
          check("s022 validate: one class, one base", any_error_has(g, "one class has one base"), ""); }

        const auto app = core::derive_app("Promo");
        const auto o = core::generate_form(f, app);
        const std::string& h = o[0].content;
        check("s022 codegen: the member is Named<widgets::MyChart>; its header included",
              has(h, "widgets::Named<widgets::MyChart> m_chart;") && has(h, "#include \"widgets/MyChart.hpp\"") &&
              has(h, "widgets::Label") && has(h, " m_hint;"), "");
        check("s022 codegen: built by name like any other", has(o[1].content, "m_chart(\"main_window.chart\")"), "");
        const auto st = core::custom_widget_stubs("MyChart", "Box", app);
        check("s022 stubs: a default-constructible Gtk::Box subclass in promo::widgets",
              st.size() == 2 && st[0].path == "include/widgets/MyChart.hpp" && st[1].path == "src/widgets/MyChart.cpp" &&
              has(st[0].content, "class MyChart : public Gtk::Box {") && has(st[0].content, "#include <gtkmm/box.h>") &&
              has(st[0].content, "namespace promo::widgets {") && has(st[1].content, "MyChart::MyChart() {"), "");

        // The verb: refused on the root and across bases; undo puts it back.
        core::Design d;
        core::DesignJournal j(d);
        d.load(f);
        std::string why;
        check("s022 promote: the root refused",
              !core::design::promote(j, d, d.root(), "Win", &why) && has(why, "can't be promoted"), why);
        check("s022 promote: another base refused",
              !core::design::promote(j, d, d.find("hint"), "MyChart", &why) && has(why, "one class has one base"), why);
        const bool p1 = core::design::promote(j, d, d.find("hint"), "Banner");
        check("s022 promote: a step; undo makes it plain again",
              p1 && d.to_form().root.children[0].children[1].cls == "Banner" && j.undo().done && d.to_form().root.children[0].children[1].cls.empty(), "");

        // The project: written once, CMake told, then let go.
        const fs::path tmp = fs::temp_directory_path() / ("mortimer_s022_" + std::to_string(::getpid()));
        fs::remove_all(tmp);
        fs::create_directories(tmp);
        std::string dir;
        try { dir = core::create_workspace(app, tmp.string()); } catch (const std::exception& e) { check("s022 project", false, e.what()); }
        if (!dir.empty()) {
            const fs::path ad(dir);
            const fs::path cm = ad / "CMakeLists.txt";
            auto slurp = [](const fs::path& p) { std::ifstream in(p); std::stringstream ss; ss << in.rdbuf(); return ss.str(); };
            check("s022 template: a new app's CMake globs src/widgets/", has(slurp(cm), "src/widgets/*.cpp"), "");
            // An older app: the line as it was before s022.
            { std::string t = slurp(cm);
              const auto a = t.find("file(GLOB APP_SOURCES"), b = t.find('\n', t.find("src/widgets/*.cpp"));
              t.replace(a, b - a, "file(GLOB APP_SOURCES CONFIGURE_DEPENDS \"${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp\")");
              std::ofstream(cm) << t; }
            auto mw = core::load_form((ad / "forms" / "MainWindow.mort").string());
            mw.root.children.back().children.push_back(chart);
            core::save_form(mw, (ad / "forms" / "MainWindow.mort").string());
            const auto g = core::generate_project(dir);
            const fs::path ch = ad / "include" / "widgets" / "MyChart.hpp";
            check("s022 generate: valid; MyChart's two files written, named as yours",
                  g.ok() && fs::exists(ch) && fs::exists(ad / "src" / "widgets" / "MyChart.cpp") &&
                  std::count(g.stubs.begin(), g.stubs.end(), "include/widgets/MyChart.hpp") == 1,
                  g.issues.empty() ? "" : g.issues.front().second.what);
            check("s022 generate: an older app's CMake gains src/widgets/*.cpp, once",
                  has(slurp(cm), "src/widgets/*.cpp") &&
                  std::any_of(g.notes.begin(), g.notes.end(), [](const std::string& n) { return has(n, "src/widgets/*.cpp added"); }), "");
            std::ofstream(ch, std::ios::app) << "// mine now\n";
            const auto g2 = core::generate_project(dir);
            check("s022 generate: then let go -- yours is never rewritten",
                  g2.stubs.empty() && has(slurp(ch), "// mine now") && g2.notes.empty(), g2.notes.empty() ? "" : g2.notes.front());
            // The same class, now a Label: the file says Gtk::Box -- a note, not an overwrite.
            mw.root.children.back().children.back().type = "Label";
            core::save_form(mw, (ad / "forms" / "MainWindow.mort").string());
            const auto g3 = core::generate_project(dir);
            check("s022 generate: a class whose file has another base -> a note",
                  std::any_of(g3.notes.begin(), g3.notes.end(), [](const std::string& n) { return has(n, "doesn't mention Gtk::Label"); }), "");
        }
        fs::remove_all(tmp);
    }

    // ── s022 m2: more containers ───────────────────────────────────────────
    {
        using core::Value;
        core::Form f;
        f.name = "MainWindow";
        core::Node body{"Box", "body", {}, {}, {}, {}};
        for (const char* t : {"Frame", "Expander", "Notebook", "Overlay", "CenterBox"})
            body.children.push_back(core::Node{t, std::string(t) == "CenterBox" ? "cb" : core::snake(t), {}, {}, {}, {}});
        f.root = {"Window", "main_window", {}, {}, {}, {body}};
        core::Design d;
        core::DesignJournal j(d);
        d.load(f);
        auto pack_of = [&](const core::Design::Id& k, const std::string& key) {
            const auto r = d.get(k);
            const auto it = r->pack.find(key);
            return it == r->pack.end() ? std::string("-") : std::get<std::string>(it->second);
        };
        const auto c1 = core::design::add(j, d, "Label", d.find("cb"));
        const auto c2 = core::design::add(j, d, "Label", d.find("cb"));
        const auto c3 = core::design::add(j, d, "Label", d.find("cb"));
        std::string why;
        const auto c4 = core::design::add(j, d, "Label", d.find("cb"), -1, &why);
        check("s022 m2 CenterBox: start, center, end, then full",
              pack_of(c1, "slot") == "start" && pack_of(c2, "slot") == "center" && pack_of(c3, "slot") == "end" &&
              c4.empty() && has(why, "start, center and end"), why);
        const auto o1 = core::design::add(j, d, "Label", d.find("overlay"));
        const auto o2 = core::design::add(j, d, "Button", d.find("overlay"));
        const auto o3 = core::design::add(j, d, "Button", d.find("overlay"));
        check("s022 m2 Overlay: the main child, then overlays",
              pack_of(o1, "slot") == "-" && pack_of(o2, "slot") == "overlay" && pack_of(o3, "slot") == "overlay", "");
        const auto n1 = core::design::add(j, d, "Label", d.find("notebook"));
        const auto n2 = core::design::add(j, d, "Label", d.find("notebook"));
        check("s022 m2 Notebook: pages titled Page 1, Page 2",
              pack_of(n1, "title") == "Page 1" && pack_of(n2, "title") == "Page 2", "");
        const auto f1 = core::design::add(j, d, "Label", d.find("frame"));
        const auto f2 = core::design::add(j, d, "Label", d.find("frame"), -1, &why);
        check("s022 m2 Frame: one child", !f1.empty() && f2.empty() && has(why, "ONE child"), why);
        const core::Form g = d.to_form();
        check("s022 m2: all valid", !core::has_errors(core::validate(g)), "");
        const auto o = core::generate_form(g, core::derive_app("Boxes"));
        const std::string& c = o[1].content;
        check("s022 m2 codegen: set_start/center/end_widget, add_overlay, append_page(title), set_child",
              has(c, "m_cb.set_start_widget(m_label") && has(c, "m_cb.set_center_widget(") && has(c, "m_cb.set_end_widget(") &&
              has(c, "m_overlay.add_overlay(") && has(c, "m_overlay.set_child(") &&
              has(c, "m_notebook.append_page(") && has(c, "\"Page 2\");") && has(c, "m_frame.set_child("), "");
        { auto h = g; h.root.children[0].children[4].children[0].pack["slot"] = Value(std::string("middle"));
          check("s022 m2 validate: a CenterBox slot it doesn't have", any_error_has(h, "\"start\", \"center\" or \"end\""), ""); }
        { auto h = g; h.root.children[0].children[4].children[1].pack["slot"] = Value(std::string("start"));
          check("s022 m2 validate: two starts", any_error_has(h, "one start, one center"), ""); }
        { auto h = g; h.root.children[0].children[3].children[1].pack.erase("slot");
          check("s022 m2 validate: an Overlay has ONE main child", any_error_has(h, "holds ONE child"), ""); }
    }

    // ── s022 m3: more controls ─────────────────────────────────────────────
    {
        using core::Value;
        core::Form f;
        f.name = "MainWindow";
        core::Node tv{"TextView", "body_text", {{"text", Value(std::string("a\nb"))},
                                                {"wrap_mode", Value(std::string("word_char"))}}, {}, {{"changed"}}, {}};
        core::Node lb{"LinkButton", "site", {{"uri", Value(std::string("https://gtk.org"))}}, {}, {{"activate_link"}}, {}};
        core::Node se{"SearchEntry", "find", {}, {}, {{"search_changed"}}, {}};
        core::Node box{"Box", "body", {}, {}, {}, {tv, lb, se, core::Node{"Separator", "rule", {}, {}, {}, {}},
                                                core::Node{"ProgressBar", "bar", {{"fraction", Value(0.25)}}, {}, {}, {}}}};
        f.root = {"Window", "main_window", {}, {}, {}, {box}};
        check("s022 m3: valid", !core::has_errors(core::validate(f)), "");
        const auto o = core::generate_form(f, core::derive_app("Ctl"));
        const std::string& c = o[1].content;
        check("s022 m3 TextView: text into its buffer, changed from the buffer",
              has(c, "m_body_text.get_buffer()->set_text(\"a\\nb\");") &&
              has(c, "m_body_text.set_wrap_mode(Gtk::WrapMode::WORD_CHAR);") &&
              has(c, "m_body_text.get_buffer()->signal_changed().connect("), "");
        check("s022 m3 LinkButton: activate_link connected BEFORE GTK opens the uri, returns false unhooked",
              has(c, "m_site.signal_activate_link().connect(") && has(c, ", false));") &&
              has(o[0].content, "virtual bool on_site_activate_link();"), "");
        check("s022 m3 SearchEntry / ProgressBar",
              has(c, "m_find.signal_search_changed().connect(") && has(c, "m_bar.set_fraction(0.25);"), "");
    }

    // ── s022 m4: the pickers ───────────────────────────────────────────────
    {
        using core::Value;
        core::Form f;
        f.name = "MainWindow";
        core::Node cb{"ColorDialogButton", "ink", {{"rgba", Value(std::string("#ff0000"))},
                                                   {"with_alpha", Value(false)}}, {}, {{"color_changed"}}, {}};
        core::Node fb{"FontDialogButton", "face", {{"font", Value(std::string("Sans 12"))}}, {}, {}, {}};
        f.root = {"Window", "main_window", {}, {}, {}, {core::Node{"Box", "body", {}, {}, {}, {cb, fb}}}};
        const auto o = core::generate_form(f, core::derive_app("Pick"));
        const std::string& c = o[1].content;
        const auto dlg = c.find("m_ink.set_dialog(Gtk::ColorDialog::create());");
        check("s022 m4: the dialog made first, then its props",
              dlg != std::string::npos && dlg < c.find("m_ink.get_dialog()->set_with_alpha(false);") &&
              has(c, "m_ink.set_rgba(Gdk::RGBA(\"#ff0000\"));") &&
              has(c, "m_face.set_dialog(Gtk::FontDialog::create());") &&
              has(c, "m_face.set_font_desc(Pango::FontDescription(\"Sans 12\"));"), "");
        check("s022 m4: color_changed from the rgba property",
              has(c, "m_ink.property_rgba().signal_changed().connect("), "");
        const auto sup = core::generate_support(core::derive_app("Pick"));
        check("s022 m4: Widgets.hpp has the dialog headers",
              has(sup[0].content, "#include <gtkmm/colordialog.h>") && has(sup[0].content, "#include <pangomm/fontdescription.h>"), "");
    }

    // ── s022 m5: the template App deletes its window when it's closed ──────
    {
        std::string app_cpp;
        for (const auto& t : core::project_templates())
            if (std::string(t.path) == "@BIN@/src/App.cpp") app_cpp = t.content;
        const auto hoc = app_cpp.find("win->set_hide_on_close(true);");
        check("s022 m5 template App: hide on close, THEN delete on hide (GTK 4 destroys a closed window without a hide)",
              hoc != std::string::npos && hoc < app_cpp.find("win->signal_hide().connect([win] { delete win; });"), "");
    }

    // ── s024: the undo gate -- the app's undo, walked on every build ────────
    {
        core::Form f;
        f.name = "MainWindow";
        f.models = {{"Note", {{"title", "string", ""}, {"n", "int", ""}, {"w", "double", ""}, {"done", "bool", ""}}}};
        f.root = {"Window", "main_window", {}, {}, {}, {}};
        core::Form bare;
        bare.name = "Plain";
        bare.root = {"Window", "plain", {}, {}, {}, {}};
        const auto app = core::derive_app("Gater");
        const auto o = core::generate_form(f, app);
        check("s024: a form's items in generated/<Form>Items.cpp, apart from the form",
              o.size() == 3 && o[2].path == "generated/MainWindowItems.cpp" &&
              has(o[2].content, "MainWindowUi::NoteItem::create(") && !has(o[1].content, "NoteItem::create("), "");
        check("s024: a form with no models has no Items file", core::generate_form(bare, app).size() == 2, "");
        const std::vector<core::ModelDef> shared = {{"Task", {{"label", "string", ""}}}};
        const auto g = core::generate_undo_gate({f, bare}, shared, app);
        const std::string& c = g.at(0).content;
        check("s024: generated/UndoGate.cpp, empty but for the gate target",
              g[0].path == "generated/UndoGate.cpp" && has(c, "#ifdef MORTIMER_UNDO_GATE") &&
              has(c, "#endif  // MORTIMER_UNDO_GATE") && has(c, core::kGeneratedMarker), "");
        check("s024: walks the gate's own type, each form's models, the shared ones",
              has(c, "walk<::gater::ui::gate::GateItem>(") && has(c, "walk<::gater::ui::MainWindowUi::NoteItem>(") &&
              has(c, "walk<::gater::model::TaskItem>(") && has(c, "#include \"generated/MainWindowUi.hpp\"") &&
              has(c, "#include \"generated/Models.hpp\"") && !has(c, "PlainUi.hpp"), "");
        check("s024: each field changed alone, to another value",
              has(c, "x.title = \"gate: \\\"two\\\", line\\nnext\";") &&
              has(c, "x.n = -42;") && has(c, "x.w = 0.25;") && has(c, "x.done = false;") &&
              has(c, "::gater::ui::MainWindowUi::NoteItem::create(\"gate one\", 7, 2.5, true)"), "");
        check("s024: add / each field / remove, undo and redo; the removed row destroyed first",
              has(c, "undo::add(list, type, id, 0, \"Add\")") && has(c, "undo::remove(list, type, id, \"Remove\")") &&
              has(c, "a.reset();") && has(c, "!Item::find(id)") && has(c, "undo::unregister_list(list);") &&
              has(c, "typing folds into one step"), "");
        check("s024: the gate's own type is made by the item generator", has(c, "class GateItem : public Glib::Object {") &&
              has(c, "return \"gate::GateItem\";"), "");
        std::string cm, bs;
        for (const auto& t : core::project_templates()) {
            if (std::string(t.path) == "@BIN@/CMakeLists.txt") cm = t.content;
            if (std::string(t.path) == "@BIN@/build.sh") bs = t.content;
        }
        check("s024 template: the gate target, defined MORTIMER_UNDO_GATE, run by the build (stamp only on a pass)",
              has(cm, "add_executable(@BIN@_undo_gate generated/UndoGate.cpp generated/Undo.cpp src/Log.cpp ${GATE_SOURCES})") &&
              has(cm, "target_compile_definitions(@BIN@_undo_gate PRIVATE MORTIMER_UNDO_GATE)") &&
              has(cm, "COMMAND @BIN@_undo_gate") && has(cm, "add_custom_target(undo_gate ALL") &&
              has(cm, "generated/*Items.cpp"), "");
        check("s024 template: build.sh says the gate passed", has(bs, "_undo_gate -v"), "");
    }

    // ── s024 m2: the build regenerates when a form is newer ────────────────
    {
        std::string cm;
        for (const auto& t : core::project_templates())
            if (std::string(t.path) == "@BIN@/CMakeLists.txt") cm = t.content;
        const auto run = cm.find("execute_process(COMMAND \"${MORTIMER_GEN}\" generate \"${CMAKE_CURRENT_SOURCE_DIR}\"");
        check("s024 m2 template: forms and mortimer.json re-run CMake; it runs generate BEFORE the globs",
              has(cm, "set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${FORM_FILES} \"${CMAKE_CURRENT_SOURCE_DIR}/mortimer.json\")") &&
              run != std::string::npos && run < cm.find("file(GLOB CORE_SOURCES"), "");
        check("s024 m2 template: a form with errors stops it; no mortimer-gen -> generated/ as it is; can be turned off",
              has(cm, "message(FATAL_ERROR \"mortimer-gen generate: the forms have errors") &&
              has(cm, "mortimer-gen not found: generated/ is used as it is") &&
              has(cm, "option(@BIN_UPPER@_REGENERATE"), "");
    }
}
