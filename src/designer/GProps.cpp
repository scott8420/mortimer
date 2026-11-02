#include "designer/GProps.hpp"

#include <algorithm>
#include <map>

namespace mortimer::designer::gprops {

namespace {
std::string dashed(std::string s) {
    std::replace(s.begin(), s.end(), '_', '-');
    return s;
}
std::string undashed(std::string s) {
    std::replace(s.begin(), s.end(), '-', '_');
    return s;
}
double dbl(const core::Value& v) {
    if (std::holds_alternative<double>(v)) return std::get<double>(v);
    if (std::holds_alternative<std::int64_t>(v)) return static_cast<double>(std::get<std::int64_t>(v));
    return 0;
}
bool numeric(const core::Value& v) {
    return std::holds_alternative<double>(v) || std::holds_alternative<std::int64_t>(v);
}
GParamSpec* find(GObjectClass* klass, const std::string& catalog_prop) {
    const std::string n = gobject_name(catalog_prop);
    return n.empty() ? nullptr : g_object_class_find_property(klass, n.c_str());
}
}  // namespace

std::string gobject_name(const std::string& p) {
    static const std::map<std::string, std::string> renamed = {
        {"tooltip", "tooltip-text"},         {"placeholder", "placeholder-text"},
        {"transition", "transition-type"},   {"hscrollbar", "hscrollbar-policy"},
        {"vscrollbar", "vscrollbar-policy"},
        {"margin", ""}, {"lower", ""}, {"upper", ""}, {"step", ""}, {"page", ""}, {"value", ""},
        {"items", ""},  {"css_classes", ""}, {"stack", ""}, {"menu", ""}, {"model", ""},
    };
    if (const auto it = renamed.find(p); it != renamed.end()) return it->second;
    return dashed(p);
}

bool set(Gtk::Widget& w, const std::string& catalog_prop, const core::Value& v) {
    GObject* o = G_OBJECT(w.gobj());
    GParamSpec* ps = find(G_OBJECT_GET_CLASS(o), catalog_prop);
    if (!ps || !(ps->flags & G_PARAM_WRITABLE)) return false;
    const GType t = G_PARAM_SPEC_VALUE_TYPE(ps);
    GValue gv = G_VALUE_INIT;
    g_value_init(&gv, t);
    bool ok = true;
    if (t == G_TYPE_BOOLEAN && std::holds_alternative<bool>(v))  g_value_set_boolean(&gv, std::get<bool>(v));
    else if (t == G_TYPE_INT && numeric(v))                      g_value_set_int(&gv, static_cast<int>(dbl(v)));
    else if (t == G_TYPE_UINT && numeric(v))                     g_value_set_uint(&gv, static_cast<guint>(std::max(0.0, dbl(v))));
    else if (t == G_TYPE_DOUBLE && numeric(v))                   g_value_set_double(&gv, dbl(v));
    else if (t == G_TYPE_FLOAT && numeric(v))                    g_value_set_float(&gv, static_cast<float>(dbl(v)));
    else if (t == G_TYPE_STRING && std::holds_alternative<std::string>(v))
        g_value_set_string(&gv, std::get<std::string>(v).c_str());
    else if (G_TYPE_IS_ENUM(t) && std::holds_alternative<std::string>(v)) {
        auto* ec = static_cast<GEnumClass*>(g_type_class_ref(t));
        const GEnumValue* ev = g_enum_get_value_by_nick(ec, dashed(std::get<std::string>(v)).c_str());
        if (ev) g_value_set_enum(&gv, ev->value);
        else ok = false;
        g_type_class_unref(ec);
    } else ok = false;
    if (ok) g_object_set_property(o, ps->name, &gv);
    g_value_unset(&gv);
    return ok;
}

std::optional<core::Value> default_of(const std::string& type, const std::string& catalog_prop) {
    const GType gt = g_type_from_name(("Gtk" + type).c_str());
    if (!gt) return std::nullopt;
    auto* klass = static_cast<GObjectClass*>(g_type_class_ref(gt));
    GParamSpec* ps = find(klass, catalog_prop);
    std::optional<core::Value> out;
    if (ps) {
        const GValue* gv = g_param_spec_get_default_value(ps);
        const GType t = G_PARAM_SPEC_VALUE_TYPE(ps);
        if (t == G_TYPE_BOOLEAN)     out = core::Value(static_cast<bool>(g_value_get_boolean(gv)));
        else if (t == G_TYPE_INT)    out = core::Value(static_cast<std::int64_t>(g_value_get_int(gv)));
        else if (t == G_TYPE_UINT)   out = core::Value(static_cast<std::int64_t>(g_value_get_uint(gv)));
        else if (t == G_TYPE_DOUBLE) out = core::Value(g_value_get_double(gv));
        else if (t == G_TYPE_FLOAT)  out = core::Value(static_cast<double>(g_value_get_float(gv)));
        else if (t == G_TYPE_STRING) {
            const char* s = g_value_get_string(gv);
            out = core::Value(std::string(s ? s : ""));
        } else if (G_TYPE_IS_ENUM(t)) {
            auto* ec = static_cast<GEnumClass*>(g_type_class_ref(t));
            if (const GEnumValue* ev = g_enum_get_value(ec, g_value_get_enum(gv)))
                out = core::Value(undashed(ev->value_nick));
            g_type_class_unref(ec);
        }
    }
    g_type_class_unref(klass);
    return out;
}

}  // namespace mortimer::designer::gprops
