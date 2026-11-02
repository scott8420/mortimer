#pragma once
// designer::Inspector -- the right-hand pane: the selected widget's name,
// properties, packing and signals, every row an editor that writes through
// the design verbs (core::design::), so every edit is an undoable step.
//
// In-your-face (CANON): a property you have SET reads bold with a reset
// button beside it; an unset one reads dim and shows GTK's own default (read
// from the GObject property itself -- designer/GProps), so the row never
// lies about what the generated window will do.
//
// Two speeds of repaint, for the reason jot learned: rebuilding the rows on
// every keystroke would destroy the Entry being typed in. So show() REBUILDS
// only when the selection, its type, or its parent's kind changed; otherwise it
// REFRESHES the editors in place under m_painting (a refresh is not a user
// choice, so it must not write).
#include "core/Design.hpp"
#include "widgets/Widgets.hpp"

#include <gtkmm/grid.h>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace mortimer::designer {

class Inspector : public widgets::ScrolledWindow {
public:
    Inspector(core::Design& d, core::DesignJournal& j);
    void show(const std::string& sel);
    // A refusal or a note for the status line ("\"class\" isn't a C++ name").
    sigc::signal<void(const std::string&)>& signal_said() { return m_said; }

private:
    struct Row {
        std::function<void(const core::Rec&)> paint;   // set the editor from the record
    };
    void rebuild();
    void refresh();
    void section(const std::string& title, const std::string& doc = "");
    void prop_row(const std::string& type, const std::string& prop, bool pack);
    void signal_row(const std::string& type, const std::string& signal);
    void name_row(bool form);
    void no_wheel_unless_focused(Gtk::Widget& w);
    void commit_prop(const std::string& prop, bool pack, const std::optional<core::Value>& v);

    core::Design&        m_d;
    core::DesignJournal& m_j;
    widgets::Box         m_page;
    Gtk::Grid*           m_grid = nullptr;
    int                  m_line = 0;
    std::vector<Row>     m_rows;
    std::string          m_key, m_type, m_parent_type;
    bool                 m_painting = false;
    bool                 m_rebuilding = false;
    sigc::signal<void(const std::string&)> m_said;
};

}  // namespace mortimer::designer
