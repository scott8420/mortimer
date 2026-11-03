// mortimer-gen -- Mortimer's generator, headless. The designer window (s002+)
// drives the same core API; this is that API from a terminal, so a form can be
// written by hand and generated today, and the selftest/proof can drive it.
//
//   mortimer-gen new <Name> [--id APP_ID] [--in DIR]   a new app's workspace at DIR/<bin>/
//   mortimer-gen generate [APP_DIR]                    regenerate every form (default .)
//   mortimer-gen hooks [APP_DIR]                       the hooks report, nothing written
//   mortimer-gen check <form.mort>                     validate one form
//   mortimer-gen catalog [Type]                        what can be placed, and how
#include "core/Catalog.hpp"
#include "core/Project.hpp"
#include "core/Resources.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace core = mortimer::core;
namespace fs = std::filesystem;

namespace {

int usage() {
    std::cerr <<
        "mortimer-gen -- the Mortimer generator\n"
        "  mortimer-gen new <Name> [--id APP_ID] [--in DIR]   new app workspace at DIR/<bin>/\n"
        "  mortimer-gen generate [APP_DIR]                    regenerate every form\n"
        "  mortimer-gen hooks [APP_DIR]                       hooked / unhooked / missing / stale\n"
        "  mortimer-gen check <form.mort>                     validate one form\n"
        "  mortimer-gen catalog [Type]                        widgets, properties, signals\n"
        "  mortimer-gen resources [APP_DIR]                   the bundle: kind, file, what code names\n"
        "  mortimer-gen add icon|image|data FILE [APP_DIR]    copy into resources/ and bundle it\n"
        "  mortimer-gen add stylesheet [APP_DIR]              resources/<bin>.css, loaded for every window\n"
        "  mortimer-gen add about [APP_DIR]                   forms/About.mort, an About dialog (+ your half)\n"
        "  mortimer-gen remove resource FILE [APP_DIR]        unbundle; the file moves to removed/\n"
        "  mortimer-gen remove form STEM [APP_DIR]            the form + your halves move to removed/\n"
        "  mortimer-gen rename form OLD NEW [APP_DIR]         the form, your halves (class inside), the forms that open it\n"
        "  mortimer-gen rename resource FILE NAME [APP_DIR]   the bundled file; forms naming the icon follow\n"
        "  mortimer-gen share FORM MODEL [APP_DIR]            the form's model becomes the project's (shared)\n"
        "  mortimer-gen unshare MODEL [APP_DIR]               every form that shows it gets its own copy back\n"
        "  mortimer-gen models [APP_DIR]                      the project's shared models\n";
    return 2;
}

void print_issues(const std::string& form, const std::vector<core::Issue>& issues) {
    for (const auto& i : issues)
        std::cout << (i.error ? "  error  " : "  warn   ") << form << ": " << i.where << ": " << i.what
                  << "\n";
}

void print_hooks(const std::vector<core::HookReport>& reps) {
    for (const auto& r : reps) {
        int hooked = 0;
        for (const auto& h : r.hooks) hooked += h.hooked;
        std::cout << "  " << r.form << ": " << r.hooks.size() << " hook(s), " << hooked << " hooked, "
                  << r.unhooked_count() << " unhooked";
        if (!r.missing.empty()) std::cout << ", " << r.missing.size() << " REQUIRED missing";
        if (!r.stale.empty()) std::cout << ", " << r.stale.size() << " STALE";
        std::cout << "\n";
        for (const auto& h : r.hooks)
            if (!h.hooked)
                std::cout << "      " << (h.hook.required ? "MISSING   " : "unhooked  ") << h.hook.decl()
                          << "\n";
        for (const auto& s : r.stale) std::cout << "      STALE     " << s << "  (override won't compile)\n";
    }
}

int cmd_generate(const std::string& dir) {
    auto r = core::generate_project(dir);
    for (const auto& [form, i] : r.issues)
        std::cout << (i.error ? "  error  " : "  warn   ") << form << ": " << i.where << ": " << i.what << "\n";
    for (const auto& f : r.written) std::cout << "  wrote      " << f << "\n";
    for (const auto& f : r.stubs) std::cout << "  your half  " << f << "  (written once -- yours now)\n";
    for (const auto& f : r.pruned) std::cout << "  removed    " << f << "\n";
    for (const auto& n : r.notes) std::cout << "  note       " << n << "\n";
    std::cout << "  " << r.unchanged.size() << " file(s) unchanged\n";
    print_hooks(r.reports);
    return r.ok() ? 0 : 1;
}

int cmd_catalog(const std::string& only) {
    for (const auto& w : core::catalog()) {
        if (!only.empty() && w.type != only) continue;
        std::cout << w.type << "  (" << w.gtk_class << ")" << (w.root ? "  [form root]" : "") << "\n"
                  << "    " << w.doc << "\n";
        if (w.children != core::ChildKind::None) {
            std::cout << "    children: " << core::child_kind_name(w.children);
            auto keys = core::packing_keys(w.children);
            if (!keys.empty()) {
                std::cout << "  (child \"pack\":";
                for (const auto& k : keys) std::cout << " " << k;
                std::cout << ")";
            }
            std::cout << "\n";
        }
        for (const auto& p : w.props) {
            std::cout << "    prop    " << p.name << " : " << core::kind_name(p.kind);
            if (!p.values.empty()) {
                std::cout << " {";
                for (std::size_t i = 0; i < p.values.size(); ++i) std::cout << (i ? "|" : "") << p.values[i].first;
                std::cout << "}";
            }
            if (!p.ref_type.empty()) std::cout << " -> a " << p.ref_type;
            if (!p.doc.empty()) std::cout << "  -- " << p.doc;
            std::cout << "\n";
        }
        for (const auto& s : w.signals)
            std::cout << "    signal  " << s.name << "  -> " << s.ret << " on_<id>_" << s.name << "(" << s.params
                      << ")  -- " << s.doc << "\n";
        if (!only.empty()) {
            std::cout << "  common props (every widget):\n";
            for (const auto& p : core::common_props())
                std::cout << "    prop    " << p.name << " : " << core::kind_name(p.kind) << "\n";
        }
    }
    if (only.empty()) {
        std::cout << "common props (every widget):";
        for (const auto& p : core::common_props()) std::cout << " " << p.name;
        std::cout << "\n";
    } else if (!core::find_widget(only)) {
        std::cerr << "no widget type \"" << only << "\"\n";
        return 1;
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> a(argv + 1, argv + argc);
    if (a.empty()) return usage();
    const std::string cmd = a[0];
    try {
        if (cmd == "new") {
            if (a.size() < 2) return usage();
            std::string id, in = ".";
            for (std::size_t i = 2; i < a.size(); ++i) {
                if (a[i] == "--id" && i + 1 < a.size()) id = a[++i];
                else if (a[i] == "--in" && i + 1 < a.size()) in = a[++i];
                else return usage();
            }
            const auto app = core::derive_app(a[1], "io.github.scott8420", id);
            const auto dir = core::create_workspace(app, in);
            std::cout << "Made " << app.name << " (" << app.app_id << ")\n"
                      << "  workspace  " << fs::path(dir).parent_path().string() << "\n"
                      << "  app        " << dir << "\n\n"
                      << "  cd " << dir << " && ./build.sh && ./build/" << app.bin << "\n";
            return 0;
        }
        if (cmd == "generate") return cmd_generate(a.size() > 1 ? a[1] : ".");
        if (cmd == "hooks") {
            print_hooks(core::hook_reports(a.size() > 1 ? a[1] : "."));
            return 0;
        }
        if (cmd == "check") {
            if (a.size() < 2) return usage();
            auto f = core::load_form(a[1]);
            // s013: in a project, its shared models are the form's to show too.
            if (const auto app = core::project_of(a[1]); !app.empty()) f.shared = core::load_project(app).models;
            const auto issues = core::validate(f);
            print_issues(f.name, issues);
            std::cout << (core::has_errors(issues) ? "  not ok\n" : "  ok\n");
            return core::has_errors(issues) ? 1 : 0;
        }
        if (cmd == "catalog") return cmd_catalog(a.size() > 1 ? a[1] : "");
        // s013: shared models.
        if (cmd == "share" && a.size() >= 3) {
            const std::string dir = a.size() > 3 ? a[3] : ".";
            const auto changed = core::share_model(dir, a[1], a[2]);
            std::cout << a[2] << " is the project's now (mortimer.json); out of:";
            for (const auto& c : changed) std::cout << " " << c;
            std::cout << "\n";
            return cmd_generate(dir);
        }
        if (cmd == "unshare" && a.size() >= 2) {   // s013 m3
            const std::string dir = a.size() > 2 ? a[2] : ".";
            const auto users = core::unshare_model(dir, a[1]);
            if (users.empty()) std::cout << a[1] << " is gone -- no form showed it\n";
            else {
                std::cout << a[1] << " is each form's own again:";
                for (const auto& u : users) std::cout << " " << u;
                std::cout << "\n";
            }
            return cmd_generate(dir);
        }
        if (cmd == "models") {
            const auto pr = core::load_project(a.size() > 1 ? a[1] : ".");
            if (pr.models.empty()) std::cout << "  (no shared models -- mortimer-gen share FORM MODEL)\n";
            for (const auto& m : pr.models) {
                std::cout << "  " << m.name << " (" << core::model_item(m) << "):";
                for (const auto& x : m.fields) std::cout << " " << x.name << ":" << x.type;
                std::cout << "\n";
            }
            return 0;
        }
        // s009: the bundle and the forms, as the navigator's + and delete do them.
        if (cmd == "resources") {
            for (const auto& r : core::list_resources(a.size() > 1 ? a[1] : "."))
                std::cout << "  " << r.kind << "  " << r.file << "  -> " << r.use << "\n";
            return 0;
        }
        if (cmd == "add" && a.size() >= 2 && a[1] == "about") {
            const std::string dir = a.size() > 2 ? a[2] : ".";
            const auto path = core::add_about(dir);
            std::cout << "Added " << path << " -- give an action \"opens\": \"About\" to show it\n";
            return cmd_generate(dir);
        }
        if (cmd == "add" && a.size() >= 2 && a[1] == "stylesheet") {
            const auto r = core::add_stylesheet(a.size() > 2 ? a[2] : ".");
            std::cout << "added resources/" << r.file << " (" << r.use << ")\n";
            return 0;
        }
        if (cmd == "add" && a.size() >= 3) {
            const auto r = core::add_resource(a.size() > 3 ? a[3] : ".", a[1], a[2]);
            std::cout << "added resources/" << r.file << "  -> " << r.use << "\n";
            return 0;
        }
        if (cmd == "remove" && a.size() >= 3 && (a[1] == "resource" || a[1] == "form")) {
            const std::string dir = a.size() > 3 ? a[3] : ".";
            const std::string went = a[1] == "form" ? core::remove_form(dir, a[2]) : core::remove_resource(dir, a[2]);
            std::cout << "moved to " << went << (a[1] == "form" ? " -- run generate to prune its generated files" : "") << "\n";
            return 0;
        }
        if (cmd == "rename" && a.size() >= 4 && a[1] == "resource") {   // s021 m2
            const std::string dir = a.size() > 4 ? a[4] : ".";
            const auto r = core::rename_resource(dir, a[2], a[3]);
            std::cout << "renamed " << a[2] << " -> " << r.now.file << "\n  code names it  " << r.now.use << "  (was " << r.was << ")\n";
            for (const auto& f : r.forms) std::cout << "  followed      forms/" << f << ".mort\n";
            for (const auto& f : r.mentions) std::cout << "  still names the old one (a string; yours)  " << f << "\n";
            if (!r.forms.empty()) std::cout << "run generate to carry the forms\n";
            return 0;
        }
        if (cmd == "rename" && a.size() >= 4 && a[1] == "form") {   // s021
            const std::string dir = a.size() > 4 ? a[4] : ".";
            const auto r = core::rename_project_form(dir, a[2], a[3]);
            std::cout << "renamed " << a[2] << " -> " << a[3] << "\n";
            for (const auto& f : r.edited) std::cout << "  yours      " << f << "\n";
            for (const auto& f : r.forms) std::cout << "  opens it   forms/" << f << ".mort\n";
            if (!r.kept.empty()) std::cout << "  originals  " << r.kept << "/\n";
            std::cout << "run generate to write " << a[3] << "Ui and prune " << a[2] << "Ui\n";
            return 0;
        }
    } catch (const std::exception& e) {
        std::cerr << "mortimer-gen: " << e.what() << "\n";
        return 1;
    }
    return usage();
}
