#pragma once
// HelpWindow -- the user guide, in the designer (s024 m6).
//
// It paints core::help_pages() -- the repo's help/*.md, compiled in -- so this
// window, `mortimer-gen help` and GitHub show ONE text. Pages on the left, a
// search over all of them on top (the pages that mention every word, best
// first; the words marked in the page, the view on the first), the page on the
// right. F1, or the menu's Help.
//
// Hide-on-close singleton (CANON: "Lifetime shape is design"), like
// ShortcutsDialog: Shell builds it once and presents it again.
#include <gtkmm/box.h>
#include <gtkmm/headerbar.h>
#include <gtkmm/label.h>
#include <gtkmm/listbox.h>
#include <gtkmm/paned.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/searchentry.h>
#include <gtkmm/textbuffer.h>
#include <gtkmm/textview.h>
#include <gtkmm/window.h>

#include <cstddef>
#include <string>
#include <vector>

namespace mortimer {

class HelpWindow : public Gtk::Window {
public:
    HelpWindow();
    ~HelpWindow() override;
    void show(Gtk::Window& parent);
    void show_page(std::size_t page);   // by index in core::help_pages()

private:
    void on_search_changed();
    void paint(std::size_t page);        // the page's blocks into the buffer
    void mark_words();                   // the search's words, tagged; the view on the first
    void insert_spans(Gtk::TextBuffer::iterator& at, const std::string& line,
                      const std::vector<Glib::RefPtr<Gtk::TextTag>>& extra = {});

    Gtk::HeaderBar      m_header;
    Gtk::Label          m_title;
    Gtk::Paned          m_split{Gtk::Orientation::HORIZONTAL};
    Gtk::Box            m_side{Gtk::Orientation::VERTICAL};
    Gtk::SearchEntry    m_search;
    Gtk::ScrolledWindow m_list_scroll;
    Gtk::ListBox        m_list;
    Gtk::ScrolledWindow m_page_scroll;
    Gtk::TextView       m_page;
    std::vector<std::size_t> m_shown;    // list row -> page index
    std::vector<std::string> m_words;    // the search, lower-cased
    std::size_t m_current = 0;
    bool m_filling = false;              // the list is being refilled: a selection is not a pick
};

}  // namespace mortimer
