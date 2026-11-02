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

#include <gtkmm/button.h>
#include <gtkmm/grid.h>
#include <gtkmm/label.h>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace mortimer::designer {

class Inspector : public widgets::ScrolledWindow {
public:
    Inspector(core::Design& d, core::DesignJournal& j);
    // true when it REBUILT (its widgets were replaced, the keyboard with them).
    bool show(const std::string& sel);
    // s010: the forms an action of this form may open (the project's, less this
    // one and the main window). Shown in each action's "opens" dropdown.
    void set_targets(std::vector<std::string> forms) { m_targets = std::move(forms); }
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
    void actions_rows();                                      // s003: the form's actions
    void files_rows();                                        // s005: the form's file dialogs
    void models_rows();                                       // s006: the form's models
    void menu_rows(Gtk::Label* label, Gtk::Button* reset);    // s003: a MenuButton's menu
    void editable_rows(Gtk::Label* label, Gtk::Button* reset);// s007: a ColumnView's editable fields
    void no_wheel_unless_focused(Gtk::Widget& w);
    struct RowModel { bool in_row = false; std::optional<core::ModelDef> model; };
    RowModel row_model() const;                               // s011: the selection inside a ListView's row?
    void commit_prop(const std::string& prop, bool pack, const std::optional<core::Value>& v);

    core::Design&        m_d;
    core::DesignJournal& m_j;
    widgets::Box         m_page;
    Gtk::Grid*           m_grid = nullptr;
    int                  m_line = 0;
    std::vector<Row>     m_rows;
    std::string          m_key, m_type, m_parent_type;
    std::vector<std::string> m_targets;  // s010: see set_targets
    std::string          m_actions_sig;   // the form's action + file dialog names, as last built
    bool                 m_painting = false;
    bool                 m_rebuilding = false;
    sigc::signal<void(const std::string&)> m_said;
};

}  // namespace mortimer::designer
