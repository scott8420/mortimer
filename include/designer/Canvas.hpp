#pragma once
// designer::Canvas -- the live canvas: the form, built from the Design as REAL
// gtkmm widgets, every time it changes.
//
// "Real widgets, hooks inert" (Scott, end of s001): what you see is what the
// generated build_ui() will make, because it is the same widgets with the same
// properties -- not a drawing of them. Nothing is connected (no hook can fire),
// a capture-phase click on the stage picks the widget under the pointer and
// selects it (the widget itself never sees the press), and the stage refuses
// focus for its whole subtree, so typing never lands in a designed Entry.
//
// How a property reaches a widget (apply()): most catalog properties ARE the
// GObject property of the same name ("label", "spacing", "halign" ...), set
// through the property system with the JSON value converted by the property's
// own type (an enum by its nick). The few that aren't -- margin (four sides),
// tooltip, a range's adjustment, a DropDown's items, css classes, a Ref -- are
// spelled out. A property neither path can apply is logged once by name: a
// catalog entry the canvas can't show is a bug to fix, not a silence.
//
// The form's window can't be put inside another window, so it is drawn as a
// card: its titlebar slot on top (or a plain title strip with the window's
// title), its child below, sized by default_width / default_height.
#include "core/Design.hpp"
#include "widgets/Widgets.hpp"

#include <gtkmm/stack.h>
#include <map>
#include <set>
#include <string>

namespace mortimer::designer {

class Canvas : public widgets::ScrolledWindow {
public:
    Canvas();

    // Rebuild from the design and outline `sel`. A Stack shows the page that
    // holds the selection (else the page it showed before the rebuild).
    void show(const core::Design& d, const std::string& sel);

    // A click on the stage picked a widget: its design key.
    sigc::signal<void(const std::string&)>& signal_pick() { return m_pick; }

    // Something dragged from the palette or the tree was dropped on a widget:
    // (payload, parent, index), resolved by core::design::landing_on.
    sigc::signal<void(const std::string&, const std::string&, int)>& signal_drop() { return m_drop; }
    sigc::signal<void(const std::string&)>& signal_refused() { return m_refused; }

    // Every "<Type>.<prop>" the canvas could not apply since it was made.
    const std::set<std::string>& unapplied() const { return m_unapplied; }

private:
    Gtk::Widget* build(const core::Design& d, const std::string& key);
    Gtk::Widget* make(const core::Rec& r);
    void apply(Gtk::Widget& w, const core::Rec& r);
    bool apply_one(Gtk::Widget& w, const core::Rec& r, const std::string& prop, const core::Value& v);
    void place(Gtk::Widget& parent, const core::Rec& prec, Gtk::Widget& child, const core::Rec& crec);
    Gtk::Widget* frame_for(const core::Design& d, const std::string& root);
    void on_press(int n, double x, double y);
    std::string key_at(double x, double y) const;   // the design key under a stage point
    void set_drop_mark(const std::string& key);

    const core::Design* m_d = nullptr;
    Gtk::Widget*        m_drop_marked = nullptr;
    sigc::signal<void(const std::string&, const std::string&, int)> m_drop;
    sigc::signal<void(const std::string&)> m_refused;

    widgets::Box m_stage;        // centres the card; owns the capture gesture
    Gtk::Widget* m_card = nullptr;
    std::map<Gtk::Widget*, std::string> m_keys;      // built widget -> design key
    std::map<std::string, Gtk::Widget*> m_widgets;   // design key -> built widget
    std::map<std::string, std::string>  m_page;      // Stack key -> page name it showed
    std::vector<std::pair<Gtk::Widget*, std::string>> m_refs;   // (StackSidebar, stack name)
    std::set<std::string> m_unapplied;
    sigc::signal<void(const std::string&)> m_pick;
};

}  // namespace mortimer::designer
