#pragma once
// core::Project -- an app Mortimer owns.
//
// Mortimer doesn't drop forms into someone else's app: it MAKES the app. A new
// project is the whole workspace, laid out the house way --
//
//   <bin>/                    the workspace root
//     backups/  backups/Old/  session zips (scripts/<bin>_backup.sh)
//     docs/                   RULES, CANON, HANDOFF, ARCHITECTURE, ARC, PROGRESS
//     fixes/                  yours alone; never shipped
//     scripts/<bin>_backup.sh
//     <bin>/                  the repo
//       mortimer.json         this file: name, id, namespace, the forms
//       forms/<Form>.mort     the designs (JSON)
//       generated/            Mortimer's half -- rewritten, never edited
//       include/ src/         your half -- written once, then yours
//       resources/ data/      icons compiled in; .desktop + app icon installed
//       CMakeLists.txt build.sh install.sh uninstall.sh README.md .gitignore
//
// GTK-FREE: files and strings only. The CLI (mortimer-gen) and, later, the
// designer window both drive this one API.
#include "core/Codegen.hpp"
#include "core/HookScan.hpp"

#include <string>
#include <vector>

namespace mortimer::core {

struct Project {
    AppInfo app;
    std::string main_form = "MainWindow";
    std::vector<std::string> forms;     // stems: forms/<stem>.mort
};

Project load_project(const std::string& app_dir);   // reads <app_dir>/mortimer.json
void save_project(const Project& p, const std::string& app_dir);

// Name -> the derived names. "Notes" -> bin "notes", ns "notes",
// id "<id_prefix>.Notes". Throws if the name can't make a C++ namespace.
AppInfo derive_app(const std::string& name, const std::string& id_prefix = "io.github.scott8420",
                   const std::string& app_id = "");

// What one `generate` did.
struct GenerateResult {
    std::vector<std::string> written;     // changed or new (relative to app dir)
    std::vector<std::string> unchanged;
    std::vector<std::string> pruned;      // generated/ files no form makes any more
    std::vector<std::string> stubs;       // your half, written for a NEW form
    std::vector<std::string> notes;       // human lines: skipped stubs, etc.
    std::vector<std::pair<std::string, Issue>> issues;   // form -> issue
    std::vector<HookReport> reports;
    bool ok() const;                       // no errors
};

// Regenerate every form of the project in `app_dir`. Writes only what changed
// (so a rebuild recompiles only what the form change touched). A form with
// validation errors is skipped -- its old generated files are left in place.
GenerateResult generate_project(const std::string& app_dir);

// The hooks report alone (reads, writes nothing).
std::vector<HookReport> hook_reports(const std::string& app_dir);

// New workspace at <parent>/<bin>/ -- refuses if it exists. Returns the app dir.
std::string create_workspace(const AppInfo& app, const std::string& parent);

// ── s002: what the designer window needs ──────────────────────────────────
// The app a form file belongs to: <app>/forms/<Stem>.mort with
// <app>/mortimer.json beside forms/ and the stem in its forms list. "" if not.
std::string project_of(const std::string& form_path);

// A new form in the project: forms/<name>.mort (a Window with an empty Box),
// listed in mortimer.json. Throws with a readable message if the name isn't a
// class name or the form exists. Returns the form's path. Doesn't generate.
std::string add_form(const std::string& app_dir, const std::string& name,
                     const std::string& root_type = "Window");   // or "Dialog" (s004)

// The hooks report for a form as it is NOW (in memory, unsaved), against the
// user's header and the last manifest on disk -- what the window's hooks panel
// shows live. Reads, writes nothing.
HookReport live_report(const std::string& app_dir, const Form& f);

// The starter form a new project gets: a window, a header bar, a little body.
Form starter_form(const AppInfo& app);
// s010 -- the About dialog a new project gets (forms/About.mort): the logo, the
// name, the version, a Close button on GTK's window.close. The starter's
// "about" action opens it.
Form about_form(const AppInfo& app);
// s010 -- add forms/About.mort to an existing project, listed, with your half
// written (its constructor shows core::version()). Throws if it has one. Give
// an action `"opens": "About"` to show it. Returns the form's path.
std::string add_about(const std::string& app_dir);

// Template rendering (the embedded project template, @TOKEN@ substitution).
std::string render_tokens(const std::string& text, const AppInfo& app);

}  // namespace mortimer::core
