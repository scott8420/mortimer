#pragma once
// core::Form -- one window's design: a tree of widget nodes, each with an id,
// properties, ticked signals and (for a child) how it sits in its parent.
//
// On disk it is JSON, `forms/<Name>.mort`, small enough to read and diff by eye:
//
//   { "mortimer": 1, "name": "MainWindow",
//     "root": { "type": "ApplicationWindow", "id": "main_window",
//               "props": { "title": "Notes" },
//               "signals": [ { "signal": "close_request" } ],
//               "children": [ { "type": "Box", "id": "body", ... } ] } }
//
// A node's `id` is the C++ name: member m_<id>, hook on_<id>_<signal>, registry
// name "<root id>.<id>". The form's own window is the root; it has no member
// (it IS the generated class).
//
// GTK-FREE (the seam). Values are kept as JSON-shaped Value variants so the
// model round-trips exactly what it read; the catalog gives them meaning.
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace mortimer::core {

using StringList = std::vector<std::string>;
using Value = std::variant<bool, std::int64_t, double, std::string, StringList>;

struct SignalHook {
    std::string signal;          // catalog signal name: "clicked"
    bool        required = false;// pure virtual: the class won't compile until it's written
    std::string handler;         // override of the hook name; "" = on_<id>_<signal>
    bool operator==(const SignalHook&) const = default;   // s002: the journal compares records
};

struct Node {
    std::string type;                       // catalog type: "Button"
    std::string id;                         // C++ identifier, unique in the form
    std::map<std::string, Value> props;     // catalog props, sorted by key on disk
    std::map<std::string, Value> pack;      // how it sits in its parent (slot, column, ...)
    std::vector<SignalHook> signals;
    std::vector<Node> children;
};

// s003 -- an ACTION the window offers: a Gio action "win.<gio name>" (the
// name with '_' as '-', GIO's spelling), a hook on_action_<name>() that runs
// it, and optionally a key. Menus name it ("Save=win.save"); a Button can name
// it too (action_name). One fact, three uses -- the key and the menu row can't
// drift from the action, because there is only the one entry.
//
// s010 -- an action can OPEN a window instead of running a hook: `opens` names
// another form of the project (an About dialog, Preferences) or "@shortcuts"
// (the keyboard-shortcuts window, generated from every form's keys). The
// generated code builds that window the first time, keeps it (it hides on
// close), and presents it over this one every time after -- no hook, no code.
// `title` is the row the shortcuts window shows for the key; "" = the label of
// the menu row that runs it, else made from the name ("save_as" -> "Save as").
struct ActionDef {
    std::string name;            // C++ identifier: "save_as" -> win.save-as, on_action_save_as()
    std::string accel;           // "<Control>s" (GTK syntax); "" = no key
    bool        required = false;// the hook is pure virtual
    std::string title;           // s010: the shortcuts window's row; "" = derived
    std::string opens;           // s010: a form stem or "@shortcuts"; "" = a hook
    bool operator==(const ActionDef&) const = default;
};
// s010 -- the shortcuts window's name as an `opens` target.
inline const char* const kShortcutsTarget = "@shortcuts";

// s005 -- a FILE DIALOG the form can ask: Gtk::FileDialog, generated async and
// hooked. choose_<name>(initial) opens it over the window; the answer arrives
// at on_<name>_chosen(path) (open_multiple: paths); Cancel / Escape arrive at
// on_<name>_cancelled() when `cancel` is ticked, else quietly nowhere. The
// window closing while it's up is handled (the answer is dropped, never
// delivered to a dead object); asking again while it's up is refused.
struct FileDef {
    std::string name;              // C++ identifier: "open_doc" -> choose_open_doc(), on_open_doc_chosen()
    std::string mode = "open";     // open | open_multiple | save | folder
    std::string title;             // "" = GTK's own ("Open File" ...)
    std::string accept;            // the accept button's label; "" = GTK's own
    StringList  filters;           // "Text=*.txt;*.md", "Images=image/*" (a '/' = a MIME type)
    bool        cancel = false;    // also a hook on_<name>_cancelled()
    bool        required = false;  // on_<name>_chosen is pure virtual
    bool operator==(const FileDef&) const = default;
};

// s006 -- a MODEL: the row type of a list. "Note" becomes <Form>Ui::NoteItem,
// a Glib::Object with one plain C++ member per field and an id(): automatic
// (a counter) or yours (a database key) -- unique per type, a taken one refused.
// A ColumnView names a model ("model": "Note") and shows a column per field.
struct FieldDef {
    std::string name;              // C++ member: "title"
    std::string type = "string";   // string | int | double | bool
    std::string title;             // the column header; "" = from the name ("due_date" -> "Due date")
    bool operator==(const FieldDef&) const = default;
};
struct ModelDef {
    std::string           name;    // "Note" -> NoteItem
    std::vector<FieldDef> fields;
    bool operator==(const ModelDef&) const = default;
};

struct Form {
    std::string name;   // the class stem: "MainWindow" -> MainWindowUi + MainWindow
    Node        root;
    std::vector<ActionDef> actions;   // s003: on disk only when there are any
    std::vector<FileDef>   files;     // s005: on disk only when there are any
    std::vector<ModelDef>  models;    // s006: on disk only when there are any
};

// s006 -- models.
inline const StringList& field_types() {
    static const StringList t{"string", "int", "double", "bool"};
    return t;
}
std::string model_item(const ModelDef& m);           // "Note" -> "NoteItem"
std::string field_cpp_type(const std::string& t);    // string -> std::string, int -> std::int64_t ...
std::string field_title(const FieldDef& f);          // the header shown
const ModelDef* find_model(const Form& f, const std::string& name);
// s007 -- a ColumnView's picking and editing. selection: "single" (the
// default, also for an unknown value), "multiple" or "none". editable: the
// field names the user may change in place (as written; validate checks them).
std::string view_selection(const Node& n);
// s008 -- the form's window comes back as it was left (root prop "remember").
bool remembers(const Form& f);
StringList  view_editable(const Node& n);
bool        view_edits(const Node& n, const std::string& field);

// s005 -- file dialogs.
inline const StringList& file_modes() {
    static const StringList m{"open", "open_multiple", "save", "folder"};
    return m;
}
std::string file_chosen_hook(const FileDef& f);     // on_<name>_chosen
std::string file_cancelled_hook(const FileDef& f);  // on_<name>_cancelled
std::string file_chooser(const FileDef& f);         // choose_<name>
// A filter row "Label=pat;pat". ok=false when it has no label or no pattern.
struct FileFilterRow {
    bool        ok = false;
    std::string label;
    StringList  patterns;          // "*.txt", "image/*"
};
FileFilterRow parse_file_filter(const std::string& s);

// "save_as" -> "save-as" (GIO action names allow letters, digits, '-', '.').
std::string gio_action_name(const std::string& name);
// The hook an action becomes (an action that opens a window has none).
std::string action_hook(const ActionDef& a);
// s010 -- the text a key's row shows: the title, else the label of this form's
// menu row that runs it, else the name made readable ("save_as" -> "Save as").
std::string action_title(const Form& f, const ActionDef& a);
// "save_as" -> "Save as"; "MainWindow" -> "Main window".
std::string readable(const std::string& name);
// s010 -- the member that keeps an opened window: m_opens_<action>.
std::string opens_member(const ActionDef& a);
// A menu entry (MenuButton "menu" items): "Label=win.action", "-" between sections.
struct MenuEntry {
    bool        separator = false;
    std::string label, action;   // action "" = a plain (insensitive) row
};
MenuEntry parse_menu_entry(const std::string& s);

// ── JSON ───────────────────────────────────────────────────────────────────
// load: throws std::runtime_error with a readable message on bad JSON / shape.
// (Meaning -- unknown types, bad ids -- is validate()'s job, not load's.)
Form form_from_json(const std::string& text);
std::string form_to_json(const Form& f);
Form load_form(const std::string& path);
void save_form(const Form& f, const std::string& path);

// ── meaning ────────────────────────────────────────────────────────────────
struct Issue {
    bool        error;   // false = a warning (generation still runs)
    std::string where;   // "main_window/body/save"
    std::string what;
};
std::vector<Issue> validate(const Form& f);
bool has_errors(const std::vector<Issue>& issues);

// The hook a ticked signal becomes. Root signals use the root's id too, so a
// hook never collides with gtkmm's own virtual on_close_request() & co.
std::string hook_name(const Node& n, const SignalHook& h);
// "MainWindow" -> "main_window" (registry prefix, file-friendly).
std::string snake(const std::string& pascal);
bool is_identifier(const std::string& s);
bool is_cpp_keyword(const std::string& s);

// Depth-first, pre-order: the order members are declared and built in.
void walk(const Node& n, const std::function<void(const Node&, const Node* parent)>& fn);

}  // namespace mortimer::core
