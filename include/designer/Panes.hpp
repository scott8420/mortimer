#pragma once
#include "designer/WidgetCard.hpp"
#include <giomm/simpleactiongroup.h>
#include <gtkmm/popovermenu.h>
// designer::Palette and designer::TreePane -- the two left-hand panes.
//
// Both are thin views over the Design: repaint(design, sel) rebuilds them from
// the model (Pole B of docs/tree-and-dnd.md -- a form is tens of widgets, so a
// full rebuild is cheap and there is no incremental update to get wrong), and
// what the user does comes out as a signal the Shell routes to a verb. Neither
// writes the model itself.
#include "core/Design.hpp"
#include "widgets/Widgets.hpp"

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace mortimer::designer {

// Where a palette click puts the new widget: INTO the selection if it is a
// container that can take it, else just AFTER the selection in its parent.
struct Drop {
    std::string parent;   // "" = nowhere it can go
    int         index = -1;
    std::string why;      // when parent is "": the reason, in words
};
Drop drop_for(const core::Design& d, const std::string& sel, const std::string& type);

// ── drag and drop ──────────────────────────────────────────────────────────
// A drag carries one string: "add:<Type>" from the palette, "move:<key>" from
// the tree. Where it lands is (parent, index), index counted among the
// parent's children WITHOUT the widget being moved (core::Design::pos_at).
struct Payload {
    bool        add = false;
    std::string what;     // the type (add) or the key (move)
};
std::optional<Payload> parse_payload(const std::string& s);
// Where it lands, and whether it may: core::design::landing (tested headless).

// A drop, already resolved: what, into which parent, at which index.
using DropSignal = sigc::signal<void(const std::string& payload, const std::string& parent, int index)>;

// s025 (Scott: "just clicking an item to create it is wrong"): a single click
// only PICKS a type (it wears the highlight); a widget is made by a
// double-click, Enter / Space on the focused type, the right-click menu, or
// a drag onto the tree / canvas. Alt+click a type (greyed or not): its card --
// a live picture, what it's for, More... for GTK's pages (WidgetCard.hpp).
class Palette : public widgets::Box {
public:
    Palette();
    ~Palette() override;
    // Grey what can't go at the selection; the tooltip says why.
    void repaint(const core::Design& d, const std::string& sel);
    sigc::signal<void(const std::string&)>& signal_add() { return m_add; }

private:
    void pick(const std::string& type);                      // the highlight, no widget made
    void popup_menu(const std::string& type, double x, double y);   // x, y in the pane's coordinates

    widgets::ScrolledWindow m_scroll;
    widgets::Box            m_list;
    std::map<std::string, Gtk::Button*> m_buttons;   // type -> its button
    std::map<std::string, std::string>  m_docs;      // type -> catalog doc
    std::map<std::string, std::string>  m_where;     // type -> "into “body”" / "after “title”" (repaint)
    std::string             m_picked;                // the highlighted type
    std::string             m_menu_type;             // the type the open menu is about
    Glib::RefPtr<Gio::SimpleActionGroup> m_actions;  // "palette.add"
    Gtk::PopoverMenu        m_menu;                  // parented to the pane (unparented in the destructor)
    WidgetCard              m_card;                  // s025: Alt+click -- the picture and the words (same)
    sigc::signal<void(const std::string&)> m_add;
};

class TreePane : public widgets::Box {
public:
    TreePane();
    ~TreePane() override;
    // `also`: the rest of a several-at-once selection (s018), marked beside `sel`.
    void repaint(const core::Design& d, const std::string& sel, const std::vector<std::string>& also = {});
    sigc::signal<void(const std::string&)>& signal_select() { return m_select; }
    // s018: a Ctrl (1) / Shift (2) click on a row -- the shell grows the selection.
    sigc::signal<void(const std::string&, int)>& signal_select_more() { return m_select_more; }
    DropSignal& signal_drop() { return m_drop; }
    // A refused drop says why (the status line).
    sigc::signal<void(const std::string&)>& signal_refused() { return m_refused; }
    // Give the keyboard to the selected row (after a verb removed the focused one).
    void focus_selection();

private:
    void add_rows(const core::Design& d, const std::string& key, int depth);
    void make_draggable(Gtk::Widget& row, const std::string& key);
    void make_drop_target(Gtk::Widget& row, const std::string& key);
    // Resolve a hover on `key`'s row to (parent, index), or "" parent if refused.
    core::design::Landing resolve(const Payload& p, const std::string& key, double y, int height) const;
    void clear_marks();

    const core::Design*      m_d = nullptr;
    Gtk::Widget*             m_marked = nullptr;   // the row wearing a drop-* class
    widgets::ScrolledWindow  m_scroll;
    widgets::ListBox         m_list;
    std::vector<std::string> m_row_keys;   // row index -> design key
    std::set<std::string>    m_collapsed;  // survives the rebuild (tree-and-dnd.md)
    std::string              m_sel;
    bool                     m_painting = false;
    sigc::signal<void(const std::string&)> m_select;
    sigc::signal<void(const std::string&, int)> m_select_more;
    std::vector<std::string> m_also;
    sigc::signal<void(const std::string&)> m_refused;
    DropSignal m_drop;

    // s025 m5: a row's right-click menu (designer/WidgetMenu.hpp) and its card.
    void popup_menu(const std::string& key, double x, double y);   // x, y in the pane
    Gtk::PopoverMenu                     m_menu;
    WidgetCard                           m_wcard;
    Glib::RefPtr<Gio::SimpleActionGroup> m_menu_actions;   // "tree.card"
    std::string                          m_menu_type;
    Gdk::Rectangle                       m_menu_at;
};

}  // namespace mortimer::designer
