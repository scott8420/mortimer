#pragma once
// core::Codegen -- a Form in, C++ out. The Qt split, done the gtkmm way:
//
//   generated/<Form>Ui.hpp/.cpp   OURS. Rewritten whenever the form changes.
//       class <Form>Ui : public Gtk::ApplicationWindow (or Window)
//         - every widget a Named member, m_<id>, built + packed in build_ui();
//         - every ticked signal wired in bind_hooks() to a VIRTUAL hook,
//           on_<id>_<signal>(), whose default body logs "unhooked" once;
//         - a REQUIRED signal's hook is pure virtual (= 0).
//   include/<Form>.hpp + src/<Form>.cpp   YOURS. Written once, never again.
//       class <Form> : public ui::<Form>Ui -- override the hooks you want.
//
// So regenerating can never touch your code, and the three ways a hook can be
// wrong all show: unhooked -> a warning when it fires (and the hooks report);
// required but missing -> your class won't compile; removed from the form but
// still overridden -> `override` is a compile error. No silently dead buttons.
//
// GTK-FREE: this writes strings. The s001 proof compiles what it writes.
#include "core/Form.hpp"

#include <string>
#include <vector>

namespace mortimer::core {

// What the generated code needs to know about the app it lives in.
struct AppInfo {
    std::string name;     // "Notes"            -- shown to people
    std::string bin;      // "notes"            -- binary, folder, file stems
    std::string app_id;   // "io.github.scott8420.Notes"
    std::string ns;       // "notes"            -- the C++ namespace
};

struct OutFile {
    std::string path;     // relative to the app folder: "generated/MainWindowUi.hpp"
    std::string content;
    bool executable = false;
};

// One hook = one ticked signal, as both halves see it.
struct Hook {
    std::string name;        // on_save_clicked
    std::string ret;         // void | bool
    std::string params;      // "Gtk::ListBoxRow* row"
    std::string ret_default; // what the unhooked default returns
    std::string widget;      // save
    std::string type;        // Button
    std::string signal;      // clicked
    bool        required = false;
    std::string decl() const;   // "void on_save_clicked()"
};

// The line every file Mortimer owns starts with -- and the marker `generate`
// looks for before it prunes a stale file from generated/.
extern const char* const kGeneratedMarker;

std::vector<Hook> hooks_of(const Form& f);

// generated/<Form>Ui.hpp + .cpp. Precondition: validate(f) has no errors.
std::vector<OutFile> generate_form(const Form& f, const AppInfo& app);
// s013 -- generated/Models.hpp/.cpp: the project's shared models (namespace
// <ns>::model), one item class each. Nothing when there are none.
std::vector<OutFile> generate_models(const std::vector<ModelDef>& models, const AppInfo& app);
// s010 -- generated/Shortcuts.hpp/.cpp: the keyboard-shortcuts window, a row
// per key any of `forms` gives an action (in that order; the main form first).
std::vector<OutFile> generate_shortcuts(const std::vector<Form>& forms, const AppInfo& app);
// s024 -- generated/UndoGate.cpp: <bin>_undo_gate, which walks every row type
// (the forms' own models, the shared ones, one of its own) through the app's
// undo -- add, each field alone, remove; undo, redo. The build runs it.
std::vector<OutFile> generate_undo_gate(const std::vector<Form>& forms, const std::vector<ModelDef>& shared,
                                        const AppInfo& app);
// generated/Widgets.hpp, Hooks.hpp/.cpp, README.md -- shared by every form.
std::vector<OutFile> generate_support(const AppInfo& app);
// include/<Form>.hpp + src/<Form>.cpp -- the one-time stubs, one override per
// hook the form has today.
std::vector<OutFile> user_stubs(const Form& f, const AppInfo& app);

// s022 -- promoted widgets. The classes a form names, each with the type of the
// first widget that names it (its base): "MyChart" -> "Box".
std::vector<std::pair<std::string, std::string>> promoted_classes(const Form& f);
// The two files of a promoted class -- include/widgets/<Cls>.hpp and
// src/widgets/<Cls>.cpp -- a default-constructible subclass of the type's gtkmm
// class, in <ns>::widgets. Written ONCE by the project, then yours.
std::vector<OutFile> custom_widget_stubs(const std::string& cls, const std::string& type, const AppInfo& app);

// The pieces a hooks report offers to paste, for one hook.
std::string override_decl(const Hook& h);                 // "    void on_x() override;"
std::string override_def(const Hook& h, const std::string& cls);  // the empty body

// C++ literal for a string (escaped, UTF-8 kept as is).
std::string cpp_string(const std::string& s);

}  // namespace mortimer::core
