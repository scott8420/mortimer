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
    put(kForm, Rec{"", f.name, "", 0, {}, {}, {}});
    std::function<void(const Node&, const Id&, double)> add = [&](const Node& n, const Id& parent, double pos) {
        const Id key = fresh_key();
        put(key, Rec{n.type, n.id, parent, pos, n.props, n.pack, n.signals});
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
    return Form{form_name(), build(r)};
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

bool set_prop(DesignJournal& j, Design& d, const Design::Id& id, const std::string& prop,
              const std::optional<Value>& v) {
    auto r = d.get(id);
    if (!r) return false;
    const WidgetSpec* w = find_widget(r->type);
    if (!w || !find_prop(*w, prop)) return false;
    if (v) r->props[prop] = *v;
    else   r->props.erase(prop);
    return j.run(prop + " of " + curly(r->name), {id}, [&] { d.put(id, *r); },
                 "prop:" + prop + ":" + id);
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
    Node bar{"HeaderBar", "bar", {}, {{"slot", Value(S("titlebar"))}}, {}, {}};
    Node win{"ApplicationWindow", "win", {{"title", Value(S("Fixture"))}}, {},
             {{"close_request", false, ""}}, {bar, body}};
    return Form{"Fixture", win};
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
    };
    return table;
}

}  // namespace design
}  // namespace mortimer::core
