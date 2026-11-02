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

// s013 -- a list of models, as a form or mortimer.json writes it.
static std::vector<ModelDef> models_from(const json& arr) {
    std::vector<ModelDef> out;
    if (!arr.is_array()) throw std::runtime_error("models must be a list");
    for (const auto& x : arr) {
        if (!x.is_object()) throw std::runtime_error("a model is {\"name\": ..., \"fields\": [...]}");
        ModelDef m;
        m.name = x.value("name", "");
        if (x.contains("fields")) {
            if (!x["fields"].is_array()) throw std::runtime_error("a model's fields must be a list");
            for (const auto& y : x["fields"]) {
                if (!y.is_object()) throw std::runtime_error("a field is {\"name\": ..., \"type\": ...}");
                m.fields.push_back({y.value("name", ""), y.value("type", "string"), y.value("title", "")});
            }
        }
        out.push_back(std::move(m));
    }
    return out;
}
static json models_to(const std::vector<ModelDef>& ms) {
    json a = json::array();
    for (const auto& m : ms) {
        json o;
        o["name"] = m.name;
        json fs = json::array();
        for (const auto& x : m.fields) {
            json y;
            y["name"] = x.name;
            y["type"] = x.type;
            if (!x.title.empty()) y["title"] = x.title;
            fs.push_back(y);
        }
        o["fields"] = fs;
        a.push_back(o);
    }
    return a;
}
std::vector<ModelDef> models_from_json(const std::string& text) {
    try { return models_from(json::parse(text)); }
    catch (const json::exception& e) { throw std::runtime_error(std::string("models: ") + e.what()); }
}
std::string models_to_json(const std::vector<ModelDef>& ms) { return models_to(ms).dump(2); }

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
            f.actions.push_back({a.value("name", ""), a.value("accel", ""), a.value("required", false),
                                 a.value("title", ""), a.value("opens", "")});
        }
    }
    if (j.contains("files")) {
        if (!j["files"].is_array()) throw std::runtime_error("files must be a list");
        for (const auto& x : j["files"]) {
            if (!x.is_object()) throw std::runtime_error("a file dialog is {\"name\": ..., \"mode\": ...}");
            FileDef d;
            d.name = x.value("name", "");
            d.mode = x.value("mode", "open");
            d.title = x.value("title", "");
            d.accept = x.value("accept", "");
            if (x.contains("filters")) {
                if (!x["filters"].is_array()) throw std::runtime_error("a file dialog's filters must be a list of strings");
                for (const auto& r : x["filters"]) {
                    if (!r.is_string()) throw std::runtime_error("a file dialog's filters must be a list of strings");
                    d.filters.push_back(r.get<std::string>());
                }
            }
            d.cancel = x.value("cancel", false);
            d.required = x.value("required", false);
            f.files.push_back(std::move(d));
        }
    }
    if (j.contains("models")) f.models = models_from(j["models"]);
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
            if (!x.title.empty()) o["title"] = x.title;
            if (!x.opens.empty()) o["opens"] = x.opens;
            a.push_back(o);
        }
        j["actions"] = a;
    }
    if (!f.files.empty()) {
        json a = json::array();
        for (const auto& x : f.files) {
            json o;
            o["name"] = x.name;
            o["mode"] = x.mode;
            if (!x.title.empty()) o["title"] = x.title;
            if (!x.accept.empty()) o["accept"] = x.accept;
            if (!x.filters.empty()) o["filters"] = x.filters;
            if (x.cancel) o["cancel"] = true;
            if (x.required) o["required"] = true;
            a.push_back(o);
        }
        j["files"] = a;
    }
    if (!f.models.empty()) j["models"] = models_to(f.models);
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
std::string opens_member(const ActionDef& a) { return "m_opens_" + a.name; }

std::string readable(const std::string& name) {
    std::string out;
    for (std::size_t i = 0; i < name.size(); ++i) {
        const char c = name[i];
        if (c == '_') { if (!out.empty() && out.back() != ' ') out += ' '; continue; }
        const bool upper = c >= 'A' && c <= 'Z';
        if (upper && i > 0 && !out.empty() && out.back() != ' ') out += ' ';
        out += (upper && !out.empty()) ? static_cast<char>(c - 'A' + 'a') : c;
    }
    if (!out.empty() && out[0] >= 'a' && out[0] <= 'z') out[0] = static_cast<char>(out[0] - 'a' + 'A');
    return out;
}

std::string action_title(const Form& f, const ActionDef& a) {
    if (!a.title.empty()) return a.title;
    // The menu row that runs it says what it does, in the user's words.
    const std::string target = "win." + gio_action_name(a.name);
    std::string found;
    walk(f.root, [&](const Node& n, const Node*) {
        if (!found.empty()) return;
        const auto it = n.props.find("menu");
        if (it == n.props.end() || !std::holds_alternative<StringList>(it->second)) return;
        for (const auto& row : std::get<StringList>(it->second)) {
            const MenuEntry e = parse_menu_entry(row);
            if (!e.separator && e.action == target && !e.label.empty()) { found = e.label; return; }
        }
    });
    return found.empty() ? readable(a.name) : found;
}

std::string file_chosen_hook(const FileDef& f) { return "on_" + f.name + "_chosen"; }
std::string file_cancelled_hook(const FileDef& f) { return "on_" + f.name + "_cancelled"; }
std::string file_chooser(const FileDef& f) { return "choose_" + f.name; }

std::string model_item(const ModelDef& m) { return m.name + "Item"; }

std::string field_cpp_type(const std::string& t) {
    if (t == "int") return "std::int64_t";
    if (t == "double") return "double";
    if (t == "bool") return "bool";
    return "std::string";
}

std::string field_title(const FieldDef& f) {
    if (!f.title.empty()) return f.title;
    std::string t = f.name;
    for (auto& c : t) if (c == '_') c = ' ';
    if (!t.empty() && t[0] >= 'a' && t[0] <= 'z') t[0] = static_cast<char>(t[0] - 'a' + 'A');
    return t;
}

const ModelDef* find_model(const Form& f, const std::string& name) {
    for (const auto& m : f.models) if (m.name == name) return &m;
    for (const auto& m : f.shared) if (m.name == name) return &m;   // s013: the project's
    return nullptr;
}

bool remembers(const Form& f) {
    const auto it = f.root.props.find("remember");
    return it != f.root.props.end() && std::holds_alternative<bool>(it->second) && std::get<bool>(it->second);
}

std::string view_selection(const Node& n) {
    const auto it = n.props.find("selection");
    if (it == n.props.end() || !std::holds_alternative<std::string>(it->second)) return "single";
    const auto& v = std::get<std::string>(it->second);
    return v == "multiple" || v == "none" ? v : "single";
}

StringList view_editable(const Node& n) {
    const auto it = n.props.find("editable");
    return it != n.props.end() && std::holds_alternative<StringList>(it->second)
               ? std::get<StringList>(it->second) : StringList{};
}

bool view_edits(const Node& n, const std::string& field) {
    const auto e = view_editable(n);
    return std::find(e.begin(), e.end(), field) != e.end();
}

FileFilterRow parse_file_filter(const std::string& s) {
    FileFilterRow r;
    const auto eq = s.find('=');
    if (eq == std::string::npos) return r;
    auto trim = [](std::string x) {
        while (!x.empty() && x.front() == ' ') x.erase(x.begin());
        while (!x.empty() && x.back() == ' ') x.pop_back();
        return x;
    };
    r.label = trim(s.substr(0, eq));
    std::string rest = s.substr(eq + 1);
    std::size_t at = 0;
    while (at <= rest.size()) {
        const auto semi = rest.find(';', at);
        const std::string p = trim(rest.substr(at, semi == std::string::npos ? std::string::npos : semi - at));
        if (!p.empty()) r.patterns.push_back(p);
        if (semi == std::string::npos) break;
        at = semi + 1;
    }
    r.ok = !r.label.empty() && !r.patterns.empty();
    return r;
}

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

void walk_members(const Node& n, const std::function<void(const Node&, const Node*)>& fn) {
    std::function<void(const Node&, const Node*)> go = [&](const Node& x, const Node* parent) {
        fn(x, parent);
        const WidgetSpec* w = find_widget(x.type);
        if (w && w->children == ChildKind::Row) return;   // the row is the row class's
        for (const auto& c : x.children) go(c, &x);
    };
    go(n, nullptr);
}

const Node* view_row(const Node& view) { return view.children.empty() ? nullptr : &view.children.front(); }

std::string pascal(const std::string& name) {
    std::string out;
    bool up = true;
    for (char c : name) {
        if (c == '_') { up = true; continue; }
        out += (up && c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
        up = false;
    }
    return out;
}

std::string row_class(const Node& view) { return pascal(view.id) + "Row"; }

std::string bound_field(const Node& n) {
    const auto it = n.props.find("bind");
    return it != n.props.end() && std::holds_alternative<std::string>(it->second) ? std::get<std::string>(it->second) : "";
}

std::string bind_refusal(const std::string& type, const std::string& ft) {
    if (type == "Label" || type == "Button" || type == "Entry") return "";   // any field, as text
    if (type == "Image") return ft == "string" ? "" : "an Image shows a string field (an icon name)";
    if (type == "CheckButton" || type == "Switch") return ft == "bool" ? "" : "a " + type + " shows a bool field";
    if (type == "Scale" || type == "SpinButton")
        return ft == "int" || ft == "double" ? "" : "a " + type + " shows a number field (int or double)";
    return "a " + type + " can't show a field";
}

bool bind_writes(const std::string& type, const std::string& ft) {
    if (type == "CheckButton" || type == "Switch" || type == "Scale" || type == "SpinButton") return true;
    return type == "Entry" && ft == "string";
}

bool carry_model(Node& root, const ModelDef& was, const ModelDef& now) {
    std::map<std::string, std::string> to;   // old field -> new ("" = gone)
    for (std::size_t i = 0; i < was.fields.size(); ++i) {
        const auto& old = was.fields[i].name;
        std::string t;
        if (now.fields.size() == was.fields.size()) t = now.fields[i].name;
        else for (const auto& x : now.fields) if (x.name == old) t = old;
        to[old] = t;
    }
    bool changed = false;
    std::function<void(Node&, bool)> go = [&](Node& n, bool in_view) {
        if (in_view) {   // a row widget: its bind
            const auto b = n.props.find("bind");
            if (b != n.props.end() && std::holds_alternative<std::string>(b->second)) {
                const auto r = to.find(std::get<std::string>(b->second));
                if (r != to.end() && r->second != r->first) {
                    if (r->second.empty()) n.props.erase(b);
                    else b->second = Value(r->second);
                    changed = true;
                }
            }
        }
        bool mine = false;
        if (const auto m = n.props.find("model"); m != n.props.end() && std::holds_alternative<std::string>(m->second) &&
                                                  std::get<std::string>(m->second) == was.name) {
            mine = true;
            if (now.name != was.name) { m->second = Value(now.name); changed = true; }
            if (const auto e = n.props.find("editable"); e != n.props.end() && std::holds_alternative<StringList>(e->second)) {
                StringList is;
                for (const auto& f : std::get<StringList>(e->second)) {
                    const auto r = to.find(f);
                    if (r == to.end()) is.push_back(f);
                    else if (!r->second.empty()) is.push_back(r->second);
                }
                if (is != std::get<StringList>(e->second)) {
                    changed = true;
                    if (is.empty()) n.props.erase(e);
                    else e->second = Value(is);
                }
            }
        }
        for (auto& c : n.children) go(c, in_view || (mine && n.type == "ListView"));
    };
    go(root, false);
    return changed;
}

// ── validate ───────────────────────────────────────────────────────────────

// s013 -- a list of models on its own (a form's, or the project's): names,
// fields, types. `owner` = a class name the items mustn't collide with.
std::vector<Issue> validate_models(const std::vector<ModelDef>& ms, const std::string& owner) {
    std::vector<Issue> out;
    auto err = [&](const std::string& w, const std::string& m) { out.push_back({true, w, m}); };
    auto warn = [&](const std::string& w, const std::string& m) { out.push_back({false, w, m}); };
    // s006: models -- a class name each, fields that are C++ members.
    std::set<std::string> models;
    for (const auto& m : ms) {
        const std::string where = "model " + (m.name.empty() ? std::string("?") : m.name);
        if (!is_identifier(m.name) || is_cpp_keyword(m.name)) {
            err(where, "a model's name must be a C++ class name (Note, Patient)");
            continue;
        }
        if (!(m.name[0] >= 'A' && m.name[0] <= 'Z'))
            warn(where, "\"" + m.name + "\" -- class names start with a capital by convention");
        if (!models.insert(m.name).second) err(where, "model \"" + m.name + "\" is declared twice");
        if (!owner.empty() && (model_item(m) == owner + "Ui" || model_item(m) == owner))
            err(where, "its class " + model_item(m) + " would be the form's own name");
        if (m.fields.empty()) err(where, "a model needs at least one field (a column to show)");
        std::set<std::string> fields;
        // The item's own names: id(), create(), id_taken(), and its private helpers.
        static const std::set<std::string> taken{"id", "create", "id_taken", "ids", "next"};
        for (const auto& x : m.fields) {
            const std::string fw = where + "." + (x.name.empty() ? std::string("?") : x.name);
            if (!is_identifier(x.name) || is_cpp_keyword(x.name))
                err(fw, "a field's name must be a C++ identifier (title, due_date)");
            else if (taken.count(x.name))
                err(fw, "\"" + x.name + "\" is taken -- every item already has " +
                            (x.name == "id" ? "an id() (automatic, or yours via create)" : "a " + x.name));
            else if (!fields.insert(x.name).second)
                err(fw, "field \"" + x.name + "\" is declared twice");
            if (std::find(field_types().begin(), field_types().end(), x.type) == field_types().end())
                err(fw, "type \"" + x.type + "\" -- a field is string, int, double or bool");
        }
    }
    return out;
}

static const ModelDef* model_of_node(const Form& f, const Node& n) {
    const auto it = n.props.find("model");
    return it != n.props.end() && std::holds_alternative<std::string>(it->second)
               ? find_model(f, std::get<std::string>(it->second)) : nullptr;
}

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
        if (a.opens.empty()) {
            if (!hooks.insert(action_hook(a)).second) err(where, "two things make the hook " + action_hook(a) + "()");
        } else {
            // s010: it opens a window -- no hook. The project checks the form exists.
            if (a.opens != kShortcutsTarget && (!is_identifier(a.opens) || is_cpp_keyword(a.opens)))
                err(where, "opens \"" + a.opens + "\" -- name a form of the project (About) or " +
                               kShortcutsTarget);
            else if (a.opens == f.name)
                err(where, "opens its own form -- " + f.name + " is already open when this runs");
            if (a.required) warn(where, "it opens " + a.opens + ", so there is no hook to require");
        }
        if (!a.accel.empty()) {
            // GTK parses the key at run time; refuse here what it never could.
            const auto close = a.accel.rfind('>');
            const std::string key = close == std::string::npos ? a.accel : a.accel.substr(close + 1);
            if (key.empty() || std::count(a.accel.begin(), a.accel.end(), '<') !=
                                   std::count(a.accel.begin(), a.accel.end(), '>'))
                err(where, "key \"" + a.accel + "\" isn't GTK's syntax (\"<Control>s\", \"<Control><Shift>z\", \"F5\")");
        }
    }
    // s005: file dialogs -- names, modes, filters; their hooks join the namespace.
    std::set<std::string> files;
    for (const auto& d : f.files) {
        const std::string where = "file dialog " + (d.name.empty() ? std::string("?") : d.name);
        if (!is_identifier(d.name) || is_cpp_keyword(d.name)) {
            err(where, "a file dialog's name must be a C++ identifier (open_doc, export_pdf)");
            continue;
        }
        if (!files.insert(d.name).second) err(where, "file dialog \"" + d.name + "\" is declared twice");
        if (std::find(file_modes().begin(), file_modes().end(), d.mode) == file_modes().end())
            err(where, "mode \"" + d.mode + "\" -- a file dialog is open, open_multiple, save or folder");
        if (!hooks.insert(file_chosen_hook(d)).second)
            err(where, "two things make the hook " + file_chosen_hook(d) + "()");
        if (d.cancel && !hooks.insert(file_cancelled_hook(d)).second)
            err(where, "two things make the hook " + file_cancelled_hook(d) + "()");
        for (const auto& row : d.filters)
            if (!parse_file_filter(row).ok)
                err(where, "filter \"" + row + "\" -- write it Label=*.txt;*.md (a '/' makes a MIME type: Images=image/*)");
        if (d.mode == "folder" && !d.filters.empty())
            warn(where, "a folder dialog shows folders only -- its filters are ignored");
    }
    // s006: models -- a class name each, fields that are C++ members.
    for (const auto& i : validate_models(f.models, f.name)) out.push_back(i);
    // s013: a form's own model can't take a shared one's name -- which would its
    // views mean?
    for (const auto& m : f.models)
        for (const auto& x : f.shared)
            if (m.name == x.name)
                err("model " + m.name, m.name + " is shared by the project -- this form's own " + m.name +
                                           " would hide it (delete it here, or share it under another name)");
    walk(f.root, [&](const Node& n, const Node*) {
        // The generated class's own members: m_hooks, m_actions, m_files, m_alive.
        static const std::set<std::string> taken{"hooks", "actions", "files", "alive"};
        if (taken.count(n.id))
            err(n.id, "id \"" + n.id + "\" is taken -- the generated class has its own m_" + n.id);
        for (const auto& a : f.actions)   // s010: the window an action opens is kept in m_opens_<action>
            if (!a.opens.empty() && "m_" + n.id == opens_member(a))
                err(n.id, "id \"" + n.id + "\" is taken -- action " + a.name + " keeps its window in " +
                              opens_member(a));
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
            if (p->kind == PropKind::Ref && p->ref_type == "@model") {   // s006: one of the form's models
                const auto& s = std::get<std::string>(v);
                if (!find_model(f, s))
                    err(where, "\"" + key + "\" names model \"" + s + "\" -- the form has no such model (add it to the form's models)");
            } else if (p->kind == PropKind::Ref) {
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

        // s006: a ColumnView shows a model; its store and selection are members.
        // s011: so does a ListView (its row is checked below).
        if (n.type == "ColumnView" || n.type == "ListView") {
            if (!n.props.count("model")) {
                if (!n.signals.empty()) err(where, "its hooks hand you a row of its model -- pick a model first");
                else warn(where, "shows nothing until it has a model");
            }
            // s007: editable cells name fields of its model; picking must allow the hooks.
            const ModelDef* md = n.props.count("model") && std::holds_alternative<std::string>(n.props.at("model"))
                                     ? find_model(f, std::get<std::string>(n.props.at("model"))) : nullptr;
            const StringList ed = view_editable(n);
            std::set<std::string> eseen;
            for (const auto& e : ed) {
                if (!eseen.insert(e).second) { err(where, "\"editable\" names " + e + " twice"); continue; }
                if (!md) {
                    if (n.props.count("model")) continue;   // the unknown model is already an error
                    err(where, "\"editable\" names fields of its model -- pick a model first");
                    break;
                }
                bool found = false;
                for (const auto& x : md->fields) found = found || x.name == e;
                if (!found) err(where, "\"editable\" names " + e + " -- " + md->name + " has no such field");
            }
            bool hooked_edit = false, hooked_pick = false;
            for (const auto& h : n.signals) {
                hooked_edit = hooked_edit || h.signal == "edited";
                hooked_pick = hooked_pick || h.signal == "selected";
            }
            if (hooked_edit && ed.empty() && n.type == "ColumnView")
                warn(where, "edited is hooked but no cell is editable -- tick fields in \"editable\"");
            if (hooked_pick && view_selection(n) == "none")
                err(where, "selected is hooked but selection is none -- nothing can be picked (single or multiple, or untick it)");
            for (const char* m : {"_store", "_selection", "_edited"})
                if (ids.count(n.id + m))
                    err(where, "the generated class has m_" + n.id + m + " -- widget \"" + n.id + m + "\" collides");
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
                case ChildKind::Row:
                    ++plain;
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

    // s011: a ListView's row -- built per row, so only what a row can be, no
    // hooks yet, and every "bind" a field of the view's model that fits.
    std::set<const Node*> in_row;
    std::set<std::string> row_classes;
    walk(f.root, [&](const Node& v, const Node*) {
        if (v.type != "ListView") return;
        const std::string where = v.id;
        if (!row_classes.insert(row_class(v)).second)
            err(where, "two ListViews make the row class " + row_class(v) + " -- rename one");
        const Node* row = view_row(v);
        if (!row) {
            if (v.props.count("model")) err(where, "it needs a row: a child (a Box of Labels...) whose widgets bind to fields");
            return;
        }
        const ModelDef* md = model_of_node(f, v);
        walk(*row, [&](const Node& n, const Node*) {
            in_row.insert(&n);
            static const std::set<std::string> ok{"Box", "Grid", "Label", "Image", "Button", "CheckButton",
                                                  "Switch", "Entry", "Scale", "SpinButton"};
            if (!ok.count(n.type))
                err(n.id, "a " + n.type + " can't be in a list row (Box, Grid, Label, Image, Button, CheckButton, "
                          "Switch, Entry, Scale, SpinButton)");
            if (const WidgetSpec* nw = find_widget(n.type))   // s011 m2: a row's hooks hand you its item
                for (const auto& h : n.signals)
                    if (const SignalSpec* ss = find_signal(*nw, h.signal); ss && ss->ret != "void")
                        err(n.id, "\"" + h.signal + "\" returns a value -- a row's hooks can't");
            const std::string b = bound_field(n);
            if (b.empty()) return;
            if (!md) {
                if (!v.props.count("model")) err(n.id, "\"bind\" names a field of the view's model -- pick a model first");
                return;
            }
            const FieldDef* fd = nullptr;
            for (const auto& x : md->fields) if (x.name == b) fd = &x;
            if (!fd) { err(n.id, "\"bind\" names " + b + " -- " + md->name + " has no such field"); return; }
            if (const auto why = bind_refusal(n.type, fd->type); !why.empty())
                err(n.id, "\"bind\" " + b + " is a " + fd->type + " -- " + why);
        });
        if (md) {
            bool writes = false, hooked = false;
            walk(*row, [&](const Node& n, const Node*) {
                const std::string b = bound_field(n);
                for (const auto& x : md->fields) writes = writes || (x.name == b && bind_writes(n.type, x.type));
            });
            for (const auto& h : v.signals) hooked = hooked || h.signal == "edited";
            if (hooked && !writes)
                warn(where, "edited is hooked but nothing in the row writes a field -- bind a check, a switch, "
                            "a number or a text box");
        }
    });
    walk(f.root, [&](const Node& n, const Node*) {
        if (!bound_field(n).empty() && !in_row.count(&n))
            err(n.id, "\"bind\" only means something inside a ListView's row");
    });
    return out;
}

bool has_errors(const std::vector<Issue>& issues) {
    for (const auto& i : issues)
        if (i.error) return true;
    return false;
}

}  // namespace mortimer::core
