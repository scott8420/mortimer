#include "core/Design.hpp"
#include "core/Catalog.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <stdexcept>

namespace mortimer::core {

// ── the store ──────────────────────────────────────────────────────────────

std::optional<Rec> Design::get(const Id& id) const {
    const auto it = m_recs.find(id);
    if (it == m_recs.end()) return std::nullopt;
    return it->second;
}

void Design::put(const Id& id, const std::optional<Rec>& v) {
    if (m_loading == 0 && m_door && !m_door()) ++m_strays;   // the gate
    if (v) m_recs[id] = *v;
    else   m_recs.erase(id);
    if (m_changed) m_changed();
}

void Design::load(const Form& f) {
    Loading scope(*this);
    m_recs.clear();
    m_next = 1;
    Rec form{"", f.name, "", 0, {}, {}, {}, {}};
    form.actions = f.actions;
    form.files = f.files;
    form.models = f.models;
    put(kForm, form);
    std::function<void(const Node&, const Id&, double)> add = [&](const Node& n, const Id& parent, double pos) {
        const Id key = fresh_key();
        put(key, Rec{n.type, n.id, parent, pos, n.props, n.pack, n.signals, {}});
        double p = 1;
        for (const auto& c : n.children) add(c, key, p++);
    };
    add(f.root, "", 1);
}

Form Design::to_form() const {
    const Id r = root();
    if (r.empty()) throw std::logic_error("the design has no root widget");
    std::function<Node(const Id&)> build = [&](const Id& key) {
        const Rec& rec = m_recs.at(key);
        Node n{rec.type, rec.name, rec.props, rec.pack, rec.signals, {}};
        for (const auto& c : children(key)) n.children.push_back(build(c));
        return n;
    };
    Form f{form_name(), build(r), {}};
    if (const auto it = m_recs.find(kForm); it != m_recs.end()) {
        f.actions = it->second.actions;
        f.files = it->second.files;
        f.models = it->second.models;
    }
    return f;
}

// ── reading ────────────────────────────────────────────────────────────────

Design::Id Design::root() const {
    for (const auto& [k, r] : m_recs)
        if (k != kForm && r.parent.empty()) return k;
    return {};
}

std::string Design::form_name() const {
    const auto it = m_recs.find(kForm);
    return it == m_recs.end() ? std::string{} : it->second.name;
}

std::vector<Design::Id> Design::children(const Id& parent) const {
    std::vector<Id> out;
    if (parent.empty()) return out;
    for (const auto& [k, r] : m_recs)
        if (k != kForm && r.parent == parent) out.push_back(k);
    std::stable_sort(out.begin(), out.end(),
                     [this](const Id& a, const Id& b) { return m_recs.at(a).pos < m_recs.at(b).pos; });
    return out;
}

std::vector<Design::Id> Design::subtree(const Id& id) const {
    std::vector<Id> out;
    if (!m_recs.count(id)) return out;
    std::function<void(const Id&)> go = [&](const Id& k) {
        out.push_back(k);
        for (const auto& c : children(k)) go(c);
    };
    go(id);
    return out;
}

std::vector<Design::Id> Design::all() const { return subtree(root()); }

Design::Id Design::find(const std::string& name) const {
    for (const auto& [k, r] : m_recs)
        if (k != kForm && r.name == name) return k;
    return {};
}

bool Design::is_under(const Id& id, const Id& ancestor) const {
    for (Id k = id; !k.empty();) {
        if (k == ancestor) return true;
        const auto it = m_recs.find(k);
        if (it == m_recs.end()) return false;
        k = it->second.parent;
    }
    return false;
}

std::size_t Design::count() const { return m_recs.size() - (m_recs.count(kForm) ? 1 : 0); }

Design::Id Design::fresh_key() {
    for (;;) {
        Id k = "n" + std::to_string(m_next++);
        if (!m_recs.count(k)) return k;
    }
}

std::string Design::fresh_name(const std::string& type) const {
    const std::string stem = snake(type);
    for (int n = 1;; ++n) {
        const std::string cand = stem + std::to_string(n);
        if (find(cand).empty()) return cand;
    }
}

double Design::pos_at(const Id& parent, int index, const Id& moving) const {
    std::vector<Id> sib = children(parent);
    std::erase(sib, moving);
    if (sib.empty()) return 1.0;
    const auto n = static_cast<int>(sib.size());
    if (index < 0 || index >= n) return m_recs.at(sib.back()).pos + 1.0;
    if (index == 0) return m_recs.at(sib.front()).pos - 1.0;
    return (m_recs.at(sib[index - 1]).pos + m_recs.at(sib[index]).pos) / 2.0;
}

// ── the verbs ──────────────────────────────────────────────────────────────

namespace design {
namespace {

std::string curly(const std::string& s) { return "“" + s + "”"; }
std::string name_of(const Design& d, const Design::Id& id) {
    const auto r = d.get(id);
    return r ? curly(r->name) : curly("?");
}
void say(std::string* why, const std::string& s) { if (why) *why = s; }

std::string str_of(const std::map<std::string, Value>& m, const std::string& k) {
    const auto it = m.find(k);
    return it != m.end() && std::holds_alternative<std::string>(it->second)
               ? std::get<std::string>(it->second) : std::string{};
}

int index_in_parent(const Design& d, const Design::Id& id) {
    const auto r = d.get(id);
    if (!r) return -1;
    const auto sib = d.children(r->parent);
    const auto it = std::find(sib.begin(), sib.end(), id);
    return it == sib.end() ? -1 : static_cast<int>(it - sib.begin());
}

}  // namespace

std::string can_hold(const Design& d, const Design::Id& parent, const std::string& type,
                     std::map<std::string, Value>* pack, const Design::Id& moving) {
    const auto pr = d.get(parent);
    if (!pr || parent == Design::kForm) return "nothing to put it in";
    const WidgetSpec* w = find_widget(type);
    if (!w) return "Mortimer doesn't know a \"" + type + "\"";
    if (w->root) return "a " + type + " is a window -- it can only be a form's top level";
    const WidgetSpec* pw = find_widget(pr->type);
    if (!pw) return "the parent's type \"" + pr->type + "\" is unknown";
    if (pw->children == ChildKind::None) return "a " + pr->type + " cannot hold children";
    // s011: a ListView's row (and anything in it) is built once per row -- only
    // what a row can be, and the view takes ONE child, the row.
    for (Design::Id up = parent; !up.empty() && up != Design::kForm;) {
        const auto u = d.get(up);
        if (!u) break;
        if (u->type == "ListView") {
            static const std::set<std::string> ok{"Box", "Grid", "Label", "Image", "Button", "CheckButton",
                                                  "Switch", "Entry", "Scale", "SpinButton"};
            if (!ok.count(type)) return "a " + type + " can't be in a list row";
            break;
        }
        up = u->parent;
    }
    if (pw->children == ChildKind::Row) {
        std::vector<Design::Id> rows = d.children(parent);
        std::erase(rows, moving);
        if (!rows.empty()) return "a ListView has ONE row -- put things in its row (" + d.get(rows.front())->name + ")";
    }

    std::vector<Design::Id> sib = d.children(parent);
    std::erase(sib, moving);
    std::map<std::string, Value> where;
    std::set<std::string> slots, names;
    std::int64_t next_row = 0;
    for (const auto& k : sib) {
        const auto r = *d.get(k);
        slots.insert(str_of(r.pack, "slot"));
        names.insert(str_of(r.pack, "name"));
        const auto it = r.pack.find("row");
        if (it != r.pack.end() && std::holds_alternative<std::int64_t>(it->second))
            next_row = std::max(next_row, std::get<std::int64_t>(it->second) + 1);
    }
    const std::string P = "a " + pr->type;

    switch (pw->children) {
        case ChildKind::Window: {
            const bool plain = slots.count("") > 0, bar = slots.count("titlebar") > 0;
            if (type == "HeaderBar" && !bar) where["slot"] = std::string("titlebar");
            else if (!plain) {}
            else if (!bar) where["slot"] = std::string("titlebar");
            else return "the window has its child and its titlebar -- put a Box in it to hold more";
            break;
        }
        case ChildKind::Single:
            if (!sib.empty()) return P + " holds ONE child -- put a Box or Grid in it to hold more";
            break;
        case ChildKind::Paned:
            if (!slots.count("start")) where["slot"] = std::string("start");
            else if (!slots.count("end")) where["slot"] = std::string("end");
            else return P + " has its start and end children already";
            break;
        case ChildKind::HeaderBar:
            where["slot"] = std::string("start");
            break;
        case ChildKind::Grid:
            where["column"] = std::int64_t{0};
            where["row"]    = next_row;
            break;
        case ChildKind::Stack:
            for (int n = 1;; ++n) {
                const std::string nm = "page" + std::to_string(n);
                if (!names.count(nm)) {
                    where["name"]  = nm;
                    where["title"] = "Page " + std::to_string(n);
                    break;
                }
            }
            break;
        default:
            break;
    }
    if (pack) *pack = std::move(where);
    return {};
}

Design::Id add(DesignJournal& j, Design& d, const std::string& type, const Design::Id& parent,
               int index, std::string* why) {
    std::map<std::string, Value> pack;
    if (auto no = can_hold(d, parent, type, &pack); !no.empty()) { say(why, no); return {}; }
    const std::string name = d.fresh_name(type);
    Design::Id made;
    j.run("Add " + type + " " + curly(name), {}, [&] {
        made = d.fresh_key();
        Rec r;
        r.type   = type;
        r.name   = name;
        r.parent = parent;
        r.pos    = d.pos_at(parent, index);
        r.pack   = pack;
        d.put(made, r);
        return std::vector<Design::Id>{made};
    });
    return made;
}

bool remove(DesignJournal& j, Design& d, const Design::Id& id, std::string* why) {
    const auto r = d.get(id);
    if (!r || id == Design::kForm) { say(why, "nothing selected"); return false; }
    if (r->parent.empty()) { say(why, "the window is the form -- it can't be deleted"); return false; }
    const auto ids = d.subtree(id);
    return j.run("Delete " + name_of(d, id), ids, [&] {
        // Every record of the subtree is in the step, so undo brings them all back.
        for (auto it = ids.rbegin(); it != ids.rend(); ++it) d.put(*it, std::nullopt);
    });
}

bool move(DesignJournal& j, Design& d, const Design::Id& id, const Design::Id& parent, int index,
          std::string* why) {
    auto r = d.get(id);
    if (!r || id == Design::kForm) { say(why, "nothing selected"); return false; }
    if (r->parent.empty()) { say(why, "the window is the form -- it doesn't move"); return false; }
    if (d.is_under(parent, id)) { say(why, "a widget can't go inside itself"); return false; }
    if (parent != r->parent) {
        std::map<std::string, Value> pack;
        if (auto no = can_hold(d, parent, r->type, &pack, id); !no.empty()) { say(why, no); return false; }
        r->pack = std::move(pack);
    }
    r->parent = parent;
    r->pos    = d.pos_at(parent, index, id);
    return j.run("Move " + name_of(d, id), {id}, [&] { d.put(id, *r); });
}

bool move_up(DesignJournal& j, Design& d, const Design::Id& id) {
    const int i = index_in_parent(d, id);
    if (i <= 0) return false;
    return move(j, d, id, d.get(id)->parent, i - 1);
}

bool move_down(DesignJournal& j, Design& d, const Design::Id& id) {
    const int i = index_in_parent(d, id);
    if (i < 0) return false;
    const auto n = static_cast<int>(d.children(d.get(id)->parent).size());
    if (i + 1 >= n) return false;
    return move(j, d, id, d.get(id)->parent, i + 1);
}

bool rename(DesignJournal& j, Design& d, const Design::Id& id, const std::string& name,
            std::string* why) {
    auto r = d.get(id);
    if (!r || id == Design::kForm) { say(why, "nothing selected"); return false; }
    if (r->name == name) return false;
    if (!is_identifier(name) || is_cpp_keyword(name)) {
        say(why, "\"" + name + "\" isn't a C++ name (letters, digits, _; not a keyword)");
        return false;
    }
    if (!d.find(name).empty()) { say(why, "\"" + name + "\" is taken"); return false; }
    const std::string old = r->name;
    // Refs to the old name follow it (a StackSidebar's stack) -- same step.
    std::vector<Design::Id> ids{id};
    for (const auto& k : d.all()) {
        if (k == id) continue;
        const auto o = *d.get(k);
        const WidgetSpec* w = find_widget(o.type);
        if (!w) continue;
        for (const auto& [p, v] : o.props) {
            const PropSpec* ps = find_prop(*w, p);
            if (ps && ps->kind == PropKind::Ref && std::holds_alternative<std::string>(v) &&
                std::get<std::string>(v) == old) { ids.push_back(k); break; }
        }
    }
    return j.run("Rename " + curly(old), ids, [&] {
        r->name = name;
        d.put(id, *r);
        for (std::size_t i = 1; i < ids.size(); ++i) {
            auto o = *d.get(ids[i]);
            const WidgetSpec* w = find_widget(o.type);
            for (auto& [p, v] : o.props) {
                const PropSpec* ps = find_prop(*w, p);
                if (ps && ps->kind == PropKind::Ref && std::holds_alternative<std::string>(v) &&
                    std::get<std::string>(v) == old) v = name;
            }
            d.put(ids[i], o);
        }
    });
}

bool rename_form(DesignJournal& j, Design& d, const std::string& name, std::string* why) {
    auto r = d.get(Design::kForm);
    if (!r || r->name == name) return false;
    if (!is_identifier(name) || is_cpp_keyword(name)) {
        say(why, "\"" + name + "\" isn't a C++ class name");
        return false;
    }
    return j.run("Rename form " + curly(r->name), {Design::kForm}, [&] {
        r->name = name;
        d.put(Design::kForm, *r);
    });
}

std::string add_action(DesignJournal& j, Design& d, const std::string& name, std::string* why) {
    auto r = d.get(Design::kForm);
    if (!r) return {};
    auto taken = [&](const std::string& n) {
        for (const auto& a : r->actions) if (a.name == n) return true;
        return false;
    };
    std::string n = name;
    if (n.empty())
        for (int i = 1; n.empty() || taken(n); ++i) n = "action" + std::to_string(i);
    if (!is_identifier(n) || is_cpp_keyword(n)) { say(why, "\"" + n + "\" isn't a C++ name"); return {}; }
    if (taken(n)) { say(why, "the form already has an action " + n); return {}; }
    r->actions.push_back({n, "", false});
    j.run("Add action " + curly(n), {Design::kForm}, [&] { d.put(Design::kForm, *r); });
    return n;
}

bool remove_action(DesignJournal& j, Design& d, const std::string& name) {
    auto r = d.get(Design::kForm);
    if (!r) return false;
    const auto n = r->actions.size();
    std::erase_if(r->actions, [&](const ActionDef& a) { return a.name == name; });
    if (r->actions.size() == n) return false;
    // The menu rows that ran it go with it (else the form no longer validates), same step.
    const std::string gone = "win." + gio_action_name(name);
    std::vector<Design::Id> ids{Design::kForm};
    std::map<Design::Id, Rec> menus;
    for (const auto& k : d.all()) {
        auto o = *d.get(k);
        const auto m = o.props.find("menu");
        if (m == o.props.end() || !std::holds_alternative<StringList>(m->second)) continue;
        auto& rows = std::get<StringList>(m->second);
        const auto before = rows.size();
        std::erase_if(rows, [&](const std::string& row) {
            const MenuEntry e = parse_menu_entry(row);
            return !e.separator && e.action == gone;
        });
        if (rows.size() != before) { ids.push_back(k); menus[k] = o; }
    }
    return j.run("Delete action " + curly(name), ids, [&] {
        d.put(Design::kForm, *r);
        for (const auto& [k, o] : menus) d.put(k, o);
    });
}

bool set_action(DesignJournal& j, Design& d, const std::string& name, const ActionDef& now, std::string* why) {
    auto r = d.get(Design::kForm);
    if (!r) return false;
    auto it = std::find_if(r->actions.begin(), r->actions.end(), [&](const ActionDef& a) { return a.name == name; });
    if (it == r->actions.end()) return false;
    if (*it == now) return false;
    if (now.name != name) {
        if (!is_identifier(now.name) || is_cpp_keyword(now.name)) { say(why, "\"" + now.name + "\" isn't a C++ name"); return false; }
        for (const auto& a : r->actions)
            if (a.name == now.name) { say(why, "the form already has an action " + now.name); return false; }
    }
    if (!now.opens.empty() && now.opens != kShortcutsTarget &&
        (!is_identifier(now.opens) || is_cpp_keyword(now.opens))) {
        say(why, "\"" + now.opens + "\" isn't a form's name");
        return false;
    }
    // s010: typing in the key or the title folds per field; a rename, an opens
    // pick or a required tick is always its own step (as set_file does).
    const ActionDef was = *it;
    std::string merge;
    if (now.name == was.name && now.opens == was.opens && now.required == was.required) {
        if (now.accel != was.accel && now.title == was.title) merge = "action:" + name + ":accel";
        else if (now.title != was.title && now.accel == was.accel) merge = "action:" + name + ":title";
    }
    *it = now;
    // A rename carries the menu rows that name it ("Save=win.save" -> win.store).
    std::vector<Design::Id> ids{Design::kForm};
    const std::string from = "win." + gio_action_name(name), to = "win." + gio_action_name(now.name);
    std::map<Design::Id, Rec> menus;
    if (from != to)
        for (const auto& k : d.all()) {
            auto o = *d.get(k);
            const auto m = o.props.find("menu");
            if (m == o.props.end() || !std::holds_alternative<StringList>(m->second)) continue;
            bool hit = false;
            for (auto& row : std::get<StringList>(m->second)) {
                const MenuEntry e = parse_menu_entry(row);
                if (!e.separator && e.action == from) { row = e.label + "=" + to; hit = true; }
            }
            if (hit) { ids.push_back(k); menus[k] = o; }
        }
    const std::string label = now.name != name     ? "Rename action " + curly(name)
                              : now.accel != was.accel ? "Key of " + curly(name)
                              : now.title != was.title ? "Title of " + curly(name)
                              : now.opens != was.opens ? "What " + curly(name) + " opens"
                                                       : "Action " + curly(name);
    return j.run(label, ids, [&] {
        d.put(Design::kForm, *r);
        for (const auto& [k, o] : menus) d.put(k, o);
    }, merge);
}

std::string add_file(DesignJournal& j, Design& d, const std::string& name, std::string* why) {
    auto r = d.get(Design::kForm);
    if (!r) return {};
    auto taken = [&](const std::string& n) {
        for (const auto& x : r->files) if (x.name == n) return true;
        return false;
    };
    std::string n = name;
    if (n.empty())
        for (int i = 1; n.empty() || taken(n); ++i) n = "file" + std::to_string(i);
    if (!is_identifier(n) || is_cpp_keyword(n)) { say(why, "\"" + n + "\" isn't a C++ name"); return {}; }
    if (taken(n)) { say(why, "the form already has a file dialog " + n); return {}; }
    FileDef fd;
    fd.name = n;
    r->files.push_back(fd);
    j.run("Add file dialog " + curly(n), {Design::kForm}, [&] { d.put(Design::kForm, *r); });
    return n;
}

bool remove_file(DesignJournal& j, Design& d, const std::string& name) {
    auto r = d.get(Design::kForm);
    if (!r) return false;
    const auto n = r->files.size();
    std::erase_if(r->files, [&](const FileDef& x) { return x.name == name; });
    if (r->files.size() == n) return false;
    return j.run("Delete file dialog " + curly(name), {Design::kForm}, [&] { d.put(Design::kForm, *r); });
}

bool set_file(DesignJournal& j, Design& d, const std::string& name, const FileDef& now, std::string* why) {
    auto r = d.get(Design::kForm);
    if (!r) return false;
    auto it = std::find_if(r->files.begin(), r->files.end(), [&](const FileDef& x) { return x.name == name; });
    if (it == r->files.end() || *it == now) return false;
    if (now.name != name) {
        if (!is_identifier(now.name) || is_cpp_keyword(now.name)) { say(why, "\"" + now.name + "\" isn't a C++ name"); return false; }
        for (const auto& x : r->files)
            if (x.name == now.name) { say(why, "the form already has a file dialog " + now.name); return false; }
    }
    if (std::find(file_modes().begin(), file_modes().end(), now.mode) == file_modes().end()) {
        say(why, "\"" + now.mode + "\" isn't a file dialog mode (open, open_multiple, save, folder)");
        return false;
    }
    // Typing in one text field folds into one step (merge key per field); a
    // rename, a mode, a tick is always its own step.
    std::string merge;
    if (now.name == name && now.mode == it->mode && now.cancel == it->cancel && now.required == it->required) {
        const int changed = (now.title != it->title) + (now.accept != it->accept) + (now.filters != it->filters);
        if (changed == 1)
            merge = "file:" + name + (now.title != it->title ? ":title" : now.accept != it->accept ? ":accept" : ":filters");
    }
    const std::string label = now.name != name ? "Rename file dialog " + curly(name) : "File dialog " + curly(name);
    *it = now;
    return j.run(label, {Design::kForm}, [&] { d.put(Design::kForm, *r); }, merge);
}

namespace {
// The ColumnViews and ListViews (s011) whose "model" is `name`.
std::vector<Design::Id> views_of(const Design& d, const std::string& name) {
    std::vector<Design::Id> out;
    for (const auto& k : d.all()) {
        const auto o = d.get(k);
        if (!o || (o->type != "ColumnView" && o->type != "ListView")) continue;
        const auto it = o->props.find("model");
        if (it != o->props.end() && std::holds_alternative<std::string>(it->second) &&
            std::get<std::string>(it->second) == name)
            out.push_back(k);
    }
    return out;
}
}  // namespace

std::string add_model(DesignJournal& j, Design& d, const std::string& name, std::string* why) {
    auto r = d.get(Design::kForm);
    if (!r) return {};
    auto taken = [&](const std::string& n) {
        for (const auto& m : r->models) if (m.name == n) return true;
        return false;
    };
    std::string n = name;
    if (n.empty())
        for (int i = 1; n.empty() || taken(n); ++i) n = "Model" + std::to_string(i);
    if (!is_identifier(n) || is_cpp_keyword(n)) { say(why, "\"" + n + "\" isn't a C++ class name"); return {}; }
    if (taken(n)) { say(why, "the form already has a model " + n); return {}; }
    r->models.push_back({n, {{"title", "string", ""}}});
    j.run("Add model " + curly(n), {Design::kForm}, [&] { d.put(Design::kForm, *r); });
    return n;
}

bool remove_model(DesignJournal& j, Design& d, const std::string& name, std::string* why) {
    auto r = d.get(Design::kForm);
    if (!r) return false;
    if (const auto v = views_of(d, name); !v.empty()) {
        say(why, d.get(v.front())->type + " " + d.get(v.front())->name + " shows " + name +
                     " -- give it another model first");
        return false;
    }
    const auto n = r->models.size();
    std::erase_if(r->models, [&](const ModelDef& m) { return m.name == name; });
    if (r->models.size() == n) return false;
    return j.run("Delete model " + curly(name), {Design::kForm}, [&] { d.put(Design::kForm, *r); });
}

bool set_model(DesignJournal& j, Design& d, const std::string& name, const ModelDef& now, std::string* why) {
    auto r = d.get(Design::kForm);
    if (!r) return false;
    auto it = std::find_if(r->models.begin(), r->models.end(), [&](const ModelDef& m) { return m.name == name; });
    if (it == r->models.end() || *it == now) return false;
    if (now.name != name) {
        if (!is_identifier(now.name) || is_cpp_keyword(now.name)) { say(why, "\"" + now.name + "\" isn't a C++ class name"); return false; }
        for (const auto& m : r->models)
            if (m.name == now.name) { say(why, "the form already has a model " + now.name); return false; }
    }
    for (const auto& x : now.fields)
        if (std::find(field_types().begin(), field_types().end(), x.type) == field_types().end()) {
            say(why, "\"" + x.type + "\" isn't a field type (string, int, double, bool)");
            return false;
        }
    // Typing in one field's name or header folds; anything else is its own step.
    std::string merge;
    if (now.name == name && now.fields.size() == it->fields.size()) {
        int changed = 0;
        std::string key;
        for (std::size_t i = 0; i < now.fields.size(); ++i) {
            const auto& a = it->fields[i];
            const auto& b = now.fields[i];
            if (a.type != b.type) { changed = 99; break; }
            if (a.name != b.name) { ++changed; key = std::to_string(i) + ":name"; }
            if (a.title != b.title) { ++changed; key = std::to_string(i) + ":title"; }
        }
        if (changed == 1) merge = "model:" + name + ":" + key;
    }
    // A rename carries the ColumnViews that show it; a field renamed or removed
    // carries their "editable" lists (s007) -- same step, so undo is exact.
    std::map<std::string, std::string> renamed;   // old field -> new ("" = gone)
    for (std::size_t i = 0; i < it->fields.size(); ++i) {
        const auto& old = it->fields[i].name;
        std::string to;
        if (now.fields.size() == it->fields.size()) to = now.fields[i].name;
        else for (const auto& x : now.fields) if (x.name == old) to = old;
        renamed[old] = to;
    }
    std::vector<Design::Id> ids{Design::kForm};
    std::map<Design::Id, Rec> views;
    for (const auto& k : views_of(d, name)) {
        auto o = *d.get(k);
        bool changed = false;
        if (now.name != name) { o.props["model"] = Value(now.name); changed = true; }
        if (const auto e = o.props.find("editable"); e != o.props.end() && std::holds_alternative<StringList>(e->second)) {
            StringList was = std::get<StringList>(e->second), is;
            for (const auto& f : was) {
                const auto r = renamed.find(f);
                if (r == renamed.end()) is.push_back(f);           // not a field anyway: leave it to validate
                // same count = positional: kept even while its name is typed empty
                // (an Entry's select-all + type passes through ""), so it survives
                else if (!r->second.empty() || now.fields.size() == it->fields.size()) is.push_back(r->second);
            }
            if (is != was) {
                changed = true;
                if (is.empty()) o.props.erase("editable");
                else e->second = Value(is);
            }
        }
        if (changed) { ids.push_back(k); views[k] = o; }
        // s011: a ListView's row widgets "bind" fields -- a renamed field carries
        // them, a removed one unbinds them (same rule as editable above).
        if (o.type != "ListView") continue;
        std::vector<Design::Id> stack = d.children(k);
        while (!stack.empty()) {
            const Design::Id c = stack.back();
            stack.pop_back();
            for (const auto& g : d.children(c)) stack.push_back(g);
            auto w = *d.get(c);
            const auto b = w.props.find("bind");
            if (b == w.props.end() || !std::holds_alternative<std::string>(b->second)) continue;
            const auto r = renamed.find(std::get<std::string>(b->second));
            if (r == renamed.end() || r->second == std::get<std::string>(b->second)) continue;
            if (r->second.empty() && now.fields.size() != it->fields.size()) w.props.erase("bind");
            else b->second = Value(r->second);
            ids.push_back(c);
            views[c] = w;
        }
    }
    const std::string label = now.name != name ? "Rename model " + curly(name) : "Model " + curly(name);
    *it = now;
    return j.run(label, ids, [&] {
        d.put(Design::kForm, *r);
        for (const auto& [k, o] : views) d.put(k, o);
    }, merge);
}

bool set_prop(DesignJournal& j, Design& d, const Design::Id& id, const std::string& prop,
              const std::optional<Value>& v, bool fold) {
    auto r = d.get(id);
    if (!r) return false;
    const WidgetSpec* w = find_widget(r->type);
    if (!w || !find_prop(*w, prop)) return false;
    if (v) r->props[prop] = *v;
    else   r->props.erase(prop);
    return j.run(prop + " of " + curly(r->name), {id}, [&] { d.put(id, *r); },
                 fold ? "prop:" + prop + ":" + id : std::string());
}

bool set_pack(DesignJournal& j, Design& d, const Design::Id& id, const std::string& key,
              const std::optional<Value>& v) {
    auto r = d.get(id);
    if (!r || r->parent.empty()) return false;
    const WidgetSpec* pw = find_widget(d.get(r->parent)->type);
    if (!pw) return false;
    const auto keys = packing_keys(pw->children);
    if (std::find(keys.begin(), keys.end(), key) == keys.end()) return false;
    if (v) r->pack[key] = *v;
    else   r->pack.erase(key);
    return j.run(key + " of " + curly(r->name), {id}, [&] { d.put(id, *r); },
                 "pack:" + key + ":" + id);
}

bool tick(DesignJournal& j, Design& d, const Design::Id& id, const std::string& signal, bool on) {
    auto r = d.get(id);
    if (!r) return false;
    const WidgetSpec* w = find_widget(r->type);
    if (!w || !find_signal(*w, signal)) return false;
    auto it = std::find_if(r->signals.begin(), r->signals.end(),
                           [&](const SignalHook& h) { return h.signal == signal; });
    if (on == (it != r->signals.end())) return false;
    if (on) r->signals.push_back({signal, false, ""});
    else    r->signals.erase(it);
    return j.run((on ? "Hook " : "Unhook ") + curly(r->name) + " " + signal, {id},
                 [&] { d.put(id, *r); });
}

bool require(DesignJournal& j, Design& d, const Design::Id& id, const std::string& signal, bool on) {
    auto r = d.get(id);
    if (!r) return false;
    const WidgetSpec* w = find_widget(r->type);
    if (!w || !find_signal(*w, signal)) return false;
    auto it = std::find_if(r->signals.begin(), r->signals.end(),
                           [&](const SignalHook& h) { return h.signal == signal; });
    if (it == r->signals.end()) {
        if (!on) return false;
        r->signals.push_back({signal, true, ""});   // requiring ticks it too
    } else {
        if (it->required == on) return false;
        it->required = on;
    }
    return j.run((on ? "Require " : "Don't require ") + curly(r->name) + " " + signal, {id},
                 [&] { d.put(id, *r); });
}

Zone zone_at(double y, int height, bool container, bool root) {
    if (root) return Zone::Inside;
    if (height <= 0) return Zone::After;   // an unrealised row divides into nonsense
    if (container) {
        if (y < height * 0.25) return Zone::Before;
        if (y > height * 0.75) return Zone::After;
        return Zone::Inside;
    }
    return y < height * 0.5 ? Zone::Before : Zone::After;
}

Landing landing(const Design& d, const std::string& type, const Design::Id& moving,
                const Design::Id& target, Zone z) {
    Landing out;
    const auto t = d.get(target);
    if (!t || target == Design::kForm) { out.why = "nowhere to drop"; return out; }
    if (!moving.empty()) {
        if (target == moving) { out.why = "that's where it is"; return out; }
        if (d.is_under(target, moving)) { out.why = "a widget can't go inside itself"; return out; }
        if (d.get(moving) && d.get(moving)->parent.empty()) { out.why = "the window is the form -- it doesn't move"; return out; }
    }
    Design::Id parent;
    int index = -1;
    if (z == Zone::Inside) {
        parent = target;
    } else {
        if (t->parent.empty()) { out.why = "nothing goes beside the window"; return out; }
        parent = t->parent;
        auto sib = d.children(parent);
        std::erase(sib, moving);
        for (std::size_t i = 0; i < sib.size(); ++i)
            if (sib[i] == target) index = static_cast<int>(i) + (z == Zone::After ? 1 : 0);
    }
    // Staying in the same parent only reorders: no packing question.
    const bool same = !moving.empty() && d.get(moving) && d.get(moving)->parent == parent;
    if (!same) {
        if (auto no = can_hold(d, parent, type, nullptr, moving); !no.empty()) { out.why = no; return out; }
    }
    out.parent = parent;
    out.index = index;
    return out;
}

Landing landing_on(const Design& d, const std::string& type, const Design::Id& moving,
                   const Design::Id& target) {
    Landing in = landing(d, type, moving, target, Zone::Inside);
    if (!in.parent.empty()) return in;
    const auto t = d.get(target);
    if (t && t->parent.empty()) return in;   // the window: into, or nothing
    Landing after = landing(d, type, moving, target, Zone::After);
    if (!after.parent.empty()) return after;
    const WidgetSpec* w = t ? find_widget(t->type) : nullptr;
    return (w && w->children != ChildKind::None) ? in : after;   // the more telling reason
}

Design::Id neighbour(const Design& d, const Design::Id& id) {
    const auto r = d.get(id);
    if (!r) return {};
    const auto sib = d.children(r->parent);
    const auto it = std::find(sib.begin(), sib.end(), id);
    if (it != sib.end() && it + 1 != sib.end()) return *(it + 1);
    if (it != sib.end() && it != sib.begin()) return *(it - 1);
    return r->parent;
}

Form fixture() {
    using S = std::string;
    Node hint{"Label", "hint", {{"label", Value(S("Hint"))}}, {}, {}, {}};
    Node ok{"Button", "ok", {{"label", Value(S("OK"))}}, {}, {{"clicked", false, ""}}, {}};
    Node cancel{"Button", "cancel", {{"label", Value(S("Cancel"))}}, {}, {}, {}};
    Node page_a{"Label", "page_a", {}, {{"name", Value(S("a"))}, {"title", Value(S("A"))}}, {}, {}};
    Node pages{"Stack", "pages", {}, {}, {}, {page_a}};
    Node side{"StackSidebar", "side", {{"stack", Value(S("pages"))}}, {}, {}, {}};
    Node cell{"Label", "cell", {}, {{"column", Value(std::int64_t{0})}, {"row", Value(std::int64_t{0})}}, {}, {}};
    Node grid{"Grid", "grid", {}, {}, {}, {cell}};
    Node body{"Box", "body", {{"orientation", Value(S("vertical"))}}, {}, {},
              {ok, cancel, hint, side, pages, grid}};
    Node menu{"MenuButton", "menu", {{"menu", Value(StringList{"Save=win.save", "-", "Quit=app.quit"})}},
              {{"slot", Value(S("end"))}}, {}, {}};
    Node bar{"HeaderBar", "bar", {}, {{"slot", Value(S("titlebar"))}}, {}, {menu}};
    Node win{"ApplicationWindow", "win", {{"title", Value(S("Fixture"))}}, {},
             {{"close_request", false, ""}}, {bar, body}};
    FileDef open_doc;
    open_doc.name = "open_doc";
    open_doc.filters = {"Text=*.txt;*.md"};
    return Form{"Fixture", win, {{"save", "<Control>s", false}}, {open_doc},
                {{"Note", {{"title", "string", ""}, {"done", "bool", ""}}}}};
}

const std::vector<Verb>& verbs() {
    using S = std::string;
    static const std::vector<Verb> table = {
        {"add",          [](DesignJournal& j, Design& d) { add(j, d, "Button", d.find("body"), 1); }},
        {"add_page",     [](DesignJournal& j, Design& d) { add(j, d, "Label", d.find("pages")); }},
        {"add_grid",     [](DesignJournal& j, Design& d) { add(j, d, "Entry", d.find("grid")); }},
        {"remove",       [](DesignJournal& j, Design& d) { remove(j, d, d.find("ok")); }},
        {"remove_tree",  [](DesignJournal& j, Design& d) { remove(j, d, d.find("body")); }},
        {"move",         [](DesignJournal& j, Design& d) { move(j, d, d.find("cancel"), d.find("grid"), -1); }},
        {"move_up",      [](DesignJournal& j, Design& d) { move_up(j, d, d.find("cancel")); }},
        {"move_down",    [](DesignJournal& j, Design& d) { move_down(j, d, d.find("ok")); }},
        {"rename",       [](DesignJournal& j, Design& d) { rename(j, d, d.find("pages"), "book"); }},
        {"rename_form",  [](DesignJournal& j, Design& d) { rename_form(j, d, "Renamed"); }},
        {"set_prop",     [](DesignJournal& j, Design& d) { set_prop(j, d, d.find("ok"), "label", Value(S("Go"))); }},
        {"clear_prop",   [](DesignJournal& j, Design& d) { set_prop(j, d, d.find("ok"), "label", std::nullopt); }},
        {"set_pack",     [](DesignJournal& j, Design& d) { set_pack(j, d, d.find("cell"), "column", Value(std::int64_t{2})); }},
        {"tick",         [](DesignJournal& j, Design& d) { tick(j, d, d.find("cancel"), "clicked", true); }},
        {"untick",       [](DesignJournal& j, Design& d) { tick(j, d, d.find("ok"), "clicked", false); }},
        {"require",      [](DesignJournal& j, Design& d) { require(j, d, d.find("ok"), "clicked", true); }},
        {"add_action",   [](DesignJournal& j, Design& d) { add_action(j, d, "refresh"); }},
        {"remove_action",[](DesignJournal& j, Design& d) { remove_action(j, d, "save"); }},
        {"rename_action",[](DesignJournal& j, Design& d) { set_action(j, d, "save", {"store", "<Control>s", false}); }},
        {"key_action",   [](DesignJournal& j, Design& d) { set_action(j, d, "save", {"save", "<Control><Shift>s", true}); }},
        {"opens_action", [](DesignJournal& j, Design& d) { set_action(j, d, "save", {"save", "<Control>s", false, "", "@shortcuts"}); }},
        {"title_action", [](DesignJournal& j, Design& d) { set_action(j, d, "save", {"save", "<Control>s", false, "Save it"}); }},
        {"add_file",     [](DesignJournal& j, Design& d) { add_file(j, d, "export_pdf"); }},
        {"add_model",    [](DesignJournal& j, Design& d) { add_model(j, d, "Patient"); }},
        {"remove_model", [](DesignJournal& j, Design& d) { remove_model(j, d, "Note"); }},
        {"rename_model", [](DesignJournal& j, Design& d) {
            set_model(j, d, "Note", {"Task", {{"title", "string", ""}, {"done", "bool", ""}}}); }},
        {"field_model",  [](DesignJournal& j, Design& d) {
            set_model(j, d, "Note", {"Note", {{"title", "string", "Name"}, {"done", "bool", ""}, {"n", "int", ""}}}); }},
        {"remove_file",  [](DesignJournal& j, Design& d) { remove_file(j, d, "open_doc"); }},
        {"rename_file",  [](DesignJournal& j, Design& d) {
            FileDef x; x.name = "load"; x.filters = {"Text=*.txt;*.md"};
            set_file(j, d, "open_doc", x); }},
        {"mode_file",    [](DesignJournal& j, Design& d) {
            FileDef x; x.name = "open_doc"; x.mode = "save"; x.title = "Save As"; x.cancel = true; x.required = true;
            set_file(j, d, "open_doc", x); }},
    };
    return table;
}

}  // namespace design
}  // namespace mortimer::core
