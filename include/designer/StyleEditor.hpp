#pragma once
// designer::StyleEditor -- s012 m2: the app's stylesheet (resources/<bin>.css)
// edited in the window, the canvas wearing it as you type.
//
//   [Revert]        • lister.css                      [Save]
//   ┌─────────────────────────────────────────────────────┐
//   │ label { color: #c00000; }                            │   monospace; GTK's own
//   │ button { colr: red; }   <- the error line, wavy      │   undo (Ctrl+Z) in the text
//   └─────────────────────────────────────────────────────┘
//   lister.css line 2: No property named "colr"
//
// Typing previews on the canvas a beat after the last key (Canvas::
// preview_stylesheet) -- the file isn't touched until Save (Ctrl+S). Close
// with unsaved changes asks; Discard puts the canvas back on the file. The
// file is the truth: it is not a form, so the design journal doesn't hold it
// (like the navigator's resources) -- the text box's own undo covers typing.
//
// Hide-on-close singleton (the dialog-lifetime stone): the Shell builds it
// once and re-presents it.
#include "designer/Canvas.hpp"

#include <gtkmm/button.h>
#include <gtkmm/headerbar.h>
#include <gtkmm/label.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/textview.h>
#include <gtkmm/window.h>

#include <string>

namespace mortimer::designer {

class StyleEditor : public Gtk::Window {
public:
    explicit StyleEditor(Canvas& canvas);
    ~StyleEditor() override;
    // Show `path` over `parent`. A different file, or the same one unchanged,
    // is (re)read; unsaved typing in the same file is kept.
    void open(const std::string& path, Gtk::Window& parent);
    bool dirty() const;

private:
    void load();                 // the file -> the text box (and the saved copy)
    void changed();              // typing: dirty mark, preview a beat later
    void preview();              // the text box -> the canvas (not the file)
    void save();
    void revert();
    void show_error();           // the canvas's first parse error: status + wavy line
    void update_title();
    bool on_close_request() override;

    Canvas&                       m_canvas;
    std::string                   m_path;
    std::string                   m_saved;      // the file's text as last read / written
    Gtk::HeaderBar                m_bar;
    Gtk::Label                    m_title;
    Gtk::Button                   m_save{"Save"};
    Gtk::Button                   m_revert{"Revert"};
    Gtk::ScrolledWindow           m_scroll;
    Gtk::TextView                 m_text;
    Gtk::Label                    m_status;
    Glib::RefPtr<Gtk::TextTag>    m_error_tag;
    sigc::connection              m_later;
    bool                          m_loading = false;
};

}  // namespace mortimer::designer
