#include "core/Shortcuts.hpp"

#include <algorithm>
#include <cctype>
#include <map>

namespace mortimer::core {

// ─────────────────────────────────────────────────────────────────────────────
// Accel formatting
// ─────────────────────────────────────────────────────────────────────────────
namespace {

std::string lower(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (char c : s) o += char(std::tolower((unsigned char)c));
    return o;
}

// GTK key name -> display glyph. Single alpha keys uppercase; named keys map to a
// glyph where one reads better, otherwise the name is prettified.
std::string map_key(const std::string& key) {
    if (key.empty()) return "";
    static const std::map<std::string, std::string> named = {
        {"comma", ","},     {"period", "."},     {"question", "?"},
        {"slash", "/"},     {"minus", "\u2212"}, {"plus", "+"},
        {"equal", "="},     {"space", "Space"},
        {"Return", "Enter"},{"Escape", "Esc"},
        {"Left", "\u2190"}, {"Right", "\u2192"}, {"Up", "\u2191"}, {"Down", "\u2193"},
        {"Delete", "Del"},  {"BackSpace", "Backspace"},
        {"Page_Up", "Page Up"}, {"Page_Down", "Page Down"},
        {"KP_Add", "Keypad +"}, {"KP_Subtract", "Keypad \u2212"}, {"KP_0", "Keypad 0"},
    };
    auto it = named.find(key);
    if (it != named.end()) return it->second;
    if (key.size() == 1) {
        std::string s = key;
        s[0] = char(std::toupper((unsigned char)s[0]));
        return s;
    }
    std::string s = key;
    for (char& c : s)
        if (c == '_') c = ' ';
    return s;
}

// Split an accel into its modifier flags + bare key name. Shared by
// format_accel and any future consumer so they can't disagree.
struct Parsed { bool ctrl = false, alt = false, shift = false, super = false; std::string key; };

Parsed parse_accel(const std::string& accel) {
    Parsed p;
    std::size_t i = 0;
    while (i < accel.size()) {
        if (accel[i] == '<') {
            std::size_t j = accel.find('>', i);
            if (j == std::string::npos) break;
            const std::string ml = lower(accel.substr(i + 1, j - i - 1));
            if (ml == "ctrl" || ml == "control" || ml == "primary") p.ctrl = true;
            else if (ml == "alt" || ml == "mod1")                   p.alt = true;
            else if (ml == "shift")                                 p.shift = true;
            else if (ml == "super" || ml == "meta" || ml == "mod4") p.super = true;
            i = j + 1;
        } else {
            p.key = accel.substr(i);
            break;
        }
    }
    return p;
}

}  // namespace

std::string format_accel(const std::string& accel) {
    const Parsed p = parse_accel(accel);
    const std::string k = map_key(p.key);
    std::string out;
    auto add = [&](const std::string& s) {
        if (!out.empty()) out += "+";
        out += s;
    };
    if (p.ctrl)  add("Ctrl");
    if (p.alt)   add("Alt");
    if (p.shift) add("Shift");
    if (p.super) add("Super");
    if (!k.empty()) add(k);
    return out;
}

std::string ShortcutSpec::display_keys() const {
    if (!keys.empty()) return keys;
    std::string out;
    for (const auto& a : accels) {
        if (!out.empty()) out += "  /  ";
        out += format_accel(a);
    }
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// The registry  (authored section [A-Z] -> row)
// ─────────────────────────────────────────────────────────────────────────────
const std::vector<ShortcutSpec>& shortcut_registry() {
    // Fields: {section, action, accels, keys, description}
    // action non-empty => wired via set_accels_for_action. keys empty => derived.
    // Deliberately small -- a seed proves the SHAPE, not a product's key set. It
    // carries one doc-only row so that half of the paradigm is exercised too.
    static const std::vector<ShortcutSpec> kReg = {
        // ── Diagnostics ───────────────────────────────────────────────────────
        // s020: was Ctrl+D -- Duplicate has it now (the designer's usual key).
        {"Diagnostics", "win.dump-registry", {"<Ctrl><Shift>d"}, "",
         "Dump the widget registry to the log"},

        // ── Edit ──────────────────────────────────────────────────────────────
        // Doc-only ON PURPOSE: an accelerator would take these keys from every
        // text box in the inspector. The window hears them instead
        // (Shell_bindings.cpp, bind_keys).
        {"Edit", "", {}, "Ctrl+Z", "Undo -- a widget edit or a project change, whichever came last (a text box keeps its own)"},
        {"Edit", "", {}, "Ctrl+Shift+Z  /  Ctrl+Y", "Redo"},
        {"Edit", "", {}, "Delete  /  Backspace", "Delete the selected widget (and what it holds)"},
        {"Edit", "", {}, "Ctrl+C  /  Ctrl+X", "Copy / cut the selected widgets (and what they hold)"},
        {"Edit", "", {}, "Ctrl+V", "Paste -- into the selection if it takes it, else after it"},
        {"Edit", "", {}, "Ctrl+D", "Duplicate the selected widgets, right after them"},
        {"Edit", "", {}, "Alt+\u2191  /  Alt+\u2193", "Move the selected widget up / down among its siblings"},

        // ── File ──────────────────────────────────────────────────────────────
        {"File", "win.new",     {"<Ctrl>n"}, "", "New scratch form (not in a project)"},
        {"File", "win.open",    {"<Ctrl>o"}, "", "Open a form"},
        {"File", "win.save",    {"<Ctrl>s"}, "", "Save"},
        {"File", "win.save-as", {"<Ctrl><Shift>s"}, "", "Save as"},
        {"File", "win.new-project", {"<Ctrl><Shift>n"}, "", "New project (a whole app workspace)"},

        // ── General ───────────────────────────────────────────────────────────
        {"General", "win.shortcuts",     {"<Ctrl>question", "<Ctrl>slash"}, "",
         "Keyboard shortcuts (this window)"},
        {"General", "win.help",          {"F1"}, "", "Help: the user guide, searchable"},
        {"General", "win.about",         {}, "", "About Mortimer"},
        {"General", "win.quit",          {"<Ctrl>q"}, "", "Quit"},

        // ── Mouse ─────────────────────────────────────────────────────────────
        {"Mouse", "", {}, "Click on the canvas", "Select the widget under the pointer (it never sees the click)"},
        {"Mouse", "", {}, "Click in the palette", "Add that widget into the selection, or just after it"},
        {"Mouse", "", {}, "Drag from the palette", "Drop on a tree row (top / middle / bottom = before / into / after) or on the canvas"},
        {"Mouse", "", {}, "Drag a tree row", "Reorder, or reparent into another container"},
        {"Mouse", "", {}, "Right-click the project list", "A row's Open / Copy, Rename…, Remove… -- and Add a form or a resource"},

        // ── Project list ──────────────────────────────────────────────────────
        // s025 m1. Doc-only: the LIST hears these, and only while it has the
        // focus (ProjectPane.cpp) -- elsewhere Delete is the canvas's.
        {"Project list", "", {}, "Delete  /  Backspace", "Remove the selected form or resource (asks first; the file goes to removed/)"},
        {"Project list", "", {}, "F2", "Rename the selected form or resource"},
        {"Project list", "", {}, "Menu  /  Shift+F10", "The selected row's menu, as a right-click"},

    };
    return kReg;
}

// ─────────────────────────────────────────────────────────────────────────────
// Collision detection
// ─────────────────────────────────────────────────────────────────────────────
std::vector<std::string> find_accel_collisions() {
    std::map<std::string, std::pair<int, bool>> seen;  // accel -> {count, any_action}
    for (const auto& s : shortcut_registry())
        for (const auto& a : s.accels) {
            auto& e = seen[a];
            e.first += 1;
            if (!s.action.empty()) e.second = true;
        }
    std::vector<std::string> out;
    for (const auto& [accel, e] : seen)
        if (e.first >= 2 && e.second) out.push_back(accel);  // std::map keeps it sorted+unique
    return out;
}

}  // namespace mortimer::core
