#include "core/UiFile.hpp"
#include "core/Xml.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <stdexcept>
#include <charconv>
#include <functional>
#include <map>
#include <set>
#include <sstream>

namespace mortimer::core {

namespace {

std::string dashes(std::string s) {
    for (auto& c : s) if (c == '_') c = '-';
    return s;
}

std::string xml_escape(const std::string& s) {
    std::string o;
    for (char c : s) {
        switch (c) {
            case '&': o += "&amp;"; break;
            case '<': o += "&lt;"; break;
            case '>': o += "&gt;"; break;
            case '"': o += "&quot;"; break;
            default: o += c;
        }
    }
    return o;
}

// The setter's own name: "set_placeholder_text({v})" -> "placeholder_text",
// "property_default_width().set_value({v})" -> "default_width".
std::string setter_name(const std::string& setter, const std::string& lead) {
    if (setter.rfind(lead, 0) != 0) return {};
    const auto paren = setter.find('(', lead.size());
    return paren == std::string::npos ? std::string() : setter.substr(lead.size(), paren - lead.size());
}

// The few GTK spells differently from the gtkmm setter.
const std::map<std::string, std::string>& renamed() {
    static const std::map<std::string, std::string> m{
        {"from_icon_name", "icon-name"},     // Image
        {"label_align", "label-xalign"},     // Frame
        {"step_increment", "step-increment"},
        {"page_increment", "page-increment"},
    };
    return m;
}

// Text inside <!-- -->: XML forbids "--" there.
std::string comment_safe(std::string s) {
    for (std::size_t at; (at = s.find("--")) != std::string::npos;) s.replace(at, 2, "-");
    return s;
}

std::string value_text(const PropSpec& p, const Value& v) {
    if (const auto* b = std::get_if<bool>(&v)) return *b ? "True" : "False";
    if (const auto* i = std::get_if<std::int64_t>(&v)) return std::to_string(*i);
    if (const auto* d = std::get_if<double>(&v)) {
        char buf[64];
        const auto r = std::to_chars(buf, buf + sizeof buf, *d);
        return std::string(buf, r.ptr);
    }
    if (const auto* s = std::get_if<std::string>(&v)) return p.kind == PropKind::Enum ? dashes(*s) : *s;
    return {};
}

// A prop's value from its default text (an `always` prop the form leaves out).
Value default_value(const PropSpec& p) {
    switch (p.kind) {
        case PropKind::Bool: return p.def == "true";
        case PropKind::Int: return static_cast<std::int64_t>(std::stoll(p.def.empty() ? "0" : p.def));
        case PropKind::Double: return std::stod(p.def.empty() ? "0" : p.def);
        default: return p.def;
    }
}

struct Writer {
    const Form& f;
    std::ostringstream out;
    std::vector<std::string> menus;   // top-level <menu>s, written after the window
    std::vector<std::string> left;

    void line(int depth, const std::string& s) { out << std::string(depth * 2, ' ') << s << "\n"; }
    void prop(int depth, const std::string& name, const std::string& text) {
        line(depth, "<property name=\"" + name + "\">" + xml_escape(text) + "</property>");
    }

    // `inside`: more lines in the object, before its close (a Grid child's <layout>).
    void object(const Node& n, int depth, const std::function<void(int)>& inside = {});
    void children(const Node& n, const WidgetSpec& w, int depth);
    void menu(const Node& n, const StringList& rows);
};

void Writer::menu(const Node& n, const StringList& rows) {
    std::ostringstream m;
    m << "  <menu id=\"" << n.id << "_menu\">\n    <section>\n";
    for (const auto& r : rows) {
        const auto e = parse_menu_entry(r);
        if (e.separator) { m << "    </section>\n    <section>\n"; continue; }
        m << "      <item>\n        <attribute name=\"label\">" << xml_escape(e.label) << "</attribute>\n";
        if (!e.action.empty()) m << "        <attribute name=\"action\">" << xml_escape(e.action) << "</attribute>\n";
        m << "      </item>\n";
    }
    m << "    </section>\n  </menu>\n";
    menus.push_back(m.str());
}

void Writer::object(const Node& n, int depth, const std::function<void(int)>& inside) {
    const WidgetSpec* w = find_widget(n.type);
    if (!w) {
        left.push_back(n.id + ": a " + n.type + " (not in the catalog) -- left out");
        return;
    }
    if (!n.cls.empty())
        left.push_back(n.id + ": promoted to " + n.cls + " -- exported as its base, " + builder_class(*w) +
                       " (a .ui can only name a class GTK has registered)");
    line(depth, "<object class=\"" + builder_class(*w) + "\" id=\"" + n.id + "\">");
    const int in = depth + 1;

    // The widget's own props, then the common ones -- in catalog order, so the
    // output is stable. Grouped ones (an adjustment, a buffer, a dialog) last.
    std::vector<std::pair<std::string, std::string>> adj, buf, dlg;
    auto each = [&](const PropSpec& p) {
        auto it = n.props.find(p.name);
        Value v;
        if (it != n.props.end()) v = it->second;
        else if (p.always) v = default_value(p);
        else return;
        const auto bp = builder_prop(*w, p);
        switch (bp.how) {
            case BuilderProp::How::Plain: prop(in, bp.name, value_text(p, v)); break;
            case BuilderProp::How::Adjustment: adj.push_back({bp.name, value_text(p, v)}); break;
            case BuilderProp::How::Buffer: buf.push_back({bp.name, value_text(p, v)}); break;
            case BuilderProp::How::Dialog: dlg.push_back({bp.name, value_text(p, v)}); break;
            case BuilderProp::How::Margins:
                // A side the form sets itself is written by its own prop.
                for (const auto& [side, own] : {std::pair{"margin-start", "margin_start"}, {"margin-end", "margin_end"},
                                                {"margin-top", "margin_top"}, {"margin-bottom", "margin_bottom"}})
                    if (!n.props.count(own)) prop(in, side, value_text(p, v));
                break;
            case BuilderProp::How::Css:
                if (const auto* l = std::get_if<StringList>(&v); l && !l->empty()) {
                    line(in, "<style>");
                    for (const auto& c : *l) line(in + 1, "<class name=\"" + xml_escape(c) + "\"/>");
                    line(in, "</style>");
                }
                break;
            case BuilderProp::How::Strings:
                if (const auto* l = std::get_if<StringList>(&v)) {
                    line(in, "<property name=\"" + bp.name + "\">");
                    line(in + 1, "<object class=\"GtkStringList\">");
                    line(in + 2, "<items>");
                    for (const auto& s : *l) line(in + 3, "<item>" + xml_escape(s) + "</item>");
                    line(in + 2, "</items>");
                    line(in + 1, "</object>");
                    line(in, "</property>");
                }
                break;
            case BuilderProp::How::Menu:
                if (const auto* l = std::get_if<StringList>(&v); l && !l->empty()) {
                    menu(n, *l);
                    prop(in, "menu-model", n.id + "_menu");
                }
                break;
            case BuilderProp::How::Skip:
                left.push_back(n.id + "." + p.name + ": " + bp.name);
                break;
        }
    };
    for (const auto& p : w->props) each(p);
    for (const auto& p : common_props()) each(p);
    auto group = [&](const char* prop_name, const char* cls, const std::vector<std::pair<std::string, std::string>>& g) {
        if (g.empty()) return;
        line(in, std::string("<property name=\"") + prop_name + "\">");
        line(in + 1, std::string("<object class=\"") + cls + "\">");
        for (const auto& [k, v] : g) prop(in + 2, k, v);
        line(in + 1, "</object>");
        line(in, "</property>");
    };
    group("adjustment", "GtkAdjustment", adj);
    group("buffer", "GtkTextBuffer", buf);
    if (n.type == "ColorDialogButton") group("dialog", "GtkColorDialog", dlg.empty() ? decltype(dlg){{"modal", "True"}} : dlg);
    if (n.type == "FontDialogButton") group("dialog", "GtkFontDialog", dlg.empty() ? decltype(dlg){{"modal", "True"}} : dlg);

    for (const auto& h : n.signals) {
        const SignalSpec* s = find_signal(*w, h.signal);
        const std::string name = s ? builder_signal(*s) : std::string();
        if (name.empty()) {
            left.push_back(n.id + ": the " + h.signal + " hook -- not a signal of the " + n.type + " itself");
            continue;
        }
        line(in, "<signal name=\"" + name + "\" handler=\"" + hook_name(n, h) + "\"/>");
    }
    children(n, *w, in);
    if (inside) inside(in);
    line(depth, "</object>");
}

// Where a child of each kind of container goes.
void Writer::children(const Node& n, const WidgetSpec& w, int in) {
    auto pack_str = [](const Node& c, const char* k) {
        const auto it = c.pack.find(k);
        return it != c.pack.end() && std::holds_alternative<std::string>(it->second) ? std::get<std::string>(it->second)
                                                                                    : std::string();
    };
    auto pack_int = [](const Node& c, const char* k, std::int64_t d) {
        const auto it = c.pack.find(k);
        return it != c.pack.end() && std::holds_alternative<std::int64_t>(it->second) ? std::get<std::int64_t>(it->second) : d;
    };
    auto as_prop = [&](const std::string& name, const Node& c) {
        line(in, "<property name=\"" + name + "\">");
        object(c, in + 1);
        line(in, "</property>");
    };
    auto as_child = [&](const Node& c, const std::string& type = {}) {
        line(in, type.empty() ? "<child>" : "<child type=\"" + type + "\">");
        object(c, in + 1);
        line(in, "</child>");
    };
    for (const auto& c : n.children) {
        switch (w.children) {
            case ChildKind::None: break;
            case ChildKind::Single: as_prop("child", c); break;
            case ChildKind::Window:
                if (pack_str(c, "slot") == "titlebar") as_child(c, "titlebar");
                else as_prop("child", c);
                break;
            case ChildKind::Box:
            case ChildKind::ListBox: as_child(c); break;
            case ChildKind::Grid: {
                line(in, "<child>");
                object(c, in + 1, [&](int at) {   // the cell: a <layout> inside the child's object
                    line(at, "<layout>");
                    prop(at + 1, "column", std::to_string(pack_int(c, "column", 0)));
                    prop(at + 1, "row", std::to_string(pack_int(c, "row", 0)));
                    if (pack_int(c, "width", 1) != 1) prop(at + 1, "column-span", std::to_string(pack_int(c, "width", 1)));
                    if (pack_int(c, "height", 1) != 1) prop(at + 1, "row-span", std::to_string(pack_int(c, "height", 1)));
                    line(at, "</layout>");
                });
                line(in, "</child>");
                break;
            }
            case ChildKind::Paned: {
                const std::string side = pack_str(c, "slot") == "end" ? "end" : "start";
                as_prop(side + "-child", c);
                if (const auto r = c.pack.find("resize"); r != c.pack.end() && std::holds_alternative<bool>(r->second))
                    prop(in, "resize-" + side + "-child", std::get<bool>(r->second) ? "True" : "False");
                if (const auto r = c.pack.find("shrink"); r != c.pack.end() && std::holds_alternative<bool>(r->second))
                    prop(in, "shrink-" + side + "-child", std::get<bool>(r->second) ? "True" : "False");
                break;
            }
            case ChildKind::Stack:
                line(in, "<child>");
                line(in + 1, "<object class=\"GtkStackPage\">");
                prop(in + 2, "name", pack_str(c, "name").empty() ? c.id : pack_str(c, "name"));
                if (!pack_str(c, "title").empty()) prop(in + 2, "title", pack_str(c, "title"));
                if (!pack_str(c, "icon_name").empty()) prop(in + 2, "icon-name", pack_str(c, "icon_name"));
                line(in + 2, "<property name=\"child\">");
                object(c, in + 3);
                line(in + 2, "</property>");
                line(in + 1, "</object>");
                line(in, "</child>");
                break;
            case ChildKind::Notebook:
                line(in, "<child>");
                line(in + 1, "<object class=\"GtkNotebookPage\">");
                if (!pack_str(c, "title").empty()) prop(in + 2, "tab-label", pack_str(c, "title"));
                line(in + 2, "<property name=\"child\">");
                object(c, in + 3);
                line(in + 2, "</property>");
                line(in + 1, "</object>");
                line(in, "</child>");
                break;
            case ChildKind::HeaderBar: {
                const std::string slot = pack_str(c, "slot");
                if (slot == "title") as_prop("title-widget", c);
                else as_child(c, slot == "end" ? "end" : "start");
                break;
            }
            case ChildKind::Overlay:
                if (pack_str(c, "slot") == "overlay") as_child(c, "overlay");
                else as_prop("child", c);
                break;
            case ChildKind::CenterBox: {
                const std::string slot = pack_str(c, "slot");
                as_prop((slot == "center" ? "center" : slot == "end" ? "end" : "start") + std::string("-widget"), c);
                break;
            }
            case ChildKind::Row:
                left.push_back(n.id + ": its row (" + c.id + ") -- a ListView's row is built by a factory in code; "
                                      "the .ui has the view, not the row");
                break;
        }
    }
}

}  // namespace

std::string builder_class(const WidgetSpec& w) {
    std::string c = w.gtk_class;
    if (c.rfind("Gtk::", 0) == 0) c = "Gtk" + c.substr(5);
    return c;
}

BuilderProp builder_prop(const WidgetSpec& w, const PropSpec& p) {
    using H = BuilderProp::How;
    const std::string& s = p.setter;
    if (p.name == "margin") return {H::Margins, ""};
    if (s == "{menu}" || s.find("{menu}") != std::string::npos) return {H::Menu, "menu-model"};
    if (s.find("{item}") != std::string::npos && p.name == "css_classes") return {H::Css, ""};
    if (s.find("Gtk::StringList::create") != std::string::npos) return {H::Strings, "model"};
    if (s.find("Pango::FontDescription") != std::string::npos)
        return {H::Skip, "a font -- GtkBuilder (4.10) can't read a font description from text; the generated code sets it"};
    if (!s.empty() && s.front() == '{') {
        if (p.name == "remember") return {H::Skip, "window state -- Mortimer's own (the generated code keeps it)"};
        if (p.name == "bind") return {H::Skip, "a row binding -- the generated factory's"};
        return {H::Skip, "the model wiring -- generated code (a .ui has no models)"};
    }
    auto named = [&](std::string x) {
        if (const auto r = renamed().find(x); r != renamed().end()) return r->second;
        return dashes(x);
    };
    if (auto x = setter_name(s, "get_adjustment()->set_"); !x.empty()) return {H::Adjustment, named(x)};
    // A ranged widget's value lives on its adjustment too.
    bool ranged = false;
    for (const auto& q : w.props) ranged = ranged || q.name == "lower";
    if (ranged && s == "set_value({v})") return {H::Adjustment, "value"};
    if (auto x = setter_name(s, "get_buffer()->set_"); !x.empty()) return {H::Buffer, named(x)};
    if (auto x = setter_name(s, "get_dialog()->set_"); !x.empty()) return {H::Dialog, named(x)};
    if (auto x = setter_name(s, "property_"); !x.empty()) return {H::Plain, named(x)};
    if (auto x = setter_name(s, "set_"); !x.empty()) return {H::Plain, named(x)};
    return {H::Skip, "no .ui property for it (setter " + s + ")"};
}

std::string builder_signal(const SignalSpec& s) {
    const std::string& c = s.connect;
    if (c.rfind("signal_", 0) == 0 && c.size() > 9 && c.substr(c.size() - 2) == "()")
        return dashes(c.substr(7, c.size() - 9));
    if (c.rfind("property_", 0) == 0) {
        const auto paren = c.find("()");
        if (paren != std::string::npos && c.substr(paren) == "().signal_changed()")
            return "notify::" + dashes(c.substr(9, paren - 9));
    }
    return {};
}

UiExport export_ui(const Form& f) {
    Writer wr{f, {}, {}, {}};
    wr.object(f.root, 1);
    std::string body = wr.out.str();

    for (const auto& a : f.actions)
        wr.left.push_back("action " + a.name + (a.accel.empty() ? "" : " (" + a.accel + ")") +
                          " -- a .ui has no actions; the generated code makes win." + gio_action_name(a.name));
    for (const auto& d : f.files) wr.left.push_back("file dialog " + d.name + " -- made in code (Gtk::FileDialog)");
    for (const auto& m : f.models) wr.left.push_back("model " + m.name + " -- a .ui has no row types");
    if (!f.keys.empty()) wr.left.push_back("the shortcuts window's own rows (" + std::to_string(f.keys.size()) + ")");

    std::ostringstream x;
    x << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      << "<!-- " << f.name << ", exported by Mortimer from forms/" << f.name << ".mort.\n"
      << "     The form is the source: this is a copy for GtkBuilder, Glade or Cambalache.\n";
    if (!wr.left.empty()) {
        x << "     Left out (a .ui can't say them):\n";
        for (const auto& l : wr.left) x << "       - " << comment_safe(l) << "\n";
    }
    x << "-->\n"
      << "<interface>\n"
      << "  <requires lib=\"gtk\" version=\"4.10\"/>\n"
      << body;
    for (const auto& m : wr.menus) x << m;
    x << "</interface>\n";
    return {x.str(), wr.left};
}

// ── s024 m4: import ─────────────────────────────────────────────────────────
// The export table, run backwards: a .ui's class -> the catalog type whose
// builder_class() it is, a property -> the catalog prop whose builder_prop()
// names it. What the catalog doesn't know is left out, and NAMED.

namespace {

bool ui_bool(const std::string& t) {
    std::string l;
    for (char c : t) l += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return l == "true" || l == "yes" || l == "1" || l == "t" || l == "y";
}

std::string trimmed(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    return s.substr(b, s.find_last_not_of(" \t\r\n") - b + 1);
}

struct Importer {
    std::map<std::string, StringList> menus;   // <menu id> -> its rows
    std::vector<std::string> left;
    std::set<std::string> used;                 // ids given out
    std::map<std::string, std::string> renamed; // the .ui's id -> the form's
    std::map<std::string, int> counters;        // "button" -> 3 (button1, button2...)

    // A C++ identifier, unique in the form. A .ui id may be anything ("main-box",
    // "1st", "class"); none at all gets the type's name and a number (button1).
    std::string give_id(const std::string& raw, const std::string& type) {
        std::string id;
        for (char c : raw) id += std::isalnum(static_cast<unsigned char>(c)) ? c : '_';
        if (!id.empty() && std::isdigit(static_cast<unsigned char>(id[0]))) id = "w_" + id;
        if (!id.empty() && is_cpp_keyword(id)) id += "_";
        if (id.empty() || used.count(id)) {
            const std::string base = id.empty() ? snake(type) : id;
            do id = base + std::to_string(++counters[base]); while (used.count(id));
        }
        used.insert(id);
        if (!raw.empty()) renamed.emplace(raw, id);
        return id;
    }

    // One <property name="..."> on a widget, by the export table backwards.
    const PropSpec* find_by_builder(const WidgetSpec& w, const std::string& name, BuilderProp::How how) {
        for (const auto* list : {&w.props, &common_props()})
            for (const auto& p : *list) {
                const auto bp = builder_prop(w, p);
                if (bp.how == how && bp.name == name) return &p;
            }
        return nullptr;
    }
    bool set_value(Node& n, const PropSpec& p, const std::string& raw, const std::string& where) {
        const std::string t = trimmed(raw);
        try {
            switch (p.kind) {
                case PropKind::Bool: n.props[p.name] = ui_bool(t); return true;
                case PropKind::Int: n.props[p.name] = static_cast<std::int64_t>(std::stoll(t)); return true;
                case PropKind::Double: n.props[p.name] = std::stod(t); return true;
                case PropKind::Enum: {
                    std::string v;
                    for (char c : t) v += c == '-' ? '_' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    for (const auto& [k, cpp] : p.values)
                        if (k == v) { n.props[p.name] = v; return true; }
                    left.push_back(where + ": \"" + t + "\" isn't one Mortimer offers for " + p.name);
                    return false;
                }
                default: n.props[p.name] = raw; return true;   // String, Ref (re-pointed at the end)
            }
        } catch (const std::exception&) {
            left.push_back(where + ": \"" + t + "\" isn't a number");
            return false;
        }
    }

    const WidgetSpec* spec_of(const std::string& cls, bool root, const XmlNode& o) {
        if (root) {
            if (cls == "GtkApplicationWindow") return find_widget("ApplicationWindow");
            if (cls == "GtkWindow") {   // a modal one that hides on close is a dialog
                bool modal = false, hides = false;
                for (const auto& c : o.children)
                    if (c.name == "property") {
                        if (c.attr("name") == "modal") modal = ui_bool(c.text);
                        if (c.attr("name") == "hide-on-close") hides = ui_bool(c.text);
                    }
                return find_widget(modal && hides ? "Dialog" : "Window");
            }
            return nullptr;
        }
        for (const auto& w : catalog())
            if (!w.root && builder_class(w) == cls) return &w;
        return nullptr;
    }

    // A <child>'s object, with how it sits in `parent` (its pack) decided here.
    void add_child(Node& parent, const WidgetSpec& pw, const XmlNode& child_el, const std::string& type);
    std::optional<Node> object(const XmlNode& o, bool root, const WidgetSpec* parent = nullptr);
};

std::optional<Node> Importer::object(const XmlNode& o, bool root, const WidgetSpec* parent) {
    const std::string cls = o.name == "template" ? o.attr("parent") : o.attr("class");
    const WidgetSpec* w = spec_of(cls, root, o);
    const std::string raw_id = o.name == "template" ? snake(o.attr("class")) : o.attr("id");
    if (!w) {
        left.push_back((raw_id.empty() ? cls : raw_id) + ": a " + cls +
                       " -- not in Mortimer's catalog; left out, with what was inside it");
        return std::nullopt;
    }
    (void)parent;
    Node n;
    n.type = w->type;
    n.id = give_id(raw_id, w->type);
    std::map<std::string, std::string> margins;
    std::map<std::string, bool> paned_pack;   // "resize-start-child" -> true, given to that child after
    for (const auto& c : o.children) {
        const std::string where = n.id + "." + c.attr("name");
        if (c.name == "property") {
            const std::string name = c.attr("name");
            const XmlNode* inner = nullptr;
            for (const auto& k : c.children) if (k.name == "object") inner = &k;
            if (inner) {
                const std::string icls = inner->attr("class");
                auto grouped = [&](BuilderProp::How how) {
                    for (const auto& ip : inner->children) {
                        if (ip.name != "property") continue;
                        if (how == BuilderProp::How::Dialog && ip.attr("name") == "modal") continue;   // export's own
                        if (const auto* p = find_by_builder(*w, ip.attr("name"), how))
                            set_value(n, *p, ip.text, n.id + "." + p->name);
                        else left.push_back(n.id + "." + name + "." + ip.attr("name") + ": not in Mortimer's catalog");
                    }
                };
                if (name == "adjustment" && icls == "GtkAdjustment") grouped(BuilderProp::How::Adjustment);
                else if (name == "buffer" && icls == "GtkTextBuffer") grouped(BuilderProp::How::Buffer);
                else if (name == "dialog") grouped(BuilderProp::How::Dialog);
                else if (name == "model" && icls == "GtkStringList") {
                    StringList items;
                    for (const auto& k : inner->children)
                        if (k.name == "items") for (const auto& it : k.children) if (it.name == "item") items.push_back(it.text);
                    if (const auto* p = find_by_builder(*w, "model", BuilderProp::How::Strings)) n.props[p->name] = items;
                    else left.push_back(where + ": a string list -- " + n.type + " has none in Mortimer");
                } else if (w->children != ChildKind::None) {
                    add_child(n, *w, c, "property:" + name);
                } else {
                    left.push_back(where + ": a " + icls + " -- not something Mortimer sets on a " + n.type);
                }
                continue;
            }
            if (name.rfind("margin-", 0) == 0) { margins[name] = c.text; continue; }
            if (name == "menu-model") {
                const auto m = menus.find(trimmed(c.text));
                if (m != menus.end()) {
                    if (const auto* p = find_by_builder(*w, "menu-model", BuilderProp::How::Menu)) n.props[p->name] = m->second;
                } else left.push_back(where + ": menu " + trimmed(c.text) + " isn't in the file");
                continue;
            }
            if (w->children == ChildKind::Paned && (name.rfind("resize-", 0) == 0 || name.rfind("shrink-", 0) == 0)) {
                paned_pack[name] = ui_bool(c.text);
                continue;
            }
            if (const auto* p = find_by_builder(*w, name, BuilderProp::How::Plain)) set_value(n, *p, c.text, where);
            else left.push_back(where + ": not in Mortimer's catalog for a " + n.type);
        } else if (c.name == "signal") {
            const std::string sig = c.attr("name"), handler = c.attr("handler");
            const SignalSpec* s = nullptr;
            for (const auto& x : w->signals) if (builder_signal(x) == sig) s = &x;
            if (!s) { left.push_back(n.id + ": signal " + sig + " -- Mortimer has no hook for it on a " + n.type); continue; }
            SignalHook h{s->name, false, ""};
            if (handler != "on_" + n.id + "_" + s->name && is_identifier(handler) && !is_cpp_keyword(handler)) h.handler = handler;
            n.signals.push_back(h);
        } else if (c.name == "style") {
            StringList classes;
            for (const auto& k : c.children) if (k.name == "class") classes.push_back(k.attr("name"));
            if (!classes.empty()) n.props["css_classes"] = classes;
        } else if (c.name == "child") {
            add_child(n, *w, c, c.attr("type"));
        } else if (c.name == "layout") {
            for (const auto& k : c.children) {
                if (k.name != "property") continue;
                const std::string ln = k.attr("name");
                const char* key = ln == "column" ? "column" : ln == "row" ? "row" : ln == "column-span" ? "width"
                                : ln == "row-span" ? "height" : nullptr;
                try {
                    if (key) n.pack[key] = static_cast<std::int64_t>(std::stoll(trimmed(k.text)));
                    else left.push_back(n.id + ": layout " + ln);
                } catch (const std::exception&) { left.push_back(n.id + ": layout " + ln + " isn't a number"); }
            }
        } else {
            left.push_back(n.id + ": <" + c.name + "> -- Mortimer doesn't keep it");
        }
    }
    // Four equal sides are one "margin" (what export writes for it).
    if (margins.size() == 4 && margins.begin()->second == margins.rbegin()->second &&
        std::all_of(margins.begin(), margins.end(), [&](const auto& m) { return m.second == margins.begin()->second; })) {
        if (const auto* p = find_prop(*w, "margin")) set_value(n, *p, margins.begin()->second, n.id + ".margin");
    } else {
        for (const auto& [k, v] : margins)
            if (const auto* p = find_by_builder(*w, k, BuilderProp::How::Plain)) set_value(n, *p, v, n.id + "." + k);
    }
    for (const auto& [k, v] : paned_pack) {   // resize-start-child -> the start child's resize
        const bool start = k.find("-start-") != std::string::npos;
        for (auto& ch : n.children) {
            const auto s = ch.pack.find("slot");
            const bool is_start = s == ch.pack.end() || std::get<std::string>(s->second) != "end";
            if (is_start == start) ch.pack[k.rfind("resize-", 0) == 0 ? "resize" : "shrink"] = v;
        }
    }
    // An `always` prop at its default is what export writes for "not set".
    for (const auto* list : {&w->props, &common_props()})
        for (const auto& p : *list) {
            const auto it = n.props.find(p.name);
            if (!p.always || it == n.props.end()) continue;
            const std::string def = p.kind == PropKind::Bool ? (p.def == "true" ? "1" : "0") : p.def;
            std::string now;
            if (const auto* b = std::get_if<bool>(&it->second)) now = *b ? "1" : "0";
            else if (const auto* d = std::get_if<double>(&it->second)) now = std::to_string(*d);
            else if (const auto* i = std::get_if<std::int64_t>(&it->second)) now = std::to_string(*i);
            const std::string def_n = p.kind == PropKind::Double ? std::to_string(std::stod(def.empty() ? "0" : def)) : def;
            if (now == def_n) n.props.erase(it);
        }
    return n;
}

void Importer::add_child(Node& parent, const WidgetSpec& pw, const XmlNode& child_el, const std::string& type) {
    const XmlNode* o = nullptr;
    for (const auto& k : child_el.children) if (k.name == "object") { o = &k; break; }
    if (!o) return;
    auto S = [](const std::string& s) { return Value(s); };
    auto prop_of = [](const XmlNode& obj, const std::string& name) -> std::string {
        for (const auto& k : obj.children) if (k.name == "property" && k.attr("name") == name) return k.text;
        return {};
    };
    auto inner_child = [](const XmlNode& page) -> const XmlNode* {
        for (const auto& k : page.children)
            if (k.name == "property" && k.attr("name") == "child")
                for (const auto& kk : k.children) if (kk.name == "object") return &kk;
        return nullptr;
    };
    // Pages: the page object carries the pack, its "child" the widget.
    if (o->attr("class") == "GtkStackPage" || o->attr("class") == "GtkNotebookPage") {
        const XmlNode* w = inner_child(*o);
        if (!w) return;
        auto c = object(*w, false, &pw);
        if (!c) return;
        if (pw.children == ChildKind::Stack) {
            const std::string nm = prop_of(*o, "name");
            c->pack["name"] = S(nm.empty() ? c->id : nm);
            if (const auto t = prop_of(*o, "title"); !t.empty()) c->pack["title"] = S(t);
            if (const auto i = prop_of(*o, "icon-name"); !i.empty()) c->pack["icon_name"] = S(i);
        } else if (pw.children == ChildKind::Notebook) {
            if (const auto t = prop_of(*o, "tab-label"); !t.empty()) c->pack["title"] = S(t);
        } else {
            left.push_back(parent.id + ": a " + o->attr("class") + " in a " + parent.type);
            return;
        }
        parent.children.push_back(std::move(*c));
        return;
    }
    // A notebook's tab given as a widget after its page (<child type="tab">): its label is the title.
    if (type == "tab" && pw.children == ChildKind::Notebook) {
        if (!parent.children.empty()) {
            const std::string t = prop_of(*o, "label");
            if (!t.empty()) parent.children.back().pack["title"] = S(t);
            else left.push_back(parent.id + ": a tab that isn't a label");
        }
        return;
    }
    auto c = object(*o, false, &pw);
    if (!c) return;
    const std::string prop = type.rfind("property:", 0) == 0 ? type.substr(9) : "";
    switch (pw.children) {
        case ChildKind::Window:
            if (type == "titlebar" || prop == "titlebar") c->pack["slot"] = S("titlebar");
            break;
        case ChildKind::HeaderBar:
            c->pack["slot"] = S(prop == "title-widget" ? "title" : type == "end" ? "end" : "start");
            break;
        case ChildKind::Paned:
            c->pack["slot"] = S(prop == "end-child" ? "end" : "start");
            break;
        case ChildKind::Overlay:
            if (type == "overlay") c->pack["slot"] = S("overlay");
            break;
        case ChildKind::CenterBox:
            c->pack["slot"] = S(prop == "center-widget" ? "center" : prop == "end-widget" ? "end" : "start");
            break;
        case ChildKind::Stack:
            c->pack["name"] = S(c->id);
            break;
        default: break;
    }
    if (pw.children != ChildKind::Grid) {   // a <layout> is only a Grid's
        c->pack.erase("column"); c->pack.erase("row"); c->pack.erase("width"); c->pack.erase("height");
    }
    parent.children.push_back(std::move(*c));
}

// Re-point Ref props (a StackSidebar's stack) at the ids the form gave out.
void repoint(Node& n, const std::map<std::string, std::string>& renamed) {
    if (const WidgetSpec* w = find_widget(n.type))
        for (const auto& p : w->props)
            if (p.kind == PropKind::Ref)
                if (auto it = n.props.find(p.name); it != n.props.end())
                    if (const auto* s = std::get_if<std::string>(&it->second))
                        if (const auto r = renamed.find(*s); r != renamed.end()) it->second = r->second;
    for (auto& c : n.children) repoint(c, renamed);
}

}  // namespace

std::string form_name_from(const std::string& stem) {
    std::string out;
    bool up = true;
    for (char c : stem) {
        if (!std::isalnum(static_cast<unsigned char>(c))) { up = true; continue; }
        out += up ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
        up = false;
    }
    if (out.empty() || std::isdigit(static_cast<unsigned char>(out[0]))) out = "Imported" + out;
    return out;
}

UiImport import_ui(const std::string& xml, const std::string& form_name) {
    const XmlNode doc = parse_xml(xml);
    if (doc.name != "interface") throw std::runtime_error("not a GtkBuilder file: it starts with <" + doc.name + ">, not <interface>");
    Importer im;
    for (const auto& c : doc.children) {
        if (c.name != "menu") continue;
        StringList rows;
        std::function<void(const XmlNode&)> walk = [&](const XmlNode& m) {
            for (const auto& k : m.children) {
                if (k.name == "section") {
                    if (!rows.empty() && rows.back() != "-") rows.push_back("-");
                    walk(k);
                } else if (k.name == "item") {
                    std::string label, action;
                    for (const auto& a : k.children)
                        if (a.name == "attribute") {
                            if (a.attr("name") == "label") label = a.text;
                            if (a.attr("name") == "action") action = a.text;
                        }
                    rows.push_back(action.empty() ? label : label + "=" + action);
                } else if (k.name == "submenu") {
                    im.left.push_back("menu " + c.attr("id") + ": a submenu -- Mortimer's menus are one level");
                }
            }
        };
        walk(c);
        while (!rows.empty() && rows.back() == "-") rows.pop_back();
        im.menus[c.attr("id")] = rows;
    }
    UiImport r;
    r.form.name = form_name;
    const XmlNode* top = nullptr;
    for (const auto& c : doc.children)
        if (c.name == "template" || c.name == "object") {
            const std::string cls = c.name == "template" ? c.attr("parent") : c.attr("class");
            if (!top && (cls == "GtkWindow" || cls == "GtkApplicationWindow")) top = &c;
        }
    std::optional<Node> root;
    if (top) {
        root = im.object(*top, true);
    } else {
        // No window: the first widget becomes the body of a plain Window.
        for (const auto& c : doc.children)
            if (!root && (c.name == "object" || c.name == "template")) {
                auto body = im.object(c, false);
                if (!body) continue;
                im.left.push_back("no window in the file -- " + body->id + " put in a new Window");
                Node w{"Window", im.give_id(snake(form_name), "Window"), {{"title", Value(form_name)}}, {}, {}, {*body}};
                root = w;
            }
    }
    if (!root) throw std::runtime_error("nothing in the file Mortimer can make a form of");
    for (const auto& c : doc.children)
        if ((c.name == "object" || c.name == "template") && &c != top && c.attr("class") != "")
            if (top) im.left.push_back(c.attr("id") + ": another top-level " + c.attr("class") + " -- one window per form");
    repoint(*root, im.renamed);
    r.form.root = std::move(*root);
    // A menu row's win.<name> needs an action of the form: made here, as a hook
    // (on_action_<name>) -- what the .ui's own code did for it, yours to write.
    std::set<std::string> have;
    walk(r.form.root, [&](const Node& n, const Node*) {
        const auto m = n.props.find("menu");
        if (m == n.props.end() || !std::holds_alternative<StringList>(m->second)) return;
        for (const auto& row : std::get<StringList>(m->second)) {
            const auto e = parse_menu_entry(row);
            if (e.action.rfind("app.", 0) == 0 && have.insert(e.action).second)
                im.left.push_back("menu row \"" + e.label + "\": " + e.action + " is the app's own action -- add it in "
                                  "your App.cpp (the form makes win.* ones)");
            if (e.action.rfind("win.", 0) != 0) continue;
            std::string name = e.action.substr(4);
            for (auto& ch : name) if (ch == '-') ch = '_';
            if (!is_identifier(name) || is_cpp_keyword(name) || !have.insert(name).second) continue;
            r.form.actions.push_back({name, "", false, "", "", ""});
            im.left.push_back("menu row \"" + e.label + "\": " + e.action + " -> a new action " + name +
                              " (hook on_action_" + name + ")");
        }
    });
    r.notes = std::move(im.left);
    return r;
}

}  // namespace mortimer::core
