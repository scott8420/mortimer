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

struct Form {
    std::string name;   // the class stem: "MainWindow" -> MainWindowUi + MainWindow
    Node        root;
};

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
