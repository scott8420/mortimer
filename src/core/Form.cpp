#include "core/Form.hpp"
#include "core/Catalog.hpp"

#include "json.hpp"

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>

namespace mortimer::core {

using json = nlohmann::ordered_json;

namespace {

Value value_from(const json& j, const std::string& where) {
    if (j.is_boolean()) return j.get<bool>();
    if (j.is_number_integer()) return j.get<std::int64_t>();
    if (j.is_number_float()) return j.get<double>();
    if (j.is_string()) return j.get<std::string>();
    if (j.is_array()) {
        StringList out;
        for (const auto& x : j) {
            if (!x.is_string()) throw std::runtime_error(where + ": a list may only hold strings");
            out.push_back(x.get<std::string>());
        }
        return out;
    }
    throw std::runtime_error(where + ": a value must be true/false, a number, a string or a list");
}

json value_to(const Value& v) {
    return std::visit([](const auto& x) -> json { return json(x); }, v);
}

std::map<std::string, Value> map_from(const json& j, const std::string& where) {
    std::map<std::string, Value> out;
    if (j.is_null()) return out;
    if (!j.is_object()) throw std::runtime_error(where + ": expected an object");
    for (auto it = j.begin(); it != j.end(); ++it)
        out[it.key()] = value_from(it.value(), where + "." + it.key());
    return out;
}

Node node_from(const json& j, const std::string& where) {
    if (!j.is_object()) throw std::runtime_error(where + ": a widget must be an object");
    Node n;
    n.type = j.value("type", "");
    n.id = j.value("id", "");
    const std::string here = where + "/" + (n.id.empty() ? "?" : n.id);
    if (j.contains("props")) n.props = map_from(j["props"], here + " props");
    if (j.contains("pack")) n.pack = map_from(j["pack"], here + " pack");
    if (j.contains("signals")) {
        if (!j["signals"].is_array()) throw std::runtime_error(here + ": signals must be a list");
        for (const auto& s : j["signals"]) {
            SignalHook h;
            if (s.is_string()) {
                h.signal = s.get<std::string>();
            } else if (s.is_object()) {
                h.signal = s.value("signal", "");
                h.required = s.value("required", false);
                h.handler = s.value("handler", "");
            } else {
                throw std::runtime_error(here + ": a signal is a name or {\"signal\": ...}");
            }
            n.signals.push_back(h);
        }
    }
    if (j.contains("children")) {
        if (!j["children"].is_array()) throw std::runtime_error(here + ": children must be a list");
        for (const auto& c : j["children"]) n.children.push_back(node_from(c, here));
    }
    return n;
}

json node_to(const Node& n) {
    json j;
    j["type"] = n.type;
    j["id"] = n.id;
    if (!n.pack.empty()) {
        json p = json::object();
        for (const auto& [k, v] : n.pack) p[k] = value_to(v);
        j["pack"] = p;
    }
    if (!n.props.empty()) {
        json p = json::object();
        for (const auto& [k, v] : n.props) p[k] = value_to(v);
        j["props"] = p;
    }
    if (!n.signals.empty()) {
        json s = json::array();
        for (const auto& h : n.signals) {
            json o;
            o["signal"] = h.signal;
            if (h.required) o["required"] = true;
            if (!h.handler.empty()) o["handler"] = h.handler;
            s.push_back(o);
        }
        j["signals"] = s;
    }
    if (!n.children.empty()) {
        json c = json::array();
        for (const auto& k : n.children) c.push_back(node_to(k));
        j["children"] = c;
    }
    return j;
}

// Does this JSON-shaped value fit the property's kind? (Double takes an
// integer too: "upper": 100 is a fine way to write 100.0.)
bool fits(const PropSpec& p, const Value& v) {
    switch (p.kind) {
        case PropKind::Bool:       return std::holds_alternative<bool>(v);
        case PropKind::Int:        return std::holds_alternative<std::int64_t>(v);
        case PropKind::Double:     return std::holds_alternative<double>(v) ||
                                          std::holds_alternative<std::int64_t>(v);
        case PropKind::String:
        case PropKind::Enum:
        case PropKind::Ref:        return std::holds_alternative<std::string>(v);
        case PropKind::StringList: return std::holds_alternative<StringList>(v);
    }
    return false;
}

}  // namespace

Form form_from_json(const std::string& text) {
    json j;
    try {
        j = json::parse(text);
    } catch (const json::parse_error& e) {
        throw std::runtime_error(std::string("not valid JSON: ") + e.what());
    }
    if (!j.is_object()) throw std::runtime_error("a form must be a JSON object");
    Form f;
    f.name = j.value("name", "");
    if (!j.contains("root")) throw std::runtime_error("a form needs a \"root\" widget");
    f.root = node_from(j["root"], f.name.empty() ? "form" : f.name);
    if (j.contains("actions")) {
        if (!j["actions"].is_array()) throw std::runtime_error("actions must be a list");
        for (const auto& a : j["actions"]) {
            if (!a.is_object()) throw std::runtime_error("an action is {\"name\": ..., \"accel\": ...}");
            f.actions.push_back({a.value("name", ""), a.value("accel", ""), a.value("required", false)});
        }
    }
    return f;
}

std::string form_to_json(const Form& f) {
    json j;
    j["mortimer"] = 1;
    j["name"] = f.name;
    j["root"] = node_to(f.root);
    if (!f.actions.empty()) {
        json a = json::array();
        for (const auto& x : f.actions) {
            json o;
            o["name"] = x.name;
            if (!x.accel.empty()) o["accel"] = x.accel;
            if (x.required) o["required"] = true;
            a.push_back(o);
        }
        j["actions"] = a;
    }
    return j.dump(2) + "\n";
}

Form load_form(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot read " + path);
    std::stringstream ss;
    ss << in.rdbuf();
    try {
        return form_from_json(ss.str());
    } catch (const std::exception& e) {
        throw std::runtime_error(path + ": " + e.what());
    }
}

void save_form(const Form& f, const std::string& path) {
    std::ofstream out(path, std::ios::trunc);
    if (!out) throw std::runtime_error("cannot write " + path);
    out << form_to_json(f);
}

// ── names ──────────────────────────────────────────────────────────────────

bool is_identifier(const std::string& s) {
    if (s.empty()) return false;
    auto alpha = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; };
    auto digit = [](char c) { return c >= '0' && c <= '9'; };
    if (!alpha(s[0])) return false;
    for (char c : s)
        if (!alpha(c) && !digit(c)) return false;
    return true;
}

bool is_cpp_keyword(const std::string& s) {
    static const std::set<std::string> k = {
        "alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand", "bitor", "bool", "break",
        "case", "catch", "char", "char8_t", "char16_t", "char32_t", "class", "compl", "concept",
        "const", "consteval", "constexpr", "constinit", "const_cast", "continue", "co_await",
        "co_return", "co_yield", "decltype", "default", "delete", "do", "double", "dynamic_cast",
        "else", "enum", "explicit", "export", "extern", "false", "float", "for", "friend", "goto",
        "if", "inline", "int", "long", "mutable", "namespace", "new", "noexcept", "not", "not_eq",
        "nullptr", "operator", "or", "or_eq", "private", "protected", "public", "register",
        "reinterpret_cast", "requires", "return", "short", "signed", "sizeof", "static",
        "static_assert", "static_cast", "struct", "switch", "template", "this", "thread_local",
        "throw", "true", "try", "typedef", "typeid", "typename", "union", "unsigned", "using",
        "virtual", "void", "volatile", "wchar_t", "while", "xor", "xor_eq"};
    return k.count(s) > 0;
}

std::string snake(const std::string& pascal) {
    std::string out;
    for (std::size_t i = 0; i < pascal.size(); ++i) {
        const char c = pascal[i];
        if (c >= 'A' && c <= 'Z') {
            if (i > 0 && pascal[i - 1] != '_') out += '_';
            out += static_cast<char>(c - 'A' + 'a');
        } else {
            out += c;
        }
    }
    return out;
}

std::string gio_action_name(const std::string& name) {
    std::string s = name;
    for (char& c : s) if (c == '_') c = '-';
    return s;
}

std::string action_hook(const ActionDef& a) { return "on_action_" + a.name; }

MenuEntry parse_menu_entry(const std::string& s) {
    MenuEntry e;
    if (s == "-" || s == "---") { e.separator = true; return e; }
    const auto eq = s.rfind('=');
    if (eq == std::string::npos) { e.label = s; return e; }
    e.label = s.substr(0, eq);
    e.action = s.substr(eq + 1);
    return e;
}

std::string hook_name(const Node& n, const SignalHook& h) {
    return h.handler.empty() ? "on_" + n.id + "_" + h.signal : h.handler;
}

void walk(const Node& n, const std::function<void(const Node&, const Node*)>& fn) {
    std::function<void(const Node&, const Node*)> go = [&](const Node& x, const Node* parent) {
        fn(x, parent);
        for (const auto& c : x.children) go(c, &x);
    };
    go(n, nullptr);
}

// ── validate ───────────────────────────────────────────────────────────────

std::vector<Issue> validate(const Form& f) {
    std::vector<Issue> out;
    auto err = [&](const std::string& w, const std::string& m) { out.push_back({true, w, m}); };
    auto warn = [&](const std::string& w, const std::string& m) { out.push_back({false, w, m}); };

    if (!is_identifier(f.name) || is_cpp_keyword(f.name))
        err("form", "name \"" + f.name + "\" must be a C++ class name (MainWindow)");
    else if (!(f.name[0] >= 'A' && f.name[0] <= 'Z'))
        warn("form", "name \"" + f.name + "\" -- class names start with a capital by convention");

    // First pass: ids, so a Ref can point forward.
    std::map<std::string, const Node*> ids;
    std::set<std::string> hooks;

    // s003: actions -- names, keys, and their hooks join the hook namespace.
    std::set<std::string> actions;
    for (const auto& a : f.actions) {
        const std::string where = "action " + (a.name.empty() ? std::string("?") : a.name);
        if (!is_identifier(a.name) || is_cpp_keyword(a.name)) {
            err(where, "an action's name must be a C++ identifier (save, save_as)");
            continue;
        }
        if (!actions.insert(a.name).second) err(where, "action \"" + a.name + "\" is declared twice");
        if (!hooks.insert(action_hook(a)).second) err(where, "two things make the hook " + action_hook(a) + "()");
        if (!a.accel.empty()) {
            // GTK parses the key at run time; refuse here what it never could.
            const auto close = a.accel.rfind('>');
            const std::string key = close == std::string::npos ? a.accel : a.accel.substr(close + 1);
            if (key.empty() || std::count(a.accel.begin(), a.accel.end(), '<') !=
                                   std::count(a.accel.begin(), a.accel.end(), '>'))
                err(where, "key \"" + a.accel + "\" isn't GTK's syntax (\"<Control>s\", \"<Control><Shift>z\", \"F5\")");
        }
    }
    walk(f.root, [&](const Node& n, const Node*) {
        if (!is_identifier(n.id) || is_cpp_keyword(n.id))
            err(n.id.empty() ? "?" : n.id,
                "id \"" + n.id + "\" must be a C++ identifier (letters, digits, _)");
        else if (ids.count(n.id))
            err(n.id, "id \"" + n.id + "\" is used twice -- every widget needs its own");
        else
            ids[n.id] = &n;
    });

    walk(f.root, [&](const Node& n, const Node* parent) {
        const std::string where = n.id;
        const WidgetSpec* w = find_widget(n.type);
        if (!w) {
            err(where, "unknown widget type \"" + n.type + "\"");
            return;
        }
        if (!parent && !w->root)
            err(where, "a form's root must be a window (ApplicationWindow or Window), not " + n.type);
        if (parent && w->root)
            err(where, n.type + " can only be a form's root");
        // A window's "visible" would SHOW it from inside build_ui() -- before
        // its titlebar and children are in place (GTK warns, and closing it
        // later can die with an X error: found driving s001). Your code shows
        // a window with present(), once it's whole.
        if (!parent && n.props.count("visible"))
            err(where, "\"visible\" can't be set on the window itself -- show it with present() "
                       "from your code, once it's built");

        // properties
        for (const auto& [key, v] : n.props) {
            const PropSpec* p = find_prop(*w, key);
            if (!p) {
                err(where, n.type + " has no property \"" + key + "\"");
                continue;
            }
            if (!fits(*p, v)) {
                err(where, "\"" + key + "\" takes a " + kind_name(p->kind));
                continue;
            }
            if (p->kind == PropKind::Enum) {
                const auto& s = std::get<std::string>(v);
                bool ok = false;
                std::string choices;
                for (const auto& [jv, cv] : p->values) {
                    ok = ok || jv == s;
                    choices += (choices.empty() ? "" : ", ") + jv;
                }
                if (!ok) err(where, "\"" + key + "\" is one of: " + choices);
            }
            if (p->kind == PropKind::Ref) {
                const auto& s = std::get<std::string>(v);
                auto it = ids.find(s);
                if (it == ids.end())
                    err(where, "\"" + key + "\" points at \"" + s + "\", which isn't in the form");
                else if (it->second->type != p->ref_type)
                    err(where, "\"" + key + "\" must point at a " + p->ref_type + ", \"" + s +
                                   "\" is a " + it->second->type);
                else if (it->second == &f.root)
                    err(where, "\"" + key + "\" cannot point at the window itself");
            }
        }

        // signals
        std::set<std::string> seen;
        for (const auto& h : n.signals) {
            if (!find_signal(*w, h.signal)) {
                std::string have;
                for (const auto& s : w->signals) have += (have.empty() ? "" : ", ") + s.name;
                err(where, n.type + " has no signal \"" + h.signal + "\"" +
                               (have.empty() ? "" : " (it has: " + have + ")"));
                continue;
            }
            if (!seen.insert(h.signal).second) err(where, "signal \"" + h.signal + "\" ticked twice");
            const std::string hn = hook_name(n, h);
            if (!is_identifier(hn)) err(where, "handler \"" + hn + "\" isn't a C++ name");
            else if (!hooks.insert(hn).second) err(where, "two signals make the hook " + hn + "()");
        }

        // s003: a menu's rows -- "Label=win.action" must name an action of this form.
        if (const auto it = n.props.find("menu"); it != n.props.end() && std::holds_alternative<StringList>(it->second)) {
            for (const auto& item : std::get<StringList>(it->second)) {
                const MenuEntry e = parse_menu_entry(item);
                if (e.separator) continue;
                if (e.label.empty()) err(where, "a menu row needs a label (\"" + item + "\")");
                if (e.action.rfind("win.", 0) == 0) {
                    bool found = false;
                    for (const auto& a : f.actions) found = found || gio_action_name(a.name) == e.action.substr(4);
                    if (!found) err(where, "menu row \"" + e.label + "\" names " + e.action +
                                               " -- the form has no such action (add it to the form's actions)");
                } else if (!e.action.empty() && e.action.rfind("app.", 0) != 0) {
                    warn(where, "menu row \"" + e.label + "\": \"" + e.action + "\" is neither win.* nor app.*");
                }
            }
        }

        // children: does this container take them, and how many?
        if (!n.children.empty() && w->children == ChildKind::None)
            err(where, n.type + " cannot hold children");
        int plain = 0, titlebar = 0, start = 0, end = 0, title = 0;
        for (const auto& c : n.children) {
            const auto keys = packing_keys(w->children);
            for (const auto& [k, v] : c.pack) {
                if (std::find(keys.begin(), keys.end(), k) == keys.end()) {
                    err(c.id, "\"" + k + "\" means nothing inside a " + n.type);
                    continue;
                }
                const bool num = k == "column" || k == "row" || k == "width" || k == "height";
                const bool flag = k == "resize" || k == "shrink";
                if (num && !std::holds_alternative<std::int64_t>(v))
                    err(c.id, "pack \"" + k + "\" takes a whole number");
                else if (flag && !std::holds_alternative<bool>(v))
                    err(c.id, "pack \"" + k + "\" takes true/false");
                else if (!num && !flag && !std::holds_alternative<std::string>(v))
                    err(c.id, "pack \"" + k + "\" takes a string");
            }
            auto slot_of = [&]() -> std::string {
                auto it = c.pack.find("slot");
                return it != c.pack.end() && std::holds_alternative<std::string>(it->second)
                           ? std::get<std::string>(it->second) : "";
            };
            const std::string slot = slot_of();
            switch (w->children) {
                case ChildKind::Window:
                    if (slot == "titlebar") ++titlebar;
                    else if (slot.empty()) ++plain;
                    else err(c.id, "a window's slot is \"titlebar\" or nothing");
                    break;
                case ChildKind::Single:
                    ++plain;
                    break;
                case ChildKind::Paned:
                    if (slot == "start") ++start;
                    else if (slot == "end") ++end;
                    else err(c.id, "a Paned child needs slot \"start\" or \"end\"");
                    break;
                case ChildKind::HeaderBar:
                    if (slot == "title") ++title;
                    else if (slot != "start" && slot != "end")
                        err(c.id, "a HeaderBar child needs slot \"start\", \"end\" or \"title\"");
                    break;
                case ChildKind::Grid:
                    if (!c.pack.count("column") || !c.pack.count("row"))
                        err(c.id, "a Grid child needs pack column and row");
                    break;
                default:
                    break;
            }
        }
        if (plain > 1)
            err(where, n.type + " holds ONE child -- put a Box or Grid in it to hold more");
        if (titlebar > 1) err(where, "only one titlebar");
        if (start > 1 || end > 1) err(where, "a Paned has one start and one end child");
        if (title > 1) err(where, "a HeaderBar has one title widget");
        if (w->root && plain == 0 && !parent)
            warn(where, "the window is empty");
    });
    return out;
}

bool has_errors(const std::vector<Issue>& issues) {
    for (const auto& i : issues)
        if (i.error) return true;
    return false;
}

}  // namespace mortimer::core
